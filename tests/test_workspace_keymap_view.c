#include "config/common.h"
#include "config/editor_config.h"
#include "config/keymap.h"
#include "input/system_vim.h"
#include "render/popup.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/tabs.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_editor_cursor_style_load_valid_values_case_insensitive(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-valid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	struct {
		const char *value;
		enum editorCursorStyle expected;
	} cases[] = {
	        {"BLOCK", EDITOR_CURSOR_STYLE_BLOCK},
	        {"bar", EDITOR_CURSOR_STYLE_BAR},
	        {"UnderLine", EDITOR_CURSOR_STYLE_UNDERLINE},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char content[128];
		int written = snprintf(content, sizeof(content),
		                       "[editor]\ncursor_style = \"%s\"\n", cases[i].value);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
		enum editorCursorStyleLoadStatus status =
		        editorCursorStyleLoadFromPaths(&style, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_OK, status);
		ASSERT_EQ_INT(cases[i].expected, style);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_global_then_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "cursor_style = \"block\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "cursor_style = \"underline\"\n"));

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status =
	        editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_OK, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_UNDERLINE, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_invalid_values_fallback_to_bar(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "cursor_style = \"invalid\"\n"));
	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_UNDERLINE;
	enum editorCursorStyleLoadStatus status =
	        editorCursorStyleLoadFromPaths(&style, global_path, NULL);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "cursor_style = \"block\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "cursor_style = \"not-real\"\n"));
	style = EDITOR_CURSOR_STYLE_UNDERLINE;
	status = editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "cursor_style = \"bad-global\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "cursor_style = \"bad-project\"\n"));
	style = EDITOR_CURSOR_STYLE_UNDERLINE;
	status = editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL |
	                      EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT,
	              status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_load_configured_ignores_project(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char *original_cwd = NULL;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char global_path[512] = "";
	char project_path[512] = "";
	char root_template[] = "/tmp/rotide-test-cursor-style-configured-XXXXXX";

	if (!backup_env_var(&home_backup, "HOME")) {
		return 1;
	}

	original_cwd = getcwd(NULL, 0);
	if (original_cwd == NULL) {
		(void)restore_env_var(&home_backup);
		return 1;
	}

	char *root_path = mkdtemp(root_template);
	if (root_path == NULL) {
		goto cleanup;
	}

	if (!path_join(home_dir, sizeof(home_dir), root_path, "home")) {
		goto cleanup;
	}
	if (mkdir(home_dir, 0700) == -1) {
		goto cleanup;
	}
	if (!path_join(dot_rotide_dir, sizeof(dot_rotide_dir), home_dir, ".rotide")) {
		goto cleanup;
	}
	if (mkdir(dot_rotide_dir, 0700) == -1) {
		goto cleanup;
	}
	if (!path_join(global_path, sizeof(global_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (!path_join(project_path, sizeof(project_path), root_path, ".rotide.toml")) {
		goto cleanup;
	}
	if (!write_text_file(global_path, "[editor]\n"
	                                  "cursor_style = \"block\"\n")) {
		goto cleanup;
	}
	if (!write_text_file(project_path, "[editor]\n"
	                                   "cursor_style = \"underline\"\n")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (chdir(root_path) != 0) {
		goto cleanup;
	}

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status = editorCursorStyleLoadConfigured(&style);
	if (status != EDITOR_CURSOR_STYLE_LOAD_OK) {
		goto cleanup;
	}
	if (style != EDITOR_CURSOR_STYLE_BLOCK) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (original_cwd != NULL) {
		if (chdir(original_cwd) != 0) {
			failed = 1;
		}
	}
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (project_path[0] != '\0') {
		(void)unlink(project_path);
	}
	if (global_path[0] != '\0') {
		(void)unlink(global_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	free(original_cwd);
	return failed;
}

static int test_editor_cursor_blink_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-blink-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "cursor_blink = true\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "cursor_blink = false\n"
	                                          "cursor_style = \"underline\"\n"));

	int cursor_blink = 1;
	enum editorCursorBlinkLoadStatus status =
	        editorCursorBlinkLoadFromPaths(&cursor_blink, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_BLINK_LOAD_OK, status);
	ASSERT_EQ_INT(0, cursor_blink);

	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "cursor_blink = maybe\n"));
	cursor_blink = 0;
	status = editorCursorBlinkLoadFromPaths(&cursor_blink, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_BLINK_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(1, cursor_blink);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_line_wrap_load_valid_bool_values(void) {
	char dir_template[] = "/tmp/rotide-test-line-wrap-valid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	struct {
		const char *value;
		int expected;
	} cases[] = {
	        {"true", 1},
	        {"false", 0},
	        {"TRUE", 1},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char content[128];
		int written = snprintf(content, sizeof(content), "[editor]\nline_wrap = %s\n",
		                       cases[i].value);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		int line_wrap = 0;
		enum editorLineWrapLoadStatus status =
		        editorLineWrapLoadFromPaths(&line_wrap, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_LINE_WRAP_LOAD_OK, status);
		ASSERT_EQ_INT(cases[i].expected, line_wrap);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_line_wrap_global_then_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-line-wrap-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "line_wrap = true\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "line_wrap = false\n"));

	int line_wrap = 1;
	enum editorLineWrapLoadStatus status =
	        editorLineWrapLoadFromPaths(&line_wrap, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_WRAP_LOAD_OK, status);
	ASSERT_EQ_INT(0, line_wrap);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_line_wrap_invalid_values_fallback_to_false(void) {
	char dir_template[] = "/tmp/rotide-test-line-wrap-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "line_wrap = \"yes\"\n"));

	int line_wrap = 1;
	enum editorLineWrapLoadStatus status =
	        editorLineWrapLoadFromPaths(&line_wrap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_WRAP_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(0, line_wrap);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_line_numbers_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-line-numbers-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "line_numbers = true\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "line_numbers = false\n"));

	int line_numbers = 1;
	enum editorLineNumbersLoadStatus status =
	        editorLineNumbersLoadFromPaths(&line_numbers, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_NUMBERS_LOAD_OK, status);
	ASSERT_EQ_INT(0, line_numbers);

	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "line_numbers = maybe\n"));
	line_numbers = 0;
	status = editorLineNumbersLoadFromPaths(&line_numbers, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_NUMBERS_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(1, line_numbers);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_indent_config_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-indent-config-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "auto_indent = true\n"
	                                         "indent_style = \"spaces\"\n"
	                                         "indent_width = 2\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "indent_style = \"tabs\"\n"
	                                          "indent_width = 4\n"));

	int auto_indent = 0;
	int indent_use_tabs = 0;
	int indent_width = 0;
	enum editorIndentConfigLoadStatus status = editorIndentConfigLoadFromPaths(
	        &auto_indent, &indent_use_tabs, &indent_width, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_INDENT_CONFIG_LOAD_OK, status);
	ASSERT_EQ_INT(1, auto_indent);
	ASSERT_EQ_INT(1, indent_use_tabs);
	ASSERT_EQ_INT(4, indent_width);

	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "auto_indent = true\n"
	                                          "indent_width = 0\n"));
	auto_indent = 1;
	indent_use_tabs = 1;
	indent_width = 8;
	status = editorIndentConfigLoadFromPaths(&auto_indent, &indent_use_tabs, &indent_width,
	                                         NULL, project_path);
	ASSERT_EQ_INT(EDITOR_INDENT_CONFIG_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(0, auto_indent);
	ASSERT_EQ_INT(0, indent_use_tabs);
	ASSERT_EQ_INT(ROTIDE_INDENT_WIDTH_DEFAULT, indent_width);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_current_line_highlight_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-current-line-highlight-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "current_line_highlight = false\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "current_line_highlight = true\n"));

	int current_line_highlight = 0;
	enum editorCurrentLineHighlightLoadStatus status = editorCurrentLineHighlightLoadFromPaths(
	        &current_line_highlight, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OK, status);
	ASSERT_EQ_INT(1, current_line_highlight);

	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "current_line_highlight = \"yes\"\n"));
	current_line_highlight = 0;
	status = editorCurrentLineHighlightLoadFromPaths(&current_line_highlight, NULL,
	                                                 project_path);
	ASSERT_EQ_INT(EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(1, current_line_highlight);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_nerd_fonts_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-nerd-fonts-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "nerd_fonts = false\n"));
	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "nerd_fonts = true\n"));

	int nerd_fonts = 0;
	enum editorNerdFontsLoadStatus status =
	        editorNerdFontsLoadFromPaths(&nerd_fonts, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_NERD_FONTS_LOAD_OK, status);
	ASSERT_EQ_INT(1, nerd_fonts);

	ASSERT_TRUE(write_text_file(project_path, "[editor]\n"
	                                          "nerd_fonts = \"yes\"\n"));
	nerd_fonts = 1;
	status = editorNerdFontsLoadFromPaths(&nerd_fonts, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_NERD_FONTS_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(0, nerd_fonts);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_parse_column_select_drag_modifier_value(void) {
	int value = 0;
	ASSERT_TRUE(editorParseColumnSelectDragModifierValue("alt", &value));
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT, value);
	ASSERT_TRUE(editorParseColumnSelectDragModifierValue("alt+shift", &value));
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT | EDITOR_MOUSE_MOD_SHIFT, value);
	ASSERT_TRUE(editorParseColumnSelectDragModifierValue("Shift+ALT", &value));
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT | EDITOR_MOUSE_MOD_SHIFT, value);
	ASSERT_TRUE(editorParseColumnSelectDragModifierValue("ctrl+alt", &value));
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT | EDITOR_MOUSE_MOD_CTRL, value);
	ASSERT_TRUE(editorParseColumnSelectDragModifierValue("none", &value));
	ASSERT_EQ_INT(0, value);
	ASSERT_TRUE(editorParseColumnSelectDragModifierValue("\"alt+shift\"", &value));
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT | EDITOR_MOUSE_MOD_SHIFT, value);

	ASSERT_TRUE(!editorParseColumnSelectDragModifierValue("", &value));
	ASSERT_TRUE(!editorParseColumnSelectDragModifierValue("foo", &value));
	ASSERT_TRUE(!editorParseColumnSelectDragModifierValue("alt+", &value));
	ASSERT_TRUE(!editorParseColumnSelectDragModifierValue("+alt", &value));
	ASSERT_TRUE(!editorParseColumnSelectDragModifierValue("alt+alt", &value));
	ASSERT_TRUE(!editorParseColumnSelectDragModifierValue("alt+none", &value));
	return 0;
}

static int test_editor_column_select_drag_modifier_load_from_paths(void) {
	char dir_template[] = "/tmp/rotide-test-cs-mod-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
	                            "[editor]\ncolumn_select_drag_modifier = \"ctrl+alt\"\n"));

	int modifier = 0;
	enum editorColumnSelectDragModifierLoadStatus status =
	        editorColumnSelectDragModifierLoadFromPaths(&modifier, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OK, status);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT | EDITOR_MOUSE_MOD_CTRL, modifier);

	ASSERT_TRUE(write_text_file(project_path,
	                            "[editor]\ncolumn_select_drag_modifier = \"banana\"\n"));
	status = editorColumnSelectDragModifierLoadFromPaths(&modifier, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_ALT, modifier);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int keymap_vim_send_key(int key) {
	int effects = 0;
	return editorVimHandleKey(key, &effects);
}

static int test_editor_keymap_vim_binding_overrides_default(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-override-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];
	enum editorKeymapLoadStatus status = EDITOR_KEYMAP_LOAD_OK;

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.move_right = \";\"\n"));

	editorVimReset();
	status = editorKeymapLoadVimBindings(NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	add_row("abcdef");
	E.cy = 0;
	E.cx = 0;
	/* The rebound key moves right. */
	ASSERT_EQ_INT(0, keymap_vim_send_key(';'));
	ASSERT_EQ_INT(1, E.cx);
	/* The relocated default is disabled. */
	E.cx = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key('l'));
	ASSERT_EQ_INT(0, E.cx);
	/* An untouched default still works. */
	E.cx = 2;
	ASSERT_EQ_INT(0, keymap_vim_send_key('h'));
	ASSERT_EQ_INT(1, E.cx);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_per_mode_binding(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-mode-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "visual.move_right = \";\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, editorKeymapLoadVimBindings(NULL, project_path));

	add_row("abcdef");
	E.cy = 0;
	E.cx = 0;
	/* The visual-mode binding does not affect normal mode. */
	ASSERT_EQ_INT(0, keymap_vim_send_key(';'));
	ASSERT_EQ_INT(0, E.cx);
	/* In visual mode the rebound key extends right. */
	ASSERT_EQ_INT(0, keymap_vim_send_key('v'));
	ASSERT_EQ_INT(0, keymap_vim_send_key(';'));
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(0, keymap_vim_send_key('\x1b'));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_accepts_git_blame_details_binding(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-gitblame-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "leader.git_blame_details = \";\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, editorKeymapLoadVimBindings(NULL, project_path));

	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	E.filename = strdup("/tmp/rotide-vim-keymap-gitblame.c");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key(' '));
	ASSERT_TRUE(keymap_vim_send_key(';') >= 0);
	ASSERT_EQ_STR("No Git repository", E.statusmsg);
	ASSERT_TRUE(!editorPopupIsVisible());

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_unknown_command_is_rejected(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-bad-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.not_a_command = \"x\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	              editorKeymapLoadVimBindings(NULL, project_path));

	/* Defaults remain intact after the rejected file. */
	add_row("abc");
	E.cy = 0;
	E.cx = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key('l'));
	ASSERT_EQ_INT(1, E.cx);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_leader_key_rebind(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-leaderkey-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.leader = \",\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, editorKeymapLoadVimBindings(NULL, project_path));

	add_row("abc");
	E.cy = 0;
	E.cx = 0;
	/* The rebound leader opens the file search drawer. */
	ASSERT_EQ_INT(0, keymap_vim_send_key(','));
	(void)keymap_vim_send_key('p');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	/* Space is no longer the leader, so it does not start a sequence. */
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	ASSERT_EQ_INT(0, keymap_vim_send_key(' '));
	ASSERT_EQ_INT(0, E.input_vim_pending_leader);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_leader_space_alias(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-leaderspace-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.leader = \"space\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, editorKeymapLoadVimBindings(NULL, project_path));

	add_row("abc");
	E.cy = 0;
	E.cx = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key(' '));
	(void)keymap_vim_send_key('m');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_leader_subkey_rebind(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-leadersub-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "leader.find_file = \"o\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, editorKeymapLoadVimBindings(NULL, project_path));

	add_row("abc");
	E.cy = 0;
	E.cx = 0;
	/* The rebound sub-key reaches file search. */
	ASSERT_EQ_INT(0, keymap_vim_send_key(' '));
	(void)keymap_vim_send_key('o');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	/* The default sub-key was relocated and no longer triggers it. */
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	ASSERT_EQ_INT(0, keymap_vim_send_key(' '));
	(void)keymap_vim_send_key('p');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_leader_unknown_action_is_rejected(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-leaderbad-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "leader.not_an_action = \"x\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	              editorKeymapLoadVimBindings(NULL, project_path));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_unknown_mode_is_rejected(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-badmode-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "elsewhere.move_left = \";\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	              editorKeymapLoadVimBindings(NULL, project_path));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_structural_key_is_rejected(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-structural-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.search_forward = \"q\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	              editorKeymapLoadVimBindings(NULL, project_path));

	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.move_left = \"3\"\n"));

	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT,
	              editorKeymapLoadVimBindings(NULL, project_path));

	add_row("abcdef");
	E.cy = 0;
	E.cx = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key('3'));
	ASSERT_EQ_INT(0, keymap_vim_send_key('l'));
	ASSERT_EQ_INT(3, E.cx);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_rebindings_preserve_structural_grammar(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-grammar-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.line_start = \"q\"\n"
	                                          "normal.insert = \"z\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, editorKeymapLoadVimBindings(NULL, project_path));

	add_row("alpha beta gamma zzz");
	E.cy = 0;
	E.cx = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key('1'));
	ASSERT_EQ_INT(0, keymap_vim_send_key('0'));
	ASSERT_EQ_INT(0, keymap_vim_send_key('l'));
	ASSERT_EQ_INT(10, E.cx);

	E.cx = 6;
	ASSERT_EQ_INT(0, keymap_vim_send_key('d'));
	ASSERT_EQ_INT(0, keymap_vim_send_key('i'));
	ASSERT_EQ_INT(0, keymap_vim_send_key('w'));
	ASSERT_ROW_TEXT_EQ(0, "alpha  gamma zzz");

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_vim_project_overrides_global(void) {
	char dir_template[] = "/tmp/rotide-test-vimkeymap-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	char global_path[512];
	char project_path[512];

	ASSERT_TRUE(dir_path != NULL);
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(write_text_file(global_path, "[keymap.vim]\n"
	                                         "normal.move_right = \";\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[keymap.vim]\n"
	                                          "normal.move_right = \",\"\n"));

	editorVimReset();
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK,
	              editorKeymapLoadVimBindings(global_path, project_path));

	add_row("abcdef");
	E.cy = 0;
	E.cx = 0;
	/* The project binding wins. */
	ASSERT_EQ_INT(0, keymap_vim_send_key(','));
	ASSERT_EQ_INT(1, E.cx);
	/* The global binding no longer applies. */
	E.cx = 0;
	ASSERT_EQ_INT(0, keymap_vim_send_key(';'));
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

const struct editorTestCase g_workspace_keymap_view_tests[] = {
        {"editor_cursor_style_load_valid_values_case_insensitive",
         test_editor_cursor_style_load_valid_values_case_insensitive},
        {"editor_cursor_style_global_then_project_precedence",
         test_editor_cursor_style_global_then_project_precedence},
        {"editor_cursor_style_invalid_values_fallback_to_bar",
         test_editor_cursor_style_invalid_values_fallback_to_bar},
        {"editor_cursor_style_load_configured_ignores_project",
         test_editor_cursor_style_load_configured_ignores_project},
        {"editor_cursor_blink_load_precedence_and_invalid_fallback",
         test_editor_cursor_blink_load_precedence_and_invalid_fallback},
        {"editor_line_wrap_load_valid_bool_values", test_editor_line_wrap_load_valid_bool_values},
        {"editor_line_wrap_global_then_project_precedence",
         test_editor_line_wrap_global_then_project_precedence},
        {"editor_line_wrap_invalid_values_fallback_to_false",
         test_editor_line_wrap_invalid_values_fallback_to_false},
        {"editor_line_numbers_load_precedence_and_invalid_fallback",
         test_editor_line_numbers_load_precedence_and_invalid_fallback},
        {"editor_indent_config_load_precedence_and_invalid_fallback",
         test_editor_indent_config_load_precedence_and_invalid_fallback},
        {"editor_current_line_highlight_load_precedence_and_invalid_fallback",
         test_editor_current_line_highlight_load_precedence_and_invalid_fallback},
        {"editor_nerd_fonts_load_precedence_and_invalid_fallback",
         test_editor_nerd_fonts_load_precedence_and_invalid_fallback},
        {"editor_parse_column_select_drag_modifier_value",
         test_editor_parse_column_select_drag_modifier_value},
        {"editor_column_select_drag_modifier_load_from_paths",
         test_editor_column_select_drag_modifier_load_from_paths},
        {"editor_keymap_vim_binding_overrides_default",
         test_editor_keymap_vim_binding_overrides_default},
        {"editor_keymap_vim_per_mode_binding", test_editor_keymap_vim_per_mode_binding},
        {"editor_keymap_vim_accepts_git_blame_details_binding",
         test_editor_keymap_vim_accepts_git_blame_details_binding},
        {"editor_keymap_vim_unknown_command_is_rejected",
         test_editor_keymap_vim_unknown_command_is_rejected},
        {"editor_keymap_vim_leader_key_rebind", test_editor_keymap_vim_leader_key_rebind},
        {"editor_keymap_vim_leader_space_alias", test_editor_keymap_vim_leader_space_alias},
        {"editor_keymap_vim_leader_subkey_rebind", test_editor_keymap_vim_leader_subkey_rebind},
        {"editor_keymap_vim_leader_unknown_action_is_rejected",
         test_editor_keymap_vim_leader_unknown_action_is_rejected},
        {"editor_keymap_vim_unknown_mode_is_rejected",
         test_editor_keymap_vim_unknown_mode_is_rejected},
        {"editor_keymap_vim_structural_key_is_rejected",
         test_editor_keymap_vim_structural_key_is_rejected},
        {"editor_keymap_vim_rebindings_preserve_structural_grammar",
         test_editor_keymap_vim_rebindings_preserve_structural_grammar},
        {"editor_keymap_vim_project_overrides_global",
         test_editor_keymap_vim_project_overrides_global},
};

const int g_workspace_keymap_view_test_count =
        (int)(sizeof(g_workspace_keymap_view_tests) / sizeof(g_workspace_keymap_view_tests[0]));
