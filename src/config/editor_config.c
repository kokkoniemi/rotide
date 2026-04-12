#include "config/editor_config.h"

#include "config/internal.h"

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
		return editorCursorStyleLoadFromPaths(style_out, NULL, ".rotide.toml");
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		*style_out = EDITOR_CURSOR_STYLE_BAR;
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorStyleLoadStatus status =
			editorCursorStyleLoadFromPaths(style_out, global_path, ".rotide.toml");
	free(global_path);
	return status;
}
