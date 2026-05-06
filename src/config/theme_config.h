#ifndef CONFIG_THEME_CONFIG_H
#define CONFIG_THEME_CONFIG_H

#include "rotide.h"

enum editorThemeLoadStatus {
	EDITOR_THEME_LOAD_OK = 0,
	EDITOR_THEME_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_THEME_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_THEME_LOAD_INVALID_THEME = 1 << 2,
	EDITOR_THEME_LOAD_OUT_OF_MEMORY = 1 << 3
};

struct editorThemeColor editorThemeDefaultColor(void);
struct editorThemeColor editorThemeAnsiColor(enum editorThemeAnsiColor color);
struct editorThemeColor editorTheme256Color(unsigned char color);
struct editorThemeColor editorThemeRgbColor(unsigned char r, unsigned char g, unsigned char b);

void editorThemeInitDefault(struct editorTheme *theme_out);
int editorThemeInitBuiltin(struct editorTheme *theme_out, const char *name);
enum editorThemeLoadStatus editorThemeLoadFromPaths(struct editorTheme *theme_out,
		const char *global_path, const char *project_path, const char *home_dir);
enum editorThemeLoadStatus editorThemeLoadConfigured(struct editorTheme *theme_out);

#endif
