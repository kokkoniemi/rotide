#ifndef KEYMAP_H
#define KEYMAP_H

#include "rotide.h"

enum editorKeymapLoadStatus {
	EDITOR_KEYMAP_LOAD_OK = 0,
	EDITOR_KEYMAP_LOAD_INVALID_GLOBAL,
	EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY
};

void editorKeymapInitDefaults(struct editorKeymap *keymap);
int editorKeymapLookupAction(const struct editorKeymap *keymap, int key,
		enum editorAction *action_out);
int editorKeymapFormatBinding(const struct editorKeymap *keymap, enum editorAction action,
		char *buf, size_t bufsize);
void editorKeymapBuildHelpStatus(const struct editorKeymap *keymap, char *buf, size_t bufsize);

enum editorKeymapLoadStatus editorKeymapLoadFromPaths(struct editorKeymap *keymap,
		const char *global_path, const char *project_path);
enum editorKeymapLoadStatus editorKeymapLoadConfigured(struct editorKeymap *keymap);

#endif
