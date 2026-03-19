#ifndef KEYMAP_H
#define KEYMAP_H

#include "rotide.h"

enum editorKeymapLoadStatus {
	EDITOR_KEYMAP_LOAD_OK = 0,
	EDITOR_KEYMAP_LOAD_INVALID_GLOBAL,
	EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY
};

enum editorCursorStyleLoadStatus {
	EDITOR_CURSOR_STYLE_LOAD_OK = 0,
	EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY = 1 << 2
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
enum editorCursorStyleLoadStatus editorCursorStyleLoadFromPaths(enum editorCursorStyle *style_out,
		const char *global_path, const char *project_path);
enum editorCursorStyleLoadStatus editorCursorStyleLoadConfigured(enum editorCursorStyle *style_out);

#endif
