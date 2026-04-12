#ifndef CONFIG_EDITOR_CONFIG_H
#define CONFIG_EDITOR_CONFIG_H

#include "rotide.h"

enum editorCursorStyleLoadStatus {
	EDITOR_CURSOR_STYLE_LOAD_OK = 0,
	EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorCursorStyleLoadStatus editorCursorStyleLoadFromPaths(enum editorCursorStyle *style_out,
		const char *global_path, const char *project_path);
enum editorCursorStyleLoadStatus editorCursorStyleLoadConfigured(enum editorCursorStyle *style_out);

#endif
