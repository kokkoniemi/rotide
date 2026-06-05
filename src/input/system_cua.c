#include "config/keymap.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/text_pairs.h"
#include "rotide.h"

#include <string.h>

static int cuaSystemHandleKey(int c, int *effects_out) {
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

static int cuaSystemResolveCommand(const char *name, int *command_id_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!editorKeymapResolveActionName(name, &action)) {
		return 0;
	}
	if (command_id_out != NULL) {
		*command_id_out = action;
	}
	return 1;
}

static int cuaSystemBindKey(const char *mode, const char *name, int key) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (mode != NULL && mode[0] != '\0' && strcmp(mode, "default") != 0) {
		return 0;
	}
	if (!editorKeymapResolveActionName(name, &action)) {
		return 0;
	}
	return editorKeymapBindAction(&E.keymap, action, key);
}

static void cuaSystemStatusSegment(char *buf, size_t bufsize) {
	if (bufsize != 0) {
		buf[0] = '\0';
	}
}

static void cuaSystemReset(void) {}

const struct editorInputSystem editorCuaInputSystem = {
        .id = "cua",
        .on_activate = NULL,
        .on_deactivate = NULL,
        .handle_key = cuaSystemHandleKey,
        .resolve_command = cuaSystemResolveCommand,
        .bind_key = cuaSystemBindKey,
        .status_segment = cuaSystemStatusSegment,
        .reset = cuaSystemReset,
};
