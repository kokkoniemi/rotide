#include "config/editor_config.h"

#include "config/common.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum configEditorCursorStyleFileStatus {
	CONFIG_EDITOR_CURSOR_STYLE_FILE_APPLIED = 0,
	CONFIG_EDITOR_CURSOR_STYLE_FILE_MISSING,
	CONFIG_EDITOR_CURSOR_STYLE_FILE_INVALID,
	CONFIG_EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY
};

enum configEditorLineWrapFileStatus {
	CONFIG_EDITOR_LINE_WRAP_FILE_APPLIED = 0,
	CONFIG_EDITOR_LINE_WRAP_FILE_MISSING,
	CONFIG_EDITOR_LINE_WRAP_FILE_INVALID,
	CONFIG_EDITOR_LINE_WRAP_FILE_OUT_OF_MEMORY
};

enum configEditorBoolFileStatus {
	CONFIG_EDITOR_BOOL_FILE_APPLIED = 0,
	CONFIG_EDITOR_BOOL_FILE_MISSING,
	CONFIG_EDITOR_BOOL_FILE_INVALID,
	CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY
};

static int configEditorParseCursorStyleValue(const char *value, enum editorCursorStyle *style_out) {
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

static int configEditorParseBoolValue(const char *value, int *bool_out) {
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

static int configEditorOnEditorSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "editor") == 0;
}

static int configEditorCursorStyleOnEntry(void *ctx, const char *key, char *value) {
	enum editorCursorStyle *style = ctx;
	if (strcmp(key, "cursor_style") != 0) {
		return 1;
	}
	char cursor_style_value[32];
	if (!editorConfigParseQuotedValue(value, cursor_style_value, sizeof(cursor_style_value))) {
		return 0;
	}
	return configEditorParseCursorStyleValue(cursor_style_value, style);
}

static enum configEditorCursorStyleFileStatus
configEditorCursorStyleApplyFile(enum editorCursorStyle *style_in_out, const char *path) {
	enum editorCursorStyle updated = *style_in_out;
	struct editorConfigScanner scanner = {configEditorOnEditorSection,
	                                      configEditorCursorStyleOnEntry};

	switch (editorConfigScanFile(path, &scanner, &updated)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return CONFIG_EDITOR_CURSOR_STYLE_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			*style_in_out = updated;
			return CONFIG_EDITOR_CURSOR_STYLE_FILE_APPLIED;
		default:
			return CONFIG_EDITOR_CURSOR_STYLE_FILE_INVALID;
	}
}

enum editorCursorStyleLoadStatus editorCursorStyleLoadFromPaths(enum editorCursorStyle *style_out,
                                                                const char *global_path,
                                                                const char *project_path) {
	if (style_out == NULL) {
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status = EDITOR_CURSOR_STYLE_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorCursorStyleFileStatus global_status =
		        configEditorCursorStyleApplyFile(&style, global_path);
		if (global_status == CONFIG_EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY) {
			*style_out = EDITOR_CURSOR_STYLE_BAR;
			return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_CURSOR_STYLE_FILE_INVALID) {
			style = EDITOR_CURSOR_STYLE_BAR;
			status = (enum editorCursorStyleLoadStatus)(
			        status | EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorCursorStyleFileStatus project_status =
		        configEditorCursorStyleApplyFile(&style, project_path);
		if (project_status == CONFIG_EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY) {
			*style_out = EDITOR_CURSOR_STYLE_BAR;
			return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_CURSOR_STYLE_FILE_INVALID) {
			style = EDITOR_CURSOR_STYLE_BAR;
			status = (enum editorCursorStyleLoadStatus)(
			        status | EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT);
		}
	}

	*style_out = style;
	return status;
}

enum editorCursorStyleLoadStatus
editorCursorStyleLoadConfigured(enum editorCursorStyle *style_out) {
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

static int configEditorLineWrapOnEntry(void *ctx, const char *key, char *value) {
	int *line_wrap = ctx;
	if (strcmp(key, "line_wrap") != 0) {
		return 1;
	}
	return configEditorParseBoolValue(value, line_wrap);
}

static enum configEditorLineWrapFileStatus configEditorLineWrapApplyFile(int *line_wrap_in_out,
                                                                         const char *path) {
	int updated = *line_wrap_in_out;
	struct editorConfigScanner scanner = {configEditorOnEditorSection,
	                                      configEditorLineWrapOnEntry};

	switch (editorConfigScanFile(path, &scanner, &updated)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return CONFIG_EDITOR_LINE_WRAP_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			*line_wrap_in_out = updated;
			return CONFIG_EDITOR_LINE_WRAP_FILE_APPLIED;
		default:
			return CONFIG_EDITOR_LINE_WRAP_FILE_INVALID;
	}
}

enum editorLineWrapLoadStatus
editorLineWrapLoadFromPaths(int *line_wrap_out, const char *global_path, const char *project_path) {
	if (line_wrap_out == NULL) {
		return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
	}

	int line_wrap = 0;
	enum editorLineWrapLoadStatus status = EDITOR_LINE_WRAP_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorLineWrapFileStatus global_status =
		        configEditorLineWrapApplyFile(&line_wrap, global_path);
		if (global_status == CONFIG_EDITOR_LINE_WRAP_FILE_OUT_OF_MEMORY) {
			*line_wrap_out = 0;
			return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_LINE_WRAP_FILE_INVALID) {
			line_wrap = 0;
			status = (enum editorLineWrapLoadStatus)(
			        status | EDITOR_LINE_WRAP_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorLineWrapFileStatus project_status =
		        configEditorLineWrapApplyFile(&line_wrap, project_path);
		if (project_status == CONFIG_EDITOR_LINE_WRAP_FILE_OUT_OF_MEMORY) {
			*line_wrap_out = 0;
			return EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_LINE_WRAP_FILE_INVALID) {
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

struct configEditorBoolApplyContext {
	const char *target;
	int value;
};

static int configEditorBoolOnEntry(void *ctx, const char *key, char *value) {
	struct configEditorBoolApplyContext *apply = ctx;
	if (strcmp(key, apply->target) != 0) {
		return 1;
	}
	return configEditorParseBoolValue(value, &apply->value);
}

static enum configEditorBoolFileStatus configEditorBoolApplyFile(int *bool_in_out, const char *path,
                                                                 const char *target_setting_name) {
	struct configEditorBoolApplyContext apply = {.target = target_setting_name,
	                                             .value = *bool_in_out};
	struct editorConfigScanner scanner = {configEditorOnEditorSection, configEditorBoolOnEntry};

	switch (editorConfigScanFile(path, &scanner, &apply)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return CONFIG_EDITOR_BOOL_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			*bool_in_out = apply.value;
			return CONFIG_EDITOR_BOOL_FILE_APPLIED;
		default:
			return CONFIG_EDITOR_BOOL_FILE_INVALID;
	}
}

enum editorCursorBlinkLoadStatus editorCursorBlinkLoadFromPaths(int *cursor_blink_out,
                                                                const char *global_path,
                                                                const char *project_path) {
	if (cursor_blink_out == NULL) {
		return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
	}

	int cursor_blink = 1;
	enum editorCursorBlinkLoadStatus status = EDITOR_CURSOR_BLINK_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorBoolFileStatus global_status =
		        configEditorBoolApplyFile(&cursor_blink, global_path, "cursor_blink");
		if (global_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*cursor_blink_out = 1;
			return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
			cursor_blink = 1;
			status = (enum editorCursorBlinkLoadStatus)(
			        status | EDITOR_CURSOR_BLINK_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorBoolFileStatus project_status =
		        configEditorBoolApplyFile(&cursor_blink, project_path, "cursor_blink");
		if (project_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*cursor_blink_out = 1;
			return EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
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
                                                                const char *global_path,
                                                                const char *project_path) {
	if (line_numbers_out == NULL) {
		return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
	}

	int line_numbers = 1;
	enum editorLineNumbersLoadStatus status = EDITOR_LINE_NUMBERS_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorBoolFileStatus global_status =
		        configEditorBoolApplyFile(&line_numbers, global_path, "line_numbers");
		if (global_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*line_numbers_out = 1;
			return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
			line_numbers = 1;
			status = (enum editorLineNumbersLoadStatus)(
			        status | EDITOR_LINE_NUMBERS_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorBoolFileStatus project_status =
		        configEditorBoolApplyFile(&line_numbers, project_path, "line_numbers");
		if (project_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*line_numbers_out = 1;
			return EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
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

enum editorCurrentLineHighlightLoadStatus
editorCurrentLineHighlightLoadFromPaths(int *current_line_highlight_out, const char *global_path,
                                        const char *project_path) {
	if (current_line_highlight_out == NULL) {
		return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
	}

	int current_line_highlight = 1;
	enum editorCurrentLineHighlightLoadStatus status = EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorBoolFileStatus global_status = configEditorBoolApplyFile(
		        &current_line_highlight, global_path, "current_line_highlight");
		if (global_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*current_line_highlight_out = 1;
			return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
			current_line_highlight = 1;
			status = (enum editorCurrentLineHighlightLoadStatus)(
			        status | EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorBoolFileStatus project_status = configEditorBoolApplyFile(
		        &current_line_highlight, project_path, "current_line_highlight");
		if (project_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*current_line_highlight_out = 1;
			return EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
			current_line_highlight = 1;
			status = (enum editorCurrentLineHighlightLoadStatus)(
			        status | EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT);
		}
	}

	*current_line_highlight_out = current_line_highlight;
	return status;
}

enum editorCurrentLineHighlightLoadStatus
editorCurrentLineHighlightLoadConfigured(int *current_line_highlight_out) {
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

	enum editorCurrentLineHighlightLoadStatus status = editorCurrentLineHighlightLoadFromPaths(
	        current_line_highlight_out, global_path, NULL);
	free(global_path);
	return status;
}

enum editorNerdFontsLoadStatus editorNerdFontsLoadFromPaths(int *nerd_fonts_out,
                                                            const char *global_path,
                                                            const char *project_path) {
	if (nerd_fonts_out == NULL) {
		return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
	}

	int nerd_fonts = 0;
	enum editorNerdFontsLoadStatus status = EDITOR_NERD_FONTS_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorBoolFileStatus global_status =
		        configEditorBoolApplyFile(&nerd_fonts, global_path, "nerd_fonts");
		if (global_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*nerd_fonts_out = 0;
			return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
			nerd_fonts = 0;
			status = (enum editorNerdFontsLoadStatus)(
			        status | EDITOR_NERD_FONTS_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorBoolFileStatus project_status =
		        configEditorBoolApplyFile(&nerd_fonts, project_path, "nerd_fonts");
		if (project_status == CONFIG_EDITOR_BOOL_FILE_OUT_OF_MEMORY) {
			*nerd_fonts_out = 0;
			return EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_BOOL_FILE_INVALID) {
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

enum configEditorIndentFileStatus {
	CONFIG_EDITOR_INDENT_FILE_APPLIED = 0,
	CONFIG_EDITOR_INDENT_FILE_MISSING,
	CONFIG_EDITOR_INDENT_FILE_INVALID,
	CONFIG_EDITOR_INDENT_FILE_OUT_OF_MEMORY
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

static int configEditorParseIndentStyleValue(const char *value, int *indent_use_tabs_out) {
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

static int configEditorParseIndentWidthValue(const char *value, int *indent_width_out) {
	if (value == NULL || indent_width_out == NULL) {
		return 0;
	}
	errno = 0;
	char *end = NULL;
	long width = strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || width < 1 ||
	    width > ROTIDE_INDENT_WIDTH_MAX) {
		return 0;
	}
	*indent_width_out = (int)width;
	return 1;
}

struct configEditorIndentApplyContext {
	int auto_indent;
	int indent_use_tabs;
	int indent_width;
};

static int configEditorIndentOnEntry(void *ctx, const char *key, char *value) {
	struct configEditorIndentApplyContext *apply = ctx;
	if (strcmp(key, "auto_indent") == 0) {
		return configEditorParseBoolValue(value, &apply->auto_indent);
	}
	if (strcmp(key, "indent_style") == 0) {
		char style_value[32];
		int parsed = 0;
		if (!editorConfigParseQuotedValue(value, style_value, sizeof(style_value)) ||
		    !configEditorParseIndentStyleValue(style_value, &parsed)) {
			return 0;
		}
		apply->indent_use_tabs = parsed;
		return 1;
	}
	if (strcmp(key, "indent_width") == 0) {
		return configEditorParseIndentWidthValue(value, &apply->indent_width);
	}
	return 1;
}

static enum configEditorIndentFileStatus configEditorIndentApplyFile(int *auto_indent_in_out,
                                                                     int *indent_use_tabs_in_out,
                                                                     int *indent_width_in_out,
                                                                     const char *path) {
	struct configEditorIndentApplyContext apply = {.auto_indent = *auto_indent_in_out,
	                                               .indent_use_tabs = *indent_use_tabs_in_out,
	                                               .indent_width = *indent_width_in_out};
	struct editorConfigScanner scanner = {configEditorOnEditorSection,
	                                      configEditorIndentOnEntry};

	switch (editorConfigScanFile(path, &scanner, &apply)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return CONFIG_EDITOR_INDENT_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			*auto_indent_in_out = apply.auto_indent;
			*indent_use_tabs_in_out = apply.indent_use_tabs;
			*indent_width_in_out = apply.indent_width;
			return CONFIG_EDITOR_INDENT_FILE_APPLIED;
		default:
			return CONFIG_EDITOR_INDENT_FILE_INVALID;
	}
}

enum editorIndentConfigLoadStatus editorIndentConfigLoadFromPaths(int *auto_indent_out,
                                                                  int *indent_use_tabs_out,
                                                                  int *indent_width_out,
                                                                  const char *global_path,
                                                                  const char *project_path) {
	if (auto_indent_out == NULL || indent_use_tabs_out == NULL || indent_width_out == NULL) {
		return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	int auto_indent = 0;
	int indent_use_tabs = 0;
	int indent_width = ROTIDE_INDENT_WIDTH_DEFAULT;
	enum editorIndentConfigLoadStatus status = EDITOR_INDENT_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum configEditorIndentFileStatus global_status = configEditorIndentApplyFile(
		        &auto_indent, &indent_use_tabs, &indent_width, global_path);
		if (global_status == CONFIG_EDITOR_INDENT_FILE_OUT_OF_MEMORY) {
			editorIndentConfigInitDefaults(auto_indent_out, indent_use_tabs_out,
			                               indent_width_out);
			return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == CONFIG_EDITOR_INDENT_FILE_INVALID) {
			editorIndentConfigInitDefaults(&auto_indent, &indent_use_tabs,
			                               &indent_width);
			status = (enum editorIndentConfigLoadStatus)(
			        status | EDITOR_INDENT_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorIndentFileStatus project_status = configEditorIndentApplyFile(
		        &auto_indent, &indent_use_tabs, &indent_width, project_path);
		if (project_status == CONFIG_EDITOR_INDENT_FILE_OUT_OF_MEMORY) {
			editorIndentConfigInitDefaults(auto_indent_out, indent_use_tabs_out,
			                               indent_width_out);
			return EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == CONFIG_EDITOR_INDENT_FILE_INVALID) {
			editorIndentConfigInitDefaults(&auto_indent, &indent_use_tabs,
			                               &indent_width);
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
                                                                   int *indent_use_tabs_out,
                                                                   int *indent_width_out) {
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

static int configEditorColumnSelectDragModifierTokenBit(const char *token, int *bit_out,
                                                        int *is_none) {
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
			if (!configEditorColumnSelectDragModifierTokenBit(token, &bit, &is_none)) {
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

enum configEditorColumnSelectDragModifierFileStatus {
	CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_APPLIED = 0,
	CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_MISSING,
	CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID,
	CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_OUT_OF_MEMORY
};

static int configEditorColumnSelectDragModifierOnEntry(void *ctx, const char *key, char *value) {
	int *modifier = ctx;
	if (strcmp(key, "column_select_drag_modifier") != 0) {
		return 1;
	}
	return editorParseColumnSelectDragModifierValue(value, modifier);
}

static enum configEditorColumnSelectDragModifierFileStatus
configEditorColumnSelectDragModifierApplyFile(int *modifier_in_out, const char *path) {
	int updated = *modifier_in_out;
	struct editorConfigScanner scanner = {configEditorOnEditorSection,
	                                      configEditorColumnSelectDragModifierOnEntry};

	switch (editorConfigScanFile(path, &scanner, &updated)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			*modifier_in_out = updated;
			return CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_APPLIED;
		default:
			return CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID;
	}
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
		enum configEditorColumnSelectDragModifierFileStatus s =
		        configEditorColumnSelectDragModifierApplyFile(&modifier, global_path);
		if (s == CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_OUT_OF_MEMORY) {
			*modifier_out = default_modifier;
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
		}
		if (s == CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID) {
			modifier = default_modifier;
			status = (enum editorColumnSelectDragModifierLoadStatus)(
			        status | EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum configEditorColumnSelectDragModifierFileStatus s =
		        configEditorColumnSelectDragModifierApplyFile(&modifier, project_path);
		if (s == CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_OUT_OF_MEMORY) {
			*modifier_out = default_modifier;
			return EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY;
		}
		if (s == CONFIG_EDITOR_COLUMN_SELECT_DRAG_MODIFIER_FILE_INVALID) {
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

struct configEditorScrollbackContext {
	int value;
	int found;
};

static int configEditorOnTerminalSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "terminal") == 0;
}

static int configEditorScrollbackOnEntry(void *ctx, const char *key, char *value) {
	struct configEditorScrollbackContext *apply = ctx;
	if (strcmp(key, "scrollback_lines") != 0) {
		return 1;
	}
	char *end = NULL;
	errno = 0;
	long parsed = strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0') {
		return 0;
	}
	if (parsed < 0) {
		parsed = 0;
	}
	if (parsed > 1000000) {
		parsed = 1000000;
	}
	apply->value = (int)parsed;
	apply->found = 1;
	return 1;
}

static int configEditorParseTerminalScrollbackLines(const char *path, int *value_out) {
	struct configEditorScrollbackContext apply = {.value = *value_out, .found = 0};
	struct editorConfigScanner scanner = {configEditorOnTerminalSection,
	                                      configEditorScrollbackOnEntry};

	if (editorConfigScanFile(path, &scanner, &apply) != EDITOR_CONFIG_SCAN_OK || !apply.found) {
		return 0;
	}
	*value_out = apply.value;
	return 1;
}

void editorTerminalConfigLoadConfigured(void) {
	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		return;
	}
	int lines = editorTerminalPaneGetDefaultScrollbackLines();
	if (configEditorParseTerminalScrollbackLines(global_path, &lines)) {
		editorTerminalPaneSetDefaultScrollbackLines(lines);
	}
	free(global_path);
}
