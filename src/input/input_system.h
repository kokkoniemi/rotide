#ifndef ROTIDE_INPUT_INPUT_SYSTEM_H
#define ROTIDE_INPUT_INPUT_SYSTEM_H

#include <stddef.h>

enum editorInputKeyEffect {
	EDITOR_INPUT_KEY_EFFECT_NONE = 0,
	EDITOR_INPUT_KEY_EFFECT_VIEWPORT_SCROLL = 1 << 0,
	EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT = 1 << 1
};

struct editorInputSystem {
	const char *id;
	int (*on_activate)(void);
	void (*on_deactivate)(void);
	int (*handle_key)(int c, int *effects_out);
	int (*resolve_command)(const char *name, int *command_id_out);
	int (*bind_key)(const char *mode, const char *name, int key);
	void (*status_segment)(char *buf, size_t bufsize);
	void (*reset)(void);
};

extern const struct editorInputSystem editorCuaInputSystem;
extern const struct editorInputSystem editorVimInputSystem;

int editorInputSystemActivate(const char *id);
const struct editorInputSystem *editorInputSystemActive(void);
const struct editorInputSystem *editorInputSystemById(const char *id);

#endif
