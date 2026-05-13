#include "config/editor_config.h"

#include "config/common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum editorCursorStyleFileStatus {
	EDITOR_CURSOR_STYLE_FILE_APPLIED = 0,
	EDITOR_CURSOR_STYLE_FILE_MISSING,
	EDITOR_CURSOR_STYLE_FILE_INVALID,
	EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY
};

enum editorLineWrapFileStatus {
	EDITOR_LINE_WRAP_FILE_APPLIED = 0,
	EDITOR_LINE_WRAP_FILE_MISSING,
	EDITOR_LINE_WRAP_FILE_INVALID,
	EDITOR_LINE_WRAP_FILE_OUT_OF_MEMORY
};

enum editorBoolFileStatus {
	EDITOR_BOOL_FILE_APPLIED = 0,
	EDITOR_BOOL_FILE_MISSING,
	EDITOR_BOOL_FILE_INVALID,
	EDITOR_BOOL_FILE_OUT_OF_MEMORY
};

static int editorParseCursorStyleValue(const char *value, enum editorCursorStyle *style_out) {
	if (strcasecmp(value, "block") == 0) {
		*style_out = EDITOR_CURSOR_STYLE_BLOCK;
		return 1;
	}
	if (strcasecmp(value, "bar") == 0) {
		*style_out = EDITOR_CURSOR_STYLE_BAR;
		return 1;
	}
	if (strcasecmp(value, "underline") == 0) {
		*style_out = EDITOR_CURSOR_STYLE_UNDERLINE;
		return 1;
	}
	return 0;
}

static int editorParseBoolValue(const char *value, int *bool_out) {
	if (strcasecmp(value, "true") == 0) {
		*bool_out = 1;
		return 1;
	}
	if (strcasecmp(value, "false") == 0) {
		*bool_out = 0;
		return 1;
	}
	return 0;
}

static enum editorCursorStyleFileStatus editorCursorStyleApplyConfigFile(
		enum editorCursorStyle *style_in_out, const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_CURSOR_STYLE_FILE_MISSING;
		}
		return EDITOR_CURSOR_STYLE_FILE_INVALID;
	}

	enum editorCursorStyle updated = *style_in_out;
	int in_editor_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
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
				fclose(fp);
				return EDITOR_CURSOR_STYLE_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_CURSOR_STYLE_FILE_INVALID;
			}

			in_editor_table = strcmp(table, "editor") == 0;
			continue;
		}

		if (!in_editor_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}
		if (strcmp(setting_name, "cursor_style") != 0) {
			continue;
		}

		char cursor_style_value[32];
		if (!editorConfigParseQuotedValue(value, cursor_style_value, sizeof(cursor_style_value))) {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}

		enum editorCursorStyle parsed = EDITOR_CURSOR_STYLE_BAR;
		if (!editorParseCursorStyleValue(cursor_style_value, &parsed)) {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}
		updated = parsed;
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_CURSOR_STYLE_FILE_INVALID;
	}

	fclose(fp);
	*style_in_out = updated;
	return EDITOR_CURSOR_STYLE_FILE_APPLIED;
}

enum editorCursorStyleLoadStatus editorCursorStyleLoadFromPaths(enum editorCursorStyle *style_out,
		const char *global_path, const char *project_path) {
	if (style_out == NULL) {
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status = EDITOR_CURSOR_STYLE_LOAD_OK;

	if (global_path != NULL) {
		enum editorCursorStyleFileStatus global_status =
				editorCursorStyleApplyConfigFile(&style, global_path);
		if (global_status == EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY) {
			*style_out = EDITOR_CURSOR_STYLE_BAR;
			return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_CURSOR_STYLE_FILE_INVALID) {
			style = EDITOR_CURSOR_STYLE_BAR;
			status = (enum editorCursorStyleLoadStatus)(
					status | EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorCursorStyleFileStatus project_status =
				editorCursorStyleApplyConfigFile(&style, project_path);
		if (project_status == EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY) {
			*style_out = EDITOR_CURSOR_STYLE_BAR;
			return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_CURSOR_STYLE_FILE_INVALID) {
			style = EDITOR_CURSOR_STYLE_BAR;
			status = (enum editorCursorStyleLoadStatus)(
					status | EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT);
		}
	}

	*style_out = style;
	return status;
}

enum editorCursorStyleLoadStatus editorCursorStyleLoadConfigured(enum editorCursorStyle *style_out) {
	if (style_out == NULL) {
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorCursorStyleLoadFromPaths(style_out, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*style_out = EDITOR_CURSOR_STYLE_BAR;
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorStyleLoadStatus status =
			editorCursorStyleLoadFromPaths(style_out, global_path, NULL);
	free(global_path);
	return status;
}

static enum editorLineWrapFileStatus editorLineWrapApplyConfigFile(int *line_wrap_in_out,
		const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_LINE_WRAP_FILE_MISSING;
		}
		return EDITOR_LINE_WRAP_FILE_INVALID;
	}

	int updated = *line_wrap_in_out;
	int in_editor_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_LINE_WRAP_FILE_INVALID;
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
				fclose(fp);
				return EDITOR_LINE_WRAP_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_LINE_WRAP_FILE_INVALID;
			}

			in_editor_table = strcmp(table, "editor") == 0;
			continue;
		}

		if (!in_editor_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_LINE_WRAP_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		editorConfigTrimRight(value);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_LINE_WRAP_FILE_INVALID;
		}
		if (strcmp(setting_name, "line_wrap") != 0) {
			continue;
		}

		int parsed = 0;
		if (!editorParseBoolValue(value, &parsed)) {
			fclose(fp);
			return EDITOR_LINE_WRAP_FILE_INVALID;
		}
		updated = parsed;
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_LINE_WRAP_FILE_INVALID;
	}

	fclose(fp);
	*line_wrap_in_out = updated;
	return EDITOR_LINE_WRAP_FILE_APPLIED;
}

enum editorLineWrapLoadStatus editorLineWrapLoadFromPaths(int *line_wrap_out,
		const char *global_path, const char *project_path) {
	if (line_wrap_out == NULL) {
		return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
	}

	int line_wrap = 0;
	enum editorLineWrapLoadStatus status = EDITOR_LINE_WRAP_LOAD_OK;

	if (global_path != NULL) {
		enum editorLineWrapFileStatus global_status =
				editorLineWrapApplyConfigFile(&line_wrap, global_path);
		if (global_status == EDITOR_LINE_WRAP_FILE_OUT_OF_MEMORY) {
			*line_wrap_out = 0;
			return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_LINE_WRAP_FILE_INVALID) {
			line_wrap = 0;
			status = (enum editorLineWrapLoadStatus)(
					status | EDITOR_LINE_WRAP_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorLineWrapFileStatus project_status =
				editorLineWrapApplyConfigFile(&line_wrap, project_path);
		if (project_status == EDITOR_LINE_WRAP_FILE_OUT_OF_MEMORY) {
			*line_wrap_out = 0;
			return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_LINE_WRAP_FILE_INVALID) {
			line_wrap = 0;
			status = (enum editorLineWrapLoadStatus)(
					status | EDITOR_LINE_WRAP_LOAD_INVALID_PROJECT);
		}
	}

	*line_wrap_out = line_wrap;
	return status;
}

enum editorLineWrapLoadStatus editorLineWrapLoadConfigured(int *line_wrap_out) {
	if (line_wrap_out == NULL) {
		return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorLineWrapLoadFromPaths(line_wrap_out, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*line_wrap_out = 0;
		return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
	}

	enum editorLineWrapLoadStatus status =
			editorLineWrapLoadFromPaths(line_wrap_out, global_path, NULL);
	free(global_path);
	return status;
}

static enum editorBoolFileStatus editorBoolApplyConfigFile(int *bool_in_out,
		const char *path, const char *target_setting_name) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_BOOL_FILE_MISSING;
		}
		return EDITOR_BOOL_FILE_INVALID;
	}

	int updated = *bool_in_out;
	int in_editor_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_BOOL_FILE_INVALID;
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
				fclose(fp);
				return EDITOR_BOOL_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_BOOL_FILE_INVALID;
			}

			in_editor_table = strcmp(table, "editor") == 0;
			continue;
		}

		if (!in_editor_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_BOOL_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		editorConfigTrimRight(value);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_BOOL_FILE_INVALID;
		}
		if (strcmp(setting_name, target_setting_name) != 0) {
			continue;
		}

		int parsed = 0;
		if (!editorParseBoolValue(value, &parsed)) {
			fclose(fp);
			return EDITOR_BOOL_FILE_INVALID;
		}
		updated = parsed;
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_BOOL_FILE_INVALID;
	}

	fclose(fp);
	*bool_in_out = updated;
	return EDITOR_BOOL_FILE_APPLIED;
}

enum editorCursorBlinkLoadStatus editorCursorBlinkLoadFromPaths(int *cursor_blink_out,
		const char *global_path, const char *project_path) {
	if (cursor_blink_out == NULL) {
		return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
	}

	int cursor_blink = 1;
	enum editorCursorBlinkLoadStatus status = EDITOR_CURSOR_BLINK_LOAD_OK;

	if (global_path != NULL) {
		enum editorBoolFileStatus global_status =
				editorBoolApplyConfigFile(&cursor_blink, global_path, "cursor_blink");
		if (global_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*cursor_blink_out = 1;
			return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_BOOL_FILE_INVALID) {
			cursor_blink = 1;
			status = (enum editorCursorBlinkLoadStatus)(
					status | EDITOR_CURSOR_BLINK_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorBoolFileStatus project_status =
				editorBoolApplyConfigFile(&cursor_blink, project_path, "cursor_blink");
		if (project_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*cursor_blink_out = 1;
			return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_BOOL_FILE_INVALID) {
			cursor_blink = 1;
			status = (enum editorCursorBlinkLoadStatus)(
					status | EDITOR_CURSOR_BLINK_LOAD_INVALID_PROJECT);
		}
	}

	*cursor_blink_out = cursor_blink;
	return status;
}

enum editorCursorBlinkLoadStatus editorCursorBlinkLoadConfigured(int *cursor_blink_out) {
	if (cursor_blink_out == NULL) {
		return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorCursorBlinkLoadFromPaths(cursor_blink_out, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*cursor_blink_out = 1;
		return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorBlinkLoadStatus status =
			editorCursorBlinkLoadFromPaths(cursor_blink_out, global_path, NULL);
	free(global_path);
	return status;
}

enum editorLineNumbersLoadStatus editorLineNumbersLoadFromPaths(int *line_numbers_out,
		const char *global_path, const char *project_path) {
	if (line_numbers_out == NULL) {
		return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
	}

	int line_numbers = 1;
	enum editorLineNumbersLoadStatus status = EDITOR_LINE_NUMBERS_LOAD_OK;

	if (global_path != NULL) {
		enum editorBoolFileStatus global_status =
				editorBoolApplyConfigFile(&line_numbers, global_path, "line_numbers");
		if (global_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*line_numbers_out = 1;
			return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_BOOL_FILE_INVALID) {
			line_numbers = 1;
			status = (enum editorLineNumbersLoadStatus)(
					status | EDITOR_LINE_NUMBERS_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorBoolFileStatus project_status =
				editorBoolApplyConfigFile(&line_numbers, project_path, "line_numbers");
		if (project_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*line_numbers_out = 1;
			return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_BOOL_FILE_INVALID) {
			line_numbers = 1;
			status = (enum editorLineNumbersLoadStatus)(
					status | EDITOR_LINE_NUMBERS_LOAD_INVALID_PROJECT);
		}
	}

	*line_numbers_out = line_numbers;
	return status;
}

enum editorLineNumbersLoadStatus editorLineNumbersLoadConfigured(int *line_numbers_out) {
	if (line_numbers_out == NULL) {
		return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorLineNumbersLoadFromPaths(line_numbers_out, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*line_numbers_out = 1;
		return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
	}

	enum editorLineNumbersLoadStatus status =
			editorLineNumbersLoadFromPaths(line_numbers_out, global_path, NULL);
	free(global_path);
	return status;
}

enum editorCurrentLineHighlightLoadStatus editorCurrentLineHighlightLoadFromPaths(
		int *current_line_highlight_out, const char *global_path, const char *project_path) {
	if (current_line_highlight_out == NULL) {
		return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
	}

	int current_line_highlight = 1;
	enum editorCurrentLineHighlightLoadStatus status = EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OK;

	if (global_path != NULL) {
		enum editorBoolFileStatus global_status = editorBoolApplyConfigFile(
				&current_line_highlight, global_path, "current_line_highlight");
		if (global_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*current_line_highlight_out = 1;
			return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_BOOL_FILE_INVALID) {
			current_line_highlight = 1;
			status = (enum editorCurrentLineHighlightLoadStatus)(
					status | EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorBoolFileStatus project_status = editorBoolApplyConfigFile(
				&current_line_highlight, project_path, "current_line_highlight");
		if (project_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*current_line_highlight_out = 1;
			return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_BOOL_FILE_INVALID) {
			current_line_highlight = 1;
			status = (enum editorCurrentLineHighlightLoadStatus)(
					status | EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT);
		}
	}

	*current_line_highlight_out = current_line_highlight;
	return status;
}

enum editorCurrentLineHighlightLoadStatus editorCurrentLineHighlightLoadConfigured(
		int *current_line_highlight_out) {
	if (current_line_highlight_out == NULL) {
		return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorCurrentLineHighlightLoadFromPaths(current_line_highlight_out, NULL,
				NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*current_line_highlight_out = 1;
		return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
	}

	enum editorCurrentLineHighlightLoadStatus status =
			editorCurrentLineHighlightLoadFromPaths(current_line_highlight_out, global_path,
					NULL);
	free(global_path);
	return status;
}

enum editorNerdFontsLoadStatus editorNerdFontsLoadFromPaths(int *nerd_fonts_out,
		const char *global_path, const char *project_path) {
	if (nerd_fonts_out == NULL) {
		return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
	}

	int nerd_fonts = 0;
	enum editorNerdFontsLoadStatus status = EDITOR_NERD_FONTS_LOAD_OK;

	if (global_path != NULL) {
		enum editorBoolFileStatus global_status =
				editorBoolApplyConfigFile(&nerd_fonts, global_path, "nerd_fonts");
		if (global_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*nerd_fonts_out = 0;
			return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_BOOL_FILE_INVALID) {
			nerd_fonts = 0;
			status = (enum editorNerdFontsLoadStatus)(
					status | EDITOR_NERD_FONTS_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorBoolFileStatus project_status =
				editorBoolApplyConfigFile(&nerd_fonts, project_path, "nerd_fonts");
		if (project_status == EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*nerd_fonts_out = 0;
			return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_BOOL_FILE_INVALID) {
			nerd_fonts = 0;
			status = (enum editorNerdFontsLoadStatus)(
					status | EDITOR_NERD_FONTS_LOAD_INVALID_PROJECT);
		}
	}

	*nerd_fonts_out = nerd_fonts;
	return status;
}

enum editorNerdFontsLoadStatus editorNerdFontsLoadConfigured(int *nerd_fonts_out) {
	if (nerd_fonts_out == NULL) {
		return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorNerdFontsLoadFromPaths(nerd_fonts_out, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*nerd_fonts_out = 0;
		return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
	}

	enum editorNerdFontsLoadStatus status =
			editorNerdFontsLoadFromPaths(nerd_fonts_out, global_path, NULL);
	free(global_path);
	return status;
}

enum editorIndentConfigFileStatus {
	EDITOR_INDENT_CONFIG_FILE_APPLIED = 0,
	EDITOR_INDENT_CONFIG_FILE_MISSING,
	EDITOR_INDENT_CONFIG_FILE_INVALID,
	EDITOR_INDENT_CONFIG_FILE_OUT_OF_MEMORY
};

void editorIndentConfigInitDefaults(int *auto_indent_out, int *indent_use_tabs_out,
		int *indent_width_out) {
	if (auto_indent_out != NULL) {
		*auto_indent_out = 0;
	}
	if (indent_use_tabs_out != NULL) {
		*indent_use_tabs_out = 0;
	}
	if (indent_width_out != NULL) {
		*indent_width_out = ROTIDE_INDENT_WIDTH_DEFAULT;
	}
}

static int editorParseIndentStyleValue(const char *value, int *indent_use_tabs_out) {
	if (value == NULL || indent_use_tabs_out == NULL) {
		return 0;
	}
	if (strcasecmp(value, "spaces") == 0 || strcasecmp(value, "space") == 0) {
		*indent_use_tabs_out = 0;
		return 1;
	}
	if (strcasecmp(value, "tabs") == 0 || strcasecmp(value, "tab") == 0) {
		*indent_use_tabs_out = 1;
		return 1;
	}
	return 0;
}

static int editorParseIndentWidthValue(const char *value, int *indent_width_out) {
	if (value == NULL || indent_width_out == NULL) {
		return 0;
	}
	errno = 0;
	char *end = NULL;
	long width = strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' ||
			width < 1 || width > ROTIDE_INDENT_WIDTH_MAX) {
		return 0;
	}
	*indent_width_out = (int)width;
	return 1;
}

static enum editorIndentConfigFileStatus editorIndentConfigApplyConfigFile(
		int *auto_indent_in_out, int *indent_use_tabs_in_out, int *indent_width_in_out,
		const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_INDENT_CONFIG_FILE_MISSING;
		}
		return EDITOR_INDENT_CONFIG_FILE_INVALID;
	}

	int updated_auto_indent = *auto_indent_in_out;
	int updated_indent_use_tabs = *indent_use_tabs_in_out;
	int updated_indent_width = *indent_width_in_out;
	int in_editor_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_INDENT_CONFIG_FILE_INVALID;
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
				fclose(fp);
				return EDITOR_INDENT_CONFIG_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_INDENT_CONFIG_FILE_INVALID;
			}

			in_editor_table = strcmp(table, "editor") == 0;
			continue;
		}

		if (!in_editor_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_INDENT_CONFIG_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		editorConfigTrimRight(value);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_INDENT_CONFIG_FILE_INVALID;
		}

		if (strcmp(setting_name, "auto_indent") == 0) {
			int parsed = 0;
			if (!editorParseBoolValue(value, &parsed)) {
				fclose(fp);
				return EDITOR_INDENT_CONFIG_FILE_INVALID;
			}
			updated_auto_indent = parsed;
			continue;
		}
		if (strcmp(setting_name, "indent_style") == 0) {
			char style_value[32];
			int parsed = 0;
			if (!editorConfigParseQuotedValue(value, style_value, sizeof(style_value)) ||
					!editorParseIndentStyleValue(style_value, &parsed)) {
				fclose(fp);
				return EDITOR_INDENT_CONFIG_FILE_INVALID;
			}
			updated_indent_use_tabs = parsed;
			continue;
		}
		if (strcmp(setting_name, "indent_width") == 0) {
			int parsed = 0;
			if (!editorParseIndentWidthValue(value, &parsed)) {
				fclose(fp);
				return EDITOR_INDENT_CONFIG_FILE_INVALID;
			}
			updated_indent_width = parsed;
		}
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_INDENT_CONFIG_FILE_INVALID;
	}

	fclose(fp);
	*auto_indent_in_out = updated_auto_indent;
	*indent_use_tabs_in_out = updated_indent_use_tabs;
	*indent_width_in_out = updated_indent_width;
	return EDITOR_INDENT_CONFIG_FILE_APPLIED;
}

enum editorIndentConfigLoadStatus editorIndentConfigLoadFromPaths(int *auto_indent_out,
		int *indent_use_tabs_out, int *indent_width_out, const char *global_path,
		const char *project_path) {
	if (auto_indent_out == NULL || indent_use_tabs_out == NULL || indent_width_out == NULL) {
		return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	int auto_indent = 0;
	int indent_use_tabs = 0;
	int indent_width = ROTIDE_INDENT_WIDTH_DEFAULT;
	enum editorIndentConfigLoadStatus status = EDITOR_INDENT_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum editorIndentConfigFileStatus global_status = editorIndentConfigApplyConfigFile(
				&auto_indent, &indent_use_tabs, &indent_width, global_path);
		if (global_status == EDITOR_INDENT_CONFIG_FILE_OUT_OF_MEMORY) {
			editorIndentConfigInitDefaults(auto_indent_out, indent_use_tabs_out,
					indent_width_out);
			return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_INDENT_CONFIG_FILE_INVALID) {
			editorIndentConfigInitDefaults(&auto_indent, &indent_use_tabs, &indent_width);
			status = (enum editorIndentConfigLoadStatus)(
					status | EDITOR_INDENT_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorIndentConfigFileStatus project_status = editorIndentConfigApplyConfigFile(
				&auto_indent, &indent_use_tabs, &indent_width, project_path);
		if (project_status == EDITOR_INDENT_CONFIG_FILE_OUT_OF_MEMORY) {
			editorIndentConfigInitDefaults(auto_indent_out, indent_use_tabs_out,
					indent_width_out);
			return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_INDENT_CONFIG_FILE_INVALID) {
			editorIndentConfigInitDefaults(&auto_indent, &indent_use_tabs, &indent_width);
			status = (enum editorIndentConfigLoadStatus)(
					status | EDITOR_INDENT_CONFIG_LOAD_INVALID_PROJECT);
		}
	}

	*auto_indent_out = auto_indent;
	*indent_use_tabs_out = indent_use_tabs;
	*indent_width_out = indent_width;
	return status;
}

enum editorIndentConfigLoadStatus editorIndentConfigLoadConfigured(int *auto_indent_out,
		int *indent_use_tabs_out, int *indent_width_out) {
	if (auto_indent_out == NULL || indent_use_tabs_out == NULL || indent_width_out == NULL) {
		return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
	}
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorIndentConfigLoadFromPaths(auto_indent_out, indent_use_tabs_out,
				indent_width_out, NULL, NULL);
	}
	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorIndentConfigInitDefaults(auto_indent_out, indent_use_tabs_out,
				indent_width_out);
		return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
	}
	enum editorIndentConfigLoadStatus status = editorIndentConfigLoadFromPaths(
			auto_indent_out, indent_use_tabs_out, indent_width_out, global_path, NULL);
	free(global_path);
	return status;
}

static int editorColumnSelectDragModifierTokenBit(const char *token, int *bit_out, int *is_none) {
	if (strcmp(token, "alt") == 0) {
		*bit_out = EDITOR_MOUSE_MOD_ALT;
		*is_none = 0;
		return 1;
	}
	if (strcmp(token, "shift") == 0) {
		*bit_out = EDITOR_MOUSE_MOD_SHIFT;
		*is_none = 0;
		return 1;
	}
	if (strcmp(token, "ctrl") == 0) {
		*bit_out = EDITOR_MOUSE_MOD_CTRL;
		*is_none = 0;
		return 1;
	}
	if (strcmp(token, "none") == 0) {
		*bit_out = 0;
		*is_none = 1;
		return 1;
	}
	return 0;
}

int editorParseColumnSelectDragModifierValue(const char *value, int *modifier_out) {
	if (value == NULL || modifier_out == NULL) {
		return 0;
	}
	const char *p = value;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p == '"' || *p == '\'') {
		p++;
	}

	int modifiers = 0;
	int parsed_any = 0;
	int saw_none = 0;
	char token[16];
	size_t token_len = 0;
	int expecting_token = 1;

	while (1) {
		char c = *p;
		if (c == '\0' || c == '"' || c == '\'' || c == '+') {
			if (token_len == 0) {
				if (c == '+') {
					return 0;
				}
				if (expecting_token && parsed_any) {
					return 0;
				}
				break;
			}
			token[token_len] = '\0';
			for (size_t i = 0; i < token_len; i++) {
				if (token[i] >= 'A' && token[i] <= 'Z') {
					token[i] = (char)(token[i] - 'A' + 'a');
				}
			}
			int bit = 0;
			int is_none = 0;
			if (!editorColumnSelectDragModifierTokenBit(token, &bit, &is_none)) {
				return 0;
			}
			if (is_none) {
				if (parsed_any) {
					return 0;
				}
				saw_none = 1;
			} else {
				if (saw_none) {
					return 0;
				}
				if (modifiers & bit) {
					return 0;
				}
				modifiers |= bit;
			}
			parsed_any = 1;
			token_len = 0;
			expecting_token = (c == '+');
			if (c == '\0' || c == '"' || c == '\'') {
				break;
			}
			p++;
			continue;
		}
		if (token_len + 1 >= sizeof(token)) {
			return 0;
		}
		token[token_len++] = c;
		p++;
	}

	if (!parsed_any) {
		return 0;
	}
	*modifier_out = saw_none ? 0 : modifiers;
	return 1;
}

enum editorColumnSelectDragModifierFileStatus {
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_APPLIED = 0,
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_MISSING,
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID,
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_OUT_OF_MEMORY
};

static enum editorColumnSelectDragModifierFileStatus
editorColumnSelectDragModifierApplyConfigFile(int *modifier_in_out, const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_MISSING;
		}
		return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
	}

	int updated = *modifier_in_out;
	int in_editor_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
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
				fclose(fp);
				return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
			}
			in_editor_table = strcmp(table, "editor") == 0;
			continue;
		}

		if (!in_editor_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		editorConfigTrimRight(value);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
		}
		if (strcmp(setting_name, "column_select_drag_modifier") != 0) {
			continue;
		}

		int parsed = 0;
		if (!editorParseColumnSelectDragModifierValue(value, &parsed)) {
			fclose(fp);
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
		}
		updated = parsed;
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
	}

	fclose(fp);
	*modifier_in_out = updated;
	return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_APPLIED;
}

enum editorColumnSelectDragModifierLoadStatus
editorColumnSelectDragModifierLoadFromPaths(int *modifier_out, const char *global_path,
		const char *project_path) {
	if (modifier_out == NULL) {
		return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
	}

	int default_modifier = EDITOR_MOUSE_MOD_ALT;
	int modifier = default_modifier;
	enum editorColumnSelectDragModifierLoadStatus status =
			EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OK;

	if (global_path != NULL) {
		enum editorColumnSelectDragModifierFileStatus s =
				editorColumnSelectDragModifierApplyConfigFile(&modifier, global_path);
		if (s == EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_OUT_OF_MEMORY) {
			*modifier_out = default_modifier;
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
		}
		if (s == EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID) {
			modifier = default_modifier;
			status = (enum editorColumnSelectDragModifierLoadStatus)(
					status | EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorColumnSelectDragModifierFileStatus s =
				editorColumnSelectDragModifierApplyConfigFile(&modifier, project_path);
		if (s == EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_OUT_OF_MEMORY) {
			*modifier_out = default_modifier;
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
		}
		if (s == EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID) {
			modifier = default_modifier;
			status = (enum editorColumnSelectDragModifierLoadStatus)(
					status | EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_PROJECT);
		}
	}

	*modifier_out = modifier;
	return status;
}

enum editorColumnSelectDragModifierLoadStatus
editorColumnSelectDragModifierLoadConfigured(int *modifier_out) {
	if (modifier_out == NULL) {
		return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
	}
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorColumnSelectDragModifierLoadFromPaths(modifier_out, NULL, NULL);
	}
	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*modifier_out = EDITOR_MOUSE_MOD_ALT;
		return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
	}
	enum editorColumnSelectDragModifierLoadStatus status =
			editorColumnSelectDragModifierLoadFromPaths(modifier_out, global_path, NULL);
	free(global_path);
	return status;
}
