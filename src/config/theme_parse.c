/* Custom-theme TOML parser and load orchestration.
 *
 * Owns the parser for ~/.rotide/themes TOML files and the
 * editorThemeLoadFromPaths / editorThemeLoadConfigured entry points
 * that the runtime calls to apply the configured theme. Built-in
 * theme tables live in theme_builtin.c — this module reads them via
 * editorThemeInitBuiltin and overlays user customizations on top.
 */
#include "config/common.h"
#include "config/theme_config.h"
#include "config/theme_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum themeParseFileStatus {
	THEME_PARSE_FILE_APPLIED = 0,
	THEME_PARSE_FILE_MISSING,
	THEME_PARSE_FILE_OUT_OF_MEMORY
};

struct themeParseContext {
	int is_theme_file;
	int in_theme_table;
	int in_theme_syntax_table;
	int in_theme_ui_table;
	int in_theme_ansi_table;
	int had_invalid;
	char selected_name[64];
	char theme_name[64];
	char inherits_name[64];
	int saw_theme_name;
};

static int themeParseNormalizeToken(const char *token, char *out, size_t out_size) {
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

static int themeParseNameIsValid(const char *name) {
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

static int themeParseSyntaxHighlightClassName(const char *name,
                                              enum editorSyntaxHighlightClass *class_out) {
	char normalized[64];
	if (class_out == NULL || !themeParseNormalizeToken(name, normalized, sizeof(normalized))) {
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
	if (strcmp(normalized, "parameter") == 0 || strcmp(normalized, "variable_parameter") == 0 ||
	    strcmp(normalized, "variable.parameter") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PARAMETER;
		return 1;
	}
	if (strcmp(normalized, "module") == 0 || strcmp(normalized, "namespace") == 0) {
		*class_out = EDITOR_SYNTAX_HL_MODULE;
		return 1;
	}
	if (strcmp(normalized, "property") == 0 || strcmp(normalized, "variable_member") == 0 ||
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

static int themeParseHexNibble(char ch, unsigned char *out) {
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

static int themeParseHexByte(const char *value, unsigned char *out) {
	unsigned char hi = 0;
	unsigned char lo = 0;
	if (!themeParseHexNibble(value[0], &hi) || !themeParseHexNibble(value[1], &lo)) {
		return 0;
	}
	*out = (unsigned char)((hi << 4) | lo);
	return 1;
}

static int themeParseColorValue(const char *value, struct editorThemeColor *color_out) {
	if (color_out == NULL || value == NULL) {
		return 0;
	}
	if (value[0] == '#' && strlen(value) == 7) {
		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;
		if (!themeParseHexByte(value + 1, &r) || !themeParseHexByte(value + 3, &g) ||
		    !themeParseHexByte(value + 5, &b)) {
			return 0;
		}
		*color_out = editorThemeRgbColor(r, g, b);
		return 1;
	}

	char normalized[64];
	if (!themeParseNormalizeToken(value, normalized, sizeof(normalized))) {
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

static int themeParseUiRoleName(const char *name, enum editorThemeUiRole *role_out,
                                enum editorThemeStyleRole *style_out, int *is_style_fg_out,
                                int *is_style_bg_out) {
	char normalized[64];
	if (!themeParseNormalizeToken(name, normalized, sizeof(normalized))) {
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
	if (strcmp(normalized, "drawer_icon") == 0) {
		*role_out = EDITOR_THEME_UI_DRAWER_ICON;
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

static int themeParseKeyValue(char *trimmed, char **key_out, char **value_out) {
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

static int themeParseAnsiSlotName(const char *name, enum editorThemeAnsiColor *slot_out) {
	char normalized[64];
	if (!themeParseNormalizeToken(name, normalized, sizeof(normalized))) {
		return 0;
	}
	static const struct {
		const char *name;
		enum editorThemeAnsiColor slot;
	} slot_names[] = {
	        {"black", EDITOR_THEME_ANSI_BLACK},
	        {"red", EDITOR_THEME_ANSI_RED},
	        {"green", EDITOR_THEME_ANSI_GREEN},
	        {"yellow", EDITOR_THEME_ANSI_YELLOW},
	        {"blue", EDITOR_THEME_ANSI_BLUE},
	        {"magenta", EDITOR_THEME_ANSI_MAGENTA},
	        {"cyan", EDITOR_THEME_ANSI_CYAN},
	        {"white", EDITOR_THEME_ANSI_WHITE},
	        {"bright_black", EDITOR_THEME_ANSI_BRIGHT_BLACK},
	        {"gray", EDITOR_THEME_ANSI_BRIGHT_BLACK},
	        {"grey", EDITOR_THEME_ANSI_BRIGHT_BLACK},
	        {"bright_red", EDITOR_THEME_ANSI_BRIGHT_RED},
	        {"bright_green", EDITOR_THEME_ANSI_BRIGHT_GREEN},
	        {"bright_yellow", EDITOR_THEME_ANSI_BRIGHT_YELLOW},
	        {"bright_blue", EDITOR_THEME_ANSI_BRIGHT_BLUE},
	        {"bright_magenta", EDITOR_THEME_ANSI_BRIGHT_MAGENTA},
	        {"bright_cyan", EDITOR_THEME_ANSI_BRIGHT_CYAN},
	        {"bright_white", EDITOR_THEME_ANSI_BRIGHT_WHITE},
	};
	for (size_t i = 0; i < sizeof(slot_names) / sizeof(slot_names[0]); i++) {
		if (strcmp(normalized, slot_names[i].name) == 0) {
			*slot_out = slot_names[i].slot;
			return 1;
		}
	}
	return 0;
}

static void themeParseApplyStyleColor(struct editorTheme *theme, enum editorThemeStyleRole role,
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

static void themeParseEntry(struct editorTheme *theme, struct themeParseContext *ctx,
                            char *trimmed) {
	if (!ctx->is_theme_file && !ctx->in_theme_table) {
		return;
	}

	char *key = NULL;
	char *value = NULL;
	if (!themeParseKeyValue(trimmed, &key, &value)) {
		ctx->had_invalid = 1;
		return;
	}

	char parsed[64];
	if (!editorConfigParseQuotedValue(value, parsed, sizeof(parsed))) {
		ctx->had_invalid = 1;
		return;
	}

	if (ctx->is_theme_file && !ctx->in_theme_syntax_table && !ctx->in_theme_ui_table &&
	    !ctx->in_theme_ansi_table) {
		if (strcmp(key, "name") == 0) {
			if (!themeParseNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return;
			}
			(void)snprintf(ctx->theme_name, sizeof(ctx->theme_name), "%s", parsed);
			ctx->saw_theme_name = 1;
			return;
		}
		if (strcmp(key, "inherits") == 0) {
			if (!themeParseNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return;
			}
			(void)snprintf(ctx->inherits_name, sizeof(ctx->inherits_name), "%s",
			               parsed);
			return;
		}
		ctx->had_invalid = 1;
		return;
	}

	if (!ctx->is_theme_file) {
		if (strcmp(key, "name") == 0) {
			if (!themeParseNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return;
			}
			(void)snprintf(ctx->selected_name, sizeof(ctx->selected_name), "%s",
			               parsed);
			return;
		}
		ctx->had_invalid = 1;
		return;
	}

	struct editorThemeColor color = editorThemeDefaultColor();
	if (!themeParseColorValue(parsed, &color)) {
		ctx->had_invalid = 1;
		return;
	}

	if (ctx->in_theme_syntax_table) {
		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		if (!themeParseSyntaxHighlightClassName(key, &highlight_class) ||
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
		if (!themeParseUiRoleName(key, &role, &style, &is_style_fg, &is_style_bg)) {
			ctx->had_invalid = 1;
			return;
		}
		if (role < EDITOR_THEME_UI_ROLE_COUNT) {
			theme->ui[role] = color;
		} else if (style < EDITOR_THEME_STYLE_ROLE_COUNT) {
			themeParseApplyStyleColor(theme, style, is_style_fg, color);
		}
		return;
	}

	if (ctx->in_theme_ansi_table) {
		enum editorThemeAnsiColor slot = EDITOR_THEME_ANSI_COUNT;
		if (!themeParseAnsiSlotName(key, &slot) || slot >= EDITOR_THEME_ANSI_COUNT) {
			ctx->had_invalid = 1;
			return;
		}
		theme->ansi[slot] = color;
		return;
	}

	ctx->had_invalid = 1;
}

static int themeParseTable(struct themeParseContext *ctx, char *trimmed) {
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
	ctx->in_theme_ansi_table = strcmp(table, "theme.ansi") == 0;
	if (!ctx->is_theme_file &&
	    (ctx->in_theme_syntax_table || ctx->in_theme_ui_table || ctx->in_theme_ansi_table)) {
		ctx->had_invalid = 1;
	}
	return 1;
}

/* Drain a theme TOML stream into `theme`. Caller owns `fp` (close it
 * after the call). Always returns THEME_PARSE_FILE_APPLIED — caller
 * inspects `ctx_out->had_invalid` for parse errors. Internal to this
 * TU; the fuzz harness reaches it via the ROTIDE_FUZZ wrapper at the
 * bottom of the file. */
static enum themeParseFileStatus themeParseApplyStream(struct editorTheme *theme, FILE *fp,
                                                       int is_theme_file,
                                                       struct themeParseContext *ctx_out) {
	struct themeParseContext ctx;
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
			if (!themeParseTable(&ctx, trimmed)) {
				ctx.had_invalid = 1;
			}
			continue;
		}
		themeParseEntry(theme, &ctx, trimmed);
	}

	if (ferror(fp)) {
		ctx.had_invalid = 1;
	}

	if (ctx_out != NULL) {
		*ctx_out = ctx;
	}
	return THEME_PARSE_FILE_APPLIED;
}

static enum themeParseFileStatus themeParseApplyFile(struct editorTheme *theme, const char *path,
                                                     int is_theme_file,
                                                     struct themeParseContext *ctx_out) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return THEME_PARSE_FILE_MISSING;
		}
		if (ctx_out != NULL) {
			ctx_out->had_invalid = 1;
		}
		return THEME_PARSE_FILE_APPLIED;
	}

	enum themeParseFileStatus status = themeParseApplyStream(theme, fp, is_theme_file, ctx_out);
	(void)fclose(fp);
	return status;
}

static enum editorThemeLoadStatus
themeParseApplyConfigSelector(const char *path, char selected_name[64],
                              enum editorThemeLoadStatus invalid_bit) {
	if (path == NULL) {
		return EDITOR_THEME_LOAD_OK;
	}
	struct editorTheme scratch;
	editorThemeInitDefault(&scratch);
	struct themeParseContext ctx;
	enum themeParseFileStatus file_status = themeParseApplyFile(&scratch, path, 0, &ctx);
	if (file_status == THEME_PARSE_FILE_MISSING) {
		return EDITOR_THEME_LOAD_OK;
	}
	if (file_status == THEME_PARSE_FILE_OUT_OF_MEMORY) {
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

static char *themeParseBuildCustomThemePath(const char *home_dir, const char *name) {
	if (home_dir == NULL || home_dir[0] == '\0' || name == NULL ||
	    !themeParseNameIsValid(name)) {
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

static enum editorThemeLoadStatus themeParseLoadCustom(struct editorTheme *theme_out,
                                                       const char *name, const char *home_dir) {
	char *path = themeParseBuildCustomThemePath(home_dir, name);
	if (path == NULL) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}

	struct editorTheme parsed;
	editorThemeInitDefault(&parsed);
	struct themeParseContext ctx;
	enum themeParseFileStatus file_status = themeParseApplyFile(&parsed, path, 1, &ctx);
	free(path);
	if (file_status != THEME_PARSE_FILE_APPLIED || ctx.had_invalid) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	if (!editorThemeInitBuiltin(&parsed, ctx.inherits_name)) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	path = themeParseBuildCustomThemePath(home_dir, name);
	if (path == NULL) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_INVALID_THEME;
	}
	file_status = themeParseApplyFile(&parsed, path, 1, &ctx);
	free(path);
	if (file_status != THEME_PARSE_FILE_APPLIED || ctx.had_invalid) {
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

static enum editorThemeLoadStatus themeParseLoadNamed(struct editorTheme *theme_out,
                                                      const char *name, const char *home_dir) {
	if (editorThemeInitBuiltin(theme_out, name)) {
		return EDITOR_THEME_LOAD_OK;
	}
	return themeParseLoadCustom(theme_out, name, home_dir);
}

enum editorThemeLoadStatus editorThemeLoadFromPaths(struct editorTheme *theme_out,
                                                    const char *global_path,
                                                    const char *project_path,
                                                    const char *home_dir) {
	if (theme_out == NULL) {
		return EDITOR_THEME_LOAD_OUT_OF_MEMORY;
	}

	char selected_name[64];
	(void)snprintf(selected_name, sizeof(selected_name), "%s", "terminal");
	enum editorThemeLoadStatus status = EDITOR_THEME_LOAD_OK;
	status = (enum editorThemeLoadStatus)(
	        status | themeParseApplyConfigSelector(global_path, selected_name,
	                                               EDITOR_THEME_LOAD_INVALID_GLOBAL));
	if ((status & EDITOR_THEME_LOAD_OUT_OF_MEMORY) != 0) {
		editorThemeInitDefault(theme_out);
		return status;
	}
	status = (enum editorThemeLoadStatus)(
	        status | themeParseApplyConfigSelector(project_path, selected_name,
	                                               EDITOR_THEME_LOAD_INVALID_PROJECT));
	if ((status & EDITOR_THEME_LOAD_OUT_OF_MEMORY) != 0) {
		editorThemeInitDefault(theme_out);
		return status;
	}

	enum editorThemeLoadStatus theme_status =
	        themeParseLoadNamed(theme_out, selected_name, home_dir);
	status = (enum editorThemeLoadStatus)(status | theme_status);
	return status;
}

enum editorThemeLoadStatus editorThemeLoadConfigured(struct editorTheme *theme_out) {
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorThemeLoadFromPaths(theme_out, NULL, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorThemeInitDefault(theme_out);
		return EDITOR_THEME_LOAD_OUT_OF_MEMORY;
	}

	enum editorThemeLoadStatus status =
	        editorThemeLoadFromPaths(theme_out, global_path, NULL, home);
	free(global_path);
	return status;
}

#ifdef ROTIDE_FUZZ
/* Fuzz-only entry. Drives `themeParseApplyStream` over an arbitrary
 * byte stream so the libFuzzer harness in tests/fuzz/toml/ does not
 * need to see the private parse-context / file-status types. */
void editorThemeApplyStreamFuzz(FILE *fp) {
	struct editorTheme scratch;
	editorThemeInitDefault(&scratch);
	struct themeParseContext ctx;
	(void)themeParseApplyStream(&scratch, fp, 1, &ctx);
}
#endif
