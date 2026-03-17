#include "input.h"

#include "alloc.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "keymap.h"
#include "output.h"
#include "terminal.h"

/*** Input ***/

static int quit_confirmed = 0;
typedef void (*editorPromptCallback)(const char *query, int key);
enum { MOUSE_WHEEL_SCROLL_LINES = 3 };

static size_t editorPromptPrevDeleteIdx(const char *buf, size_t buflen) {
	if (buflen == 0) {
		return 0;
	}

	size_t seq_start = buflen - 1;
	while (seq_start > 0 &&
			editorIsUtf8ContinuationByte((unsigned char)buf[seq_start])) {
		seq_start--;
	}

	unsigned int cp = 0;
	int seq_len = editorUtf8DecodeCodepoint(&buf[seq_start], (int)(buflen - seq_start), &cp);
	if (seq_len > 1 && seq_start + (size_t)seq_len == buflen) {
		return seq_start;
	}

	return buflen - 1;
}

static void editorSetQuitConfirmStatus(void) {
	char quit_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_QUIT, quit_binding,
				sizeof(quit_binding))) {
		editorSetStatusMsg("File has unsaved changes. Press %s again to quit", quit_binding);
		return;
	}

	editorSetStatusMsg("File has unsaved changes. Press quit key again to quit");
}

static void quit(void) {
	if (E.dirty && !quit_confirmed) {
		editorSetQuitConfirmStatus();
		quit_confirmed = 1;
		return;
	}

	editorRestoreTerminal();
	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_SUCCESS);
}

static void editorAlignCursorWithRowEnd(void) {
	int rowlen = 0;
	if (E.numrows > E.cy) {
		struct erow *row = &E.rows[E.cy];
		// Never leave the cursor in the middle of a UTF-8 grapheme.
		rowlen = row->size;
		E.cx = editorRowClampCxToClusterBoundary(row, E.cx);
	}
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

static void editorClearActiveSearchMatch(void) {
	E.search_match_row = -1;
	E.search_match_start = 0;
	E.search_match_len = 0;
}

static void editorClearSearchState(void) {
	free(E.search_query);
	E.search_query = NULL;
	E.search_direction = 1;
	editorClearActiveSearchMatch();
}

static void editorClearSelectionMode(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_cx = 0;
	E.selection_anchor_cy = 0;
}

static void editorToggleSelectionMode(void) {
	if (E.selection_mode_active) {
		editorClearSelectionMode();
		return;
	}

	editorAlignCursorWithRowEnd();
	E.selection_mode_active = 1;
	E.selection_anchor_cx = E.cx;
	E.selection_anchor_cy = E.cy;
}

static int editorCopyRangeToClipboard(const struct editorSelectionRange *range, size_t *copied_len_out) {
	char *copied = NULL;
	size_t copied_len = 0;
	int extracted = editorExtractRangeText(range, &copied, &copied_len);
	if (extracted <= 0) {
		return extracted;
	}

	if (!editorClipboardSet(copied, copied_len)) {
		free(copied);
		return -1;
	}
	free(copied);

	if (copied_len_out != NULL) {
		*copied_len_out = copied_len;
	}
	return 1;
}

static void editorCopySelection(void) {
	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		editorSetStatusMsg("No selection");
		return;
	}

	size_t copied_len = 0;
	int copied = editorCopyRangeToClipboard(&range, &copied_len);
	if (copied <= 0) {
		if (copied == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	editorClearSelectionMode();
	editorSetStatusMsg("Copied %zu bytes", copied_len);
}

static void editorCutSelection(void) {
	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		editorSetStatusMsg("No selection");
		return;
	}

	size_t copied_len = 0;
	int copied = editorCopyRangeToClipboard(&range, &copied_len);
	if (copied <= 0) {
		if (copied == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	int deleted = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	if (deleted <= 0) {
		if (deleted == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	editorClearSelectionMode();
	editorSetStatusMsg("Cut %zu bytes", copied_len);
}

static void editorDeleteSelection(void) {
	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		editorSetStatusMsg("No selection");
		return;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	int deleted = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	if (deleted <= 0) {
		if (deleted == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	editorClearSelectionMode();
}

static void editorPasteClipboard(void) {
	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	if (clip_len <= 0) {
		editorSetStatusMsg("Clipboard is empty");
		return;
	}

	editorClearSelectionMode();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	size_t inserted = 0;
	int failed = 0;
	for (size_t i = 0; i < clip_len; i++) {
		int step_dirty_before = E.dirty;
		if (clip[i] == '\n') {
			editorInsertNewline();
		} else {
			editorInsertChar((unsigned char)clip[i]);
		}
		if (E.dirty == step_dirty_before) {
			failed = 1;
			break;
		}
		inserted++;
	}

	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();

	if (!failed) {
		editorSetStatusMsg("Pasted %zu bytes", inserted);
	}
}

static int editorFindForwardInRow(const struct erow *row, const char *query, int from_idx,
		int *out_idx) {
	if (from_idx < 0) {
		from_idx = 0;
	}
	if (from_idx > row->size) {
		return 0;
	}

	const char *match = strstr(&row->chars[from_idx], query);
	if (match == NULL) {
		return 0;
	}
	*out_idx = (int)(match - row->chars);
	return 1;
}

static int editorFindLastInRowBefore(const struct erow *row, const char *query, int before_idx,
		int *out_idx) {
	if (before_idx <= 0) {
		return 0;
	}

	int last = -1;
	const char *scan = row->chars;
	while (1) {
		const char *match = strstr(scan, query);
		if (match == NULL) {
			break;
		}

		int idx = (int)(match - row->chars);
		if (idx >= before_idx) {
			break;
		}

		last = idx;
		scan = match + 1;
	}

	if (last == -1) {
		return 0;
	}

	*out_idx = last;
	return 1;
}

static int editorFindLastInRowAfter(const struct erow *row, const char *query, int after_idx,
		int *out_idx) {
	int last = -1;
	const char *scan = row->chars;
	while (1) {
		const char *match = strstr(scan, query);
		if (match == NULL) {
			break;
		}

		int idx = (int)(match - row->chars);
		if (idx > after_idx) {
			last = idx;
		}

		scan = match + 1;
	}

	if (last == -1) {
		return 0;
	}

	*out_idx = last;
	return 1;
}

static int editorFindForward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col) {
	if (E.numrows == 0) {
		return 0;
	}
	if (start_row < 0 || start_row >= E.numrows) {
		start_row = 0;
		start_col = -1;
	}

	int col = 0;
	if (editorFindForwardInRow(&E.rows[start_row], query, start_col + 1, &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	for (int offset = 1; offset < E.numrows; offset++) {
		int row = (start_row + offset) % E.numrows;
		if (editorFindForwardInRow(&E.rows[row], query, 0, &col)) {
			*out_row = row;
			*out_col = col;
			return 1;
		}
	}

	if (editorFindForwardInRow(&E.rows[start_row], query, 0, &col) && col <= start_col) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	return 0;
}

static int editorFindBackward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col) {
	if (E.numrows == 0) {
		return 0;
	}
	if (start_row < 0 || start_row >= E.numrows) {
		start_row = E.numrows - 1;
		start_col = E.rows[start_row].size;
	}

	int col = 0;
	if (editorFindLastInRowBefore(&E.rows[start_row], query, start_col, &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	for (int offset = 1; offset < E.numrows; offset++) {
		int row = (start_row - offset + E.numrows) % E.numrows;
		if (editorFindLastInRowBefore(&E.rows[row], query, E.rows[row].size + 1, &col)) {
			*out_row = row;
			*out_col = col;
			return 1;
		}
	}

	if (editorFindLastInRowAfter(&E.rows[start_row], query, start_col, &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	return 0;
}

static void editorMoveCursorToSearchMatch(int row_idx, int match_col, int match_len) {
	E.search_match_row = row_idx;
	E.search_match_start = match_col;
	E.search_match_len = match_len;
	E.cy = row_idx;

	struct erow *row = &E.rows[row_idx];
	int cx = editorRowClampCxToCharBoundary(row, match_col);
	E.cx = editorRowClampCxToClusterBoundary(row, cx);
	editorAlignCursorWithRowEnd();
}

static void editorRestoreCursorToSavedSearchPosition(void) {
	E.cy = E.search_saved_cy;
	E.cx = E.search_saved_cx;
	editorAlignCursorWithRowEnd();
}

static void editorFindCallback(const char *query, int key) {
	if (key == '\x1b') {
		editorRestoreCursorToSavedSearchPosition();
		editorClearSearchState();
		return;
	}
	if (key == '\r') {
		return;
	}
	if (query[0] == '\0') {
		editorRestoreCursorToSavedSearchPosition();
		editorClearActiveSearchMatch();
		E.search_direction = 1;
		return;
	}

	int match_row = -1;
	int match_col = -1;
	int direction = 1;
	int start_row = 0;
	int start_col = -1;

	if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
		if (E.search_match_row != -1) {
			start_row = E.search_match_row;
			start_col = E.search_match_start;
		} else {
			start_row = E.search_saved_cy;
			start_col = E.search_saved_cx - 1;
		}
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
		if (E.search_match_row != -1) {
			start_row = E.search_match_row;
			start_col = E.search_match_start;
		} else {
			start_row = E.search_saved_cy;
			start_col = E.search_saved_cx;
		}
	}

	E.search_direction = direction;
	int found = direction == 1 ?
			editorFindForward(query, start_row, start_col, &match_row, &match_col) :
			editorFindBackward(query, start_row, start_col, &match_row, &match_col);

	if (!found) {
		editorRestoreCursorToSavedSearchPosition();
		editorClearActiveSearchMatch();
		return;
	}

	editorMoveCursorToSearchMatch(match_row, match_col, (int)strlen(query));
}

static char *editorPromptWithCallback(const char *prompt, int allow_empty,
		editorPromptCallback callback) {
	size_t bufmax = 128;
	char *buf = editorMalloc(bufmax);
	if (buf == NULL) {
		editorSetStatusMsg("Out of memory");
		return NULL;
	}

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
			editorSetStatusMsg(prompt, buf);
			editorRefreshScreen();

			int c = editorReadKey();
			if (c == RESIZE_EVENT) {
				(void)editorRefreshWindowSize();
				continue;
			}
			// Prompt editing is keyboard-only; ignore mouse packets without invoking callbacks.
			if (c == MOUSE_EVENT) {
				continue;
		}
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) {
				buflen = editorPromptPrevDeleteIdx(buf, buflen);
				buf[buflen] = '\0';
			}
		} else if (c == '\x1b') {
			if (callback != NULL) {
				callback(buf, c);
			}
			editorSetStatusMsg("");
			free(buf);
			return NULL;
		} else if (c == '\r' && (allow_empty || buflen != 0)) {
			if (callback != NULL) {
				callback(buf, c);
			}
			editorSetStatusMsg("");
			return buf;
		} else if (c >= CHAR_MIN && c <= CHAR_MAX) {
			unsigned char byte = (unsigned char)c;
			// Keep non-ASCII bytes verbatim; only filter ASCII control bytes.
			if (byte >= 0x80 || !iscntrl(byte)) {
				if (buflen == bufmax - 1) {
					size_t new_bufmax = bufmax * 2;
					char *new_buf = editorRealloc(buf, new_bufmax);
					if (new_buf == NULL) {
						free(buf);
						editorSetStatusMsg("Out of memory");
						return NULL;
					}
					buf = new_buf;
					bufmax = new_bufmax;
				}
				buf[buflen] = (char)byte;
				buflen++;
				buf[buflen] = '\0';
			}
		}

		if (callback != NULL) {
			callback(buf, c);
		}
	}
}

char *editorPrompt(const char *prompt) {
	return editorPromptWithCallback(prompt, 0, NULL);
}

static void editorFind(void) {
	E.search_saved_cx = E.cx;
	E.search_saved_cy = E.cy;
	E.search_direction = 1;
	editorClearActiveSearchMatch();

	char *query = editorPromptWithCallback(
			"Search: %s (Use ESC/Arrows/Enter)", 1, editorFindCallback);
	if (query == NULL) {
		return;
	}

	free(E.search_query);
	E.search_query = query;
	if (E.search_match_row == -1) {
		editorSetStatusMsg("No matches for \"%s\"", query);
	}
}

static int editorParsePositiveLineNumber(const char *query, long *out_line) {
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

static void editorGoToLine(void) {
	char *query = editorPrompt("Go to line: %s");
	if (query == NULL) {
		return;
	}

	long line = 0;
	int valid = editorParsePositiveLineNumber(query, &line);
	free(query);
	if (!valid) {
		editorSetStatusMsg("Invalid line number");
		return;
	}

	if (E.numrows == 0) {
		E.cy = 0;
		E.cx = 0;
		editorSetStatusMsg("Buffer is empty");
		return;
	}

	if (line > E.numrows) {
		line = E.numrows;
	}

	E.cy = (int)(line - 1);
	E.cx = 0;
	editorAlignCursorWithRowEnd();
}

static void editorMoveCursor(int k) {
	int target_rx = 0;
	if ((k == ARROW_UP || k == ARROW_DOWN) && E.cy < E.numrows) {
		target_rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}

	switch (k) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				if (E.cy < E.numrows) {
					// Step by grapheme cluster instead of byte index.
					E.cx = editorRowPrevClusterIdx(&E.rows[E.cy], E.cx);
				} else {
					E.cx--;
				}
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.rows[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (E.numrows > E.cy && E.cx < E.rows[E.cy].size) {
				// Step by grapheme cluster instead of byte index.
				E.cx = editorRowNextClusterIdx(&E.rows[E.cy], E.cx);
			} else if (E.numrows > E.cy && E.cx == E.rows[E.cy].size) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) {
				E.cy++;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
	}

	if ((k == ARROW_UP || k == ARROW_DOWN) && E.cy < E.numrows) {
		E.cx = editorRowRxToCx(&E.rows[E.cy], target_rx);
	}

	editorAlignCursorWithRowEnd();
}

static int editorResolveMouseToBufferPosition(const struct editorMouseEvent *event,
		int clamp_to_viewport, int *row_idx_out, int *cx_out) {
	if (event == NULL || row_idx_out == NULL || cx_out == NULL || E.numrows == 0) {
		return 0;
	}

	// SGR mouse coordinates are terminal-absolute and 1-based.
	int mouse_row = event->y - 1;
	int mouse_col = event->x - 1;
	if (clamp_to_viewport) {
		if (E.window_rows <= 0 || E.window_cols <= 0) {
			return 0;
		}
		if (mouse_row < 0) {
			mouse_row = 0;
		}
		if (mouse_row >= E.window_rows) {
			mouse_row = E.window_rows - 1;
		}
		if (mouse_col < 0) {
			mouse_col = 0;
		}
		if (mouse_col >= E.window_cols) {
			mouse_col = E.window_cols - 1;
		}
	} else {
		// Ignore clicks outside the text viewport (status/message bars are below window_rows).
		if (mouse_row < 0 || mouse_row >= E.window_rows || mouse_col < 0) {
			return 0;
		}
	}

	int row_idx = E.rowoff + mouse_row;
	if (clamp_to_viewport) {
		if (row_idx < 0) {
			row_idx = 0;
		}
		if (row_idx >= E.numrows) {
			row_idx = E.numrows - 1;
		}
	} else {
		// Ignore filler rows beyond the end of file.
		if (row_idx < 0 || row_idx >= E.numrows) {
			return 0;
		}
	}

	int target_rx = E.coloff + mouse_col;
	if (target_rx < 0) {
		target_rx = 0;
	}

	// Convert rendered column -> buffer byte index while respecting grapheme boundaries.
	*row_idx_out = row_idx;
	*cx_out = editorRowRxToCx(&E.rows[row_idx], target_rx);
	return 1;
}

static int editorMoveCursorToMouse(const struct editorMouseEvent *event, int clamp_to_viewport) {
	int row_idx = 0;
	int cx = 0;
	if (!editorResolveMouseToBufferPosition(event, clamp_to_viewport, &row_idx, &cx)) {
		return 0;
	}

	E.cy = row_idx;
	E.cx = cx;
	editorAlignCursorWithRowEnd();
	return 1;
}

static void editorHandleMouseLeftPress(const struct editorMouseEvent *event) {
	if (!editorMoveCursorToMouse(event, 0)) {
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return;
	}

	E.mouse_left_button_down = 1;
	E.mouse_drag_anchor_cx = E.cx;
	E.mouse_drag_anchor_cy = E.cy;
	E.mouse_drag_started = 0;
}

static void editorHandleMouseLeftDrag(const struct editorMouseEvent *event) {
	if (!E.mouse_left_button_down) {
		return;
	}
	if (!editorMoveCursorToMouse(event, 1)) {
		return;
	}

	if (!E.mouse_drag_started) {
		// A new drag always starts a fresh selection anchored at the initial press point.
		E.selection_mode_active = 1;
		E.selection_anchor_cx = E.mouse_drag_anchor_cx;
		E.selection_anchor_cy = E.mouse_drag_anchor_cy;
		E.mouse_drag_started = 1;
	}
}

static void editorHandleMouseLeftRelease(void) {
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
}

static void editorHandleMouseWheel(const struct editorMouseEvent *event) {
	int direction = event->kind == EDITOR_MOUSE_EVENT_WHEEL_UP ? ARROW_UP : ARROW_DOWN;
	// Reuse arrow movement so cursor clamping/desired-column rules stay centralized.
	for (int i = 0; i < MOUSE_WHEEL_SCROLL_LINES; i++) {
		editorMoveCursor(direction);
	}
}

static void editorHandleMouseEvent(void) {
	struct editorMouseEvent event;
	// terminal.c queues one decoded event per MOUSE_EVENT keycode.
	if (!editorConsumeMouseEvent(&event)) {
		return;
	}

	switch (event.kind) {
		case EDITOR_MOUSE_EVENT_LEFT_PRESS:
			editorHandleMouseLeftPress(&event);
			break;
		case EDITOR_MOUSE_EVENT_LEFT_DRAG:
			editorHandleMouseLeftDrag(&event);
			break;
		case EDITOR_MOUSE_EVENT_LEFT_RELEASE:
			editorHandleMouseLeftRelease();
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_UP:
		case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
			editorHandleMouseWheel(&event);
			break;
		default:
			break;
	}
}

static int editorProcessMappedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_QUIT:
			editorHistoryBreakGroup();
			quit();
			return 1;
		case EDITOR_ACTION_SAVE:
			editorHistoryBreakGroup();
			editorSave();
			return 0;
		case EDITOR_ACTION_FIND:
			editorHistoryBreakGroup();
			editorFind();
			return 0;
		case EDITOR_ACTION_GOTO_LINE:
			editorHistoryBreakGroup();
			editorGoToLine();
			return 0;
		case EDITOR_ACTION_TOGGLE_SELECTION:
			editorHistoryBreakGroup();
			editorToggleSelectionMode();
			return 0;
		case EDITOR_ACTION_COPY_SELECTION:
			editorHistoryBreakGroup();
			editorCopySelection();
			return 0;
		case EDITOR_ACTION_CUT_SELECTION:
			editorHistoryBreakGroup();
			editorCutSelection();
			return 0;
		case EDITOR_ACTION_DELETE_SELECTION:
			editorHistoryBreakGroup();
			editorDeleteSelection();
			return 0;
		case EDITOR_ACTION_PASTE:
			editorHistoryBreakGroup();
			editorPasteClipboard();
			return 0;
		case EDITOR_ACTION_UNDO:
			editorHistoryBreakGroup();
			if (editorUndo() == 1) {
				editorClearSearchState();
			}
			return 0;
		case EDITOR_ACTION_REDO:
			editorHistoryBreakGroup();
			if (editorRedo() == 1) {
				editorClearSearchState();
			}
			return 0;
		case EDITOR_ACTION_MOVE_HOME:
			editorHistoryBreakGroup();
			E.cx = 0;
			return 0;
		case EDITOR_ACTION_MOVE_END:
			editorHistoryBreakGroup();
			if (E.cy < E.numrows) {
				E.cx = E.rows[E.cy].size;
			}
			return 0;
		case EDITOR_ACTION_PAGE_UP:
			editorHistoryBreakGroup();
			E.cy = E.rowoff;
			// Reuse arrow movement so cursor clamping behavior stays consistent.
			for (int i = 0; i < E.window_rows; i++) {
				editorMoveCursor(ARROW_UP);
			}
			return 0;
		case EDITOR_ACTION_PAGE_DOWN:
			editorHistoryBreakGroup();
			E.cy = E.rowoff + E.window_rows - 1;
			if (E.cy > E.numrows) {
				E.cy = E.numrows;
			}
			// Reuse arrow movement so cursor clamping behavior stays consistent.
			for (int i = 0; i < E.window_rows; i++) {
				editorMoveCursor(ARROW_DOWN);
			}
			return 0;
		case EDITOR_ACTION_MOVE_UP:
			editorHistoryBreakGroup();
			editorMoveCursor(ARROW_UP);
			return 0;
		case EDITOR_ACTION_MOVE_DOWN:
			editorHistoryBreakGroup();
			editorMoveCursor(ARROW_DOWN);
			return 0;
		case EDITOR_ACTION_MOVE_LEFT:
			editorHistoryBreakGroup();
			editorMoveCursor(ARROW_LEFT);
			return 0;
		case EDITOR_ACTION_MOVE_RIGHT:
			editorHistoryBreakGroup();
			editorMoveCursor(ARROW_RIGHT);
			return 0;
		case EDITOR_ACTION_NEWLINE: {
			editorClearSelectionMode();
			editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
			int dirty_before = E.dirty;
			editorInsertNewline();
			editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
			return 0;
		}
		case EDITOR_ACTION_ESCAPE:
			// In normal editor mode Escape only clears transient selection state; quit is configurable.
			editorHistoryBreakGroup();
			editorClearSelectionMode();
			return 0;
		case EDITOR_ACTION_REDRAW:
			editorHistoryBreakGroup();
			return 0;
		case EDITOR_ACTION_DELETE_CHAR: {
			editorClearSelectionMode();
			editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
			int dirty_before = E.dirty;
			// DEL deletes under cursor; editorDelChar() implements backspace semantics.
			editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
			return 0;
		}
		case EDITOR_ACTION_BACKSPACE: {
			editorClearSelectionMode();
			editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
			int dirty_before = E.dirty;
			editorDelChar();
			editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
			return 0;
		}
		case EDITOR_ACTION_COUNT:
		default:
			return 0;
	}
}

void editorProcessKeypress(void) {
	int c = editorReadKey();

	if (c == RESIZE_EVENT) {
		(void)editorRefreshWindowSize();
		return;
	}

	if (c == MOUSE_EVENT) {
		// Mouse input can move cursor/selection, but it should not create edit history entries.
		editorHistoryBreakGroup();
		editorHandleMouseEvent();
	} else {
		enum editorAction action = EDITOR_ACTION_COUNT;
		if (editorKeymapLookupAction(&E.keymap, c, &action)) {
			if (editorProcessMappedAction(action)) {
				return;
			}
		} else {
			editorClearSelectionMode();
			editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
			int dirty_before = E.dirty;
			editorInsertChar(c);
			editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
		}
	}

	quit_confirmed = 0;
}
