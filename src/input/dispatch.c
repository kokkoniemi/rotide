#include "input/dispatch.h"

#include "config/common.h"
#include "config/dap_config.h"
#include "config/keymap.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "debug/dap.h"
#include "language/lsp.h"
#include "language/syntax_worker.h"
#include "render/popup.h"
#include "language/autocomplete.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "support/terminal.h"
#include "terminal/terminal_pane.h"
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
#include <strings.h>
#include <string.h>

#include "text/row.h"
#include "text/utf8.h"

/*** Input ***/

enum {
	DRAWER_DOUBLE_CLICK_THRESHOLD_MS = 400,
	TEXT_MULTI_CLICK_THRESHOLD_MS = 400,
	DRAWER_RESIZE_STEP = 1,
	KEYBOARD_SCROLL_COLS = 3
};

enum editorKeypressEffect {
	EDITOR_KEYPRESS_EFFECT_NONE = 0,
	EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL = 1 << 0,
	EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT = 1 << 1
};

static int editorProcessMappedAction(enum editorAction action, int *effects_out);
static void editorPinActivePreviewForEdit(void);
static int editorSetCursorFromOffset(size_t offset);
static int editorSetCursorFromPosition(int cy, int cx);
static void editorClearSelectionMode(void);
static void editorCtrlClickGoToDefinitionAction(void);
static void editorGoToDefinition(void);

static int editorIsWordByte(unsigned char b) {
	return isalnum(b) || b == '_' || b >= 0x80;
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
	editorColumnSelectionClear();
}

static void editorCtrlClickGoToDefinitionAction(void) {
	editorHistoryBreakGroup();
	editorGoToDefinition();
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
		case EDITOR_ACTION_FIND_REPLACE:
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

	editorColumnSelectionClear();
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

static int editorCopyColumnSelectionToClipboard(size_t *copied_len_out) {
	char *text = NULL;
	size_t len = 0;
	int rc = editorColumnSelectionExtractText(&text, &len);
	if (rc <= 0) {
		return rc;
	}
	if (!editorClipboardSet(text, len)) {
		free(text);
		return -1;
	}
	free(text);
	if (copied_len_out != NULL) {
		*copied_len_out = len;
	}
	return 1;
}

static void editorCopySelection(void) {
	if (E.column_select_active) {
		size_t copied_len = 0;
		int copied = editorCopyColumnSelectionToClipboard(&copied_len);
		if (copied < 0) {
			return;
		}
		if (copied == 0) {
			editorSetStatusMsg("Selection is empty");
			return;
		}
		editorSetStatusMsg("Copied %zu bytes", copied_len);
		return;
	}

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
	if (E.column_select_active) {
		size_t copied_len = 0;
		int copied = editorCopyColumnSelectionToClipboard(&copied_len);
		if (copied < 0) {
			return;
		}
		if (copied == 0) {
			editorSetStatusMsg("Selection is empty");
			return;
		}
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		int deleted = editorColumnSelectionDelete();
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		if (deleted < 0) {
			return;
		}
		editorSetStatusMsg("Cut %zu bytes", copied_len);
		return;
	}

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

static const char *editorCommentPrefixForLanguage(enum editorSyntaxLanguage lang) {
	switch (lang) {
	case EDITOR_SYNTAX_C:
	case EDITOR_SYNTAX_CPP:
	case EDITOR_SYNTAX_GO:
	case EDITOR_SYNTAX_JAVASCRIPT:
	case EDITOR_SYNTAX_TYPESCRIPT:
	case EDITOR_SYNTAX_TSX:
	case EDITOR_SYNTAX_JAVA:
	case EDITOR_SYNTAX_RUST:
	case EDITOR_SYNTAX_CSS:
	case EDITOR_SYNTAX_CSHARP:
	case EDITOR_SYNTAX_SCALA:
	case EDITOR_SYNTAX_PHP:
		return "//";
	case EDITOR_SYNTAX_PYTHON:
	case EDITOR_SYNTAX_SHELL:
	case EDITOR_SYNTAX_RUBY:
	case EDITOR_SYNTAX_JULIA:
		return "#";
	case EDITOR_SYNTAX_HASKELL:
		return "--";
	default:
		return NULL;
	}
}

static void editorToggleCommentLines(void) {
	const char *prefix = editorCommentPrefixForLanguage(E.syntax_language);
	if (prefix == NULL) {
		editorSetStatusMsg("No line comment for this language");
		return;
	}
	int prefix_len = (int)strlen(prefix);

	struct editorSelectionRange range;
	int had_selection = editorGetSelectionRange(&range);
	if (!had_selection) {
		if (E.cy < 0 || E.cy >= E.numrows) {
			return;
		}
		range.start_cy = E.cy;
		range.start_cx = 0;
		range.end_cy = E.cy;
		range.end_cx = E.rows[E.cy].size;
	}

	int last_row = range.end_cy;
	if (had_selection && last_row > range.start_cy && range.end_cx == 0) {
		last_row--;
	}
	if (range.start_cy < 0 || last_row >= E.numrows) {
		return;
	}

	// Determine toggle direction: all non-empty lines commented → remove, else add
	int removing = 1;
	for (int row = range.start_cy; row <= last_row; row++) {
		const char *chars = E.rows[row].chars;
		int size = E.rows[row].size;
		if (size == 0) {
			continue;
		}
		int i = 0;
		while (i < size && (chars[i] == ' ' || chars[i] == '\t')) {
			i++;
		}
		if (i + prefix_len > size || strncmp(chars + i, prefix, (size_t)prefix_len) != 0) {
			removing = 0;
			break;
		}
	}

	size_t first_start = 0, dummy = 0, last_end = 0;
	if (!editorBufferLineByteRange(range.start_cy, &first_start, &dummy) ||
			!editorBufferLineByteRange(last_row, &dummy, &last_end)) {
		return;
	}
	size_t old_len = last_end - first_start;

	// Compute new_len
	size_t new_len = old_len;
	for (int row = range.start_cy; row <= last_row; row++) {
		const char *chars = E.rows[row].chars;
		int size = E.rows[row].size;
		if (size == 0) {
			continue;
		}
		if (!removing) {
			new_len += (size_t)prefix_len + 1;
		} else {
			int i = 0;
			while (i < size && (chars[i] == ' ' || chars[i] == '\t')) {
				i++;
			}
			int skip = prefix_len;
			if (i + prefix_len < size && chars[i + prefix_len] == ' ') {
				skip++;
			}
			new_len -= (size_t)skip;
		}
	}

	char *new_text = editorMalloc(new_len > 0 ? new_len : 1);
	if (new_text == NULL) {
		editorSetAllocFailureStatus();
		return;
	}

	size_t out = 0;
	size_t cur_row_new_start = first_start;
	size_t cur_row_new_size = 0;

	for (int row = range.start_cy; row <= last_row; row++) {
		const char *chars = E.rows[row].chars;
		int size = E.rows[row].size;
		size_t out_before = out;

		if (size == 0) {
			// empty: unchanged
		} else if (!removing) {
			memcpy(new_text + out, prefix, (size_t)prefix_len);
			out += (size_t)prefix_len;
			new_text[out++] = ' ';
			memcpy(new_text + out, chars, (size_t)size);
			out += (size_t)size;
		} else {
			int i = 0;
			while (i < size && (chars[i] == ' ' || chars[i] == '\t')) {
				i++;
			}
			memcpy(new_text + out, chars, (size_t)i);
			out += (size_t)i;
			int skip = prefix_len;
			if (i + prefix_len < size && chars[i + prefix_len] == ' ') {
				skip++;
			}
			int rest = size - i - skip;
			if (rest > 0) {
				memcpy(new_text + out, chars + i + skip, (size_t)rest);
				out += (size_t)rest;
			}
		}

		if (row < E.cy) {
			cur_row_new_start += (out - out_before) + 1;
		} else if (row == E.cy) {
			cur_row_new_size = out - out_before;
		}

		if (row < last_row) {
			new_text[out++] = '\n';
		}
	}

	size_t before_offset = 0;
	(void)editorBufferPosToOffset(E.cy, E.cx, &before_offset);

	size_t after_offset;
	if (E.cy >= range.start_cy && E.cy <= last_row && E.cy < E.numrows) {
		int size = E.rows[E.cy].size;
		size_t new_cx = (size_t)E.cx;
		if (size > 0) {
			if (!removing) {
				new_cx += (size_t)prefix_len + 1;
			} else {
				const char *chars = E.rows[E.cy].chars;
				int i = 0;
				while (i < size && (chars[i] == ' ' || chars[i] == '\t')) {
					i++;
				}
				int skip = prefix_len;
				if (i + prefix_len < size && chars[i + prefix_len] == ' ') {
					skip++;
				}
				if (new_cx > (size_t)(i + skip)) {
					new_cx -= (size_t)skip;
				} else {
					new_cx = (size_t)i;
				}
			}
		}
		if (new_cx > cur_row_new_size) {
			new_cx = cur_row_new_size;
		}
		after_offset = cur_row_new_start + new_cx;
	} else {
		ptrdiff_t net = (ptrdiff_t)new_len - (ptrdiff_t)old_len;
		after_offset = (size_t)((ptrdiff_t)before_offset + net);
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

	editorPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	free(new_text);
	if (had_selection) {
		editorClearSelectionMode();
	}
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

static int editorReplaceSelectionWithChar(int c) {
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

	editorPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	if (editorApplyDocumentEdit(&edit)) {
		(void)editorSyncCursorFromOffsetByteBoundary(start_offset + 1);
	}
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	editorClearSelectionMode();
	return 1;
}

static int editorIndentSelection(void) {
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
		size_t row_len = (size_t)E.rows[row].size;
		memcpy(new_text + out, E.rows[row].chars, row_len);
		out += row_len;
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
			cur_row_new_start += 1 + (size_t)E.rows[row].size + 1;
		}
		size_t new_cx = (size_t)E.cx + 1;
		size_t max_cx = (size_t)E.rows[E.cy].size + 1;
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

	editorPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	free(new_text);
	editorClearSelectionMode();
	return 1;
}

static void editorDeleteSelection(void) {
	if (E.column_select_active) {
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		int deleted = editorColumnSelectionDelete();
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		if (deleted < 0) {
			return;
		}
		return;
	}

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

	if (E.column_select_active) {
		editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
		int dirty_before = E.dirty;
		int pasted = editorColumnSelectionPasteText(clip, clip_len);
		editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
		editorHistoryBreakGroup();
		if (pasted) {
			editorSetStatusMsg("Pasted %zu bytes", clip_len);
		}
		return;
	}

	struct editorSelectionRange range;
	int has_selection = editorGetSelectionRange(&range);
	int indent_cy = has_selection ? range.start_cy : E.cy;
	int indent_cx = has_selection ? range.start_cx : E.cx;
	char *indented_clip = NULL;
	size_t indented_clip_len = 0;
	int indent_result = editorBuildAutoIndentedText(clip, clip_len, indent_cy, indent_cx,
			&indented_clip, &indented_clip_len);
	if (indent_result < 0) {
		return;
	}
	const char *paste_text = indent_result > 0 ? indented_clip : clip;
	size_t paste_len = indent_result > 0 ? indented_clip_len : clip_len;

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	int pasted = 0;

	if (has_selection) {
		editorClearSelectionMode();
		pasted = editorReplaceRange(&range, paste_text, paste_len) > 0;
	} else {
		editorClearSelectionMode();
		pasted = editorInsertText(paste_text, paste_len);
	}

	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	free(indented_clip);

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

static int editorReplaceAtOffset(size_t offset, size_t old_len,
		const char *new_text, size_t new_len) {
	int dirty_before = E.dirty;
	editorPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_INSERT_TEXT,
		.start_offset = offset,
		.old_len = old_len,
		.new_text = new_text,
		.new_len = new_len,
		.before_cursor_offset = offset,
		.after_cursor_offset = offset + new_len,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + 1
	};
	int ok = editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, ok && E.dirty != dirty_before);
	return ok;
}

static int editorCollectMatchOffsets(const char *query, size_t query_len,
		size_t **offsets_out) {
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
		if (row < E.numrows && next_col > E.rows[row].size) {
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

static int editorReplaceAllInBuffer(const char *query, size_t query_len,
		const char *replacement, size_t replacement_len) {
	size_t *offsets = NULL;
	int count = editorCollectMatchOffsets(query, query_len, &offsets);
	if (count <= 0) {
		return count;
	}

	size_t first_offset = offsets[0];

	int replaced = 0;
	for (int i = count - 1; i >= 0; i--) {
		editorHistoryBreakGroup();
		if (editorReplaceAtOffset(offsets[i], query_len, replacement, replacement_len)) {
			replaced++;
		}
	}
	free(offsets);

	if (replaced > 0) {
		(void)editorSyncCursorFromOffset(first_offset + replacement_len);
		editorViewportEnsureCursorVisible();
		free(E.search_query);
		E.search_query = NULL;
		editorClearActiveSearchMatch();
	}
	return replaced;
}

static int editorReplaceNavigateNext(const char *query, int query_len) {
	int match_row = -1;
	int match_col = -1;
	int cur_row = E.cy;
	int cur_start_col;
	int active_row = -1;
	int active_col = -1;
	if (E.search_match_len > 0 && editorSearchMatchPosition(&active_row, &active_col)) {
		cur_row = active_row;
		cur_start_col = active_col + query_len - 1;
	} else {
		cur_start_col = E.cx > 0 ? E.cx - 1 : -1;
	}
	if (!editorBufferFindForward(query, cur_row, cur_start_col, &match_row, &match_col)) {
		editorClearActiveSearchMatch();
		return 0;
	}
	editorMoveCursorToSearchMatch(match_row, match_col, query_len);
	return 1;
}

static void editorProjectReplaceFromSearch(void) {
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

	typedef struct { char *path; int start_row; } FileEntry;
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
			FileEntry *grown = editorRealloc(files, (size_t)new_cap * sizeof(FileEntry));
			if (grown == NULL) {
				for (int j = 0; j < file_count; j++) free(files[j].path);
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
			for (int j = 0; j < file_count; j++) free(files[j].path);
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
	E.pane_focus = EDITOR_PANE_TEXT;

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
			int count = editorReplaceAllInBuffer(E.search_query, find_len,
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
		if (!editorBufferFindForward(E.search_query, start_row, -1, &match_row, &match_col)) {
			continue;
		}
		if (match_row < start_row) {
			if (!editorBufferFindForward(E.search_query, 0, -1, &match_row, &match_col)) {
				continue;
			}
		}

		editorMoveCursorToSearchMatch(match_row, match_col, (int)find_len);
		editorViewportEnsureCursorVisible();

		size_t file_start_offset = E.search_match_offset;
		const char *sep = strrchr(files[fi].path, '/');
		const char *basename = sep != NULL ? sep + 1 : files[fi].path;
		int file_replaced = 0;
		int done_with_file = 0;

		while (!done_with_file) {
			editorSetStatusMsg(
					"[%s] Replace? Enter=this Tab=skip Ctrl+A=all Esc=done (%d replaced)",
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
					if (editorReplaceAtOffset(offset, find_len, replace_query, replace_len)) {
						file_replaced++;
					}
				}
				if (!editorReplaceNavigateNext(E.search_query, (int)find_len) ||
						E.search_match_offset <= file_start_offset) {
					editorClearActiveSearchMatch();
					done_with_file = 1;
				}
			} else if (c == '\t') {
				if (!editorReplaceNavigateNext(E.search_query, (int)find_len) ||
						E.search_match_offset <= file_start_offset) {
					editorClearActiveSearchMatch();
					done_with_file = 1;
				}
			} else if (c == CTRL_KEY('a')) {
				int count = editorReplaceAllInBuffer(E.search_query, find_len,
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
		editorSetStatusMsg("Replaced %d occurrence(s) across %d file(s)%s",
				total_replaced, total_files, aborted ? " (stopped)" : "");
	} else {
		editorSetStatusMsg("No replacements made");
	}
}

static void editorFindReplace(void) {
	editorAlignCursorWithRowEnd();
	E.search_saved_offset = E.cursor_offset;
	E.search_direction = 1;
	editorClearActiveSearchMatch();

	char *find_query = editorPromptWithCallback(
			"Find: %s (Arrows/Enter to confirm, Esc to cancel)", 1, editorFindCallback);
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
		if (!editorBufferFindForward(find_query, saved_row, saved_col - 1,
					&match_row, &match_col)) {
			editorSetStatusMsg("No matches for \"%s\"", find_query);
			return;
		}
		editorMoveCursorToSearchMatch(match_row, match_col, (int)strlen(find_query));
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
		editorSetStatusMsg(
				"Replace? Enter=this Tab=skip Ctrl+A=all Esc=done (%d replaced)", replaced);
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
			if (editorReplaceAtOffset(match_offset, find_len, replace_query, replace_len)) {
				replaced++;
			}
			if (!editorReplaceNavigateNext(find_query, (int)find_len)) {
				editorSetStatusMsg("Done. Replaced %d occurrence(s)", replaced);
				free(replace_query);
				return;
			}
		} else if (c == '\t') {
			if (!editorReplaceNavigateNext(find_query, (int)find_len)) {
				editorSetStatusMsg("No more matches. Replaced %d occurrence(s)", replaced);
				free(replace_query);
				return;
			}
		} else if (c == CTRL_KEY('a')) {
			int count = editorReplaceAllInBuffer(
					find_query, find_len, replace_query, replace_len);
			if (count < 0) {
				editorSetStatusMsg("Replace all failed");
			} else {
				editorSetStatusMsg("Replaced %d occurrence(s)", replaced + count);
			}
			free(replace_query);
			return;
		} else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
			if (!editorReplaceNavigateNext(find_query, (int)find_len)) {
				editorSetStatusMsg("No more matches");
			}
		} else if (c == ARROW_LEFT || c == ARROW_UP) {
			int match_row = -1;
			int match_col = -1;
			int have_active = editorSearchMatchPosition(&match_row, &match_col);
			int start_row = have_active ? match_row : E.cy;
			int start_col = have_active ? match_col : E.cx;
			if (editorBufferFindBackward(find_query, start_row, start_col,
						&match_row, &match_col)) {
				editorMoveCursorToSearchMatch(match_row, match_col, (int)find_len);
			}
		}
	}

	editorSetStatusMsg(replaced > 0 ? "Replaced %d occurrence(s)" : "Cancelled", replaced);
	free(replace_query);
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

static int editorJumpToPathLocation(const char *path, int line, int character, int preview,
		int center) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	int opened = preview ? editorTabOpenOrSwitchToPreviewFile(path) :
			editorTabOpenOrSwitchToFile(path);
	if (!opened) {
		return 0;
	}

	if (E.numrows <= 0) {
		(void)editorSetCursorFromOffset(0);
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
	if (center) {
		editorViewportCenterCursor();
	} else {
		editorViewportEnsureCursorVisible();
	}
	return 1;
}

static int editorJumpToDefinitionLocation(const struct editorLspLocation *location) {
	if (location == NULL) {
		return 0;
	}
	return editorJumpToPathLocation(location->path, location->line, location->character, 0, 0);
}

static int editorPromptLocationChoice(const char *kind_capitalized, int count, int *choice_out) {
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
	int parsed = editorParsePositiveLineNumber(query, &selected);
	free(query);
	if (!parsed || selected > count) {
		editorSetStatusMsg("Invalid %s choice", kind_capitalized);
		return 0;
	}

	*choice_out = (int)(selected - 1);
	return 1;
}

typedef int (*editorLspLocationRequestFn)(const char *filename, enum editorSyntaxLanguage language,
		int line, int character, struct editorLspLocation **locations_out, int *count_out,
		int *timed_out_out);

static void editorRunLocationLookup(const char *kind_lower, const char *kind_capitalized,
		const char *kind_plural, editorLspLocationRequestFn request_fn) {
	if (!editorGoToDefinitionSupportedLanguage(E.syntax_language)) {
		editorSetStatusMsg(
				"Go to %s is available for Go, C, C++, HTML, CSS/SCSS, JSON, and JavaScript files only",
				kind_lower);
		return;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		const char *language_label = editorGoToDefinitionLanguageLabel();
		if (language_label == NULL) {
			language_label = "source";
		}
		editorSetStatusMsg("Save this %s buffer before using go to %s", language_label, kind_lower);
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
	int request_result = request_fn(E.filename, E.syntax_language, E.cy, E.cx,
			&locations, &count, &timed_out);
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
		if (!editorPromptLocationChoice(kind_capitalized, count, &selected_index)) {
			editorLspFreeLocations(locations, count);
			return;
		}
	}

	const struct editorLspLocation *selected = &locations[selected_index];
	if (!editorJumpToDefinitionLocation(selected)) {
		editorSetStatusMsg("Unable to jump to %s", kind_lower);
		editorLspFreeLocations(locations, count);
		return;
	}

	editorSetStatusMsg("%s: %s:%d", kind_capitalized, editorBasenameFromPath(selected->path),
			selected->line + 1);
	editorLspFreeLocations(locations, count);
}

static void editorGoToDefinition(void) {
	editorRunLocationLookup("definition", "Definition", "definitions",
			editorLspRequestDefinition);
}

static void editorGoToImplementation(void) {
	editorRunLocationLookup("implementation", "Implementation", "implementations",
			editorLspRequestImplementation);
}

static void editorGoToSymbol(void) {
	if (!editorGoToDefinitionSupportedLanguage(E.syntax_language)) {
		editorSetStatusMsg(
				"Go to symbol is available for Go, C, C++, HTML, CSS/SCSS, JSON, and JavaScript files only");
		return;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		const char *language_label = editorGoToDefinitionLanguageLabel();
		if (language_label == NULL) {
			language_label = "source";
		}
		editorSetStatusMsg("Save this %s buffer before using go to symbol", language_label);
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
		if (!editorPromptLocationChoice("Symbol", count, &selected_index)) {
			editorLspFreeSymbols(symbols, count);
			return;
		}
	}

	const struct editorLspSymbol *selected = &symbols[selected_index];
	if (!editorJumpToPathLocation(E.filename, selected->line, selected->character, 0, 0)) {
		editorSetStatusMsg("Unable to jump to symbol");
		editorLspFreeSymbols(symbols, count);
		return;
	}

	editorSetStatusMsg("%s %s:%d", editorLspSymbolKindLabel(selected->kind),
			selected->name != NULL ? selected->name : "(unnamed)", selected->line + 1);
	editorLspFreeSymbols(symbols, count);
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

static int editorRowIsWordAt(const struct erow *row, int cx) {
	return cx >= 0 && cx < row->size && editorIsWordByte((unsigned char)row->chars[cx]);
}

static void editorMoveCursorWordLeft(void) {
	editorAlignCursorWithRowEnd();

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
			cx = E.rows[cy].size;
			continue;
		}

		struct erow *row = &E.rows[cy];
		cx = editorRowClampCxToCharBoundary(row, cx);
		if (cx > 0) {
			int scan = editorRowPrevCharIdx(row, cx);
			int found_word = 0;
			while (1) {
				if (editorRowIsWordAt(row, scan)) {
					found_word = 1;
					break;
				}
				if (scan == 0) {
					break;
				}
				int prev = editorRowPrevCharIdx(row, scan);
				if (prev >= scan) {
					break;
				}
				scan = prev;
			}
			if (found_word) {
				while (scan > 0) {
					int prev = editorRowPrevCharIdx(row, scan);
					if (prev >= scan || !editorRowIsWordAt(row, prev)) {
						break;
					}
					scan = prev;
				}
				cx = scan;
				break;
			}
		}

		if (cy == 0) {
			cx = 0;
			break;
		}
		cy--;
		cx = E.rows[cy].size;
	}

	(void)editorSetCursorFromPosition(cy, cx);
}

static void editorMoveCursorWordRight(void) {
	editorAlignCursorWithRowEnd();

	int cy = E.cy;
	int cx = E.cx;
	if (cy < 0) {
		cy = 0;
		cx = 0;
	}

	while (cy < E.numrows) {
		struct erow *row = &E.rows[cy];
		cx = editorRowClampCxToCharBoundary(row, cx);
		int found_word = 0;

		while (cx < row->size && !editorRowIsWordAt(row, cx)) {
			int next = editorRowNextCharIdx(row, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		while (cx < row->size && editorRowIsWordAt(row, cx)) {
			found_word = 1;
			int next = editorRowNextCharIdx(row, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		if (found_word) {
			break;
		}

		if (cy >= E.numrows - 1) {
			cx = row->size;
			break;
		}
		cy++;
		cx = 0;
	}

	(void)editorSetCursorFromPosition(cy, cx);
}

static int editorColumnSelectionCurrentRx(void) {
	if (E.cy >= 0 && E.cy < E.numrows) {
		return editorRowCxToRx(&E.rows[E.cy], E.cx);
	}
	return 0;
}

static void editorColumnSelectionEnsureActive(void) {
	if (E.column_select_active) {
		return;
	}
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.column_select_active = 1;
	E.column_select_anchor_cy = E.cy;
	E.column_select_anchor_rx = editorColumnSelectionCurrentRx();
	E.column_select_cursor_rx = E.column_select_anchor_rx;
}

static void editorColumnSelectionApplyCursorRx(void) {
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
		cx = editorRowRxToCx(&E.rows[cy], target_rx);
	}
	(void)editorSetCursorFromPosition(cy, cx);
}

static void editorColumnSelectionMove(int dy, int drx) {
	editorColumnSelectionEnsureActive();
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
	editorColumnSelectionApplyCursorRx();
}

static int editorProcessMappedAction(enum editorAction action, int *effects_out) {
	int effects = EDITOR_KEYPRESS_EFFECT_NONE;
	int drawer_search_cursor_or_edit = 0;
	if (editorHandleDrawerSearchMappedAction(action, &drawer_search_cursor_or_edit,
				editorProjectReplaceFromSearch)) {
		if (drawer_search_cursor_or_edit) {
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
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

	if (editorHandleTerminalDebugMappedAction(action)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	switch (action) {
		case EDITOR_ACTION_QUIT:
			editorHistoryBreakGroup();
			editorActionQuit();
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
			editorActionCloseTab();
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
		case EDITOR_ACTION_MAIN_MENU:
			editorHistoryBreakGroup();
			(void)editorDrawerMainMenuToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU ?
					"Main menu opened" : "Project drawer shown");
			break;
		case EDITOR_ACTION_GIT_DRAWER:
			editorHistoryBreakGroup();
			(void)editorDrawerGitToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_GIT ?
					(E.git_repo_root != NULL ? "Git changes shown" :
					"Not in a git repository") :
					"Project drawer shown");
			break;
		case EDITOR_ACTION_LSP_DRAWER:
			editorHistoryBreakGroup();
			(void)editorDrawerLspToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_LSP ?
					"LSP drawer shown" : "Project drawer shown");
			break;
		case EDITOR_ACTION_DAP_DRAWER:
			editorHistoryBreakGroup();
			(void)editorDrawerDapToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_DAP ?
					"DAP drawer shown" : "Project drawer shown");
			break;
		case EDITOR_ACTION_SPLIT_HORIZONTAL:
			editorHistoryBreakGroup();
			if (editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5) != NULL) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_SPLIT_VERTICAL:
			editorHistoryBreakGroup();
			if (editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5) != NULL) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_CLOSE_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutCloseFocused() != NULL) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_FOCUS_LEFT_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_LEFT)) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_FOCUS_RIGHT_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_RIGHT)) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_FOCUS_UP_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_UP)) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_FOCUS_DOWN_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_DOWN)) {
				editorPaneAnnounceFocus();
			}
			break;
		case EDITOR_ACTION_PANE_GROW:
			editorHistoryBreakGroup();
			(void)editorLayoutResizeFocused(1);
			break;
		case EDITOR_ACTION_PANE_SHRINK:
			editorHistoryBreakGroup();
			(void)editorLayoutResizeFocused(0);
			break;
		case EDITOR_ACTION_OPEN_SETTINGS:
			editorHistoryBreakGroup();
			editorOpenSettings();
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
		case EDITOR_ACTION_COLUMN_SELECT_UP:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				break;
			}
			editorColumnSelectionMove(-1, 0);
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_COLUMN_SELECT_DOWN:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				break;
			}
			editorColumnSelectionMove(1, 0);
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_COLUMN_SELECT_LEFT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				if (editorDrawerIsCollapsed()) {
					(void)editorDrawerSetCollapsed(0);
				}
				(void)editorDrawerResizeByDeltaForCols(-DRAWER_RESIZE_STEP, E.window_cols);
				break;
			}
			editorColumnSelectionMove(0, -1);
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_COLUMN_SELECT_RIGHT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				if (editorDrawerIsCollapsed()) {
					(void)editorDrawerSetCollapsed(0);
				}
				(void)editorDrawerResizeByDeltaForCols(DRAWER_RESIZE_STEP, E.window_cols);
				break;
			}
			editorColumnSelectionMove(0, 1);
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
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
			editorOpenFileSearchDrawer();
			break;
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorOpenProjectSearchDrawer();
			break;
		case EDITOR_ACTION_FIND:
			editorHistoryBreakGroup();
			editorFind();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_FIND_REPLACE:
			editorHistoryBreakGroup();
			editorFindReplace();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_LINE:
			editorHistoryBreakGroup();
			editorGoToLine();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_MATCHING_BRACKET:
			editorHistoryBreakGroup();
			if (E.pane_focus != EDITOR_PANE_DRAWER && editorJumpToMatchingBracket()) {
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
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
		case EDITOR_ACTION_GOTO_DEFINITION:
			editorHistoryBreakGroup();
			editorGoToDefinition();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_IMPLEMENTATION:
			editorHistoryBreakGroup();
			editorGoToImplementation();
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			break;
		case EDITOR_ACTION_GOTO_SYMBOL:
			editorHistoryBreakGroup();
			editorGoToSymbol();
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
		case EDITOR_ACTION_MOVE_WORD_LEFT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerCollapseSelection(E.window_rows);
			} else {
				editorColumnSelectionClear();
				editorMoveCursorWordLeft();
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_WORD_RIGHT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerExpandSelection(E.window_rows);
			} else {
				editorColumnSelectionClear();
				editorMoveCursorWordRight();
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
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
				if (editorDrawerMoveSelectionBy(-1, E.window_rows)) {
					editorDrawerPreviewSelectionAfterMove(editorJumpToPathLocation);
				}
			} else {
				editorColumnSelectionClear();
				editorMoveCursor(ARROW_UP);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_DOWN:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				if (editorDrawerMoveSelectionBy(1, E.window_rows)) {
					editorDrawerPreviewSelectionAfterMove(editorJumpToPathLocation);
				}
			} else {
				editorColumnSelectionClear();
				editorMoveCursor(ARROW_DOWN);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_LEFT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerCollapseSelection(E.window_rows);
			} else {
				editorColumnSelectionClear();
				editorMoveCursor(ARROW_LEFT);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_MOVE_RIGHT:
			editorHistoryBreakGroup();
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				(void)editorDrawerExpandSelection(E.window_rows);
			} else {
				editorColumnSelectionClear();
				editorMoveCursor(ARROW_RIGHT);
				effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
			}
			break;
		case EDITOR_ACTION_NEWLINE:
			if (E.pane_focus == EDITOR_PANE_DRAWER) {
				editorHistoryBreakGroup();
				editorResetDrawerClickTracking();
				if (editorDrawerSelectedIsDirectory()) {
					(void)editorDrawerToggleSelectionExpanded(E.window_rows);
				} else if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
					if (editorOpenSelectedGitDiff()) {
						E.pane_focus = EDITOR_PANE_TEXT;
						effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
					}
				} else if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
					if (editorJumpToSelectedLspDrawerLocation(0, editorJumpToPathLocation)) {
						E.pane_focus = EDITOR_PANE_TEXT;
						effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
					}
				} else if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
					int launch_idx = -1;
					int default_idx = -1;
					if (editorDrawerSelectedDapLaunch(&launch_idx)) {
						E.dap_selected_launch = launch_idx;
						if (editorDapStartLaunch(launch_idx)) {
							effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
						}
					} else if (editorDrawerSelectedDapDefault(&default_idx)) {
						if (editorDapCreateProjectLaunchFromDefault(default_idx,
									E.drawer_root_path)) {
							effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
						}
					} else if (editorJumpToSelectedDapDrawerLocation(0, editorJumpToPathLocation)) {
						E.pane_focus = EDITOR_PANE_TEXT;
						effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
					}
				} else {
					enum editorAction menu_action = EDITOR_ACTION_COUNT;
					if (editorDrawerSelectedMenuAction(&menu_action)) {
						int mapped_effects = EDITOR_KEYPRESS_EFFECT_NONE;
						if (editorProcessMappedAction(menu_action, &mapped_effects)) {
							if (effects_out != NULL) {
								*effects_out = effects | mapped_effects;
							}
							return 1;
						}
						effects |= mapped_effects;
					} else if (editorDrawerOpenSelectedFileInTab()) {
						E.pane_focus = EDITOR_PANE_TEXT;
					}
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
				if (E.column_select_active) {
					editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
					int dirty_before = E.dirty;
					editorColumnSelectionDeleteForward();
					editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
				} else if (editorGetSelectionRange(&range)) {
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
				if (E.column_select_active) {
					editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
					int dirty_before = E.dirty;
					editorColumnSelectionBackspace();
					editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
				} else if (editorGetSelectionRange(&range)) {
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
		case EDITOR_ACTION_TOGGLE_COMMENT:
			editorPinActivePreviewForEdit();
			editorToggleCommentLines();
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
		int closed = editorTerminalPaneCloseExited(&E.layout_root,
				&E.focused_leaf, &E.dap_terminal_leaf);
		if (closed > 0 && E.focused_leaf != NULL &&
				E.focused_leaf != prev_focus) {
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
					editorPinActivePreviewForEdit();
					editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
					int dirty_before = E.dirty;
					int applied = editorAutocompleteAcceptSelection();
					editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
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
		// Mouse input can move cursor/selection, but it should not create edit history entries.
		editorHistoryBreakGroup();
		int mouse_effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
		(void)editorHandleMouseEventDispatch(DRAWER_DOUBLE_CLICK_THRESHOLD_MS,
				TEXT_MULTI_CLICK_THRESHOLD_MS, editorProcessMappedAction,
				editorJumpToPathLocation, editorCtrlClickGoToDefinitionAction,
				&mouse_effects);
		if ((mouse_effects & EDITOR_MOUSE_DISPATCH_EFFECT_VIEWPORT_SCROLL) != 0) {
			effects |= EDITOR_KEYPRESS_EFFECT_VIEWPORT_SCROLL;
		}
		if ((mouse_effects & EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT) != 0) {
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
		}
	} else {
		if (editorClearHoverLinkState()) {
			effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
		}
		/* In focused terminal panes keys go to PTY, except terminal_prefix. */
		if (E.focused_leaf != NULL && !E.focused_leaf->is_split &&
				E.focused_leaf->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
				E.focused_leaf->as.leaf.kind_state != NULL &&
				E.pane_focus != EDITOR_PANE_DRAWER) {
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
						(struct editorTerminalPane *)E.focused_leaf->as.leaf.kind_state;
				(void)editorTerminalPaneSendKey(terminal, c);
				return;
			}
		}
		if (editorKeymapLookupAction(&E.keymap, c, &action)) {
			int mapped_effects = EDITOR_KEYPRESS_EFFECT_NONE;
			mapped_action = 1;
			if (editorProcessMappedAction(action, &mapped_effects)) {
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
			} else if (E.pane_focus != EDITOR_PANE_DRAWER) {
				if (editorActiveTabIsReadOnly()) {
					editorSetStatusMsg(editorActiveTabIsUnsupportedFile() ?
							"File is unsupported" : "Task log is read-only");
					goto done;
				}
				if (E.column_select_active && c >= 0x20 && c < 0x7f) {
					editorPinActivePreviewForEdit();
					editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
					int dirty_before = E.dirty;
					editorColumnSelectionInsertChar(c);
					editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (c == '\t' && editorIndentSelection()) {
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (editorReplaceSelectionWithChar(c)) {
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (editorTrySkipOverClosingPair(c)) {
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else if (editorTryAutoClosePair(c)) {
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				} else {
					editorClearSelectionMode();
					editorPinActivePreviewForEdit();
					editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
					int dirty_before = E.dirty;
					editorInsertChar(c);
					editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
					editorAutocompleteOnCharInserted(c);
					effects |= EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT;
				}
			}
		}
	}

done:
	editorFileTabActionsAfterKeypress(mapped_action, action);
	if ((effects & EDITOR_KEYPRESS_EFFECT_CURSOR_OR_EDIT) != 0) {
		editorViewportEnsureCursorVisible();
	}

	editorRecoveryMaybeAutosaveOnActivity();
}
