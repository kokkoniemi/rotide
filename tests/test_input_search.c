#include "input/input_system.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <stddef.h>
#include <unistd.h>

static int test_editor_process_keypress_find_file_filters_previews_and_opens(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char alpha_file[512];
	char beta_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), src_dir, "beta.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char ctrl_p[] = {CTRL_KEY('p')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_p, sizeof(ctrl_p)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());

	char filter[] = {'b'};
	ASSERT_TRUE(editor_process_keypress_with_input(filter, sizeof(filter)) == 0);
	ASSERT_EQ_STR("b", editorFileSearchQuery());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);
	ASSERT_ROW_TEXT_EQ(0, "beta");

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_EQ_INT(0, editorActiveTabIsPreview());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);
	int src_idx = -1;
	int beta_idx = -1;
	struct editorDrawerEntryView beta_view;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("beta.txt", &beta_idx, &beta_view));
	ASSERT_TRUE(src_idx >= 0);
	ASSERT_TRUE(beta_idx > src_idx);
	ASSERT_EQ_INT(beta_idx, E.drawer_selected_index);
	ASSERT_EQ_STR(beta_file, beta_view.path);

	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_find_file_recovers_collapsed_drawer_on_open(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char beta_file[512];
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	char ctrl_p[] = {CTRL_KEY('p')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_p, sizeof(ctrl_p)) == 0);
	ASSERT_EQ_INT(0, E.drawer_collapsed);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);

	char filter[] = {'b'};
	ASSERT_TRUE(editor_process_keypress_with_input(filter, sizeof(filter)) == 0);

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(1, E.drawer_collapsed);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);

	ASSERT_TRUE(unlink(beta_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_project_search_filters_previews_and_opens(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "before\nneedle here\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char ctrl_alt_f[] = {'\x1b', CTRL_KEY('f')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_alt_f, sizeof(ctrl_alt_f)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	const char *query = "needle";
	for (size_t i = 0; query[i] != '\0'; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(&query[i], 1) == 0);
	}
	ASSERT_EQ_STR("needle", editorProjectSearchQuery());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(alpha_file, E.filename);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_INT(0, editorActiveTabIsPreview());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(alpha_file, E.filename);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_project_search_recovers_collapsed_drawer_on_open(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "before\nneedle here\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	char ctrl_alt_f[] = {'\x1b', CTRL_KEY('f')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_alt_f, sizeof(ctrl_alt_f)) == 0);
	ASSERT_EQ_INT(0, E.drawer_collapsed);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);

	const char *query = "needle";
	for (size_t i = 0; query[i] != '\0'; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(&query[i], 1) == 0);
	}

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(1, E.drawer_collapsed);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(alpha_file, E.filename);

	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_incremental_find_first_match(void) {
	add_row("zz alpha");
	add_row("alpha later");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("alpha", E.search_query);
	ASSERT_EQ_INT(0, assert_active_search_match(0, 3, 5));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_arrow_navigation_wraps(void) {
	add_row("alpha one");
	add_row("middle alpha");
	add_row("tail alpha");
	E.cy = 0;
	E.cx = 0;

	const char input[] = {CTRL_KEY('f'), 'a',    'l',    'p', 'h', 'a',    '\x1b',
	                      '[',           'B',    '\x1b', '[', 'B', '\x1b', '[',
	                      'B',           '\x1b', '[',    'A', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("alpha", E.search_query);
	ASSERT_EQ_INT(0, assert_active_search_match(2, 5, 5));
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match(void) {
	add_row("alpha row");
	add_row("other");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\x1b', '[', 'x'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.search_query == NULL);
	ASSERT_EQ_INT(0, E.search_match_len);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_enter_keeps_active_match(void) {
	add_row("xx alpha");
	add_row("alpha second");
	E.cy = 1;
	E.cx = 4;

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("alpha", E.search_query);
	ASSERT_EQ_INT(0, assert_active_search_match(0, 3, 5));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status(void) {
	add_row("hello world");
	add_row("second line");
	E.cy = 0;
	E.cx = 5;

	const char input[] = {CTRL_KEY('f'), 'z', 'z', 'z', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("zzz", E.search_query);
	ASSERT_EQ_INT(0, E.search_match_len);
	ASSERT_EQ_STR("No matches for \"zzz\"", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_find_file_filters_in_vim_normal_mode(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char beta_file[512];
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorInputSystemActivate("vim"));

	/* <leader>p (space, then p) opens find-file from Vim Normal mode. Vim mode
	 * captures control keys, so Ctrl-P no longer falls through to the action. */
	char leader_key[] = {' '};
	char find_key[] = {'p'};
	ASSERT_TRUE(editor_process_keypress_with_input(leader_key, sizeof(leader_key)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(find_key, sizeof(find_key)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);

	/* 'b' is a Vim motion in Normal mode, but here it must filter the field. */
	char filter[] = {'b'};
	ASSERT_TRUE(editor_process_keypress_with_input(filter, sizeof(filter)) == 0);
	ASSERT_EQ_STR("b", editorFileSearchQuery());

	ASSERT_TRUE(editorInputSystemActivate("cua"));
	ASSERT_TRUE(unlink(beta_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_project_search_filters_in_vim_normal_mode(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "before\nneedle here\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorInputSystemActivate("vim"));

	char ctrl_alt_f[] = {'\x1b', CTRL_KEY('f')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_alt_f, sizeof(ctrl_alt_f)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);

	const char *query = "needle";
	for (size_t i = 0; query[i] != '\0'; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(&query[i], 1) == 0);
	}
	ASSERT_EQ_STR("needle", editorProjectSearchQuery());

	ASSERT_TRUE(editorInputSystemActivate("cua"));
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

const struct editorTestCase g_input_search_tests[] = {
        {"editor_process_keypress_find_file_filters_in_vim_normal_mode",
         test_editor_process_keypress_find_file_filters_in_vim_normal_mode},
        {"editor_process_keypress_project_search_filters_in_vim_normal_mode",
         test_editor_process_keypress_project_search_filters_in_vim_normal_mode},
        {"editor_process_keypress_find_file_filters_previews_and_opens",
         test_editor_process_keypress_find_file_filters_previews_and_opens},
        {"editor_process_keypress_find_file_recovers_collapsed_drawer_on_open",
         test_editor_process_keypress_find_file_recovers_collapsed_drawer_on_open},
        {"editor_process_keypress_project_search_filters_previews_and_opens",
         test_editor_process_keypress_project_search_filters_previews_and_opens},
        {"editor_process_keypress_project_search_recovers_collapsed_drawer_on_open",
         test_editor_process_keypress_project_search_recovers_collapsed_drawer_on_open},
        {"editor_process_keypress_ctrl_f_incremental_find_first_match",
         test_editor_process_keypress_ctrl_f_incremental_find_first_match},
        {"editor_process_keypress_ctrl_f_arrow_navigation_wraps",
         test_editor_process_keypress_ctrl_f_arrow_navigation_wraps},
        {"editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match",
         test_editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match},
        {"editor_process_keypress_ctrl_f_enter_keeps_active_match",
         test_editor_process_keypress_ctrl_f_enter_keeps_active_match},
        {"editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status",
         test_editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status},
};

const int g_input_search_test_count =
        (int)(sizeof(g_input_search_tests) / sizeof(g_input_search_tests[0]));
