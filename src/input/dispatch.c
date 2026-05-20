#include "input/dispatch.h"

#include "config/common.h"
#include "config/dap_config.h"
#include "config/keymap.h"
#include "debug/dap.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "language/autocomplete.h"
#include "language/lsp.h"
#include "language/syntax_worker.h"
#include "render/popup.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "support/terminal.h"
#include "terminal/terminal_pane.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/task.h"
#include "workspace/workspace_state.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*** Input ***/

enum {
	DRAWER_DOUBLE_CLICK_THRESHOLD_MS = 400,
	TEXT_MULTI_CLICK_THRESHOLD_MS = 400,
	DRAWER_RESIZE_STEP = 1,
	KEYBOARD_SCROLL_COLS = 3
};

enum dispatchKeypressEffect {
	DISPATCH_KEYPRESS_EFFECT_NONE = 0,
	DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL = 1 << 0,
	DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT = 1 << 1
};

static void dispatchGoToDefinition(void);
static void dispatchMoveCursor(int k);
static void dispatchMoveCurrentLine(int direction);

static int dispatchIsWordByte(unsigned char b) {
	return isalnum(b) || b == '_' || b >= 0x80;
}

static int dispatchSetCursorFromOffset(size_t offset) {
	int cy = 0;
	int cx = 0;
	size_t normalized_offset = 0;

	if (!editorBufferOffsetToPos(offset, &cy, &cx)) {
		return 0;
	}
	if (cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
			cx = editorBytesClampCxToClusterBoundary(line.data, line.size, cx);
			editorLineViewRelease(&line);
		}
	} else {
		cx = 0;
	}
	if (!editorBufferPosToOffset(cy, cx, &normalized_offset)) {
		return 0;
	}
	E.cursor_offset = normalized_offset;
	E.cy = cy;
	E.cx = cx;
	return 1;
}

static int dispatchSetCursorFromPosition(int cy, int cx) {
	size_t offset = 0;

	if (cy < 0) {
		cy = 0;
	}
	if (cy > E.numrows) {
		cy = E.numrows;
	}
	if (cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
			cx = editorBytesClampCxToClusterBoundary(line.data, line.size, cx);
			editorLineViewRelease(&line);
		}
	} else {
		cx = 0;
	}
	if (!editorBufferPosToOffset(cy, cx, &offset)) {
		return 0;
	}
	return dispatchSetCursorFromOffset(offset);
}

static void dispatchAlignCursorWithRowEnd(void) {
	if (dispatchSetCursorFromPosition(E.cy, E.cx)) {
		return;
	}

	int rowlen = 0;
	if (E.numrows > E.cy) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, E.cy, &line)) {
			rowlen = line.size;
			// Never leave the cursor in the middle of a UTF-8 grapheme.
			E.cx = editorBytesClampCxToClusterBoundary(line.data, line.size, E.cx);
			editorLineViewRelease(&line);
		}
	}
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
	if (!editorBufferPosToOffset(E.cy, E.cx, &E.cursor_offset)) {
		E.cursor_offset = 0;
	}
}

static void dispatchClearActiveSearchMatch(void) {
	E.search_match_offset = 0;
	E.search_match_len = 0;
}

static void dispatchClearSearchState(void) {
	free(E.search_query);
	E.search_query = NULL;
	E.search_direction = 1;
	dispatchClearActiveSearchMatch();
}

static int dispatchSearchMatchPosition(int *row_out, int *col_out) {
	if (E.search_match_len <= 0 || row_out == NULL || col_out == NULL) {
		return 0;
	}
	return editorBufferOffsetToPos(E.search_match_offset, row_out, col_out);
}

static void dispatchClearSelectionMode(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	editorColumnSelectionClear();
}

static void dispatchCtrlClickGoToDefinitionAction(void) {
	editorHistoryBreakGroup();
	dispatchGoToDefinition();
}

static void dispatchPinActivePreviewForEdit(void) {
	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
		editorTabPinActivePreview();
	}
}

static void dispatchMoveLineUpAction(void) {
	dispatchMoveCurrentLine(-1);
}

static void dispatchMoveLineDownAction(void) {
	dispatchMoveCurrentLine(1);
}

static void dispatchDeleteCharAction(void) {
	struct editorSelectionRange range;
	if (E.column_select_active) {
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		editorColumnSelectionDeleteForward();
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		return;
	}
	if (editorGetSelectionRange(&range)) {
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		editorDeleteRange(&range);
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		dispatchClearSelectionMode();
		return;
	}
	dispatchClearSelectionMode();
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	// DEL deletes under cursor; editorDelChar() implements backspace semantics.
	dispatchMoveCursor(ARROW_RIGHT);
	editorDelChar();
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
}

static void dispatchBackspaceAction(void) {
	struct editorSelectionRange range;
	if (E.column_select_active) {
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		editorColumnSelectionBackspace();
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		return;
	}
	if (editorGetSelectionRange(&range)) {
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		editorDeleteRange(&range);
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		dispatchClearSelectionMode();
		return;
	}
	dispatchClearSelectionMode();
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	editorDelChar();
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
}

static int dispatchActionMutatesReadOnlyBuffer(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_NEWLINE:
		case EDITOR_ACTION_DELETE_CHAR:
		case EDITOR_ACTION_BACKSPACE:
		case EDITOR_ACTION_PASTE:
		case EDITOR_ACTION_ESLINT_FIX:
		case EDITOR_ACTION_CUT_SELECTION:
		case EDITOR_ACTION_DELETE_SELECTION:
		case EDITOR_ACTION_UNDO:
		case EDITOR_ACTION_REDO:
		case EDITOR_ACTION_FIND_REPLACE:
			return 1;
		default:
			return 0;
	}
}

static void dispatchToggleSelectionMode(void) {
	editorEditToggleSelectionMode(dispatchClearSelectionMode, dispatchAlignCursorWithRowEnd);
}

static void dispatchCopySelection(void) {
	editorEditCopySelection(dispatchClearSelectionMode);
}

static void dispatchCutSelection(void) {
	editorEditCutSelection(dispatchClearSelectionMode);
}

static void dispatchToggleCommentLines(void) {
	editorEditToggleCommentLines(dispatchClearSelectionMode, dispatchPinActivePreviewForEdit);
}

static void dispatchMoveCurrentLine(int direction) {
	editorEditMoveCurrentLine(direction);
}

static int dispatchReplaceSelectionWithChar(int c) {
	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		return 0;
	}

	size_t start_offset = 0, end_offset = 0;
	if (!editorBufferPosToOffset(range.start_cy, range.start_cx, &start_offset) ||
	    !editorBufferPosToOffset(range.end_cy, range.end_cx, &end_offset) ||
	    end_offset < start_offset) {
		return 1;
	}

	char inserted = (char)c;
	struct editorDocumentEdit edit = {
	        .kind = EDITOR_EDIT_INSERT_TEXT,
	        .start_offset = start_offset,
	        .old_len = end_offset - start_offset,
	        .new_text = &inserted,
	        .new_len = 1,
	        .before_cursor_offset = start_offset,
	        .after_cursor_offset = start_offset + 1,
	        .before_dirty = E.dirty,
	        .after_dirty = E.dirty + 1,
	};

	dispatchPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	if (editorApplyDocumentEdit(&edit)) {
		(void)editorSyncCursorFromOffsetByteBoundary(start_offset + 1);
	}
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	dispatchClearSelectionMode();
	return 1;
}

static int dispatchIndentSelection(void) {
	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		return 0;
	}

	// If selection ends exactly at column 0, that last row isn't visually selected
	int last_row = range.end_cy;
	if (last_row > range.start_cy && range.end_cx == 0) {
		last_row--;
	}

	size_t first_start = 0, dummy_end = 0;
	size_t dummy_start = 0, last_end = 0;
	if (!editorBufferLineByteRange(range.start_cy, &first_start, &dummy_end) ||
	    !editorBufferLineByteRange(last_row, &dummy_start, &last_end)) {
		return 1;
	}

	int num_rows = last_row - range.start_cy + 1;
	size_t old_len = last_end - first_start;
	size_t new_len = old_len + (size_t)num_rows;

	char *new_text = editorMalloc(new_len);
	if (new_text == NULL) {
		editorSetAllocFailureStatus();
		return 1;
	}

	size_t out = 0;
	for (int row = range.start_cy; row <= last_row; row++) {
		new_text[out++] = '\t';
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, row, &line)) {
			free(new_text);
			editorSetAllocFailureStatus();
			return 1;
		}
		memcpy(new_text + out, line.data, (size_t)line.size);
		out += (size_t)line.size;
		editorLineViewRelease(&line);
		if (row < last_row) {
			new_text[out++] = '\n';
		}
	}

	// Cursor: keep on same row, shift cx right by 1 for the prepended tab
	size_t before_offset = 0;
	(void)editorBufferPosToOffset(E.cy, E.cx, &before_offset);

	size_t after_offset;
	if (E.cy >= range.start_cy && E.cy <= last_row) {
		size_t cur_row_new_start = first_start;
		for (int row = range.start_cy; row < E.cy; row++) {
			cur_row_new_start += 1 + editorDocumentLineLength(E.document, row) + 1;
		}
		size_t new_cx = (size_t)E.cx + 1;
		size_t max_cx = editorDocumentLineLength(E.document, E.cy) + 1;
		if (new_cx > max_cx) {
			new_cx = max_cx;
		}
		after_offset = cur_row_new_start + new_cx;
	} else {
		after_offset = before_offset + (size_t)num_rows;
	}

	struct editorDocumentEdit edit = {
	        .kind = EDITOR_EDIT_INSERT_TEXT,
	        .start_offset = first_start,
	        .old_len = old_len,
	        .new_text = new_text,
	        .new_len = new_len,
	        .before_cursor_offset = before_offset,
	        .after_cursor_offset = after_offset,
	        .before_dirty = E.dirty,
	        .after_dirty = E.dirty + 1,
	};

	dispatchPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	free(new_text);
	dispatchClearSelectionMode();
	return 1;
}

static void dispatchDeleteSelection(void) {
	editorEditDeleteSelection(dispatchClearSelectionMode);
}

static void dispatchPasteClipboard(void) {
	editorEditPasteClipboard(dispatchClearSelectionMode);
}

static void dispatchMoveCursorToSearchMatch(int row_idx, int match_col, int match_len) {
	size_t match_offset = 0;
	if (!editorBufferPosToOffset(row_idx, match_col, &match_offset)) {
		dispatchClearActiveSearchMatch();
		return;
	}

	E.search_match_offset = match_offset;
	E.search_match_len = match_len;
	(void)dispatchSetCursorFromOffset(match_offset);
}

static void dispatchRestoreCursorToSavedSearchPosition(void) {
	if (!dispatchSetCursorFromOffset(E.search_saved_offset)) {
		(void)dispatchSetCursorFromOffset(0);
	}
}

static void dispatchFindCallback(const char *query, int key) {
	if (key == '\x1b') {
		dispatchRestoreCursorToSavedSearchPosition();
		dispatchClearSearchState();
		return;
	}
	if (key == '\r') {
		return;
	}
	if (query[0] == '\0') {
		dispatchRestoreCursorToSavedSearchPosition();
		dispatchClearActiveSearchMatch();
		E.search_direction = 1;
		return;
	}

	int match_row = -1;
	int match_col = -1;
	int direction = 1;
	int start_row = 0;
	int start_col = -1;
	int saved_row = 0;
	int saved_col = 0;
	(void)editorBufferOffsetToPos(E.search_saved_offset, &saved_row, &saved_col);
	int active_match_row = -1;
	int active_match_col = -1;
	int have_active_match = dispatchSearchMatchPosition(&active_match_row, &active_match_col);

	if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
		if (have_active_match) {
			start_row = active_match_row;
			start_col = active_match_col;
		} else {
			start_row = saved_row;
			start_col = saved_col - 1;
		}
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
		if (have_active_match) {
			start_row = active_match_row;
			start_col = active_match_col;
		} else {
			start_row = saved_row;
			start_col = saved_col;
		}
	}

	E.search_direction = direction;
	int found = direction == 1 ? editorBufferFindForward(query, start_row, start_col,
	                                                     &match_row, &match_col)
	                           : editorBufferFindBackward(query, start_row, start_col,
	                                                      &match_row, &match_col);

	if (!found) {
		dispatchRestoreCursorToSavedSearchPosition();
		dispatchClearActiveSearchMatch();
		return;
	}

	dispatchMoveCursorToSearchMatch(match_row, match_col, (int)strlen(query));
}

static int dispatchReplaceAtOffset(size_t offset, size_t old_len, const char *new_text,
                                   size_t new_len) {
	int dirty_before = E.dirty;
	dispatchPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	struct editorDocumentEdit edit = {.kind = EDITOR_EDIT_INSERT_TEXT,
	                                  .start_offset = offset,
	                                  .old_len = old_len,
	                                  .new_text = new_text,
	                                  .new_len = new_len,
	                                  .before_cursor_offset = offset,
	                                  .after_cursor_offset = offset + new_len,
	                                  .before_dirty = E.dirty,
	                                  .after_dirty = E.dirty + 1};
	int ok = editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, ok && E.dirty != dirty_before);
	return ok;
}

static int dispatchCollectMatchOffsets(const char *query, size_t query_len, size_t **offsets_out) {
	if (offsets_out == NULL || query == NULL || query_len == 0 || E.numrows == 0) {
		if (offsets_out != NULL) {
			*offsets_out = NULL;
		}
		return 0;
	}

	size_t *offsets = NULL;
	int count = 0;
	int cap = 0;
	int row = 0;
	int col = -1;
	int match_row = 0;
	int match_col = 0;
	int started = 0;
	size_t prev_offset = 0;

	while (editorBufferFindForward(query, row, col, &match_row, &match_col)) {
		size_t offset = 0;
		if (!editorBufferPosToOffset(match_row, match_col, &offset)) {
			break;
		}
		if (started && offset <= prev_offset) {
			break;
		}
		started = 1;
		prev_offset = offset;

		if (count == cap) {
			int new_cap = cap == 0 ? 16 : cap * 2;
			size_t *grown = editorRealloc(offsets, (size_t)new_cap * sizeof(size_t));
			if (grown == NULL) {
				free(offsets);
				*offsets_out = NULL;
				return -1;
			}
			offsets = grown;
			cap = new_cap;
		}
		offsets[count++] = offset;

		row = match_row;
		int next_col = match_col + (int)query_len;
		if (row < E.numrows &&
		    (size_t)next_col > editorDocumentLineLength(E.document, row)) {
			row++;
			if (row >= E.numrows) {
				break;
			}
			col = -1;
		} else {
			col = next_col - 1;
		}
	}

	*offsets_out = offsets;
	return count;
}

static int dispatchReplaceAllInBuffer(const char *query, size_t query_len, const char *replacement,
                                      size_t replacement_len) {
	size_t *offsets = NULL;
	int count = dispatchCollectMatchOffsets(query, query_len, &offsets);
	if (count <= 0) {
		return count;
	}

	size_t first_offset = offsets[0];

	int replaced = 0;
	for (int i = count - 1; i >= 0; i--) {
		editorHistoryBreakGroup();
		if (dispatchReplaceAtOffset(offsets[i], query_len, replacement, replacement_len)) {
			replaced++;
		}
	}
	free(offsets);

	if (replaced > 0) {
		(void)editorSyncCursorFromOffset(first_offset + replacement_len);
		editorViewportEnsureCursorVisible();
		free(E.search_query);
		E.search_query = NULL;
		dispatchClearActiveSearchMatch();
	}
	return replaced;
}

static int dispatchReplaceNavigateNext(const char *query, int query_len) {
	int match_row = -1;
	int match_col = -1;
	int cur_row = E.cy;
	int cur_start_col;
	int active_row = -1;
	int active_col = -1;
	if (E.search_match_len > 0 && dispatchSearchMatchPosition(&active_row, &active_col)) {
		cur_row = active_row;
		cur_start_col = active_col + query_len - 1;
	} else {
		cur_start_col = E.cx > 0 ? E.cx - 1 : -1;
	}
	if (!editorBufferFindForward(query, cur_row, cur_start_col, &match_row, &match_col)) {
		dispatchClearActiveSearchMatch();
		return 0;
	}
	dispatchMoveCursorToSearchMatch(match_row, match_col, query_len);
	return 1;
}

static void dispatchProjectReplaceFromSearch(void) {
	const char *find = editorProjectSearchQuery();
	if (find == NULL || find[0] == '\0') {
		editorSetStatusMsg("No active search query to replace");
		return;
	}
	if (E.drawer_project_search_result_count == 0) {
		editorSetStatusMsg("No search results to replace");
		return;
	}

	char prompt_buf[256];
	int pn = snprintf(prompt_buf, sizeof(prompt_buf),
	                  "Replace \"%.*s\" with: %%s (Enter to confirm, Esc to cancel)", 40, find);
	if (pn < 0 || pn >= (int)sizeof(prompt_buf)) {
		prompt_buf[sizeof(prompt_buf) - 1] = '\0';
	}
	char *replace_query = editorPromptWithCallback(prompt_buf, 1, NULL);
	if (replace_query == NULL) {
		return;
	}

	char *find_copy = strdup(find);
	if (find_copy == NULL) {
		free(replace_query);
		editorSetAllocFailureStatus();
		return;
	}

	typedef struct {
		char *path;
		int start_row;
	} FileEntry;
	FileEntry *files = NULL;
	int file_count = 0;
	int file_cap = 0;
	int result_count = E.drawer_project_search_result_count;

	for (int i = 0; i < result_count; i++) {
		const struct editorProjectSearchResult *r = &E.drawer_project_search_results[i];
		if (r->path == NULL || r->path[0] == '\0') {
			continue;
		}
		int already = 0;
		for (int j = 0; j < file_count; j++) {
			if (strcmp(files[j].path, r->path) == 0) {
				already = 1;
				break;
			}
		}
		if (already) {
			continue;
		}
		if (file_count == file_cap) {
			int new_cap = file_cap == 0 ? 8 : file_cap * 2;
			FileEntry *grown =
			        editorRealloc(files, (size_t)new_cap * sizeof(FileEntry));
			if (grown == NULL) {
				for (int j = 0; j < file_count; j++)
					free(files[j].path);
				free(files);
				free(find_copy);
				free(replace_query);
				editorSetAllocFailureStatus();
				return;
			}
			files = grown;
			file_cap = new_cap;
		}
		char *path_copy = strdup(r->path);
		if (path_copy == NULL) {
			for (int j = 0; j < file_count; j++)
				free(files[j].path);
			free(files);
			free(find_copy);
			free(replace_query);
			editorSetAllocFailureStatus();
			return;
		}
		files[file_count].path = path_copy;
		files[file_count].start_row = r->line > 0 ? r->line - 1 : 0;
		file_count++;
	}

	int saved_active_tab = editorTabActiveIndex();
	editorProjectSearchExit(0);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	size_t find_len = strlen(find_copy);
	size_t replace_len = strlen(replace_query);
	int total_replaced = 0;
	int total_files = 0;
	int aborted = 0;
	int replace_all_remaining = 0;

	for (int fi = 0; fi < file_count; fi++) {
		if (!editorTabOpenOrSwitchToFile(files[fi].path)) {
			continue;
		}
		editorHistoryBreakGroup();
		free(E.search_query);
		E.search_query = strdup(find_copy);
		if (E.search_query == NULL) {
			aborted = 1;
			break;
		}

		if (replace_all_remaining) {
			int count = dispatchReplaceAllInBuffer(E.search_query, find_len,
			                                       replace_query, replace_len);
			if (count > 0) {
				total_replaced += count;
				total_files++;
			}
			continue;
		}

		int start_row = files[fi].start_row;
		if (start_row >= E.numrows) {
			start_row = 0;
		}
		int match_row = -1;
		int match_col = -1;
		if (!editorBufferFindForward(E.search_query, start_row, -1, &match_row,
		                             &match_col)) {
			continue;
		}
		if (match_row < start_row) {
			if (!editorBufferFindForward(E.search_query, 0, -1, &match_row,
			                             &match_col)) {
				continue;
			}
		}

		dispatchMoveCursorToSearchMatch(match_row, match_col, (int)find_len);
		editorViewportEnsureCursorVisible();

		size_t file_start_offset = E.search_match_offset;
		const char *sep = strrchr(files[fi].path, '/');
		const char *basename = sep != NULL ? sep + 1 : files[fi].path;
		int file_replaced = 0;
		int done_with_file = 0;

		while (!done_with_file) {
			editorSetStatusMsg("[%s] Replace? Enter=this Tab=skip Ctrl+A=all Esc=done "
			                   "(%d replaced)",
			                   basename, total_replaced + file_replaced);
			editorRefreshScreen();

			int c = editorReadKey();
			if (c == INPUT_EOF_EVENT) {
				aborted = 1;
				done_with_file = 1;
				fi = file_count;
				editorExitOnInputShutdown();
			}
			if (c == RESIZE_EVENT) {
				(void)editorRefreshWindowSize();
				continue;
			}
			if (c == SYNTAX_EVENT || c == TASK_EVENT || c == WATCH_EVENT) {
				continue;
			}
			if (c == MOUSE_EVENT) {
				continue;
			}

			if (c == '\x1b') {
				aborted = 1;
				done_with_file = 1;
			} else if (c == '\r') {
				if (E.search_match_len > 0) {
					size_t offset = E.search_match_offset;
					editorHistoryBreakGroup();
					if (dispatchReplaceAtOffset(offset, find_len, replace_query,
					                            replace_len)) {
						file_replaced++;
					}
				}
				if (!dispatchReplaceNavigateNext(E.search_query, (int)find_len) ||
				    E.search_match_offset <= file_start_offset) {
					dispatchClearActiveSearchMatch();
					done_with_file = 1;
				}
			} else if (c == '\t') {
				if (!dispatchReplaceNavigateNext(E.search_query, (int)find_len) ||
				    E.search_match_offset <= file_start_offset) {
					dispatchClearActiveSearchMatch();
					done_with_file = 1;
				}
			} else if (c == CTRL_KEY('a')) {
				int count = dispatchReplaceAllInBuffer(E.search_query, find_len,
				                                       replace_query, replace_len);
				if (count > 0) {
					file_replaced += count;
				}
				done_with_file = 1;
				replace_all_remaining = 1;
			}
		}

		if (file_replaced > 0) {
			total_replaced += file_replaced;
			total_files++;
		}

		if (aborted) {
			break;
		}
	}

	free(replace_query);
	free(find_copy);
	for (int j = 0; j < file_count; j++) {
		free(files[j].path);
	}
	free(files);

	if (!aborted && saved_active_tab < 0) {
		(void)editorTabSwitchToIndex(0);
	}

	if (total_replaced > 0 || aborted) {
		editorSetStatusMsg("Replaced %d occurrence(s) across %d file(s)%s", total_replaced,
		                   total_files, aborted ? " (stopped)" : "");
	} else {
		editorSetStatusMsg("No replacements made");
	}
}

static void dispatchFindReplace(void) {
	dispatchAlignCursorWithRowEnd();
	E.search_saved_offset = E.cursor_offset;
	E.search_direction = 1;
	dispatchClearActiveSearchMatch();

	char *find_query = editorPromptWithCallback(
	        "Find: %s (Arrows/Enter to confirm, Esc to cancel)", 1, dispatchFindCallback);
	if (find_query == NULL) {
		return;
	}

	free(E.search_query);
	E.search_query = find_query;

	if (E.search_match_len == 0) {
		int match_row = -1;
		int match_col = -1;
		int saved_row = 0;
		int saved_col = 0;
		(void)editorBufferOffsetToPos(E.search_saved_offset, &saved_row, &saved_col);
		if (!editorBufferFindForward(find_query, saved_row, saved_col - 1, &match_row,
		                             &match_col)) {
			editorSetStatusMsg("No matches for \"%s\"", find_query);
			return;
		}
		dispatchMoveCursorToSearchMatch(match_row, match_col, (int)strlen(find_query));
	}

	char *replace_query = editorPromptWithCallback(
	        "Replace with: %s (Enter to confirm, Esc to cancel)", 1, NULL);
	if (replace_query == NULL) {
		return;
	}

	size_t find_len = strlen(find_query);
	size_t replace_len = strlen(replace_query);
	int replaced = 0;

	while (1) {
		editorSetStatusMsg("Replace? Enter=this Tab=skip Ctrl+A=all Esc=done (%d replaced)",
		                   replaced);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == INPUT_EOF_EVENT) {
			free(replace_query);
			editorExitOnInputShutdown();
			return;
		}
		if (c == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (c == SYNTAX_EVENT || c == TASK_EVENT || c == WATCH_EVENT) {
			continue;
		}
		if (c == MOUSE_EVENT) {
			continue;
		}

		if (c == '\x1b') {
			break;
		}

		if (c == '\r') {
			if (E.search_match_len <= 0) {
				break;
			}
			size_t match_offset = E.search_match_offset;
			editorHistoryBreakGroup();
			if (dispatchReplaceAtOffset(match_offset, find_len, replace_query,
			                            replace_len)) {
				replaced++;
			}
			if (!dispatchReplaceNavigateNext(find_query, (int)find_len)) {
				editorSetStatusMsg("Done. Replaced %d occurrence(s)", replaced);
				free(replace_query);
				return;
			}
		} else if (c == '\t') {
			if (!dispatchReplaceNavigateNext(find_query, (int)find_len)) {
				editorSetStatusMsg("No more matches. Replaced %d occurrence(s)",
				                   replaced);
				free(replace_query);
				return;
			}
		} else if (c == CTRL_KEY('a')) {
			int count = dispatchReplaceAllInBuffer(find_query, find_len, replace_query,
			                                       replace_len);
			if (count < 0) {
				editorSetStatusMsg("Replace all failed");
			} else {
				editorSetStatusMsg("Replaced %d occurrence(s)", replaced + count);
			}
			free(replace_query);
			return;
		} else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
			if (!dispatchReplaceNavigateNext(find_query, (int)find_len)) {
				editorSetStatusMsg("No more matches");
			}
		} else if (c == ARROW_LEFT || c == ARROW_UP) {
			int match_row = -1;
			int match_col = -1;
			int have_active = dispatchSearchMatchPosition(&match_row, &match_col);
			int start_row = have_active ? match_row : E.cy;
			int start_col = have_active ? match_col : E.cx;
			if (editorBufferFindBackward(find_query, start_row, start_col, &match_row,
			                             &match_col)) {
				dispatchMoveCursorToSearchMatch(match_row, match_col,
				                                (int)find_len);
			}
		}
	}

	editorSetStatusMsg(replaced > 0 ? "Replaced %d occurrence(s)" : "Cancelled", replaced);
	free(replace_query);
}

static void dispatchFind(void) {
	dispatchAlignCursorWithRowEnd();
	E.search_saved_offset = E.cursor_offset;
	E.search_direction = 1;
	dispatchClearActiveSearchMatch();

	char *query = editorPromptWithCallback("Search: %s (Use ESC/Arrows/Enter)", 1,
	                                       dispatchFindCallback);
	if (query == NULL) {
		return;
	}

	free(E.search_query);
	E.search_query = query;
	if (E.search_match_len == 0) {
		editorSetStatusMsg("No matches for \"%s\"", query);
	}
}

static int dispatchParsePositiveLineNumber(const char *query, long *out_line) {
	if (query[0] == '\0') {
		return 0;
	}

	long line = 0;
	for (size_t i = 0; query[i] != '\0'; i++) {
		unsigned char ch = (unsigned char)query[i];
		if (!isdigit(ch)) {
			return 0;
		}

		int digit = query[i] - '0';
		if (line > (LONG_MAX - digit) / 10) {
			return 0;
		}
		line = line * 10 + digit;
	}

	if (line <= 0) {
		return 0;
	}

	*out_line = line;
	return 1;
}

static void dispatchGoToLine(void) {
	char *query = editorPrompt("Go to line: %s");
	if (query == NULL) {
		return;
	}

	long line = 0;
	int valid = dispatchParsePositiveLineNumber(query, &line);
	free(query);
	if (!valid) {
		editorSetStatusMsg("Invalid line number");
		return;
	}

	if (E.numrows == 0) {
		(void)dispatchSetCursorFromOffset(0);
		editorSetStatusMsg("Buffer is empty");
		return;
	}

	if (line > E.numrows) {
		line = E.numrows;
	}

	size_t target_offset = 0;
	if (!editorBufferPosToOffset((int)(line - 1), 0, &target_offset) ||
	    !dispatchSetCursorFromOffset(target_offset)) {
		(void)dispatchSetCursorFromOffset(0);
	}
}

static const char *dispatchBasenameFromPath(const char *path) {
	if (path == NULL) {
		return "";
	}
	const char *base = strrchr(path, '/');
	if (base == NULL) {
		return path;
	}
	return base + 1;
}

static int dispatchJumpToPathLocation(const char *path, int line, int character, int preview,
                                      int center) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	int opened = preview ? editorTabOpenOrSwitchToPreviewFile(path)
	                     : editorTabOpenOrSwitchToFile(path);
	if (!opened) {
		return 0;
	}

	if (E.numrows <= 0) {
		(void)dispatchSetCursorFromOffset(0);
		if (center) {
			editorViewportCenterCursor();
		} else {
			editorViewportEnsureCursorVisible();
		}
		return 1;
	}

	if (line < 0) {
		line = 0;
	}
	if (line >= E.numrows) {
		line = E.numrows - 1;
	}
	if (character < 0) {
		character = 0;
	}
	character = editorLspProtocolCharacterToBufferColumn(line, character);
	int target_cx = character;
	struct editorLineView lview = {0};
	if (editorDocumentLineView(E.document, line, &lview)) {
		if (character > lview.size) {
			character = lview.size;
		}
		target_cx = editorBytesClampCxToClusterBoundary(lview.data, lview.size, character);
		if (target_cx > lview.size) {
			target_cx = lview.size;
		}
		editorLineViewRelease(&lview);
	}
	size_t target_offset = 0;
	if (!editorBufferPosToOffset(line, target_cx, &target_offset) ||
	    !dispatchSetCursorFromOffset(target_offset)) {
		(void)dispatchSetCursorFromOffset(0);
	}
	if (center) {
		editorViewportCenterCursor();
	} else {
		editorViewportEnsureCursorVisible();
	}
	return 1;
}

static int dispatchJumpToDefinitionLocation(const struct editorLspLocation *location) {
	if (location == NULL) {
		return 0;
	}
	return dispatchJumpToPathLocation(location->path, location->line, location->character, 0,
	                                  0);
}

static int dispatchPromptLocationChoice(const char *kind_capitalized, int count, int *choice_out) {
	if (choice_out == NULL || count <= 0) {
		return 0;
	}

	char prompt[80];
	int written = snprintf(prompt, sizeof(prompt), "%s (1-%d): %%s", kind_capitalized, count);
	if (written <= 0 || (size_t)written >= sizeof(prompt)) {
		return 0;
	}

	char *query = editorPrompt(prompt);
	if (query == NULL) {
		return 0;
	}

	long selected = 0;
	int parsed = dispatchParsePositiveLineNumber(query, &selected);
	free(query);
	if (!parsed || selected > count) {
		editorSetStatusMsg("Invalid %s choice", kind_capitalized);
		return 0;
	}

	*choice_out = (int)(selected - 1);
	return 1;
}

typedef int (*dispatchLspLocationRequestFn)(const char *filename,
                                            enum editorSyntaxLanguage language, int line,
                                            int character, struct editorLspLocation **locations_out,
                                            int *count_out, int *timed_out_out);

static void dispatchRunLocationLookup(const char *kind_lower, const char *kind_capitalized,
                                      const char *kind_plural,
                                      dispatchLspLocationRequestFn request_fn) {
	if (!editorLanguageGoToSupported(E.syntax_language)) {
		editorSetStatusMsg("Go to %s is available for Go, C, C++, HTML, CSS/SCSS, JSON, "
		                   "and JavaScript files only",
		                   kind_lower);
		return;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		const char *language_label = editorLanguageGoToLabel();
		if (language_label == NULL) {
			language_label = "source";
		}
		editorSetStatusMsg("Save this %s buffer before using go to %s", language_label,
		                   kind_lower);
		return;
	}
	if (!editorLanguageGoToEnabled()) {
		editorSetStatusMsg("%s is disabled in config", editorLanguageGoToServerName());
		return;
	}
	const char *command = editorLanguageGoToCommand();
	const char *command_setting = editorLanguageGoToCommandSettingName();
	if (command == NULL || command_setting == NULL) {
		editorSetStatusMsg("LSP unavailable for this file");
		return;
	}
	if (command[0] == '\0') {
		editorSetStatusMsg("LSP disabled: [lsp].%s is empty", command_setting);
		return;
	}
	dispatchAlignCursorWithRowEnd();
	if (E.cy < 0 || E.cy >= E.numrows) {
		editorSetStatusMsg("Cursor is not on a source line");
		return;
	}

	size_t full_text_len = 0;
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		editorSetStatusMsg("File too large");
		return;
	}
	char *full_text = editorTextSourceDupRange(&source, 0, source.length, &full_text_len);
	if (full_text == NULL) {
		if (source.length > ROTIDE_MAX_TEXT_BYTES) {
			editorSetStatusMsg("File too large");
		} else {
			editorSetStatusMsg("Out of memory");
		}
		return;
	}

	int ready = editorLspEnsureDocumentOpen(E.filename, E.syntax_language, &E.lsp_doc_open,
	                                        &E.lsp_doc_version,
	                                        full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
	if (!ready) {
		if (editorLspLastStartupFailureReason() ==
		    EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
			editorLanguageMaybePromptInstallServer();
			return;
		}
		if (strncmp(E.statusmsg, "LSP ", strlen("LSP ")) != 0) {
			editorSetStatusMsg("LSP unavailable for this file");
		}
		return;
	}

	struct editorLspLocation *locations = NULL;
	int count = 0;
	int timed_out = 0;
	int request_result = request_fn(E.filename, E.syntax_language, E.cy, E.cx, &locations,
	                                &count, &timed_out);
	if (request_result == -2 || timed_out) {
		editorSetStatusMsg("Go to %s timed out", kind_lower);
		editorLspFreeLocations(locations, count);
		return;
	}
	if (request_result <= 0) {
		editorSetStatusMsg("Go to %s failed", kind_lower);
		editorLspFreeLocations(locations, count);
		return;
	}
	if (count <= 0) {
		editorSetStatusMsg("%s not found", kind_capitalized);
		editorLspFreeLocations(locations, count);
		return;
	}

	int selected_index = 0;
	if (count > 1) {
		editorSetStatusMsg("Found %d %s; choose 1-%d", count, kind_plural, count);
		if (!dispatchPromptLocationChoice(kind_capitalized, count, &selected_index)) {
			editorLspFreeLocations(locations, count);
			return;
		}
	}

	const struct editorLspLocation *selected = &locations[selected_index];
	if (!dispatchJumpToDefinitionLocation(selected)) {
		editorSetStatusMsg("Unable to jump to %s", kind_lower);
		editorLspFreeLocations(locations, count);
		return;
	}

	editorSetStatusMsg("%s: %s:%d", kind_capitalized, dispatchBasenameFromPath(selected->path),
	                   selected->line + 1);
	editorLspFreeLocations(locations, count);
}

static void dispatchGoToDefinition(void) {
	dispatchRunLocationLookup("definition", "Definition", "definitions",
	                          editorLspRequestDefinition);
}

static void dispatchGoToImplementation(void) {
	dispatchRunLocationLookup("implementation", "Implementation", "implementations",
	                          editorLspRequestImplementation);
}

static void dispatchGoToSymbol(void) {
	if (!editorLanguageGoToSupported(E.syntax_language)) {
		editorSetStatusMsg("Go to symbol is available for Go, C, C++, HTML, CSS/SCSS, "
		                   "JSON, and JavaScript files only");
		return;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		const char *language_label = editorLanguageGoToLabel();
		if (language_label == NULL) {
			language_label = "source";
		}
		editorSetStatusMsg("Save this %s buffer before using go to symbol", language_label);
		return;
	}
	if (!editorLanguageGoToEnabled()) {
		editorSetStatusMsg("%s is disabled in config", editorLanguageGoToServerName());
		return;
	}
	const char *command = editorLanguageGoToCommand();
	const char *command_setting = editorLanguageGoToCommandSettingName();
	if (command == NULL || command_setting == NULL) {
		editorSetStatusMsg("LSP unavailable for this file");
		return;
	}
	if (command[0] == '\0') {
		editorSetStatusMsg("LSP disabled: [lsp].%s is empty", command_setting);
		return;
	}

	size_t full_text_len = 0;
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		editorSetStatusMsg("File too large");
		return;
	}
	char *full_text = editorTextSourceDupRange(&source, 0, source.length, &full_text_len);
	if (full_text == NULL) {
		if (source.length > ROTIDE_MAX_TEXT_BYTES) {
			editorSetStatusMsg("File too large");
		} else {
			editorSetStatusMsg("Out of memory");
		}
		return;
	}

	int ready = editorLspEnsureDocumentOpen(E.filename, E.syntax_language, &E.lsp_doc_open,
	                                        &E.lsp_doc_version,
	                                        full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
	if (!ready) {
		if (editorLspLastStartupFailureReason() ==
		    EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
			editorLanguageMaybePromptInstallServer();
			return;
		}
		if (strncmp(E.statusmsg, "LSP ", strlen("LSP ")) != 0) {
			editorSetStatusMsg("LSP unavailable for this file");
		}
		return;
	}

	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	int timed_out = 0;
	int request_result = editorLspRequestDocumentSymbols(E.filename, E.syntax_language,
	                                                     &symbols, &count, &timed_out);
	if (request_result == -2 || timed_out) {
		editorSetStatusMsg("Go to symbol timed out");
		editorLspFreeSymbols(symbols, count);
		return;
	}
	if (request_result <= 0) {
		editorSetStatusMsg("Go to symbol failed");
		editorLspFreeSymbols(symbols, count);
		return;
	}
	if (count <= 0) {
		editorSetStatusMsg("No symbols");
		editorLspFreeSymbols(symbols, count);
		return;
	}

	int selected_index = 0;
	if (count > 1) {
		editorSetStatusMsg("Found %d symbols; choose 1-%d", count, count);
		if (!dispatchPromptLocationChoice("Symbol", count, &selected_index)) {
			editorLspFreeSymbols(symbols, count);
			return;
		}
	}

	const struct editorLspSymbol *selected = &symbols[selected_index];
	if (!dispatchJumpToPathLocation(E.filename, selected->line, selected->character, 0, 0)) {
		editorSetStatusMsg("Unable to jump to symbol");
		editorLspFreeSymbols(symbols, count);
		return;
	}

	editorSetStatusMsg("%s %s:%d", editorLspSymbolKindLabel(selected->kind),
	                   selected->name != NULL ? selected->name : "(unnamed)",
	                   selected->line + 1);
	editorLspFreeSymbols(symbols, count);
}

static void dispatchApplyEslintFixes(void) {
	if (E.filename == NULL || E.filename[0] == '\0') {
		editorSetStatusMsg("Save this JavaScript buffer before applying ESLint fixes");
		return;
	}
	if (editorLspServerNameForFile(E.filename, E.syntax_language) == NULL ||
	    !editorLspFileUsesEslint(E.filename, E.syntax_language)) {
		editorSetStatusMsg("ESLint fixes are available for JavaScript files only");
		return;
	}
	if (!E.lsp_eslint_enabled) {
		editorSetStatusMsg("vscode-eslint-language-server is disabled in config");
		return;
	}
	if (E.lsp_eslint_command[0] == '\0') {
		editorSetStatusMsg("LSP disabled: [lsp].eslint_command is empty");
		return;
	}

	int result = editorLspRequestCodeActionFixes(E.filename, E.syntax_language);
	if (result > 0) {
		editorSetStatusMsg("ESLint fixes applied");
		return;
	}
	if (result == 0) {
		editorSetStatusMsg("No ESLint fixes available");
		return;
	}
	if (editorLspLastStartupFailureReason() == EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
		editorLanguagePromptInstallSharedVscodeServers();
		return;
	}
	if (result == -2) {
		editorSetStatusMsg("ESLint fixes timed out");
		return;
	}
	editorSetStatusMsg("ESLint fixes failed");
}

static void dispatchMoveCursor(int k) {
	dispatchAlignCursorWithRowEnd();

	int cy = E.cy;
	int cx = E.cx;
	int target_rx = 0;
	if ((k == ARROW_UP || k == ARROW_DOWN) && cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			target_rx = editorBytesCxToRx(line.data, line.size, cx);
			editorLineViewRelease(&line);
		}
	}

	switch (k) {
		case ARROW_LEFT:
			if (cx != 0) {
				if (cy < E.numrows) {
					struct editorLineView line = {0};
					if (editorDocumentLineView(E.document, cy, &line)) {
						cx = editorBytesPrevClusterIdx(line.data, line.size,
						                               cx);
						editorLineViewRelease(&line);
					}
				} else {
					cx--;
				}
			} else if (cy > 0) {
				cy--;
				cx = (int)editorDocumentLineLength(E.document, cy);
			}
			break;
		case ARROW_RIGHT:
			if (E.numrows > cy) {
				struct editorLineView line = {0};
				if (editorDocumentLineView(E.document, cy, &line)) {
					if (cx < line.size) {
						cx = editorBytesNextClusterIdx(line.data, line.size,
						                               cx);
					} else if (cx == line.size) {
						cy++;
						cx = 0;
					}
					editorLineViewRelease(&line);
				}
			}
			break;
		case ARROW_DOWN:
			if (cy < E.numrows) {
				cy++;
			}
			break;
		case ARROW_UP:
			if (cy != 0) {
				cy--;
			}
			break;
	}

	if ((k == ARROW_UP || k == ARROW_DOWN) && cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesRxToCx(line.data, line.size, target_rx);
			editorLineViewRelease(&line);
		}
	}

	(void)dispatchSetCursorFromPosition(cy, cx);
}

static int dispatchBytesIsWordAt(const char *bytes, int size, int cx) {
	return cx >= 0 && cx < size && dispatchIsWordByte((unsigned char)bytes[cx]);
}

static void dispatchMoveCursorWordLeft(void) {
	dispatchAlignCursorWithRowEnd();

	int cy = E.cy;
	int cx = E.cx;
	if (cy > E.numrows) {
		cy = E.numrows;
	}

	while (cy >= 0) {
		if (cy >= E.numrows) {
			if (E.numrows == 0) {
				cy = 0;
				cx = 0;
				break;
			}
			cy = E.numrows - 1;
			cx = (int)editorDocumentLineLength(E.document, cy);
			continue;
		}

		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			break;
		}
		cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
		int found_word = 0;
		if (cx > 0) {
			int scan = editorBytesPrevCharIdx(line.data, line.size, cx);
			while (1) {
				if (dispatchBytesIsWordAt(line.data, line.size, scan)) {
					found_word = 1;
					break;
				}
				if (scan == 0) {
					break;
				}
				int prev = editorBytesPrevCharIdx(line.data, line.size, scan);
				if (prev >= scan) {
					break;
				}
				scan = prev;
			}
			if (found_word) {
				while (scan > 0) {
					int prev =
					        editorBytesPrevCharIdx(line.data, line.size, scan);
					if (prev >= scan ||
					    !dispatchBytesIsWordAt(line.data, line.size, prev)) {
						break;
					}
					scan = prev;
				}
				cx = scan;
			}
		}
		editorLineViewRelease(&line);
		if (found_word) {
			break;
		}

		if (cy == 0) {
			cx = 0;
			break;
		}
		cy--;
		cx = (int)editorDocumentLineLength(E.document, cy);
	}

	(void)dispatchSetCursorFromPosition(cy, cx);
}

static void dispatchMoveCursorWordRight(void) {
	dispatchAlignCursorWithRowEnd();

	int cy = E.cy;
	int cx = E.cx;
	if (cy < 0) {
		cy = 0;
		cx = 0;
	}

	while (cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			break;
		}
		cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
		int found_word = 0;

		while (cx < line.size && !dispatchBytesIsWordAt(line.data, line.size, cx)) {
			int next = editorBytesNextCharIdx(line.data, line.size, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		while (cx < line.size && dispatchBytesIsWordAt(line.data, line.size, cx)) {
			found_word = 1;
			int next = editorBytesNextCharIdx(line.data, line.size, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		int line_end = line.size;
		editorLineViewRelease(&line);
		if (found_word) {
			break;
		}

		if (cy >= E.numrows - 1) {
			cx = line_end;
			break;
		}
		cy++;
		cx = 0;
	}

	(void)dispatchSetCursorFromPosition(cy, cx);
}

static int dispatchColumnSelectionCurrentRx(void) {
	if (E.cy >= 0 && E.cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, E.cy, &line)) {
			return 0;
		}
		int rx = editorBytesCxToRx(line.data, line.size, E.cx);
		editorLineViewRelease(&line);
		return rx;
	}
	return 0;
}

static void dispatchColumnSelectionEnsureActive(void) {
	if (E.column_select_active) {
		return;
	}
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.column_select_active = 1;
	E.column_select_anchor_cy = E.cy;
	E.column_select_anchor_rx = dispatchColumnSelectionCurrentRx();
	E.column_select_cursor_rx = E.column_select_anchor_rx;
}

static void dispatchColumnSelectionApplyCursorRx(void) {
	int target_rx = E.column_select_cursor_rx;
	int cy = E.cy;
	int cx = 0;
	if (cy < 0) {
		cy = 0;
	}
	if (cy > E.numrows) {
		cy = E.numrows;
	}
	if (cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesRxToCx(line.data, line.size, target_rx);
			editorLineViewRelease(&line);
		}
	}
	(void)dispatchSetCursorFromPosition(cy, cx);
}

static void dispatchColumnSelectionMove(int dy, int drx) {
	dispatchColumnSelectionEnsureActive();
	int new_cy = E.cy + dy;
	if (new_cy < 0) {
		new_cy = 0;
	}
	if (new_cy > E.numrows) {
		new_cy = E.numrows;
	}
	E.cy = new_cy;
	int new_rx = E.column_select_cursor_rx + drx;
	if (new_rx < 0) {
		new_rx = 0;
	}
	E.column_select_cursor_rx = new_rx;
	dispatchColumnSelectionApplyCursorRx();
}

static int dispatchProcessMappedAction(enum editorAction action, int *effects_out) {
	int effects = DISPATCH_KEYPRESS_EFFECT_NONE;
	int drawer_search_cursor_or_edit = 0;
	if (editorHandleDrawerSearchMappedAction(action, &drawer_search_cursor_or_edit,
	                                         dispatchProjectReplaceFromSearch)) {
		if (drawer_search_cursor_or_edit) {
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
		}
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 0;
	}

	if (editorActiveTabIsReadOnly()) {
		if (action == EDITOR_ACTION_SAVE) {
			editorSetStatusMsg(editorActiveTabIsUnsupportedFile()
			                           ? "Unsupported files cannot be saved"
			                           : "Task logs cannot be saved");
			if (effects_out != NULL) {
				*effects_out = effects;
			}
			return 1;
		}
		if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER &&
		    dispatchActionMutatesReadOnlyBuffer(action)) {
			editorSetStatusMsg(editorActiveTabIsUnsupportedFile()
			                           ? "File is unsupported"
			                           : "Task log is read-only");
			if (effects_out != NULL) {
				*effects_out = effects;
			}
			return 1;
		}
	}

	if (editorHandleFileTabMappedAction(action)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	if (editorHandleTerminalDebugMappedAction(action)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	if (editorHandleWorkspaceMappedAction(action, DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT,
	                                      dispatchProcessMappedAction,
	                                      dispatchJumpToPathLocation, &effects)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	if (editorHandleLanguageMappedAction(action, DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT,
	                                     dispatchPinActivePreviewForEdit,
	                                     dispatchGoToDefinition, dispatchGoToImplementation,
	                                     dispatchGoToSymbol, dispatchApplyEslintFixes,
	                                     &effects)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	if (editorHandleEditMappedAction(
	            action, DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT, dispatchClearSelectionMode,
	            dispatchPinActivePreviewForEdit, dispatchClearSearchState,
	            dispatchToggleSelectionMode, dispatchCopySelection, dispatchCutSelection,
	            dispatchDeleteSelection, dispatchPasteClipboard, dispatchDeleteCharAction,
	            dispatchBackspaceAction, dispatchMoveLineUpAction, dispatchMoveLineDownAction,
	            dispatchToggleCommentLines, &effects)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	switch (action) {
		case EDITOR_ACTION_COLUMN_SELECT_UP:
			editorHistoryBreakGroup();
			if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
				break;
			}
			dispatchColumnSelectionMove(-1, 0);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_COLUMN_SELECT_DOWN:
			editorHistoryBreakGroup();
			if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
				break;
			}
			dispatchColumnSelectionMove(1, 0);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_COLUMN_SELECT_LEFT:
			editorHistoryBreakGroup();
			dispatchColumnSelectionMove(0, -1);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_COLUMN_SELECT_RIGHT:
			editorHistoryBreakGroup();
			dispatchColumnSelectionMove(0, 1);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_TOGGLE_LINE_WRAP:
			editorHistoryBreakGroup();
			E.line_wrap_enabled = !E.line_wrap_enabled;
			if (E.line_wrap_enabled) {
				E.coloff = 0;
			} else {
				E.wrapoff = 0;
			}
			editorViewportEnsureCursorVisible();
			editorSetStatusMsg("Line wrap %s",
			                   E.line_wrap_enabled ? "enabled" : "disabled");
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_TOGGLE_LINE_NUMBERS:
			editorHistoryBreakGroup();
			E.line_numbers_enabled = !E.line_numbers_enabled;
			editorViewportEnsureCursorVisible();
			editorSetStatusMsg("Line numbers %s",
			                   E.line_numbers_enabled ? "enabled" : "disabled");
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT:
			editorHistoryBreakGroup();
			E.current_line_highlight_enabled = !E.current_line_highlight_enabled;
			editorSetStatusMsg("Current-line highlight %s",
			                   E.current_line_highlight_enabled ? "enabled"
			                                                    : "disabled");
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_FIND:
			editorHistoryBreakGroup();
			dispatchFind();
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_FIND_REPLACE:
			editorHistoryBreakGroup();
			dispatchFindReplace();
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_LINE:
			editorHistoryBreakGroup();
			dispatchGoToLine();
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_MATCHING_BRACKET:
			editorHistoryBreakGroup();
			if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER &&
			    editorJumpToMatchingBracket()) {
				effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_DRAWER_CREATE_FILE:
			editorHistoryBreakGroup();
			editorDrawerPromptCreateFile();
			break;
		case EDITOR_ACTION_DRAWER_CREATE_FOLDER:
			editorHistoryBreakGroup();
			editorDrawerPromptCreateFolder();
			break;
		case EDITOR_ACTION_DRAWER_RENAME:
			editorHistoryBreakGroup();
			editorDrawerPromptRename();
			break;
		case EDITOR_ACTION_DRAWER_DELETE:
			editorHistoryBreakGroup();
			editorDrawerPromptDelete();
			break;
		case EDITOR_ACTION_MOVE_HOME:
			editorHistoryBreakGroup();
			(void)dispatchSetCursorFromPosition(E.cy, 0);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_END:
			editorHistoryBreakGroup();
			if (E.cy < E.numrows) {
				(void)dispatchSetCursorFromPosition(
				        E.cy, (int)editorDocumentLineLength(E.document, E.cy));
			} else {
				(void)dispatchSetCursorFromPosition(E.numrows, 0);
			}
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_WORD_LEFT:
			editorHistoryBreakGroup();
			editorColumnSelectionClear();
			dispatchMoveCursorWordLeft();
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_WORD_RIGHT:
			editorHistoryBreakGroup();
			editorColumnSelectionClear();
			dispatchMoveCursorWordRight();
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_PAGE_UP: {
			editorHistoryBreakGroup();
			int page_rows = E.window_rows;
			if (page_rows < 1) {
				page_rows = 1;
			}
			editorViewportScrollByRows(-page_rows);
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		}
		case EDITOR_ACTION_PAGE_DOWN: {
			editorHistoryBreakGroup();
			int page_rows = E.window_rows;
			if (page_rows < 1) {
				page_rows = 1;
			}
			editorViewportScrollByRows(page_rows);
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		}
		case EDITOR_ACTION_SCROLL_LEFT:
			editorHistoryBreakGroup();
			editorViewportScrollByCols(-KEYBOARD_SCROLL_COLS);
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_SCROLL_RIGHT:
			editorHistoryBreakGroup();
			editorViewportScrollByCols(KEYBOARD_SCROLL_COLS);
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_MOVE_UP:
			editorHistoryBreakGroup();
			editorColumnSelectionClear();
			dispatchMoveCursor(ARROW_UP);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_DOWN:
			editorHistoryBreakGroup();
			editorColumnSelectionClear();
			dispatchMoveCursor(ARROW_DOWN);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_LEFT:
			editorHistoryBreakGroup();
			editorColumnSelectionClear();
			dispatchMoveCursor(ARROW_LEFT);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_RIGHT:
			editorHistoryBreakGroup();
			editorColumnSelectionClear();
			dispatchMoveCursor(ARROW_RIGHT);
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_ESCAPE:
			// In normal editor mode Escape only clears transient selection state; quit
			// is configurable.
			editorHistoryBreakGroup();
			if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
				E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
				break;
			}
			dispatchClearSelectionMode();
			break;
		case EDITOR_ACTION_REDRAW:
			editorHistoryBreakGroup();
			break;
		case EDITOR_ACTION_COUNT:
		default:
			break;
	}

	if (effects_out != NULL) {
		*effects_out = effects;
	}
	return 0;
}

void editorProcessKeypress(void) {
	int c = editorReadKey();
	enum editorAction action = EDITOR_ACTION_COUNT;
	int mapped_action = 0;
	int effects = DISPATCH_KEYPRESS_EFFECT_NONE;

	if (c == INPUT_EOF_EVENT) {
		editorExitOnInputShutdown();
		return;
	}
	if (c == RESIZE_EVENT) {
		(void)editorRefreshWindowSize();
		editorTerminalPaneResizeAllToLayout(E.layout_root);
		return;
	}
	if (c == TASK_EVENT) {
		return;
	}
	if (c == SYNTAX_EVENT) {
		return;
	}
	if (c == WATCH_EVENT) {
		return;
	}
	if (c == TERMINAL_EVENT) {
		struct editorPaneNode *prev_focus = E.focused_leaf;
		int closed = editorTerminalPaneCloseExited(&E.layout_root, &E.focused_leaf,
		                                           &E.dap_terminal_leaf);
		if (closed > 0 && E.focused_leaf != NULL && E.focused_leaf != prev_focus) {
			(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
		}
		/* Pump already happened in editorReadKey. */
		return;
	}
	if (c == BRACKETED_PASTE_START_EVENT) {
		E.paste_active = 1;
		if (E.focused_leaf != NULL && !E.focused_leaf->is_split &&
		    E.focused_leaf->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
		    E.focused_leaf->as.leaf.kind_state != NULL) {
			(void)editorTerminalPaneSendPasteStart(
			        (struct editorTerminalPane *)E.focused_leaf->as.leaf.kind_state);
		}
		return;
	}
	if (c == BRACKETED_PASTE_END_EVENT) {
		E.paste_active = 0;
		if (E.focused_leaf != NULL && !E.focused_leaf->is_split &&
		    E.focused_leaf->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
		    E.focused_leaf->as.leaf.kind_state != NULL) {
			(void)editorTerminalPaneSendPasteEnd(
			        (struct editorTerminalPane *)E.focused_leaf->as.leaf.kind_state);
		}
		return;
	}

	if (editorPopupIsVisible()) {
		/*
		 * If the upcoming character would simply narrow the autocomplete popup, skip the
		 * popup key handler entirely so the popup stays open across the keystroke. The
		 * insertion flow below will call editorAutocompleteOnCharInserted which refilters
		 * the visible items in place.
		 */
		if (!editorAutocompleteWouldRefilter(c)) {
			enum editorPopupKeyResult popup_result = editorPopupHandleKey(c);
			if (popup_result == EDITOR_POPUP_KEY_ACCEPTED) {
				if (editorAutocompleteIsVisible()) {
					dispatchPinActivePreviewForEdit();
					editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
					int dirty_before = E.dirty;
					int applied = editorAutocompleteAcceptSelection();
					editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT,
					                        E.dirty != dirty_before);
					if (applied) {
						editorViewportEnsureCursorVisible();
					}
				} else {
					editorPopupClose();
				}
				return;
			}
			if (popup_result == EDITOR_POPUP_KEY_CONSUMED) {
				return;
			}
			if (editorAutocompleteIsVisible()) {
				editorAutocompleteCancel();
			}
		}
	}

	if (c == MOUSE_EVENT) {
		// Mouse input can move cursor/selection, but it should not create edit history
		// entries.
		editorHistoryBreakGroup();
		int mouse_effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
		(void)editorHandleMouseEventDispatch(
		        DRAWER_DOUBLE_CLICK_THRESHOLD_MS, TEXT_MULTI_CLICK_THRESHOLD_MS,
		        dispatchProcessMappedAction, dispatchJumpToPathLocation,
		        dispatchCtrlClickGoToDefinitionAction, &mouse_effects);
		if ((mouse_effects & EDITOR_MOUSE_DISPATCH_EFFECT_VIEWPORT_SCROLL) != 0) {
			effects |= DISPATCH_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
		}
		if ((mouse_effects & EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT) != 0) {
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
		}
	} else {
		if (editorClearHoverLinkState()) {
			effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
		}
		/* In focused terminal panes keys go to PTY, except terminal_prefix. */
		if (E.focused_leaf != NULL && !E.focused_leaf->is_split &&
		    E.focused_leaf->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
		    E.focused_leaf->as.leaf.kind_state != NULL &&
		    E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
			if (E.terminal_prefix_armed) {
				E.terminal_prefix_armed = 0;
				/* Fall through to keymap lookup below. */
			} else {
				enum editorAction terminal_action = EDITOR_ACTION_COUNT;
				if (editorKeymapLookupAction(&E.keymap, c, &terminal_action) &&
				    terminal_action == EDITOR_ACTION_TERMINAL_PREFIX) {
					E.terminal_prefix_armed = 1;
					editorSetStatusMsg(
					        "Terminal prefix armed: next key is rotide");
					return;
				}
				struct editorTerminalPane *terminal =
				        (struct editorTerminalPane *)
				                E.focused_leaf->as.leaf.kind_state;
				(void)editorTerminalPaneSendKey(terminal, c);
				return;
			}
		}
		if (editorKeymapLookupAction(&E.keymap, c, &action)) {
			int mapped_effects = DISPATCH_KEYPRESS_EFFECT_NONE;
			mapped_action = 1;
			if (dispatchProcessMappedAction(action, &mapped_effects)) {
				return;
			}
			effects |= mapped_effects;
		} else if (editorByteShouldInsertAsText(c)) {
			if (!editorDrawerIsCollapsed() && editorFileSearchIsActive()) {
				if (editorFileSearchAppendByte(c)) {
					(void)editorFileSearchPreviewSelection();
				}
			} else if (!editorDrawerIsCollapsed() && editorProjectSearchIsActive()) {
				if (editorProjectSearchAppendByte(c)) {
					(void)editorProjectSearchPreviewSelection();
				}
			} else if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
				if (editorActiveTabIsReadOnly()) {
					editorSetStatusMsg(editorActiveTabIsUnsupportedFile()
					                           ? "File is unsupported"
					                           : "Task log is read-only");
					goto done;
				}
				if (E.column_select_active && c >= 0x20 && c < 0x7f) {
					dispatchPinActivePreviewForEdit();
					editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
					int dirty_before = E.dirty;
					editorColumnSelectionInsertChar(c);
					editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT,
					                        E.dirty != dirty_before);
					effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (c == '\t' && dispatchIndentSelection()) {
					effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (dispatchReplaceSelectionWithChar(c)) {
					effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (editorTrySkipOverClosingPair(c)) {
					effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (editorTryAutoClosePair(c)) {
					effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else {
					dispatchClearSelectionMode();
					dispatchPinActivePreviewForEdit();
					editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
					int dirty_before = E.dirty;
					editorInsertChar(c);
					editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT,
					                        E.dirty != dirty_before);
					editorAutocompleteOnCharInserted(c);
					effects |= DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				}
			}
		}
	}

done:
	editorFileTabActionsAfterKeypress(mapped_action, action);
	if ((effects & DISPATCH_KEYPRESS_EFFECT_CURSOR_OR_EDIT) != 0) {
		editorViewportEnsureCursorVisible();
	}

	editorRecoveryMaybeAutosaveOnActivity();
}
