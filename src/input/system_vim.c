#include "config/keymap.h"
#include "editing/document_position.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/text_pairs.h"
#include "rotide.h"
#include "text/document.h"
#include "text/row.h"

#include <ctype.h>
#include <stdio.h>
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
	VIM_SYSTEM_MOTION_LINE_END,
	VIM_SYSTEM_MOTION_FIRST_NONBLANK,
	VIM_SYSTEM_MOTION_FIRST_LINE,
	VIM_SYSTEM_MOTION_LAST_LINE
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

static void vimSystemSetMode(enum vimSystemMode mode) {
	E.input_vim_mode = mode;
	E.input_vim_pending_g = 0;
}

const char *editorVimModeLabel(void) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return "-- INSERT --";
		case VIM_SYSTEM_MODE_VISUAL:
			return "-- VISUAL --";
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return "-- VISUAL LINE --";
		default:
			return "-- NORMAL --";
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

static int vimSystemApplyMotion(enum vimSystemMotion motion, int *effects_out) {
	int cy = E.cy;
	int cx = E.cx;

	if (!vimSystemMotionTarget(motion, &cy, &cx)) {
		return 0;
	}
	(void)vimSystemSetCursor(cy, cx, effects_out);
	return 1;
}

static int vimSystemTryMotionKey(int c, int *effects_out) {
	enum vimSystemMotion motion;

	if (E.input_vim_pending_g) {
		E.input_vim_pending_g = 0;
		if (c == 'g') {
			return vimSystemApplyMotion(VIM_SYSTEM_MOTION_FIRST_LINE, effects_out);
		}
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
		case '0': {
			int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
			enum editorAction action = vimSystemMode() == VIM_SYSTEM_MODE_NORMAL
			                                   ? EDITOR_ACTION_MOVE_HOME
			                                   : EDITOR_ACTION_SELECT_HOME;
			(void)editorDispatchProcessMappedAction(action, &mapped_effects);
			if (effects_out != NULL) {
				*effects_out |= mapped_effects;
			}
			return 1;
		}
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
			E.input_vim_pending_g = 1;
			return 1;
		default:
			return 0;
	}
	return vimSystemApplyMotion(motion, effects_out);
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

static int vimSystemHandleNormalKey(int c, int *effects_out) {
	if (vimSystemTryMotionKey(c, effects_out)) {
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
		case 'v':
			vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL);
			return 0;
		case 'V':
			vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL_LINE);
			return 0;
		default:
			int return_now = 0;
			if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
				return return_now;
			}
			return 0;
	}
}

static int vimSystemHandleVisualKey(int c, int *effects_out) {
	if (c == '\x1b') {
		E.selection_mode_active = 0;
		E.selection_anchor_offset = 0;
		editorColumnSelectionClear();
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
	if (vimSystemTryMotionKey(c, effects_out)) {
		return 0;
	}
	int return_now = 0;
	if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
		return return_now;
	}
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
	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!editorKeymapResolveActionName(name, &action)) {
		return 0;
	}
	if (command_id_out != NULL) {
		*command_id_out = action;
	}
	return 1;
}

static int vimSystemBindKey(const char *mode, const char *name, int key) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (mode != NULL && mode[0] != '\0' && strcmp(mode, "default") != 0) {
		return 0;
	}
	if (!editorKeymapResolveActionName(name, &action)) {
		return 0;
	}
	return editorKeymapBindAction(&E.keymap, action, key);
}

static void vimSystemStatusSegment(char *buf, size_t bufsize) {
	if (bufsize != 0) {
		(void)snprintf(buf, bufsize, "%s", editorVimModeLabel());
	}
}

static int vimSystemOnActivate(void) {
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
        .reset = vimSystemReset,
};
