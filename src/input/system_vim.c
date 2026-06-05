#include "config/keymap.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/text_pairs.h"
#include "rotide.h"

#include <stdio.h>
#include <string.h>

static int vimSystemHandleKey(int c, int *effects_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (editorKeymapLookupAction(&E.keymap, c, &action)) {
		int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
		if (editorDispatchProcessMappedAction(action, &mapped_effects)) {
			return 1;
		}
		if (effects_out != NULL) {
			*effects_out |= mapped_effects;
		}
		return 0;
	}
	if (editorByteShouldInsertAsText(c)) {
		editorDispatchHandleTextByte(c, effects_out);
	}
	return 0;
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
		(void)snprintf(buf, bufsize, "VIM");
	}
}

static void vimSystemReset(void) {}

const struct editorInputSystem editorVimInputSystem = {
        .id = "vim",
        .on_activate = NULL,
        .on_deactivate = NULL,
        .handle_key = vimSystemHandleKey,
        .resolve_command = vimSystemResolveCommand,
        .bind_key = vimSystemBindKey,
        .status_segment = vimSystemStatusSegment,
        .reset = vimSystemReset,
};
