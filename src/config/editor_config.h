#ifndef CONFIG_EDITOR_CONFIG_H
#define CONFIG_EDITOR_CONFIG_H

#include "rotide.h"

enum editorCursorStyleLoadStatus {
	EDITOR_CURSOR_STYLE_LOAD_OK = 0,
	EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorLineWrapLoadStatus {
	EDITOR_LINE_WRAP_LOAD_OK = 0,
	EDITOR_LINE_WRAP_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_LINE_WRAP_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorCursorBlinkLoadStatus {
	EDITOR_CURSOR_BLINK_LOAD_OK = 0,
	EDITOR_CURSOR_BLINK_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_CURSOR_BLINK_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorLineNumbersLoadStatus {
	EDITOR_LINE_NUMBERS_LOAD_OK = 0,
	EDITOR_LINE_NUMBERS_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_LINE_NUMBERS_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorCurrentLineHighlightLoadStatus {
	EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OK = 0,
	EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorColumnSelectDragModifierLoadStatus {
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OK = 0,
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY = 1 << 2
};

enum editorCursorStyleLoadStatus editorCursorStyleLoadFromPaths(enum editorCursorStyle *style_out,
		const char *global_path, const char *project_path);
enum editorCursorStyleLoadStatus editorCursorStyleLoadConfigured(enum editorCursorStyle *style_out);
enum editorLineWrapLoadStatus editorLineWrapLoadFromPaths(int *line_wrap_out,
		const char *global_path, const char *project_path);
enum editorLineWrapLoadStatus editorLineWrapLoadConfigured(int *line_wrap_out);
enum editorCursorBlinkLoadStatus editorCursorBlinkLoadFromPaths(int *cursor_blink_out,
		const char *global_path, const char *project_path);
enum editorCursorBlinkLoadStatus editorCursorBlinkLoadConfigured(int *cursor_blink_out);
enum editorLineNumbersLoadStatus editorLineNumbersLoadFromPaths(int *line_numbers_out,
		const char *global_path, const char *project_path);
enum editorLineNumbersLoadStatus editorLineNumbersLoadConfigured(int *line_numbers_out);
enum editorCurrentLineHighlightLoadStatus editorCurrentLineHighlightLoadFromPaths(
		int *current_line_highlight_out, const char *global_path, const char *project_path);
enum editorCurrentLineHighlightLoadStatus editorCurrentLineHighlightLoadConfigured(
		int *current_line_highlight_out);

enum editorColumnSelectDragModifierLoadStatus editorColumnSelectDragModifierLoadFromPaths(
		int *modifier_out, const char *global_path, const char *project_path);
enum editorColumnSelectDragModifierLoadStatus editorColumnSelectDragModifierLoadConfigured(
		int *modifier_out);
int editorParseColumnSelectDragModifierValue(const char *value, int *modifier_out);

#endif
