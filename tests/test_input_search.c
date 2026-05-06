#include "test_case.h"
#include "test_support.h"
#include "editing/selection.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

static int test_editor_task_log_document_stays_authoritative(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Echo", "printf 'alpha\\nbeta\\n'", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_TRUE(E.document != NULL);

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	return 0;
}

static int test_editor_task_log_streams_output_while_inactive(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Background",
			"printf 'alpha\\n'; sleep 0.1; printf 'beta\\n'", NULL, NULL));
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Background", editorActiveBufferDisplayName());
	ASSERT_TRUE(E.document != NULL);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "alpha") != NULL);
	ASSERT_TRUE(strstr(text, "beta") != NULL);
	free(text);
	return 0;
}

static int test_editor_task_runner_merges_stderr_and_close_requires_confirmation(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Mixed",
			"printf 'out\\n'; printf 'err\\n' 1>&2; sleep 1", NULL, NULL));
	ASSERT_TRUE(editorTaskIsRunning());

	char close_once[] = {CTRL_KEY('w')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(close_once, sizeof(close_once)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "Task is still running") != NULL);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_EQ_INT(2, editorTabCount());

	ASSERT_TRUE(editor_process_keypress_with_input_silent(close_once, sizeof(close_once)) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(editorTaskStart("Task: Mixed Output",
			"printf 'out\\n'; printf 'err\\n' 1>&2", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "out") != NULL);
	ASSERT_TRUE(strstr(text, "err") != NULL);
	free(text);
	return 0;
}

static int test_editor_task_runner_truncates_large_output(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Large Output",
			"yes 1234567890 | head -c 150000", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(3000));

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(textlen <= ROTIDE_TASK_LOG_MAX_BYTES + 256);
	ASSERT_TRUE(strstr(text, "[output truncated]") != NULL);
	free(text);
	return 0;
}

static int test_editor_process_keypress_keymap_remap_changes_dispatch(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-dispatch-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"
				"redraw = \"ctrl+s\"\n"));

	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&E.keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	char save_path[] = "/tmp/rotide-test-keymap-dispatch-save-XXXXXX";
	int fd = mkstemp(save_path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("line1");
	E.filename = strdup(save_path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	char ctrl_s[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_s, sizeof(ctrl_s)) == 0);
	ASSERT_EQ_INT(1, E.dirty);

	size_t first_read_len = 0;
	char *first_contents = read_file_contents(save_path, &first_read_len);
	ASSERT_TRUE(first_contents != NULL);
	ASSERT_EQ_INT(0, first_read_len);
	free(first_contents);

	char ctrl_a[] = {CTRL_KEY('a')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_a, sizeof(ctrl_a)) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	size_t second_read_len = 0;
	char *second_contents = read_file_contents(save_path, &second_read_len);
	ASSERT_TRUE(second_contents != NULL);
	ASSERT_MEM_EQ("line1\n", second_contents, second_read_len);
	free(second_contents);

	ASSERT_TRUE(unlink(save_path) == 0);
	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-ctrl-alt-dispatch-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"new_tab = \"ctrl+alt+a\"\n"));

	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&E.keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);
	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(1, editorTabCount());

	char input[] = {'\x1b', CTRL_KEY('a')};
	ASSERT_TRUE(editor_process_keypress_with_input(input, sizeof(input)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_resize_drawer_shortcuts(void) {
	E.window_cols = 40;
	E.drawer_width_cols = 10;
	E.pane_focus = EDITOR_PANE_DRAWER;

	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
				sizeof(alt_shift_right) - 1) == 0);
	ASSERT_EQ_INT(11, editorDrawerWidthForCols(E.window_cols));

	const char alt_shift_left[] = "\x1b[1;4D";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_left,
				sizeof(alt_shift_left) - 1) == 0);
	ASSERT_EQ_INT(10, editorDrawerWidthForCols(E.window_cols));

	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_left,
				sizeof(alt_shift_left) - 1) == 0);
	ASSERT_EQ_INT(9, editorDrawerWidthForCols(E.window_cols));

	E.drawer_width_cols = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_left,
				sizeof(alt_shift_left) - 1) == 0);
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(E.window_cols));
	return 0;
}

static int test_editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char toggle_drawer[] = {'\x1b', CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_EQ_STR("Drawer collapsed", E.statusmsg);

	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	ASSERT_TRUE(editorDrawerSetCollapsed(1));
	E.pane_focus = EDITOR_PANE_TEXT;
	char focus_drawer[] = {CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_toggle_drawer_preserves_search_modes(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	add_row("body");

	char find_file[] = {CTRL_KEY('p')};
	ASSERT_TRUE(editor_process_keypress_with_input(find_file, sizeof(find_file)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	char file_query[] = {'a'};
	ASSERT_TRUE(editor_process_keypress_with_input(file_query, sizeof(file_query)) == 0);
	ASSERT_EQ_STR("a", editorFileSearchQuery());

	char toggle_drawer[] = {'\x1b', CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	char hidden_file_query_input[] = {'b'};
	ASSERT_TRUE(editor_process_keypress_with_input(hidden_file_query_input,
				sizeof(hidden_file_query_input)) == 0);
	ASSERT_EQ_STR("a", editorFileSearchQuery());

	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("a", editorFileSearchQuery());
	editorFileSearchExit(1);

	char project_search[] = {'\x1b', CTRL_KEY('f')};
	ASSERT_TRUE(editor_process_keypress_with_input(project_search, sizeof(project_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	char project_query[] = {'x'};
	ASSERT_TRUE(editor_process_keypress_with_input(project_query, sizeof(project_query)) == 0);
	ASSERT_EQ_STR("x", editorProjectSearchQuery());

	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	char hidden_project_query_input[] = {'y'};
	ASSERT_TRUE(editor_process_keypress_with_input(hidden_project_query_input,
				sizeof(hidden_project_query_input)) == 0);
	ASSERT_EQ_STR("x", editorProjectSearchQuery());

	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("x", editorProjectSearchQuery());
	editorProjectSearchExit(1);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_main_menu_runs_selected_action(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char match_file[512];
	ASSERT_TRUE(path_join(match_file, sizeof(match_file), env.project_dir, "match.txt"));
	ASSERT_TRUE(write_text_file(match_file, "match\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	const char alt_m[] = "\x1bm";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_m, sizeof(alt_m) - 1) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_EQ_INT(-1, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Main menu opened", E.statusmsg);

	int find_file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("Find File", &find_file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(find_file_idx, E.window_rows));

	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter, sizeof(enter)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	editorFileSearchExit(1);
	ASSERT_TRUE(unlink(match_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_tabs_switch_restores_per_tab_state(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("tab-zero");
	E.cx = 4;
	E.cy = 0;
	E.search_query = strdup("zero");
	ASSERT_TRUE(E.search_query != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	add_row("tab-one");
	E.cx = 2;
	E.cy = 0;
	free(E.search_query);
	E.search_query = strdup("one");
	ASSERT_TRUE(E.search_query != NULL);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("tab-zero", E.rows[0].chars);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("zero", E.search_query);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("tab-one", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("one", E.search_query);
	return 0;
}

static int test_editor_tab_close_last_tab_keeps_one_empty_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("x");
	E.dirty = 1;
	E.filename = strdup("dirty.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabCloseActive());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(E.filename == NULL);
	return 0;
}

static int test_editor_process_keypress_ctrl_w_dirty_requires_second_press(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	char ctrl_w[] = {CTRL_KEY('w')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);

	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_editor_process_keypress_close_tab_confirmation_resets_on_other_action(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	char ctrl_w[] = {CTRL_KEY('w')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, E.numrows);

	const char move_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(move_right, sizeof(move_right) - 1) == 0);

	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, E.numrows);

	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_q_checks_dirty_tabs_globally(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty-first-tab");
	E.dirty = 1;
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.dirty);

	char ctrl_q[] = {CTRL_KEY('q')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);
	return 0;
}

static int test_editor_process_keypress_tab_actions_new_next_prev(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("left");

	char ctrl_n[] = {CTRL_KEY('n')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_n, sizeof(ctrl_n)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	add_row("right");
	const char alt_left[] = "\x1b[1;3D";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_left, sizeof(alt_left) - 1) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("left", E.rows[0].chars);

	const char alt_right_fallback[] = "\x1b\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_right_fallback,
				sizeof(alt_right_fallback) - 1) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("right", E.rows[0].chars);
	return 0;
}

static int test_editor_tab_open_file_reuses_active_clean_empty_buffer(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(E.filename == NULL);

	ASSERT_TRUE(editorTabOpenFileAsNew(open_file));
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	// Leave tab 0 as a clean empty buffer, then make tab 1 active and non-empty.
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	add_row("keep");

	ASSERT_TRUE(editorTabOpenFileAsNew(open_file));
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_process_keypress_focus_drawer_and_arrow_navigation(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	add_row("line");
	E.cy = 0;
	E.cx = 2;
	int initial_cy = E.cy;
	int initial_cx = E.cx;

	char focus_drawer[] = {CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_TRUE(E.drawer_selected_index > 0);
	ASSERT_EQ_INT(initial_cy, E.cy);
	ASSERT_EQ_INT(initial_cx, E.cx);

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	int collapsed_count = editorDrawerVisibleCount();

	const char arrow_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_right, sizeof(arrow_right) - 1) == 0);
	ASSERT_TRUE(editorDrawerVisibleCount() > collapsed_count);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	const char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());

	const char esc_input[] = "\x1b[x";
	ASSERT_TRUE(editor_process_keypress_with_input_silent(esc_input, sizeof(esc_input) - 1) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_drawer_enter_toggles_directory(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));
	int collapsed_count = editorDrawerVisibleCount();

	E.pane_focus = EDITOR_PANE_DRAWER;
	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_TRUE(editorDrawerVisibleCount() > collapsed_count);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_drawer_enter_opens_file_in_new_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "open.txt"));
	ASSERT_TRUE(write_text_file(open_file, "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("open.txt", &file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));
	E.pane_focus = EDITOR_PANE_DRAWER;

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

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
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());

	char filter[] = {'b'};
	ASSERT_TRUE(editor_process_keypress_with_input(filter, sizeof(filter)) == 0);
	ASSERT_EQ_STR("b", editorFileSearchQuery());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);
	ASSERT_EQ_STR("beta", E.rows[0].chars);

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
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
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

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
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_INT(0, editorActiveTabIsPreview());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(alpha_file, E.filename);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_insert_move_and_backspace(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;

	char backspace[] = {BACKSPACE};
	ASSERT_TRUE(editor_process_keypress_with_input(backspace, sizeof(backspace)) == 0);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);

	char insert_z[] = {'Z'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_z, sizeof(insert_z)) == 0);
	ASSERT_EQ_STR("aZ", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(1, E.cx);

	char home_key[] = "\x1b[H";
	ASSERT_TRUE(editor_process_keypress_with_input(home_key, sizeof(home_key) - 1) == 0);
	ASSERT_EQ_INT(0, E.cx);

	char end_key[] = "\x1b[F";
	ASSERT_TRUE(editor_process_keypress_with_input(end_key, sizeof(end_key) - 1) == 0);
	ASSERT_EQ_INT((int)strlen(E.rows[0].chars), E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_j_does_not_insert_newline(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('j')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_tab_inserts_literal_tab(void) {
	add_row("");
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editor_process_single_key('\t') == 0);
	ASSERT_EQ_INT(1, E.rows[0].size);
	ASSERT_EQ_STR("\t", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_utf8_bytes_insert_verbatim(void) {
	static const unsigned char input[] = {0xC3, 0xB6, 0xF0, 0x9F, 0x99, 0x82};
	static const unsigned char expected[] = {0xC3, 0xB6, 0xF0, 0x9F, 0x99, 0x82, '\0'};

	add_row("");
	E.cy = 0;
	E.cx = 0;

	for (size_t i = 0; i < sizeof(input); i++) {
		ASSERT_TRUE(editor_process_single_key((char)input[i]) == 0);
	}

	ASSERT_EQ_INT((int)sizeof(input), E.rows[0].size);
	ASSERT_MEM_EQ(expected, E.rows[0].chars, sizeof(expected));
	ASSERT_EQ_INT((int)sizeof(input), E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_delete_key(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;

	char del_key[] = "\x1b[3~";
	ASSERT_TRUE(editor_process_keypress_with_input(del_key, sizeof(del_key) - 1) == 0);
	ASSERT_EQ_STR("acd", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_arrow_down_keeps_visual_column(void) {
	add_row("a\t\tb");
	add_row("0123456789ABCDEFGHI");
	E.cy = 0;
	E.cx = 3;

	char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(16, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_s_saves_file(void) {
	char path[] = "/tmp/rotide-test-ctrls-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("line1");
	add_row("line2");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 7;

	char ctrl_s[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_s, sizeof(ctrl_s)) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_MEM_EQ("line1\nline2\n", contents, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_process_keypress_resize_event_updates_window_size(void) {
	char response[] = "\x1b[9;33R";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 8;
	E.window_cols = 40;
	E.undo_history.len = 0;
	E.redo_history.len = 0;

	editorQueueResizeEvent();
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);
	editorProcessKeypress();
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(6, E.window_rows);
	ASSERT_EQ_INT(33, E.window_cols);
	ASSERT_EQ_INT(0, E.undo_history.len);
	ASSERT_EQ_INT(0, E.redo_history.len);
	free(stdout_bytes);
	return 0;
}

static int test_editor_process_keypress_alt_z_toggles_line_wrap_without_dirty(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 10;
	E.line_wrap_enabled = 0;
	E.dirty = 7;
	E.coloff = 4;
	E.wrapoff = 2;

	const char alt_z[] = "\x1bz";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_z, sizeof(alt_z) - 1) == 0);
	ASSERT_EQ_INT(1, E.line_wrap_enabled);
	ASSERT_EQ_INT(0, E.coloff);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_EQ_STR("Line wrap enabled", E.statusmsg);

	ASSERT_TRUE(editor_process_keypress_with_input(alt_z, sizeof(alt_z) - 1) == 0);
	ASSERT_EQ_INT(0, E.line_wrap_enabled);
	ASSERT_EQ_INT(0, E.wrapoff);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_EQ_STR("Line wrap disabled", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_alt_n_toggles_line_numbers_without_dirty(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 20;
	E.line_numbers_enabled = 1;
	E.dirty = 7;

	const char alt_n[] = "\x1bn";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_n, sizeof(alt_n) - 1) == 0);
	ASSERT_EQ_INT(0, E.line_numbers_enabled);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_EQ_STR("Line numbers disabled", E.statusmsg);

	ASSERT_TRUE(editor_process_keypress_with_input(alt_n, sizeof(alt_n) - 1) == 0);
	ASSERT_EQ_INT(1, E.line_numbers_enabled);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_EQ_STR("Line numbers enabled", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_alt_h_toggles_current_line_highlight_without_dirty(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 20;
	E.current_line_highlight_enabled = 1;
	E.dirty = 7;

	const char alt_h[] = "\x1bh";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_h, sizeof(alt_h) - 1) == 0);
	ASSERT_EQ_INT(0, E.current_line_highlight_enabled);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_EQ_STR("Current-line highlight disabled", E.statusmsg);

	ASSERT_TRUE(editor_process_keypress_with_input(alt_h, sizeof(alt_h) - 1) == 0);
	ASSERT_EQ_INT(1, E.current_line_highlight_enabled);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_EQ_STR("Current-line highlight enabled", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_places_cursor_with_offsets(void) {
	add_row("0123456789");
	add_row("abcdefghij");
	add_row("klmnopqrst");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 1;
	E.coloff = 2;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_click_maps_same_column_with_line_numbers(void) {
	add_row("0123456789");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	char click[32];
	E.line_numbers_enabled = 0;
	int text_start = editorTextBodyStartColForCols(E.window_cols);
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(4, E.cx);

	E.cy = 0;
	E.cx = 0;
	E.line_numbers_enabled = 1;
	text_start = editorTextBodyStartColForCols(E.window_cols);
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(4, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_places_cursor_on_wrapped_segment(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 4, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection(void) {
	add_row("abcdef");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_ignores_non_text_rows(void) {
	add_row("abc");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 1;

	const char click_status_bar[] = "\x1b[<0;2;6M";
	ASSERT_TRUE(editor_process_keypress_with_input(click_status_bar,
				sizeof(click_status_bar) - 1) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click_filler_row[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_filler_row, sizeof(click_filler_row), 0,
				text_start + 2, 4, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_filler_row, strlen(click_filler_row)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns(void) {
	add_row("0123456789");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 3;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	char click_left_padding[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_left_padding, sizeof(click_left_padding), 0,
				text_start + 1, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_left_padding,
				strlen(click_left_padding)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char click_right_padding[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_right_padding, sizeof(click_right_padding), 0,
				text_start + text_cols, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_right_padding,
				strlen(click_right_padding)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	int row = src_idx - E.drawer_rowoff + 2;
	char click_src[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_src, sizeof(click_src), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_src, strlen(click_src)) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(editor_process_keypress_with_input(click_src, strlen(click_src)) == 0);
	ASSERT_EQ_INT(0, find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_click_expands_collapsed_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char click_collapse[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_collapse, sizeof(click_collapse), 0, 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_collapse, strlen(click_collapse)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_EQ_STR("Drawer collapsed", E.statusmsg);

	char click_toggle[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_toggle, sizeof(click_toggle), 0, 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_toggle, strlen(click_toggle)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_header_mode_buttons(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	E.window_rows = 6;
	E.window_cols = 60;

	char click_file_search[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file_search, sizeof(click_file_search), 0, 7, 1,
				'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file_search,
				strlen(click_file_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	char query_char[] = {'a'};
	ASSERT_TRUE(editor_process_keypress_with_input(query_char, sizeof(query_char)) == 0);
	ASSERT_EQ_STR("a", editorFileSearchQuery());
	ASSERT_TRUE(editor_process_keypress_with_input(click_file_search,
				strlen(click_file_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_STR("a", editorFileSearchQuery());

	char click_project_search[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_project_search, sizeof(click_project_search), 0, 10,
				1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_project_search,
				strlen(click_project_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	char click_main_menu[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_main_menu, sizeof(click_main_menu), 0, 13, 1,
				'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_main_menu,
				strlen(click_main_menu)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_EQ_INT(-1, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	char click_explorer[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_explorer, sizeof(click_explorer), 0, 4, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_explorer, strlen(click_explorer)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(-1, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_collapsed_drawer_body_click_edits_text_pane(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 4;
	E.window_cols = 20;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 2;
	E.pane_focus = EDITOR_PANE_TEXT;
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char click_body[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_body, sizeof(click_body), 0, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_body, strlen(click_body)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "single.txt"));
	ASSERT_TRUE(write_text_file(open_file, "single\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("single.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	ASSERT_TRUE(row >= 2);
	char click_file[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file, sizeof(click_file), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(file_idx, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR("Preview tab opened. Double-click to keep it open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_STR("single", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_open_selected_file_in_preview_reuses_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char first_file[512];
	char second_file[512];
	ASSERT_TRUE(path_join(first_file, sizeof(first_file), env.project_dir, "first.txt"));
	ASSERT_TRUE(path_join(second_file, sizeof(second_file), env.project_dir, "second.txt"));
	ASSERT_TRUE(write_text_file(first_file, "first\n"));
	ASSERT_TRUE(write_text_file(second_file, "second\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int first_idx = -1;
	int second_idx = -1;
	ASSERT_TRUE(find_drawer_entry("first.txt", &first_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("second.txt", &second_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(first_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInPreviewTab());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR(first_file, E.filename);

	ASSERT_TRUE(editorDrawerSelectVisibleIndex(second_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInPreviewTab());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR(second_file, E.filename);
	ASSERT_EQ_STR("second", E.rows[0].chars);

	ASSERT_TRUE(unlink(first_file) == 0);
	ASSERT_TRUE(unlink(second_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "double.txt"));
	ASSERT_TRUE(write_text_file(open_file, "double\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("orig");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("double.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	ASSERT_TRUE(row >= 2);
	char click_file[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file, sizeof(click_file), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR("Preview tab opened. Double-click to keep it open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);

	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_TRUE(!editorActiveTabIsPreview());
	ASSERT_EQ_STR("Tab kept open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("double", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_top_row_click_switches_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("zero");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("one");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("two");
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_EQ_INT(2, editorTabActiveIndex());

	E.window_cols = 80;
	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	char click_first_tab[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_first_tab, sizeof(click_first_tab), 0,
				text_start + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_first_tab, strlen(click_first_tab)) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("zero", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	return 0;
}

static int test_editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/aaaaaaaaaaabbbbbbbbbbbccccccccccc");
	ASSERT_TRUE(E.filename != NULL);
	add_row("zero");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/one.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("one");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/two.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("two");

	E.window_cols = 90;
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(text_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count >= 2);

	int second_tab_col = -1;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 1) {
			second_tab_col = layout[i].start_col + 1;
			break;
		}
	}
	ASSERT_TRUE(second_tab_col >= 0);

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	char click_second_tab[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_second_tab, sizeof(click_second_tab), 0,
				text_start + second_tab_col + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_second_tab, strlen(click_second_tab)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR("one", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_on_splitter_resizes_drawer(void) {
	add_row("abcdef");
	E.window_rows = 4;
	E.window_cols = 40;
	E.drawer_width_cols = 12;
	E.cy = 0;
	E.cx = 3;
	E.pane_focus = EDITOR_PANE_TEXT;

	int separator_x = editorDrawerWidthForCols(E.window_cols) + 1;
	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, separator_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.drawer_resize_active);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char drag_smaller[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag_smaller, sizeof(drag_smaller), 32, 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag_smaller, strlen(drag_smaller)) == 0);
	ASSERT_EQ_INT(5, editorDrawerWidthForCols(E.window_cols));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char drag_larger[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag_larger, sizeof(drag_larger), 32, 200, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag_larger, strlen(drag_larger)) == 0);
	ASSERT_EQ_INT(38, editorDrawerWidthForCols(E.window_cols));

	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, 200, 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.drawer_resize_active);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 20;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(3, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	E.rowoff = 8;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(4, E.cy);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 2, 'M'));
	E.rowoff = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(4, E.cy);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_wrapped_segments(void) {
	add_row("abcdefghijklmnopqrst");
	E.window_rows = 2;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(3, E.wrapoff);
	ASSERT_EQ_INT(0, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(0, E.wrapoff);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps(void) {
	add_row("abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 5;
	E.window_cols = 12;
	E.cy = 0;
	E.cx = 5;
	E.coloff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_right[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_right, sizeof(wheel_right), 67, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_right, strlen(wheel_right)) == 0);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(3, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	E.coloff = 24;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_right, strlen(wheel_right)) == 0);
	ASSERT_EQ_INT(25, E.coloff);

	char wheel_left[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_left, sizeof(wheel_left), 66, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_left, strlen(wheel_left)) == 0);
	ASSERT_EQ_INT(22, E.coloff);

	E.coloff = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_left, strlen(wheel_left)) == 0);
	ASSERT_EQ_INT(0, E.coloff);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(write_text_file(path, "x\n"));
	}

	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 30;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 2;
	E.drawer_rowoff = 0;

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.pane_focus = EDITOR_PANE_TEXT;

	const char wheel_down[] = "\x1b[<65;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.drawer_rowoff);
	ASSERT_EQ_INT(2, E.rowoff);

	const char wheel_up[] = "\x1b[<64;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, sizeof(wheel_up) - 1) == 0);
	ASSERT_EQ_INT(0, E.drawer_rowoff);
	ASSERT_EQ_INT(2, E.rowoff);

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(unlink(path) == 0);
	}
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(write_text_file(path, "x\n"));
	}

	E.window_rows = 4;
	E.window_cols = 30;
	E.drawer_rowoff = 0;

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	const char wheel_down[] = "\x1b[<65;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.drawer_rowoff);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(3, E.drawer_rowoff);

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(unlink(path) == 0);
	}
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 30;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 0;
	E.drawer_rowoff = 2;
	E.pane_focus = EDITOR_PANE_DRAWER;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(3, E.rowoff);
	ASSERT_EQ_INT(2, E.drawer_rowoff);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(2, E.drawer_rowoff);
	return 0;
}

static int test_editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor(void) {
	for (int i = 0; i < 20; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 20;
	E.cy = 10;
	E.cx = 2;
	E.rowoff = 4;

	const char page_down[] = "\x1b[6~";
	ASSERT_TRUE(editor_process_keypress_with_input(page_down, sizeof(page_down) - 1) == 0);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char page_up[] = "\x1b[5~";
	ASSERT_TRUE(editor_process_keypress_with_input(page_up, sizeof(page_up) - 1) == 0);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(4, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	return 0;
}

static int test_editor_process_keypress_ctrl_arrow_scrolls_horizontally_without_moving_cursor(void) {
	add_row("abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 5;
	E.window_cols = 12;
	E.cy = 0;
	E.cx = 7;
	E.coloff = 0;

	const char ctrl_right[] = "\x1b[1;5C";
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_right, sizeof(ctrl_right) - 1) == 0);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(3, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char ctrl_left[] = "\x1b[1;5D";
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_left, sizeof(ctrl_left) - 1) == 0);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(0, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	return 0;
}

static int test_editor_process_keypress_free_scroll_can_leave_cursor_offscreen(void) {
	for (int i = 0; i < 12; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	ASSERT_TRUE(E.cy < E.rowoff);
	return 0;
}

static int test_editor_process_keypress_cursor_move_resyncs_follow_scroll(void) {
	for (int i = 0; i < 12; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_TRUE(E.cy < E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FOLLOW_CURSOR, E.viewport_mode);
	ASSERT_TRUE(E.cy >= E.rowoff);
	ASSERT_TRUE(E.cy < E.rowoff + E.window_rows);
	return 0;
}

static int test_editor_process_keypress_edit_resyncs_follow_scroll(void) {
	for (int i = 0; i < 12; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_TRUE(E.cy < E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char insert_char[] = {'x'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_char, sizeof(insert_char)) == 0);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FOLLOW_CURSOR, E.viewport_mode);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(E.rows[0].chars[0] == 'x');
	return 0;
}

static int test_editor_process_keypress_mouse_click_clears_existing_selection(void) {
	add_row("abcdef");
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.selection_anchor_offset);
	ASSERT_EQ_INT(4, E.cx);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 1));

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(5, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_press_without_drag_keeps_click_behavior(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 4, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, text_start + 4, 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(1, E.mouse_left_button_down);

	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_resets_existing_selection_anchor(void) {
	add_row("abcdef");
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 4;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 6, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 3, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 5));

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(2, range.start_cx);
	ASSERT_EQ_INT(5, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_clamps_to_viewport_without_autoscroll(void) {
	for (int i = 0; i < 6; i++) {
		add_row("0123456789");
	}
	E.window_rows = 3;
	E.window_cols = 10;
	E.line_numbers_enabled = 0;
	E.rowoff = 2;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 3, 3, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 50, 9, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(3, 3));
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(2, E.rowoff);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_honors_rowoff_and_coloff(void) {
	add_row("0123456789");
	add_row("abcdefghij");
	add_row("klmnopqrst");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 1;
	E.coloff = 2;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 4, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(1, E.selection_mode_active);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(1, range.start_cy);
	ASSERT_EQ_INT(3, range.start_cx);
	ASSERT_EQ_INT(2, range.end_cy);
	ASSERT_EQ_INT(5, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_release_stops_drag_session(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	char release[32];
	char drag_after_release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 5, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, text_start + 5, 2, 'm'));
	ASSERT_TRUE(format_sgr_mouse_event(drag_after_release, sizeof(drag_after_release), 32,
				text_start + 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 1));
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_TRUE(editor_process_keypress_with_input(drag_after_release, strlen(drag_after_release)) == 0);
	ASSERT_EQ_INT(4, E.cx);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(4, range.end_cx);
	return 0;
}

static int test_editor_prompt_ignores_mouse_events(void) {
	add_row("abcdef");
	E.cy = 0;
	E.cx = 2;

	const char input[] = "\x1b[<0;6;1M\x1b[<32;6;1M\x1b[<0;6;1m\x1b[x";
	char *result = editor_prompt_with_input(input, sizeof(input) - 1, "Prompt: %s");
	ASSERT_TRUE(result == NULL);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_editor_prompt_ignores_resize_events(void) {
	const char input[] = "\x1b[8;20Rok\r";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 4;
	E.window_cols = 10;
	editorQueueResizeEvent();

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(input, sizeof(input) - 1, &saved_stdin) == 0);
	char *result = editorPrompt("Prompt: %s");
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_TRUE(result != NULL);
	ASSERT_EQ_STR("ok", result);
	ASSERT_EQ_INT(5, E.window_rows);
	ASSERT_EQ_INT(20, E.window_cols);

	free(result);
	free(stdout_bytes);
	return 0;
}

static int test_editor_process_keypress_ctrl_b_toggles_selection_mode(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 2;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 2));

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_selection_range_tracks_cursor_movement(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);

	char arrow_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_right, sizeof(arrow_right) - 1) == 0);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(2, range.end_cx);

	ASSERT_TRUE(editor_process_keypress_with_input(arrow_right, sizeof(arrow_right) - 1) == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(3, range.end_cx);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(2, range.end_cx);
	return 0;
}

static int test_editor_extract_range_text_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	char *text = (char *)1;
	size_t len = 123;
	int extracted = editorExtractRangeText(&range, &text, &len);
	ASSERT_EQ_INT(1, extracted);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT(4, len);
	ASSERT_MEM_EQ("abc\n", text, len);
	free(text);
	return 0;
}

static int test_editor_delete_range_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	int deleted = editorDeleteRange(&range);
	ASSERT_EQ_INT(1, deleted);
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	size_t len = 123;
	char *text = editorRowsToStr(&len);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT(0, len);
	free(text);
	return 0;
}

static int test_editor_process_keypress_ctrl_c_copies_single_line_selection(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 5;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(5, clip_len);
	ASSERT_MEM_EQ("hello", clip, (size_t)clip_len);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_ctrl_c_copies_multiline_selection(void) {
	add_row("abc");
	add_row("def");
	add_row("ghi");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cy = 2;
	E.cx = 2;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(9, clip_len);
	ASSERT_MEM_EQ("bc\ndef\ngh", clip, (size_t)clip_len);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard(void) {
	add_row("hello");
	add_row("world");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cy = 1;
	E.cx = 3;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('x')) == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("held", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.dirty > dirty_before);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(7, clip_len);
	ASSERT_MEM_EQ("llo\nwor", clip, (size_t)clip_len);
	return 0;
}

static int test_editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard(void) {
	ASSERT_TRUE(editorClipboardSet("keep", 4));

	add_row("hello");
	add_row("world");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cy = 1;
	E.cx = 3;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('d')) == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("held", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(4, clip_len);
	ASSERT_MEM_EQ("keep", clip, (size_t)clip_len);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_pastes_clipboard_text(void) {
	ASSERT_TRUE(editorClipboardSet("XYZ", 3));
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("aXYZb", E.rows[0].chars);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(E.dirty > dirty_before);
	ASSERT_EQ_STR("Pasted 3 bytes", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text(void) {
	ASSERT_TRUE(editorClipboardSet("A\nB", 3));
	add_row("xy");
	E.cy = 0;
	E.cx = 1;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("xA", E.rows[0].chars);
	ASSERT_EQ_STR("By", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_STR("Pasted 3 bytes", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_empty_clipboard_is_noop(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 2;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_STR("Clipboard is empty", E.statusmsg);
	return 0;
}

static int test_editor_clipboard_sync_osc52_plain_sequence(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hello", 5));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b]52;c;aGVsbG8=\a") != NULL);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_tmux_wrapped_sequence(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(setenv("TMUX", "tmux-session", 1) == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hi", 2));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1bPtmux;\x1b\x1b]52;c;aGk=\a\x1b\\") != NULL);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_screen_wrapped_sequence(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(setenv("STY", "screen-session", 1) == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hi", 2));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1bP\x1b]52;c;aGk=\a\x1b\\") != NULL);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_mode_off_emits_nothing(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "off", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hello", 5));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_auto_mode_skips_non_tty(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "auto", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hello", 5));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_payload_cap_skips_external_write(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	size_t payload_len = ROTIDE_OSC52_MAX_COPY_BYTES + 1;
	char *payload = malloc(payload_len);
	ASSERT_TRUE(payload != NULL);
	memset(payload, 'a', payload_len);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet(payload, payload_len));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(payload_len, clip_len);
	ASSERT_MEM_EQ(payload, clip, payload_len);

	free(output);
	free(payload);
	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_process_keypress_ctrl_v_clears_selection_mode(void) {
	ASSERT_TRUE(editorClipboardSet("Z", 1));
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_STR("aZb", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_undo_roundtrip_single_step(void) {
	ASSERT_TRUE(editorClipboardSet("XY", 2));
	add_row("ab");
	E.cy = 0;
	E.cx = 1;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("aXYb", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("aXYb", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_selection_ops_noop_without_selection(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);
	ASSERT_EQ_STR("No selection", E.statusmsg);
	ASSERT_EQ_STR("abc", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('x')) == 0);
	ASSERT_EQ_STR("No selection", E.statusmsg);
	ASSERT_EQ_STR("abc", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('d')) == 0);
	ASSERT_EQ_STR("No selection", E.statusmsg);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_escape_clears_selection_mode(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 3;
	ASSERT_EQ_INT(1, E.selection_mode_active);

	const char esc_input[] = "\x1b[x";
	ASSERT_TRUE(editor_process_keypress_with_input_silent(esc_input, sizeof(esc_input) - 1) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_edit_ops_clear_selection_mode(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_TRUE(editor_process_single_key('Z') == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_STR("aZb", E.rows[0].chars);

	reset_editor_state();
	add_row("ab");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_STR("a", E.rows[0].chars);

	reset_editor_state();
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_TRUE(editor_process_single_key('\r') == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_STR("b", E.rows[1].chars);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut(void) {
	add_row("abcde");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 3;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('x')) == 0);
	ASSERT_EQ_STR("ade", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("abcde", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("ade", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_refresh_screen_highlights_active_selection_spans(void) {
	add_row("prefix alpha suffix");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 12;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 7));
	ASSERT_TRUE(set_active_search_match(0, 0, 6));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7malpha\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mprefix\x1b[m") == NULL);
	free(output);

	reset_editor_state();
	add_row("abc");
	add_row("def");
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 1;
	E.cx = 2;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 1));

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "a\x1b[7mbc\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mde\x1b[mf") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_ctrl_c_oom_preserves_buffer(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 5;

	editorTestAllocFailAfter(0);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	editorTestAllocReset();
	return 0;
}

static int test_editor_process_keypress_ctrl_g_jumps_to_line_and_sets_col_zero(void) {
	add_row("one");
	add_row("two");
	add_row("three");
	E.cy = 2;
	E.cx = 4;

	const char input[] = {CTRL_KEY('g'), '2', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_clamps_to_last_line(void) {
	add_row("first");
	add_row("last");
	E.cy = 0;
	E.cx = 2;

	const char input[] = {CTRL_KEY('g'), '9', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_rejects_invalid_input(void) {
	add_row("alpha");
	add_row("beta");
	E.cy = 1;
	E.cx = 2;

	const char letters[] = {CTRL_KEY('g'), 'a', 'b', 'c', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(letters, sizeof(letters)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	const char zero[] = {CTRL_KEY('g'), '0', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(zero, sizeof(zero)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	char overflow[66];
	overflow[0] = CTRL_KEY('g');
	for (size_t i = 1; i < sizeof(overflow) - 1; i++) {
		overflow[i] = '9';
	}
	overflow[sizeof(overflow) - 1] = '\r';
	ASSERT_TRUE(editor_process_keypress_with_input_silent(overflow, sizeof(overflow)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	return 0;
}

static int test_editor_process_keypress_ctrl_g_escape_cancels(void) {
	add_row("alpha");
	add_row("beta");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {CTRL_KEY('g'), '1', '2', '\x1b', '[', 'x'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_empty_buffer_sets_status(void) {
	E.cy = 0;
	E.cx = 0;

	const char input[] = {CTRL_KEY('g'), '1', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_STR("Buffer is empty", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_breaks_undo_typed_run_group(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);

	const char goto_first_line[] = {CTRL_KEY('g'), '1', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(
				goto_first_line, sizeof(goto_first_line)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(editor_process_single_key('z') == 0);
	ASSERT_EQ_STR("zab", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_q_exits_promptly(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(91);
		}

		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(92);
		}
		_exit(93);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));
	return 0;
}

static int test_editor_process_keypress_ctrl_q_restores_cursor_shape(void) {
	int pipefd[2];
	ASSERT_TRUE(pipe(pipefd) == 0);

	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		if (close(pipefd[0]) == -1) {
			_exit(111);
		}
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			_exit(112);
		}
		if (close(pipefd[1]) == -1) {
			_exit(113);
		}

		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(114);
		}
		_exit(115);
	}

	ASSERT_TRUE(close(pipefd[1]) == 0);
	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));

	size_t output_len = 0;
	char *output = read_all_fd(pipefd[0], &output_len);
	ASSERT_TRUE(close(pipefd[0]) == 0);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[0 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]112\x07") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_ctrl_q_dirty_requires_second_press(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(101);
		}

		add_row("unsaved");
		E.dirty = 1;

		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(102);
		}
		if (strcmp(E.statusmsg, "File has unsaved changes. Press Ctrl-Q again to quit") != 0) {
			_exit(103);
		}
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(104);
		}

		_exit(105);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));
	return 0;
}

static int test_editor_process_keypress_eof_exits_promptly_with_failure(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(121);
		}

		add_row("unsaved");
		E.dirty = 1;

		if (editor_process_keypress_with_input("", 0) == -1) {
			_exit(122);
		}
		_exit(123);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
	return 0;
}

static int test_editor_process_keypress_eof_restores_terminal_visual_state(void) {
	int pipefd[2];
	ASSERT_TRUE(pipe(pipefd) == 0);

	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		if (close(pipefd[0]) == -1) {
			_exit(131);
		}
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			_exit(132);
		}
		if (close(pipefd[1]) == -1) {
			_exit(133);
		}

		if (editor_process_keypress_with_input("", 0) == -1) {
			_exit(134);
		}
		_exit(135);
	}

	ASSERT_TRUE(close(pipefd[1]) == 0);
	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));

	size_t output_len = 0;
	char *output = read_all_fd(pipefd[0], &output_len);
	ASSERT_TRUE(close(pipefd[0]) == 0);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[0 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]112\x07") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2J") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[H") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_prompt_eof_exits_with_failure(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(141);
		}

		char input[] = {CTRL_KEY('f')};
		if (editor_process_keypress_with_input(input, sizeof(input)) == -1) {
			_exit(142);
		}
		_exit(143);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
	return 0;
}

static int test_process_terminates_promptly_on_sigterm(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		for (;;) {
			pause();
		}
	}

	ASSERT_TRUE(kill(pid, SIGTERM) == 0);
	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ_INT(SIGTERM, WTERMSIG(status));
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

	const char input[] = {
		CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a',
		'\x1b', '[', 'B',
		'\x1b', '[', 'B',
		'\x1b', '[', 'B',
		'\x1b', '[', 'A',
		'\r'
	};
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

static int test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	int dirty_after_insert = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(dirty_after_insert, E.dirty);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_group_break_on_navigation(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);
	ASSERT_EQ_STR("acb", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_for_delete_and_newline_steps(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);

	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key('\r') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_STR("b", E.rows[1].chars);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_y_clears_after_new_edit(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);
	ASSERT_EQ_STR("abc", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editor_process_single_key('x') == 0);
	ASSERT_EQ_STR("x", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("x", E.rows[0].chars);
	ASSERT_EQ_STR("Nothing to redo", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status(void) {
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("Nothing to undo", E.statusmsg);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("Nothing to redo", E.statusmsg);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_history_cap_eviction(void) {
	char text[ROTIDE_UNDO_HISTORY_LIMIT + 2];
	memset(text, 'x', ROTIDE_UNDO_HISTORY_LIMIT + 1);
	text[ROTIDE_UNDO_HISTORY_LIMIT + 1] = '\0';

	add_row(text);
	E.cy = 0;
	E.cx = ROTIDE_UNDO_HISTORY_LIMIT + 1;

	for (int i = 0; i < ROTIDE_UNDO_HISTORY_LIMIT + 1; i++) {
		ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	}
	ASSERT_EQ_STR("", E.rows[0].chars);

	for (int i = 0; i < ROTIDE_UNDO_HISTORY_LIMIT; i++) {
		ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	}
	ASSERT_EQ_INT(ROTIDE_UNDO_HISTORY_LIMIT, E.rows[0].size);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(ROTIDE_UNDO_HISTORY_LIMIT, E.rows[0].size);
	ASSERT_EQ_STR("Nothing to undo", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_capture_oom_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 5;

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);

	editorTestAllocFailAfter(0);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	editorTestAllocReset();
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_restore_oom_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 5;

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);

	editorTestAllocFailAfter(1);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	editorTestAllocReset();
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	return 0;
}

static int test_editor_column_select_extends_rectangle_with_shift_alt_arrows(void) {
	add_row("hello world");
	add_row("foobar baz!");
	add_row("0123456789a");
	E.cy = 0;
	E.cx = 2;
	E.pane_focus = EDITOR_PANE_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	for (int i = 0; i < 4; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
					sizeof(alt_shift_right) - 1) == 0);
	}
	ASSERT_EQ_INT(1, E.column_select_active);

	struct editorColumnSelectionRect rect;
	ASSERT_TRUE(editorColumnSelectionGetRect(&rect));
	ASSERT_EQ_INT(0, rect.top_cy);
	ASSERT_EQ_INT(2, rect.bottom_cy);
	ASSERT_EQ_INT(2, rect.left_rx);
	ASSERT_EQ_INT(6, rect.right_rx);
	return 0;
}

static int test_editor_column_select_copy_joins_per_row_slices_with_newlines(void) {
	add_row("hello world");
	add_row("foobar baz!");
	add_row("0123456789a");
	E.cy = 0;
	E.cx = 2;
	E.pane_focus = EDITOR_PANE_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	for (int i = 0; i < 4; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
					sizeof(alt_shift_right) - 1) == 0);
	}

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);
	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_TRUE(clip != NULL);
	ASSERT_EQ_INT(14, (int)clip_len);
	ASSERT_MEM_EQ("llo \nobar\n2345", clip, clip_len);
	return 0;
}

static int test_editor_column_select_delete_removes_rectangle_per_row(void) {
	add_row("hello world");
	add_row("foobar baz!");
	add_row("0123456789a");
	E.cy = 0;
	E.cx = 2;
	E.pane_focus = EDITOR_PANE_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	for (int i = 0; i < 4; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
					sizeof(alt_shift_right) - 1) == 0);
	}

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('d')) == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	ASSERT_EQ_STR("heworld", E.rows[0].chars);
	ASSERT_EQ_STR("fo baz!", E.rows[1].chars);
	ASSERT_EQ_STR("016789a", E.rows[2].chars);
	return 0;
}

static int test_editor_column_select_typing_inserts_char_on_each_row(void) {
	add_row("aaaa");
	add_row("bbbb");
	add_row("cccc");
	E.cy = 0;
	E.cx = 1;
	E.pane_focus = EDITOR_PANE_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);

	ASSERT_TRUE(editor_process_single_key('X') == 0);
	ASSERT_EQ_STR("aXaaa", E.rows[0].chars);
	ASSERT_EQ_STR("bXbbb", E.rows[1].chars);
	ASSERT_EQ_STR("cXccc", E.rows[2].chars);
	ASSERT_EQ_INT(1, E.column_select_active);

	// After typing the rect must remain multi-row (width 0) so subsequent typing
	// continues to insert on every row in the original column-selection.
	struct editorColumnSelectionRect rect;
	ASSERT_TRUE(editorColumnSelectionGetRect(&rect));
	ASSERT_EQ_INT(0, rect.top_cy);
	ASSERT_EQ_INT(2, rect.bottom_cy);
	ASSERT_EQ_INT(rect.right_rx, rect.left_rx);

	ASSERT_TRUE(editor_process_single_key('Y') == 0);
	ASSERT_EQ_STR("aXYaaa", E.rows[0].chars);
	ASSERT_EQ_STR("bXYbbb", E.rows[1].chars);
	ASSERT_EQ_STR("cXYccc", E.rows[2].chars);
	return 0;
}

static int test_editor_column_select_toggling_linear_selection_clears_column_mode(void) {
	add_row("abcdef");
	add_row("ghijkl");
	E.cy = 0;
	E.cx = 1;
	E.pane_focus = EDITOR_PANE_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	return 0;
}

static int test_editor_column_select_alt_mouse_drag_starts_column_selection(void) {
	add_row("abcdef");
	add_row("ghijkl");
	add_row("mnopqr");
	E.window_rows = 4;
	E.window_cols = 30;
	E.cy = 0;
	E.cx = 0;
	E.pane_focus = EDITOR_PANE_TEXT;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	// SGR press code for left button with Alt+Shift modifier (cb=12 = 8|4)
	int written = snprintf(press, sizeof(press), "\x1b[<12;%d;%dM", text_start + 2, 2);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(press));
	// Drag with motion bit (cb=32+12=44)
	written = snprintf(drag, sizeof(drag), "\x1b[<44;%d;%dM", text_start + 5, 4);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(drag));

	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	struct editorColumnSelectionRect rect;
	ASSERT_TRUE(editorColumnSelectionGetRect(&rect));
	ASSERT_EQ_INT(0, rect.top_cy);
	ASSERT_EQ_INT(2, rect.bottom_cy);
	ASSERT_EQ_INT(1, rect.left_rx);
	ASSERT_EQ_INT(4, rect.right_rx);
	return 0;
}

static int test_editor_column_select_plain_arrow_clears_mode(void) {
	add_row("abcdef");
	add_row("ghijkl");
	E.cy = 0;
	E.cx = 1;
	E.pane_focus = EDITOR_PANE_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);

	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down,
				sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	return 0;
}

const struct editorTestCase g_input_search_tests[] = {
	{"editor_process_keypress_keymap_remap_changes_dispatch", test_editor_process_keypress_keymap_remap_changes_dispatch},
	{"editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action", test_editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action},
	{"editor_task_log_document_stays_authoritative", test_editor_task_log_document_stays_authoritative},
	{"editor_task_log_streams_output_while_inactive", test_editor_task_log_streams_output_while_inactive},
	{"editor_task_runner_merges_stderr_and_close_requires_confirmation", test_editor_task_runner_merges_stderr_and_close_requires_confirmation},
	{"editor_task_runner_truncates_large_output", test_editor_task_runner_truncates_large_output},
	{"editor_process_keypress_resize_drawer_shortcuts", test_editor_process_keypress_resize_drawer_shortcuts},
	{"editor_column_select_extends_rectangle_with_shift_alt_arrows", test_editor_column_select_extends_rectangle_with_shift_alt_arrows},
	{"editor_column_select_copy_joins_per_row_slices_with_newlines", test_editor_column_select_copy_joins_per_row_slices_with_newlines},
	{"editor_column_select_delete_removes_rectangle_per_row", test_editor_column_select_delete_removes_rectangle_per_row},
	{"editor_column_select_typing_inserts_char_on_each_row", test_editor_column_select_typing_inserts_char_on_each_row},
	{"editor_column_select_toggling_linear_selection_clears_column_mode", test_editor_column_select_toggling_linear_selection_clears_column_mode},
	{"editor_column_select_alt_mouse_drag_starts_column_selection", test_editor_column_select_alt_mouse_drag_starts_column_selection},
	{"editor_column_select_plain_arrow_clears_mode", test_editor_column_select_plain_arrow_clears_mode},
	{"editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands", test_editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands},
	{"editor_process_keypress_toggle_drawer_preserves_search_modes", test_editor_process_keypress_toggle_drawer_preserves_search_modes},
	{"editor_process_keypress_main_menu_runs_selected_action", test_editor_process_keypress_main_menu_runs_selected_action},
	{"editor_tabs_switch_restores_per_tab_state", test_editor_tabs_switch_restores_per_tab_state},
	{"editor_tab_close_last_tab_keeps_one_empty_tab", test_editor_tab_close_last_tab_keeps_one_empty_tab},
	{"editor_process_keypress_ctrl_w_dirty_requires_second_press", test_editor_process_keypress_ctrl_w_dirty_requires_second_press},
	{"editor_process_keypress_close_tab_confirmation_resets_on_other_action", test_editor_process_keypress_close_tab_confirmation_resets_on_other_action},
	{"editor_process_keypress_ctrl_q_checks_dirty_tabs_globally", test_editor_process_keypress_ctrl_q_checks_dirty_tabs_globally},
	{"editor_process_keypress_tab_actions_new_next_prev", test_editor_process_keypress_tab_actions_new_next_prev},
	{"editor_tab_open_file_reuses_active_clean_empty_buffer", test_editor_tab_open_file_reuses_active_clean_empty_buffer},
	{"editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive", test_editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive},
	{"editor_process_keypress_focus_drawer_and_arrow_navigation", test_editor_process_keypress_focus_drawer_and_arrow_navigation},
	{"editor_process_keypress_drawer_enter_toggles_directory", test_editor_process_keypress_drawer_enter_toggles_directory},
	{"editor_process_keypress_drawer_enter_opens_file_in_new_tab", test_editor_process_keypress_drawer_enter_opens_file_in_new_tab},
	{"editor_process_keypress_find_file_filters_previews_and_opens", test_editor_process_keypress_find_file_filters_previews_and_opens},
	{"editor_process_keypress_project_search_filters_previews_and_opens", test_editor_process_keypress_project_search_filters_previews_and_opens},
	{"editor_process_keypress_insert_move_and_backspace", test_editor_process_keypress_insert_move_and_backspace},
	{"editor_process_keypress_ctrl_j_does_not_insert_newline", test_editor_process_keypress_ctrl_j_does_not_insert_newline},
	{"editor_process_keypress_tab_inserts_literal_tab", test_editor_process_keypress_tab_inserts_literal_tab},
	{"editor_process_keypress_utf8_bytes_insert_verbatim", test_editor_process_keypress_utf8_bytes_insert_verbatim},
	{"editor_process_keypress_delete_key", test_editor_process_keypress_delete_key},
	{"editor_process_keypress_arrow_down_keeps_visual_column", test_editor_process_keypress_arrow_down_keeps_visual_column},
	{"editor_process_keypress_ctrl_s_saves_file", test_editor_process_keypress_ctrl_s_saves_file},
	{"editor_process_keypress_resize_event_updates_window_size", test_editor_process_keypress_resize_event_updates_window_size},
	{"editor_process_keypress_alt_z_toggles_line_wrap_without_dirty", test_editor_process_keypress_alt_z_toggles_line_wrap_without_dirty},
	{"editor_process_keypress_alt_n_toggles_line_numbers_without_dirty", test_editor_process_keypress_alt_n_toggles_line_numbers_without_dirty},
	{"editor_process_keypress_alt_h_toggles_current_line_highlight_without_dirty", test_editor_process_keypress_alt_h_toggles_current_line_highlight_without_dirty},
	{"editor_process_keypress_mouse_left_click_places_cursor_with_offsets", test_editor_process_keypress_mouse_left_click_places_cursor_with_offsets},
	{"editor_process_keypress_mouse_click_maps_same_column_with_line_numbers", test_editor_process_keypress_mouse_click_maps_same_column_with_line_numbers},
	{"editor_process_keypress_mouse_left_click_places_cursor_on_wrapped_segment", test_editor_process_keypress_mouse_left_click_places_cursor_on_wrapped_segment},
	{"editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection", test_editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection},
	{"editor_process_keypress_mouse_left_click_ignores_non_text_rows", test_editor_process_keypress_mouse_left_click_ignores_non_text_rows},
	{"editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns", test_editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns},
	{"editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory", test_editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory},
	{"editor_process_keypress_mouse_click_expands_collapsed_drawer", test_editor_process_keypress_mouse_click_expands_collapsed_drawer},
	{"editor_process_keypress_mouse_drawer_header_mode_buttons", test_editor_process_keypress_mouse_drawer_header_mode_buttons},
	{"editor_process_keypress_mouse_collapsed_drawer_body_click_edits_text_pane", test_editor_process_keypress_mouse_collapsed_drawer_body_click_edits_text_pane},
	{"editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab", test_editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab},
	{"editor_drawer_open_selected_file_in_preview_reuses_preview_tab", test_editor_drawer_open_selected_file_in_preview_reuses_preview_tab},
	{"editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab", test_editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab},
	{"editor_process_keypress_mouse_top_row_click_switches_tab", test_editor_process_keypress_mouse_top_row_click_switches_tab},
	{"editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout", test_editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout},
	{"editor_process_keypress_mouse_drag_on_splitter_resizes_drawer", test_editor_process_keypress_mouse_drag_on_splitter_resizes_drawer},
	{"editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps", test_editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps},
	{"editor_process_keypress_mouse_wheel_scrolls_wrapped_segments", test_editor_process_keypress_mouse_wheel_scrolls_wrapped_segments},
	{"editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps", test_editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps},
	{"editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered", test_editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered},
	{"editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer", test_editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer},
	{"editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused", test_editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused},
	{"editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor", test_editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor},
	{"editor_process_keypress_ctrl_arrow_scrolls_horizontally_without_moving_cursor", test_editor_process_keypress_ctrl_arrow_scrolls_horizontally_without_moving_cursor},
	{"editor_process_keypress_free_scroll_can_leave_cursor_offscreen", test_editor_process_keypress_free_scroll_can_leave_cursor_offscreen},
	{"editor_process_keypress_cursor_move_resyncs_follow_scroll", test_editor_process_keypress_cursor_move_resyncs_follow_scroll},
	{"editor_process_keypress_edit_resyncs_follow_scroll", test_editor_process_keypress_edit_resyncs_follow_scroll},
	{"editor_process_keypress_mouse_click_clears_existing_selection", test_editor_process_keypress_mouse_click_clears_existing_selection},
	{"editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b", test_editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b},
	{"editor_process_keypress_mouse_press_without_drag_keeps_click_behavior", test_editor_process_keypress_mouse_press_without_drag_keeps_click_behavior},
	{"editor_process_keypress_mouse_drag_resets_existing_selection_anchor", test_editor_process_keypress_mouse_drag_resets_existing_selection_anchor},
	{"editor_process_keypress_mouse_drag_clamps_to_viewport_without_autoscroll", test_editor_process_keypress_mouse_drag_clamps_to_viewport_without_autoscroll},
	{"editor_process_keypress_mouse_drag_honors_rowoff_and_coloff", test_editor_process_keypress_mouse_drag_honors_rowoff_and_coloff},
	{"editor_process_keypress_mouse_release_stops_drag_session", test_editor_process_keypress_mouse_release_stops_drag_session},
	{"editor_prompt_ignores_mouse_events", test_editor_prompt_ignores_mouse_events},
	{"editor_prompt_ignores_resize_events", test_editor_prompt_ignores_resize_events},
	{"editor_process_keypress_ctrl_b_toggles_selection_mode", test_editor_process_keypress_ctrl_b_toggles_selection_mode},
	{"editor_selection_range_tracks_cursor_movement", test_editor_selection_range_tracks_cursor_movement},
	{"editor_extract_range_text_uses_document_when_row_cache_corrupt", test_editor_extract_range_text_uses_document_when_row_cache_corrupt},
	{"editor_delete_range_uses_document_when_row_cache_corrupt", test_editor_delete_range_uses_document_when_row_cache_corrupt},
	{"editor_process_keypress_ctrl_c_copies_single_line_selection", test_editor_process_keypress_ctrl_c_copies_single_line_selection},
	{"editor_process_keypress_ctrl_c_copies_multiline_selection", test_editor_process_keypress_ctrl_c_copies_multiline_selection},
	{"editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard", test_editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard},
	{"editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard", test_editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard},
	{"editor_process_keypress_ctrl_v_pastes_clipboard_text", test_editor_process_keypress_ctrl_v_pastes_clipboard_text},
	{"editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text", test_editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text},
	{"editor_process_keypress_ctrl_v_empty_clipboard_is_noop", test_editor_process_keypress_ctrl_v_empty_clipboard_is_noop},
	{"editor_clipboard_sync_osc52_plain_sequence", test_editor_clipboard_sync_osc52_plain_sequence},
	{"editor_clipboard_sync_osc52_tmux_wrapped_sequence", test_editor_clipboard_sync_osc52_tmux_wrapped_sequence},
	{"editor_clipboard_sync_osc52_screen_wrapped_sequence", test_editor_clipboard_sync_osc52_screen_wrapped_sequence},
	{"editor_clipboard_sync_osc52_mode_off_emits_nothing", test_editor_clipboard_sync_osc52_mode_off_emits_nothing},
	{"editor_clipboard_sync_osc52_auto_mode_skips_non_tty", test_editor_clipboard_sync_osc52_auto_mode_skips_non_tty},
	{"editor_clipboard_sync_osc52_payload_cap_skips_external_write", test_editor_clipboard_sync_osc52_payload_cap_skips_external_write},
	{"editor_process_keypress_ctrl_v_clears_selection_mode", test_editor_process_keypress_ctrl_v_clears_selection_mode},
	{"editor_process_keypress_ctrl_v_undo_roundtrip_single_step", test_editor_process_keypress_ctrl_v_undo_roundtrip_single_step},
	{"editor_process_keypress_selection_ops_noop_without_selection", test_editor_process_keypress_selection_ops_noop_without_selection},
	{"editor_process_keypress_escape_clears_selection_mode", test_editor_process_keypress_escape_clears_selection_mode},
	{"editor_process_keypress_edit_ops_clear_selection_mode", test_editor_process_keypress_edit_ops_clear_selection_mode},
	{"editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut", test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut},
	{"editor_process_keypress_ctrl_c_oom_preserves_buffer", test_editor_process_keypress_ctrl_c_oom_preserves_buffer},
	{"editor_process_keypress_ctrl_g_jumps_to_line_and_sets_col_zero", test_editor_process_keypress_ctrl_g_jumps_to_line_and_sets_col_zero},
	{"editor_process_keypress_ctrl_g_clamps_to_last_line", test_editor_process_keypress_ctrl_g_clamps_to_last_line},
	{"editor_process_keypress_ctrl_g_rejects_invalid_input", test_editor_process_keypress_ctrl_g_rejects_invalid_input},
	{"editor_process_keypress_ctrl_g_escape_cancels", test_editor_process_keypress_ctrl_g_escape_cancels},
	{"editor_process_keypress_ctrl_g_empty_buffer_sets_status", test_editor_process_keypress_ctrl_g_empty_buffer_sets_status},
	{"editor_process_keypress_ctrl_g_breaks_undo_typed_run_group", test_editor_process_keypress_ctrl_g_breaks_undo_typed_run_group},
	{"editor_process_keypress_ctrl_q_exits_promptly", test_editor_process_keypress_ctrl_q_exits_promptly},
	{"editor_process_keypress_ctrl_q_restores_cursor_shape", test_editor_process_keypress_ctrl_q_restores_cursor_shape},
	{"editor_process_keypress_ctrl_q_dirty_requires_second_press", test_editor_process_keypress_ctrl_q_dirty_requires_second_press},
	{"editor_process_keypress_eof_exits_promptly_with_failure", test_editor_process_keypress_eof_exits_promptly_with_failure},
	{"editor_process_keypress_eof_restores_terminal_visual_state", test_editor_process_keypress_eof_restores_terminal_visual_state},
	{"editor_process_keypress_prompt_eof_exits_with_failure", test_editor_process_keypress_prompt_eof_exits_with_failure},
	{"process_terminates_promptly_on_sigterm", test_process_terminates_promptly_on_sigterm},
	{"editor_process_keypress_ctrl_f_incremental_find_first_match", test_editor_process_keypress_ctrl_f_incremental_find_first_match},
	{"editor_process_keypress_ctrl_f_arrow_navigation_wraps", test_editor_process_keypress_ctrl_f_arrow_navigation_wraps},
	{"editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match", test_editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match},
	{"editor_process_keypress_ctrl_f_enter_keeps_active_match", test_editor_process_keypress_ctrl_f_enter_keeps_active_match},
	{"editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status", test_editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status},
	{"editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run", test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run},
	{"editor_process_keypress_ctrl_z_group_break_on_navigation", test_editor_process_keypress_ctrl_z_group_break_on_navigation},
	{"editor_process_keypress_ctrl_z_for_delete_and_newline_steps", test_editor_process_keypress_ctrl_z_for_delete_and_newline_steps},
	{"editor_process_keypress_ctrl_y_clears_after_new_edit", test_editor_process_keypress_ctrl_y_clears_after_new_edit},
	{"editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status", test_editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status},
	{"editor_process_keypress_ctrl_z_history_cap_eviction", test_editor_process_keypress_ctrl_z_history_cap_eviction},
	{"editor_process_keypress_ctrl_z_capture_oom_preserves_state", test_editor_process_keypress_ctrl_z_capture_oom_preserves_state},
	{"editor_process_keypress_ctrl_z_restore_oom_preserves_state", test_editor_process_keypress_ctrl_z_restore_oom_preserves_state},
	{"editor_refresh_screen_highlights_active_selection_spans", test_editor_refresh_screen_highlights_active_selection_spans},
};

const int g_input_search_test_count =
		(int)(sizeof(g_input_search_tests) / sizeof(g_input_search_tests[0]));
