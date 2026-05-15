#ifndef CONFIG_THEME_CONFIG_H
#define CONFIG_THEME_CONFIG_H

#include "language/syntax.h"

enum editorThemeColorKind {
	EDITOR_THEME_COLOR_DEFAULT = 0,
	EDITOR_THEME_COLOR_ANSI,
	EDITOR_THEME_COLOR_256,
	EDITOR_THEME_COLOR_RGB
};

enum editorThemeAnsiColor {
	EDITOR_THEME_ANSI_BLACK = 0,
	EDITOR_THEME_ANSI_RED,
	EDITOR_THEME_ANSI_GREEN,
	EDITOR_THEME_ANSI_YELLOW,
	EDITOR_THEME_ANSI_BLUE,
	EDITOR_THEME_ANSI_MAGENTA,
	EDITOR_THEME_ANSI_CYAN,
	EDITOR_THEME_ANSI_WHITE,
	EDITOR_THEME_ANSI_BRIGHT_BLACK,
	EDITOR_THEME_ANSI_BRIGHT_RED,
	EDITOR_THEME_ANSI_BRIGHT_GREEN,
	EDITOR_THEME_ANSI_BRIGHT_YELLOW,
	EDITOR_THEME_ANSI_BRIGHT_BLUE,
	EDITOR_THEME_ANSI_BRIGHT_MAGENTA,
	EDITOR_THEME_ANSI_BRIGHT_CYAN,
	EDITOR_THEME_ANSI_BRIGHT_WHITE,
	EDITOR_THEME_ANSI_COUNT
};

struct editorThemeColor {
	enum editorThemeColorKind kind;
	unsigned char value;
	unsigned char r;
	unsigned char g;
	unsigned char b;
};

struct editorThemeStyle {
	struct editorThemeColor fg;
	struct editorThemeColor bg;
	int reverse;
};

enum editorThemeUiRole {
	EDITOR_THEME_UI_FOREGROUND = 0,
	EDITOR_THEME_UI_BACKGROUND,
	EDITOR_THEME_UI_LINE_NUMBER,
	EDITOR_THEME_UI_DRAWER_CONNECTOR,
	EDITOR_THEME_UI_DRAWER_ICON,
	EDITOR_THEME_UI_PLACEHOLDER,
	EDITOR_THEME_UI_CURRENT_LINE_BG,
	EDITOR_THEME_UI_DRAWER_HEADER_BG,
	EDITOR_THEME_UI_DIRECTORY,
	EDITOR_THEME_UI_ROOT,
	EDITOR_THEME_UI_GIT_MODIFIED,
	EDITOR_THEME_UI_GIT_UNTRACKED,
	EDITOR_THEME_UI_GIT_CONFLICT,
	EDITOR_THEME_UI_CURSOR,
	EDITOR_THEME_UI_ROLE_COUNT
};

enum editorThemeStyleRole {
	EDITOR_THEME_STYLE_SELECTION = 0,
	EDITOR_THEME_STYLE_STATUS,
	EDITOR_THEME_STYLE_TAB_ACTIVE,
	EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE,
	EDITOR_THEME_STYLE_ROLE_COUNT
};

struct editorTheme {
	char name[64];
	struct editorThemeColor syntax[EDITOR_SYNTAX_HL_CLASS_COUNT];
	struct editorThemeColor ui[EDITOR_THEME_UI_ROLE_COUNT];
	struct editorThemeStyle styles[EDITOR_THEME_STYLE_ROLE_COUNT];
};

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
