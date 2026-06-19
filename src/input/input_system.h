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
	/* Optional: theme ANSI palette index (0-15) for the status segment's
	 * background, or a negative value for none. Lets a system color-code its
	 * status block (e.g. Vim's mode indicator). */
	int (*status_segment_color)(void);
	/* Optional: terminal cursor shape override (an enum editorCursorStyle
	 * value) for the current mode, or a negative value to keep the configured
	 * style. Lets Vim show a block cursor in Normal/Visual (so the inclusive
	 * selection's last cell coincides with the cursor) and a bar in Insert. */
	int (*cursor_style)(void);
	void (*reset)(void);
};

extern const struct editorInputSystem editorCuaInputSystem;
extern const struct editorInputSystem editorVimInputSystem;

int editorInputSystemActivate(const char *id);
const struct editorInputSystem *editorInputSystemActive(void);
const struct editorInputSystem *editorInputSystemById(const char *id);

/* Reset the Vim per-mode key bindings to their built-in defaults. Called before
 * (re)applying `[keymap.vim]` config so reloads do not accumulate bindings. */
void editorVimKeymapResetDefaults(void);

#endif
