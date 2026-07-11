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
#include "language/syntax.h"

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

struct themeParseDriver {
	struct editorTheme *theme;
	struct themeParseContext *ctx;
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

	static const struct {
		const char *name;
		enum editorSyntaxHighlightClass highlight_class;
	} class_names[] = {
	        {"comment", EDITOR_SYNTAX_HL_COMMENT},
	        {"keyword", EDITOR_SYNTAX_HL_KEYWORD},
	        {"type", EDITOR_SYNTAX_HL_TYPE},
	        {"function", EDITOR_SYNTAX_HL_FUNCTION},
	        {"string", EDITOR_SYNTAX_HL_STRING},
	        {"number", EDITOR_SYNTAX_HL_NUMBER},
	        {"constant", EDITOR_SYNTAX_HL_CONSTANT},
	        {"variable", EDITOR_SYNTAX_HL_VARIABLE},
	        {"parameter", EDITOR_SYNTAX_HL_PARAMETER},
	        {"variable_parameter", EDITOR_SYNTAX_HL_PARAMETER},
	        {"variable.parameter", EDITOR_SYNTAX_HL_PARAMETER},
	        {"module", EDITOR_SYNTAX_HL_MODULE},
	        {"namespace", EDITOR_SYNTAX_HL_MODULE},
	        {"property", EDITOR_SYNTAX_HL_PROPERTY},
	        {"variable_member", EDITOR_SYNTAX_HL_PROPERTY},
	        {"variable.member", EDITOR_SYNTAX_HL_PROPERTY},
	        {"preprocessor", EDITOR_SYNTAX_HL_PREPROCESSOR},
	        {"operator", EDITOR_SYNTAX_HL_OPERATOR},
	        {"punctuation", EDITOR_SYNTAX_HL_PUNCTUATION},
	};
	for (size_t i = 0; i < sizeof(class_names) / sizeof(class_names[0]); i++) {
		if (strcmp(normalized, class_names[i].name) == 0) {
			*class_out = class_names[i].highlight_class;
			return 1;
		}
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

	static const struct {
		const char *name;
		enum editorThemeUiRole role;
	} role_names[] = {
	        {"foreground", EDITOR_THEME_UI_FOREGROUND},
	        {"background", EDITOR_THEME_UI_BACKGROUND},
	        {"line_number", EDITOR_THEME_UI_LINE_NUMBER},
	        {"drawer_connector", EDITOR_THEME_UI_DRAWER_CONNECTOR},
	        {"drawer_icon", EDITOR_THEME_UI_DRAWER_ICON},
	        {"placeholder", EDITOR_THEME_UI_PLACEHOLDER},
	        {"current_line_bg", EDITOR_THEME_UI_CURRENT_LINE_BG},
	        {"drawer_header_bg", EDITOR_THEME_UI_DRAWER_HEADER_BG},
	        {"directory", EDITOR_THEME_UI_DIRECTORY},
	        {"root", EDITOR_THEME_UI_ROOT},
	        {"git_modified", EDITOR_THEME_UI_GIT_MODIFIED},
	        {"git_added", EDITOR_THEME_UI_GIT_ADDED},
	        {"git_deleted", EDITOR_THEME_UI_GIT_DELETED},
	        {"git_untracked", EDITOR_THEME_UI_GIT_UNTRACKED},
	        {"git_conflict", EDITOR_THEME_UI_GIT_CONFLICT},
	        {"cursor", EDITOR_THEME_UI_CURSOR},
	        {"breakpoint", EDITOR_THEME_UI_BREAKPOINT},
	        {"debug_stopped_line", EDITOR_THEME_UI_DEBUG_STOPPED_LINE},
	        {"debug_stopped_line_bg", EDITOR_THEME_UI_DEBUG_STOPPED_LINE_BG},
	        {"diff_added_bg", EDITOR_THEME_UI_DIFF_ADDED_BG},
	        {"diff_removed_bg", EDITOR_THEME_UI_DIFF_REMOVED_BG},
	};
	for (size_t i = 0; i < sizeof(role_names) / sizeof(role_names[0]); i++) {
		if (strcmp(normalized, role_names[i].name) == 0) {
			*role_out = role_names[i].role;
			return 1;
		}
	}

	static const struct {
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

static int themeParseOnEntry(void *vdriver, const char *key, char *value) {
	struct themeParseDriver *driver = vdriver;
	struct editorTheme *theme = driver->theme;
	struct themeParseContext *ctx = driver->ctx;

	char parsed[64];
	if (!editorConfigParseQuotedValue(value, parsed, sizeof(parsed))) {
		ctx->had_invalid = 1;
		return 1;
	}

	if (ctx->is_theme_file && !ctx->in_theme_syntax_table && !ctx->in_theme_ui_table &&
	    !ctx->in_theme_ansi_table) {
		if (strcmp(key, "name") == 0) {
			if (!themeParseNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return 1;
			}
			(void)snprintf(ctx->theme_name, sizeof(ctx->theme_name), "%s", parsed);
			ctx->saw_theme_name = 1;
			return 1;
		}
		if (strcmp(key, "inherits") == 0) {
			if (!themeParseNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return 1;
			}
			(void)snprintf(ctx->inherits_name, sizeof(ctx->inherits_name), "%s",
			               parsed);
			return 1;
		}
		ctx->had_invalid = 1;
		return 1;
	}

	if (!ctx->is_theme_file) {
		if (strcmp(key, "name") == 0) {
			if (!themeParseNameIsValid(parsed)) {
				ctx->had_invalid = 1;
				return 1;
			}
			(void)snprintf(ctx->selected_name, sizeof(ctx->selected_name), "%s",
			               parsed);
			return 1;
		}
		ctx->had_invalid = 1;
		return 1;
	}

	struct editorThemeColor color = editorThemeDefaultColor();
	if (!themeParseColorValue(parsed, &color)) {
		ctx->had_invalid = 1;
		return 1;
	}

	if (ctx->in_theme_syntax_table) {
		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		if (!themeParseSyntaxHighlightClassName(key, &highlight_class) ||
		    highlight_class == EDITOR_SYNTAX_HL_NONE) {
			ctx->had_invalid = 1;
			return 1;
		}
		theme->syntax[highlight_class] = color;
		return 1;
	}

	if (ctx->in_theme_ui_table) {
		enum editorThemeUiRole role = EDITOR_THEME_UI_ROLE_COUNT;
		enum editorThemeStyleRole style = EDITOR_THEME_STYLE_ROLE_COUNT;
		int is_style_fg = 0;
		int is_style_bg = 0;
		if (!themeParseUiRoleName(key, &role, &style, &is_style_fg, &is_style_bg)) {
			ctx->had_invalid = 1;
			return 1;
		}
		if (role < EDITOR_THEME_UI_ROLE_COUNT) {
			theme->ui[role] = color;
		} else if (style < EDITOR_THEME_STYLE_ROLE_COUNT) {
			themeParseApplyStyleColor(theme, style, is_style_fg, color);
		}
		return 1;
	}

	if (ctx->in_theme_ansi_table) {
		enum editorThemeAnsiColor slot = EDITOR_THEME_ANSI_COUNT;
		if (!themeParseAnsiSlotName(key, &slot) || slot >= EDITOR_THEME_ANSI_COUNT) {
			ctx->had_invalid = 1;
			return 1;
		}
		theme->ansi[slot] = color;
		return 1;
	}

	ctx->had_invalid = 1;
	return 1;
}

static int themeParseOnSection(void *vdriver, const char *table) {
	struct themeParseContext *ctx = ((struct themeParseDriver *)vdriver)->ctx;
	ctx->in_theme_table = strcmp(table, "theme") == 0;
	ctx->in_theme_syntax_table = strcmp(table, "theme.syntax") == 0;
	ctx->in_theme_ui_table = strcmp(table, "theme.ui") == 0;
	ctx->in_theme_ansi_table = strcmp(table, "theme.ansi") == 0;
	if (!ctx->is_theme_file &&
	    (ctx->in_theme_syntax_table || ctx->in_theme_ui_table || ctx->in_theme_ansi_table)) {
		ctx->had_invalid = 1;
	}
	/* Theme files route every section's entries (top-level name/inherits and the
	 * color sub-tables); config files only read the [theme] selector. */
	return ctx->is_theme_file || ctx->in_theme_table;
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

	struct themeParseDriver driver = {.theme = theme, .ctx = &ctx};
	struct editorConfigScanner scanner = {themeParseOnSection, themeParseOnEntry};
	if (editorConfigScanStream(fp, &scanner, &driver) != EDITOR_CONFIG_SCAN_OK) {
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
