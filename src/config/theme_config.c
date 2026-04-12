#include "config/theme_config.h"

#include "config/common.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum editorSyntaxThemeFileStatus {
	EDITOR_SYNTAX_THEME_FILE_APPLIED = 0,
	EDITOR_SYNTAX_THEME_FILE_MISSING,
	EDITOR_SYNTAX_THEME_FILE_OUT_OF_MEMORY
};

void editorSyntaxThemeInitDefaults(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]) {
	if (theme_out == NULL) {
		return;
	}

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme_out[i] = EDITOR_THEME_COLOR_DEFAULT;
	}

	theme_out[EDITOR_SYNTAX_HL_COMMENT] = EDITOR_THEME_COLOR_BRIGHT_BLACK;
	theme_out[EDITOR_SYNTAX_HL_KEYWORD] = EDITOR_THEME_COLOR_BRIGHT_BLUE;
	theme_out[EDITOR_SYNTAX_HL_TYPE] = EDITOR_THEME_COLOR_BRIGHT_CYAN;
	theme_out[EDITOR_SYNTAX_HL_FUNCTION] = EDITOR_THEME_COLOR_BRIGHT_YELLOW;
	theme_out[EDITOR_SYNTAX_HL_STRING] = EDITOR_THEME_COLOR_GREEN;
	theme_out[EDITOR_SYNTAX_HL_NUMBER] = EDITOR_THEME_COLOR_MAGENTA;
	theme_out[EDITOR_SYNTAX_HL_CONSTANT] = EDITOR_THEME_COLOR_BRIGHT_MAGENTA;
	theme_out[EDITOR_SYNTAX_HL_PREPROCESSOR] = EDITOR_THEME_COLOR_BRIGHT_RED;
	theme_out[EDITOR_SYNTAX_HL_OPERATOR] = EDITOR_THEME_COLOR_BRIGHT_WHITE;
	theme_out[EDITOR_SYNTAX_HL_PUNCTUATION] = EDITOR_THEME_COLOR_DEFAULT;
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

static int editorParseSyntaxHighlightClassName(const char *name,
		enum editorSyntaxHighlightClass *class_out) {
	char normalized[64];
	if (!editorNormalizeThemeToken(name, normalized, sizeof(normalized))) {
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

static int editorParseThemeColorValue(const char *value, enum editorThemeColor *color_out) {
	char normalized[64];
	if (!editorNormalizeThemeToken(value, normalized, sizeof(normalized))) {
		return 0;
	}

	if (strcmp(normalized, "default") == 0) {
		*color_out = EDITOR_THEME_COLOR_DEFAULT;
		return 1;
	}
	if (strcmp(normalized, "black") == 0) {
		*color_out = EDITOR_THEME_COLOR_BLACK;
		return 1;
	}
	if (strcmp(normalized, "red") == 0) {
		*color_out = EDITOR_THEME_COLOR_RED;
		return 1;
	}
	if (strcmp(normalized, "green") == 0) {
		*color_out = EDITOR_THEME_COLOR_GREEN;
		return 1;
	}
	if (strcmp(normalized, "yellow") == 0) {
		*color_out = EDITOR_THEME_COLOR_YELLOW;
		return 1;
	}
	if (strcmp(normalized, "blue") == 0) {
		*color_out = EDITOR_THEME_COLOR_BLUE;
		return 1;
	}
	if (strcmp(normalized, "magenta") == 0) {
		*color_out = EDITOR_THEME_COLOR_MAGENTA;
		return 1;
	}
	if (strcmp(normalized, "cyan") == 0) {
		*color_out = EDITOR_THEME_COLOR_CYAN;
		return 1;
	}
	if (strcmp(normalized, "white") == 0) {
		*color_out = EDITOR_THEME_COLOR_WHITE;
		return 1;
	}
	if (strcmp(normalized, "bright_black") == 0 || strcmp(normalized, "gray") == 0 ||
			strcmp(normalized, "grey") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_BLACK;
		return 1;
	}
	if (strcmp(normalized, "bright_red") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_RED;
		return 1;
	}
	if (strcmp(normalized, "bright_green") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_GREEN;
		return 1;
	}
	if (strcmp(normalized, "bright_yellow") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_YELLOW;
		return 1;
	}
	if (strcmp(normalized, "bright_blue") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_BLUE;
		return 1;
	}
	if (strcmp(normalized, "bright_magenta") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_MAGENTA;
		return 1;
	}
	if (strcmp(normalized, "bright_cyan") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_CYAN;
		return 1;
	}
	if (strcmp(normalized, "bright_white") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_WHITE;
		return 1;
	}

	return 0;
}

static enum editorSyntaxThemeFileStatus editorSyntaxThemeApplyConfigFile(
		enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT], const char *path,
		int *had_invalid_out) {
	if (had_invalid_out != NULL) {
		*had_invalid_out = 0;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_SYNTAX_THEME_FILE_MISSING;
		}
		if (had_invalid_out != NULL) {
			*had_invalid_out = 1;
		}
		return EDITOR_SYNTAX_THEME_FILE_APPLIED;
	}

	enum editorThemeColor updated[EDITOR_SYNTAX_HL_CLASS_COUNT];
	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		updated[i] = theme[i];
	}

	int in_theme_syntax_table = 0;
	int had_invalid = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			had_invalid = 1;
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
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				had_invalid = 1;
				continue;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				had_invalid = 1;
				continue;
			}

			in_theme_syntax_table = strcmp(table, "theme.syntax") == 0;
			continue;
		}

		if (!in_theme_syntax_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			had_invalid = 1;
			continue;
		}

		*eq = '\0';
		char *class_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(class_name);
		char *value = editorConfigTrimLeft(eq + 1);
		if (class_name[0] == '\0') {
			had_invalid = 1;
			continue;
		}

		char color_name[64];
		if (!editorConfigParseQuotedValue(value, color_name, sizeof(color_name))) {
			had_invalid = 1;
			continue;
		}

		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		enum editorThemeColor color = EDITOR_THEME_COLOR_DEFAULT;
		if (!editorParseSyntaxHighlightClassName(class_name, &highlight_class) ||
				highlight_class == EDITOR_SYNTAX_HL_NONE ||
				!editorParseThemeColorValue(color_name, &color)) {
			had_invalid = 1;
			continue;
		}

		updated[highlight_class] = color;
	}

	if (ferror(fp)) {
		had_invalid = 1;
	}

	fclose(fp);
	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme[i] = updated[i];
	}
	if (had_invalid_out != NULL) {
		*had_invalid_out = had_invalid;
	}
	return EDITOR_SYNTAX_THEME_FILE_APPLIED;
}

enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadFromPaths(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT],
		const char *global_path, const char *project_path) {
	if (theme_out == NULL) {
		return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
	}

	enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	editorSyntaxThemeInitDefaults(theme);
	enum editorSyntaxThemeLoadStatus status = EDITOR_SYNTAX_THEME_LOAD_OK;

	if (global_path != NULL) {
		int global_invalid = 0;
		enum editorSyntaxThemeFileStatus global_status =
				editorSyntaxThemeApplyConfigFile(theme, global_path, &global_invalid);
		if (global_status == EDITOR_SYNTAX_THEME_FILE_OUT_OF_MEMORY) {
			editorSyntaxThemeInitDefaults(theme_out);
			return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
		}
		if (global_invalid) {
			status = (enum editorSyntaxThemeLoadStatus)(
					status | EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		int project_invalid = 0;
		enum editorSyntaxThemeFileStatus project_status =
				editorSyntaxThemeApplyConfigFile(theme, project_path, &project_invalid);
		if (project_status == EDITOR_SYNTAX_THEME_FILE_OUT_OF_MEMORY) {
			editorSyntaxThemeInitDefaults(theme_out);
			return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
		}
		if (project_invalid) {
			status = (enum editorSyntaxThemeLoadStatus)(
					status | EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT);
		}
	}

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme_out[i] = theme[i];
	}
	return status;
}

enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadConfigured(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]) {
	if (theme_out == NULL) {
		return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorSyntaxThemeLoadFromPaths(theme_out, NULL, ".rotide.toml");
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorSyntaxThemeInitDefaults(theme_out);
		return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
	}

	enum editorSyntaxThemeLoadStatus status =
			editorSyntaxThemeLoadFromPaths(theme_out, global_path, ".rotide.toml");
	free(global_path);
	return status;
}
