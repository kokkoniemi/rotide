#include "config/common.h"
#include "config/dap_config.h"
#include "config/editor_config.h"
#include "config/keymap.h"
#include "config/theme_config.h"
#include "editing/edit.h"
#include "input/actions_file_tab.h"
#include "language/syntax.h"
#include "render/ansi_style.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/tabs.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int theme_color_is_ansi(struct editorThemeColor color, enum editorThemeAnsiColor ansi) {
	return color.kind == EDITOR_THEME_COLOR_ANSI && color.value == (unsigned char)ansi;
}

static int theme_color_is_256(struct editorThemeColor color, unsigned char value) {
	return color.kind == EDITOR_THEME_COLOR_256 && color.value == value;
}

static int theme_color_is_rgb(struct editorThemeColor color, unsigned char r, unsigned char g,
                              unsigned char b) {
	return color.kind == EDITOR_THEME_COLOR_RGB && color.r == r && color.g == g && color.b == b;
}

static int test_editor_theme_load_builtin_global_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-theme-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[theme]\n"
	                                         "name = \"a11y-dark\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[theme]\n"
	                                          "name = \"a11y-light\"\n"));

	struct editorTheme theme;
	enum editorThemeLoadStatus status =
	        editorThemeLoadFromPaths(&theme, global_path, project_path, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_OK, status);
	ASSERT_EQ_STR("a11y-light", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0xFE, 0xFE, 0xFE));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x32, 0x6B, 0xAD));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_theme_load_ignores_non_theme_sections(void) {
	char dir_template[] = "/tmp/rotide-test-theme-mixed-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[editor]\n"
	                                         "cursor_style = \"bar\"\n"
	                                         "line_numbers = true\n"
	                                         "\n"
	                                         "[theme]\n"
	                                         "name = \"github-dark\"\n"
	                                         "\n"
	                                         "[lsp]\n"
	                                         "gopls_command = \"gopls\"\n"
	                                         "\n"
	                                         "[keymap]\n"
	                                         "save = \"ctrl+s\"\n"));

	struct editorTheme theme;
	enum editorThemeLoadStatus status =
	        editorThemeLoadFromPaths(&theme, global_path, NULL, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_OK, status);
	ASSERT_EQ_STR("github-dark", theme.name);

	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_theme_loads_modus_builtins(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "modus-operandi"));
	ASSERT_EQ_STR("modus-operandi", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0xFF, 0xFF, 0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x53, 0x1A, 0xB6));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_SELECTION].bg, 0xBD, 0xBD,
	                               0xBD));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_TAB_ACTIVE].bg, 0xC8, 0xC8,
	                               0xC8));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "modus-operandi-tinted"));
	ASSERT_EQ_STR("modus-operandi-tinted", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0xFB, 0xF7, 0xF0));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_CURSOR], 0xD0, 0x00, 0x00));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_TAB_ACTIVE].bg, 0xCA, 0xB9,
	                               0xB2));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "modus-vivendi"));
	ASSERT_EQ_STR("modus-vivendi", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x00, 0x00, 0x00));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0xB6, 0xA0, 0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_TAB_ACTIVE].bg, 0x50, 0x50,
	                               0x50));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "modus-vivendi-tinted"));
	ASSERT_EQ_STR("modus-vivendi-tinted", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x0D, 0x0E, 0x1C));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_CURSOR], 0xFF, 0x66, 0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_TAB_ACTIVE].bg, 0x48, 0x4D,
	                               0x67));
	return 0;
}

static int test_editor_theme_loads_github_builtins(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "github-light"));
	ASSERT_EQ_STR("github-light", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0xFF, 0xFF, 0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0x1F, 0x23, 0x28));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0xCF, 0x22, 0x2E));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_STRING], 0x0A, 0x30, 0x69));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_TYPE], 0x1F, 0x23, 0x28));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_VARIABLE], 0x1F, 0x23, 0x28));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_SELECTION].bg, 0xBB, 0xDF,
	                               0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_TAB_ACTIVE].bg, 0xBB, 0xDF,
	                               0xFF));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "github-dark"));
	ASSERT_EQ_STR("github-dark", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x0D, 0x11, 0x17));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0xE6, 0xED, 0xF3));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DIRECTORY], 0x79, 0xC0, 0xFF));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DRAWER_CONNECTOR], 0x30, 0x36, 0x3D));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DRAWER_ICON], 0xB1, 0xBA, 0xC4));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 0x8B, 0x94, 0x9E));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_FUNCTION], 0xD2, 0xA8, 0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_TYPE], 0xE6, 0xED, 0xF3));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_VARIABLE], 0xE6, 0xED, 0xF3));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_CURRENT_LINE_BG], 0x17, 0x1C, 0x23));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_TAB_ACTIVE].bg, 0x24, 0x3B,
	                               0x61));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_SELECTION].bg, 0x24, 0x3B,
	                               0x61));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE].bg,
	                               0x0D, 0x11, 0x17));
	return 0;
}

static int test_editor_theme_loads_acme_builtin(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "acme"));
	ASSERT_EQ_STR("acme", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0xFF, 0xFF, 0xEA));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0x00, 0x00, 0x00));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DRAWER_HEADER_BG], 0xAE, 0xEE, 0xEE));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DIRECTORY], 0x10, 0x10, 0x10));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DRAWER_CONNECTOR], 0x50, 0x50, 0x50));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DRAWER_ICON], 0x50, 0x50, 0x50));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 0x50, 0x50, 0x50));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0xAF, 0x5F, 0x00));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_SELECTION].bg, 0xAE, 0xEE,
	                               0xEE));
	return 0;
}

static int test_editor_theme_loads_silentium_builtin(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "silentium"));
	ASSERT_EQ_STR("silentium", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x14, 0x14, 0x14));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0xE6, 0xE6, 0xE6));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DIRECTORY], 0xFF, 0x7E, 0x6B));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_CURRENT_LINE_BG], 0x28, 0x28, 0x28));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 0x73, 0x73, 0x73));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0xFF, 0x7E, 0x6B));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_STRING], 0xA6, 0xA6, 0xA6));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_FUNCTION], 0xE6, 0xE6, 0xE6));
	return 0;
}

static int test_editor_theme_loads_256noir_builtin(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "256noir"));
	ASSERT_EQ_STR("256noir", theme.name);
	ASSERT_TRUE(theme_color_is_256(theme.ui[EDITOR_THEME_UI_BACKGROUND], 16));
	ASSERT_TRUE(theme_color_is_256(theme.ui[EDITOR_THEME_UI_FOREGROUND], 250));
	ASSERT_TRUE(theme_color_is_256(theme.ui[EDITOR_THEME_UI_DIRECTORY], 255));
	ASSERT_TRUE(theme_color_is_256(theme.ui[EDITOR_THEME_UI_CURRENT_LINE_BG], 233));
	ASSERT_TRUE(theme_color_is_256(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 240));
	ASSERT_TRUE(theme_color_is_256(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 255));
	ASSERT_TRUE(theme_color_is_256(theme.syntax[EDITOR_SYNTAX_HL_STRING], 245));
	ASSERT_TRUE(theme_color_is_256(theme.syntax[EDITOR_SYNTAX_HL_NUMBER], 196));
	ASSERT_TRUE(theme_color_is_256(theme.syntax[EDITOR_SYNTAX_HL_FUNCTION], 255));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "256_noir"));
	ASSERT_EQ_STR("256noir", theme.name);
	return 0;
}

static int test_editor_theme_loads_molokai_builtin(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "molokai"));
	ASSERT_EQ_STR("molokai", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x1B, 0x1D, 0x1E));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0xF8, 0xF8, 0xF2));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DIRECTORY], 0x66, 0xD9, 0xEF));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_CURRENT_LINE_BG], 0x29, 0x37, 0x39));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 0x7E, 0x8E, 0x91));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0xF9, 0x26, 0x72));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_STRING], 0xE6, 0xDB, 0x74));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_FUNCTION], 0xA6, 0xE2, 0x2E));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_NUMBER], 0xAE, 0x81, 0xFF));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_TYPE], 0x66, 0xD9, 0xEF));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_VARIABLE], 0xFD, 0x97, 0x1F));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_SELECTION].bg, 0x40, 0x3D,
	                               0x3D));
	return 0;
}

static int test_editor_theme_loads_kanagawa_builtins(void) {
	struct editorTheme theme;

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "kanagawa-wave"));
	ASSERT_EQ_STR("kanagawa-wave", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x1F, 0x1F, 0x28));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0xDC, 0xD7, 0xBA));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_DIRECTORY], 0x7E, 0x9C, 0xD8));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 0x72, 0x71, 0x69));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x95, 0x7F, 0xB8));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_FUNCTION], 0x7E, 0x9C, 0xD8));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_TYPE], 0x7A, 0xA8, 0x9F));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_STRING], 0x98, 0xBB, 0x6C));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_NUMBER], 0xD2, 0x7E, 0x99));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_CONSTANT], 0xFF, 0xA0, 0x66));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_SELECTION].bg, 0x2D, 0x4F,
	                               0x67));
	ASSERT_TRUE(theme_color_is_rgb(theme.styles[EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE].bg,
	                               0x1F, 0x1F, 0x28));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "kanagawa"));
	ASSERT_EQ_STR("kanagawa-wave", theme.name);

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "kanagawa-dragon"));
	ASSERT_EQ_STR("kanagawa-dragon", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0x18, 0x16, 0x16));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0xC5, 0xC9, 0xC5));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x89, 0x92, 0xA7));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_STRING], 0x87, 0xA9, 0x87));

	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "kanagawa-lotus"));
	ASSERT_EQ_STR("kanagawa-lotus", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_BACKGROUND], 0xF2, 0xEC, 0xBC));
	ASSERT_TRUE(theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_FOREGROUND], 0x1F, 0x1F, 0x28));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x62, 0x4C, 0x83));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_STRING], 0x6F, 0x89, 0x4E));
	return 0;
}

static int test_editor_theme_project_config_cannot_override_theme_colors(void) {
	char dir_template[] = "/tmp/rotide-test-theme-no-project-overrides-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(project_path, "[theme]\n"
	                                          "name = \"a11y-light\"\n"
	                                          "[theme.syntax]\n"
	                                          "keyword = \"red\"\n"
	                                          "[keymap]\n"
	                                          "save = \"ctrl+u\"\n"));

	struct editorTheme theme;
	enum editorThemeLoadStatus theme_status =
	        editorThemeLoadFromPaths(&theme, NULL, project_path, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_INVALID_PROJECT, theme_status);
	ASSERT_EQ_STR("a11y-light", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x32, 0x6B, 0xAD));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
	        editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);
	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('u'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_theme_loads_custom_theme_from_home_themes(void) {
	char dir_template[] = "/tmp/rotide-test-theme-custom-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char dot_rotide[512];
	char themes_dir[512];
	char theme_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(dot_rotide, sizeof(dot_rotide), dir_path, ".rotide"));
	ASSERT_TRUE(path_join(themes_dir, sizeof(themes_dir), dot_rotide, "themes"));
	ASSERT_TRUE(path_join(theme_path, sizeof(theme_path), themes_dir, "custom.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(make_dir(dot_rotide));
	ASSERT_TRUE(make_dir(themes_dir));
	ASSERT_TRUE(write_text_file(theme_path, "name = \"custom\"\n"
	                                        "inherits = \"a11y-dark\"\n"
	                                        "[theme.syntax]\n"
	                                        "comment = \"#010203\"\n"
	                                        "[theme.ui]\n"
	                                        "current_line_bg = \"#0A0B0C\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[theme]\n"
	                                          "name = \"custom\"\n"));

	struct editorTheme theme;
	enum editorThemeLoadStatus status =
	        editorThemeLoadFromPaths(&theme, NULL, project_path, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_OK, status);
	ASSERT_EQ_STR("custom", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_COMMENT], 0x01, 0x02, 0x03));
	ASSERT_TRUE(theme_color_is_rgb(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD], 0x6B, 0xBE, 0xFF));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ui[EDITOR_THEME_UI_CURRENT_LINE_BG], 0x0A, 0x0B, 0x0C));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(theme_path) == 0);
	ASSERT_TRUE(rmdir(themes_dir) == 0);
	ASSERT_TRUE(rmdir(dot_rotide) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_theme_invalid_values_fall_back_to_terminal(void) {
	char dir_template[] = "/tmp/rotide-test-theme-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char dot_rotide[512];
	char themes_dir[512];
	char theme_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(dot_rotide, sizeof(dot_rotide), dir_path, ".rotide"));
	ASSERT_TRUE(path_join(themes_dir, sizeof(themes_dir), dot_rotide, "themes"));
	ASSERT_TRUE(path_join(theme_path, sizeof(theme_path), themes_dir, "bad.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(make_dir(dot_rotide));
	ASSERT_TRUE(make_dir(themes_dir));
	ASSERT_TRUE(write_text_file(theme_path, "name = \"bad\"\n"
	                                        "[theme.syntax]\n"
	                                        "keyword = \"#12\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[theme]\n"
	                                          "name = \"bad\"\n"));

	struct editorTheme theme;
	enum editorThemeLoadStatus status =
	        editorThemeLoadFromPaths(&theme, NULL, project_path, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_INVALID_THEME, status);
	ASSERT_EQ_STR("terminal", theme.name);
	ASSERT_TRUE(theme_color_is_ansi(theme.syntax[EDITOR_SYNTAX_HL_KEYWORD],
	                                EDITOR_THEME_ANSI_BRIGHT_BLUE));

	ASSERT_TRUE(write_text_file(project_path, "[theme]\n"
	                                          "name = \"../bad\"\n"));
	status = editorThemeLoadFromPaths(&theme, NULL, project_path, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_STR("terminal", theme.name);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(theme_path) == 0);
	ASSERT_TRUE(rmdir(themes_dir) == 0);
	ASSERT_TRUE(rmdir(dot_rotide) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_config_ensure_global_creates_default_when_missing(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char config_path[512] = "";
	char root_template[] = "/tmp/rotide-test-bootstrap-XXXXXX";

	if (!backup_env_var(&home_backup, "HOME")) {
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
	if (!path_join(config_path, sizeof(config_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}

	enum editorConfigBootstrapStatus status = editorConfigEnsureGlobalConfig();
	if (status != EDITOR_CONFIG_BOOTSTRAP_CREATED) {
		goto cleanup;
	}

	struct stat st;
	if (stat(config_path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) {
		goto cleanup;
	}

	enum editorConfigBootstrapStatus second = editorConfigEnsureGlobalConfig();
	if (second != EDITOR_CONFIG_BOOTSTRAP_OK) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (config_path[0] != '\0') {
		(void)unlink(config_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	return failed;
}

static int test_editor_open_settings_opens_global_config_in_tab(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char config_path[512] = "";
	char root_template[] = "/tmp/rotide-test-open-settings-XXXXXX";

	if (!backup_env_var(&home_backup, "HOME")) {
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
	if (!path_join(config_path, sizeof(config_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}

	if (!editorTabsInit()) {
		goto cleanup;
	}

	editorOpenSettings();

	struct stat st;
	if (stat(config_path, &st) != 0 || !S_ISREG(st.st_mode)) {
		goto cleanup;
	}
	if (E.filename == NULL || strcmp(E.filename, config_path) != 0) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (config_path[0] != '\0') {
		(void)unlink(config_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	return failed;
}

static int test_editor_save_global_config_can_reload_settings(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char config_path[512] = "";
	char root_template[] = "/tmp/rotide-test-config-save-reload-XXXXXX";
	int saved_stdin = -1;
	int saved_stdout = -1;

	if (!backup_env_var(&home_backup, "HOME")) {
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
	if (!path_join(config_path, sizeof(config_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (!write_text_file(config_path, "[editor]\nline_wrap = true\n")) {
		goto cleanup;
	}
	if (!editorTabsInit() || !editorOpen(config_path)) {
		goto cleanup;
	}

	E.line_wrap_enabled = 0;
	if (setup_stdin_bytes("y\r", 2, &saved_stdin) != 0) {
		goto cleanup;
	}
	if (redirect_stdout_to_devnull(&saved_stdout) != 0) {
		goto cleanup;
	}
	editorSave();
	if (restore_stdout(saved_stdout) != 0) {
		goto cleanup;
	}
	saved_stdout = -1;
	if (restore_stdin(saved_stdin) != 0) {
		goto cleanup;
	}
	saved_stdin = -1;

	if (E.line_wrap_enabled != 1) {
		goto cleanup;
	}
	if (strcmp(E.statusmsg, "Settings reloaded") != 0) {
		goto cleanup;
	}
	if (E.dirty != 0) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (saved_stdout != -1 && !restore_stdout(saved_stdout)) {
		failed = 1;
	}
	if (saved_stdin != -1 && !restore_stdin(saved_stdin)) {
		failed = 1;
	}
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (config_path[0] != '\0') {
		(void)unlink(config_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	return failed;
}

static int test_editor_save_global_config_can_skip_reload(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char config_path[512] = "";
	char root_template[] = "/tmp/rotide-test-config-save-skip-XXXXXX";
	int saved_stdin = -1;
	int saved_stdout = -1;

	if (!backup_env_var(&home_backup, "HOME")) {
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
	if (!path_join(config_path, sizeof(config_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (!write_text_file(config_path, "[editor]\nline_wrap = true\n")) {
		goto cleanup;
	}
	if (!editorTabsInit() || !editorOpen(config_path)) {
		goto cleanup;
	}

	E.line_wrap_enabled = 0;
	if (setup_stdin_bytes("n\r", 2, &saved_stdin) != 0) {
		goto cleanup;
	}
	if (redirect_stdout_to_devnull(&saved_stdout) != 0) {
		goto cleanup;
	}
	editorSave();
	if (restore_stdout(saved_stdout) != 0) {
		goto cleanup;
	}
	saved_stdout = -1;
	if (restore_stdin(saved_stdin) != 0) {
		goto cleanup;
	}
	saved_stdin = -1;

	if (E.line_wrap_enabled != 0) {
		goto cleanup;
	}
	if (strcmp(E.statusmsg, "Settings reload skipped") != 0) {
		goto cleanup;
	}
	if (E.dirty != 0) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (saved_stdout != -1 && !restore_stdout(saved_stdout)) {
		failed = 1;
	}
	if (saved_stdin != -1 && !restore_stdin(saved_stdin)) {
		failed = 1;
	}
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (config_path[0] != '\0') {
		(void)unlink(config_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	return failed;
}

static int test_editor_theme_builtin_kanagawa_wave_populates_ansi_palette(void) {
	struct editorTheme theme;
	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "kanagawa-wave"));

	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_RED], 0xC3, 0x40, 0x43));
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_GREEN], 0x76, 0x94, 0x6A));
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_YELLOW], 0xDC, 0xA5, 0x61));
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_BLUE], 0x7E, 0x9C, 0xD8));
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_MAGENTA], 0x95, 0x7F, 0xB8));
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_CYAN], 0x7A, 0xA8, 0x9F));
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_BRIGHT_RED], 0xC3, 0x40, 0x43));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_BRIGHT_BLUE], 0x7E, 0x9C, 0xD8));
	return 0;
}

static int test_editor_theme_terminal_builtin_leaves_ansi_default(void) {
	struct editorTheme theme;
	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "terminal"));
	for (int i = 0; i < EDITOR_THEME_ANSI_COUNT; i++) {
		ASSERT_TRUE(editorThemeColorIsDefault(theme.ansi[i]));
	}
	return 0;
}

static int test_editor_theme_loads_custom_ansi_table(void) {
	char dir_template[] = "/tmp/rotide-test-theme-ansi-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char dot_rotide[512];
	char themes_dir[512];
	char theme_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(dot_rotide, sizeof(dot_rotide), dir_path, ".rotide"));
	ASSERT_TRUE(path_join(themes_dir, sizeof(themes_dir), dot_rotide, "themes"));
	ASSERT_TRUE(path_join(theme_path, sizeof(theme_path), themes_dir, "ansi-custom.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(make_dir(dot_rotide));
	ASSERT_TRUE(make_dir(themes_dir));
	ASSERT_TRUE(write_text_file(theme_path, "name = \"ansi-custom\"\n"
	                                        "inherits = \"terminal\"\n"
	                                        "[theme.ansi]\n"
	                                        "red = \"#FF0011\"\n"
	                                        "green = \"bright_green\"\n"
	                                        "bright_magenta = \"#AA00BB\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[theme]\n"
	                                          "name = \"ansi-custom\"\n"));

	struct editorTheme theme;
	enum editorThemeLoadStatus status =
	        editorThemeLoadFromPaths(&theme, NULL, project_path, dir_path);
	ASSERT_EQ_INT(EDITOR_THEME_LOAD_OK, status);
	ASSERT_EQ_STR("ansi-custom", theme.name);
	ASSERT_TRUE(theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_RED], 0xFF, 0x00, 0x11));
	ASSERT_TRUE(theme_color_is_ansi(theme.ansi[EDITOR_THEME_ANSI_GREEN],
	                                EDITOR_THEME_ANSI_BRIGHT_GREEN));
	ASSERT_TRUE(
	        theme_color_is_rgb(theme.ansi[EDITOR_THEME_ANSI_BRIGHT_MAGENTA], 0xAA, 0x00, 0xBB));
	/* Slots not mentioned remain default. */
	ASSERT_TRUE(editorThemeColorIsDefault(theme.ansi[EDITOR_THEME_ANSI_BLUE]));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(theme_path) == 0);
	ASSERT_TRUE(rmdir(themes_dir) == 0);
	ASSERT_TRUE(rmdir(dot_rotide) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_theme_resolve_ansi_falls_back_to_foreground(void) {
	struct editorTheme saved = E.theme;
	struct editorTheme theme;
	ASSERT_TRUE(editorThemeInitBuiltin(&theme, "kanagawa-wave"));
	theme.ansi[EDITOR_THEME_ANSI_RED] = editorThemeDefaultColor();
	E.theme = theme;

	struct editorThemeColor resolved = editorThemeResolveAnsi(EDITOR_THEME_ANSI_RED, 1);
	ASSERT_TRUE(theme_color_is_rgb(resolved, 0xDC, 0xD7, 0xBA)); /* fg of kanagawa-wave */

	resolved = editorThemeResolveAnsi(EDITOR_THEME_ANSI_BLUE, 1);
	ASSERT_TRUE(theme_color_is_rgb(resolved, 0x7E, 0x9C, 0xD8)); /* populated */

	resolved = editorThemeResolveAnsi(EDITOR_THEME_ANSI_RED, 0);
	ASSERT_TRUE(theme_color_is_rgb(resolved, 0x1F, 0x1F, 0x28)); /* bg fallback */

	E.theme = saved;
	return 0;
}

static int test_editor_config_default_global_loads_cleanly(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char config_path[512] = "";
	char root_template[] = "/tmp/rotide-test-default-loads-XXXXXX";

	if (!backup_env_var(&home_backup, "HOME")) {
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
	if (!path_join(config_path, sizeof(config_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}

	if (editorConfigEnsureGlobalConfig() != EDITOR_CONFIG_BOOTSTRAP_CREATED) {
		goto cleanup;
	}

	struct editorKeymap keymap;
	if (editorKeymapLoadFromPaths(&keymap, config_path, NULL) != EDITOR_KEYMAP_LOAD_OK) {
		goto cleanup;
	}

	struct editorTheme theme;
	if (editorThemeLoadFromPaths(&theme, config_path, NULL, home_dir) != EDITOR_THEME_LOAD_OK) {
		goto cleanup;
	}

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	if (editorCursorStyleLoadFromPaths(&style, config_path, NULL) !=
	    EDITOR_CURSOR_STYLE_LOAD_OK) {
		goto cleanup;
	}
	int line_wrap = 0;
	if (editorLineWrapLoadFromPaths(&line_wrap, config_path, NULL) !=
	    EDITOR_LINE_WRAP_LOAD_OK) {
		goto cleanup;
	}

	if (editorDapConfigLoadFromPaths(config_path, NULL) != EDITOR_DAP_CONFIG_LOAD_OK) {
		goto cleanup;
	}
	if (editorDapAdapterById("go") == NULL || editorDapAdapterById("c") == NULL ||
	    editorDapAdapterById("cpp") == NULL) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (config_path[0] != '\0') {
		(void)unlink(config_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	return failed;
}

const struct editorTestCase g_workspace_theme_config_tests[] = {
        {"editor_theme_load_builtin_global_project_precedence",
         test_editor_theme_load_builtin_global_project_precedence},
        {"editor_theme_load_ignores_non_theme_sections",
         test_editor_theme_load_ignores_non_theme_sections},
        {"editor_theme_loads_modus_builtins", test_editor_theme_loads_modus_builtins},
        {"editor_theme_loads_github_builtins", test_editor_theme_loads_github_builtins},
        {"editor_theme_loads_acme_builtin", test_editor_theme_loads_acme_builtin},
        {"editor_theme_loads_silentium_builtin", test_editor_theme_loads_silentium_builtin},
        {"editor_theme_loads_256noir_builtin", test_editor_theme_loads_256noir_builtin},
        {"editor_theme_loads_molokai_builtin", test_editor_theme_loads_molokai_builtin},
        {"editor_theme_loads_kanagawa_builtins", test_editor_theme_loads_kanagawa_builtins},
        {"editor_theme_project_config_cannot_override_theme_colors",
         test_editor_theme_project_config_cannot_override_theme_colors},
        {"editor_theme_loads_custom_theme_from_home_themes",
         test_editor_theme_loads_custom_theme_from_home_themes},
        {"editor_theme_invalid_values_fall_back_to_terminal",
         test_editor_theme_invalid_values_fall_back_to_terminal},
        {"editor_theme_builtin_kanagawa_wave_populates_ansi_palette",
         test_editor_theme_builtin_kanagawa_wave_populates_ansi_palette},
        {"editor_theme_terminal_builtin_leaves_ansi_default",
         test_editor_theme_terminal_builtin_leaves_ansi_default},
        {"editor_theme_loads_custom_ansi_table", test_editor_theme_loads_custom_ansi_table},
        {"editor_theme_resolve_ansi_falls_back_to_foreground",
         test_editor_theme_resolve_ansi_falls_back_to_foreground},
        {"editor_config_ensure_global_creates_default_when_missing",
         test_editor_config_ensure_global_creates_default_when_missing},
        {"editor_open_settings_opens_global_config_in_tab",
         test_editor_open_settings_opens_global_config_in_tab},
        {"editor_save_global_config_can_reload_settings",
         test_editor_save_global_config_can_reload_settings},
        {"editor_save_global_config_can_skip_reload",
         test_editor_save_global_config_can_skip_reload},
        {"editor_config_default_global_loads_cleanly",
         test_editor_config_default_global_loads_cleanly},
};

const int g_workspace_theme_config_test_count =
        (int)(sizeof(g_workspace_theme_config_tests) / sizeof(g_workspace_theme_config_tests[0]));
