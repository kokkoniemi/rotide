#include "config/keymap.h"
#include "editing/document_position.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/text_pairs.h"
#include "rotide.h"

#include <stdio.h>
#include <string.h>

enum vimSystemMode {
	VIM_SYSTEM_MODE_NORMAL = 0,
	VIM_SYSTEM_MODE_INSERT,
	VIM_SYSTEM_MODE_VISUAL,
	VIM_SYSTEM_MODE_VISUAL_LINE
};

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
