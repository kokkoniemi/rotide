#include "config/theme_config.h"

#include "config/common.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum editorThemeFileStatus {
	EDITOR_THEME_FILE_APPLIED = 0,
	EDITOR_THEME_FILE_MISSING,
	EDITOR_THEME_FILE_OUT_OF_MEMORY
};

struct editorThemeParseContext {
	int is_theme_file;
	int in_theme_table;
	int in_theme_syntax_table;
	int in_theme_ui_table;
	int had_invalid;
	char selected_name[64];
	char theme_name[64];
	char inherits_name[64];
	int saw_theme_name;
};

struct editorThemeColor editorThemeDefaultColor(void) {
	struct editorThemeColor color = {EDITOR_THEME_COLOR_DEFAULT, 0, 0, 0, 0};
	return color;
}

struct editorThemeColor editorThemeAnsiColor(enum editorThemeAnsiColor color) {
	struct editorThemeColor theme_color = {EDITOR_THEME_COLOR_ANSI, (unsigned char)color, 0, 0, 0};
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

static struct editorThemeStyle editorThemeStyleDefault(void) {
	struct editorThemeStyle style;
	style.fg = editorThemeDefaultColor();
	style.bg = editorThemeDefaultColor();
	style.reverse = 0;
	return style;
}

static struct editorThemeStyle editorThemeStyleReverse(void) {
	struct editorThemeStyle style = editorThemeStyleDefault();
	style.reverse = 1;
	return style;
}

static struct editorThemeStyle editorThemeStylePair(struct editorThemeColor fg,
		struct editorThemeColor bg) {
	struct editorThemeStyle style;
	style.fg = fg;
	style.bg = bg;
	style.reverse = 0;
	return style;
}

static void editorThemeSetName(struct editorTheme *theme, const char *name) {
	if (theme == NULL) {
		return;
	}
	(void)snprintf(theme->name, sizeof(theme->name), "%s", name != NULL ? name : "terminal");
}

void editorThemeInitDefault(struct editorTheme *theme_out) {
	(void)editorThemeInitBuiltin(theme_out, "terminal");
}

static void editorThemeInitTerminal(struct editorTheme *theme) {
	memset(theme, 0, sizeof(*theme));
	editorThemeSetName(theme, "terminal");
	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme->syntax[i] = editorThemeDefaultColor();
	}
	for (int i = 0; i < EDITOR_THEME_UI_ROLE_COUNT; i++) {
		theme->ui[i] = editorThemeDefaultColor();
	}
	for (int i = 0; i < EDITOR_THEME_STYLE_ROLE_COUNT; i++) {
		theme->styles[i] = editorThemeStyleDefault();
	}

	theme->syntax[EDITOR_SYNTAX_HL_COMMENT] =
			editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLACK);
	theme->syntax[EDITOR_SYNTAX_HL_KEYWORD] =
			editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLUE);
	theme->syntax[EDITOR_SYNTAX_HL_TYPE] =
			editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_CYAN);
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

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = editorThemeStyleReverse();
	theme->styles[EDITOR_THEME_STYLE_STATUS] = editorThemeStyleReverse();
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = editorThemeStyleReverse();
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = editorThemeStyleReverse();
}

static void editorThemeInitA11yDark(struct editorTheme *theme) {
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

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = editorThemeStylePair(bg, yellow);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = editorThemeStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = editorThemeStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = editorThemeStylePair(bg, fg);
}

static void editorThemeInitA11yLight(struct editorTheme *theme) {
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

	theme->styles[EDITOR_THEME_STYLE_SELECTION] = editorThemeStylePair(bg, blue);
	theme->styles[EDITOR_THEME_STYLE_STATUS] = editorThemeStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_TAB_ACTIVE] = editorThemeStylePair(bg, fg);
	theme->styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE] = editorThemeStylePair(bg, fg);
}

int editorThemeInitBuiltin(struct editorTheme *theme_out, const char *name) {
	if (theme_out == NULL || name == NULL) {
		return 0;
	}
	if (strcmp(name, "terminal") == 0) {
		editorThemeInitTerminal(theme_out);
		return 1;
	}
	if (strcmp(name, "a11y-dark") == 0) {
		editorThemeInitA11yDark(theme_out);
		return 1;
	}
	if (strcmp(name, "a11y-light") == 0) {
		editorThemeInitA11yLight(theme_out);
		return 1;
	}
	return 0;
}

static int editorNormalizeThemeToken(const char *token, char *out, size_t out_size) {
	if (token == NULL || out == NULL || out_size == 0) {
		return 0;
	}

	size_t write_idx = 0;
	for (size_t i = 0; token[i] != '\0'; i++) {
		unsigned char ch = (unsigned char)token[i];
		if (isspace(ch)) {
			continue;
		}
		if (ch == '-') {
			ch = '_';
		}
		if (write_idx + 1 >= out_size) {
			return 0;
		}
		out[write_idx++] = (char)tolower(ch);
	}

	if (write_idx == 0) {
		return 0;
	}
	out[write_idx] = '\0';
	return 1;
}

static int editorThemeNameIsValid(const char *name) {
	if (name == NULL || name[0] == '\0' || strlen(name) >= 64) {
		return 0;
	}
	for (size_t i = 0; name[i] != '\0'; i++) {
		unsigned char ch = (unsigned char)name[i];
		if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
			return 0;
		}
	}
	return 1;
}

static int editorParseSyntaxHighlightClassName(const char *name,
		enum editorSyntaxHighlightClass *class_out) {
	char normalized[64];
	if (class_out == NULL || !editorNormalizeThemeToken(name, normalized, sizeof(normalized))) {
		return 0;
	}

	if (strcmp(normalized, "comment") == 0) {
		*class_out = EDITOR_SYNTAX_HL_COMMENT;
		return 1;
	}
	if (strcmp(normalized, "keyword") == 0) {
		*class_out = EDITOR_SYNTAX_HL_KEYWORD;
		return 1;
	}
	if (strcmp(normalized, "type") == 0) {
		*class_out = EDITOR_SYNTAX_HL_TYPE;
		return 1;
	}
	if (strcmp(normalized, "function") == 0) {
		*class_out = EDITOR_SYNTAX_HL_FUNCTION;
		return 1;
	}
	if (strcmp(normalized, "string") == 0) {
		*class_out = EDITOR_SYNTAX_HL_STRING;
		return 1;
	}
	if (strcmp(normalized, "number") == 0) {
		*class_out = EDITOR_SYNTAX_HL_NUMBER;
		return 1;
	}
	if (strcmp(normalized, "constant") == 0) {
		*class_out = EDITOR_SYNTAX_HL_CONSTANT;
		return 1;
	}
	if (strcmp(normalized, "variable") == 0) {
		*class_out = EDITOR_SYNTAX_HL_VARIABLE;
		return 1;
	}
	if (strcmp(normalized, "parameter") == 0 ||
			strcmp(normalized, "variable_parameter") == 0 ||
			strcmp(normalized, "variable.parameter") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PARAMETER;
		return 1;
	}
	if (strcmp(normalized, "module") == 0 || strcmp(normalized, "namespace") == 0) {
		*class_out = EDITOR_SYNTAX_HL_MODULE;
		return 1;
	}
	if (strcmp(normalized, "property") == 0 ||
			strcmp(normalized, "variable_member") == 0 ||
			strcmp(normalized, "variable.member") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PROPERTY;
		return 1;
	}
	if (strcmp(normalized, "preprocessor") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PREPROCESSOR;
		return 1;
	}
	if (strcmp(normalized, "operator") == 0) {
		*class_out = EDITOR_SYNTAX_HL_OPERATOR;
		return 1;
	}
	if (strcmp(normalized, "punctuation") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PUNCTUATION;
		return 1;
	}

	return 0;
}

static int editorParseHexNibble(char ch, unsigned char *out) {
	if (ch >= '0' && ch <= '9') {
		*out = (unsigned char)(ch - '0');
		return 1;
	}
	if (ch >= 'a' && ch <= 'f') {
		*out = (unsigned char)(10 + ch - 'a');
		return 1;
	}
	if (ch >= 'A' && ch <= 'F') {
		*out = (unsigned char)(10 + ch - 'A');
		return 1;
	}
	return 0;
}

static int editorParseHexByte(const char *value, unsigned char *out) {
	unsigned char hi = 0;
	unsigned char lo = 0;
	if (!editorParseHexNibble(value[0], &hi) || !editorParseHexNibble(value[1], &lo)) {
		return 0;
	}
	*out = (unsigned char)((hi << 4) | lo);
	return 1;
}

static int editorParseThemeColorValue(const char *value, struct editorThemeColor *color_out) {
	if (color_out == NULL || value == NULL) {
		return 0;
	}
	if (value[0] == '#' && strlen(value) == 7) {
		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;
		if (!editorParseHexByte(value + 1, &r) || !editorParseHexByte(value + 3, &g) ||
				!editorParseHexByte(value + 5, &b)) {
			return 0;
		}
		*color_out = editorThemeRgbColor(r, g, b);
		return 1;
	}

	char normalized[64];
	if (!editorNormalizeThemeToken(value, normalized, sizeof(normalized))) {
		return 0;
	}

	if (strcmp(normalized, "default") == 0) {
		*color_out = editorThemeDefaultColor();
		return 1;
	}
	if (strcmp(normalized, "black") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BLACK);
		return 1;
	}
	if (strcmp(normalized, "red") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_RED);
		return 1;
	}
	if (strcmp(normalized, "green") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_GREEN);
		return 1;
	}
	if (strcmp(normalized, "yellow") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_YELLOW);
		return 1;
	}
	if (strcmp(normalized, "blue") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BLUE);
		return 1;
	}
	if (strcmp(normalized, "magenta") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_MAGENTA);
		return 1;
	}
	if (strcmp(normalized, "cyan") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_CYAN);
		return 1;
	}
	if (strcmp(normalized, "white") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_WHITE);
		return 1;
	}
	if (strcmp(normalized, "bright_black") == 0 || strcmp(normalized, "gray") == 0 ||
			strcmp(normalized, "grey") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLACK);
		return 1;
	}
	if (strcmp(normalized, "bright_red") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_RED);
		return 1;
	}
	if (strcmp(normalized, "bright_green") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_GREEN);
		return 1;
	}
	if (strcmp(normalized, "bright_yellow") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_YELLOW);
		return 1;
	}
	if (strcmp(normalized, "bright_blue") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_BLUE);
		return 1;
	}
	if (strcmp(normalized, "bright_magenta") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_MAGENTA);
		return 1;
	}
	if (strcmp(normalized, "bright_cyan") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_CYAN);
		return 1;
	}
	if (strcmp(normalized, "bright_white") == 0) {
		*color_out = editorThemeAnsiColor(EDITOR_THEME_ANSI_BRIGHT_WHITE);
		return 1;
	}

	return 0;
}

static int editorParseThemeUiRoleName(const char *name, enum editorThemeUiRole *role_out,
		enum editorThemeStyleRole *style_out, int *is_style_fg_out, int *is_style_bg_out) {
	char normalized[64];
	if (!editorNormalizeThemeToken(name, normalized, sizeof(normalized))) {
		return 0;
	}
	if (role_out != NULL) {
		*role_out = EDITOR_THEME_UI_ROLE_COUNT;
	}
	if (style_out != NULL) {
		*style_out = EDITOR_THEME_STYLE_ROLE_COUNT;
	}
	if (is_style_fg_out != NULL) {
		*is_style_fg_out = 0;
	}
	if (is_style_bg_out != NULL) {
		*is_style_bg_out = 0;
	}

	if (strcmp(normalized, "foreground") == 0) {
		*role_out = EDITOR_THEME_UI_FOREGROUND;
		return 1;
	}
	if (strcmp(normalized, "background") == 0) {
		*role_out = EDITOR_THEME_UI_BACKGROUND;
		return 1;
	}
	if (strcmp(normalized, "line_number") == 0) {
		*role_out = EDITOR_THEME_UI_LINE_NUMBER;
		return 1;
	}
	if (strcmp(normalized, "drawer_connector") == 0) {
		*role_out = EDITOR_THEME_UI_DRAWER_CONNECTOR;
		return 1;
	}
	if (strcmp(normalized, "placeholder") == 0) {
		*role_out = EDITOR_THEME_UI_PLACEHOLDER;
		return 1;
	}
	if (strcmp(normalized, "current_line_bg") == 0) {
		*role_out = EDITOR_THEME_UI_CURRENT_LINE_BG;
		return 1;
	}
	if (strcmp(normalized, "drawer_header_bg") == 0) {
		*role_out = EDITOR_THEME_UI_DRAWER_HEADER_BG;
		return 1;
	}
	if (strcmp(normalized, "directory") == 0) {
		*role_out = EDITOR_THEME_UI_DIRECTORY;
		return 1;
	}
	if (strcmp(normalized, "root") == 0) {
		*role_out = EDITOR_THEME_UI_ROOT;
		return 1;
	}
	if (strcmp(normalized, "git_modified") == 0) {
		*role_out = EDITOR_THEME_UI_GIT_MODIFIED;
		return 1;
	}
	if (strcmp(normalized, "git_untracked") == 0) {
		*role_out = EDITOR_THEME_UI_GIT_UNTRACKED;
		return 1;
	}
	if (strcmp(normalized, "git_conflict") == 0) {
		*role_out = EDITOR_THEME_UI_GIT_CONFLICT;
		return 1;
	}
	if (strcmp(normalized, "cursor") == 0) {
		*role_out = EDITOR_THEME_UI_CURSOR;
		return 1;
	}

	struct {
		const char *fg;
		const char *bg;
		enum editorThemeStyleRole role;
	} style_names[] = {
		{"selection_fg", "selection_bg", EDITOR_THEME_STYLE_SELECTION},
		{"status_fg", "status_bg", EDITOR_THEME_STYLE_STATUS},
		{"tab_active_fg", "tab_active_bg", EDITOR_THEME_STYLE_TAB_ACTIVE},
		{"drawer_header_active_fg", "drawer_header_active_bg",
				EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE},
	};
	for (size_t i = 0; i < sizeof(style_names) / sizeof(style_names[0]); i++) {
		if (strcmp(normalized, style_names[i].fg) == 0) {
			*style_out = style_names[i].role;
			*is_style_fg_out = 1;
			return 1;
		}
		if (strcmp(normalized, style_names[i].bg) == 0) {
			*style_out = style_names[i].role;
			*is_style_bg_out = 1;
			return 1;
		}
	}

	return 0;
}

static int editorThemeParseKeyValue(char *trimmed, char **key_out, char **value_out) {
	char *eq = strchr(trimmed, '=');
	if (eq == NULL) {
		return 0;
	}
	*eq = '\0';
	char *key = editorConfigTrimLeft(trimmed);
	editorConfigTrimRight(key);
	char *value = editorConfigTrimLeft(eq + 1);
	if (key[0] == '\0') {
		return 0;
	}
	*key_out = key;
	*value_out = value;
	return 1;
}

static void editorThemeApplyStyleColor(struct editorTheme *theme, enum editorThemeStyleRole role,
		int is_fg, struct editorThemeColor color) {
	if (role < 0 || role >= EDITOR_THEME_STYLE_ROLE_COUNT) {
		return;
	}
	if (is_fg) {
		theme->styles[role].fg = color;
	} else {
		theme->styles[role].bg = color;
	}
	theme->styles[role].reverse = 0;
}

static void editorThemeParseEntry(struct editorTheme *theme, struct editorThemeParseContext *ctx,
		char *trimmed) {
	char *key = NULL;
	char *value = NULL;
	if (!editorThemeParseKeyValue(trimmed, &key, &value)) {
		ctx->had_invalid = 1;
		return;
	}

	char parsed[64];
	if (!editorConfigParseQuotedValue(value, parsed, sizeof(parsed))) {
		ctx->had_invalid = 1;
		return;
	}

	if (ctx->is_theme_file && !ctx->in_theme_syntax_table && !ctx->in_theme_ui_table) {
		if (strcmp(key, "name") == 0) {
			if (!editorThemeNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return;
			}
			(void)snprintf(ctx->theme_name, sizeof(ctx->theme_name), "%s", parsed);
			ctx->saw_theme_name = 1;
			return;
		}
		if (strcmp(key, "inherits") == 0) {
			if (!editorThemeNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return;
			}
			(void)snprintf(ctx->inherits_name, sizeof(ctx->inherits_name), "%s", parsed);
			return;
		}
		ctx->had_invalid = 1;
		return;
	}

	if (!ctx->is_theme_file) {
		if (ctx->in_theme_table && strcmp(key, "name") == 0) {
			if (!editorThemeNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return;
			}
			(void)snprintf(ctx->selected_name, sizeof(ctx->selected_name), "%s", parsed);
			return;
		}
		ctx->had_invalid = 1;
		return;
	}

	struct editorThemeColor color = editorThemeDefaultColor();
	if (!editorParseThemeColorValue(parsed, &color)) {
		ctx->had_invalid = 1;
		return;
	}

	if (ctx->in_theme_syntax_table) {
		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		if (!editorParseSyntaxHighlightClassName(key, &highlight_class) ||
				highlight_class == EDITOR_SYNTAX_HL_NONE) {
			ctx->had_invalid = 1;
			return;
		}
		theme->syntax[highlight_class] = color;
		return;
	}

	if (ctx->in_theme_ui_table) {
		enum editorThemeUiRole role = EDITOR_THEME_UI_ROLE_COUNT;
		enum editorThemeStyleRole style = EDITOR_THEME_STYLE_ROLE_COUNT;
		int is_style_fg = 0;
		int is_style_bg = 0;
		if (!editorParseThemeUiRoleName(key, &role, &style, &is_style_fg, &is_style_bg)) {
			ctx->had_invalid = 1;
			return;
		}
		if (role < EDITOR_THEME_UI_ROLE_COUNT) {
			theme->ui[role] = color;
		} else if (style < EDITOR_THEME_STYLE_ROLE_COUNT) {
			editorThemeApplyStyleColor(theme, style, is_style_fg, color);
		}
		return;
	}

	ctx->had_invalid = 1;
}

static int editorThemeParseTable(struct editorThemeParseContext *ctx, char *trimmed) {
	char *close = strchr(trimmed, ']');
	if (close == NULL) {
		return 0;
	}
	*close = '\0';
	char *table = editorConfigTrimLeft(trimmed + 1);
	editorConfigTrimRight(table);
	char *tail = editorConfigTrimLeft(close + 1);
	if (tail[0] != '\0') {
		return 0;
	}

	ctx->in_theme_table = strcmp(table, "theme") == 0;
	ctx->in_theme_syntax_table = strcmp(table, "theme.syntax") == 0;
	ctx->in_theme_ui_table = strcmp(table, "theme.ui") == 0;
	if (!ctx->is_theme_file && (ctx->in_theme_syntax_table || ctx->in_theme_ui_table)) {
		ctx->had_invalid = 1;
	}
	return 1;
}

static enum editorThemeFileStatus editorThemeApplyFile(struct editorTheme *theme, const char *path,
		int is_theme_file, struct editorThemeParseContext *ctx_out) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_THEME_FILE_MISSING;
		}
		if (ctx_out != NULL) {
			ctx_out->had_invalid = 1;
		}
		return EDITOR_THEME_FILE_APPLIED;
	}

	struct editorThemeParseContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.is_theme_file = is_theme_file;
	(void)snprintf(ctx.inherits_name, sizeof(ctx.inherits_name), "%s", "terminal");

	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			ctx.had_invalid = 1;
			int ch = 0;
			while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
				;
			}
			continue;
		}

		editorConfigStripInlineComment(line);
		editorConfigTrimRight(line);
		char *trimmed = editorConfigTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}
		if (trimmed[0] == '[') {
			if (!editorThemeParseTable(&ctx, trimmed)) {
				ctx.had_invalid = 1;
			}
			continue;
		}
		editorThemeParseEntry(theme, &ctx, trimmed);
	}

	if (ferror(fp)) {
		ctx.had_invalid = 1;
	}
	fclose(fp);

	if (ctx_out != NULL) {
		*ctx_out = ctx;
	}
	return EDITOR_THEME_FILE_APPLIED;
}

static enum editorThemeLoadStatus editorThemeApplyConfigSelector(const char *path,
		char selected_name[64], enum editorThemeLoadStatus invalid_bit) {
	if (path == NULL) {
		return EDITOR_THEME_LOAD_OK;
	}
	struct editorTheme scratch;
	editorThemeInitDefault(&scratch);
	struct editorThemeParseContext ctx;
	enum editorThemeFileStatus file_status = editorThemeApplyFile(&scratch, path, 0, &ctx);
	if (file_status == EDITOR_THEME_FILE_MISSING) {
		return EDITOR_THEME_LOAD_OK;
	}
	if (file_status == EDITOR_THEME_FILE_OUT_OF_MEMORY) {
		return EDITOR_THEME_LOAD_OUT_OF_MEMORY;
	}
	enum editorThemeLoadStatus status = EDITOR_THEME_LOAD_OK;
	if (ctx.had_invalid) {
		status = (enum editorThemeLoadStatus)(status | invalid_bit);
	}
	if (ctx.selected_name[0] != '\0') {
		(void)snprintf(selected_name, 64, "%s", ctx.selected_name);
	}
	return status;
}

static char *editorThemeBuildCustomThemePath(const char *home_dir, const char *name) {
	if (home_dir == NULL || home_dir[0] == '\0' || name == NULL || !editorThemeNameIsValid(name)) {
		return NULL;
	}
	const char *middle = "/.rotide/themes/";
	const char *suffix = ".toml";
	size_t len = strlen(home_dir) + strlen(middle) + strlen(name) + strlen(suffix) + 1;
	char *path = malloc(len);
	if (path == NULL) {
		return NULL;
	}
	int written = snprintf(path, len, "%s%s%s%s", home_dir, middle, name, suffix);
	if (written < 0 || (size_t)written >= len) {
		free(path);
		return NULL;
	}
	return path;
}

static enum editorThemeLoadStatus editorThemeLoadCustom(struct editorTheme *theme_out,
		const char *name, const char *home_dir) {
	char *path = editorThemeBuildCustomThemePath(home_dir, name);
	if (path == NULL) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}

	struct editorTheme parsed;
	editorThemeInitDefault(&parsed);
	struct editorThemeParseContext ctx;
	enum editorThemeFileStatus file_status = editorThemeApplyFile(&parsed, path, 1, &ctx);
	free(path);
	if (file_status != EDITOR_THEME_FILE_APPLIED || ctx.had_invalid) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	if (!editorThemeInitBuiltin(&parsed, ctx.inherits_name)) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	path = editorThemeBuildCustomThemePath(home_dir, name);
	if (path == NULL) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	file_status = editorThemeApplyFile(&parsed, path, 1, &ctx);
	free(path);
	if (file_status != EDITOR_THEME_FILE_APPLIED || ctx.had_invalid) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	if (ctx.saw_theme_name && strcmp(ctx.theme_name, name) != 0) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	editorThemeSetName(&parsed, name);
	*theme_out = parsed;
	return EDITOR_THEME_LOAD_OK;
}

static enum editorThemeLoadStatus editorThemeLoadNamed(struct editorTheme *theme_out,
		const char *name, const char *home_dir) {
	if (editorThemeInitBuiltin(theme_out, name)) {
		return EDITOR_THEME_LOAD_OK;
	}
	return editorThemeLoadCustom(theme_out, name, home_dir);
}

enum editorThemeLoadStatus editorThemeLoadFromPaths(struct editorTheme *theme_out,
		const char *global_path, const char *project_path, const char *home_dir) {
	if (theme_out == NULL) {
		return EDITOR_THEME_LOAD_OUT_OF_MEMORY;
	}

	char selected_name[64];
	(void)snprintf(selected_name, sizeof(selected_name), "%s", "terminal");
	enum editorThemeLoadStatus status = EDITOR_THEME_LOAD_OK;
	status = (enum editorThemeLoadStatus)(status |
			editorThemeApplyConfigSelector(global_path, selected_name,
					EDITOR_THEME_LOAD_INVALID_GLOBAL));
	if ((status & EDITOR_THEME_LOAD_OUT_OF_MEMORY) != 0) {
		editorThemeInitDefault(theme_out);
		return status;
	}
	status = (enum editorThemeLoadStatus)(status |
			editorThemeApplyConfigSelector(project_path, selected_name,
					EDITOR_THEME_LOAD_INVALID_PROJECT));
	if ((status & EDITOR_THEME_LOAD_OUT_OF_MEMORY) != 0) {
		editorThemeInitDefault(theme_out);
		return status;
	}

	enum editorThemeLoadStatus theme_status =
			editorThemeLoadNamed(theme_out, selected_name, home_dir);
	status = (enum editorThemeLoadStatus)(status | theme_status);
	return status;
}

enum editorThemeLoadStatus editorThemeLoadConfigured(struct editorTheme *theme_out) {
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorThemeLoadFromPaths(theme_out, NULL, ".rotide.toml", NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_OUT_OF_MEMORY;
	}

	enum editorThemeLoadStatus status =
			editorThemeLoadFromPaths(theme_out, global_path, ".rotide.toml", home);
	free(global_path);
	return status;
}
