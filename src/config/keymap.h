#ifndef ROTIDE_CONFIG_KEYMAP_H
#define ROTIDE_CONFIG_KEYMAP_H

#include "rotide.h"

enum editorKeymapLoadStatus {
	EDITOR_KEYMAP_LOAD_OK = 0,
	EDITOR_KEYMAP_LOAD_INVALID_GLOBAL,
	EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY
};

void editorKeymapInitDefaults(struct editorKeymap *keymap);
int editorKeymapResolveActionName(const char *name, enum editorAction *action_out);
int editorKeymapBindAction(struct editorKeymap *keymap, enum editorAction action, int key);
int editorKeymapLookupAction(const struct editorKeymap *keymap, int key,
                             enum editorAction *action_out);
int editorKeymapFormatBinding(const struct editorKeymap *keymap, enum editorAction action,
                              char *buf, size_t bufsize);
void editorKeymapBuildHelpStatus(const struct editorKeymap *keymap, char *buf, size_t bufsize);

enum editorKeymapLoadStatus editorKeymapLoadFromPaths(struct editorKeymap *keymap,
                                                      const char *global_path,
                                                      const char *project_path);
enum editorKeymapLoadStatus editorKeymapLoadConfigured(struct editorKeymap *keymap);

/* Apply `[keymap.vim]` mode-qualified bindings to the active input system when
 * it is Vim. Resets Vim bindings to defaults first, then global then project. */
enum editorKeymapLoadStatus editorKeymapLoadVimBindings(const char *global_path,
                                                        const char *project_path);
enum editorKeymapLoadStatus editorKeymapLoadVimBindingsConfigured(void);

#endif
