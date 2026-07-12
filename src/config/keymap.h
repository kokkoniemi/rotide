#ifndef ROTIDE_CONFIG_KEYMAP_H
#define ROTIDE_CONFIG_KEYMAP_H

#include "rotide.h"

enum editorKeymapLoadStatus {
	EDITOR_KEYMAP_LOAD_OK = 0,
	EDITOR_KEYMAP_LOAD_INVALID_GLOBAL,
	EDITOR_KEYMAP_LOAD_INVALID_PROJECT
};

/* Apply `[keymap.vim]` mode-qualified bindings, then global then project. */
enum editorKeymapLoadStatus editorKeymapLoadVimBindings(const char *global_path,
                                                        const char *project_path);
enum editorKeymapLoadStatus editorKeymapLoadVimBindingsConfigured(void);

#endif
