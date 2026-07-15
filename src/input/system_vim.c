#include "input/system_vim.h"

#include "config/theme_config.h"
#include "editing/buffer_core.h"
#include "editing/buffer_search.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/jumplist.h"
#include "editing/selection.h"
#include "input/actions_file_tab.h"
#include "input/actions_language.h"
#include "input/dispatch.h"
#include "input/prompt.h"
#include "input/text_pairs.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/alloc.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum vimSystemMode {
	VIM_SYSTEM_MODE_NORMAL = 0,
	VIM_SYSTEM_MODE_INSERT,
	VIM_SYSTEM_MODE_VISUAL,
	VIM_SYSTEM_MODE_VISUAL_LINE,
	VIM_SYSTEM_MODE_VISUAL_BLOCK
};

enum vimSystemMotion {
	VIM_SYSTEM_MOTION_LEFT = 0,
	VIM_SYSTEM_MOTION_DOWN,
	VIM_SYSTEM_MOTION_UP,
	VIM_SYSTEM_MOTION_RIGHT,
	VIM_SYSTEM_MOTION_WORD_FORWARD,
	VIM_SYSTEM_MOTION_WORD_BACKWARD,
	VIM_SYSTEM_MOTION_WORD_END,
	VIM_SYSTEM_MOTION_LINE_START,
	VIM_SYSTEM_MOTION_LINE_END,
	VIM_SYSTEM_MOTION_FIRST_NONBLANK,
	VIM_SYSTEM_MOTION_FIRST_LINE,
	VIM_SYSTEM_MOTION_LAST_LINE,
	VIM_SYSTEM_MOTION_FIND_FORWARD,
	VIM_SYSTEM_MOTION_FIND_BACKWARD,
	VIM_SYSTEM_MOTION_TILL_FORWARD,
	VIM_SYSTEM_MOTION_TILL_BACKWARD,
	VIM_SYSTEM_MOTION_PARAGRAPH_FORWARD,
	VIM_SYSTEM_MOTION_PARAGRAPH_BACKWARD,
	VIM_SYSTEM_MOTION_MATCH_BRACKET,
	VIM_SYSTEM_MOTION_SCREEN_TOP,
	VIM_SYSTEM_MOTION_SCREEN_MIDDLE,
	VIM_SYSTEM_MOTION_SCREEN_BOTTOM
};

enum vimSystemMotionParse {
	VIM_SYSTEM_MOTION_PARSE_NONE = 0,
	VIM_SYSTEM_MOTION_PARSE_PENDING,
	VIM_SYSTEM_MOTION_PARSE_FOUND
};

enum vimSystemOperator {
	VIM_SYSTEM_OPERATOR_NONE = 0,
	VIM_SYSTEM_OPERATOR_DELETE = 'd',
	VIM_SYSTEM_OPERATOR_CHANGE = 'c',
	VIM_SYSTEM_OPERATOR_YANK = 'y',
	VIM_SYSTEM_OPERATOR_INDENT = '>',
	VIM_SYSTEM_OPERATOR_DEDENT = '<',
	VIM_SYSTEM_OPERATOR_REFLOW = 'q'
};

enum vimSystemCharClass { VIM_SYSTEM_CHAR_SPACE = 0, VIM_SYSTEM_CHAR_WORD, VIM_SYSTEM_CHAR_PUNCT };

static enum vimSystemMode vimSystemMode(void) {
	switch (E.input_vim_mode) {
		case VIM_SYSTEM_MODE_INSERT:
			return VIM_SYSTEM_MODE_INSERT;
		case VIM_SYSTEM_MODE_VISUAL:
			return VIM_SYSTEM_MODE_VISUAL;
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return VIM_SYSTEM_MODE_VISUAL_LINE;
		case VIM_SYSTEM_MODE_VISUAL_BLOCK:
			return VIM_SYSTEM_MODE_VISUAL_BLOCK;
		default:
			return VIM_SYSTEM_MODE_NORMAL;
	}
}

/* Per-mode bindable commands. Each command has a fixed canonical key (the key
 * the handlers switch on) and a mutable bound key resolved from `[keymap.vim]`.
 * Rebinding relocates a command's single key: the new key triggers it and the
 * canonical default is disabled unless it is itself a binding for another
 * command. Structural keys (digits, `"`, register letters) are never bound. */
struct vimBindableCommand {
	const char *name;
	enum vimSystemMode mode;
	int canonical_key;
	int bound_key;
};

static struct vimBindableCommand g_vim_commands[] = {
        {"move_left", VIM_SYSTEM_MODE_NORMAL, 'h', 'h'},
        {"move_down", VIM_SYSTEM_MODE_NORMAL, 'j', 'j'},
        {"move_up", VIM_SYSTEM_MODE_NORMAL, 'k', 'k'},
        {"move_right", VIM_SYSTEM_MODE_NORMAL, 'l', 'l'},
        {"word_forward", VIM_SYSTEM_MODE_NORMAL, 'w', 'w'},
        {"word_backward", VIM_SYSTEM_MODE_NORMAL, 'b', 'b'},
        {"word_end", VIM_SYSTEM_MODE_NORMAL, 'e', 'e'},
        {"line_start", VIM_SYSTEM_MODE_NORMAL, '0', '0'},
        {"line_end", VIM_SYSTEM_MODE_NORMAL, '$', '$'},
        {"first_nonblank", VIM_SYSTEM_MODE_NORMAL, '^', '^'},
        {"last_line", VIM_SYSTEM_MODE_NORMAL, 'G', 'G'},
        {"insert", VIM_SYSTEM_MODE_NORMAL, 'i', 'i'},
        {"append", VIM_SYSTEM_MODE_NORMAL, 'a', 'a'},
        {"insert_line_start", VIM_SYSTEM_MODE_NORMAL, 'I', 'I'},
        {"append_line_end", VIM_SYSTEM_MODE_NORMAL, 'A', 'A'},
        {"open_below", VIM_SYSTEM_MODE_NORMAL, 'o', 'o'},
        {"open_above", VIM_SYSTEM_MODE_NORMAL, 'O', 'O'},
        {"delete_char", VIM_SYSTEM_MODE_NORMAL, 'x', 'x'},
        {"delete", VIM_SYSTEM_MODE_NORMAL, 'd', 'd'},
        {"change", VIM_SYSTEM_MODE_NORMAL, 'c', 'c'},
        {"yank", VIM_SYSTEM_MODE_NORMAL, 'y', 'y'},
        {"delete_to_eol", VIM_SYSTEM_MODE_NORMAL, 'D', 'D'},
        {"change_to_eol", VIM_SYSTEM_MODE_NORMAL, 'C', 'C'},
        {"yank_line", VIM_SYSTEM_MODE_NORMAL, 'Y', 'Y'},
        {"paste_after", VIM_SYSTEM_MODE_NORMAL, 'p', 'p'},
        {"paste_before", VIM_SYSTEM_MODE_NORMAL, 'P', 'P'},
        {"search_forward", VIM_SYSTEM_MODE_NORMAL, '/', '/'},
        {"search_backward", VIM_SYSTEM_MODE_NORMAL, '?', '?'},
        {"search_next", VIM_SYSTEM_MODE_NORMAL, 'n', 'n'},
        {"search_prev", VIM_SYSTEM_MODE_NORMAL, 'N', 'N'},
        {"ex_command", VIM_SYSTEM_MODE_NORMAL, ':', ':'},
        {"visual", VIM_SYSTEM_MODE_NORMAL, 'v', 'v'},
        {"visual_line", VIM_SYSTEM_MODE_NORMAL, 'V', 'V'},
        {"move_left", VIM_SYSTEM_MODE_VISUAL, 'h', 'h'},
        {"move_down", VIM_SYSTEM_MODE_VISUAL, 'j', 'j'},
        {"move_up", VIM_SYSTEM_MODE_VISUAL, 'k', 'k'},
        {"move_right", VIM_SYSTEM_MODE_VISUAL, 'l', 'l'},
        {"word_forward", VIM_SYSTEM_MODE_VISUAL, 'w', 'w'},
        {"word_backward", VIM_SYSTEM_MODE_VISUAL, 'b', 'b'},
        {"word_end", VIM_SYSTEM_MODE_VISUAL, 'e', 'e'},
        {"line_start", VIM_SYSTEM_MODE_VISUAL, '0', '0'},
        {"line_end", VIM_SYSTEM_MODE_VISUAL, '$', '$'},
        {"first_nonblank", VIM_SYSTEM_MODE_VISUAL, '^', '^'},
        {"last_line", VIM_SYSTEM_MODE_VISUAL, 'G', 'G'},
        {"delete", VIM_SYSTEM_MODE_VISUAL, 'd', 'd'},
        {"change", VIM_SYSTEM_MODE_VISUAL, 'c', 'c'},
        {"yank", VIM_SYSTEM_MODE_VISUAL, 'y', 'y'},
        {"delete_char", VIM_SYSTEM_MODE_VISUAL, 'x', 'x'},
        {"paste", VIM_SYSTEM_MODE_VISUAL, 'p', 'p'},
        {"normal_mode", VIM_SYSTEM_MODE_INSERT, '\x1b', '\x1b'},
};

#define VIM_COMMAND_COUNT (sizeof(g_vim_commands) / sizeof(g_vim_commands[0]))
static const size_t g_vim_command_count = VIM_COMMAND_COUNT;

/* Leader sequences: `<leader>` followed by one key dispatches an editor action.
 * Leader is space by default. Both the leader key and each sub-key are bindable
 * via `[keymap.vim]` (`normal.leader` and `leader.<command>`). */
#define VIM_SYSTEM_LEADER_DEFAULT ' '

struct vimLeaderBinding {
	const char *name;
	enum editorAction action;
	int canonical_key;
	int bound_key;
};

static struct vimLeaderBinding g_vim_leader_map[] = {
        {"find_file", EDITOR_ACTION_FIND_FILE, 'p', 'p'},
        {"project_search", EDITOR_ACTION_PROJECT_SEARCH, 'f', 'f'},
        {"explorer_drawer", EDITOR_ACTION_EXPLORER_DRAWER, 'e', 'e'},
        {"toggle_drawer", EDITOR_ACTION_TOGGLE_DRAWER, -1, -1},
        {"main_menu", EDITOR_ACTION_MAIN_MENU, 'm', 'm'},
        {"git_drawer", EDITOR_ACTION_GIT_DRAWER, 'g', 'g'},
        {"lsp_drawer", EDITOR_ACTION_LSP_DRAWER, 'l', 'l'},
        {"dap_drawer", EDITOR_ACTION_DAP_DRAWER, 'd', 'd'},
        {"git_blame_details", EDITOR_ACTION_GIT_BLAME_DETAILS, -1, -1},
        {"latex_forward_search", EDITOR_ACTION_LATEX_FORWARD_SEARCH, -1, -1},
};

#define VIM_LEADER_MAP_COUNT (sizeof(g_vim_leader_map) / sizeof(g_vim_leader_map[0]))
static const size_t g_vim_leader_count = VIM_LEADER_MAP_COUNT;

static int g_vim_leader_key = VIM_SYSTEM_LEADER_DEFAULT;

struct vimExCommand {
	const char *name;
	enum editorAction action;
};

static const struct vimExCommand g_vim_ex_commands[] = {
        {"split", EDITOR_ACTION_SPLIT_HORIZONTAL},
        {"sp", EDITOR_ACTION_SPLIT_HORIZONTAL},
        {"vsplit", EDITOR_ACTION_SPLIT_VERTICAL},
        {"vs", EDITOR_ACTION_SPLIT_VERTICAL},
        {"vsp", EDITOR_ACTION_SPLIT_VERTICAL},
        {"close", EDITOR_ACTION_CLOSE_PANE},
        {"clo", EDITOR_ACTION_CLOSE_PANE},
        {"tabclose", EDITOR_ACTION_CLOSE_TAB},
        {"tabc", EDITOR_ACTION_CLOSE_TAB},
        {"bd", EDITOR_ACTION_CLOSE_TAB},
        {"bdelete", EDITOR_ACTION_CLOSE_TAB},
        {"term", EDITOR_ACTION_TERMINAL_OPEN},
        {"terminal", EDITOR_ACTION_TERMINAL_OPEN},
        {"vterm", EDITOR_ACTION_TERMINAL_OPEN_VERTICAL},
        {"tabterm", EDITOR_ACTION_TERMINAL_NEW_TAB},
        {"only", EDITOR_ACTION_CLOSE_OTHER_PANES},
        {"on", EDITOR_ACTION_CLOSE_OTHER_PANES},
        {"tabnew", EDITOR_ACTION_NEW_TAB},
};

static const size_t g_vim_ex_command_count =
        sizeof(g_vim_ex_commands) / sizeof(g_vim_ex_commands[0]);

static const char *const g_vim_ex_builtin_commands[] = {"w",    "q",   "q!",  "wq",    "x",    "e",
                                                        "edit", "git", "lsp", "latex", "jumps"};

static const size_t g_vim_ex_builtin_command_count =
        sizeof(g_vim_ex_builtin_commands) / sizeof(g_vim_ex_builtin_commands[0]);

void editorVimKeymapResetDefaults(void) {
	for (size_t i = 0; i < g_vim_command_count; i++) {
		g_vim_commands[i].bound_key = g_vim_commands[i].canonical_key;
	}
	for (size_t i = 0; i < g_vim_leader_count; i++) {
		g_vim_leader_map[i].bound_key = g_vim_leader_map[i].canonical_key;
	}
	g_vim_leader_key = VIM_SYSTEM_LEADER_DEFAULT;
}

static int vimSystemLeaderLookup(int c, enum editorAction *action_out) {
	for (size_t i = 0; i < g_vim_leader_count; i++) {
		if (g_vim_leader_map[i].bound_key == c) {
			if (action_out != NULL) {
				*action_out = g_vim_leader_map[i].action;
			}
			return 1;
		}
	}
	return 0;
}

/* `g`-prefixed navigation: the second key of a `g` sequence that maps to an
 * editor action. `gg` and any other key fall through to the motion parser. */
static int vimSystemGPrefixAction(int c, enum editorAction *action_out) {
	enum editorAction action;

	switch (c) {
		case 'b':
			action = EDITOR_ACTION_GIT_BLAME_DETAILS;
			break;
		case 'd':
			action = EDITOR_ACTION_GOTO_DEFINITION;
			break;
		case 'i':
			action = EDITOR_ACTION_GOTO_IMPLEMENTATION;
			break;
		case 'r':
			action = EDITOR_ACTION_GOTO_REFERENCES;
			break;
		case 's':
			action = EDITOR_ACTION_GOTO_SYMBOL;
			break;
		case 'S':
			action = EDITOR_ACTION_LSP_DRAWER;
			break;
		default:
			return 0;
	}
	if (action_out != NULL) {
		*action_out = action;
	}
	return 1;
}

static int vimSystemCtrlWAction(int c, enum editorAction *action_out) {
	enum editorAction action;

	switch (c) {
		case 's':
			action = EDITOR_ACTION_SPLIT_HORIZONTAL;
			break;
		case 'v':
			action = EDITOR_ACTION_SPLIT_VERTICAL;
			break;
		case 'c':
		case 'q':
			action = EDITOR_ACTION_CLOSE_PANE;
			break;
		case 'o':
			action = EDITOR_ACTION_CLOSE_OTHER_PANES;
			break;
		case 't':
			action = EDITOR_ACTION_TERMINAL_NEW_TAB;
			break;
		case 'w':
		case CTRL_KEY('w'):
			action = EDITOR_ACTION_FOCUS_NEXT_PANE;
			break;
		case 'W':
			action = EDITOR_ACTION_FOCUS_PREV_PANE;
			break;
		case 'h':
		case ARROW_LEFT:
			action = EDITOR_ACTION_FOCUS_LEFT_PANE;
			break;
		case 'j':
		case ARROW_DOWN:
			action = EDITOR_ACTION_FOCUS_DOWN_PANE;
			break;
		case 'k':
		case ARROW_UP:
			action = EDITOR_ACTION_FOCUS_UP_PANE;
			break;
		case 'l':
		case ARROW_RIGHT:
			action = EDITOR_ACTION_FOCUS_RIGHT_PANE;
			break;
		case 'H':
			action = EDITOR_ACTION_MOVE_TAB_LEFT_PANE;
			break;
		case 'J':
			action = EDITOR_ACTION_MOVE_TAB_DOWN_PANE;
			break;
		case 'K':
			action = EDITOR_ACTION_MOVE_TAB_UP_PANE;
			break;
		case 'L':
			action = EDITOR_ACTION_MOVE_TAB_RIGHT_PANE;
			break;
		default:
			return 0;
	}
	if (action_out != NULL) {
		*action_out = action;
	}
	return 1;
}

int editorVimLeaderKey(void) {
	return g_vim_leader_key;
}

int editorVimLeaderAction(int c, int *action_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;
	int ok = vimSystemLeaderLookup(c, &action);
	if (ok && action_out != NULL) {
		*action_out = (int)action;
	}
	return ok;
}

int editorVimCtrlWAction(int c, int *action_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;
	int ok = vimSystemCtrlWAction(c, &action);
	if (ok && action_out != NULL) {
		*action_out = (int)action;
	}
	return ok;
}

static enum vimSystemMode vimSystemRemapLookupMode(enum vimSystemMode mode) {
	return mode == VIM_SYSTEM_MODE_VISUAL_LINE ? VIM_SYSTEM_MODE_VISUAL : mode;
}

/* Translate an input key into the canonical key the handlers expect. When the
 * key is a canonical default whose command was rebound to another key, it is
 * reported as disabled so the caller can ignore it. */
static int vimSystemRemapKey(enum vimSystemMode mode, int c, int *disabled_out) {
	enum vimSystemMode lookup = vimSystemRemapLookupMode(mode);

	if (disabled_out != NULL) {
		*disabled_out = 0;
	}
	for (size_t i = 0; i < g_vim_command_count; i++) {
		if (g_vim_commands[i].mode == lookup && g_vim_commands[i].bound_key == c) {
			return g_vim_commands[i].canonical_key;
		}
	}
	for (size_t i = 0; i < g_vim_command_count; i++) {
		if (g_vim_commands[i].mode == lookup && g_vim_commands[i].canonical_key == c &&
		    g_vim_commands[i].bound_key != c) {
			if (disabled_out != NULL) {
				*disabled_out = 1;
			}
			return c;
		}
	}
	return c;
}

static void vimSystemResetPending(void) {
	E.input_vim_pending_g = 0;
	E.input_vim_pending_operator = VIM_SYSTEM_OPERATOR_NONE;
	E.input_vim_pending_operator_g = 0;
	E.input_vim_count = 0;
	E.input_vim_operator_count = 0;
	E.input_vim_active_register = 0;
	E.input_vim_pending_register = 0;
	E.input_vim_pending_text_object = 0;
	E.input_vim_pending_leader = 0;
	E.input_vim_pending_ctrl_w = 0;
	E.input_vim_pending_find = 0;
	E.input_vim_pending_replace = 0;
	E.input_vim_pending_z = 0;
	E.input_vim_pending_mark = 0;
	E.input_vim_pending_bracket = 0;
	E.input_vim_visual_selection_half_open = 0;
}

static void vimSystemSetMode(enum vimSystemMode mode) {
	E.input_vim_mode = mode;
	vimSystemResetPending();
}

void editorVimRegistersClear(void) {
	for (int i = 0; i < 26; i++) {
		free(E.vim_registers[i].text);
		E.vim_registers[i].text = NULL;
		E.vim_registers[i].len = 0;
		E.vim_registers[i].linewise = 0;
	}
	E.vim_default_register_linewise = 0;
}

void editorVimMarksClear(void) {
	for (int i = 0; i < 26; i++) {
		free(E.vim_marks[i].filename);
		E.vim_marks[i].filename = NULL;
		E.vim_marks[i].set = 0;
		E.vim_marks[i].cy = 0;
		E.vim_marks[i].cx = 0;
	}
}

/* Store yank/delete text into the active register (a named register if one was
 * selected via `"x`, otherwise the default register backed by the clipboard). */
static int vimSystemRegisterStore(const char *text, size_t len, int linewise) {
	int reg = E.input_vim_active_register;

	if (reg >= 'a' && reg <= 'z') {
		struct editorVimRegister *slot = &E.vim_registers[reg - 'a'];
		char *copy = editorMalloc(len + 1);
		if (copy == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
		if (len > 0) {
			memcpy(copy, text, len);
		}
		copy[len] = '\0';
		free(slot->text);
		slot->text = copy;
		slot->len = len;
		slot->linewise = linewise ? 1 : 0;
		return 1;
	}
	if (!editorClipboardSet(text, len)) {
		return 0;
	}
	E.vim_default_register_linewise = linewise ? 1 : 0;
	return 1;
}

/* Returns NULL when the active register is empty. */
static const char *vimSystemRegisterFetch(size_t *len_out, int *linewise_out) {
	int reg = E.input_vim_active_register;

	if (reg >= 'a' && reg <= 'z') {
		struct editorVimRegister *slot = &E.vim_registers[reg - 'a'];
		if (len_out != NULL) {
			*len_out = slot->len;
		}
		if (linewise_out != NULL) {
			*linewise_out = slot->linewise;
		}
		return slot->text;
	}
	if (linewise_out != NULL) {
		*linewise_out = E.vim_default_register_linewise;
	}
	return editorClipboardGet(len_out);
}

static int vimSystemRejectReadOnlyMutation(void) {
	if (!editorActiveTabIsReadOnly()) {
		return 0;
	}
	editorSetStatusMsg(editorActiveTabIsUnsupportedFile() ? "File is unsupported"
	                                                      : "Task log is read-only");
	return 1;
}

static int vimSystemEffectiveCount(void) {
	return E.input_vim_count > 0 ? E.input_vim_count : 1;
}

static int vimSystemConsumeCountKey(int c) {
	if (!isdigit(c) || (c == '0' && E.input_vim_count == 0)) {
		return 0;
	}
	if (E.input_vim_count < 100000000) {
		E.input_vim_count = E.input_vim_count * 10 + (c - '0');
	}
	return 1;
}

static void vimSystemConsumeRegisterKey(int c) {
	E.input_vim_pending_register = 0;
	if (c >= 'a' && c <= 'z') {
		E.input_vim_active_register = c;
	} else if (c >= 'A' && c <= 'Z') {
		E.input_vim_active_register = c - 'A' + 'a';
	}
}

static int vimSystemOperatorTotalCount(void) {
	int operator_count = E.input_vim_operator_count > 0 ? E.input_vim_operator_count : 1;
	int motion_count = E.input_vim_count > 0 ? E.input_vim_count : 1;
	return operator_count * motion_count;
}

const char *editorVimModeLabel(void) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return "INSERT";
		case VIM_SYSTEM_MODE_VISUAL:
			return "VISUAL";
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return "VISUAL LINE";
		case VIM_SYSTEM_MODE_VISUAL_BLOCK:
			return "VISUAL BLOCK";
		default:
			return "NORMAL";
	}
}

/* Theme ANSI palette index for the current mode's status-segment background,
 * echoing the conventional Vim/lualine mode colors. */
static int vimSystemStatusColor(void) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return EDITOR_THEME_ANSI_GREEN;
		case VIM_SYSTEM_MODE_VISUAL:
		case VIM_SYSTEM_MODE_VISUAL_LINE:
		case VIM_SYSTEM_MODE_VISUAL_BLOCK:
			return EDITOR_THEME_ANSI_MAGENTA;
		default:
			return EDITOR_THEME_ANSI_BLUE;
	}
}

static int vimSystemCursorStyle(void) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return -1;
		case VIM_SYSTEM_MODE_VISUAL:
		case VIM_SYSTEM_MODE_VISUAL_LINE:
		case VIM_SYSTEM_MODE_VISUAL_BLOCK:
		case VIM_SYSTEM_MODE_NORMAL:
		default:
			return EDITOR_CURSOR_STYLE_BLOCK;
	}
}

static void vimSystemSyncVisualSelectionFlags(void) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_VISUAL:
			E.selection_inclusive = E.input_vim_visual_selection_half_open ? 0 : 1;
			E.selection_linewise = 0;
			break;
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			E.selection_inclusive = 0;
			E.selection_linewise = 1;
			break;
		default:
			E.selection_inclusive = 0;
			E.selection_linewise = 0;
			break;
	}
}

static void vimSystemSetVisualHalfOpen(int half_open) {
	E.input_vim_visual_selection_half_open = half_open ? 1 : 0;
	vimSystemSyncVisualSelectionFlags();
}

static void vimSystemBeginVisual(enum vimSystemMode mode) {
	size_t cursor_offset = E.cursor_offset;
	(void)editorBufferPosToOffset(E.cy, E.cx, &cursor_offset);
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = cursor_offset;
	vimSystemSetMode(mode);
	vimSystemSyncVisualSelectionFlags();
}

void editorVimBeginSelection(size_t anchor_offset) {
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = anchor_offset;
	vimSystemSetMode(VIM_SYSTEM_MODE_VISUAL);
	vimSystemSetVisualHalfOpen(1);
}

void editorVimBeginLineSelection(size_t anchor_offset) {
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = anchor_offset;
	vimSystemSetMode(VIM_SYSTEM_MODE_VISUAL_LINE);
	vimSystemSyncVisualSelectionFlags();
}

void editorVimBeginColumnSelection(void) {
	int anchor_cx = 0;
	struct editorLineView line = {0};

	if (E.column_select_anchor_cy >= 0 && E.column_select_anchor_cy < E.numrows &&
	    editorDocumentLineView(E.document, E.column_select_anchor_cy, &line)) {
		anchor_cx = editorBytesRxToCx(line.data, line.size, E.column_select_anchor_rx);
		editorLineViewRelease(&line);
	}
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.column_select_active = 1;
	vimSystemSetMode(VIM_SYSTEM_MODE_VISUAL_BLOCK);
	E.input_vim_block_anchor_cx = anchor_cx;
	vimSystemSyncVisualSelectionFlags();
}

void editorVimCancelSelection(void) {
	enum vimSystemMode mode = vimSystemMode();

	editorClearSelectionState();
	if (mode == VIM_SYSTEM_MODE_VISUAL || mode == VIM_SYSTEM_MODE_VISUAL_LINE ||
	    mode == VIM_SYSTEM_MODE_VISUAL_BLOCK) {
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
	}
}

static int vimSystemRxAt(int cy, int cx) {
	struct editorLineView line = {0};
	int rx = 0;

	if (cy < 0 || cy >= E.numrows || !editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	rx = editorBytesCxToRx(line.data, line.size, cx);
	editorLineViewRelease(&line);
	return rx;
}

static int vimSystemNextClusterCx(int cy, int cx) {
	struct editorLineView line = {0};
	int next = cx;

	if (cy < 0 || cy >= E.numrows || !editorDocumentLineView(E.document, cy, &line)) {
		return cx;
	}
	next = cx < line.size ? editorBytesNextClusterIdx(line.data, line.size, cx) : line.size;
	editorLineViewRelease(&line);
	return next;
}

/* Recompute the column-selection rect from the block's fixed anchor cell and the
 * live cursor cell, keeping both columns inclusive whichever side leads. */
static void vimSystemBlockSync(void) {
	int anchor_cy = E.column_select_anchor_cy;
	int anchor_cx = E.input_vim_block_anchor_cx;
	int anchor_l = vimSystemRxAt(anchor_cy, anchor_cx);
	int anchor_r = vimSystemRxAt(anchor_cy, vimSystemNextClusterCx(anchor_cy, anchor_cx));
	int cur_l = vimSystemRxAt(E.cy, E.cx);
	int cur_r = vimSystemRxAt(E.cy, vimSystemNextClusterCx(E.cy, E.cx));

	if (cur_l >= anchor_l) {
		E.column_select_anchor_rx = anchor_l;
		E.column_select_cursor_rx = cur_r;
	} else {
		E.column_select_anchor_rx = anchor_r;
		E.column_select_cursor_rx = cur_l;
	}
}

static void vimSystemBeginVisualBlock(void) {
	editorClearSelectionState();
	vimSystemSetMode(VIM_SYSTEM_MODE_VISUAL_BLOCK);
	E.selection_mode_active = 0;
	E.column_select_active = 1;
	E.column_select_anchor_cy = E.cy;
	E.input_vim_block_anchor_cx = E.cx;
	vimSystemBlockSync();
}

static void vimSystemExitVisualBlock(void) {
	editorColumnSelectionClear();
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
}

static enum vimSystemCharClass vimSystemClassAt(const struct editorLineView *line, int cx) {
	unsigned char byte = (unsigned char)line->data[cx];

	if (isspace(byte)) {
		return VIM_SYSTEM_CHAR_SPACE;
	}
	if (isalnum(byte) || byte == '_' || byte >= 0x80) {
		return VIM_SYSTEM_CHAR_WORD;
	}
	return VIM_SYSTEM_CHAR_PUNCT;
}

static int vimSystemLineFirstNonblank(int cy) {
	struct editorLineView line = {0};
	int cx = 0;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	while (cx < line.size && isspace((unsigned char)line.data[cx])) {
		int next = editorBytesNextClusterIdx(line.data, line.size, cx);
		if (next <= cx) {
			break;
		}
		cx = next;
	}
	if (cx == line.size) {
		cx = 0;
	}
	editorLineViewRelease(&line);
	return cx;
}

static int vimSystemLineLastCluster(int cy) {
	struct editorLineView line = {0};
	int cx = 0;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	if (line.size > 0) {
		cx = editorBytesPrevClusterIdx(line.data, line.size, line.size);
	}
	editorLineViewRelease(&line);
	return cx;
}

static int vimSystemLineEndCx(int cy) {
	return (int)editorDocumentLineLength(E.document, cy);
}

static void vimSystemAddEditEffect(int *effects_out) {
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
}

static int vimSystemSetCursor(int cy, int cx, int *effects_out) {
	size_t old_offset = E.cursor_offset;
	size_t offset = 0;

	if (!editorBufferPosToOffset(cy, cx, &offset) || !editorSyncCursorFromOffset(offset)) {
		return 0;
	}
	editorHistoryBreakGroup();
	if (effects_out != NULL && E.cursor_offset != old_offset) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
	return 1;
}

static int vimSystemSyncCursor(void) {
	size_t offset = 0;

	if (!editorBufferPosToOffset(E.cy, E.cx, &offset)) {
		return 0;
	}
	return editorSyncCursorFromOffset(offset);
}

static int vimSystemWordForwardTarget(int *cy_out, int *cx_out) {
	int original_cy = E.cy;
	int original_cx = E.cx;
	int cy = E.cy;
	int cx = E.cx;

	while (cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		if (cx < line.size && vimSystemClassAt(&line, cx) != VIM_SYSTEM_CHAR_SPACE) {
			enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
			do {
				int next = editorBytesNextClusterIdx(line.data, line.size, cx);
				if (next <= cx) {
					break;
				}
				cx = next;
			} while (cx < line.size && vimSystemClassAt(&line, cx) == char_class);
		}
		while (cx < line.size && vimSystemClassAt(&line, cx) == VIM_SYSTEM_CHAR_SPACE) {
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		if (cx < line.size) {
			editorLineViewRelease(&line);
			*cy_out = cy;
			*cx_out = cx;
			return 1;
		}
		editorLineViewRelease(&line);
		cy++;
		cx = 0;
	}

	*cy_out = original_cy;
	*cx_out = original_cx;
	return 1;
}

static int vimSystemWordBackwardTarget(int *cy_out, int *cx_out) {
	int cy = E.cy;
	int cx = E.cx;

	while (cy >= 0) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		if (cx > 0) {
			cx = editorBytesPrevClusterIdx(line.data, line.size, cx);
			if (vimSystemClassAt(&line, cx) != VIM_SYSTEM_CHAR_SPACE) {
				enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
				while (cx > 0) {
					int prev =
					        editorBytesPrevClusterIdx(line.data, line.size, cx);
					if (prev >= cx ||
					    vimSystemClassAt(&line, prev) != char_class) {
						break;
					}
					cx = prev;
				}
				editorLineViewRelease(&line);
				*cy_out = cy;
				*cx_out = cx;
				return 1;
			}
			editorLineViewRelease(&line);
			continue;
		}
		editorLineViewRelease(&line);
		if (cy == 0) {
			break;
		}
		cy--;
		cx = (int)editorDocumentLineLength(E.document, cy);
	}

	*cy_out = 0;
	*cx_out = 0;
	return 1;
}

static int vimSystemWordEndTarget(int *cy_out, int *cx_out) {
	int original_cy = E.cy;
	int original_cx = E.cx;
	int cy = E.cy;
	int cx = E.cx;
	int find_next_run = 0;

	while (cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		if (!find_next_run && cx < line.size &&
		    vimSystemClassAt(&line, cx) != VIM_SYSTEM_CHAR_SPACE) {
			enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
			int end = cx;
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			while (next < line.size && vimSystemClassAt(&line, next) == char_class) {
				end = next;
				next = editorBytesNextClusterIdx(line.data, line.size, next);
			}
			if (end != cx) {
				editorLineViewRelease(&line);
				*cy_out = cy;
				*cx_out = end;
				return 1;
			}
			cx = next;
		}
		find_next_run = 1;
		while (cx < line.size && vimSystemClassAt(&line, cx) == VIM_SYSTEM_CHAR_SPACE) {
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		if (cx < line.size) {
			enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
			int end = cx;
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			while (next < line.size && vimSystemClassAt(&line, next) == char_class) {
				end = next;
				next = editorBytesNextClusterIdx(line.data, line.size, next);
			}
			editorLineViewRelease(&line);
			*cy_out = cy;
			*cx_out = end;
			return 1;
		}
		editorLineViewRelease(&line);
		cy++;
		cx = 0;
	}

	*cy_out = original_cy;
	*cx_out = original_cx;
	return 1;
}

/* Within-line `f`/`F`/`t`/`T` target. Matches the find char byte-wise (ASCII
 * targets); `t`/`T` stop one cluster shy of the match. Returns 0 if not found. */
static int vimSystemFindMotionTarget(enum vimSystemMotion motion, int cy, int cx, int *cx_out) {
	struct editorLineView line = {0};
	int target = E.input_vim_last_find_char;
	int forward = motion == VIM_SYSTEM_MOTION_FIND_FORWARD ||
	              motion == VIM_SYSTEM_MOTION_TILL_FORWARD;
	int till = motion == VIM_SYSTEM_MOTION_TILL_FORWARD ||
	           motion == VIM_SYSTEM_MOTION_TILL_BACKWARD;
	int found = -1;

	if (target == 0 || !editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	if (forward) {
		for (int i = editorBytesNextClusterIdx(line.data, line.size, cx); i < line.size;) {
			int n = editorBytesNextClusterIdx(line.data, line.size, i);
			if ((unsigned char)line.data[i] == (unsigned char)target) {
				found = i;
				break;
			}
			if (n <= i) {
				break;
			}
			i = n;
		}
	} else if (cx > 0) {
		for (int i = editorBytesPrevClusterIdx(line.data, line.size, cx);;) {
			if ((unsigned char)line.data[i] == (unsigned char)target) {
				found = i;
				break;
			}
			if (i == 0) {
				break;
			}
			int p = editorBytesPrevClusterIdx(line.data, line.size, i);
			if (p >= i) {
				break;
			}
			i = p;
		}
	}
	if (found >= 0 && till) {
		found = forward ? editorBytesPrevClusterIdx(line.data, line.size, found)
		                : editorBytesNextClusterIdx(line.data, line.size, found);
	}
	editorLineViewRelease(&line);
	if (found < 0) {
		return 0;
	}
	*cx_out = found;
	return 1;
}

/* `%`: from the first bracket at/after the cursor on the line, jump to its match.
 * Reuses the bracket-match highlight scanner (cursor-on-bracket based). */
static int vimSystemMatchBracketTarget(int *cy_out, int *cx_out) {
	struct editorLineView line = {0};
	int bcol = -1;
	int rows[2];
	int cols[2];
	int saved_cx;
	int ok;

	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 0;
	}
	for (int i = E.cx; i < line.size; i++) {
		char ch = line.data[i];
		if (ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}') {
			bcol = i;
			break;
		}
	}
	editorLineViewRelease(&line);
	if (bcol < 0) {
		return 0;
	}
	saved_cx = E.cx;
	E.cx = bcol;
	ok = editorBracketMatchComputeForCursor(rows, cols);
	E.cx = saved_cx;
	if (!ok) {
		return 0;
	}
	*cy_out = rows[1];
	*cx_out = cols[1];
	return 1;
}

/* `H`/`M`/`L`: top/middle/bottom visible buffer row of the focused pane. */
static int vimSystemScreenMotionTarget(enum vimSystemMotion motion) {
	int body = editorViewportFocusedPaneBodyRows();
	int top = E.rowoff < 0 ? 0 : E.rowoff;
	int last_visible;
	int target;

	if (body < 1) {
		body = 1;
	}
	last_visible = top + body - 1;
	if (last_visible > E.numrows - 1) {
		last_visible = E.numrows - 1;
	}
	if (motion == VIM_SYSTEM_MOTION_SCREEN_TOP) {
		target = top;
	} else if (motion == VIM_SYSTEM_MOTION_SCREEN_BOTTOM) {
		target = last_visible;
	} else {
		target = top + (last_visible - top) / 2;
	}
	if (target < 0) {
		target = 0;
	}
	if (target > E.numrows - 1) {
		target = E.numrows - 1;
	}
	return target;
}

static int vimSystemMotionTarget(enum vimSystemMotion motion, int *cy_out, int *cx_out) {
	int cy = E.cy;
	int cx = E.cx;

	if (!vimSystemSyncCursor() || E.numrows == 0) {
		return 0;
	}
	cy = E.cy;
	cx = E.cx;

	switch (motion) {
		case VIM_SYSTEM_MOTION_LEFT:
			if (cx > 0) {
				struct editorLineView line = {0};
				if (editorDocumentLineView(E.document, cy, &line)) {
					cx = editorBytesPrevClusterIdx(line.data, line.size, cx);
					editorLineViewRelease(&line);
				}
			}
			break;
		case VIM_SYSTEM_MOTION_RIGHT: {
			struct editorLineView line = {0};
			if (editorDocumentLineView(E.document, cy, &line)) {
				int next = editorBytesNextClusterIdx(line.data, line.size, cx);
				if (next < line.size) {
					cx = next;
				}
				editorLineViewRelease(&line);
			}
			break;
		}
		case VIM_SYSTEM_MOTION_UP:
		case VIM_SYSTEM_MOTION_DOWN: {
			int target_cy = cy + (motion == VIM_SYSTEM_MOTION_UP ? -1 : 1);
			if (target_cy >= 0 && target_cy < E.numrows) {
				struct editorLineView current = {0};
				struct editorLineView target = {0};
				int target_rx = 0;
				if (editorDocumentLineView(E.document, cy, &current)) {
					target_rx =
					        editorBytesCxToRx(current.data, current.size, cx);
					editorLineViewRelease(&current);
				}
				if (editorDocumentLineView(E.document, target_cy, &target)) {
					cx = editorBytesRxToCx(target.data, target.size, target_rx);
					if (cx == target.size && target.size > 0) {
						cx = editorBytesPrevClusterIdx(target.data,
						                               target.size, cx);
					}
					editorLineViewRelease(&target);
					cy = target_cy;
				}
			}
			break;
		}
		case VIM_SYSTEM_MOTION_WORD_FORWARD:
			return vimSystemWordForwardTarget(cy_out, cx_out);
		case VIM_SYSTEM_MOTION_WORD_BACKWARD:
			return vimSystemWordBackwardTarget(cy_out, cx_out);
		case VIM_SYSTEM_MOTION_WORD_END:
			return vimSystemWordEndTarget(cy_out, cx_out);
		case VIM_SYSTEM_MOTION_LINE_START:
			cx = 0;
			break;
		case VIM_SYSTEM_MOTION_LINE_END:
			cx = vimSystemLineLastCluster(cy);
			break;
		case VIM_SYSTEM_MOTION_FIRST_NONBLANK:
			cx = vimSystemLineFirstNonblank(cy);
			break;
		case VIM_SYSTEM_MOTION_FIRST_LINE:
			cy = 0;
			cx = vimSystemLineFirstNonblank(cy);
			break;
		case VIM_SYSTEM_MOTION_LAST_LINE:
			cy = E.numrows - 1;
			cx = vimSystemLineFirstNonblank(cy);
			break;
		case VIM_SYSTEM_MOTION_FIND_FORWARD:
		case VIM_SYSTEM_MOTION_FIND_BACKWARD:
		case VIM_SYSTEM_MOTION_TILL_FORWARD:
		case VIM_SYSTEM_MOTION_TILL_BACKWARD: {
			int target_cx = cx;
			if (vimSystemFindMotionTarget(motion, cy, cx, &target_cx)) {
				cx = target_cx;
			}
			break;
		}
		case VIM_SYSTEM_MOTION_PARAGRAPH_FORWARD: {
			int target = -1;
			for (int i = cy + 1; i < E.numrows; i++) {
				if (editorDocumentLineLength(E.document, i) == 0) {
					target = i;
					break;
				}
			}
			if (target >= 0) {
				cy = target;
				cx = 0;
			} else {
				cy = E.numrows - 1;
				cx = vimSystemLineEndCx(cy);
			}
			break;
		}
		case VIM_SYSTEM_MOTION_PARAGRAPH_BACKWARD: {
			int target = -1;
			for (int i = cy - 1; i >= 0; i--) {
				if (editorDocumentLineLength(E.document, i) == 0) {
					target = i;
					break;
				}
			}
			cy = target >= 0 ? target : 0;
			cx = 0;
			break;
		}
		case VIM_SYSTEM_MOTION_MATCH_BRACKET: {
			int tcy = cy;
			int tcx = cx;
			if (vimSystemMatchBracketTarget(&tcy, &tcx)) {
				cy = tcy;
				cx = tcx;
			}
			break;
		}
		case VIM_SYSTEM_MOTION_SCREEN_TOP:
		case VIM_SYSTEM_MOTION_SCREEN_MIDDLE:
		case VIM_SYSTEM_MOTION_SCREEN_BOTTOM:
			cy = vimSystemScreenMotionTarget(motion);
			cx = vimSystemLineFirstNonblank(cy);
			break;
	}

	*cy_out = cy;
	*cx_out = cx;
	return 1;
}

static int vimSystemMotionIsLinewise(enum vimSystemMotion motion) {
	return motion == VIM_SYSTEM_MOTION_DOWN || motion == VIM_SYSTEM_MOTION_UP ||
	       motion == VIM_SYSTEM_MOTION_FIRST_LINE || motion == VIM_SYSTEM_MOTION_LAST_LINE ||
	       motion == VIM_SYSTEM_MOTION_SCREEN_TOP ||
	       motion == VIM_SYSTEM_MOTION_SCREEN_MIDDLE ||
	       motion == VIM_SYSTEM_MOTION_SCREEN_BOTTOM;
}

static int vimSystemMotionIsInclusive(enum vimSystemMotion motion) {
	return motion == VIM_SYSTEM_MOTION_WORD_END || motion == VIM_SYSTEM_MOTION_LINE_END ||
	       motion == VIM_SYSTEM_MOTION_FIND_FORWARD ||
	       motion == VIM_SYSTEM_MOTION_TILL_FORWARD ||
	       motion == VIM_SYSTEM_MOTION_MATCH_BRACKET;
}

static int vimSystemPositionComesBefore(int left_cy, int left_cx, int right_cy, int right_cx) {
	if (left_cy != right_cy) {
		return left_cy < right_cy;
	}
	return left_cx < right_cx;
}

static int vimSystemPositionAfterCluster(int cy, int cx, int *cy_out, int *cx_out) {
	struct editorLineView line = {0};
	int next = cx;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	if (cx < 0 || cx >= line.size) {
		editorLineViewRelease(&line);
		return 0;
	}
	next = editorBytesNextClusterIdx(line.data, line.size, cx);
	if (next <= cx || next > line.size) {
		editorLineViewRelease(&line);
		return 0;
	}
	editorLineViewRelease(&line);
	*cy_out = cy;
	*cx_out = next;
	return 1;
}

static void vimSystemClampNormalCursor(void) {
	struct editorLineView line = {0};
	int cx = E.cx;

	if (!vimSystemSyncCursor() || E.cy < 0 || E.cy >= E.numrows ||
	    !editorDocumentLineView(E.document, E.cy, &line)) {
		return;
	}
	if (line.size > 0 && cx >= line.size) {
		cx = editorBytesPrevClusterIdx(line.data, line.size, line.size);
	}
	editorLineViewRelease(&line);
	(void)vimSystemSetCursor(E.cy, cx, NULL);
}

static int vimSystemMakeRange(int start_cy, int start_cx, int end_cy, int end_cx,
                              struct editorSelectionRange *range_out) {
	if (range_out == NULL) {
		return 0;
	}
	if (start_cy == end_cy && start_cx == end_cx) {
		return 0;
	}
	if (vimSystemPositionComesBefore(end_cy, end_cx, start_cy, start_cx)) {
		range_out->start_cy = end_cy;
		range_out->start_cx = end_cx;
		range_out->end_cy = start_cy;
		range_out->end_cx = start_cx;
		return 1;
	}
	range_out->start_cy = start_cy;
	range_out->start_cx = start_cx;
	range_out->end_cy = end_cy;
	range_out->end_cx = end_cx;
	return 1;
}

static int vimSystemLineRange(int start_cy, int end_cy, struct editorSelectionRange *range_out) {
	if (range_out == NULL || E.numrows <= 0) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (start_cy < 0) {
		start_cy = 0;
	}
	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}
	range_out->start_cy = start_cy;
	range_out->start_cx = 0;
	if (end_cy + 1 < E.numrows) {
		range_out->end_cy = end_cy + 1;
		range_out->end_cx = 0;
	} else {
		range_out->end_cy = end_cy;
		range_out->end_cx = vimSystemLineEndCx(end_cy);
	}
	return range_out->start_cy != range_out->end_cy || range_out->start_cx != range_out->end_cx;
}

static int vimSystemLineRangeLastRow(const struct editorSelectionRange *range) {
	if (range == NULL) {
		return 0;
	}
	if (range->end_cx == 0 && range->end_cy > range->start_cy) {
		return range->end_cy - 1;
	}
	return range->end_cy;
}

/* Resolve a motion applied `count` times. Motions read the live cursor, so this
 * walks E.cy/E.cx forward step by step and restores them before returning. */
static int vimSystemMotionTargetCounted(enum vimSystemMotion motion, int count, int *cy_out,
                                        int *cx_out) {
	int saved_cy = E.cy;
	int saved_cx = E.cx;
	int cy = E.cy;
	int cx = E.cx;
	int found = 0;

	if (motion == VIM_SYSTEM_MOTION_FIRST_LINE || motion == VIM_SYSTEM_MOTION_LAST_LINE) {
		int target_cy;
		if (E.numrows <= 0) {
			return 0;
		}
		if (E.input_vim_count > 0) {
			target_cy = E.input_vim_count - 1;
		} else {
			target_cy = motion == VIM_SYSTEM_MOTION_FIRST_LINE ? 0 : E.numrows - 1;
		}
		if (target_cy < 0) {
			target_cy = 0;
		}
		if (target_cy > E.numrows - 1) {
			target_cy = E.numrows - 1;
		}
		*cy_out = target_cy;
		*cx_out = vimSystemLineFirstNonblank(target_cy);
		return 1;
	}

	if (count < 1) {
		count = 1;
	}
	for (int i = 0; i < count; i++) {
		int target_cy = cy;
		int target_cx = cx;
		if (!vimSystemMotionTarget(motion, &target_cy, &target_cx)) {
			break;
		}
		found = 1;
		if (target_cy == cy && target_cx == cx) {
			break;
		}
		cy = target_cy;
		cx = target_cx;
		E.cy = cy;
		E.cx = cx;
		(void)vimSystemSyncCursor();
	}
	E.cy = saved_cy;
	E.cx = saved_cx;
	(void)vimSystemSyncCursor();
	*cy_out = cy;
	*cx_out = cx;
	return found;
}

static int vimSystemMotionRange(enum vimSystemMotion motion, int count,
                                struct editorSelectionRange *range_out, int *linewise_out) {
	int start_cy = E.cy;
	int start_cx = E.cx;
	int target_cy = E.cy;
	int target_cx = E.cx;

	if (linewise_out != NULL) {
		*linewise_out = 0;
	}
	if (!vimSystemMotionTargetCounted(motion, count, &target_cy, &target_cx)) {
		return 0;
	}
	if (vimSystemMotionIsLinewise(motion)) {
		if (linewise_out != NULL) {
			*linewise_out = 1;
		}
		return vimSystemLineRange(start_cy, target_cy, range_out);
	}
	if (vimSystemMotionIsInclusive(motion) &&
	    !vimSystemPositionComesBefore(target_cy, target_cx, start_cy, start_cx)) {
		if (!vimSystemPositionAfterCluster(target_cy, target_cx, &target_cy, &target_cx)) {
			return 0;
		}
	}
	return vimSystemMakeRange(start_cy, start_cx, target_cy, target_cx, range_out);
}

static int vimSystemMotionIsJump(enum vimSystemMotion motion) {
	switch (motion) {
		case VIM_SYSTEM_MOTION_FIRST_LINE:
		case VIM_SYSTEM_MOTION_LAST_LINE:
		case VIM_SYSTEM_MOTION_PARAGRAPH_FORWARD:
		case VIM_SYSTEM_MOTION_PARAGRAPH_BACKWARD:
		case VIM_SYSTEM_MOTION_MATCH_BRACKET:
		case VIM_SYSTEM_MOTION_SCREEN_TOP:
		case VIM_SYSTEM_MOTION_SCREEN_MIDDLE:
		case VIM_SYSTEM_MOTION_SCREEN_BOTTOM:
			return 1;
		default:
			return 0;
	}
}

static int vimSystemApplyMotion(enum vimSystemMotion motion, int count, int *effects_out) {
	int cy = E.cy;
	int cx = E.cx;

	if (!vimSystemMotionTargetCounted(motion, count, &cy, &cx)) {
		return 0;
	}
	if (vimSystemMotionIsJump(motion) && (cy != E.cy || cx != E.cx)) {
		editorJumplistRecord();
	}
	(void)vimSystemSetCursor(cy, cx, effects_out);
	return 1;
}

static int vimSystemFindKeyIsCommand(int c) {
	return c == 'f' || c == 'F' || c == 't' || c == 'T';
}

static enum vimSystemMotion vimSystemFindMotionForCmd(int cmd) {
	switch (cmd) {
		case 'F':
			return VIM_SYSTEM_MOTION_FIND_BACKWARD;
		case 't':
			return VIM_SYSTEM_MOTION_TILL_FORWARD;
		case 'T':
			return VIM_SYSTEM_MOTION_TILL_BACKWARD;
		default:
			return VIM_SYSTEM_MOTION_FIND_FORWARD;
	}
}

/* `;` repeats the last find as-is; `,` flips its direction (find/till kind kept). */
static enum vimSystemMotion vimSystemFindRepeatMotion(int cmd, int reverse) {
	int eff = cmd;

	if (reverse) {
		switch (cmd) {
			case 'f':
				eff = 'F';
				break;
			case 'F':
				eff = 'f';
				break;
			case 't':
				eff = 'T';
				break;
			case 'T':
				eff = 't';
				break;
			default:
				break;
		}
	}
	return vimSystemFindMotionForCmd(eff);
}

static enum vimSystemMotionParse vimSystemParseMotionKey(int c, int *pending_g,
                                                         enum vimSystemMotion *motion_out) {
	enum vimSystemMotion motion;

	if (pending_g != NULL && *pending_g) {
		*pending_g = 0;
		if (c == 'g') {
			*motion_out = VIM_SYSTEM_MOTION_FIRST_LINE;
			return VIM_SYSTEM_MOTION_PARSE_FOUND;
		}
		return VIM_SYSTEM_MOTION_PARSE_NONE;
	}

	switch (c) {
		case 'h':
		case ARROW_LEFT:
			motion = VIM_SYSTEM_MOTION_LEFT;
			break;
		case 'j':
		case ARROW_DOWN:
			motion = VIM_SYSTEM_MOTION_DOWN;
			break;
		case 'k':
		case ARROW_UP:
			motion = VIM_SYSTEM_MOTION_UP;
			break;
		case 'l':
		case ARROW_RIGHT:
			motion = VIM_SYSTEM_MOTION_RIGHT;
			break;
		case 'w':
			motion = VIM_SYSTEM_MOTION_WORD_FORWARD;
			break;
		case 'b':
			motion = VIM_SYSTEM_MOTION_WORD_BACKWARD;
			break;
		case 'e':
			motion = VIM_SYSTEM_MOTION_WORD_END;
			break;
		case '0':
			motion = VIM_SYSTEM_MOTION_LINE_START;
			break;
		case '$':
			motion = VIM_SYSTEM_MOTION_LINE_END;
			break;
		case '^':
			motion = VIM_SYSTEM_MOTION_FIRST_NONBLANK;
			break;
		case 'G':
			motion = VIM_SYSTEM_MOTION_LAST_LINE;
			break;
		case '}':
			motion = VIM_SYSTEM_MOTION_PARAGRAPH_FORWARD;
			break;
		case '{':
			motion = VIM_SYSTEM_MOTION_PARAGRAPH_BACKWARD;
			break;
		case '%':
			motion = VIM_SYSTEM_MOTION_MATCH_BRACKET;
			break;
		case 'H':
			motion = VIM_SYSTEM_MOTION_SCREEN_TOP;
			break;
		case 'M':
			motion = VIM_SYSTEM_MOTION_SCREEN_MIDDLE;
			break;
		case 'L':
			motion = VIM_SYSTEM_MOTION_SCREEN_BOTTOM;
			break;
		case 'g':
			if (pending_g != NULL) {
				*pending_g = 1;
			}
			return VIM_SYSTEM_MOTION_PARSE_PENDING;
		case ';':
		case ',':
			if (E.input_vim_last_find_cmd == 0) {
				return VIM_SYSTEM_MOTION_PARSE_NONE;
			}
			motion = vimSystemFindRepeatMotion(E.input_vim_last_find_cmd, c == ',');
			break;
		default:
			return VIM_SYSTEM_MOTION_PARSE_NONE;
	}
	*motion_out = motion;
	return VIM_SYSTEM_MOTION_PARSE_FOUND;
}

/* Returns 0 when `c` is not a motion key, 1 when it began a multi-key motion
 * (e.g. `g` awaiting `g`), 2 when a motion was applied. */
static int vimSystemTryMotionKey(int c, int count, int *effects_out) {
	enum vimSystemMotion motion;
	enum vimSystemMotionParse parsed =
	        vimSystemParseMotionKey(c, &E.input_vim_pending_g, &motion);

	if (parsed == VIM_SYSTEM_MOTION_PARSE_PENDING) {
		return 1;
	}
	if (parsed != VIM_SYSTEM_MOTION_PARSE_FOUND) {
		return 0;
	}
	(void)vimSystemApplyMotion(motion, count, effects_out);
	return 2;
}

static int vimSystemYankRange(const struct editorSelectionRange *range, int linewise, int report) {
	char *text = NULL;
	size_t len = 0;
	int extracted = editorExtractRangeText(range, &text, &len);

	if (extracted <= 0) {
		return extracted;
	}
	if (!vimSystemRegisterStore(text, len, linewise ? 1 : 0)) {
		free(text);
		return -1;
	}
	if (report) {
		editorSetStatusMsg("Copied %zu bytes", len);
	}
	free(text);
	return 1;
}

static int vimSystemYankLines(int start_cy, int end_cy, int report) {
	size_t total = 0;
	char *text = NULL;
	size_t pos = 0;

	if (E.numrows <= 0) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (start_cy < 0) {
		start_cy = 0;
	}
	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}
	for (int cy = start_cy; cy <= end_cy; cy++) {
		size_t line_len = editorDocumentLineLength(E.document, cy);
		if (total > ROTIDE_MAX_TEXT_BYTES || line_len > ROTIDE_MAX_TEXT_BYTES - total - 1) {
			editorSetOperationTooLargeStatus();
			return -1;
		}
		total += line_len + 1;
	}
	text = editorMalloc(total + 1);
	if (text == NULL) {
		editorSetAllocFailureStatus();
		return -1;
	}
	for (int cy = start_cy; cy <= end_cy; cy++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			free(text);
			return -1;
		}
		if (line.size > 0) {
			memcpy(text + pos, line.data, (size_t)line.size);
			pos += (size_t)line.size;
		}
		text[pos++] = '\n';
		editorLineViewRelease(&line);
	}
	text[pos] = '\0';
	if (!vimSystemRegisterStore(text, pos, 1)) {
		free(text);
		return -1;
	}
	if (report) {
		editorSetStatusMsg("Copied %zu bytes", pos);
	}
	free(text);
	return 1;
}

static int vimSystemChangeLineRange(int start_cy, int end_cy, int *effects_out) {
	struct editorSelectionRange range;
	int dirty_before = E.dirty;
	int changed = 0;

	if (vimSystemRejectReadOnlyMutation()) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (!vimSystemYankLines(start_cy, end_cy, 0)) {
		return 0;
	}
	range.start_cy = start_cy;
	range.start_cx = 0;
	range.end_cy = end_cy;
	range.end_cx = vimSystemLineEndCx(end_cy);
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	changed = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	editorClearSelectionState();
	if (changed > 0) {
		(void)vimSystemSetCursor(start_cy, 0, effects_out);
		vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
		vimSystemAddEditEffect(effects_out);
	}
	return changed > 0;
}

/* `>`/`<`: add or remove one indent level on each line in [start_cy, end_cy].
 * Indent unit follows the editor's tab/width settings; blank lines are skipped. */
static int vimSystemIndentLines(int start_cy, int end_cy, int dedent, int *effects_out) {
	int dirty_before = E.dirty;
	int changed = 0;
	int width = E.indent_width > 0 ? E.indent_width : ROTIDE_INDENT_WIDTH_DEFAULT;
	char indent[16];
	int indent_len;

	if (vimSystemRejectReadOnlyMutation() || E.numrows <= 0) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (start_cy < 0) {
		start_cy = 0;
	}
	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}
	if (width > (int)sizeof(indent)) {
		width = (int)sizeof(indent);
	}
	if (E.indent_use_tabs) {
		indent[0] = '\t';
		indent_len = 1;
	} else {
		for (int i = 0; i < width; i++) {
			indent[i] = ' ';
		}
		indent_len = width;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	for (int cy = start_cy; cy <= end_cy; cy++) {
		struct editorSelectionRange range;
		range.start_cy = cy;
		range.start_cx = 0;
		range.end_cy = cy;
		if (dedent) {
			struct editorLineView line = {0};
			int remove = 0;
			if (editorDocumentLineView(E.document, cy, &line)) {
				if (line.size > 0 && line.data[0] == '\t') {
					remove = 1;
				} else {
					while (remove < width && remove < line.size &&
					       line.data[remove] == ' ') {
						remove++;
					}
				}
				editorLineViewRelease(&line);
			}
			if (remove > 0) {
				range.end_cx = remove;
				if (editorDeleteRange(&range) > 0) {
					changed = 1;
				}
			}
		} else if (editorDocumentLineLength(E.document, cy) > 0) {
			range.end_cx = 0;
			if (editorReplaceRange(&range, indent, (size_t)indent_len) > 0) {
				changed = 1;
			}
		}
	}
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	editorClearSelectionState();
	if (changed) {
		vimSystemAddEditEffect(effects_out);
	}
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
	(void)vimSystemSetCursor(start_cy, vimSystemLineFirstNonblank(start_cy), effects_out);
	return changed;
}

/* Display width of a byte span: tabs expand to the tab stop, other bytes count
 * one column per UTF-8 lead byte (a reasonable proxy for non-CJK text). */
static int vimSystemDisplayWidth(const char *s, int len) {
	int w = 0;

	for (int i = 0; i < len; i++) {
		unsigned char b = (unsigned char)s[i];
		if (b == '\t') {
			w += ROTIDE_TAB_WIDTH;
		} else if ((b & 0xC0) != 0x80) {
			w++;
		}
	}
	return w;
}

/* `gq`: re-wrap lines [start_cy, end_cy] to `E.text_width`, packing whitespace-
 * separated words and preserving the first line's leading indent. One edit. */
static int vimSystemReflowLines(int start_cy, int end_cy, int *effects_out) {
	struct editorLineView line0 = {0};
	char *indent = NULL;
	int indent_len = 0;
	int indent_w = 0;
	size_t total_word_bytes = 0;
	size_t num_words = 0;
	char *out = NULL;
	size_t out_len = 0;
	size_t cap = 0;
	int cur_w = 0;
	int line_has_word = 0;
	struct editorSelectionRange range;
	int dirty_before = E.dirty;
	int width = E.text_width > 0 ? E.text_width : ROTIDE_TEXT_WIDTH_DEFAULT;
	int replaced = 0;

	if (vimSystemRejectReadOnlyMutation() || E.numrows <= 0) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (start_cy < 0) {
		start_cy = 0;
	}
	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}

	if (editorDocumentLineView(E.document, start_cy, &line0)) {
		while (indent_len < line0.size &&
		       (line0.data[indent_len] == ' ' || line0.data[indent_len] == '\t')) {
			indent_len++;
		}
		indent = editorMalloc((size_t)indent_len + 1);
		if (indent == NULL) {
			editorLineViewRelease(&line0);
			editorSetAllocFailureStatus();
			return 0;
		}
		memcpy(indent, line0.data, (size_t)indent_len);
		indent[indent_len] = '\0';
		indent_w = vimSystemDisplayWidth(indent, indent_len);
		editorLineViewRelease(&line0);
	}

	for (int cy = start_cy; cy <= end_cy; cy++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			continue;
		}
		for (int i = 0; i < line.size;) {
			int start;
			while (i < line.size && (line.data[i] == ' ' || line.data[i] == '\t')) {
				i++;
			}
			start = i;
			while (i < line.size && line.data[i] != ' ' && line.data[i] != '\t') {
				i++;
			}
			if (i > start) {
				num_words++;
				total_word_bytes += (size_t)(i - start);
			}
		}
		editorLineViewRelease(&line);
	}
	if (num_words == 0) {
		free(indent);
		return 0;
	}

	cap = total_word_bytes + num_words * (size_t)(indent_len + 1) + (size_t)indent_len + 2;
	if (cap > ROTIDE_MAX_TEXT_BYTES) {
		free(indent);
		editorSetOperationTooLargeStatus();
		return 0;
	}
	out = editorMalloc(cap);
	if (out == NULL) {
		free(indent);
		editorSetAllocFailureStatus();
		return 0;
	}

	memcpy(out, indent, (size_t)indent_len);
	out_len = (size_t)indent_len;
	cur_w = indent_w;
	for (int cy = start_cy; cy <= end_cy; cy++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			continue;
		}
		for (int i = 0; i < line.size;) {
			int start;
			int wl;
			int ww;
			while (i < line.size && (line.data[i] == ' ' || line.data[i] == '\t')) {
				i++;
			}
			start = i;
			while (i < line.size && line.data[i] != ' ' && line.data[i] != '\t') {
				i++;
			}
			wl = i - start;
			if (wl == 0) {
				continue;
			}
			ww = vimSystemDisplayWidth(line.data + start, wl);
			if (!line_has_word) {
				memcpy(out + out_len, line.data + start, (size_t)wl);
				out_len += (size_t)wl;
				cur_w += ww;
				line_has_word = 1;
			} else if (cur_w + 1 + ww <= width) {
				out[out_len++] = ' ';
				memcpy(out + out_len, line.data + start, (size_t)wl);
				out_len += (size_t)wl;
				cur_w += 1 + ww;
			} else {
				out[out_len++] = '\n';
				memcpy(out + out_len, indent, (size_t)indent_len);
				out_len += (size_t)indent_len;
				memcpy(out + out_len, line.data + start, (size_t)wl);
				out_len += (size_t)wl;
				cur_w = indent_w + ww;
			}
		}
		editorLineViewRelease(&line);
	}

	range.start_cy = start_cy;
	range.start_cx = 0;
	range.end_cy = end_cy;
	range.end_cx = vimSystemLineEndCx(end_cy);
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	replaced = editorReplaceRange(&range, out, out_len);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	free(indent);
	free(out);
	editorClearSelectionState();
	if (replaced > 0) {
		vimSystemAddEditEffect(effects_out);
	}
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
	vimSystemClampNormalCursor();
	return replaced > 0;
}

static int vimSystemApplyOperatorToRange(enum vimSystemOperator op,
                                         const struct editorSelectionRange *range, int linewise,
                                         int *effects_out) {
	int dirty_before = E.dirty;
	int changed = 0;

	if (op == VIM_SYSTEM_OPERATOR_INDENT || op == VIM_SYSTEM_OPERATOR_DEDENT) {
		int last = linewise ? vimSystemLineRangeLastRow(range) : range->end_cy;
		return vimSystemIndentLines(range->start_cy, last, op == VIM_SYSTEM_OPERATOR_DEDENT,
		                            effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_REFLOW) {
		int last = linewise ? vimSystemLineRangeLastRow(range) : range->end_cy;
		return vimSystemReflowLines(range->start_cy, last, effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_YANK) {
		if (linewise) {
			return vimSystemYankLines(range->start_cy,
			                          vimSystemLineRangeLastRow(range), 1) > 0;
		}
		return vimSystemYankRange(range, 0, 1) > 0;
	}
	if (vimSystemRejectReadOnlyMutation()) {
		return 0;
	}
	if (linewise &&
	    !vimSystemYankLines(range->start_cy, vimSystemLineRangeLastRow(range), 0)) {
		return 0;
	}
	if (!linewise && !vimSystemYankRange(range, 0, 0)) {
		return 0;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	changed = editorDeleteRange(range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	editorClearSelectionState();
	if (changed > 0) {
		vimSystemAddEditEffect(effects_out);
		if (op == VIM_SYSTEM_OPERATOR_CHANGE) {
			vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
		} else {
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
			vimSystemClampNormalCursor();
		}
	}
	return changed > 0;
}

static int vimSystemApplyOperatorMotion(enum vimSystemOperator op, enum vimSystemMotion motion,
                                        int count, int *effects_out) {
	struct editorSelectionRange range;
	int linewise = 0;

	if (!vimSystemMotionRange(motion, count, &range, &linewise)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(op, &range, linewise, effects_out);
}

static int vimSystemApplyLineOperator(enum vimSystemOperator op, int count, int *effects_out) {
	struct editorSelectionRange range;
	int end_cy = E.cy + (count > 1 ? count - 1 : 0);

	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}
	if (op == VIM_SYSTEM_OPERATOR_INDENT || op == VIM_SYSTEM_OPERATOR_DEDENT) {
		return vimSystemIndentLines(E.cy, end_cy, op == VIM_SYSTEM_OPERATOR_DEDENT,
		                            effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_REFLOW) {
		return vimSystemReflowLines(E.cy, end_cy, effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_CHANGE) {
		return vimSystemChangeLineRange(E.cy, end_cy, effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_YANK) {
		return vimSystemYankLines(E.cy, end_cy, 1) > 0;
	}
	if (!vimSystemLineRange(E.cy, end_cy, &range)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(op, &range, 1, effects_out);
}

static int vimSystemLineIsBlank(int cy) {
	return editorDocumentLineLength(E.document, cy) == 0;
}

/* `iw`/`aw` range on the current line; end column is exclusive. */
static int vimSystemWordObjectRange(int inner, struct editorSelectionRange *range_out) {
	struct editorLineView line = {0};
	int cy = E.cy;
	int cx = E.cx;
	enum vimSystemCharClass char_class;
	int start = 0;
	int end_excl = 0;
	int next = 0;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	if (line.size == 0) {
		editorLineViewRelease(&line);
		return 0;
	}
	if (cx >= line.size) {
		cx = editorBytesPrevClusterIdx(line.data, line.size, line.size);
	}
	char_class = vimSystemClassAt(&line, cx);
	start = cx;
	while (start > 0) {
		int prev = editorBytesPrevClusterIdx(line.data, line.size, start);
		if (prev >= start || vimSystemClassAt(&line, prev) != char_class) {
			break;
		}
		start = prev;
	}
	end_excl = cx;
	next = editorBytesNextClusterIdx(line.data, line.size, end_excl);
	while (next < line.size && vimSystemClassAt(&line, next) == char_class) {
		end_excl = next;
		next = editorBytesNextClusterIdx(line.data, line.size, next);
	}
	end_excl = next;
	if (!inner && char_class != VIM_SYSTEM_CHAR_SPACE) {
		int extended = 0;
		int edge = end_excl;
		while (edge < line.size && vimSystemClassAt(&line, edge) == VIM_SYSTEM_CHAR_SPACE) {
			int step = editorBytesNextClusterIdx(line.data, line.size, edge);
			if (step <= edge) {
				break;
			}
			edge = step;
			extended = 1;
		}
		if (extended) {
			end_excl = edge;
		} else {
			while (start > 0) {
				int prev = editorBytesPrevClusterIdx(line.data, line.size, start);
				if (prev >= start ||
				    vimSystemClassAt(&line, prev) != VIM_SYSTEM_CHAR_SPACE) {
					break;
				}
				start = prev;
			}
		}
	}
	editorLineViewRelease(&line);
	range_out->start_cy = cy;
	range_out->start_cx = start;
	range_out->end_cy = cy;
	range_out->end_cx = end_excl;
	return start != end_excl;
}

static int vimSystemParagraphObjectRange(int inner, struct editorSelectionRange *range_out) {
	int blank = 0;
	int start = E.cy;
	int end = E.cy;

	if (E.numrows <= 0) {
		return 0;
	}
	blank = vimSystemLineIsBlank(E.cy);
	while (start > 0 && vimSystemLineIsBlank(start - 1) == blank) {
		start--;
	}
	while (end + 1 < E.numrows && vimSystemLineIsBlank(end + 1) == blank) {
		end++;
	}
	if (!inner && !blank) {
		while (end + 1 < E.numrows && vimSystemLineIsBlank(end + 1)) {
			end++;
		}
	}
	return vimSystemLineRange(start, end, range_out);
}

/* Scan backward from just before (from_cy, from_bx) for the nearest unmatched
 * `open`, honoring nested pairs. Brackets are ASCII, so a byte-wise scan is safe
 * even across multibyte runs (continuation bytes never equal a bracket). */
static int vimSystemScanOpenBefore(char open, char close, int from_cy, int from_bx, int *ocy,
                                   int *obx) {
	int depth = 0;

	for (int cy = from_cy; cy >= 0; cy--) {
		struct editorLineView line = {0};
		int start = 0;
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		start = (cy == from_cy) ? from_bx - 1 : line.size - 1;
		if (start >= line.size) {
			start = line.size - 1;
		}
		for (int i = start; i >= 0; i--) {
			unsigned char ch = (unsigned char)line.data[i];
			if (ch == (unsigned char)close) {
				depth++;
			} else if (ch == (unsigned char)open) {
				if (depth == 0) {
					*ocy = cy;
					*obx = i;
					editorLineViewRelease(&line);
					return 1;
				}
				depth--;
			}
		}
		editorLineViewRelease(&line);
	}
	return 0;
}

/* Scan forward from just after (from_cy, from_bx) for the matching `close`. */
static int vimSystemScanCloseAfter(char open, char close, int from_cy, int from_bx, int *ccy,
                                   int *cbx) {
	int depth = 0;

	for (int cy = from_cy; cy < E.numrows; cy++) {
		struct editorLineView line = {0};
		int start = 0;
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		start = (cy == from_cy) ? from_bx + 1 : 0;
		for (int i = start; i < line.size; i++) {
			unsigned char ch = (unsigned char)line.data[i];
			if (ch == (unsigned char)open) {
				depth++;
			} else if (ch == (unsigned char)close) {
				if (depth == 0) {
					*ccy = cy;
					*cbx = i;
					editorLineViewRelease(&line);
					return 1;
				}
				depth--;
			}
		}
		editorLineViewRelease(&line);
	}
	return 0;
}

static unsigned char vimSystemByteAtCursor(void) {
	struct editorLineView line = {0};
	unsigned char ch = 0;

	if (editorDocumentLineView(E.document, E.cy, &line)) {
		if (E.cx >= 0 && E.cx < line.size) {
			ch = (unsigned char)line.data[E.cx];
		}
		editorLineViewRelease(&line);
	}
	return ch;
}

/* `i(`/`a(` and friends: the pair enclosing the cursor (or the pair the cursor
 * sits on). Inner excludes the delimiters; `around` includes them. End column is
 * exclusive, matching the word object. */
static int vimSystemBracketObjectRange(int inner, char open, char close,
                                       struct editorSelectionRange *range_out) {
	unsigned char cur = vimSystemByteAtCursor();
	int ocy = 0;
	int obx = 0;
	int ccy = 0;
	int cbx = 0;

	if (cur == (unsigned char)open) {
		ocy = E.cy;
		obx = E.cx;
		if (!vimSystemScanCloseAfter(open, close, ocy, obx, &ccy, &cbx)) {
			return 0;
		}
	} else if (cur == (unsigned char)close) {
		ccy = E.cy;
		cbx = E.cx;
		if (!vimSystemScanOpenBefore(open, close, ccy, cbx, &ocy, &obx)) {
			return 0;
		}
	} else {
		if (!vimSystemScanOpenBefore(open, close, E.cy, E.cx, &ocy, &obx) ||
		    !vimSystemScanCloseAfter(open, close, ocy, obx, &ccy, &cbx)) {
			return 0;
		}
	}

	if (inner) {
		range_out->start_cy = ocy;
		range_out->start_cx = obx + 1;
		range_out->end_cy = ccy;
		range_out->end_cx = cbx;
	} else {
		range_out->start_cy = ocy;
		range_out->start_cx = obx;
		range_out->end_cy = ccy;
		range_out->end_cx = cbx + 1;
	}
	return !(range_out->start_cy == range_out->end_cy &&
	         range_out->start_cx == range_out->end_cx);
}

/* `i"`/`a"` and friends: the quoted span on the current line containing or after
 * the cursor. Pairs are matched left-to-right; escapes are not handled. */
static int vimSystemQuoteObjectRange(int inner, char quote,
                                     struct editorSelectionRange *range_out) {
	struct editorLineView line = {0};
	int open_idx = -1;
	int close_idx = -1;
	int prev = -1;

	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 0;
	}
	for (int i = 0; i < line.size; i++) {
		if ((unsigned char)line.data[i] != (unsigned char)quote) {
			continue;
		}
		if (prev < 0) {
			prev = i;
		} else {
			if (E.cx <= i) {
				open_idx = prev;
				close_idx = i;
				break;
			}
			prev = -1;
		}
	}
	editorLineViewRelease(&line);
	if (open_idx < 0 || close_idx < 0) {
		return 0;
	}
	range_out->start_cy = E.cy;
	range_out->end_cy = E.cy;
	if (inner) {
		range_out->start_cx = open_idx + 1;
		range_out->end_cx = close_idx;
	} else {
		range_out->start_cx = open_idx;
		range_out->end_cx = close_idx + 1;
	}
	return range_out->start_cx != range_out->end_cx;
}

enum { VIM_TAG_NAME_MAX = 32, VIM_TAG_TOKEN_MAX = 512 };

struct vimTagToken {
	char name[VIM_TAG_NAME_MAX];
	int closing;
	int self_close;
	int start_cy;
	int start_cx;
	int after_cy;
	int after_cx;
};

/* Collect single-line `<...>` tags across the document (tags that don't close on
 * their start line are skipped). Returns the token count. */
static int vimSystemCollectTags(struct vimTagToken *toks, int max) {
	int n = 0;

	for (int cy = 0; cy < E.numrows && n < max; cy++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			break;
		}
		for (int i = 0; i < line.size && n < max; i++) {
			int j;
			int closing = 0;
			int nl = 0;
			int gt = -1;
			struct vimTagToken *tok;
			if (line.data[i] != '<') {
				continue;
			}
			j = i + 1;
			if (j < line.size && line.data[j] == '/') {
				closing = 1;
				j++;
			}
			tok = &toks[n];
			while (j < line.size && nl < VIM_TAG_NAME_MAX - 1) {
				char ch = line.data[j];
				if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' ||
				    ch == ':') {
					tok->name[nl++] = ch;
					j++;
				} else {
					break;
				}
			}
			tok->name[nl] = '\0';
			if (nl == 0) {
				continue;
			}
			for (int k = j; k < line.size; k++) {
				if (line.data[k] == '>') {
					gt = k;
					break;
				}
			}
			if (gt < 0) {
				continue;
			}
			tok->closing = closing;
			tok->self_close = gt > 0 && line.data[gt - 1] == '/';
			tok->start_cy = cy;
			tok->start_cx = i;
			tok->after_cy = cy;
			tok->after_cx = gt + 1;
			n++;
			i = gt;
		}
		editorLineViewRelease(&line);
	}
	return n;
}

/* `it`/`at`: the innermost `<tag>...</tag>` pair enclosing the cursor. */
static int vimSystemTagObjectRange(int inner, struct editorSelectionRange *range_out) {
	struct vimTagToken toks[VIM_TAG_TOKEN_MAX];
	int stack[VIM_TAG_TOKEN_MAX];
	int sp = 0;
	int count = vimSystemCollectTags(toks, VIM_TAG_TOKEN_MAX);
	size_t cursor_off = 0;
	int best_open = -1;
	int best_close = -1;
	size_t best_open_off = 0;

	if (count <= 0 || !editorBufferPosToOffset(E.cy, E.cx, &cursor_off)) {
		return 0;
	}
	for (int t = 0; t < count; t++) {
		size_t open_start = 0;
		size_t close_after = 0;
		int open_t;
		if (toks[t].self_close) {
			continue;
		}
		if (!toks[t].closing) {
			stack[sp++] = t;
			continue;
		}
		while (sp > 0 && strcmp(toks[stack[sp - 1]].name, toks[t].name) != 0) {
			sp--;
		}
		if (sp == 0) {
			continue;
		}
		open_t = stack[--sp];
		if (!editorBufferPosToOffset(toks[open_t].start_cy, toks[open_t].start_cx,
		                             &open_start) ||
		    !editorBufferPosToOffset(toks[t].after_cy, toks[t].after_cx, &close_after)) {
			continue;
		}
		if (cursor_off >= open_start && cursor_off < close_after &&
		    (best_open < 0 || open_start > best_open_off)) {
			best_open = open_t;
			best_close = t;
			best_open_off = open_start;
		}
	}
	if (best_open < 0) {
		return 0;
	}
	if (inner) {
		range_out->start_cy = toks[best_open].after_cy;
		range_out->start_cx = toks[best_open].after_cx;
		range_out->end_cy = toks[best_close].start_cy;
		range_out->end_cx = toks[best_close].start_cx;
	} else {
		range_out->start_cy = toks[best_open].start_cy;
		range_out->start_cx = toks[best_open].start_cx;
		range_out->end_cy = toks[best_close].after_cy;
		range_out->end_cx = toks[best_close].after_cx;
	}
	return !(range_out->start_cy == range_out->end_cy &&
	         range_out->start_cx == range_out->end_cx);
}

static int vimSystemTextObjectRange(int inner, int object_key,
                                    struct editorSelectionRange *range_out, int *linewise_out) {
	if (linewise_out != NULL) {
		*linewise_out = 0;
	}
	switch (object_key) {
		case 'w':
			return vimSystemWordObjectRange(inner, range_out);
		case 'p':
			if (linewise_out != NULL) {
				*linewise_out = 1;
			}
			return vimSystemParagraphObjectRange(inner, range_out);
		case '(':
		case ')':
		case 'b':
			return vimSystemBracketObjectRange(inner, '(', ')', range_out);
		case '{':
		case '}':
		case 'B':
			return vimSystemBracketObjectRange(inner, '{', '}', range_out);
		case '[':
		case ']':
			return vimSystemBracketObjectRange(inner, '[', ']', range_out);
		case '<':
		case '>':
			return vimSystemBracketObjectRange(inner, '<', '>', range_out);
		case '"':
			return vimSystemQuoteObjectRange(inner, '"', range_out);
		case '\'':
			return vimSystemQuoteObjectRange(inner, '\'', range_out);
		case '`':
			return vimSystemQuoteObjectRange(inner, '`', range_out);
		case 't':
			return vimSystemTagObjectRange(inner, range_out);
		default:
			return 0;
	}
}

static int vimSystemHandlePendingOperatorKey(int c, int *effects_out) {
	enum vimSystemOperator op = (enum vimSystemOperator)E.input_vim_pending_operator;
	enum vimSystemMotion motion;
	enum vimSystemMotionParse parsed;

	if (c == '\x1b') {
		vimSystemResetPending();
		return 0;
	}

	if (E.input_vim_pending_text_object) {
		int inner = E.input_vim_pending_text_object == 'i';
		struct editorSelectionRange range;
		int linewise = 0;
		if (vimSystemTextObjectRange(inner, c, &range, &linewise)) {
			(void)vimSystemApplyOperatorToRange(op, &range, linewise, effects_out);
		}
		vimSystemResetPending();
		return 0;
	}
	if (vimSystemConsumeCountKey(c)) {
		return 0;
	}
	if (c == 'i' || c == 'a') {
		E.input_vim_pending_text_object = c;
		return 0;
	}
	if (c == E.input_vim_pending_operator) {
		int count = vimSystemOperatorTotalCount();
		(void)vimSystemApplyLineOperator(op, count, effects_out);
		vimSystemResetPending();
		return 0;
	}
	if (vimSystemFindKeyIsCommand(c)) {
		E.input_vim_pending_find = c;
		return 0;
	}

	parsed = vimSystemParseMotionKey(c, &E.input_vim_pending_operator_g, &motion);
	if (parsed == VIM_SYSTEM_MOTION_PARSE_PENDING) {
		return 0;
	}
	if (parsed == VIM_SYSTEM_MOTION_PARSE_FOUND) {
		int count = vimSystemOperatorTotalCount();
		(void)vimSystemApplyOperatorMotion(op, motion, count, effects_out);
	}
	vimSystemResetPending();
	return 0;
}

static int vimSystemPendingOperatorKeyIsStructural(int c) {
	return c == '\x1b' || E.input_vim_pending_text_object || c == 'i' || c == 'a' ||
	       (isdigit(c) && !(c == '0' && E.input_vim_count == 0));
}

static int vimSystemDeleteUnderCursor(int count, int *effects_out) {
	struct editorSelectionRange range;
	int end_cy = E.cy;
	int end_cx = E.cx;
	int moved = 0;

	if (E.numrows <= 0) {
		return 0;
	}
	if (count < 1) {
		count = 1;
	}
	for (int i = 0; i < count; i++) {
		int next_cy = end_cy;
		int next_cx = end_cx;
		if (!vimSystemPositionAfterCluster(end_cy, end_cx, &next_cy, &next_cx)) {
			break;
		}
		end_cy = next_cy;
		end_cx = next_cx;
		moved = 1;
	}
	if (!moved || !vimSystemMakeRange(E.cy, E.cx, end_cy, end_cx, &range)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(VIM_SYSTEM_OPERATOR_DELETE, &range, 0, effects_out);
}

static int vimSystemDeleteToLineEnd(enum vimSystemOperator op, int *effects_out) {
	struct editorSelectionRange range;
	int end_cx = vimSystemLineEndCx(E.cy);

	if (!vimSystemMakeRange(E.cy, E.cx, E.cy, end_cx, &range)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(op, &range, 0, effects_out);
}

static int vimSystemPasteDefaultRegister(int after, int *effects_out) {
	size_t clip_len = 0;
	int reg_linewise = 0;
	const char *clip = vimSystemRegisterFetch(&clip_len, &reg_linewise);
	const char *paste_text = clip;
	size_t paste_len = clip_len;
	char *owned_text = NULL;
	int dirty_before = E.dirty;
	int pasted = 0;

	if (vimSystemRejectReadOnlyMutation()) {
		return 0;
	}
	if (clip == NULL || clip_len == 0) {
		editorSetStatusMsg("Clipboard is empty");
		return 0;
	}

	if (reg_linewise) {
		int insert_cy = after ? E.cy + 1 : E.cy;
		if (insert_cy < E.numrows) {
			(void)vimSystemSetCursor(insert_cy, 0, NULL);
		} else if (E.numrows > 0) {
			size_t body_len = clip_len;
			if (body_len > 0 && clip[body_len - 1] == '\n') {
				body_len--;
			}
			owned_text = editorMalloc(body_len + 2);
			if (owned_text == NULL) {
				editorSetAllocFailureStatus();
				return 0;
			}
			owned_text[0] = '\n';
			if (body_len > 0) {
				memcpy(owned_text + 1, clip, body_len);
			}
			owned_text[body_len + 1] = '\0';
			paste_text = owned_text;
			paste_len = body_len + 1;
			(void)vimSystemSetCursor(E.numrows - 1, vimSystemLineEndCx(E.numrows - 1),
			                         NULL);
		}
	} else if (after) {
		int end_cy = E.cy;
		int end_cx = E.cx;
		if (vimSystemPositionAfterCluster(E.cy, E.cx, &end_cy, &end_cx)) {
			(void)vimSystemSetCursor(end_cy, end_cx, NULL);
		}
	}

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	pasted = editorInsertText(paste_text, paste_len);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	free(owned_text);
	if (pasted) {
		vimSystemAddEditEffect(effects_out);
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		vimSystemClampNormalCursor();
	}
	return pasted;
}

/* `r<char>`: overwrite `count` clusters under the cursor with `replacement`.
 * No-op (like Vim's beep) if fewer than `count` clusters remain on the line. */
static int vimSystemReplaceChar(int replacement, int count, int *effects_out) {
	struct editorLineView line = {0};
	struct editorSelectionRange range;
	char stack_buf[64];
	char *buf = stack_buf;
	int end_cx;
	int dirty_before = E.dirty;
	int last_cx;
	int replaced;
	size_t replace_len;

	if (replacement == '\x1b' || replacement == '\r') {
		return 0;
	}
	if (vimSystemRejectReadOnlyMutation() || count < 1) {
		return 0;
	}
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 0;
	}
	end_cx = E.cx;
	for (int i = 0; i < count; i++) {
		int next = editorBytesNextClusterIdx(line.data, line.size, end_cx);
		if (next <= end_cx || end_cx >= line.size) {
			editorLineViewRelease(&line);
			return 0;
		}
		end_cx = next;
	}
	last_cx = editorBytesPrevClusterIdx(line.data, line.size, end_cx);
	editorLineViewRelease(&line);

	replace_len = (size_t)count;
	if (replace_len > ROTIDE_MAX_TEXT_BYTES) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (replace_len > sizeof(stack_buf)) {
		buf = editorMalloc(replace_len);
		if (buf == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
	}
	memset(buf, replacement, replace_len);
	range.start_cy = E.cy;
	range.start_cx = E.cx;
	range.end_cy = E.cy;
	range.end_cx = end_cx;
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	replaced = editorReplaceRange(&range, buf, replace_len);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	if (buf != stack_buf) {
		free(buf);
	}
	if (replaced <= 0) {
		return 0;
	}
	vimSystemAddEditEffect(effects_out);
	(void)vimSystemSetCursor(E.cy, last_cx, effects_out);
	return 1;
}

/* `~`: toggle case of `count` clusters and advance the cursor past them. */
static int vimSystemToggleCase(int count, int *effects_out) {
	struct editorLineView line = {0};
	char buf[64];
	int n = 0;
	int start_cx = E.cx;
	int cx;
	int dirty_before = E.dirty;
	struct editorSelectionRange range;

	if (vimSystemRejectReadOnlyMutation() || count < 1) {
		return 0;
	}
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 0;
	}
	cx = E.cx;
	for (int i = 0; i < count && cx < line.size && n < (int)sizeof(buf); i++) {
		int next = editorBytesNextClusterIdx(line.data, line.size, cx);
		if (next <= cx) {
			break;
		}
		for (int b = cx; b < next && n < (int)sizeof(buf); b++) {
			unsigned char ch = (unsigned char)line.data[b];
			if (ch >= 'a' && ch <= 'z') {
				ch = (unsigned char)(ch - 'a' + 'A');
			} else if (ch >= 'A' && ch <= 'Z') {
				ch = (unsigned char)(ch - 'A' + 'a');
			}
			buf[n++] = (char)ch;
		}
		cx = next;
	}
	editorLineViewRelease(&line);
	if (n == 0 || cx == start_cx) {
		return 0;
	}
	range.start_cy = E.cy;
	range.start_cx = start_cx;
	range.end_cy = E.cy;
	range.end_cx = cx;
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	(void)editorReplaceRange(&range, buf, (size_t)n);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	vimSystemAddEditEffect(effects_out);
	(void)vimSystemSetCursor(E.cy, cx < vimSystemLineEndCx(E.cy) ? cx : start_cx, effects_out);
	return 1;
}

/* `J`: join the current line with the following one(s), collapsing the next
 * line's leading whitespace to a single space (none if it would be redundant). */
static int vimSystemJoinLines(int count, int *effects_out) {
	int joins = count > 1 ? count - 1 : 1;
	int dirty_before = E.dirty;
	int any = 0;

	if (vimSystemRejectReadOnlyMutation()) {
		return 0;
	}
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	for (int k = 0; k < joins; k++) {
		struct editorLineView cur = {0};
		struct editorLineView nxt = {0};
		struct editorSelectionRange range;
		int cur_len;
		int skip = 0;
		int sep_space;

		if (E.cy + 1 >= E.numrows) {
			break;
		}
		if (!editorDocumentLineView(E.document, E.cy, &cur)) {
			break;
		}
		cur_len = cur.size;
		int cur_blank = cur.size == 0;
		int cur_ends_space = cur.size > 0 && (cur.data[cur.size - 1] == ' ' ||
		                                      cur.data[cur.size - 1] == '\t');
		editorLineViewRelease(&cur);
		if (editorDocumentLineView(E.document, E.cy + 1, &nxt)) {
			while (skip < nxt.size &&
			       (nxt.data[skip] == ' ' || nxt.data[skip] == '\t')) {
				skip++;
			}
			editorLineViewRelease(&nxt);
		}
		sep_space = !cur_blank && !cur_ends_space &&
		            editorDocumentLineLength(E.document, E.cy + 1) > (size_t)skip;
		range.start_cy = E.cy;
		range.start_cx = cur_len;
		range.end_cy = E.cy + 1;
		range.end_cx = skip;
		(void)editorReplaceRange(&range, sep_space ? " " : "", sep_space ? 1 : 0);
		(void)vimSystemSetCursor(E.cy, cur_len, effects_out);
		any = 1;
	}
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	if (any) {
		vimSystemAddEditEffect(effects_out);
	}
	return any;
}

static int vimSystemActionForKey(enum vimSystemMode mode, int c, enum editorAction *action_out) {
	enum editorAction action;

	switch (c) {
		case ALT_ARROW_LEFT:
			action = EDITOR_ACTION_PREV_TAB;
			break;
		case ALT_ARROW_RIGHT:
			action = EDITOR_ACTION_NEXT_TAB;
			break;
		case ALT_ARROW_UP:
			action = EDITOR_ACTION_MOVE_LINE_UP;
			break;
		case ALT_ARROW_DOWN:
			action = EDITOR_ACTION_MOVE_LINE_DOWN;
			break;
		case EDITOR_ALT_LETTER_KEY('c'):
			action = EDITOR_ACTION_TOGGLE_COMMENT;
			break;
		case EDITOR_ALT_LETTER_KEY('z'):
			action = EDITOR_ACTION_TOGGLE_LINE_WRAP;
			break;
		case EDITOR_ALT_LETTER_KEY('n'):
			action = EDITOR_ACTION_TOGGLE_LINE_NUMBERS;
			break;
		case EDITOR_ALT_LETTER_KEY('h'):
			action = EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT;
			break;
		default:
			if (mode != VIM_SYSTEM_MODE_INSERT) {
				return 0;
			}
			switch (c) {
				case ARROW_LEFT:
					action = EDITOR_ACTION_MOVE_LEFT;
					break;
				case ARROW_RIGHT:
					action = EDITOR_ACTION_MOVE_RIGHT;
					break;
				case ARROW_UP:
					action = EDITOR_ACTION_MOVE_UP;
					break;
				case ARROW_DOWN:
					action = EDITOR_ACTION_MOVE_DOWN;
					break;
				case HOME_KEY:
					action = EDITOR_ACTION_MOVE_HOME;
					break;
				case END_KEY:
					action = EDITOR_ACTION_MOVE_END;
					break;
				case PAGE_UP:
					action = EDITOR_ACTION_PAGE_UP;
					break;
				case PAGE_DOWN:
					action = EDITOR_ACTION_PAGE_DOWN;
					break;
				case CTRL_ARROW_LEFT:
					action = EDITOR_ACTION_MOVE_WORD_LEFT;
					break;
				case CTRL_ARROW_RIGHT:
					action = EDITOR_ACTION_MOVE_WORD_RIGHT;
					break;
				case DEL_KEY:
					action = EDITOR_ACTION_DELETE_CHAR;
					break;
				case BACKSPACE:
					action = EDITOR_ACTION_BACKSPACE;
					break;
				case '\r':
				case '\n':
					action = EDITOR_ACTION_NEWLINE;
					break;
				default:
					return 0;
			}
			break;
	}
	if (action_out != NULL) {
		*action_out = action;
	}
	return 1;
}

static int vimSystemTryActionKey(int c, int *effects_out, int *return_now_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (return_now_out != NULL) {
		*return_now_out = 0;
	}
	if (vimSystemActionForKey(vimSystemMode(), c, &action)) {
		int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
		if (editorDispatchProcessMappedAction(action, &mapped_effects)) {
			if (return_now_out != NULL) {
				*return_now_out = 1;
			}
			return 1;
		}
		if (effects_out != NULL) {
			*effects_out |= mapped_effects;
		}
		return 1;
	}
	return 0;
}

static int vimSystemTryDrawerFocusedActionKey(int c, int *effects_out, int *return_now_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (return_now_out != NULL) {
		*return_now_out = 0;
	}
	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
		return 0;
	}

	switch (c) {
		case 'k':
		case ARROW_UP:
			action = EDITOR_ACTION_MOVE_UP;
			break;
		case 'j':
		case ARROW_DOWN:
			action = EDITOR_ACTION_MOVE_DOWN;
			break;
		case 'h':
		case ARROW_LEFT:
			action = EDITOR_ACTION_MOVE_LEFT;
			break;
		case 'l':
		case ARROW_RIGHT:
			action = EDITOR_ACTION_MOVE_RIGHT;
			break;
		case '\r':
		case '\n':
			action = EDITOR_ACTION_NEWLINE;
			break;
		case '\x1b':
			action = EDITOR_ACTION_ESCAPE;
			break;
		default:
			return 0;
	}

	int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
	int return_now = editorDispatchProcessMappedAction(action, &mapped_effects);
	if (effects_out != NULL) {
		*effects_out |= mapped_effects;
	}
	if (return_now_out != NULL) {
		*return_now_out = return_now;
	}
	return 1;
}

static int vimSystemEnterInsertWithAction(enum editorAction action, int *effects_out) {
	int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
	(void)editorDispatchProcessMappedAction(action, &mapped_effects);
	if (effects_out != NULL) {
		*effects_out |= mapped_effects;
	}
	vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
	return 0;
}

/* Vim owns the C0 control range. Unmapped controls are inert; Tab, Enter, and
 * Esc are left to the text and mode paths. */
static int vimSystemIsControlKey(int c) {
	return c > 0 && c < 32 && c != '\t' && c != '\r' && c != '\n' && c != '\x1b';
}

/* Column on `cy` that Insert-mode Ctrl-W deletes back to: skip whitespace
 * immediately before the cursor, then one run of like-classed characters. */
static int vimSystemInsertWordBackCx(int cy, int cx) {
	struct editorLineView line = {0};
	int i = cx;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return cx;
	}
	while (i > 0) {
		int p = editorBytesPrevClusterIdx(line.data, line.size, i);
		if (p >= i || !isspace((unsigned char)line.data[p])) {
			break;
		}
		i = p;
	}
	if (i > 0) {
		int p = editorBytesPrevClusterIdx(line.data, line.size, i);
		enum vimSystemCharClass cls = vimSystemClassAt(&line, p);
		while (i > 0) {
			int q = editorBytesPrevClusterIdx(line.data, line.size, i);
			if (q >= i || vimSystemClassAt(&line, q) != cls) {
				break;
			}
			i = q;
		}
	}
	editorLineViewRelease(&line);
	return i;
}

/* Delete from `target_cx` up to the cursor on the current line (Insert Ctrl-W /
 * Ctrl-U). editorDeleteRange leaves the cursor at the range start. */
static int vimSystemInsertDeleteBackTo(int target_cx, int *effects_out) {
	struct editorSelectionRange range = {
	        .start_cy = E.cy, .start_cx = target_cx, .end_cy = E.cy, .end_cx = E.cx};
	int dirty_before;
	int deleted;

	if (target_cx >= E.cx || vimSystemRejectReadOnlyMutation()) {
		return 0;
	}
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	dirty_before = E.dirty;
	deleted = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	if (deleted > 0) {
		vimSystemAddEditEffect(effects_out);
	}
	return deleted > 0;
}

static int vimSystemHandleInsertKey(int c, int *effects_out) {
	c = vimSystemRemapKey(VIM_SYSTEM_MODE_INSERT, c, NULL);
	if (c == '\x1b') {
		/* End a block insert: drop the width-zero multi-cursor column. */
		editorColumnSelectionClear();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	if (vimSystemIsControlKey(c)) {
		switch (c) {
			case CTRL_KEY('c'):
				editorColumnSelectionClear();
				vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
				return 0;
			case CTRL_KEY('h'): {
				int mapped = EDITOR_INPUT_KEY_EFFECT_NONE;
				(void)editorDispatchProcessMappedAction(EDITOR_ACTION_BACKSPACE,
				                                        &mapped);
				if (effects_out != NULL) {
					*effects_out |= mapped;
				}
				return 0;
			}
			case CTRL_KEY('w'):
				(void)vimSystemInsertDeleteBackTo(
				        vimSystemInsertWordBackCx(E.cy, E.cx), effects_out);
				return 0;
			case CTRL_KEY('u'): {
				int fnb = vimSystemLineFirstNonblank(E.cy);
				int target = E.cx > fnb ? fnb : 0;
				(void)vimSystemInsertDeleteBackTo(target, effects_out);
				return 0;
			}
			default:
				/* Unmapped control keys are inert in Insert mode. */
				return 0;
		}
	}
	int return_now = 0;
	if (vimSystemTryActionKey(c, effects_out, &return_now)) {
		return return_now;
	}
	if (editorByteShouldInsertAsText(c)) {
		editorDispatchHandleTextByte(c, effects_out);
	}
	return 0;
}

/* Live incremental search while the `/` or `?` prompt is open. Searches from the
 * cursor position captured when the prompt opened; restores it on Esc/empty. */
static void vimSystemSearchCallback(const char *query, int key) {
	int match_row = -1;
	int match_col = -1;
	int saved_row = 0;
	int saved_col = 0;
	int found = 0;

	if (key == '\x1b' || query == NULL || query[0] == '\0') {
		(void)editorSyncCursorFromOffset(E.search_saved_offset);
		editorViewportEnsureCursorVisible();
		return;
	}
	(void)editorBufferOffsetToPos(E.search_saved_offset, &saved_row, &saved_col);
	if (E.input_vim_search_direction == 1) {
		found = editorBufferFindForward(query, saved_row, saved_col, &match_row,
		                                &match_col);
	} else {
		found = editorBufferFindBackward(query, saved_row, saved_col, &match_row,
		                                 &match_col);
	}
	if (found) {
		size_t offset = 0;
		if (editorBufferPosToOffset(match_row, match_col, &offset)) {
			(void)editorSyncCursorFromOffset(offset);
			editorViewportEnsureCursorVisible();
			return;
		}
	}
	(void)editorSyncCursorFromOffset(E.search_saved_offset);
	editorViewportEnsureCursorVisible();
}

static int vimSystemSearchSetQuery(const char *query) {
	char *copy = strdup(query);

	if (copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	free(E.input_vim_search_query);
	E.input_vim_search_query = copy;
	return 1;
}

/* Repeats the search `count` times; returns 1 if any match was found. */
static int vimSystemSearchExecute(const char *query, int direction, int count, int *effects_out) {
	int found_any = 0;

	if (query == NULL || query[0] == '\0' || E.numrows == 0) {
		return 0;
	}
	if (count < 1) {
		count = 1;
	}
	editorJumplistRecord();
	for (int i = 0; i < count; i++) {
		int match_row = -1;
		int match_col = -1;
		int found = direction == 1 ? editorBufferFindForward(query, E.cy, E.cx, &match_row,
		                                                     &match_col)
		                           : editorBufferFindBackward(query, E.cy, E.cx, &match_row,
		                                                      &match_col);
		if (!found) {
			break;
		}
		(void)vimSystemSetCursor(match_row, match_col, effects_out);
		editorViewportEnsureCursorVisible();
		found_any = 1;
	}
	if (!found_any) {
		editorSetStatusMsg("Pattern not found: %s", query);
	}
	return found_any;
}

static int vimSystemSearchPrompt(int direction, int *effects_out) {
	char *query = NULL;
	int origin_cy = E.cy;
	int origin_cx = E.cx;

	E.input_vim_search_direction = direction;
	E.search_saved_offset = E.cursor_offset;
	query = editorPromptWithCallback(direction == 1 ? "/%s" : "?%s", 1,
	                                 vimSystemSearchCallback);
	if (query == NULL) {
		(void)editorSyncCursorFromOffset(E.search_saved_offset);
		return 0;
	}
	if (query[0] == '\0') {
		free(query);
		return 0;
	}
	(void)vimSystemSearchSetQuery(query);
	free(query);
	/* Incremental search has already moved the live cursor. */
	editorJumplistRecordPos(origin_cy, origin_cx);
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
	return 1;
}

static int vimSystemSearchRepeat(int direction_sign, int count, int *effects_out) {
	int direction;

	if (E.input_vim_search_query == NULL) {
		editorSetStatusMsg("No previous search");
		return 0;
	}
	direction = E.input_vim_search_direction * direction_sign;
	return vimSystemSearchExecute(E.input_vim_search_query, direction, count, effects_out);
}

/* The keyword run at/after the cursor on the current line, as an owned string.
 * Substring-based (no word-boundary anchoring); caller frees. */
static char *vimSystemWordUnderCursor(void) {
	struct editorLineView line = {0};
	int wcol = -1;
	int saved_cx;
	struct editorSelectionRange range;
	char *text = NULL;
	size_t len = 0;
	int extracted = 0;

	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return NULL;
	}
	for (int i = E.cx; i < line.size;) {
		unsigned char b = (unsigned char)line.data[i];
		if (isalnum(b) || b == '_' || b >= 0x80) {
			wcol = i;
			break;
		}
		int n = editorBytesNextClusterIdx(line.data, line.size, i);
		if (n <= i) {
			break;
		}
		i = n;
	}
	editorLineViewRelease(&line);
	if (wcol < 0) {
		return NULL;
	}
	saved_cx = E.cx;
	E.cx = wcol;
	if (vimSystemWordObjectRange(1, &range)) {
		extracted = editorExtractRangeText(&range, &text, &len);
	}
	E.cx = saved_cx;
	if (extracted <= 0) {
		free(text);
		return NULL;
	}
	return text;
}

/* `*` / `#`: search for the word under the cursor; also primes `n`/`N`. */
static int vimSystemSearchWordUnderCursor(int direction, int count, int *effects_out) {
	char *word = vimSystemWordUnderCursor();

	if (word == NULL || word[0] == '\0') {
		free(word);
		editorSetStatusMsg("No word under cursor");
		return 0;
	}
	if (!vimSystemSearchSetQuery(word)) {
		free(word);
		return 0;
	}
	E.input_vim_search_direction = direction;
	(void)vimSystemSearchExecute(word, direction, count, effects_out);
	free(word);
	return 1;
}

static int vimSystemStringIsAllDigits(const char *s) {
	if (s == NULL || *s == '\0') {
		return 0;
	}
	for (; *s != '\0'; s++) {
		if (!isdigit((unsigned char)*s)) {
			return 0;
		}
	}
	return 1;
}

static void vimSystemGotoLine(long line_no, int *effects_out) {
	int target = 0;

	if (E.numrows == 0) {
		return;
	}
	if (line_no < 1) {
		line_no = 1;
	}
	if (line_no > E.numrows) {
		line_no = E.numrows;
	}
	target = (int)(line_no - 1);
	if (target != E.cy) {
		editorJumplistRecord();
	}
	(void)vimSystemSetCursor(target, vimSystemLineFirstNonblank(target), effects_out);
}

/* Parse and apply `:[%]s/pattern/replacement/[flags]`. `args` points just past
 * the `s` and is mutated in place to terminate the pattern/replacement fields. */
static void vimSystemExSubstitute(char *args, int *effects_out) {
	char delimiter = 0;
	char *pattern = NULL;
	char *replacement = NULL;
	char *pattern_end = NULL;
	char *replacement_end = NULL;
	const char *flags = "";
	int global = 0;
	int replaced = 0;

	if (args == NULL || args[0] == '\0' || args[0] == ' ') {
		editorSetStatusMsg("E486: Pattern delimiter expected");
		return;
	}
	delimiter = args[0];
	pattern = args + 1;
	pattern_end = strchr(pattern, delimiter);
	if (pattern_end == NULL) {
		editorSetStatusMsg("Invalid substitute command");
		return;
	}
	*pattern_end = '\0';
	replacement = pattern_end + 1;
	replacement_end = strchr(replacement, delimiter);
	if (replacement_end != NULL) {
		*replacement_end = '\0';
		flags = replacement_end + 1;
	}
	if (pattern[0] == '\0') {
		editorSetStatusMsg("Empty search pattern");
		return;
	}
	global = strchr(flags, 'g') != NULL;

	if (vimSystemRejectReadOnlyMutation()) {
		return;
	}
	replaced = editorDispatchSubstituteInBuffer(pattern, replacement, global);
	if (replaced > 0) {
		vimSystemAddEditEffect(effects_out);
		editorSetStatusMsg("%d substitution(s)", replaced);
	} else if (replaced == 0) {
		editorSetStatusMsg("Pattern not found: %s", pattern);
	}
}

static void vimSystemExDispatch(enum editorAction action, int *effects_out) {
	int mapped = EDITOR_INPUT_KEY_EFFECT_NONE;
	(void)editorDispatchProcessMappedAction(action, &mapped);
	if (effects_out != NULL) {
		*effects_out |= mapped;
	}
}

static char *vimSystemExTrim(char *s) {
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
		s[--len] = '\0';
	}
	return s;
}

static int vimSystemExActionForName(const char *name, enum editorAction *action_out) {
	for (size_t i = 0; i < g_vim_ex_command_count; i++) {
		if (strcmp(g_vim_ex_commands[i].name, name) == 0) {
			if (action_out != NULL) {
				*action_out = g_vim_ex_commands[i].action;
			}
			return 1;
		}
	}
	return 0;
}

static int vimSystemExCommandIs(const char *cmd, const char *a, const char *b, const char *c) {
	return strcmp(cmd, a) == 0 || (b != NULL && strcmp(cmd, b) == 0) ||
	       (c != NULL && strcmp(cmd, c) == 0);
}

static int vimSystemExOpenPath(const char *path, int *effects_out) {
	if (!editorTabOpenOrSwitchToFile(path)) {
		editorSetStatusMsg("Could not open %s", path);
		return 0;
	}
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
	return 1;
}

static void vimSystemExSplitOpen(enum editorAction split_action, const char *path,
                                 int *effects_out) {
	/* :split <file> / :vsplit <file> open the file in a NEW window showing only
	 * that file (matching Vim), unlike bare :split/:vsplit which duplicate the
	 * current window. */
	enum editorSplitOrientation orientation = split_action == EDITOR_ACTION_SPLIT_VERTICAL
	                                                  ? EDITOR_SPLIT_VERTICAL
	                                                  : EDITOR_SPLIT_HORIZONTAL;
	if (editorTabOpenFileInSplit(orientation, 0.5, path) == NULL) {
		editorSetStatusMsg("Could not open %s", path);
		return;
	}
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
}

static char *vimSystemExCompleteFn(const char *current, const char *anchor, void *ctx,
                                   int tab_iteration) {
	(void)anchor;
	(void)ctx;
	if (current == NULL) {
		return NULL;
	}
	const char *matches[64];
	int match_count = 0;
	size_t prefix_len = strlen(current);
	for (size_t i = 0; i < g_vim_ex_command_count && match_count < 64; i++) {
		if (strncmp(g_vim_ex_commands[i].name, current, prefix_len) == 0) {
			matches[match_count++] = g_vim_ex_commands[i].name;
		}
	}
	for (size_t i = 0; i < g_vim_ex_builtin_command_count && match_count < 64; i++) {
		if (strncmp(g_vim_ex_builtin_commands[i], current, prefix_len) == 0) {
			matches[match_count++] = g_vim_ex_builtin_commands[i];
		}
	}
	if (match_count == 0) {
		return NULL;
	}
	int idx = tab_iteration % match_count;
	if (idx < 0) {
		idx = 0;
	}
	return strdup(matches[idx]);
}

char *vimSystemExCompletionTest(const char *current, int tab_iteration) {
	return vimSystemExCompleteFn(current, current, NULL, tab_iteration);
}

/* `:git [subcommand]` — no args opens the Git drawer. */
static void vimSystemExGit(const char *args, int *effects_out) {
	static const struct vimExCommand k_git_subcommands[] = {
	        {"branches", EDITOR_ACTION_GIT_BRANCHES}, {"log", EDITOR_ACTION_GIT_LOG},
	        {"stash", EDITOR_ACTION_GIT_STASHES},     {"stashes", EDITOR_ACTION_GIT_STASHES},
	        {"commit", EDITOR_ACTION_GIT_COMMIT},     {"amend", EDITOR_ACTION_GIT_COMMIT_AMEND},
	        {"push", EDITOR_ACTION_GIT_PUSH},         {"pull", EDITOR_ACTION_GIT_PULL},
	        {"fetch", EDITOR_ACTION_GIT_FETCH},
	};
	if (args == NULL || args[0] == '\0') {
		vimSystemExDispatch(EDITOR_ACTION_GIT_DRAWER, effects_out);
		return;
	}
	for (size_t i = 0; i < sizeof(k_git_subcommands) / sizeof(k_git_subcommands[0]); i++) {
		if (strcmp(k_git_subcommands[i].name, args) == 0) {
			vimSystemExDispatch(k_git_subcommands[i].action, effects_out);
			return;
		}
	}
	editorSetStatusMsg("Unknown :git subcommand: %s", args);
}

static void vimSystemExLatex(const char *args, int *effects_out) {
	static const struct vimExCommand k_latex_subcommands[] = {
	        {"view", EDITOR_ACTION_LATEX_FORWARD_SEARCH},
	        {"build", EDITOR_ACTION_LATEX_BUILD},
	};
	if (args == NULL || args[0] == '\0') {
		editorSetStatusMsg("Usage: :latex view|build");
		return;
	}
	for (size_t i = 0; i < sizeof(k_latex_subcommands) / sizeof(k_latex_subcommands[0]); i++) {
		if (strcmp(k_latex_subcommands[i].name, args) == 0) {
			vimSystemExDispatch(k_latex_subcommands[i].action, effects_out);
			return;
		}
	}
	editorSetStatusMsg("Unknown :latex subcommand: %s", args);
}

/*
 * `:lsp [subcommand]` — no args opens the LSP drawer. `install-server [name]`
 * installs a language server; with no name it targets the current buffer's
 * language server.
 */
static void vimSystemExLsp(const char *args, int *effects_out) {
	if (args == NULL || args[0] == '\0') {
		vimSystemExDispatch(EDITOR_ACTION_LSP_DRAWER, effects_out);
		return;
	}

	const char *sub = args;
	size_t sub_len = strcspn(sub, " \t");
	const char *name = sub + sub_len;
	while (*name == ' ' || *name == '\t') {
		name++;
	}

	if (strncmp(sub, "install-server", sub_len) == 0 && sub_len == strlen("install-server")) {
		if (name[0] == '\0') {
			/* No name: install the current buffer's language server. */
			const char *current = editorLanguageGoToServerName();
			if (current == NULL || !editorLanguageInstallServerByName(current)) {
				editorSetStatusMsg(
				        "No installable language server for this buffer; "
				        "name one: :lsp install-server <server>");
			}
			return;
		}
		if (!editorLanguageInstallServerByName(name)) {
			editorSetStatusMsg("Unknown language server: %s (try gopls, clangd, "
			                   "texlab, typescript-language-server, "
			                   "vscode-langservers-extracted)",
			                   name);
		}
		return;
	}

	editorSetStatusMsg("Unknown :lsp subcommand: %s", args);
}

static const char *vimSystemJumpBasename(const char *path) {
	if (path == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(path, '/');
	return slash != NULL ? slash + 1 : path;
}

static void vimSystemExJumps(void) {
	int count = editorJumplistActiveCount();
	int index = editorJumplistActiveIndex();

	if (count == 0) {
		editorSetStatusMsg("jumps: empty");
		return;
	}

	const struct editorJumpEntry *back = NULL;
	const struct editorJumpEntry *fwd = NULL;
	if (index >= count) {
		back = editorJumplistActiveEntry(count - 1);
	} else if (index > 0) {
		back = editorJumplistActiveEntry(index - 1);
	}
	if (index < count - 1) {
		fwd = editorJumplistActiveEntry(index + 1);
	}

	char back_buf[40];
	char fwd_buf[40];
	if (back != NULL) {
		(void)snprintf(back_buf, sizeof(back_buf), "%s:%d",
		               vimSystemJumpBasename(editorJumplistResolvePath(back->path_id)),
		               back->cy + 1);
	} else {
		(void)snprintf(back_buf, sizeof(back_buf), "-");
	}
	if (fwd != NULL) {
		(void)snprintf(fwd_buf, sizeof(fwd_buf), "%s:%d",
		               vimSystemJumpBasename(editorJumplistResolvePath(fwd->path_id)),
		               fwd->cy + 1);
	} else {
		(void)snprintf(fwd_buf, sizeof(fwd_buf), "-");
	}
	editorSetStatusMsg("jumps %d/%d  <- %s  -> %s", index, count, back_buf, fwd_buf);
}

static void vimSystemRunExCommand(char *line, int *effects_out) {
	char *cmd = line;
	char *args = NULL;
	enum editorAction action = EDITOR_ACTION_COUNT;
	size_t len = 0;

	while (*cmd == ' ' || *cmd == '\t' || *cmd == ':') {
		cmd++;
	}
	cmd = vimSystemExTrim(cmd);
	if (*cmd == '\0') {
		return;
	}

	len = strcspn(cmd, " \t");
	if (cmd[len] != '\0') {
		args = cmd + len + 1;
		cmd[len] = '\0';
		args = vimSystemExTrim(args);
	}

	if (vimSystemStringIsAllDigits(cmd)) {
		vimSystemGotoLine(strtol(cmd, NULL, 10), effects_out);
		return;
	}
	if (strcmp(cmd, "w") == 0) {
		editorSave();
		return;
	}
	if (strcmp(cmd, "q") == 0) {
		if (E.dirty) {
			editorSetStatusMsg("No write since last change (add ! to override)");
			return;
		}
		editorActionQuit();
		return;
	}
	if (strcmp(cmd, "q!") == 0) {
		editorActionQuitForce();
		return;
	}
	if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
		editorSave();
		editorActionQuit();
		return;
	}
	if (cmd[0] == '%' && cmd[1] == 's') {
		vimSystemExSubstitute(cmd + 2, effects_out);
		return;
	}
	if (strcmp(cmd, "git") == 0) {
		vimSystemExGit(args, effects_out);
		return;
	}
	if (strcmp(cmd, "lsp") == 0) {
		vimSystemExLsp(args, effects_out);
		return;
	}
	if (strcmp(cmd, "latex") == 0) {
		vimSystemExLatex(args, effects_out);
		return;
	}
	if (strcmp(cmd, "jumps") == 0) {
		vimSystemExJumps();
		return;
	}
	if (args != NULL && args[0] != '\0') {
		if (vimSystemExCommandIs(cmd, "e", "edit", NULL)) {
			(void)vimSystemExOpenPath(args, effects_out);
			return;
		}
		if (vimSystemExCommandIs(cmd, "split", "sp", NULL)) {
			vimSystemExSplitOpen(EDITOR_ACTION_SPLIT_HORIZONTAL, args, effects_out);
			return;
		}
		if (vimSystemExCommandIs(cmd, "vsplit", "vs", "vsp")) {
			vimSystemExSplitOpen(EDITOR_ACTION_SPLIT_VERTICAL, args, effects_out);
			return;
		}
	} else if (vimSystemExCommandIs(cmd, "e", "edit", NULL)) {
		if (E.filename != NULL) {
			(void)editorOpen(E.filename);
			if (effects_out != NULL) {
				*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
			}
		}
		return;
	}
	if (vimSystemExActionForName(cmd, &action)) {
		vimSystemExDispatch(action, effects_out);
		return;
	}
	editorSetStatusMsg("Not an editor command: %s", cmd);
}

static int vimSystemExCommandLine(int *effects_out) {
	char *line = editorPromptWithCompletion(":%s", 1, vimSystemExCompleteFn, NULL);

	if (line == NULL) {
		return 0;
	}
	vimSystemRunExCommand(line, effects_out);
	free(line);
	return 1;
}

/* Resolve the target char of a pending `f`/`F`/`t`/`T`. Applies the find as an
 * operator motion when an operator is pending, otherwise moves the cursor. */
static int vimSystemResolveFind(int target, int *effects_out) {
	int cmd = E.input_vim_pending_find;
	enum vimSystemMotion motion;

	E.input_vim_pending_find = 0;
	if (cmd == 0 || target == '\x1b') {
		vimSystemResetPending();
		return 0;
	}
	E.input_vim_last_find_cmd = cmd;
	E.input_vim_last_find_char = target;
	motion = vimSystemFindMotionForCmd(cmd);
	if (E.input_vim_pending_operator != VIM_SYSTEM_OPERATOR_NONE) {
		enum vimSystemOperator op = (enum vimSystemOperator)E.input_vim_pending_operator;
		(void)vimSystemApplyOperatorMotion(op, motion, vimSystemOperatorTotalCount(),
		                                   effects_out);
	} else {
		(void)vimSystemApplyMotion(motion, vimSystemEffectiveCount(), effects_out);
	}
	vimSystemResetPending();
	return 0;
}

static void vimSystemDotRepeatSuppress(void);

/* Move the cursor and viewport together by `delta` rows (Ctrl-D/U/F/B). Moving
 * the cursor is what keeps the scroll from snapping back when the frame
 * re-runs cursor-visibility. */
static int vimSystemScrollAndMove(int delta, int *effects_out) {
	int target;

	if (E.numrows == 0) {
		return 0;
	}
	editorViewportScrollByRows(delta);
	target = E.cy + delta;
	if (target < 0) {
		target = 0;
	}
	if (target > E.numrows - 1) {
		target = E.numrows - 1;
	}
	E.cy = target;
	vimSystemClampNormalCursor();
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_VIEWPORT_SCROLL |
		                EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
	return 0;
}

/* Scroll the viewport by `delta` rows (Ctrl-E/Y), pulling the cursor only as far
 * as needed to keep it on screen. */
static int vimSystemScrollView(int delta, int *effects_out) {
	int body;
	int top;

	if (E.numrows == 0) {
		return 0;
	}
	editorViewportScrollByRows(delta);
	body = editorViewportFocusedPaneBodyRows();
	if (body < 1) {
		body = 1;
	}
	top = E.rowoff < 0 ? 0 : E.rowoff;
	if (E.cy < top) {
		E.cy = top;
	} else if (E.cy > top + body - 1) {
		E.cy = top + body - 1;
	}
	if (E.cy > E.numrows - 1) {
		E.cy = E.numrows - 1;
	}
	vimSystemClampNormalCursor();
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_VIEWPORT_SCROLL |
		                EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
	return 0;
}

/* Control-key bindings shared by Normal and Visual mode. Returns 1 when the key
 * was a control key (handled or deliberately swallowed), 0 otherwise. */
static int vimSystemHandleSharedControlKey(int c, int *effects_out, int *result_out) {
	int body = editorViewportFocusedPaneBodyRows();
	int half;
	int page;

	if (!vimSystemIsControlKey(c)) {
		return 0;
	}
	if (body < 1) {
		body = 1;
	}
	half = body / 2 < 1 ? 1 : body / 2;
	page = body > 1 ? body - 1 : 1;
	*result_out = 0;
	switch (c) {
		case CTRL_KEY('d'):
			*result_out = vimSystemScrollAndMove(half, effects_out);
			break;
		case CTRL_KEY('u'):
			*result_out = vimSystemScrollAndMove(-half, effects_out);
			break;
		case CTRL_KEY('f'):
			*result_out = vimSystemScrollAndMove(page, effects_out);
			break;
		case CTRL_KEY('b'):
			*result_out = vimSystemScrollAndMove(-page, effects_out);
			break;
		case CTRL_KEY('e'):
			*result_out = vimSystemScrollView(1, effects_out);
			break;
		case CTRL_KEY('y'):
			*result_out = vimSystemScrollView(-1, effects_out);
			break;
		default:
			break;
	}
	return 1;
}

static int vimSystemHandleNormalKey(int c, int *effects_out) {
	int motion_count = 0;
	int motion_result = 0;
	int disabled = 0;
	int shared_result = 0;

	if (E.input_vim_pending_ctrl_w) {
		enum editorAction action = EDITOR_ACTION_COUNT;
		int return_now = 0;
		vimSystemResetPending();
		if (c != '\x1b' && vimSystemCtrlWAction(c, &action)) {
			int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
			return_now = editorDispatchProcessMappedAction(action, &mapped_effects);
			if (effects_out != NULL) {
				*effects_out |= mapped_effects;
			}
		}
		return return_now;
	}

	if (vimSystemIsControlKey(c)) {
		switch (c) {
			case CTRL_KEY('w'):
				vimSystemResetPending();
				E.input_vim_pending_ctrl_w = 1;
				return 0;
			case CTRL_KEY('r'): {
				int mapped = EDITOR_INPUT_KEY_EFFECT_NONE;
				(void)editorDispatchProcessMappedAction(EDITOR_ACTION_REDO,
				                                        &mapped);
				vimSystemClampNormalCursor();
				vimSystemDotRepeatSuppress();
				if (effects_out != NULL) {
					*effects_out |= mapped;
				}
				vimSystemResetPending();
				return 0;
			}
			case CTRL_KEY('c'):
				vimSystemResetPending();
				return 0;
			case CTRL_KEY('v'):
				vimSystemResetPending();
				vimSystemBeginVisualBlock();
				return 0;
			case CTRL_KEY('o'): {
				int count = vimSystemEffectiveCount();
				vimSystemResetPending();
				(void)editorDispatchJumplistBack(count, effects_out);
				return 0;
			}
			default:
				break;
		}
		(void)vimSystemHandleSharedControlKey(c, effects_out, &shared_result);
		return shared_result;
	}

	/* Terminals encode Ctrl-I as Tab, outside the control-key path above. */
	if (c == '\t') {
		int count = vimSystemEffectiveCount();
		vimSystemResetPending();
		(void)editorDispatchJumplistForward(count, effects_out);
		return 0;
	}

	if (E.input_vim_pending_find) {
		return vimSystemResolveFind(c, effects_out);
	}
	if (E.input_vim_pending_replace) {
		int count = vimSystemEffectiveCount();
		E.input_vim_pending_replace = 0;
		if (c != '\x1b') {
			(void)vimSystemReplaceChar(c, count, effects_out);
		}
		vimSystemResetPending();
		return 0;
	}
	if (E.input_vim_pending_z) {
		E.input_vim_pending_z = 0;
		vimSystemResetPending();
		if (c == 'Z') {
			editorSave();
			editorActionQuit();
		} else if (c == 'Q') {
			editorActionQuitForce();
		}
		return 0;
	}
	if (E.input_vim_pending_bracket) {
		int bracket = E.input_vim_pending_bracket;
		E.input_vim_pending_bracket = 0;
		vimSystemResetPending();
		if (c == 'g') {
			enum editorAction action = bracket == ']' ? EDITOR_ACTION_DIAGNOSTIC_NEXT
			                                          : EDITOR_ACTION_DIAGNOSTIC_PREV;
			int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
			int return_now = editorDispatchProcessMappedAction(action, &mapped_effects);
			if (effects_out != NULL) {
				*effects_out |= mapped_effects;
			}
			return return_now;
		}
		return 0;
	}
	if (E.input_vim_pending_mark) {
		int cmd = E.input_vim_pending_mark;
		E.input_vim_pending_mark = 0;
		if (c >= 'a' && c <= 'z') {
			struct editorVimMark *mark = &E.vim_marks[c - 'a'];
			if (cmd == 'm') {
				free(mark->filename);
				mark->filename = E.filename != NULL ? strdup(E.filename) : NULL;
				mark->set = 1;
				mark->cy = E.cy;
				mark->cx = E.cx;
			} else if (mark->set) {
				int same_file = (mark->filename == NULL && E.filename == NULL) ||
				                (mark->filename != NULL && E.filename != NULL &&
				                 strcmp(mark->filename, E.filename) == 0);
				editorJumplistRecord();
				if (same_file) {
					int tcy = mark->cy;
					if (tcy >= E.numrows) {
						tcy = E.numrows - 1;
					}
					if (tcy < 0) {
						tcy = 0;
					}
					int tcx = cmd == '`' ? mark->cx
					                     : vimSystemLineFirstNonblank(tcy);
					(void)vimSystemSetCursor(tcy, tcx, effects_out);
				} else if (mark->filename != NULL) {
					int tcx = cmd == '`' ? mark->cx : 0;
					if (editorDispatchGoToBufferPosition(mark->filename,
					                                     mark->cy, tcx)) {
						if (cmd == '\'') {
							(void)vimSystemSetCursor(
							        E.cy,
							        vimSystemLineFirstNonblank(E.cy),
							        effects_out);
						}
						if (effects_out != NULL) {
							*effects_out |=
							        EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
						}
					}
				}
			}
		}
		vimSystemResetPending();
		return 0;
	}
	if (E.input_vim_pending_register) {
		vimSystemConsumeRegisterKey(c);
		return 0;
	}
	if (E.input_vim_pending_leader) {
		enum editorAction action = EDITOR_ACTION_COUNT;
		int return_now = 0;
		vimSystemResetPending();
		if (c != '\x1b' && vimSystemLeaderLookup(c, &action)) {
			int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
			return_now = editorDispatchProcessMappedAction(action, &mapped_effects);
			if (effects_out != NULL) {
				*effects_out |= mapped_effects;
			}
		}
		return return_now;
	}
	if (E.input_vim_pending_g) {
		enum editorAction action = EDITOR_ACTION_COUNT;
		if (c == 'q') {
			/* `gq` becomes a reflow operator; the next motion (or `q` for the
			 * current line) supplies the range. */
			E.input_vim_pending_g = 0;
			E.input_vim_pending_operator = VIM_SYSTEM_OPERATOR_REFLOW;
			E.input_vim_pending_operator_g = 0;
			E.input_vim_operator_count = E.input_vim_count;
			E.input_vim_count = 0;
			return 0;
		}
		if (vimSystemGPrefixAction(c, &action)) {
			int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
			int return_now = 0;
			vimSystemResetPending();
			return_now = editorDispatchProcessMappedAction(action, &mapped_effects);
			if (effects_out != NULL) {
				*effects_out |= mapped_effects;
			}
			return return_now;
		}
	}
	if (E.input_vim_pending_operator != VIM_SYSTEM_OPERATOR_NONE &&
	    vimSystemPendingOperatorKeyIsStructural(c)) {
		return vimSystemHandlePendingOperatorKey(c, effects_out);
	}
	if (vimSystemConsumeCountKey(c)) {
		return 0;
	}
	c = vimSystemRemapKey(VIM_SYSTEM_MODE_NORMAL, c, &disabled);
	if (disabled) {
		return 0;
	}
	if (E.input_vim_pending_operator != VIM_SYSTEM_OPERATOR_NONE) {
		return vimSystemHandlePendingOperatorKey(c, effects_out);
	}
	{
		int return_now = 0;
		if (vimSystemTryDrawerFocusedActionKey(c, effects_out, &return_now)) {
			vimSystemResetPending();
			return return_now;
		}
	}
	if (c == '"') {
		E.input_vim_pending_register = 1;
		return 0;
	}
	if (c == g_vim_leader_key) {
		vimSystemResetPending();
		E.input_vim_pending_leader = 1;
		return 0;
	}
	if (vimSystemFindKeyIsCommand(c)) {
		E.input_vim_pending_find = c;
		return 0;
	}

	motion_count = vimSystemEffectiveCount();
	motion_result = vimSystemTryMotionKey(c, motion_count, effects_out);
	if (motion_result == 2) {
		vimSystemResetPending();
		return 0;
	}
	if (motion_result == 1) {
		return 0;
	}

	switch (c) {
		case '\x1b':
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
			return 0;
		case 'i':
			vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
			return 0;
		case 'a':
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_RIGHT,
			                                      effects_out);
		case 'I':
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_HOME, effects_out);
		case 'A':
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_END, effects_out);
		case 'o':
			(void)vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_END, effects_out);
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_NEWLINE, effects_out);
		case 'O':
			(void)vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_HOME, effects_out);
			(void)vimSystemEnterInsertWithAction(EDITOR_ACTION_NEWLINE, effects_out);
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_UP, effects_out);
		case 'x':
			(void)vimSystemDeleteUnderCursor(motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case 'd':
		case 'c':
		case 'y':
		case '>':
		case '<':
			E.input_vim_pending_operator = c;
			E.input_vim_pending_operator_g = 0;
			E.input_vim_operator_count = E.input_vim_count;
			E.input_vim_count = 0;
			return 0;
		case 'D':
			(void)vimSystemDeleteToLineEnd(VIM_SYSTEM_OPERATOR_DELETE, effects_out);
			vimSystemResetPending();
			return 0;
		case 'C':
			(void)vimSystemDeleteToLineEnd(VIM_SYSTEM_OPERATOR_CHANGE, effects_out);
			return 0;
		case 'Y':
			(void)vimSystemYankLines(E.cy, E.cy, 1);
			vimSystemResetPending();
			return 0;
		case 'r':
			E.input_vim_pending_replace = 1;
			return 0;
		case 'u': {
			int mapped = EDITOR_INPUT_KEY_EFFECT_NONE;
			(void)editorDispatchProcessMappedAction(EDITOR_ACTION_UNDO, &mapped);
			vimSystemClampNormalCursor();
			vimSystemDotRepeatSuppress();
			if (effects_out != NULL) {
				*effects_out |= mapped;
			}
			vimSystemResetPending();
			return 0;
		}
		case '~':
			(void)vimSystemToggleCase(motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case 'J':
			(void)vimSystemJoinLines(motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case 'K': {
			int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
			int return_now = 0;
			vimSystemResetPending();
			return_now = editorDispatchProcessMappedAction(EDITOR_ACTION_HOVER,
			                                               &mapped_effects);
			if (effects_out != NULL) {
				*effects_out |= mapped_effects;
			}
			return return_now;
		}
		case 'Z':
			E.input_vim_pending_z = 1;
			return 0;
		case 'm':
		case '`':
		case '\'':
			E.input_vim_pending_mark = c;
			return 0;
		case '[':
		case ']':
			E.input_vim_pending_bracket = c;
			return 0;
		case 'p':
			(void)vimSystemPasteDefaultRegister(1, effects_out);
			vimSystemResetPending();
			return 0;
		case 'P':
			(void)vimSystemPasteDefaultRegister(0, effects_out);
			vimSystemResetPending();
			return 0;
		case '/':
			(void)vimSystemSearchPrompt(1, effects_out);
			vimSystemResetPending();
			return 0;
		case '?':
			(void)vimSystemSearchPrompt(-1, effects_out);
			vimSystemResetPending();
			return 0;
		case 'n':
			(void)vimSystemSearchRepeat(1, motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case 'N':
			(void)vimSystemSearchRepeat(-1, motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case '*':
			(void)vimSystemSearchWordUnderCursor(1, motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case '#':
			(void)vimSystemSearchWordUnderCursor(-1, motion_count, effects_out);
			vimSystemResetPending();
			return 0;
		case ':':
			(void)vimSystemExCommandLine(effects_out);
			vimSystemResetPending();
			return 0;
		case 'v':
			vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL);
			return 0;
		case 'V':
			vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL_LINE);
			return 0;
		default:
			int return_now = 0;
			if (vimSystemTryActionKey(c, effects_out, &return_now)) {
				vimSystemResetPending();
				return return_now;
			}
			vimSystemResetPending();
			return 0;
	}
}

static int vimSystemVisualRange(struct editorSelectionRange *range_out, int *linewise_out) {
	if (linewise_out != NULL) {
		*linewise_out = vimSystemMode() == VIM_SYSTEM_MODE_VISUAL_LINE;
	}
	if (vimSystemMode() == VIM_SYSTEM_MODE_VISUAL_LINE) {
		int anchor_cy = 0;
		int anchor_cx = 0;
		if (!editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_cy, &anchor_cx)) {
			return 0;
		}
		(void)anchor_cx;
		return vimSystemLineRange(anchor_cy, E.cy, range_out);
	}
	if (E.input_vim_visual_selection_half_open) {
		return editorGetSelectionRange(range_out);
	}

	int anchor_cy = 0;
	int anchor_cx = 0;
	if (!editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_cy, &anchor_cx)) {
		return 0;
	}
	int start_cy = anchor_cy;
	int start_cx = anchor_cx;
	int end_cy = E.cy;
	int end_cx = E.cx;
	if (vimSystemPositionComesBefore(end_cy, end_cx, start_cy, start_cx)) {
		int tmp_cy = start_cy;
		int tmp_cx = start_cx;
		start_cy = end_cy;
		start_cx = end_cx;
		end_cy = tmp_cy;
		end_cx = tmp_cx;
	}
	int after_cy = end_cy;
	int after_cx = end_cx;
	if (vimSystemPositionAfterCluster(end_cy, end_cx, &after_cy, &after_cx)) {
		end_cy = after_cy;
		end_cx = after_cx;
	}
	return vimSystemMakeRange(start_cy, start_cx, end_cy, end_cx, range_out);
}

/* Select a text object in Visual mode: anchor at the object start, cursor on its
 * last inclusive position (or last row for the linewise paragraph object). */
static int vimSystemVisualSelectObject(int inner, int object_key, int *effects_out) {
	struct editorSelectionRange range;
	int linewise = 0;
	size_t anchor_offset = 0;
	int cursor_cy = 0;
	int cursor_cx = 0;

	if (!vimSystemTextObjectRange(inner, object_key, &range, &linewise)) {
		return 0;
	}
	if (linewise) {
		int last_row = vimSystemLineRangeLastRow(&range);
		if (!editorBufferPosToOffset(range.start_cy, 0, &anchor_offset)) {
			return 0;
		}
		E.selection_mode_active = 1;
		E.selection_anchor_offset = anchor_offset;
		E.input_vim_mode = VIM_SYSTEM_MODE_VISUAL_LINE;
		vimSystemSetVisualHalfOpen(0);
		(void)vimSystemSetCursor(last_row, vimSystemLineFirstNonblank(last_row),
		                         effects_out);
		return 1;
	}
	cursor_cy = range.end_cy;
	cursor_cx = range.end_cx;
	if (!editorBufferPosToOffset(range.start_cy, range.start_cx, &anchor_offset)) {
		return 0;
	}
	E.selection_mode_active = 1;
	E.selection_anchor_offset = anchor_offset;
	vimSystemSetVisualHalfOpen(1);
	(void)vimSystemSetCursor(cursor_cy, cursor_cx, effects_out);
	return 1;
}

static int vimSystemHandleVisualKey(int c, int *effects_out) {
	int motion_count = 0;
	int motion_result = 0;
	int disabled = 0;
	int shared_result = 0;

	if (vimSystemIsControlKey(c)) {
		if (c == CTRL_KEY('c')) {
			editorClearSelectionState();
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
			return 0;
		}
		if (c == CTRL_KEY('v')) {
			int anchor_cy = E.cy;
			int anchor_cx = E.cx;
			(void)editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_cy,
			                              &anchor_cx);
			editorClearSelectionState();
			vimSystemSetMode(VIM_SYSTEM_MODE_VISUAL_BLOCK);
			E.selection_mode_active = 0;
			E.column_select_active = 1;
			E.column_select_anchor_cy = anchor_cy;
			E.input_vim_block_anchor_cx = anchor_cx;
			vimSystemBlockSync();
			return 0;
		}
		(void)vimSystemHandleSharedControlKey(c, effects_out, &shared_result);
		return shared_result;
	}

	if (E.input_vim_pending_find) {
		int cmd = E.input_vim_pending_find;
		E.input_vim_pending_find = 0;
		if (c != '\x1b') {
			E.input_vim_last_find_cmd = cmd;
			E.input_vim_last_find_char = c;
			(void)vimSystemApplyMotion(vimSystemFindMotionForCmd(cmd),
			                           vimSystemEffectiveCount(), effects_out);
			vimSystemSetVisualHalfOpen(0);
		}
		E.input_vim_count = 0;
		return 0;
	}
	if (E.input_vim_pending_register) {
		vimSystemConsumeRegisterKey(c);
		return 0;
	}
	if (E.input_vim_pending_text_object) {
		int inner = E.input_vim_pending_text_object == 'i';
		E.input_vim_pending_text_object = 0;
		(void)vimSystemVisualSelectObject(inner, c, effects_out);
		return 0;
	}
	if (E.input_vim_pending_g && c == 'q') {
		struct editorSelectionRange range;
		E.input_vim_pending_g = 0;
		if (vimSystemVisualRange(&range, NULL)) {
			(void)vimSystemReflowLines(range.start_cy,
			                           vimSystemLineRangeLastRow(&range), effects_out);
		}
		editorClearSelectionState();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	if (vimSystemConsumeCountKey(c)) {
		return 0;
	}
	c = vimSystemRemapKey(vimSystemMode(), c, &disabled);
	if (disabled) {
		return 0;
	}
	if (c == '\x1b') {
		editorClearSelectionState();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	if (c == 'v') {
		vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL);
		return 0;
	}
	if (c == 'V') {
		vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL_LINE);
		return 0;
	}
	if (c == '"') {
		E.input_vim_pending_register = 1;
		return 0;
	}
	if (c == 'i' || c == 'a') {
		E.input_vim_pending_text_object = c;
		return 0;
	}
	if (vimSystemFindKeyIsCommand(c)) {
		E.input_vim_pending_find = c;
		return 0;
	}

	motion_count = vimSystemEffectiveCount();
	motion_result = vimSystemTryMotionKey(c, motion_count, effects_out);
	if (motion_result == 2) {
		vimSystemSetVisualHalfOpen(0);
		E.input_vim_count = 0;
		return 0;
	}
	if (motion_result == 1) {
		return 0;
	}
	if (c == '>' || c == '<') {
		struct editorSelectionRange range;
		if (vimSystemVisualRange(&range, NULL)) {
			(void)vimSystemIndentLines(range.start_cy,
			                           vimSystemLineRangeLastRow(&range), c == '<',
			                           effects_out);
		}
		editorClearSelectionState();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	if (c == 'd' || c == 'c' || c == 'y' || c == 'x') {
		enum vimSystemOperator op =
		        c == 'x' ? VIM_SYSTEM_OPERATOR_DELETE : (enum vimSystemOperator)c;
		struct editorSelectionRange range;
		int linewise = 0;
		if (vimSystemVisualRange(&range, &linewise)) {
			(void)vimSystemApplyOperatorToRange(op, &range, linewise, effects_out);
		}
		editorClearSelectionState();
		if (op != VIM_SYSTEM_OPERATOR_CHANGE) {
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		}
		return 0;
	}
	if (c == 'p' || c == 'P') {
		struct editorSelectionRange range;
		if (vimSystemVisualRange(&range, NULL)) {
			if (vimSystemRejectReadOnlyMutation()) {
				editorClearSelectionState();
				vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
				return 0;
			}
			editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
			int dirty_before = E.dirty;
			size_t clip_len = 0;
			const char *clip = vimSystemRegisterFetch(&clip_len, NULL);
			int pasted = 0;
			if (clip != NULL && clip_len > 0) {
				pasted = editorReplaceRange(&range, clip, clip_len) > 0;
			}
			editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
			editorHistoryBreakGroup();
			if (pasted) {
				vimSystemAddEditEffect(effects_out);
				vimSystemClampNormalCursor();
			}
		}
		editorClearSelectionState();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	int return_now = 0;
	if (vimSystemTryActionKey(c, effects_out, &return_now)) {
		E.input_vim_count = 0;
		return return_now;
	}
	E.input_vim_count = 0;
	return 0;
}

/* Move the cursor to the top-left cell of the current column-selection rect,
 * where the cursor lands after a block operator (matching Vim). */
static void vimSystemBlockCursorToTopLeft(const struct editorColumnSelectionRect *rect,
                                          int *effects_out) {
	struct editorLineView line = {0};
	int cx = 0;

	if (rect->top_cy < E.numrows && editorDocumentLineView(E.document, rect->top_cy, &line)) {
		cx = editorBytesRxToCx(line.data, line.size, rect->left_rx);
		editorLineViewRelease(&line);
	}
	(void)vimSystemSetCursor(rect->top_cy, cx, effects_out);
}

/* Collapse the block to a width-zero column at `rx` spanning the same rows and
 * enter Insert; typed characters then replicate on every row through the
 * column-selection insert path (dispatchInsertTextByte). */
static void vimSystemBlockEnterInsert(const struct editorColumnSelectionRect *rect, int rx,
                                      int *effects_out) {
	struct editorLineView line = {0};
	int cx = 0;

	if (rect->top_cy < E.numrows && editorDocumentLineView(E.document, rect->top_cy, &line)) {
		cx = editorBytesRxToCx(line.data, line.size, rx);
		editorLineViewRelease(&line);
	}
	(void)vimSystemSetCursor(rect->top_cy, cx, effects_out);
	E.column_select_active = 1;
	E.column_select_anchor_cy = rect->bottom_cy;
	E.column_select_anchor_rx = rx;
	E.column_select_cursor_rx = rx;
	editorHistoryBreakGroup();
	vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
}

static int vimSystemBlockInsertStart(int c, int *effects_out) {
	struct editorColumnSelectionRect rect;

	if (!editorColumnSelectionGetRect(&rect)) {
		vimSystemExitVisualBlock();
		return 0;
	}
	if (vimSystemRejectReadOnlyMutation()) {
		vimSystemExitVisualBlock();
		return 0;
	}
	vimSystemBlockEnterInsert(&rect, c == 'A' ? rect.right_rx : rect.left_rx, effects_out);
	return 0;
}

static int vimSystemBlockOperate(int c, int *effects_out) {
	struct editorColumnSelectionRect rect;

	if (!editorColumnSelectionGetRect(&rect)) {
		vimSystemExitVisualBlock();
		return 0;
	}
	if (c == 'y') {
		char *text = NULL;
		size_t len = 0;
		if (editorColumnSelectionExtractText(&text, &len) > 0) {
			if (vimSystemRegisterStore(text, len, 0)) {
				editorSetStatusMsg("Copied %zu bytes", len);
			}
			free(text);
		}
		vimSystemBlockCursorToTopLeft(&rect, effects_out);
		vimSystemExitVisualBlock();
		return 0;
	}
	if (vimSystemRejectReadOnlyMutation()) {
		vimSystemExitVisualBlock();
		return 0;
	}
	/* d / x / c all remove the block; c then enters Insert at the top-left. */
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	int deleted = editorColumnSelectionDelete();
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	if (deleted > 0) {
		vimSystemAddEditEffect(effects_out);
	}
	if (c == 'c') {
		vimSystemBlockEnterInsert(&rect, rect.left_rx, effects_out);
	} else {
		vimSystemBlockCursorToTopLeft(&rect, effects_out);
		vimSystemExitVisualBlock();
	}
	return 0;
}

static int vimSystemHandleVisualBlockKey(int c, int *effects_out) {
	int shared_result = 0;
	int motion_count = 0;
	int motion_result = 0;

	if (vimSystemIsControlKey(c)) {
		if (c == CTRL_KEY('c') || c == CTRL_KEY('v')) {
			vimSystemExitVisualBlock();
			return 0;
		}
		(void)vimSystemHandleSharedControlKey(c, effects_out, &shared_result);
		if (vimSystemMode() == VIM_SYSTEM_MODE_VISUAL_BLOCK) {
			vimSystemBlockSync();
		}
		return shared_result;
	}
	if (E.input_vim_pending_register) {
		vimSystemConsumeRegisterKey(c);
		return 0;
	}
	if (vimSystemConsumeCountKey(c)) {
		return 0;
	}
	if (c == '\x1b') {
		vimSystemExitVisualBlock();
		return 0;
	}
	if (c == 'v') {
		vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL);
		return 0;
	}
	if (c == 'V') {
		vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL_LINE);
		return 0;
	}
	if (c == '"') {
		E.input_vim_pending_register = 1;
		return 0;
	}
	if (c == 'I' || c == 'A') {
		return vimSystemBlockInsertStart(c, effects_out);
	}

	motion_count = vimSystemEffectiveCount();
	motion_result = vimSystemTryMotionKey(c, motion_count, effects_out);
	if (motion_result == 2) {
		vimSystemBlockSync();
		E.input_vim_count = 0;
		return 0;
	}
	if (motion_result == 1) {
		return 0;
	}
	if (c == 'd' || c == 'x' || c == 'c' || c == 'y') {
		return vimSystemBlockOperate(c, effects_out);
	}
	E.input_vim_count = 0;
	return 0;
}

static int vimSystemDispatchByMode(int c, int *effects_out) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return vimSystemHandleInsertKey(c, effects_out);
		case VIM_SYSTEM_MODE_VISUAL:
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return vimSystemHandleVisualKey(c, effects_out);
		case VIM_SYSTEM_MODE_VISUAL_BLOCK:
			return vimSystemHandleVisualBlockKey(c, effects_out);
		default:
			return vimSystemHandleNormalKey(c, effects_out);
	}
}

/* Dot-repeat: keys of the last buffer-changing command are recorded from one
 * idle-Normal boundary to the next and replayed verbatim by `.`. */
enum { VIM_DOT_BUF_MAX = 1024 };

static char g_vim_dot_record[VIM_DOT_BUF_MAX];
static int g_vim_dot_record_len;
static int g_vim_dot_record_dirty;
static int g_vim_dot_record_overflow;
static int g_vim_dot_record_suppress;
static char g_vim_dot_command[VIM_DOT_BUF_MAX];
static int g_vim_dot_command_len;
static int g_vim_dot_replaying;

static void vimSystemDotRepeatReset(void) {
	g_vim_dot_record_len = 0;
	g_vim_dot_record_overflow = 0;
	g_vim_dot_record_suppress = 0;
	g_vim_dot_command_len = 0;
	g_vim_dot_replaying = 0;
}

/* Mark the in-flight key sequence as not a repeatable change so `.` ignores it
 * even though it altered the buffer (undo/redo are not Vim "changes"). */
static void vimSystemDotRepeatSuppress(void) {
	g_vim_dot_record_suppress = 1;
}

static int vimSystemIsIdleNormal(void) {
	return vimSystemMode() == VIM_SYSTEM_MODE_NORMAL && E.input_vim_pending_operator == 0 &&
	       E.input_vim_pending_g == 0 && E.input_vim_pending_find == 0 &&
	       E.input_vim_pending_replace == 0 && E.input_vim_pending_z == 0 &&
	       E.input_vim_pending_mark == 0 && E.input_vim_pending_register == 0 &&
	       E.input_vim_pending_text_object == 0 && E.input_vim_pending_leader == 0 &&
	       E.input_vim_pending_ctrl_w == 0 && E.input_vim_pending_bracket == 0 &&
	       E.input_vim_count == 0;
}

static int vimSystemKeySequencePending(void) {
	return E.input_vim_pending_operator != VIM_SYSTEM_OPERATOR_NONE ||
	       E.input_vim_pending_g != 0 || E.input_vim_pending_find != 0 ||
	       E.input_vim_pending_replace != 0 || E.input_vim_pending_z != 0 ||
	       E.input_vim_pending_mark != 0 || E.input_vim_pending_register != 0 ||
	       E.input_vim_pending_text_object != 0 || E.input_vim_pending_leader != 0 ||
	       E.input_vim_pending_ctrl_w != 0 || E.input_vim_pending_bracket != 0;
}

static int vimSystemDotReplay(int *effects_out) {
	int last = 0;

	if (g_vim_dot_command_len <= 0 || g_vim_dot_replaying) {
		return 0;
	}
	g_vim_dot_replaying = 1;
	for (int i = 0; i < g_vim_dot_command_len; i++) {
		last = vimSystemDispatchByMode((unsigned char)g_vim_dot_command[i], effects_out);
	}
	g_vim_dot_replaying = 0;
	return last;
}

static int vimSystemHandleKey(int c, int *effects_out) {
	int result;

	if (g_vim_dot_replaying) {
		return vimSystemDispatchByMode(c, effects_out);
	}
	if (c == '.' && vimSystemIsIdleNormal()) {
		return vimSystemDotReplay(effects_out);
	}
	if (vimSystemIsIdleNormal()) {
		g_vim_dot_record_len = 0;
		g_vim_dot_record_overflow = 0;
		g_vim_dot_record_suppress = 0;
		g_vim_dot_record_dirty = E.dirty;
	}
	if (g_vim_dot_record_len < VIM_DOT_BUF_MAX) {
		g_vim_dot_record[g_vim_dot_record_len++] = (char)c;
	} else {
		g_vim_dot_record_overflow = 1;
	}

	result = vimSystemDispatchByMode(c, effects_out);

	if (vimSystemIsIdleNormal() && g_vim_dot_record_len > 0) {
		if (!g_vim_dot_record_overflow && !g_vim_dot_record_suppress &&
		    E.dirty != g_vim_dot_record_dirty) {
			memcpy(g_vim_dot_command, g_vim_dot_record, (size_t)g_vim_dot_record_len);
			g_vim_dot_command_len = g_vim_dot_record_len;
		}
		g_vim_dot_record_len = 0;
		g_vim_dot_record_overflow = 0;
	}
	return result;
}

static int vimSystemResolveCommand(const char *name, int *command_id_out) {
	if (name == NULL) {
		return 0;
	}
	for (size_t i = 0; i < g_vim_command_count; i++) {
		if (strcmp(g_vim_commands[i].name, name) == 0) {
			if (command_id_out != NULL) {
				*command_id_out = g_vim_commands[i].canonical_key;
			}
			return 1;
		}
	}
	return 0;
}

static int vimSystemModeFromName(const char *mode, enum vimSystemMode *mode_out) {
	if (mode == NULL) {
		return 0;
	}
	if (strcmp(mode, "normal") == 0) {
		*mode_out = VIM_SYSTEM_MODE_NORMAL;
		return 1;
	}
	if (strcmp(mode, "insert") == 0) {
		*mode_out = VIM_SYSTEM_MODE_INSERT;
		return 1;
	}
	if (strcmp(mode, "visual") == 0) {
		*mode_out = VIM_SYSTEM_MODE_VISUAL;
		return 1;
	}
	return 0;
}

static int vimSystemCommandIsStructural(enum vimSystemMode mode, const char *name) {
	if (mode != VIM_SYSTEM_MODE_NORMAL || name == NULL) {
		return 0;
	}
	return strcmp(name, "search_forward") == 0 || strcmp(name, "search_backward") == 0 ||
	       strcmp(name, "search_next") == 0 || strcmp(name, "search_prev") == 0 ||
	       strcmp(name, "ex_command") == 0;
}

static int vimSystemKeyIsStructural(enum vimSystemMode mode, int key) {
	if (mode == VIM_SYSTEM_MODE_INSERT) {
		return 0;
	}
	if (key >= '0' && key <= '9') {
		return 1;
	}
	if (key == '"') {
		return 1;
	}
	if (mode == VIM_SYSTEM_MODE_NORMAL) {
		return key == '/' || key == '?' || key == ':' || key == 'n' || key == 'N' ||
		       key == 'g';
	}
	return key == 'i' || key == 'a' || key == 'g';
}

/* Leader key and sub-keys must be plain printable characters: control keys
 * (Esc cancels a pending leader) and the leader key itself are rejected. */
static int vimSystemLeaderKeyIsBindable(int key) {
	return key >= 0x20 && key <= 0x7e;
}

static int vimSystemSetLeaderKey(int key) {
	if (!vimSystemLeaderKeyIsBindable(key)) {
		return 0;
	}
	g_vim_leader_key = key;
	return 1;
}

static int vimSystemBindLeaderAction(const char *name, int key) {
	struct vimLeaderBinding *target = NULL;

	if (name == NULL || !vimSystemLeaderKeyIsBindable(key)) {
		return 0;
	}
	for (size_t i = 0; i < g_vim_leader_count; i++) {
		if (strcmp(g_vim_leader_map[i].name, name) == 0) {
			target = &g_vim_leader_map[i];
			break;
		}
	}
	if (target == NULL) {
		return 0;
	}
	for (size_t i = 0; i < g_vim_leader_count; i++) {
		if (&g_vim_leader_map[i] != target && g_vim_leader_map[i].bound_key == key) {
			g_vim_leader_map[i].bound_key = -1;
		}
	}
	target->bound_key = key;
	return 1;
}

static int vimSystemBindKey(const char *mode, const char *name, int key) {
	enum vimSystemMode target_mode = VIM_SYSTEM_MODE_NORMAL;
	struct vimBindableCommand *target = NULL;

	if (mode == NULL || name == NULL) {
		return 0;
	}
	if (strcmp(mode, "leader") == 0) {
		return vimSystemBindLeaderAction(name, key);
	}
	if (strcmp(mode, "normal") == 0 && strcmp(name, "leader") == 0) {
		return vimSystemSetLeaderKey(key);
	}
	if (!vimSystemModeFromName(mode, &target_mode)) {
		return 0;
	}
	if (vimSystemCommandIsStructural(target_mode, name) ||
	    vimSystemKeyIsStructural(target_mode, key)) {
		return 0;
	}
	for (size_t i = 0; i < g_vim_command_count; i++) {
		if (g_vim_commands[i].mode == target_mode &&
		    strcmp(g_vim_commands[i].name, name) == 0) {
			target = &g_vim_commands[i];
			break;
		}
	}
	if (target == NULL) {
		return 0;
	}
	/* A key triggers one command per mode: release it from any other command. */
	for (size_t i = 0; i < g_vim_command_count; i++) {
		if (&g_vim_commands[i] != target && g_vim_commands[i].mode == target_mode &&
		    g_vim_commands[i].bound_key == key) {
			g_vim_commands[i].bound_key = -1;
		}
	}
	target->bound_key = key;
	return 1;
}

static void vimSystemStatusSegment(char *buf, size_t bufsize) {
	if (bufsize != 0) {
		(void)snprintf(buf, bufsize, "%s", editorVimModeLabel());
	}
}

void editorVimInitialize(void) {
	editorVimKeymapResetDefaults();
	vimSystemDotRepeatReset();
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
}

void editorVimReset(void) {
	vimSystemDotRepeatReset();
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
}

int editorVimHandleKey(int c, int *effects_out) {
	return vimSystemHandleKey(c, effects_out);
}

int editorVimOpenExCommandLine(int *effects_out) {
	return vimSystemExCommandLine(effects_out);
}

int editorVimKeySequencePending(void) {
	return vimSystemKeySequencePending();
}

int editorVimResolveCommand(const char *name, int *command_id_out) {
	return vimSystemResolveCommand(name, command_id_out);
}

int editorVimBindKey(const char *mode, const char *name, int key) {
	return vimSystemBindKey(mode, name, key);
}

void editorVimStatusSegment(char *buf, size_t bufsize) {
	vimSystemStatusSegment(buf, bufsize);
}

int editorVimStatusColor(void) {
	return vimSystemStatusColor();
}

int editorVimCursorStyle(void) {
	return vimSystemCursorStyle();
}
