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

enum editorSyntaxThemeLoadStatus {
	EDITOR_SYNTAX_THEME_LOAD_OK = 0,
	EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorLspConfigLoadStatus {
	EDITOR_LSP_CONFIG_LOAD_OK = 0,
	EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY = 1 << 2
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
void editorSyntaxThemeInitDefaults(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]);
enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadFromPaths(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT],
		const char *global_path, const char *project_path);
enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadConfigured(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]);
void editorLspConfigInitDefaults(int *enabled_out, char *command_out, size_t command_out_size,
		char *install_command_out, size_t install_command_out_size);
enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(int *enabled_out,
		char *command_out, size_t command_out_size, char *install_command_out,
		size_t install_command_out_size, const char *global_path, const char *project_path);
enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(int *enabled_out,
		char *command_out, size_t command_out_size, char *install_command_out,
		size_t install_command_out_size);

#endif
