#ifndef CONFIG_THEME_CONFIG_H
#define CONFIG_THEME_CONFIG_H

#include "rotide.h"

enum editorSyntaxThemeLoadStatus {
	EDITOR_SYNTAX_THEME_LOAD_OK = 0,
	EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY = 1 << 2
};

void editorSyntaxThemeInitDefaults(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]);
enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadFromPaths(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT],
		const char *global_path, const char *project_path);
enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadConfigured(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]);

#endif
