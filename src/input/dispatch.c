#include "input/dispatch.h"

#include "config/keymap.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "language/lsp.h"
#include "language/syntax_worker.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "support/terminal.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/task.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "text/row.h"
#include "text/utf8.h"

/*** Input ***/

static int quit_confirmed = 0;
static int quit_task_confirmed = 0;
typedef void (*editorPromptCallback)(const char *query, int key);
static char *editorPromptWithCallback(const char *prompt, int allow_empty,
		editorPromptCallback callback);
enum {
	MOUSE_WHEEL_SCROLL_LINES = 3,
	MOUSE_WHEEL_SCROLL_COLS = 3,
	DRAWER_DOUBLE_CLICK_THRESHOLD_MS = 400,
	DRAWER_RESIZE_STEP = 1,
	KEYBOARD_SCROLL_COLS = 3
};

enum editorKeypressEffect {
	EDITOR_KEYPRESS_EFFECT_NONE = 0,
	EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL = 1 << 0,
	EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT = 1 << 1
};

static long long editorMonotonicMillis(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static void editorResetDrawerClickTracking(void) {
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
}

static void editorSetDrawerCollapseStatus(int collapsed) {
	editorSetStatusMsg(collapsed ? "Drawer collapsed" : "Drawer expanded");
}

static void editorExpandDrawerForFocus(void) {
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
}

static void editorToggleDrawerFocus(void) {
	if (E.pane_focus == EDITOR_PANE_DRAWER) {
		E.pane_focus = EDITOR_PANE_TEXT;
		return;
	}
	editorExpandDrawerForFocus();
}

static int editorMouseIsOverDrawer(const struct editorMouseEvent *event) {
	if (event == NULL) {
		return 0;
	}

	int mouse_col = event->x - 1;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int drawer_view_rows = E.window_rows + 1;
	int drawer_row = event->y - 1;
	return drawer_row >= 0 && drawer_row < drawer_view_rows &&
			mouse_col >= 0 && mouse_col < drawer_cols;
}

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

static int editorByteShouldInsertAsText(int c) {
	if (c < CHAR_MIN || c > CHAR_MAX) {
		return 0;
	}

	unsigned char byte = (unsigned char)c;
	/* Keep non-ASCII bytes verbatim and allow literal Tab; filter other ASCII controls. */
	return byte == '\t' || byte >= 0x80 || !iscntrl(byte);
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

static void editorSetQuitTaskConfirmStatus(void) {
	char quit_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_QUIT, quit_binding,
				sizeof(quit_binding))) {
		editorSetStatusMsg("Task is still running. Press %s again to terminate it and quit",
				quit_binding);
		return;
	}
	editorSetStatusMsg("Task is still running. Press quit key again to terminate it and quit");
}

static void editorSetCloseTabConfirmStatus(void) {
	char close_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_CLOSE_TAB, close_binding,
				sizeof(close_binding))) {
		editorSetStatusMsg("Tab has unsaved changes. Press %s again to close tab", close_binding);
		return;
	}

	editorSetStatusMsg("Tab has unsaved changes. Press close key again to close tab");
}

static void editorSetCloseTaskConfirmStatus(void) {
	char close_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_CLOSE_TAB, close_binding,
				sizeof(close_binding))) {
		editorSetStatusMsg("Task is still running. Press %s again to terminate it and close tab",
				close_binding);
		return;
	}
	editorSetStatusMsg("Task is still running. Press close key again to terminate it and close tab");
}

static void quit(void) {
	if (editorTaskIsRunning() && !quit_task_confirmed) {
		editorSetQuitTaskConfirmStatus();
		quit_task_confirmed = 1;
		return;
	}
	if (editorTaskIsRunning()) {
		(void)editorTaskTerminate();
		quit_task_confirmed = 0;
	}

	if (editorTabAnyDirty() && !quit_confirmed) {
		editorSetQuitConfirmStatus();
		quit_confirmed = 1;
		return;
	}

	editorLspShutdown();
	editorSyntaxBackgroundStop();
	editorRecoveryCleanupOnCleanExit();
	editorRestoreTerminal();
	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_SUCCESS);
}

static void editorCloseTab(void) {
	if (editorActiveTaskTabIsRunning() && !E.close_confirmed) {
		editorSetCloseTaskConfirmStatus();
		E.close_confirmed = 1;
		return;
	}
	if (editorActiveTaskTabIsRunning()) {
		(void)editorTaskTerminate();
		E.close_confirmed = 0;
	}

	if (E.dirty && !E.close_confirmed) {
		editorSetCloseTabConfirmStatus();
		E.close_confirmed = 1;
		return;
	}

	if (editorTabCloseActive()) {
		E.close_confirmed = 0;
	}
}

static void editorExitOnInputShutdown(void) {
	if (editorTaskIsRunning()) {
		(void)editorTaskTerminate();
	}
	editorLspShutdown();
	editorRestoreTerminal();
	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_FAILURE);
}

static int editorSetCursorFromOffset(size_t offset) {
	int cy = 0;
	int cx = 0;
	size_t normalized_offset = 0;

	if (!editorBufferOffsetToPos(offset, &cy, &cx)) {
		return 0;
	}
	if (cy < E.numrows) {
		struct erow *row = &E.rows[cy];
		cx = editorRowClampCxToCharBoundary(row, cx);
		cx = editorRowClampCxToClusterBoundary(row, cx);
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

static int editorSetCursorFromPosition(int cy, int cx) {
	size_t offset = 0;

	if (cy < 0) {
		cy = 0;
	}
	if (cy > E.numrows) {
		cy = E.numrows;
	}
	if (cy < E.numrows) {
		struct erow *row = &E.rows[cy];
		cx = editorRowClampCxToCharBoundary(row, cx);
		cx = editorRowClampCxToClusterBoundary(row, cx);
	} else {
		cx = 0;
	}
	if (!editorBufferPosToOffset(cy, cx, &offset)) {
		return 0;
	}
	return editorSetCursorFromOffset(offset);
}

static void editorAlignCursorWithRowEnd(void) {
	if (editorSetCursorFromPosition(E.cy, E.cx)) {
		return;
	}

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
	if (!editorBufferPosToOffset(E.cy, E.cx, &E.cursor_offset)) {
		E.cursor_offset = 0;
	}
}

static void editorClearActiveSearchMatch(void) {
	E.search_match_offset = 0;
	E.search_match_len = 0;
}

static void editorClearSearchState(void) {
	free(E.search_query);
	E.search_query = NULL;
	E.search_direction = 1;
	editorClearActiveSearchMatch();
}

static int editorSearchMatchPosition(int *row_out, int *col_out) {
	if (E.search_match_len <= 0 || row_out == NULL || col_out == NULL) {
		return 0;
	}
	return editorBufferOffsetToPos(E.search_match_offset, row_out, col_out);
}

static void editorClearSelectionMode(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
}

int editorPromptYesNo(const char *prompt) {
	char *response = editorPromptWithCallback(prompt, 1, NULL);
	int accepted = 0;
	if (response == NULL) {
		return 0;
	}
	if (strcasecmp(response, "y") == 0 || strcasecmp(response, "yes") == 0) {
		accepted = 1;
	}
	free(response);
	return accepted;
}

enum editorGoToDefinitionInstallFamily {
	EDITOR_GOTO_DEF_INSTALL_NONE = 0,
	EDITOR_GOTO_DEF_INSTALL_GOPLS,
	EDITOR_GOTO_DEF_INSTALL_CLANGD,
	EDITOR_GOTO_DEF_INSTALL_JAVASCRIPT,
	EDITOR_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS
};

static int editorGoToDefinitionSupportedLanguage(enum editorSyntaxLanguage language) {
	if (editorLspFileSupportsDefinition(E.filename, language)) {
		return 1;
	}
	return language == EDITOR_SYNTAX_GO || language == EDITOR_SYNTAX_C ||
			language == EDITOR_SYNTAX_HTML || language == EDITOR_SYNTAX_CSS ||
			language == EDITOR_SYNTAX_JAVASCRIPT;
}

static int editorGoToDefinitionEnabledForLanguage(void) {
	return editorLspFileEnabled(E.filename, E.syntax_language);
}

static const char *editorGoToDefinitionLanguageLabel(void) {
	const char *label = editorLspLanguageLabelForFile(E.filename, E.syntax_language);
	if (label != NULL) {
		return label;
	}
	if (E.syntax_language == EDITOR_SYNTAX_GO) {
		return "Go";
	}
	if (E.syntax_language == EDITOR_SYNTAX_C) {
		return "C/C++";
	}
	if (E.syntax_language == EDITOR_SYNTAX_HTML) {
		return "HTML";
	}
	if (E.syntax_language == EDITOR_SYNTAX_CSS) {
		return "CSS/SCSS";
	}
	return NULL;
}

static const char *editorGoToDefinitionServerName(void) {
	return editorLspServerNameForFile(E.filename, E.syntax_language);
}

static const char *editorGoToDefinitionCommand(void) {
	return editorLspCommandForFile(E.filename, E.syntax_language);
}

static const char *editorGoToDefinitionCommandSettingName(void) {
	return editorLspCommandSettingNameForFile(E.filename, E.syntax_language);
}

static enum editorGoToDefinitionInstallFamily editorGoToDefinitionInstallFamilyForLanguage(void) {
	const char *server_name = editorGoToDefinitionServerName();
	if (server_name != NULL && strcmp(server_name, "gopls") == 0) {
		return EDITOR_GOTO_DEF_INSTALL_GOPLS;
	}
	if (server_name != NULL && strcmp(server_name, "clangd") == 0) {
		return EDITOR_GOTO_DEF_INSTALL_CLANGD;
	}
	if (server_name != NULL && strcmp(server_name, "typescript-language-server") == 0) {
		return EDITOR_GOTO_DEF_INSTALL_JAVASCRIPT;
	}
	if (editorLspUsesSharedVscodeInstallPrompt(E.filename, E.syntax_language)) {
		return EDITOR_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS;
	}
	return EDITOR_GOTO_DEF_INSTALL_NONE;
}

static void editorPromptInstallJavascriptLanguageServer(void) {
	if (!editorPromptYesNo("typescript-language-server not found. Install now? [y/N] %s")) {
		editorSetStatusMsg("typescript-language-server not installed");
		return;
	}
	if (!editorTaskStart("Task: Install typescript-language-server",
				E.lsp_javascript_install_command,
				"typescript-language-server installed. Retry Ctrl-O",
				"typescript-language-server install failed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("Unable to start typescript-language-server install");
		}
	}
}

static void editorPromptInstallSharedVscodeLanguageServers(void) {
	if (!editorPromptYesNo("vscode-langservers-extracted not found. Install now? [y/N] %s")) {
		editorSetStatusMsg("vscode-langservers-extracted not installed");
		return;
	}
	if (!editorTaskStart("Task: Install vscode-langservers-extracted",
				E.lsp_vscode_langservers_install_command,
				"vscode-langservers-extracted installed. Retry Ctrl-O",
				"vscode-langservers-extracted install failed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("Unable to start vscode-langservers-extracted install");
		}
	}
}

static void editorMaybePromptInstallLanguageServer(void) {
	if (editorLspLastStartupFailureReason() != EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
		return;
	}
	switch (editorGoToDefinitionInstallFamilyForLanguage()) {
		case EDITOR_GOTO_DEF_INSTALL_GOPLS:
			if (!editorPromptYesNo("gopls not found. Install now? [y/N] %s")) {
				editorSetStatusMsg("gopls not installed");
				return;
			}
			if (!editorTaskStart("Task: Install gopls", E.lsp_gopls_install_command,
						"gopls installed. Retry Ctrl-O",
						"gopls install failed; see task log")) {
				if (E.statusmsg[0] == '\0') {
					editorSetStatusMsg("Unable to start gopls install");
				}
			}
			return;
		case EDITOR_GOTO_DEF_INSTALL_CLANGD: {
			static const char message[] =
					"clangd was not found on PATH.\n"
					"\n"
					"Install instructions:\n"
					"https://clangd.llvm.org/installation\n"
					"\n"
					"clangd usually needs a compile_commands.json compilation database for C/C++ projects.\n"
					"\n"
					"Create compile_commands.json with CMake:\n"
					"- cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n"
					"- use build/compile_commands.json, or copy/symlink it into the project root\n"
					"\n"
					"Create compile_commands.json with Bear:\n"
					"- bear -- make\n"
					"- or bear -- <your normal build command>\n"
					"- this is often a good fit for pure C projects that already build without CMake\n"
					"\n"
					"After installing clangd and setting up compile_commands.json:\n"
					"- retry Ctrl-O or Ctrl + left click\n"
					"- set [lsp].clangd_command in .rotide.toml if clangd is installed in a custom location\n";
			if (!editorPromptYesNo("clangd not found. Show install instructions? [y/N] %s")) {
				editorSetStatusMsg("clangd not installed");
				return;
			}
			if (!editorTaskShowMessage("Task: Install clangd", message,
						"clangd not installed; see task log")) {
				if (E.statusmsg[0] == '\0') {
					editorSetStatusMsg("clangd not installed");
				}
			}
			return;
		}
		case EDITOR_GOTO_DEF_INSTALL_JAVASCRIPT:
			editorPromptInstallJavascriptLanguageServer();
			return;
		case EDITOR_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS:
			editorPromptInstallSharedVscodeLanguageServers();
			return;
		default:
			return;
	}
}

static void editorPinActivePreviewForEdit(void) {
	if (E.pane_focus != EDITOR_PANE_DRAWER) {
		editorTabPinActivePreview();
	}
}

static int editorActionMutatesReadOnlyBuffer(enum editorAction action) {
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
			return 1;
		default:
			return 0;
	}
}

static void editorToggleSelectionMode(void) {
	if (E.selection_mode_active) {
		editorClearSelectionMode();
		return;
	}

	editorAlignCursorWithRowEnd();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = E.cursor_offset;
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

static void editorMoveCurrentLine(int direction) {
	int cur = E.cy;
	int other = cur + direction;

	if (cur < 0 || cur >= E.numrows || other < 0 || other >= E.numrows) {
		return;
	}

	int first = direction < 0 ? other : cur;
	int second = direction < 0 ? cur : other;

	size_t first_start = 0, first_end = 0;
	size_t second_start = 0, second_end = 0;
	if (!editorBufferLineByteRange(first, &first_start, &first_end) ||
			!editorBufferLineByteRange(second, &second_start, &second_end)) {
		return;
	}

	int first_len = E.rows[first].size;
	int second_len = E.rows[second].size;
	const char *first_chars = E.rows[first].chars;
	const char *second_chars = E.rows[second].chars;

	// new_text = second_content + '\n' + first_content
	size_t new_len = (size_t)second_len + 1 + (size_t)first_len;
	char *new_text = editorMalloc(new_len);
	if (new_text == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	memcpy(new_text, second_chars, (size_t)second_len);
	new_text[second_len] = '\n';
	memcpy(new_text + second_len + 1, first_chars, (size_t)first_len);

	// Replace the combined content of both rows (including the '\n' between them)
	size_t old_len = second_end - first_start;

	size_t cx = (size_t)E.cx;
	size_t after_offset;
	if (direction < 0) {
		if (cx > (size_t)second_len) {
			cx = (size_t)second_len;
		}
		after_offset = first_start + cx;
	} else {
		if (cx > (size_t)first_len) {
			cx = (size_t)first_len;
		}
		after_offset = first_start + (size_t)second_len + 1 + cx;
	}

	size_t before_offset = 0;
	(void)editorBufferPosToOffset(cur, E.cx, &before_offset);

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

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	free(new_text);
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
	int pasted = editorInsertText(clip, clip_len);

	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();

	if (pasted) {
		editorSetStatusMsg("Pasted %zu bytes", clip_len);
	}
}

static void editorMoveCursorToSearchMatch(int row_idx, int match_col, int match_len) {
	size_t match_offset = 0;
	if (!editorBufferPosToOffset(row_idx, match_col, &match_offset)) {
		editorClearActiveSearchMatch();
		return;
	}

	E.search_match_offset = match_offset;
	E.search_match_len = match_len;
	(void)editorSetCursorFromOffset(match_offset);
}

static void editorRestoreCursorToSavedSearchPosition(void) {
	if (!editorSetCursorFromOffset(E.search_saved_offset)) {
		(void)editorSetCursorFromOffset(0);
	}
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
	int saved_row = 0;
	int saved_col = 0;
	(void)editorBufferOffsetToPos(E.search_saved_offset, &saved_row, &saved_col);
	int active_match_row = -1;
	int active_match_col = -1;
	int have_active_match = editorSearchMatchPosition(&active_match_row, &active_match_col);

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
	int found = direction == 1 ?
			editorBufferFindForward(query, start_row, start_col, &match_row, &match_col) :
			editorBufferFindBackward(query, start_row, start_col, &match_row, &match_col);

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
		if (c == INPUT_EOF_EVENT) {
			free(buf);
			editorExitOnInputShutdown();
			return NULL;
		}
		if (c == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (c == SYNTAX_EVENT || c == TASK_EVENT || c == WATCH_EVENT) {
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
	editorAlignCursorWithRowEnd();
	E.search_saved_offset = E.cursor_offset;
	E.search_direction = 1;
	editorClearActiveSearchMatch();

	char *query = editorPromptWithCallback(
			"Search: %s (Use ESC/Arrows/Enter)", 1, editorFindCallback);
	if (query == NULL) {
		return;
	}

	free(E.search_query);
	E.search_query = query;
	if (E.search_match_len == 0) {
		editorSetStatusMsg("No matches for \"%s\"", query);
	}
}

static void editorFindFile(void) {
	editorHistoryBreakGroup();
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(0);
	}
	if (!editorFileSearchEnter()) {
		return;
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
	(void)editorFileSearchPreviewSelection();
}

static void editorFindTextInProject(void) {
	editorHistoryBreakGroup();
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	if (editorFileSearchIsActive()) {
		editorFileSearchExit(0);
	}
	if (!editorProjectSearchEnter()) {
		return;
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
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
		(void)editorSetCursorFromOffset(0);
		editorSetStatusMsg("Buffer is empty");
		return;
	}

	if (line > E.numrows) {
		line = E.numrows;
	}

	size_t target_offset = 0;
	if (!editorBufferPosToOffset((int)(line - 1), 0, &target_offset) ||
			!editorSetCursorFromOffset(target_offset)) {
		(void)editorSetCursorFromOffset(0);
	}
}

static const char *editorBasenameFromPath(const char *path) {
	if (path == NULL) {
		return "";
	}
	const char *base = strrchr(path, '/');
	if (base == NULL) {
		return path;
	}
	return base + 1;
}

static int editorJumpToDefinitionLocation(const struct editorLspLocation *location) {
	if (location == NULL || location->path == NULL || location->path[0] == '\0') {
		return 0;
	}
	if (!editorTabOpenOrSwitchToFile(location->path)) {
		return 0;
	}

	if (E.numrows <= 0) {
		(void)editorSetCursorFromOffset(0);
		editorViewportEnsureCursorVisible();
		return 1;
	}

	int line = location->line;
	if (line < 0) {
		line = 0;
	}
	if (line >= E.numrows) {
		line = E.numrows - 1;
	}
	int character = location->character;
	if (character < 0) {
		character = 0;
	}
	character = editorLspProtocolCharacterToBufferColumn(line, character);
	if (character > E.rows[line].size) {
		character = E.rows[line].size;
	}
	int target_cx = editorRowClampCxToClusterBoundary(&E.rows[line], character);
	if (target_cx > E.rows[line].size) {
		target_cx = E.rows[line].size;
	}
	size_t target_offset = 0;
	if (!editorBufferPosToOffset(line, target_cx, &target_offset) ||
			!editorSetCursorFromOffset(target_offset)) {
		(void)editorSetCursorFromOffset(0);
	}
	editorViewportEnsureCursorVisible();
	return 1;
}

static int editorPromptDefinitionChoice(int count, int *choice_out) {
	if (choice_out == NULL || count <= 0) {
		return 0;
	}

	char prompt[64];
	int written = snprintf(prompt, sizeof(prompt), "Definition (1-%d): %%s", count);
	if (written <= 0 || (size_t)written >= sizeof(prompt)) {
		return 0;
	}

	char *query = editorPrompt(prompt);
	if (query == NULL) {
		return 0;
	}

	long selected = 0;
	int parsed = editorParsePositiveLineNumber(query, &selected);
	free(query);
	if (!parsed || selected > count) {
		editorSetStatusMsg("Invalid definition choice");
		return 0;
	}

	*choice_out = (int)(selected - 1);
	return 1;
}

static void editorGoToDefinition(void) {
	if (!editorGoToDefinitionSupportedLanguage(E.syntax_language)) {
		editorSetStatusMsg(
				"Go to definition is available for Go, C, C++, HTML, CSS/SCSS, JSON, and JavaScript files only");
		return;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		const char *language_label = editorGoToDefinitionLanguageLabel();
		if (language_label == NULL) {
			language_label = "source";
		}
		editorSetStatusMsg("Save this %s buffer before using go to definition", language_label);
		return;
	}
	if (!editorGoToDefinitionEnabledForLanguage()) {
		editorSetStatusMsg("%s is disabled in config", editorGoToDefinitionServerName());
		return;
	}
	const char *command = editorGoToDefinitionCommand();
	const char *command_setting = editorGoToDefinitionCommandSettingName();
	if (command == NULL || command_setting == NULL) {
		editorSetStatusMsg("LSP unavailable for this file");
		return;
	}
	if (command[0] == '\0') {
		editorSetStatusMsg("LSP disabled: [lsp].%s is empty", command_setting);
		return;
	}
	editorAlignCursorWithRowEnd();
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

	int ready = editorLspEnsureDocumentOpen(E.filename, E.syntax_language,
			&E.lsp_doc_open, &E.lsp_doc_version,
			full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
	if (!ready) {
		if (editorLspLastStartupFailureReason() == EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
			editorMaybePromptInstallLanguageServer();
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
	int request_result = editorLspRequestDefinition(E.filename, E.syntax_language, E.cy, E.cx,
			&locations, &count, &timed_out);
	if (request_result == -2 || timed_out) {
		editorSetStatusMsg("Go to definition timed out");
		editorLspFreeLocations(locations, count);
		return;
	}
	if (request_result <= 0) {
		editorSetStatusMsg("Go to definition failed");
		editorLspFreeLocations(locations, count);
		return;
	}
	if (count <= 0) {
		editorSetStatusMsg("Definition not found");
		editorLspFreeLocations(locations, count);
		return;
	}

	int selected_index = 0;
	if (count > 1) {
		editorSetStatusMsg("Found %d definitions; choose 1-%d", count, count);
		if (!editorPromptDefinitionChoice(count, &selected_index)) {
			editorLspFreeLocations(locations, count);
			return;
		}
	}

	const struct editorLspLocation *selected = &locations[selected_index];
	if (!editorJumpToDefinitionLocation(selected)) {
		editorSetStatusMsg("Unable to jump to definition");
		editorLspFreeLocations(locations, count);
		return;
	}

	editorSetStatusMsg("Definition: %s:%d", editorBasenameFromPath(selected->path),
			selected->line + 1);
	editorLspFreeLocations(locations, count);
}

static void editorApplyEslintFixes(void) {
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
		editorPromptInstallSharedVscodeLanguageServers();
		return;
	}
	if (result == -2) {
		editorSetStatusMsg("ESLint fixes timed out");
		return;
	}
	editorSetStatusMsg("ESLint fixes failed");
}

static void editorMoveCursor(int k) {
	editorAlignCursorWithRowEnd();

	int cy = E.cy;
	int cx = E.cx;
	int target_rx = 0;
	if ((k == ARROW_UP || k == ARROW_DOWN) && cy < E.numrows) {
		target_rx = editorRowCxToRx(&E.rows[cy], cx);
	}

	switch (k) {
		case ARROW_LEFT:
			if (cx != 0) {
				if (cy < E.numrows) {
					// Step by grapheme cluster instead of byte index.
					cx = editorRowPrevClusterIdx(&E.rows[cy], cx);
				} else {
					cx--;
				}
			} else if (cy > 0) {
				cy--;
				cx = E.rows[cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (E.numrows > cy && cx < E.rows[cy].size) {
				// Step by grapheme cluster instead of byte index.
				cx = editorRowNextClusterIdx(&E.rows[cy], cx);
			} else if (E.numrows > cy && cx == E.rows[cy].size) {
				cy++;
				cx = 0;
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
		cx = editorRowRxToCx(&E.rows[cy], target_rx);
	}

	(void)editorSetCursorFromPosition(cy, cx);
}

static int editorResolveMouseToBufferOffset(const struct editorMouseEvent *event,
		int clamp_to_viewport, size_t *offset_out) {
	if (event == NULL || offset_out == NULL || E.numrows == 0) {
		return 0;
	}

	// SGR mouse coordinates are terminal-absolute and 1-based.
	// Row 1 is the tab bar; text viewport starts on row 2.
	int mouse_row = event->y - 2;
	int raw_col = event->x - 1;
	int text_cols = editorTextBodyViewportCols(E.window_cols);
	int text_start_col = editorTextBodyStartColForCols(E.window_cols);
	int mouse_col = raw_col - text_start_col;
	if (clamp_to_viewport) {
		if (E.window_rows <= 0 || text_cols <= 0) {
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
		if (mouse_col >= text_cols) {
			mouse_col = text_cols - 1;
		}
	} else {
		// Ignore clicks outside text rows (tab/status/message bars are excluded).
		if (mouse_row < 0 || mouse_row >= E.window_rows || mouse_col < 0 ||
				mouse_col >= text_cols) {
			return 0;
		}
	}

	int row_idx = E.rowoff + mouse_row;
	int segment_coloff = E.coloff;
	int segment_indent_cols = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferPosition(mouse_row, &row_idx, &segment_coloff,
					&segment_indent_cols)) {
			return 0;
		}
	}
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

	int target_rx = segment_coloff + mouse_col - segment_indent_cols;
	if (target_rx < segment_coloff) {
		target_rx = segment_coloff;
	}

	// Convert rendered column -> buffer byte index while respecting grapheme boundaries.
	int cx = editorRowRxToCx(&E.rows[row_idx], target_rx);
	return editorBufferPosToOffset(row_idx, cx, offset_out);
}

static int editorMoveCursorToMouse(const struct editorMouseEvent *event, int clamp_to_viewport) {
	size_t offset = 0;
	if (!editorResolveMouseToBufferOffset(event, clamp_to_viewport, &offset)) {
		return 0;
	}
	return editorSetCursorFromOffset(offset);
}

static int editorHandleMouseLeftPress(const struct editorMouseEvent *event) {
	int mouse_col = event->x - 1;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_start_col = editorDrawerTextStartColForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	int drawer_view_rows = E.window_rows + 1;
	long long now_ms = editorMonotonicMillis();

	int drawer_row = event->y - 1;
	if (drawer_row >= 0 && drawer_row < drawer_view_rows &&
			separator_cols == 1 && mouse_col == drawer_cols) {
		editorResetDrawerClickTracking();
		E.drawer_resize_active = 1;
		(void)editorDrawerSetWidthForCols(mouse_col, E.window_cols);
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}
	E.drawer_resize_active = 0;

	if (drawer_row >= 0 && drawer_row < drawer_view_rows &&
			mouse_col >= 0 && mouse_col < drawer_cols) {
		if (editorDrawerIsCollapsed()) {
			editorResetDrawerClickTracking();
			editorExpandDrawerForFocus();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			return EDITOR_KEYPRESS_EFFECT_NONE;
		}
		if (drawer_row == 0 && mouse_col < ROTIDE_DRAWER_COLLAPSED_WIDTH) {
			editorResetDrawerClickTracking();
			if (editorDrawerSetCollapsed(1)) {
				editorSetDrawerCollapseStatus(1);
			}
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			return EDITOR_KEYPRESS_EFFECT_NONE;
		}
		int visible_idx =
				((editorFileSearchIsActive() || editorProjectSearchIsActive()) && drawer_row == 0) ?
						0 : E.drawer_rowoff + drawer_row;
		if (!editorDrawerSelectVisibleIndex(visible_idx, drawer_view_rows)) {
			editorResetDrawerClickTracking();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			return EDITOR_KEYPRESS_EFFECT_NONE;
		}
		if (editorDrawerSelectedIsDirectory()) {
			(void)editorDrawerToggleSelectionExpanded(drawer_view_rows);
			editorResetDrawerClickTracking();
			E.pane_focus = EDITOR_PANE_DRAWER;
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			return EDITOR_KEYPRESS_EFFECT_NONE;
		}

		int should_open_file = E.drawer_last_click_visible_idx == visible_idx &&
				E.drawer_last_click_ms > 0 &&
				now_ms > 0 &&
				now_ms - E.drawer_last_click_ms <= DRAWER_DOUBLE_CLICK_THRESHOLD_MS;
		if (should_open_file) {
			if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
				if (editorDrawerOpenSelectedFileInTab()) {
					E.pane_focus = EDITOR_PANE_DRAWER;
				}
			} else if (editorActiveTabIsPreview()) {
				editorTabPinActivePreview();
				editorSetStatusMsg("Tab kept open");
				E.pane_focus = EDITOR_PANE_TEXT;
			} else if (editorDrawerOpenSelectedFileInTab()) {
				E.pane_focus = EDITOR_PANE_TEXT;
			}
			editorResetDrawerClickTracking();
		} else {
			if (editorDrawerOpenSelectedFileInPreviewTab()) {
				editorSetStatusMsg("Preview tab opened. Double-click to keep it open");
			}
			E.drawer_last_click_visible_idx = visible_idx;
			E.drawer_last_click_ms = now_ms;
			E.pane_focus = EDITOR_PANE_DRAWER;
		}
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}

	editorResetDrawerClickTracking();

	if (event->y == 1) {
		int tab_col = mouse_col - text_start_col;
		int tab_idx = editorTabHitTestColumn(tab_col, text_cols);
		if (tab_idx >= 0) {
			(void)editorTabSwitchToIndex(tab_idx);
		}
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}

	if (!editorMoveCursorToMouse(event, 0)) {
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}

	E.pane_focus = EDITOR_PANE_TEXT;
	// A fresh text click exits any previous selection; dragging can start a new one.
	editorClearSelectionMode();
	if (event->modifiers == EDITOR_MOUSE_MOD_CTRL) {
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		editorHistoryBreakGroup();
		editorGoToDefinition();
		return EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
	}
	E.mouse_left_button_down = 1;
	E.mouse_drag_anchor_offset = E.cursor_offset;
	E.mouse_drag_started = 0;
	return EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
}

static int editorHandleMouseLeftDrag(const struct editorMouseEvent *event) {
	if (E.drawer_resize_active) {
		int mouse_col = event->x - 1;
		(void)editorDrawerSetWidthForCols(mouse_col, E.window_cols);
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}
	if (!E.mouse_left_button_down) {
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}
	if (!editorMoveCursorToMouse(event, 1)) {
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}

	if (!E.mouse_drag_started) {
		// A new drag always starts a fresh selection anchored at the initial press point.
		E.selection_mode_active = 1;
		E.selection_anchor_offset = E.mouse_drag_anchor_offset;
		E.mouse_drag_started = 1;
	}
	return EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
}

static int editorHandleMouseLeftRelease(void) {
	E.drawer_resize_active = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return EDITOR_KEYPRESS_EFFECT_NONE;
}

static int editorHandleMouseWheel(const struct editorMouseEvent *event) {
	int over_drawer = editorMouseIsOverDrawer(event);

	switch (event->kind) {
		case EDITOR_MOUSE_EVENT_WHEEL_UP:
			if (over_drawer) {
				(void)editorDrawerScrollBy(-MOUSE_WHEEL_SCROLL_LINES, E.window_rows + 1);
				break;
			}
			editorViewportScrollByRows(-MOUSE_WHEEL_SCROLL_LINES);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
			if (over_drawer) {
				(void)editorDrawerScrollBy(MOUSE_WHEEL_SCROLL_LINES, E.window_rows + 1);
				break;
			}
			editorViewportScrollByRows(MOUSE_WHEEL_SCROLL_LINES);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_LEFT:
			editorViewportScrollByCols(-MOUSE_WHEEL_SCROLL_COLS);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_RIGHT:
			editorViewportScrollByCols(MOUSE_WHEEL_SCROLL_COLS);
			break;
		default:
			return EDITOR_KEYPRESS_EFFECT_NONE;
	}
	return EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
}

static int editorHandleMouseEvent(void) {
	struct editorMouseEvent event;
	// terminal.c queues one decoded event per MOUSE_EVENT keycode.
	if (!editorConsumeMouseEvent(&event)) {
		return EDITOR_KEYPRESS_EFFECT_NONE;
	}

	switch (event.kind) {
		case EDITOR_MOUSE_EVENT_LEFT_PRESS:
			return editorHandleMouseLeftPress(&event);
		case EDITOR_MOUSE_EVENT_LEFT_DRAG:
			return editorHandleMouseLeftDrag(&event);
		case EDITOR_MOUSE_EVENT_LEFT_RELEASE:
			return editorHandleMouseLeftRelease();
		case EDITOR_MOUSE_EVENT_WHEEL_UP:
		case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
		case EDITOR_MOUSE_EVENT_WHEEL_LEFT:
		case EDITOR_MOUSE_EVENT_WHEEL_RIGHT:
			return editorHandleMouseWheel(&event);
		default:
			return EDITOR_KEYPRESS_EFFECT_NONE;
	}
}

static int editorProcessMappedAction(enum editorAction action, int *effects_out) {
	int effects = EDITOR_KEYPRESS_EFFECT_NONE;

	if (editorFileSearchIsActive()) {
		switch (action) {
			case EDITOR_ACTION_PROJECT_SEARCH:
				editorFindTextInProject();
				break;
			case EDITOR_ACTION_FIND_FILE:
				editorFindFile();
				break;
			case EDITOR_ACTION_MOVE_UP:
				if (editorFileSearchMoveSelectionBy(-1, E.window_rows + 1)) {
					(void)editorFileSearchPreviewSelection();
				}
				break;
			case EDITOR_ACTION_MOVE_DOWN:
				if (editorFileSearchMoveSelectionBy(1, E.window_rows + 1)) {
					(void)editorFileSearchPreviewSelection();
				}
				break;
			case EDITOR_ACTION_NEWLINE:
				if (editorFileSearchOpenSelectedFileInTab()) {
					E.pane_focus = EDITOR_PANE_DRAWER;
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				}
				break;
			case EDITOR_ACTION_ESCAPE:
				editorFileSearchExit(1);
				E.pane_focus = EDITOR_PANE_TEXT;
				break;
			case EDITOR_ACTION_BACKSPACE:
			case EDITOR_ACTION_DELETE_CHAR:
				if (editorFileSearchBackspace()) {
					(void)editorFileSearchPreviewSelection();
				}
				break;
			case EDITOR_ACTION_TOGGLE_DRAWER:
				if (editorDrawerSetCollapsed(1)) {
					editorSetDrawerCollapseStatus(1);
				}
				break;
			default:
				break;
		}
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 0;
	}

	if (editorProjectSearchIsActive()) {
		switch (action) {
			case EDITOR_ACTION_FIND_FILE:
				editorFindFile();
				break;
			case EDITOR_ACTION_PROJECT_SEARCH:
				editorFindTextInProject();
				break;
			case EDITOR_ACTION_MOVE_UP:
				if (editorProjectSearchMoveSelectionBy(-1, E.window_rows + 1)) {
					(void)editorProjectSearchPreviewSelection();
				}
				break;
			case EDITOR_ACTION_MOVE_DOWN:
				if (editorProjectSearchMoveSelectionBy(1, E.window_rows + 1)) {
					(void)editorProjectSearchPreviewSelection();
				}
				break;
			case EDITOR_ACTION_NEWLINE:
				if (editorProjectSearchOpenSelectedFileInTab()) {
					E.pane_focus = EDITOR_PANE_DRAWER;
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				}
				break;
			case EDITOR_ACTION_ESCAPE:
				editorProjectSearchExit(1);
				E.pane_focus = EDITOR_PANE_TEXT;
				break;
			case EDITOR_ACTION_BACKSPACE:
			case EDITOR_ACTION_DELETE_CHAR:
				if (editorProjectSearchBackspace()) {
					(void)editorProjectSearchPreviewSelection();
				}
				break;
			case EDITOR_ACTION_TOGGLE_DRAWER:
				if (editorDrawerSetCollapsed(1)) {
					editorSetDrawerCollapseStatus(1);
				}
				break;
			default:
				break;
		}
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 0;
	}

	if (editorActiveTabIsReadOnly()) {
		if (action == EDITOR_ACTION_SAVE) {
			editorSetStatusMsg(editorActiveTabIsUnsupportedFile() ?
					"Unsupported files cannot be saved" : "Task logs cannot be saved");
			if (effects_out != NULL) {
				*effects_out = effects;
			}
			return 1;
		}
		if (E.pane_focus != EDITOR_PANE_DRAWER && editorActionMutatesReadOnlyBuffer(action)) {
			editorSetStatusMsg(editorActiveTabIsUnsupportedFile() ?
					"File is unsupported" : "Task log is read-only");
			if (effects_out != NULL) {
				*effects_out = effects;
			}
			return 1;
		}
	}

	switch (action) {
		case EDITOR_ACTION_QUIT:
			editorHistoryBreakGroup();
			quit();
			if (effects_out != NULL) {
				*effects_out = effects;
			}
			return 1;
		case EDITOR_ACTION_SAVE:
			editorHistoryBreakGroup();
			editorSave();
			break;
		case EDITOR_ACTION_NEW_TAB:
			editorHistoryBreakGroup();
			(void)editorTabNewEmpty();
			break;
		case EDITOR_ACTION_CLOSE_TAB:
			editorHistoryBreakGroup();
			editorCloseTab();
			break;
		case EDITOR_ACTION_NEXT_TAB:
			editorHistoryBreakGroup();
			(void)editorTabSwitchByDelta(1);
			break;
		case EDITOR_ACTION_PREV_TAB:
			editorHistoryBreakGroup();
			(void)editorTabSwitchByDelta(-1);
			break;
		case EDITOR_ACTION_FOCUS_DRAWER:
			editorHistoryBreakGroup();
			editorToggleDrawerFocus();
			break;
		case EDITOR_ACTION_TOGGLE_DRAWER:
			editorHistoryBreakGroup();
			if (editorDrawerToggleCollapsed()) {
				editorSetDrawerCollapseStatus(editorDrawerIsCollapsed());
				if (!editorDrawerIsCollapsed()) {
					E.pane_focus = EDITOR_PANE_DRAWER;
				}
			}
			break;
		case EDITOR_ACTION_RESIZE_DRAWER_NARROW:
			editorHistoryBreakGroup();
			if (editorDrawerIsCollapsed()) {
				(void)editorDrawerSetCollapsed(0);
			}
			(void)editorDrawerResizeByDeltaForCols(-DRAWER_RESIZE_STEP, E.window_cols);
			break;
		case EDITOR_ACTION_RESIZE_DRAWER_WIDEN:
			editorHistoryBreakGroup();
			if (editorDrawerIsCollapsed()) {
				(void)editorDrawerSetCollapsed(0);
			}
			(void)editorDrawerResizeByDeltaForCols(DRAWER_RESIZE_STEP, E.window_cols);
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
			editorSetStatusMsg("Line wrap %s", E.line_wrap_enabled ? "enabled" : "disabled");
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_TOGGLE_LINE_NUMBERS:
			editorHistoryBreakGroup();
			E.line_numbers_enabled = !E.line_numbers_enabled;
			editorViewportEnsureCursorVisible();
			editorSetStatusMsg("Line numbers %s", E.line_numbers_enabled ? "enabled" : "disabled");
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT:
			editorHistoryBreakGroup();
			E.current_line_highlight_enabled = !E.current_line_highlight_enabled;
			editorSetStatusMsg("Current-line highlight %s",
					E.current_line_highlight_enabled ? "enabled" : "disabled");
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_FIND_FILE:
			editorFindFile();
			break;
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorFindTextInProject();
			break;
		case EDITOR_ACTION_FIND:
			editorHistoryBreakGroup();
			editorFind();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_LINE:
			editorHistoryBreakGroup();
			editorGoToLine();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_DEFINITION:
			editorHistoryBreakGroup();
			editorGoToDefinition();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_ESLINT_FIX:
			editorHistoryBreakGroup();
			editorPinActivePreviewForEdit();
			editorApplyEslintFixes();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_TOGGLE_SELECTION:
			editorHistoryBreakGroup();
			editorToggleSelectionMode();
			break;
		case EDITOR_ACTION_COPY_SELECTION:
			editorHistoryBreakGroup();
			editorCopySelection();
			break;
		case EDITOR_ACTION_CUT_SELECTION:
			editorHistoryBreakGroup();
			editorPinActivePreviewForEdit();
			editorCutSelection();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_DELETE_SELECTION:
			editorHistoryBreakGroup();
			editorPinActivePreviewForEdit();
			editorDeleteSelection();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_PASTE:
			editorHistoryBreakGroup();
			editorPinActivePreviewForEdit();
			editorPasteClipboard();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_UNDO:
			editorHistoryBreakGroup();
			editorPinActivePreviewForEdit();
			if (editorUndo() == 1) {
				editorClearSearchState();
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
			case EDITOR_ACTION_REDO:
				editorHistoryBreakGroup();
				editorPinActivePreviewForEdit();
				if (editorRedo() == 1) {
					editorClearSearchState();
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				}
				break;
			case EDITOR_ACTION_MOVE_HOME:
				editorHistoryBreakGroup();
				(void)editorSetCursorFromPosition(E.cy, 0);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				break;
			case EDITOR_ACTION_MOVE_END:
				editorHistoryBreakGroup();
				if (E.cy < E.numrows) {
					(void)editorSetCursorFromPosition(E.cy, E.rows[E.cy].size);
				} else {
					(void)editorSetCursorFromPosition(E.numrows, 0);
				}
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				break;
		case EDITOR_ACTION_PAGE_UP: {
			editorHistoryBreakGroup();
			int page_rows = E.window_rows;
			if (page_rows < 1) {
				page_rows = 1;
			}
			editorViewportScrollByRows(-page_rows);
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		}
		case EDITOR_ACTION_PAGE_DOWN: {
			editorHistoryBreakGroup();
			int page_rows = E.window_rows;
			if (page_rows < 1) {
				page_rows = 1;
			}
			editorViewportScrollByRows(page_rows);
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		}
		case EDITOR_ACTION_SCROLL_LEFT:
			editorHistoryBreakGroup();
			editorViewportScrollByCols(-KEYBOARD_SCROLL_COLS);
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_SCROLL_RIGHT:
			editorHistoryBreakGroup();
			editorViewportScrollByCols(KEYBOARD_SCROLL_COLS);
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
			break;
		case EDITOR_ACTION_MOVE_UP:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerMoveSelectionBy(-1, E.window_rows + 1);
			} else {
				editorMoveCursor(ARROW_UP);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_DOWN:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerMoveSelectionBy(1, E.window_rows + 1);
			} else {
				editorMoveCursor(ARROW_DOWN);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_LEFT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerCollapseSelection(E.window_rows + 1);
			} else {
				editorMoveCursor(ARROW_LEFT);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_RIGHT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerExpandSelection(E.window_rows + 1);
			} else {
				editorMoveCursor(ARROW_RIGHT);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_NEWLINE:
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				editorHistoryBreakGroup();
				editorResetDrawerClickTracking();
				if (editorDrawerSelectedIsDirectory()) {
					(void)editorDrawerToggleSelectionExpanded(E.window_rows + 1);
				} else if (editorDrawerOpenSelectedFileInTab()) {
					E.pane_focus = EDITOR_PANE_TEXT;
				}
				break;
			}
			editorClearSelectionMode();
			editorPinActivePreviewForEdit();
			editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
			{
				int dirty_before = E.dirty;
				editorInsertNewline();
				editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
			}
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_ESCAPE:
			// In normal editor mode Escape only clears transient selection state; quit is configurable.
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				E.pane_focus = EDITOR_PANE_TEXT;
				break;
			}
			editorClearSelectionMode();
			break;
		case EDITOR_ACTION_REDRAW:
			editorHistoryBreakGroup();
			break;
		case EDITOR_ACTION_DELETE_CHAR:
			editorPinActivePreviewForEdit();
			{
				struct editorSelectionRange range;
				if (editorGetSelectionRange(&range)) {
					editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
					int dirty_before = E.dirty;
					editorDeleteRange(&range);
					editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
					editorClearSelectionMode();
				} else {
					editorClearSelectionMode();
					editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
					int dirty_before = E.dirty;
					// DEL deletes under cursor; editorDelChar() implements backspace semantics.
					editorMoveCursor(ARROW_RIGHT);
					editorDelChar();
					editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
				}
			}
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_BACKSPACE:
			editorPinActivePreviewForEdit();
			{
				struct editorSelectionRange range;
				if (editorGetSelectionRange(&range)) {
					editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
					int dirty_before = E.dirty;
					editorDeleteRange(&range);
					editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
					editorClearSelectionMode();
				} else {
					editorClearSelectionMode();
					editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
					int dirty_before = E.dirty;
					editorDelChar();
					editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
				}
			}
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_LINE_UP:
			editorClearSelectionMode();
			editorPinActivePreviewForEdit();
			editorMoveCurrentLine(-1);
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_MOVE_LINE_DOWN:
			editorClearSelectionMode();
			editorPinActivePreviewForEdit();
			editorMoveCurrentLine(1);
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
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
	int effects = EDITOR_KEYPRESS_EFFECT_NONE;

	if (c == INPUT_EOF_EVENT) {
		editorExitOnInputShutdown();
		return;
	}
	if (c == RESIZE_EVENT) {
		(void)editorRefreshWindowSize();
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

	if (c == MOUSE_EVENT) {
		// Mouse input can move cursor/selection, but it should not create edit history entries.
		editorHistoryBreakGroup();
		effects |= editorHandleMouseEvent();
	} else {
		if (editorKeymapLookupAction(&E.keymap, c, &action)) {
			int mapped_effects = EDITOR_KEYPRESS_EFFECT_NONE;
			mapped_action = 1;
			if (editorProcessMappedAction(action, &mapped_effects)) {
				return;
			}
			effects |= mapped_effects;
		} else if (editorByteShouldInsertAsText(c)) {
			if (editorFileSearchIsActive()) {
				if (editorFileSearchAppendByte(c)) {
					(void)editorFileSearchPreviewSelection();
				}
			} else if (editorProjectSearchIsActive()) {
				if (editorProjectSearchAppendByte(c)) {
					(void)editorProjectSearchPreviewSelection();
				}
			} else if (E.pane_focus != EDITOR_PANE_DRAWER) {
				if (editorActiveTabIsReadOnly()) {
					editorSetStatusMsg(editorActiveTabIsUnsupportedFile() ?
							"File is unsupported" : "Task log is read-only");
					goto done;
				}
				editorClearSelectionMode();
				editorPinActivePreviewForEdit();
				editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
				int dirty_before = E.dirty;
				editorInsertChar(c);
				editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
		}
	}

done:
	if (!mapped_action || action != EDITOR_ACTION_CLOSE_TAB) {
		E.close_confirmed = 0;
	}
	if (!mapped_action || action != EDITOR_ACTION_QUIT) {
		quit_confirmed = 0;
	}
	if (!mapped_action || action != EDITOR_ACTION_QUIT) {
		quit_task_confirmed = 0;
	}
	if ((effects & EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT) != 0) {
		editorViewportEnsureCursorVisible();
	}

	editorRecoveryMaybeAutosaveOnActivity();
}
