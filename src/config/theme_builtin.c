/* Built-in theme tables.
 *
 * Owns the editorTheme color/style helpers, the default-theme bootstrap
 * (editorThemeInitDefault), and every named built-in theme initializer
 * (terminal, a11y-dark/light, acme, silentium, 256noir, github
 * light/dark, modus, molokai, kanagawa wave/dragon/lotus, …).
 * editorThemeInitBuiltin is the public dispatcher that maps a config
 * name to the right initializer.
 *
 * Parser code for custom TOML themes lives in theme_parse.c.
 */
#include "config/theme_config.h"
#include "config/theme_internal.h"

#include <stdio.h>
#include <string.h>

struct editorThemeColor editorThemeDefaultColor(void) {
	struct editorThemeColor color = {EDITOR_THEME_COLOR_DEFAULT, 0, 0, 0, 0};
	return color;
}

struct editorThemeColor editorThemeAnsiColor(enum editorThemeAnsiColor color) {
	struct editorThemeColor theme_color = {EDITOR_THEME_COLOR_ANSI, (unsigned char)color, 0, 0,
	                                       0};
	return theme_color;
}

struct editorThemeColor editorTheme256Color(unsigned char color) {
	struct editorThemeColor theme_color = {EDITOR_THEME_COLOR_256, color, 0, 0, 0};
	return theme_color;
}

struct editorThemeColor editorThemeRgbColor(unsigned char r, unsigned char g, unsigned char b) {
	struct editorThemeColor color = {EDITOR_THEME_COLOR_RGB, 0, r, g, b};
	return color;
}

static struct editorThemeStyle themeBuiltinStyleDefault(void) {
	struct editorThemeStyle style;
	style.fg = editorThemeDefaultColor();
	style.bg = editorThemeDefaultColor();
	style.reverse = 0;
	return style;
}

static struct editorThemeStyle themeBuiltinStyleReverse(void) {
	struct editorThemeStyle style = themeBuiltinStyleDefault();
	style.reverse = 1;
	return style;
}

static struct editorThemeStyle themeBuiltinStylePair(struct editorThemeColor fg,
                                                     struct editorThemeColor bg) {
	struct editorThemeStyle style;
	style.fg = fg;
	style.bg = bg;
	style.reverse = 0;
	return style;
}

void editorThemeSetName(struct editorTheme *theme, const char *name) {
	if (theme == NULL) {
		return;
	}
	(void)snprintf(theme->name, sizeof(theme->name), "%s", name != NULL ? name : "terminal");
}

void editorThemeInitDefault(struct editorTheme *theme_out) {
	(void)editorThemeInitBuiltin(theme_out, "terminal");
}

static void themeBuiltinInitTerminal(struct editorTheme *theme) {
	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "terminal");
	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = editorThemeDefaultColor();
	}
	for (int i = 0; i < EDITOR_THEME_UI_ROLE_COUNT; i++) {
		theme->ui[i] = editorThemeDefaultColor();
	}
	for (int i = 0; i < EDITOR_THEME_STYLE_ROLE_COUNT; i++) {
		theme->styles[i] = themeBuiltinStyleDefault();
	}

	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLACK);
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLUE);
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_CYAN);
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_YELLOW);
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = editorThemeAnsiColor(EDITOR_THEME_ANSI_GREEN);
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = editorThemeAnsiColor(EDITOR_THEME_ANSI_MAGENTA);
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_MAGENTA);
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = editorThemeAnsiColor(EDITOR_THEME_ANSI_WHITE);
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = editorThemeAnsiColor(EDITOR_THEME_ANSI_YELLOW);
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = editorThemeAnsiColor(EDITOR_THEME_ANSI_CYAN);
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_MAGENTA);
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_RED);
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_WHITE);
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = editorThemeDefaultColor();

	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLACK);
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLACK);
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeAnsiColor(EDITOR_THEME_ANSI_WHITE);
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] =
	        editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLACK);
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = editorTheme256Color(236);
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = editorTheme256Color(236);
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = editorThemeAnsiColor(EDITOR_THEME_ANSI_CYAN);
	theme->ui[EDITOR_THEME_UI_ROOT] = editorThemeAnsiColor(EDITOR_THEME_ANSI_WHITE);
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = editorThemeAnsiColor(EDITOR_THEME_ANSI_YELLOW);
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = editorThemeAnsiColor(EDITOR_THEME_ANSI_GREEN);
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = editorThemeAnsiColor(EDITOR_THEME_ANSI_RED);
	theme->ui[EDITOR_THEME_UI_CURSOR] = editorThemeAnsiColor(EDITOR_THEME_ANSI_WHITE);

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStyleReverse();
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStyleReverse();
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStyleReverse();
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStyleReverse();
}

static void themeBuiltinInitA11yDark(struct editorTheme *theme) {
	struct editorThemeColor bg = editorThemeRgbColor(0x2B, 0x2B, 0x2B);
	struct editorThemeColor fg = editorThemeRgbColor(0xF8, 0xF8, 0xF2);
	struct editorThemeColor comment = editorThemeRgbColor(0xD4, 0xD0, 0xAB);
	struct editorThemeColor blue = editorThemeRgbColor(0x6B, 0xBE, 0xFF);
	struct editorThemeColor cyan = editorThemeRgbColor(0x66, 0xDD, 0xEC);
	struct editorThemeColor green = editorThemeRgbColor(0xAB, 0xE3, 0x38);
	struct editorThemeColor orange = editorThemeRgbColor(0xF5, 0xAB, 0x32);
	struct editorThemeColor purple = editorThemeRgbColor(0xDC, 0xC6, 0xE0);
	struct editorThemeColor red = editorThemeRgbColor(0xFF, 0xA0, 0x7A);
	struct editorThemeColor yellow = editorThemeRgbColor(0xFF, 0xD7, 0x00);

	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "a11y-dark");
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = fg;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = bg;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = comment;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = comment;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = comment;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = editorThemeRgbColor(0x3A, 0x3A, 0x3A);
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = editorThemeRgbColor(0x3A, 0x3A, 0x3A);
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = cyan;
	theme->ui[EDITOR_THEME_UI_ROOT] = fg;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = yellow;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = green;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = red;
	theme->ui[EDITOR_THEME_UI_CURSOR] = fg;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = fg;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = comment;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = blue;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = cyan;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = yellow;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = green;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = purple;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = orange;
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = orange;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = cyan;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = purple;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = red;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = yellow;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = fg;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(bg, yellow);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStylePair(bg, fg);
}

static void themeBuiltinInitA11yLight(struct editorTheme *theme) {
	struct editorThemeColor bg = editorThemeRgbColor(0xFE, 0xFE, 0xFE);
	struct editorThemeColor fg = editorThemeRgbColor(0x54, 0x54, 0x54);
	struct editorThemeColor comment = editorThemeRgbColor(0x80, 0x22, 0x00);
	struct editorThemeColor blue = editorThemeRgbColor(0x32, 0x6B, 0xAD);
	struct editorThemeColor cyan = editorThemeRgbColor(0x1F, 0x7C, 0x93);
	struct editorThemeColor green = editorThemeRgbColor(0x00, 0x80, 0x00);
	struct editorThemeColor gray = editorThemeRgbColor(0x69, 0x69, 0x69);
	struct editorThemeColor orange = editorThemeRgbColor(0xA8, 0x5D, 0x00);
	struct editorThemeColor purple = editorThemeRgbColor(0x94, 0x00, 0xD3);
	struct editorThemeColor red = editorThemeRgbColor(0xD9, 0x1E, 0x18);
	struct editorThemeColor yellow = editorThemeRgbColor(0x85, 0x65, 0x14);

	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "a11y-light");
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = fg;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = bg;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = gray;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = gray;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = gray;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = editorThemeRgbColor(0xF0, 0xF0, 0xF0);
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = editorThemeRgbColor(0xEE, 0xEE, 0xEE);
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = cyan;
	theme->ui[EDITOR_THEME_UI_ROOT] = fg;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = yellow;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = green;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = red;
	theme->ui[EDITOR_THEME_UI_CURSOR] = fg;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = fg;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = comment;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = blue;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = cyan;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = yellow;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = green;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = purple;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = orange;
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = orange;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = cyan;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = purple;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = red;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = yellow;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = fg;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(bg, blue);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStylePair(bg, fg);
}

static void themeBuiltinInitAcme(struct editorTheme *theme) {
	struct editorThemeColor bg = editorThemeRgbColor(0xFF, 0xFF, 0xEA);
	struct editorThemeColor fg = editorThemeRgbColor(0x00, 0x00, 0x00);
	struct editorThemeColor black = editorThemeRgbColor(0x10, 0x10, 0x10);
	struct editorThemeColor red = editorThemeRgbColor(0xAF, 0x5F, 0x00);
	struct editorThemeColor green = editorThemeRgbColor(0xCC, 0xCC, 0x7C);
	struct editorThemeColor yellow = editorThemeRgbColor(0xFF, 0xFF, 0x5F);
	struct editorThemeColor blue = editorThemeRgbColor(0xAE, 0xEE, 0xEE);
	struct editorThemeColor magenta = editorThemeRgbColor(0x50, 0x50, 0x50);
	struct editorThemeColor white = editorThemeRgbColor(0xFC, 0xFC, 0xCE);

	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "acme");
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = fg;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = bg;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = magenta;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = magenta;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = magenta;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = white;
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = blue;
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = black;
	theme->ui[EDITOR_THEME_UI_ROOT] = black;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = red;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = green;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = red;
	theme->ui[EDITOR_THEME_UI_CURSOR] = fg;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = fg;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = magenta;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = red;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = black;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = red;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = red;
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = red;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = black;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = red;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = fg;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = fg;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(fg, blue);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(fg, blue);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(fg, blue);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStylePair(fg, yellow);
}

static void themeBuiltinInitSilentium(struct editorTheme *theme) {
	struct editorThemeColor accent = editorThemeRgbColor(0xF6, 0xCE, 0x4E);
	struct editorThemeColor white = editorThemeRgbColor(0xE6, 0xE6, 0xE6);
	struct editorThemeColor light_gray = editorThemeRgbColor(0xA6, 0xA6, 0xA6);
	struct editorThemeColor gray = editorThemeRgbColor(0x73, 0x73, 0x73);
	struct editorThemeColor ghost = editorThemeRgbColor(0x4D, 0x4D, 0x4D);
	struct editorThemeColor dark_gray = editorThemeRgbColor(0x28, 0x28, 0x28);
	struct editorThemeColor dark = editorThemeRgbColor(0x14, 0x14, 0x14);
	struct editorThemeColor red = editorThemeRgbColor(0xE8, 0x5A, 0x4F);
	struct editorThemeColor green = editorThemeRgbColor(0x5F, 0xB3, 0x6A);

	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "silentium");
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = white;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = dark;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = light_gray;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = gray;
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = light_gray;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = ghost;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = dark_gray;
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = ghost;
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = accent;
	theme->ui[EDITOR_THEME_UI_ROOT] = white;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = accent;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = green;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = red;
	theme->ui[EDITOR_THEME_UI_CURSOR] = white;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = white;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = gray;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = accent;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = light_gray;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = accent;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = white;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = white;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = white;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = white;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(white, dark_gray);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(white, ghost);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(dark, accent);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] =
	        themeBuiltinStylePair(dark, accent);
}

static void themeBuiltinInit256Noir(struct editorTheme *theme) {
	struct editorThemeColor bg = editorTheme256Color(16);
	struct editorThemeColor fg = editorTheme256Color(250);
	struct editorThemeColor keyword = editorTheme256Color(255);
	struct editorThemeColor constant = editorTheme256Color(252);
	struct editorThemeColor string = editorTheme256Color(245);
	struct editorThemeColor comment = editorTheme256Color(240);
	struct editorThemeColor number = editorTheme256Color(196);
	struct editorThemeColor current_line = editorTheme256Color(233);

	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "256noir");
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = fg;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = bg;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = comment;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = comment;
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorTheme256Color(245);
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = comment;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = current_line;
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = bg;
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = keyword;
	theme->ui[EDITOR_THEME_UI_ROOT] = fg;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = keyword;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = constant;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = number;
	theme->ui[EDITOR_THEME_UI_CURSOR] = fg;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = fg;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = comment;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = keyword;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = keyword;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = keyword;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = string;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = number;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = constant;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = keyword;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = keyword;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = keyword;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = fg;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(bg, string);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(bg, keyword);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStylePair(bg, keyword);
}

struct themeBuiltinGithubPalette {
	const char *name;
	struct editorThemeColor bg;
	struct editorThemeColor fg;
	struct editorThemeColor muted;
	struct editorThemeColor border;
	struct editorThemeColor current_line;
	struct editorThemeColor header_bg;
	struct editorThemeColor selection_bg;
	struct editorThemeColor accent;
	struct editorThemeColor success;
	struct editorThemeColor attention;
	struct editorThemeColor danger;
	struct editorThemeColor comment;
	struct editorThemeColor constant;
	struct editorThemeColor entity;
	struct editorThemeColor keyword;
	struct editorThemeColor string;
	struct editorThemeColor variable;
	struct editorThemeColor type;
};

static void themeBuiltinInitGithub(struct editorTheme *theme, struct themeBuiltinGithubPalette p) {
	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, p.name);
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = p.fg;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = p.bg;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = p.muted;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = p.border;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = p.muted;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = p.current_line;
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = p.header_bg;
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = p.accent;
	theme->ui[EDITOR_THEME_UI_ROOT] = p.fg;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = p.attention;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = p.success;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = p.danger;
	theme->ui[EDITOR_THEME_UI_CURSOR] = p.fg;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = p.fg;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = p.comment;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = p.keyword;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = p.type;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = p.entity;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = p.string;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = p.variable;
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = p.variable;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = p.keyword;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = p.fg;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = p.fg;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(p.fg, p.selection_bg);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(p.muted, p.header_bg);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(p.fg, p.header_bg);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStylePair(p.fg, p.bg);
}

static void themeBuiltinInitGithubLight(struct editorTheme *theme) {
	themeBuiltinInitGithub(theme, (struct themeBuiltinGithubPalette){
	                                      .name = "github-light",
	                                      .bg = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                                      .fg = editorThemeRgbColor(0x1F, 0x23, 0x28),
	                                      .muted = editorThemeRgbColor(0x65, 0x6D, 0x76),
	                                      .border = editorThemeRgbColor(0xD0, 0xD7, 0xDE),
	                                      .current_line = editorThemeRgbColor(0xF4, 0xF6, 0xF8),
	                                      .header_bg = editorThemeRgbColor(0xF6, 0xF8, 0xFA),
	                                      .selection_bg = editorThemeRgbColor(0xBB, 0xDF, 0xFF),
	                                      .accent = editorThemeRgbColor(0x09, 0x69, 0xDA),
	                                      .success = editorThemeRgbColor(0x1A, 0x7F, 0x37),
	                                      .attention = editorThemeRgbColor(0x9A, 0x67, 0x00),
	                                      .danger = editorThemeRgbColor(0xCF, 0x22, 0x2E),
	                                      .comment = editorThemeRgbColor(0x57, 0x60, 0x6A),
	                                      .constant = editorThemeRgbColor(0x05, 0x50, 0xAE),
	                                      .entity = editorThemeRgbColor(0x82, 0x50, 0xDF),
	                                      .keyword = editorThemeRgbColor(0xCF, 0x22, 0x2E),
	                                      .string = editorThemeRgbColor(0x0A, 0x30, 0x69),
	                                      .variable = editorThemeRgbColor(0x1F, 0x23, 0x28),
	                                      .type = editorThemeRgbColor(0x1F, 0x23, 0x28),
	                              });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0x57, 0x60, 0x6A);
}

static void themeBuiltinInitGithubDark(struct editorTheme *theme) {
	themeBuiltinInitGithub(theme, (struct themeBuiltinGithubPalette){
	                                      .name = "github-dark",
	                                      .bg = editorThemeRgbColor(0x0D, 0x11, 0x17),
	                                      .fg = editorThemeRgbColor(0xE6, 0xED, 0xF3),
	                                      .muted = editorThemeRgbColor(0x84, 0x8D, 0x97),
	                                      .border = editorThemeRgbColor(0x30, 0x36, 0x3D),
	                                      .current_line = editorThemeRgbColor(0x17, 0x1C, 0x23),
	                                      .header_bg = editorThemeRgbColor(0x16, 0x1B, 0x22),
	                                      .selection_bg = editorThemeRgbColor(0x24, 0x3B, 0x61),
	                                      .accent = editorThemeRgbColor(0x2F, 0x81, 0xF7),
	                                      .success = editorThemeRgbColor(0x3F, 0xB9, 0x50),
	                                      .attention = editorThemeRgbColor(0xD2, 0x99, 0x22),
	                                      .danger = editorThemeRgbColor(0xF8, 0x51, 0x49),
	                                      .comment = editorThemeRgbColor(0x8B, 0x94, 0x9E),
	                                      .constant = editorThemeRgbColor(0x79, 0xC0, 0xFF),
	                                      .entity = editorThemeRgbColor(0xD2, 0xA8, 0xFF),
	                                      .keyword = editorThemeRgbColor(0xFF, 0x7B, 0x72),
	                                      .string = editorThemeRgbColor(0xA5, 0xD6, 0xFF),
	                                      .variable = editorThemeRgbColor(0xE6, 0xED, 0xF3),
	                                      .type = editorThemeRgbColor(0xE6, 0xED, 0xF3),
	                              });
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = editorThemeRgbColor(0x79, 0xC0, 0xFF);
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0xB1, 0xBA, 0xC4);
}

static void themeBuiltinInitMolokai(struct editorTheme *theme) {
	themeBuiltinInitGithub(theme, (struct themeBuiltinGithubPalette){
	                                      .name = "molokai",
	                                      .bg = editorThemeRgbColor(0x1B, 0x1D, 0x1E),
	                                      .fg = editorThemeRgbColor(0xF8, 0xF8, 0xF2),
	                                      .muted = editorThemeRgbColor(0x7E, 0x8E, 0x91),
	                                      .border = editorThemeRgbColor(0x45, 0x53, 0x54),
	                                      .current_line = editorThemeRgbColor(0x29, 0x37, 0x39),
	                                      .header_bg = editorThemeRgbColor(0x23, 0x25, 0x26),
	                                      .selection_bg = editorThemeRgbColor(0x40, 0x3D, 0x3D),
	                                      .accent = editorThemeRgbColor(0x66, 0xD9, 0xEF),
	                                      .success = editorThemeRgbColor(0xA6, 0xE2, 0x2E),
	                                      .attention = editorThemeRgbColor(0xFD, 0x97, 0x1F),
	                                      .danger = editorThemeRgbColor(0xF9, 0x26, 0x72),
	                                      .comment = editorThemeRgbColor(0x7E, 0x8E, 0x91),
	                                      .constant = editorThemeRgbColor(0xAE, 0x81, 0xFF),
	                                      .entity = editorThemeRgbColor(0xA6, 0xE2, 0x2E),
	                                      .keyword = editorThemeRgbColor(0xF9, 0x26, 0x72),
	                                      .string = editorThemeRgbColor(0xE6, 0xDB, 0x74),
	                                      .variable = editorThemeRgbColor(0xFD, 0x97, 0x1F),
	                                      .type = editorThemeRgbColor(0x66, 0xD9, 0xEF),
	                              });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0xB5, 0xC4, 0xC7);
}

struct themeBuiltinModusPalette {
	const char *name;
	struct editorThemeColor bg_main;
	struct editorThemeColor bg_dim;
	struct editorThemeColor fg_main;
	struct editorThemeColor fg_dim;
	struct editorThemeColor fg_alt;
	struct editorThemeColor border;
	struct editorThemeColor bg_hl_line;
	struct editorThemeColor bg_region;
	struct editorThemeColor fg_region;
	struct editorThemeColor bg_mode_line_active;
	struct editorThemeColor fg_mode_line_active;
	struct editorThemeColor bg_mode_line_inactive;
	struct editorThemeColor bg_tab_current;
	struct editorThemeColor cursor;
	struct editorThemeColor red;
	struct editorThemeColor green;
	struct editorThemeColor yellow;
	struct editorThemeColor cyan;
	struct editorThemeColor comment;
	struct editorThemeColor keyword;
	struct editorThemeColor type;
	struct editorThemeColor function;
	struct editorThemeColor string;
	struct editorThemeColor number;
	struct editorThemeColor constant;
	struct editorThemeColor variable;
	struct editorThemeColor parameter;
	struct editorThemeColor module;
	struct editorThemeColor property;
	struct editorThemeColor preprocessor;
};

static void themeBuiltinInitModus(struct editorTheme *theme, struct themeBuiltinModusPalette p) {
	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, p.name);
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = p.fg_main;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = p.bg_main;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = p.fg_dim;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = p.border;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = p.fg_dim;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = p.bg_hl_line;
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = p.bg_mode_line_inactive;
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = p.cyan;
	theme->ui[EDITOR_THEME_UI_ROOT] = p.fg_alt;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = p.yellow;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = p.green;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = p.red;
	theme->ui[EDITOR_THEME_UI_CURSOR] = p.cursor;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = p.fg_main;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = p.comment;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = p.keyword;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = p.type;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = p.function;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = p.string;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = p.number;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = p.variable;
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = p.parameter;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = p.module;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = p.property;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = p.preprocessor;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = p.fg_main;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = p.fg_main;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] =
	        themeBuiltinStylePair(p.fg_region, p.bg_region);
	theme->styles[EDITOR_THEME_STYLE_STATUS] =
	        themeBuiltinStylePair(p.fg_mode_line_active, p.bg_mode_line_active);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] =
	        themeBuiltinStylePair(p.fg_main, p.bg_tab_current);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] =
	        themeBuiltinStylePair(p.fg_mode_line_active, p.bg_mode_line_active);
}

static void themeBuiltinInitModusOperandi(struct editorTheme *theme) {
	themeBuiltinInitModus(
	        theme, (struct themeBuiltinModusPalette){
	                       .name = "modus-operandi",
	                       .bg_main = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .bg_dim = editorThemeRgbColor(0xF2, 0xF2, 0xF2),
	                       .fg_main = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .fg_dim = editorThemeRgbColor(0x59, 0x59, 0x59),
	                       .fg_alt = editorThemeRgbColor(0x19, 0x36, 0x68),
	                       .border = editorThemeRgbColor(0x9F, 0x9F, 0x9F),
	                       .bg_hl_line = editorThemeRgbColor(0xDA, 0xE5, 0xEC),
	                       .bg_region = editorThemeRgbColor(0xBD, 0xBD, 0xBD),
	                       .fg_region = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .bg_mode_line_active = editorThemeRgbColor(0xC8, 0xC8, 0xC8),
	                       .fg_mode_line_active = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .bg_mode_line_inactive = editorThemeRgbColor(0xE6, 0xE6, 0xE6),
	                       .bg_tab_current = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .cursor = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .red = editorThemeRgbColor(0xA6, 0x00, 0x00),
	                       .green = editorThemeRgbColor(0x00, 0x68, 0x00),
	                       .yellow = editorThemeRgbColor(0x6F, 0x55, 0x00),
	                       .cyan = editorThemeRgbColor(0x00, 0x5E, 0x8B),
	                       .comment = editorThemeRgbColor(0x59, 0x59, 0x59),
	                       .keyword = editorThemeRgbColor(0x53, 0x1A, 0xB6),
	                       .type = editorThemeRgbColor(0x00, 0x5F, 0x5F),
	                       .function = editorThemeRgbColor(0x72, 0x10, 0x45),
	                       .string = editorThemeRgbColor(0x35, 0x48, 0xCF),
	                       .number = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .constant = editorThemeRgbColor(0x00, 0x00, 0xB0),
	                       .variable = editorThemeRgbColor(0x00, 0x5E, 0x8B),
	                       .parameter = editorThemeRgbColor(0x7A, 0x4F, 0x2F),
	                       .module = editorThemeRgbColor(0x00, 0x5F, 0x5F),
	                       .property = editorThemeRgbColor(0x00, 0x5E, 0x8B),
	                       .preprocessor = editorThemeRgbColor(0xA0, 0x13, 0x2F),
	               });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0x40, 0x40, 0x40);
}

static void themeBuiltinInitModusOperandiTinted(struct editorTheme *theme) {
	themeBuiltinInitModus(
	        theme, (struct themeBuiltinModusPalette){
	                       .name = "modus-operandi-tinted",
	                       .bg_main = editorThemeRgbColor(0xFB, 0xF7, 0xF0),
	                       .bg_dim = editorThemeRgbColor(0xEF, 0xE9, 0xDD),
	                       .fg_main = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .fg_dim = editorThemeRgbColor(0x59, 0x59, 0x59),
	                       .fg_alt = editorThemeRgbColor(0x19, 0x36, 0x68),
	                       .border = editorThemeRgbColor(0x9F, 0x96, 0x90),
	                       .bg_hl_line = editorThemeRgbColor(0xF1, 0xD5, 0xD0),
	                       .bg_region = editorThemeRgbColor(0xC2, 0xBC, 0xB5),
	                       .fg_region = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .bg_mode_line_active = editorThemeRgbColor(0xCA, 0xB9, 0xB2),
	                       .fg_mode_line_active = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .bg_mode_line_inactive = editorThemeRgbColor(0xDF, 0xD9, 0xCF),
	                       .bg_tab_current = editorThemeRgbColor(0xFB, 0xF7, 0xF0),
	                       .cursor = editorThemeRgbColor(0xD0, 0x00, 0x00),
	                       .red = editorThemeRgbColor(0xA6, 0x00, 0x00),
	                       .green = editorThemeRgbColor(0x00, 0x63, 0x00),
	                       .yellow = editorThemeRgbColor(0x6D, 0x50, 0x00),
	                       .cyan = editorThemeRgbColor(0x00, 0x59, 0x8B),
	                       .comment = editorThemeRgbColor(0x7F, 0x00, 0x00),
	                       .keyword = editorThemeRgbColor(0x00, 0x31, 0xA9),
	                       .type = editorThemeRgbColor(0x30, 0x60, 0x10),
	                       .function = editorThemeRgbColor(0x60, 0x29, 0x38),
	                       .string = editorThemeRgbColor(0x00, 0x59, 0x8B),
	                       .number = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .constant = editorThemeRgbColor(0x53, 0x1A, 0xB6),
	                       .variable = editorThemeRgbColor(0x00, 0x60, 0x3F),
	                       .parameter = editorThemeRgbColor(0x57, 0x43, 0x16),
	                       .module = editorThemeRgbColor(0x00, 0x5F, 0x5F),
	                       .property = editorThemeRgbColor(0x00, 0x60, 0x3F),
	                       .preprocessor = editorThemeRgbColor(0x89, 0x40, 0x00),
	               });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0x40, 0x3D, 0x38);
}

static void themeBuiltinInitModusVivendi(struct editorTheme *theme) {
	themeBuiltinInitModus(
	        theme, (struct themeBuiltinModusPalette){
	                       .name = "modus-vivendi",
	                       .bg_main = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .bg_dim = editorThemeRgbColor(0x1E, 0x1E, 0x1E),
	                       .fg_main = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .fg_dim = editorThemeRgbColor(0x98, 0x98, 0x98),
	                       .fg_alt = editorThemeRgbColor(0xC6, 0xDA, 0xFF),
	                       .border = editorThemeRgbColor(0x64, 0x64, 0x64),
	                       .bg_hl_line = editorThemeRgbColor(0x2F, 0x38, 0x49),
	                       .bg_region = editorThemeRgbColor(0x5A, 0x5A, 0x5A),
	                       .fg_region = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .bg_mode_line_active = editorThemeRgbColor(0x50, 0x50, 0x50),
	                       .fg_mode_line_active = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .bg_mode_line_inactive = editorThemeRgbColor(0x2D, 0x2D, 0x2D),
	                       .bg_tab_current = editorThemeRgbColor(0x00, 0x00, 0x00),
	                       .cursor = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .red = editorThemeRgbColor(0xFF, 0x5F, 0x59),
	                       .green = editorThemeRgbColor(0x44, 0xBC, 0x44),
	                       .yellow = editorThemeRgbColor(0xD0, 0xBC, 0x00),
	                       .cyan = editorThemeRgbColor(0x00, 0xD3, 0xD0),
	                       .comment = editorThemeRgbColor(0x98, 0x98, 0x98),
	                       .keyword = editorThemeRgbColor(0xB6, 0xA0, 0xFF),
	                       .type = editorThemeRgbColor(0x6A, 0xE4, 0xB9),
	                       .function = editorThemeRgbColor(0xFE, 0xAC, 0xD0),
	                       .string = editorThemeRgbColor(0x79, 0xA8, 0xFF),
	                       .number = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .constant = editorThemeRgbColor(0x00, 0xBC, 0xFF),
	                       .variable = editorThemeRgbColor(0x00, 0xD3, 0xD0),
	                       .parameter = editorThemeRgbColor(0xD2, 0xB5, 0x80),
	                       .module = editorThemeRgbColor(0x6A, 0xE4, 0xB9),
	                       .property = editorThemeRgbColor(0x00, 0xD3, 0xD0),
	                       .preprocessor = editorThemeRgbColor(0xFF, 0x7F, 0x86),
	               });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0xB5, 0xB5, 0xB5);
}

static void themeBuiltinInitModusVivendiTinted(struct editorTheme *theme) {
	themeBuiltinInitModus(
	        theme, (struct themeBuiltinModusPalette){
	                       .name = "modus-vivendi-tinted",
	                       .bg_main = editorThemeRgbColor(0x0D, 0x0E, 0x1C),
	                       .bg_dim = editorThemeRgbColor(0x1D, 0x22, 0x35),
	                       .fg_main = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .fg_dim = editorThemeRgbColor(0x98, 0x98, 0x98),
	                       .fg_alt = editorThemeRgbColor(0xC6, 0xDA, 0xFF),
	                       .border = editorThemeRgbColor(0x61, 0x64, 0x7A),
	                       .bg_hl_line = editorThemeRgbColor(0x30, 0x3A, 0x6F),
	                       .bg_region = editorThemeRgbColor(0x55, 0x5A, 0x66),
	                       .fg_region = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .bg_mode_line_active = editorThemeRgbColor(0x48, 0x4D, 0x67),
	                       .fg_mode_line_active = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .bg_mode_line_inactive = editorThemeRgbColor(0x29, 0x2D, 0x48),
	                       .bg_tab_current = editorThemeRgbColor(0x0D, 0x0E, 0x1C),
	                       .cursor = editorThemeRgbColor(0xFF, 0x66, 0xFF),
	                       .red = editorThemeRgbColor(0xFF, 0x5F, 0x59),
	                       .green = editorThemeRgbColor(0x44, 0xBC, 0x44),
	                       .yellow = editorThemeRgbColor(0xD0, 0xBC, 0x00),
	                       .cyan = editorThemeRgbColor(0x00, 0xD3, 0xD0),
	                       .comment = editorThemeRgbColor(0xEF, 0x83, 0x86),
	                       .keyword = editorThemeRgbColor(0x79, 0xA8, 0xFF),
	                       .type = editorThemeRgbColor(0x11, 0xC7, 0x77),
	                       .function = editorThemeRgbColor(0xF7, 0x8F, 0xE7),
	                       .string = editorThemeRgbColor(0x2F, 0xAF, 0xFF),
	                       .number = editorThemeRgbColor(0xFF, 0xFF, 0xFF),
	                       .constant = editorThemeRgbColor(0xB6, 0xA0, 0xFF),
	                       .variable = editorThemeRgbColor(0x4A, 0xE2, 0xF0),
	                       .parameter = editorThemeRgbColor(0xD2, 0xB5, 0x80),
	                       .module = editorThemeRgbColor(0x6A, 0xE4, 0xB9),
	                       .property = editorThemeRgbColor(0x4A, 0xE2, 0xF0),
	                       .preprocessor = editorThemeRgbColor(0xFF, 0x7F, 0x86),
	               });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0xB0, 0xB5, 0xC5);
}

struct themeBuiltinKanagawaPalette {
	const char *name;
	struct editorThemeColor bg;
	struct editorThemeColor fg;
	struct editorThemeColor muted;
	struct editorThemeColor border;
	struct editorThemeColor current_line;
	struct editorThemeColor header_bg;
	struct editorThemeColor selection_bg;
	struct editorThemeColor accent;
	struct editorThemeColor success;
	struct editorThemeColor attention;
	struct editorThemeColor danger;
	struct editorThemeColor comment;
	struct editorThemeColor keyword;
	struct editorThemeColor func;
	struct editorThemeColor type;
	struct editorThemeColor string;
	struct editorThemeColor number;
	struct editorThemeColor constant;
	struct editorThemeColor variable;
	struct editorThemeColor parameter;
	struct editorThemeColor property;
	struct editorThemeColor preprocessor;
	struct editorThemeColor op;
};

static void themeBuiltinInitKanagawa(struct editorTheme *theme,
                                     struct themeBuiltinKanagawaPalette p) {
	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, p.name);
	theme->ui[EDITOR_THEME_UI_FOREGROUND] = p.fg;
	theme->ui[EDITOR_THEME_UI_BACKGROUND] = p.bg;
	theme->ui[EDITOR_THEME_UI_LINE_NUMBER] = p.muted;
	theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] = p.border;
	theme->ui[EDITOR_THEME_UI_PLACEHOLDER] = p.muted;
	theme->ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = p.current_line;
	theme->ui[EDITOR_THEME_UI_DRAWER_HEADER_BG] = p.header_bg;
	theme->ui[EDITOR_THEME_UI_DIRECTORY] = p.accent;
	theme->ui[EDITOR_THEME_UI_ROOT] = p.fg;
	theme->ui[EDITOR_THEME_UI_GIT_MODIFIED] = p.attention;
	theme->ui[EDITOR_THEME_UI_GIT_UNTRACKED] = p.success;
	theme->ui[EDITOR_THEME_UI_GIT_CONFLICT] = p.danger;
	theme->ui[EDITOR_THEME_UI_CURSOR] = p.fg;

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = p.fg;
	}
	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] = p.comment;
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] = p.keyword;
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] = p.type;
	theme->syntax[EDITOR_SYNTAX_HL_FUNCTION] = p.func;
	theme->syntax[EDITOR_SYNTAX_HL_STRING] = p.string;
	theme->syntax[EDITOR_SYNTAX_HL_NUMBER] = p.number;
	theme->syntax[EDITOR_SYNTAX_HL_CONSTANT] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_VARIABLE] = p.variable;
	theme->syntax[EDITOR_SYNTAX_HL_PARAMETER] = p.parameter;
	theme->syntax[EDITOR_SYNTAX_HL_MODULE] = p.constant;
	theme->syntax[EDITOR_SYNTAX_HL_PROPERTY] = p.property;
	theme->syntax[EDITOR_SYNTAX_HL_PREPROCESSOR] = p.preprocessor;
	theme->syntax[EDITOR_SYNTAX_HL_OPERATOR] = p.op;
	theme->syntax[EDITOR_SYNTAX_HL_PUNCTUATION] = p.fg;

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = themeBuiltinStylePair(p.fg, p.selection_bg);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = themeBuiltinStylePair(p.muted, p.header_bg);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = themeBuiltinStylePair(p.fg, p.header_bg);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = themeBuiltinStylePair(p.fg, p.bg);
}

static void themeBuiltinInitKanagawaWave(struct editorTheme *theme) {
	themeBuiltinInitKanagawa(theme,
	                         (struct themeBuiltinKanagawaPalette){
	                                 .name = "kanagawa-wave",
	                                 .bg = editorThemeRgbColor(0x1F, 0x1F, 0x28),
	                                 .fg = editorThemeRgbColor(0xDC, 0xD7, 0xBA),
	                                 .muted = editorThemeRgbColor(0x54, 0x54, 0x6D),
	                                 .border = editorThemeRgbColor(0x36, 0x36, 0x46),
	                                 .current_line = editorThemeRgbColor(0x2A, 0x2A, 0x37),
	                                 .header_bg = editorThemeRgbColor(0x18, 0x18, 0x20),
	                                 .selection_bg = editorThemeRgbColor(0x2D, 0x4F, 0x67),
	                                 .accent = editorThemeRgbColor(0x7E, 0x9C, 0xD8),
	                                 .success = editorThemeRgbColor(0x76, 0x94, 0x6A),
	                                 .attention = editorThemeRgbColor(0xDC, 0xA5, 0x61),
	                                 .danger = editorThemeRgbColor(0xC3, 0x40, 0x43),
	                                 .comment = editorThemeRgbColor(0x72, 0x71, 0x69),
	                                 .keyword = editorThemeRgbColor(0x95, 0x7F, 0xB8),
	                                 .func = editorThemeRgbColor(0x7E, 0x9C, 0xD8),
	                                 .type = editorThemeRgbColor(0x7A, 0xA8, 0x9F),
	                                 .string = editorThemeRgbColor(0x98, 0xBB, 0x6C),
	                                 .number = editorThemeRgbColor(0xD2, 0x7E, 0x99),
	                                 .constant = editorThemeRgbColor(0xFF, 0xA0, 0x66),
	                                 .variable = editorThemeRgbColor(0xDC, 0xD7, 0xBA),
	                                 .parameter = editorThemeRgbColor(0x9C, 0xAB, 0xCA),
	                                 .property = editorThemeRgbColor(0xE6, 0xC3, 0x84),
	                                 .preprocessor = editorThemeRgbColor(0xFF, 0xA0, 0x66),
	                                 .op = editorThemeRgbColor(0xC0, 0xA3, 0x6E),
	                         });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0x9D, 0x9C, 0xB0);
}

static void themeBuiltinInitKanagawaDragon(struct editorTheme *theme) {
	themeBuiltinInitKanagawa(theme,
	                         (struct themeBuiltinKanagawaPalette){
	                                 .name = "kanagawa-dragon",
	                                 .bg = editorThemeRgbColor(0x18, 0x16, 0x16),
	                                 .fg = editorThemeRgbColor(0xC5, 0xC9, 0xC5),
	                                 .muted = editorThemeRgbColor(0x62, 0x5E, 0x5A),
	                                 .border = editorThemeRgbColor(0x39, 0x38, 0x36),
	                                 .current_line = editorThemeRgbColor(0x28, 0x27, 0x27),
	                                 .header_bg = editorThemeRgbColor(0x12, 0x12, 0x0F),
	                                 .selection_bg = editorThemeRgbColor(0x22, 0x32, 0x49),
	                                 .accent = editorThemeRgbColor(0x8B, 0xA4, 0xB0),
	                                 .success = editorThemeRgbColor(0x87, 0xA9, 0x87),
	                                 .attention = editorThemeRgbColor(0xC4, 0xB2, 0x8A),
	                                 .danger = editorThemeRgbColor(0xC4, 0x74, 0x6E),
	                                 .comment = editorThemeRgbColor(0x73, 0x7C, 0x73),
	                                 .keyword = editorThemeRgbColor(0x89, 0x92, 0xA7),
	                                 .func = editorThemeRgbColor(0x8B, 0xA4, 0xB0),
	                                 .type = editorThemeRgbColor(0x8E, 0xA4, 0xA2),
	                                 .string = editorThemeRgbColor(0x87, 0xA9, 0x87),
	                                 .number = editorThemeRgbColor(0xA2, 0x92, 0xA3),
	                                 .constant = editorThemeRgbColor(0xB6, 0x92, 0x7B),
	                                 .variable = editorThemeRgbColor(0xC5, 0xC9, 0xC5),
	                                 .parameter = editorThemeRgbColor(0x94, 0x9F, 0xB5),
	                                 .property = editorThemeRgbColor(0xC4, 0xB2, 0x8A),
	                                 .preprocessor = editorThemeRgbColor(0xB6, 0x92, 0x7B),
	                                 .op = editorThemeRgbColor(0xC4, 0xB2, 0x8A),
	                         });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0x9C, 0x95, 0x8E);
}

static void themeBuiltinInitKanagawaLotus(struct editorTheme *theme) {
	themeBuiltinInitKanagawa(theme,
	                         (struct themeBuiltinKanagawaPalette){
	                                 .name = "kanagawa-lotus",
	                                 .bg = editorThemeRgbColor(0xF2, 0xEC, 0xBC),
	                                 .fg = editorThemeRgbColor(0x1F, 0x1F, 0x28),
	                                 .muted = editorThemeRgbColor(0x8A, 0x89, 0x80),
	                                 .border = editorThemeRgbColor(0xE7, 0xDB, 0xA0),
	                                 .current_line = editorThemeRgbColor(0xDC, 0xD5, 0xAC),
	                                 .header_bg = editorThemeRgbColor(0xE5, 0xDD, 0xB0),
	                                 .selection_bg = editorThemeRgbColor(0xD7, 0xE3, 0xD8),
	                                 .accent = editorThemeRgbColor(0x4E, 0x8C, 0xA2),
	                                 .success = editorThemeRgbColor(0x6F, 0x89, 0x4E),
	                                 .attention = editorThemeRgbColor(0xCC, 0x6D, 0x00),
	                                 .danger = editorThemeRgbColor(0xC8, 0x40, 0x53),
	                                 .comment = editorThemeRgbColor(0x8A, 0x83, 0x6F),
	                                 .keyword = editorThemeRgbColor(0x62, 0x4C, 0x83),
	                                 .func = editorThemeRgbColor(0x66, 0x93, 0xBF),
	                                 .type = editorThemeRgbColor(0x59, 0x7B, 0x75),
	                                 .string = editorThemeRgbColor(0x6F, 0x89, 0x4E),
	                                 .number = editorThemeRgbColor(0xC8, 0x40, 0x53),
	                                 .constant = editorThemeRgbColor(0xCC, 0x6D, 0x00),
	                                 .variable = editorThemeRgbColor(0x1F, 0x1F, 0x28),
	                                 .parameter = editorThemeRgbColor(0x5A, 0x77, 0x85),
	                                 .property = editorThemeRgbColor(0x83, 0x6F, 0x4A),
	                                 .preprocessor = editorThemeRgbColor(0xCC, 0x6D, 0x00),
	                                 .op = editorThemeRgbColor(0x77, 0x71, 0x3F),
	                         });
	theme->ui[EDITOR_THEME_UI_DRAWER_ICON] = editorThemeRgbColor(0x5A, 0x57, 0x4D);
}

static void themeBuiltinFinalize(struct editorTheme *theme) {
	if (theme == NULL) {
		return;
	}
	if (theme->ui[EDITOR_THEME_UI_DRAWER_ICON].kind == EDITOR_THEME_COLOR_DEFAULT) {
		theme->ui[EDITOR_THEME_UI_DRAWER_ICON] =
		        theme->ui[EDITOR_THEME_UI_DRAWER_CONNECTOR];
	}
}

int editorThemeInitBuiltin(struct editorTheme *theme_out, const char *name) {
	if (theme_out == NULL || name == NULL) {
		return 0;
	}
	int matched = 1;
	if (strcmp(name, "terminal") == 0) {
		themeBuiltinInitTerminal(theme_out);
	} else if (strcmp(name, "a11y-dark") == 0) {
		themeBuiltinInitA11yDark(theme_out);
	} else if (strcmp(name, "a11y-light") == 0) {
		themeBuiltinInitA11yLight(theme_out);
	} else if (strcmp(name, "acme") == 0) {
		themeBuiltinInitAcme(theme_out);
	} else if (strcmp(name, "silentium") == 0) {
		themeBuiltinInitSilentium(theme_out);
	} else if (strcmp(name, "256noir") == 0 || strcmp(name, "256_noir") == 0) {
		themeBuiltinInit256Noir(theme_out);
	} else if (strcmp(name, "github-light") == 0) {
		themeBuiltinInitGithubLight(theme_out);
	} else if (strcmp(name, "github-dark") == 0) {
		themeBuiltinInitGithubDark(theme_out);
	} else if (strcmp(name, "modus-operandi") == 0) {
		themeBuiltinInitModusOperandi(theme_out);
	} else if (strcmp(name, "modus-operandi-tinted") == 0) {
		themeBuiltinInitModusOperandiTinted(theme_out);
	} else if (strcmp(name, "modus-vivendi") == 0) {
		themeBuiltinInitModusVivendi(theme_out);
	} else if (strcmp(name, "modus-vivendi-tinted") == 0) {
		themeBuiltinInitModusVivendiTinted(theme_out);
	} else if (strcmp(name, "molokai") == 0) {
		themeBuiltinInitMolokai(theme_out);
	} else if (strcmp(name, "kanagawa") == 0 || strcmp(name, "kanagawa-wave") == 0) {
		themeBuiltinInitKanagawaWave(theme_out);
	} else if (strcmp(name, "kanagawa-dragon") == 0) {
		themeBuiltinInitKanagawaDragon(theme_out);
	} else if (strcmp(name, "kanagawa-lotus") == 0) {
		themeBuiltinInitKanagawaLotus(theme_out);
	} else {
		matched = 0;
	}
	if (matched) {
		themeBuiltinFinalize(theme_out);
	}
	return matched;
}
