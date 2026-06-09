#include "config/keymap.h"
#include "config/theme_config.h"
#include "editing/buffer_core.h"
#include "editing/buffer_search.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "input/actions_file_tab.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/prompt.h"
#include "input/text_pairs.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/alloc.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum vimSystemMode {
	VIM_SYSTEM_MODE_NORMAL = 0,
	VIM_SYSTEM_MODE_INSERT,
	VIM_SYSTEM_MODE_VISUAL,
	VIM_SYSTEM_MODE_VISUAL_LINE
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
	VIM_SYSTEM_MOTION_LAST_LINE
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
	VIM_SYSTEM_OPERATOR_YANK = 'y'
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

static const size_t g_vim_command_count = sizeof(g_vim_commands) / sizeof(g_vim_commands[0]);

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
        {"toggle_drawer", EDITOR_ACTION_TOGGLE_DRAWER, 'e', 'e'},
        {"main_menu", EDITOR_ACTION_MAIN_MENU, 'm', 'm'},
};

static const size_t g_vim_leader_count = sizeof(g_vim_leader_map) / sizeof(g_vim_leader_map[0]);

static int g_vim_leader_key = VIM_SYSTEM_LEADER_DEFAULT;

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
			return EDITOR_THEME_ANSI_MAGENTA;
		default:
			return EDITOR_THEME_ANSI_BLUE;
	}
}

static void vimSystemBeginVisual(enum vimSystemMode mode) {
	size_t cursor_offset = E.cursor_offset;
	(void)editorBufferPosToOffset(E.cy, E.cx, &cursor_offset);
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = cursor_offset;
	vimSystemSetMode(mode);
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
	}

	*cy_out = cy;
	*cx_out = cx;
	return 1;
}

static int vimSystemMotionIsLinewise(enum vimSystemMotion motion) {
	return motion == VIM_SYSTEM_MOTION_DOWN || motion == VIM_SYSTEM_MOTION_UP ||
	       motion == VIM_SYSTEM_MOTION_FIRST_LINE || motion == VIM_SYSTEM_MOTION_LAST_LINE;
}

static int vimSystemMotionIsInclusive(enum vimSystemMotion motion) {
	return motion == VIM_SYSTEM_MOTION_WORD_END || motion == VIM_SYSTEM_MOTION_LINE_END;
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

static int vimSystemApplyMotion(enum vimSystemMotion motion, int count, int *effects_out) {
	int cy = E.cy;
	int cx = E.cx;

	if (!vimSystemMotionTargetCounted(motion, count, &cy, &cx)) {
		return 0;
	}
	(void)vimSystemSetCursor(cy, cx, effects_out);
	return 1;
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
			motion = VIM_SYSTEM_MOTION_LEFT;
			break;
		case 'j':
			motion = VIM_SYSTEM_MOTION_DOWN;
			break;
		case 'k':
			motion = VIM_SYSTEM_MOTION_UP;
			break;
		case 'l':
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
		case 'g':
			if (pending_g != NULL) {
				*pending_g = 1;
			}
			return VIM_SYSTEM_MOTION_PARSE_PENDING;
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

static int vimSystemYankRange(const struct editorSelectionRange *range, int linewise) {
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
	free(text);
	return 1;
}

static int vimSystemYankLines(int start_cy, int end_cy) {
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
	if (!vimSystemYankLines(start_cy, end_cy)) {
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

static int vimSystemApplyOperatorToRange(enum vimSystemOperator op,
                                         const struct editorSelectionRange *range, int linewise,
                                         int *effects_out) {
	int dirty_before = E.dirty;
	int changed = 0;

	if (op == VIM_SYSTEM_OPERATOR_YANK) {
		if (linewise) {
			return vimSystemYankLines(range->start_cy,
			                          vimSystemLineRangeLastRow(range)) > 0;
		}
		return vimSystemYankRange(range, 0) > 0;
	}
	if (vimSystemRejectReadOnlyMutation()) {
		return 0;
	}
	if (linewise && !vimSystemYankLines(range->start_cy, vimSystemLineRangeLastRow(range))) {
		return 0;
	}
	if (!linewise && !vimSystemYankRange(range, 0)) {
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
	if (op == VIM_SYSTEM_OPERATOR_CHANGE) {
		return vimSystemChangeLineRange(E.cy, end_cy, effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_YANK) {
		return vimSystemYankLines(E.cy, end_cy) > 0;
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

static int vimSystemTextObjectRange(int inner, int object_key,
                                    struct editorSelectionRange *range_out, int *linewise_out) {
	if (linewise_out != NULL) {
		*linewise_out = 0;
	}
	if (object_key == 'w') {
		return vimSystemWordObjectRange(inner, range_out);
	}
	if (object_key == 'p') {
		if (linewise_out != NULL) {
			*linewise_out = 1;
		}
		return vimSystemParagraphObjectRange(inner, range_out);
	}
	return 0;
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

static int vimSystemTryMappedActionKey(int c, int *effects_out, int *return_now_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (return_now_out != NULL) {
		*return_now_out = 0;
	}
	if (editorKeymapLookupAction(&E.keymap, c, &action)) {
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

static int vimSystemEnterInsertWithAction(enum editorAction action, int *effects_out) {
	int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
	(void)editorDispatchProcessMappedAction(action, &mapped_effects);
	if (effects_out != NULL) {
		*effects_out |= mapped_effects;
	}
	vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
	return 0;
}

static int vimSystemHandleInsertKey(int c, int *effects_out) {
	c = vimSystemRemapKey(VIM_SYSTEM_MODE_INSERT, c, NULL);
	if (c == '\x1b') {
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	int return_now = 0;
	if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
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

static void vimSystemRunExCommand(char *line, int *effects_out) {
	char *cmd = line;
	size_t len = 0;

	while (*cmd == ' ' || *cmd == '\t' || *cmd == ':') {
		cmd++;
	}
	len = strlen(cmd);
	while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) {
		cmd[--len] = '\0';
	}
	if (*cmd == '\0') {
		return;
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
	editorSetStatusMsg("Not an editor command: %s", cmd);
}

static int vimSystemExCommandLine(int *effects_out) {
	char *line = editorPromptWithCallback(":%s", 1, NULL);

	if (line == NULL) {
		return 0;
	}
	vimSystemRunExCommand(line, effects_out);
	free(line);
	return 1;
}

static int vimSystemHandleNormalKey(int c, int *effects_out) {
	int motion_count = 0;
	int motion_result = 0;
	int disabled = 0;

	if (E.input_vim_pending_register) {
		E.input_vim_pending_register = 0;
		if (c >= 'a' && c <= 'z') {
			E.input_vim_active_register = c;
		} else if (c >= 'A' && c <= 'Z') {
			E.input_vim_active_register = c - 'A' + 'a';
		}
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
	if (c == '"') {
		E.input_vim_pending_register = 1;
		return 0;
	}
	if (c == g_vim_leader_key) {
		vimSystemResetPending();
		E.input_vim_pending_leader = 1;
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
			(void)vimSystemYankLines(E.cy, E.cy);
			vimSystemResetPending();
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
			if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
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
		E.input_vim_visual_selection_half_open = 0;
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
	E.input_vim_visual_selection_half_open = 1;
	(void)vimSystemSetCursor(cursor_cy, cursor_cx, effects_out);
	return 1;
}

static int vimSystemHandleVisualKey(int c, int *effects_out) {
	int motion_count = 0;
	int motion_result = 0;
	int disabled = 0;

	if (E.input_vim_pending_register) {
		E.input_vim_pending_register = 0;
		if (c >= 'a' && c <= 'z') {
			E.input_vim_active_register = c;
		} else if (c >= 'A' && c <= 'Z') {
			E.input_vim_active_register = c - 'A' + 'a';
		}
		return 0;
	}
	if (E.input_vim_pending_text_object) {
		int inner = E.input_vim_pending_text_object == 'i';
		E.input_vim_pending_text_object = 0;
		(void)vimSystemVisualSelectObject(inner, c, effects_out);
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

	motion_count = vimSystemEffectiveCount();
	motion_result = vimSystemTryMotionKey(c, motion_count, effects_out);
	if (motion_result == 2) {
		E.input_vim_visual_selection_half_open = 0;
		E.input_vim_count = 0;
		return 0;
	}
	if (motion_result == 1) {
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
	if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
		E.input_vim_count = 0;
		return return_now;
	}
	E.input_vim_count = 0;
	return 0;
}

static int vimSystemHandleKey(int c, int *effects_out) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return vimSystemHandleInsertKey(c, effects_out);
		case VIM_SYSTEM_MODE_VISUAL:
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return vimSystemHandleVisualKey(c, effects_out);
		default:
			return vimSystemHandleNormalKey(c, effects_out);
	}
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

static int vimSystemOnActivate(void) {
	editorVimKeymapResetDefaults();
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
	return 1;
}

static void vimSystemReset(void) {
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
}

const struct editorInputSystem editorVimInputSystem = {
        .id = "vim",
        .on_activate = vimSystemOnActivate,
        .on_deactivate = NULL,
        .handle_key = vimSystemHandleKey,
        .resolve_command = vimSystemResolveCommand,
        .bind_key = vimSystemBindKey,
        .status_segment = vimSystemStatusSegment,
        .status_segment_color = vimSystemStatusColor,
        .reset = vimSystemReset,
};
