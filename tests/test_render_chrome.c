#include "input/input_system.h"
#include "render/display_text.h"
#include "render/status_bar.h"
#include "render/tab_bar.h"
#include "render/viewport.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TEST_HEADER_BG "\x1b[48;5;236m"
#define TEST_HEADER_ACTIVE "\x1b[7m"
#define TEST_HEADER_RESET "\x1b[m"
#define TEST_DRAWER_COLLAPSE_SYMBOL "\xE2\x80\xB9"
#define TEST_DRAWER_EXPAND_SYMBOL "\xE2\x80\xBA"
#define TEST_DRAWER_EXPLORER_SYMBOL "E"
#define TEST_DRAWER_FILE_SEARCH_SYMBOL "F"
#define TEST_DRAWER_PROJECT_SEARCH_SYMBOL "/"
#define TEST_DRAWER_LSP_SYMBOL "L"
#define TEST_DRAWER_DAP_SYMBOL "D"
#define TEST_DRAWER_GIT_SYMBOL "\xE2\x91\x82"
#define TEST_DRAWER_MAIN_MENU_SYMBOL "\xE2\x89\xA1"
#define TEST_NERD_FOLDER "\xEF\x81\xBB"
#define TEST_NERD_FOLDER_OPEN "\xEF\x81\xBC"
#define TEST_NERD_FILE_TEXT "\xEF\x85\x9C"
#define TEST_NERD_FILE_CODE "\xEF\x87\x89"
#define TEST_NERD_SEARCH "\xEF\x80\x82"
#define TEST_NERD_BRANCH "\xEF\x84\xA6"
#define TEST_NERD_BARS "\xEF\x83\x89"
#define TEST_NERD_TERMINAL "\xEF\x84\xA0"
#define TEST_DRAWER_COLLAPSE_CELL                                                                  \
	TEST_HEADER_BG " " TEST_DRAWER_COLLAPSE_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_EXPAND_CELL TEST_HEADER_BG " " TEST_DRAWER_EXPAND_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_EXPLORER_CELL                                                                  \
	TEST_HEADER_BG " " TEST_DRAWER_EXPLORER_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_FILE_SEARCH_CELL                                                               \
	TEST_HEADER_BG " " TEST_DRAWER_FILE_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_PROJECT_SEARCH_CELL                                                            \
	TEST_HEADER_BG " " TEST_DRAWER_PROJECT_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_LSP_CELL TEST_HEADER_BG " " TEST_DRAWER_LSP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_DAP_CELL TEST_HEADER_BG " " TEST_DRAWER_DAP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_GIT_CELL TEST_HEADER_BG " " TEST_DRAWER_GIT_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_MAIN_MENU_CELL                                                                 \
	TEST_HEADER_BG " " TEST_DRAWER_MAIN_MENU_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_EXPLORER_CELL                                                           \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_EXPLORER_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_FILE_SEARCH_CELL                                                        \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_FILE_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_PROJECT_SEARCH_CELL                                                     \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_PROJECT_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_LSP_CELL                                                                \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_LSP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_DAP_CELL                                                                \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_DAP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_GIT_CELL                                                                \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_GIT_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_MAIN_MENU_CELL                                                          \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_MAIN_MENU_SYMBOL " " TEST_HEADER_RESET

static int test_editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/a\x1b"
	                    "[31m.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/beta.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/gamma.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 3;
	E.window_cols = 50;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "* a") != NULL);
	ASSERT_TRUE(strstr(output, "   >\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "beta.txt") == NULL);
	ASSERT_TRUE(strstr(output, "gamma.txt") == NULL);
	ASSERT_TRUE(strstr(output, ">") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_preview_tab_label_uses_italics(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/sticky.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/preview.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.is_preview = 1;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 3;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3mpreview.txt\x1b[23m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3msticky.txt\x1b[23m") == NULL);
	free(output);
	return 0;
}

static int test_editor_tab_layout_width_includes_right_label_padding(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/ab.txt");
	ASSERT_TRUE(E.filename != NULL);

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(80, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_EQ_INT(1, layout_count);
	ASSERT_EQ_INT(6 + (int)strlen("ab.txt"), layout[0].width_cols);
	return 0;
}

static int test_editor_tabs_align_view_keeps_active_visible_with_variable_widths(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/first_tab_with_a_long_name_001.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/second_tab_with_a_long_name_002.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/third_tab_with_a_long_name_003.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/fourth_tab_with_a_long_name_004.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabSwitchToIndex(3));
	E.window_cols = 46;
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(
	        editorTabBuildLayoutForWidth(text_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count >= 1);
	ASSERT_TRUE(E.focused_leaf != NULL);
	ASSERT_TRUE(E.focused_leaf->as.leaf.view.tab_view_start > 0);

	int active_visible = 0;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 3) {
			active_visible = 1;
			break;
		}
	}
	ASSERT_TRUE(active_visible);
	return 0;
}

static int test_editor_draw_pane_tab_strip_uses_pane_membership_and_active_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/alpha.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/beta.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/gamma.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/delta.txt");
	ASSERT_TRUE(E.filename != NULL);

	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	ASSERT_TRUE(left != NULL);
	ASSERT_TRUE(right != NULL);
	ASSERT_TRUE(editorPaneViewAddTab(&left->as.leaf.view, 0));
	ASSERT_TRUE(editorPaneViewAddTab(&left->as.leaf.view, 1));
	left->as.leaf.view.active_tab_idx = 1;
	ASSERT_TRUE(editorPaneViewAddTab(&right->as.leaf.view, 2));
	ASSERT_TRUE(editorPaneViewAddTab(&right->as.leaf.view, 3));
	right->as.leaf.view.active_tab_idx = 2;

	struct writeBuf left_wb = WRITEBUF_INIT;
	struct writeBuf right_wb = WRITEBUF_INIT;
	ASSERT_TRUE(editorDrawPaneTabStrip(&left_wb, left, 80, 0));
	ASSERT_TRUE(editorDrawPaneTabStrip(&right_wb, right, 80, 0));
	ASSERT_TRUE(wbAppend(&left_wb, "\0", 1));
	ASSERT_TRUE(wbAppend(&right_wb, "\0", 1));

	ASSERT_TRUE(strstr(left_wb.b, "alpha.txt") != NULL);
	ASSERT_TRUE(strstr(left_wb.b, "beta.txt") != NULL);
	ASSERT_TRUE(strstr(left_wb.b, "gamma.txt") == NULL);
	ASSERT_TRUE(strstr(left_wb.b, "delta.txt") == NULL);
	ASSERT_TRUE(strstr(left_wb.b, "\x1b[7m   beta.txt") != NULL);
	ASSERT_TRUE(strstr(left_wb.b, "\x1b[7m   alpha.txt") == NULL);

	ASSERT_TRUE(strstr(right_wb.b, "alpha.txt") == NULL);
	ASSERT_TRUE(strstr(right_wb.b, "beta.txt") == NULL);
	ASSERT_TRUE(strstr(right_wb.b, "gamma.txt") != NULL);
	ASSERT_TRUE(strstr(right_wb.b, "delta.txt") != NULL);
	ASSERT_TRUE(strstr(right_wb.b, "\x1b[7m   gamma.txt") != NULL);
	ASSERT_TRUE(strstr(right_wb.b, "\x1b[7m   delta.txt") == NULL);

	wbFree(&left_wb);
	wbFree(&right_wb);
	editorPaneNodeFree(left);
	editorPaneNodeFree(right);
	return 0;
}

static int test_editor_refresh_screen_renders_drawer_entries_and_selection(void) {
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
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	E.window_rows = 4;
	E.window_cols = 40;
	E.line_numbers_enabled = 0;
	add_row("body");
	struct editorDrawerEntryView root_view;
	ASSERT_TRUE(editorDrawerVisibleEntryView(0, &root_view));
	char expected_root_bold[256];
	ASSERT_TRUE(snprintf(expected_root_bold, sizeof(expected_root_bold),
	                     "\x1b[1m\x1b[37m%s\x1b[39m\x1b[22m", root_view.name) > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_root_bold) != NULL);
	const char *drawer_header = strstr(output, TEST_DRAWER_COLLAPSE_SYMBOL);
	const char *root_text = strstr(output, expected_root_bold);
	ASSERT_TRUE(drawer_header != NULL);
	ASSERT_TRUE(root_text != NULL);
	const char *header_end = strstr(drawer_header, "\r\n");
	ASSERT_TRUE(header_end != NULL);
	ASSERT_TRUE(header_end < root_text);
	ASSERT_TRUE(strstr(output, "\x1b[7m \xE2\x96\xBE src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x9C src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94 src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xBE") != NULL);
	ASSERT_TRUE(strstr(output, "src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x80") != NULL);
	ASSERT_TRUE(strstr(output, "child.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x82 body") != NULL);
	free(output);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_colors_files_by_git_status(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char modified_file[512];
	char untracked_file[512];
	char conflict_file[512];
	ASSERT_TRUE(
	        path_join(modified_file, sizeof(modified_file), env.project_dir, "modified.txt"));
	ASSERT_TRUE(path_join(untracked_file, sizeof(untracked_file), env.project_dir,
	                      "untracked.txt"));
	ASSERT_TRUE(
	        path_join(conflict_file, sizeof(conflict_file), env.project_dir, "conflict.txt"));
	ASSERT_TRUE(write_text_file(modified_file, "m\n"));
	ASSERT_TRUE(write_text_file(untracked_file, "u\n"));
	ASSERT_TRUE(write_text_file(conflict_file, "c\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	free(E.git_repo_root);
	E.git_repo_root = strdup(env.project_dir);
	ASSERT_TRUE(E.git_repo_root != NULL);
	// Inject in alphabetical order so editorGitFileStatus's binary search works.
	struct editorGitEntry *grown = realloc(E.git_entries, 3 * sizeof(struct editorGitEntry));
	ASSERT_TRUE(grown != NULL);
	E.git_entries = grown;
	E.git_entry_capacity = 3;
	E.git_entries[0].rel_path = strdup("conflict.txt");
	E.git_entries[0].status = EDITOR_GIT_STATUS_CONFLICT;
	E.git_entries[0].index_status = 'U';
	E.git_entries[0].worktree_status = 'U';
	E.git_entries[1].rel_path = strdup("modified.txt");
	E.git_entries[1].status = EDITOR_GIT_STATUS_MODIFIED;
	E.git_entries[1].index_status = ' ';
	E.git_entries[1].worktree_status = 'M';
	E.git_entries[2].rel_path = strdup("untracked.txt");
	E.git_entries[2].status = EDITOR_GIT_STATUS_UNTRACKED;
	E.git_entries[2].index_status = '?';
	E.git_entries[2].worktree_status = '?';
	E.git_entry_count = 3;

	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 8;
	E.window_cols = 50;
	E.line_numbers_enabled = 0;
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);

	// Yellow (modified), green (untracked), red (conflict) precede the file name.
	ASSERT_TRUE(strstr(output, "\x1b[33mmodified.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32muntracked.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[31mconflict.txt") != NULL);
	free(output);

	editorGitFree();
	ASSERT_TRUE(unlink(conflict_file) == 0);
	ASSERT_TRUE(unlink(untracked_file) == 0);
	ASSERT_TRUE(unlink(modified_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_renders_directories_bold_and_cyan(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), env.project_dir, "leaf.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "leaf\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 6;
	E.window_cols = 40;
	E.line_numbers_enabled = 0;
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);

	// Directory "src" should be wrapped in bold + cyan + reset.
	const char *dir_marker = strstr(output, "src");
	ASSERT_TRUE(dir_marker != NULL);
	const char *dir_bold_cyan = strstr(output, "\x1b[1m\x1b[36msrc\x1b[39m\x1b[22m");
	ASSERT_TRUE(dir_bold_cyan != NULL);

	// Regular file "leaf.txt" should NOT be wrapped in cyan.
	ASSERT_TRUE(strstr(output, "\x1b[36mleaf.txt") == NULL);
	ASSERT_TRUE(strstr(output, "leaf.txt") != NULL);
	free(output);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_hides_selection_marker_when_unfocused(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));

	E.window_rows = 4;
	E.window_cols = 40;
	add_row("body");

	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	size_t focused_len = 0;
	char *focused = refresh_screen_and_capture(&focused_len);
	ASSERT_TRUE(focused != NULL);
	ASSERT_TRUE(strstr(focused, "\x1b[7m \xE2\x96\xB8 src") != NULL);
	free(focused);

	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	size_t unfocused_len = 0;
	char *unfocused = refresh_screen_and_capture(&unfocused_len);
	ASSERT_TRUE(unfocused != NULL);
	ASSERT_TRUE(strstr(unfocused, "\xE2\x97\x8F") == NULL);
	ASSERT_TRUE(strstr(unfocused, "\x1b[7m \xE2\x96\xB8 src") == NULL);
	free(unfocused);

	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_active_file_uses_inverted_background(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char active_file[512];
	ASSERT_TRUE(path_join(active_file, sizeof(active_file), env.project_dir, "active.txt"));
	ASSERT_TRUE(write_text_file(active_file, "active\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenFileAsNew(active_file));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 6;
	E.window_cols = 60;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m active.txt") != NULL);
	ASSERT_TRUE(strstr(output, "active.txt\x1b[m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[m\xE2\x94\x82") != NULL);
	free(output);

	ASSERT_TRUE(unlink(active_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_collapsed_renders_expand_indicator(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	add_row("body");
	E.window_rows = 4;
	E.window_cols = 20;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, TEST_DRAWER_EXPAND_CELL) != NULL);
	ASSERT_TRUE(strstr(output, TEST_DRAWER_COLLAPSE_SYMBOL) == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x82") == NULL);
	ASSERT_EQ_INT(ROTIDE_DRAWER_COLLAPSED_WIDTH, editorDrawerWidthForCols(E.window_cols));
	ASSERT_EQ_INT(0, editorDrawerSeparatorWidthForCols(E.window_cols));
	ASSERT_EQ_INT(ROTIDE_DRAWER_COLLAPSED_WIDTH,
	              editorDrawerTextStartColForCols(E.window_cols));
	ASSERT_EQ_INT(E.window_cols - ROTIDE_DRAWER_COLLAPSED_WIDTH,
	              editorDrawerTextViewportCols(E.window_cols));
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_multi_pane_collapsed_reserves_toggle_area(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("alpha");
	E.window_rows = 4;
	E.window_cols = 40;
	E.line_numbers_enabled = 0;
	ASSERT_TRUE(editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5) != NULL);
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	struct editorRect viewport;
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	ASSERT_EQ_INT(ROTIDE_DRAWER_COLLAPSED_WIDTH, viewport.x);
	ASSERT_EQ_INT(E.window_cols - ROTIDE_DRAWER_COLLAPSED_WIDTH, viewport.w);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, TEST_DRAWER_EXPAND_CELL) != NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_header_mode_buttons(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("body");
	E.window_rows = 6;
	E.window_cols = 60;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(
	        strstr(output,
	               TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_ACTIVE_EXPLORER_CELL
	                       TEST_DRAWER_FILE_SEARCH_CELL TEST_DRAWER_PROJECT_SEARCH_CELL
	                               TEST_DRAWER_LSP_CELL TEST_DRAWER_DAP_CELL
	                                       TEST_DRAWER_GIT_CELL TEST_DRAWER_MAIN_MENU_CELL) !=
	        NULL);
	free(output);

	ASSERT_TRUE(editorFileSearchEnter());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(
	        strstr(output,
	               TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_EXPLORER_CELL
	                       TEST_DRAWER_ACTIVE_FILE_SEARCH_CELL TEST_DRAWER_PROJECT_SEARCH_CELL
	                               TEST_DRAWER_LSP_CELL TEST_DRAWER_DAP_CELL
	                                       TEST_DRAWER_GIT_CELL TEST_DRAWER_MAIN_MENU_CELL) !=
	        NULL);
	free(output);
	editorFileSearchExit(1);

	ASSERT_TRUE(editorProjectSearchEnter());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(
	        strstr(output,
	               TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_EXPLORER_CELL
	                       TEST_DRAWER_FILE_SEARCH_CELL TEST_DRAWER_ACTIVE_PROJECT_SEARCH_CELL
	                               TEST_DRAWER_LSP_CELL TEST_DRAWER_DAP_CELL
	                                       TEST_DRAWER_GIT_CELL TEST_DRAWER_MAIN_MENU_CELL) !=
	        NULL);
	free(output);
	editorProjectSearchExit(1);

	ASSERT_TRUE(editorDrawerLspToggle());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(
	        strstr(output,
	               TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_EXPLORER_CELL
	                       TEST_DRAWER_FILE_SEARCH_CELL TEST_DRAWER_PROJECT_SEARCH_CELL
	                               TEST_DRAWER_ACTIVE_LSP_CELL TEST_DRAWER_DAP_CELL
	                                       TEST_DRAWER_GIT_CELL TEST_DRAWER_MAIN_MENU_CELL) !=
	        NULL);
	free(output);
	ASSERT_TRUE(editorDrawerLspToggle());

	ASSERT_TRUE(editorDrawerDapToggle());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(
	        strstr(output,
	               TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_EXPLORER_CELL
	                       TEST_DRAWER_FILE_SEARCH_CELL TEST_DRAWER_PROJECT_SEARCH_CELL
	                               TEST_DRAWER_LSP_CELL TEST_DRAWER_ACTIVE_DAP_CELL
	                                       TEST_DRAWER_GIT_CELL TEST_DRAWER_MAIN_MENU_CELL) !=
	        NULL);
	free(output);
	ASSERT_TRUE(editorDrawerDapToggle());

	ASSERT_TRUE(editorDrawerGitToggle());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output,
	                   TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_EXPLORER_CELL
	                           TEST_DRAWER_FILE_SEARCH_CELL TEST_DRAWER_PROJECT_SEARCH_CELL
	                                   TEST_DRAWER_LSP_CELL TEST_DRAWER_DAP_CELL
	                                           TEST_DRAWER_ACTIVE_GIT_CELL
	                                                   TEST_DRAWER_MAIN_MENU_CELL) != NULL);
	free(output);
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editorDrawerMainMenuToggle());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output,
	                   TEST_DRAWER_COLLAPSE_CELL TEST_DRAWER_EXPLORER_CELL
	                           TEST_DRAWER_FILE_SEARCH_CELL TEST_DRAWER_PROJECT_SEARCH_CELL
	                                   TEST_DRAWER_LSP_CELL TEST_DRAWER_DAP_CELL
	                                           TEST_DRAWER_GIT_CELL
	                                                   TEST_DRAWER_ACTIVE_MAIN_MENU_CELL) !=
	            NULL);
	free(output);

	ASSERT_TRUE(editorDrawerSetWidthForCols(14, E.window_cols));
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	const char *drawer_header = strstr(output, TEST_DRAWER_COLLAPSE_SYMBOL);
	ASSERT_TRUE(drawer_header != NULL);
	const char *header_end = strstr(drawer_header, "\r\n");
	ASSERT_TRUE(header_end != NULL);
	ASSERT_TRUE(strstr(drawer_header, TEST_DRAWER_EXPLORER_SYMBOL) == NULL ||
	            strstr(drawer_header, TEST_DRAWER_EXPLORER_SYMBOL) > header_end);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_header_background_fills_wide_row(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("body");
	E.window_rows = 6;
	E.window_cols = 80;
	ASSERT_TRUE(editorDrawerSetWidthForCols(30, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	const char *main_menu_cell = strstr(output, TEST_DRAWER_MAIN_MENU_CELL);
	ASSERT_TRUE(main_menu_cell != NULL);

	const char *drawer_header = strstr(output, TEST_DRAWER_COLLAPSE_SYMBOL);
	ASSERT_TRUE(drawer_header != NULL);
	const char *header_end = strstr(drawer_header, "\r\n");
	ASSERT_TRUE(header_end != NULL);

	const int button_cols = ROTIDE_DRAWER_COLLAPSED_WIDTH + 7 * 3;
	const int fill_cols = editorDrawerWidthForCols(E.window_cols) - button_cols;
	ASSERT_TRUE(fill_cols > 0);
	char expected_end[128];
	int expected_len = snprintf(expected_end, sizeof(expected_end),
	                            TEST_HEADER_BG "%*s" TEST_HEADER_RESET, fill_cols, "");
	ASSERT_TRUE(expected_len > 0 && expected_len < (int)sizeof(expected_end));
	const char *fill_start = main_menu_cell + strlen(TEST_DRAWER_MAIN_MENU_CELL);
	ASSERT_TRUE(fill_start < header_end);
	ASSERT_TRUE((size_t)(header_end - fill_start) >= (size_t)expected_len);
	ASSERT_TRUE(strncmp(fill_start, expected_end, expected_len) == 0);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_uses_nerd_font_icons_when_enabled(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char code_file[512];
	char text_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(code_file, sizeof(code_file), src_dir, "main.c"));
	ASSERT_TRUE(path_join(text_file, sizeof(text_file), env.project_dir, "README.md"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(code_file, "int main(void) { return 0; }\n"));
	ASSERT_TRUE(write_text_file(text_file, "# title\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	E.nerd_fonts_enabled = 1;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 8;
	E.window_cols = 70;
	E.line_numbers_enabled = 0;
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_FOLDER) != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_FILE_TEXT) != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_SEARCH) != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_TERMINAL) != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_BRANCH) != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_BARS) != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_FOLDER_OPEN " src") == NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_FOLDER " src") == NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_FILE_CODE "\x1b[39m main.c") != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_FILE_TEXT "\x1b[39m README.md") != NULL);
	free(output);

	ASSERT_TRUE(editorDrawerMainMenuToggle());
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_BARS " Main Menu") == NULL);
	ASSERT_TRUE(strstr(output, "Main Menu") != NULL);
	ASSERT_TRUE(strstr(output, TEST_NERD_SEARCH "\x1b[39m Find File") != NULL);
	free(output);

	ASSERT_TRUE(unlink(code_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	ASSERT_TRUE(unlink(text_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_main_menu_drawer_groups_actions(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerMainMenuToggle());

	add_row("body");
	E.window_rows = 14;
	E.window_cols = 60;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Main Menu") != NULL);
	ASSERT_TRUE(strstr(output, "Find") != NULL);
	ASSERT_TRUE(strstr(output, "Find File") != NULL);
	ASSERT_TRUE(strstr(output, "Find & replace") != NULL);
	ASSERT_TRUE(strstr(output, "Search Project Text") != NULL);
	ASSERT_TRUE(strstr(output, "Save") != NULL);
	ASSERT_TRUE(strstr(output, "Close Tab") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mMain Menu") == NULL);
	ASSERT_TRUE(find_drawer_entry("Project Files", NULL, NULL));
	free(output);

	int find_idx = -1;
	ASSERT_TRUE(find_drawer_entry("Find", &find_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(find_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerToggleSelectionExpanded(E.window_rows));

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Find File") == NULL);
	ASSERT_TRUE(strstr(output, "Save") != NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_renders_unicode_tree_connectors(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char zzz_dir[512];
	char alloc_file[512];
	char rotide_file[512];
	char helpers_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(zzz_dir, sizeof(zzz_dir), env.project_dir, "zzz"));
	ASSERT_TRUE(path_join(alloc_file, sizeof(alloc_file), src_dir, "alloc_test_hooks.c"));
	ASSERT_TRUE(path_join(rotide_file, sizeof(rotide_file), src_dir, "rotide_tests.c"));
	ASSERT_TRUE(path_join(helpers_file, sizeof(helpers_file), src_dir, "test_helpers.c"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(make_dir(zzz_dir));
	ASSERT_TRUE(write_text_file(alloc_file, "a\n"));
	ASSERT_TRUE(write_text_file(rotide_file, "b\n"));
	ASSERT_TRUE(write_text_file(helpers_file, "c\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	E.window_rows = 8;
	E.window_cols = 80;
	(void)editorDrawerSetWidthForCols(40, E.window_cols);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xBE \x1b[1m\x1b[36msrc") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xB8 \x1b[1m\x1b[36mzzz") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x9C src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94 src") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x9C\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x94\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x80\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "alloc_test_hooks.c") != NULL);
	ASSERT_TRUE(strstr(output, "rotide_tests.c") != NULL);
	ASSERT_TRUE(strstr(output, "test_helpers.c") != NULL);
	free(output);

	ASSERT_TRUE(unlink(alloc_file) == 0);
	ASSERT_TRUE(unlink(rotide_file) == 0);
	ASSERT_TRUE(unlink(helpers_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	ASSERT_TRUE(rmdir(zzz_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_selected_overflow_spills_into_text_area(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char long_dir[512];
	const char *dirname = "drawer_item_with_overflow_tail_segment";
	ASSERT_TRUE(path_join(long_dir, sizeof(long_dir), env.project_dir, dirname));
	ASSERT_TRUE(make_dir(long_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	int long_idx = -1;
	ASSERT_TRUE(find_drawer_entry(dirname, &long_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(long_idx, E.window_rows + 1));

	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	E.window_rows = 4;
	E.window_cols = 60;
	ASSERT_TRUE(editorDrawerSetWidthForCols(12, E.window_cols));
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "overflow_tail_segment") != NULL);

	int highlighted_tail = 0;
	const char *scan = output;
	while ((scan = strstr(scan, "\x1b[7m")) != NULL) {
		const char *normal = strstr(scan + 4, "\x1b[m");
		if (normal == NULL) {
			break;
		}
		const char *tail = strstr(scan, "overflow_tail_segment");
		if (tail != NULL && tail < normal) {
			highlighted_tail = 1;
			break;
		}
		scan = normal + 3;
	}
	ASSERT_TRUE(highlighted_tail);

	free(output);
	ASSERT_TRUE(rmdir(long_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_splitter_spans_editor_rows(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	E.window_rows = 4;
	E.window_cols = 40;
	E.drawer_width_cols = 10;
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);

	const char *separator = "\xE2\x94\x82";
	size_t separator_len = strlen(separator);
	int separator_count = 0;
	const char *cursor = output;
	while ((cursor = strstr(cursor, separator)) != NULL) {
		separator_count++;
		cursor += separator_len;
	}
	ASSERT_EQ_INT(E.window_rows + 1, separator_count);

	free(output);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_cursor_column_offsets_for_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	E.rowoff = 0;
	E.coloff = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	int expected_col = editorTextBodyStartColForCols(E.window_cols) + 2;
	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[2;%dH", expected_col) >
	            0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_hides_cursor_when_drawer_focused(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	E.rowoff = 0;
	E.coloff = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") == NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_file_search_header_shows_cursor(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char beta_file[512];
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorFileSearchEnter());
	ASSERT_TRUE(editorFileSearchAppendByte('b'));
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	E.window_rows = 3;
	E.window_cols = 40;
	add_row("body");

	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[2;13H") > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "File name: b") != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	free(output);

	ASSERT_TRUE(unlink(beta_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_project_search_header_shows_cursor(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char beta_file[512];
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(beta_file, "beta needle\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorProjectSearchEnter());
	ASSERT_TRUE(editorProjectSearchAppendByte('n'));
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	E.window_rows = 3;
	E.window_cols = 40;
	add_row("body");

	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[2;9H") > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Query: n") != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	free(output);

	ASSERT_TRUE(unlink(beta_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_layout_clamps_tiny_widths(void) {
	E.line_numbers_enabled = 0;
	ASSERT_EQ_INT(0, editorDrawerWidthForCols(1));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(1));
	ASSERT_EQ_INT(0, editorTextBodyStartColForCols(1));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(1));
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(2));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(2));
	ASSERT_EQ_INT(1, editorTextBodyStartColForCols(2));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(2));
	ASSERT_EQ_INT(0, editorDrawerSeparatorWidthForCols(2));

	E.drawer_width_cols = 24;
	E.drawer_width_user_set = 0;
	ASSERT_EQ_INT(4, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerSeparatorWidthForCols(10));
	ASSERT_EQ_INT(5, editorDrawerTextViewportCols(10));
	ASSERT_EQ_INT(6, editorTextBodyStartColForCols(10));
	ASSERT_EQ_INT(3, editorTextBodyViewportCols(10));

	ASSERT_TRUE(editorDrawerSetWidthForCols(24, 10));
	ASSERT_EQ_INT(8, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(10));
	ASSERT_EQ_INT(9, editorTextBodyStartColForCols(10));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(10));

	ASSERT_TRUE(editorDrawerSetWidthForCols(3, 10));
	ASSERT_EQ_INT(3, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(6, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerResizeByDeltaForCols(-10, 10));
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(8, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerResizeByDeltaForCols(50, 10));
	ASSERT_EQ_INT(8, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerSetCollapsed(1));
	ASSERT_EQ_INT(3, editorDrawerCollapsedToggleWidthForCols(10));
	ASSERT_EQ_INT(3, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(0, editorDrawerSeparatorWidthForCols(10));
	ASSERT_EQ_INT(3, editorDrawerTextStartColForCols(10));
	ASSERT_EQ_INT(7, editorDrawerTextViewportCols(10));
	return 0;
}

static int test_editor_refresh_screen_status_bar_single_row_percent(void) {
	add_row("single");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("single.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_percent_uses_viewport_top(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 3;
	E.window_cols = 50;
	E.cy = 9;
	E.cx = 0;
	E.rowoff = 0;
	editorViewportSetMode(EDITOR_VIEWPORT_FREE_SCROLL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "10,1    0%") != NULL);
	free(output);

	E.rowoff = 4;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "10,1    57%") != NULL);
	free(output);

	E.rowoff = 7;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "10,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_wrapped_percent_uses_visual_top(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 2;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	editorViewportSetMode(EDITOR_VIEWPORT_FREE_SCROLL);
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,1    0%") != NULL);
	free(output);

	E.wrapoff = 1;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_cursor_multibyte_col(void) {
	add_row("\xC3\xB6"
	        "a");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 2;
	E.filename = strdup("multi.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,2    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_cursor_tab_display_col(void) {
	add_row("a\tb");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 2;
	E.filename = strdup("tabs.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,9    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_shows_full_path_when_space_allows(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 110;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("/project/src/modules/editor/very_long_filename.c");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "/project/src/modules/editor/very_long_filename.c") != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 45;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("/very/long/prefix/that/keeps/growing/path/target_file_name.c");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "target_file_name.c") != NULL);
	ASSERT_TRUE(strstr(output, "...") != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_input_segment(void) {
	add_row("line");
	ASSERT_TRUE(editorInputSystemActivate("vim"));
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	E.dirty = 0;
	E.filename = strdup("input.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* The mode block renders plain (no "--") and to the left of the file path. */
	const char *mode = strstr(output, "NORMAL");
	const char *name = strstr(output, "input.txt");
	ASSERT_TRUE(mode != NULL);
	ASSERT_TRUE(name != NULL);
	ASSERT_TRUE(mode < name);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	ASSERT_TRUE(strstr(output, "--") == NULL);
	free(output);
	ASSERT_TRUE(editorInputSystemActivate("cua"));
	return 0;
}

static int test_editor_refresh_screen_status_bar_input_segment_truncates(void) {
	add_row("line");
	ASSERT_TRUE(editorInputSystemActivate("vim"));
	E.window_rows = 3;
	E.window_cols = 14;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("input.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* Too narrow for the mode block: it is dropped entirely, never half-shown. */
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	ASSERT_TRUE(strstr(output, "NORMAL") == NULL);
	free(output);
	ASSERT_TRUE(editorInputSystemActivate("cua"));
	return 0;
}

/*
 * Strips CSI escape sequences (ESC '[' … final-byte) from `src` in place,
 * returning the number of on-screen display columns of the remaining visible
 * text. Used to assert the status bar fills its row edge to edge regardless of
 * how many color/style escapes it emits.
 */
static int status_row_visible_cols(const char *src) {
	char *plain = strdup(src);
	if (plain == NULL) {
		return -1;
	}
	size_t w = 0;
	for (size_t r = 0; src[r] != '\0';) {
		if (src[r] == '\x1b' && src[r + 1] == '[') {
			r += 2;
			while (src[r] != '\0' && (src[r] < 0x40 || src[r] > 0x7E)) {
				r++;
			}
			if (src[r] != '\0') {
				r++;
			}
			continue;
		}
		plain[w++] = src[r++];
	}
	plain[w] = '\0';
	int cols = editorDisplayTextCols(plain);
	free(plain);
	return cols;
}

/*
 * The ahead/behind markers use multi-byte ↑/↓ glyphs, so the right segment's
 * byte length overstates its width. The status background must still reach the
 * right edge: the rendered row must be exactly window_cols display columns.
 */
static int test_editor_refresh_screen_status_bar_fills_width_with_ahead_behind(void) {
	add_row("body");
	E.window_rows = 3;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("file.c");
	ASSERT_TRUE(E.filename != NULL);
	E.git_branch = strdup("main");
	ASSERT_TRUE(E.git_branch != NULL);
	E.git_entry_count = 3; /* uncommitted changes -> "+" dirty marker */
	E.git_ahead = 2;
	E.git_behind = 1;

	struct writeBuf wb = WRITEBUF_INIT;
	ASSERT_TRUE(editorDrawStatusBar(&wb, 100));
	ASSERT_TRUE(wb.b != NULL);
	/* editorDrawStatusBar ends the row with a trailing CRLF; drop it. */
	ASSERT_TRUE(wb.len >= 2);
	wb.b[wb.len - 2] = '\0';
	ASSERT_TRUE(strstr(wb.b, "\xE2\x86\x91" "2") != NULL); /* ↑2 present */
	ASSERT_TRUE(strstr(wb.b, "\xE2\x86\x93" "1") != NULL); /* ↓1 present */
	ASSERT_EQ_INT(E.window_cols, status_row_visible_cols(wb.b));
	wbFree(&wb);
	return 0;
}

static int test_editor_refresh_screen_tab_labels_middle_truncate_at_25_cols(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/aaaaaaaaaaabbbbbbbbbbbccccccccccc");
	ASSERT_TRUE(E.filename != NULL);
	E.window_rows = 3;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "aaaaaaaaaaa...ccccccccccc") != NULL);
	free(output);
	return 0;
}

const struct editorTestCase g_render_chrome_tests[] = {
        {"editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels",
         test_editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels},
        {"editor_refresh_screen_preview_tab_label_uses_italics",
         test_editor_refresh_screen_preview_tab_label_uses_italics},
        {"editor_tab_layout_width_includes_right_label_padding",
         test_editor_tab_layout_width_includes_right_label_padding},
        {"editor_tabs_align_view_keeps_active_visible_with_variable_widths",
         test_editor_tabs_align_view_keeps_active_visible_with_variable_widths},
        {"editor_draw_pane_tab_strip_uses_pane_membership_and_active_tab",
         test_editor_draw_pane_tab_strip_uses_pane_membership_and_active_tab},
        {"editor_refresh_screen_renders_drawer_entries_and_selection",
         test_editor_refresh_screen_renders_drawer_entries_and_selection},
        {"editor_refresh_screen_drawer_colors_files_by_git_status",
         test_editor_refresh_screen_drawer_colors_files_by_git_status},
        {"editor_refresh_screen_drawer_renders_directories_bold_and_cyan",
         test_editor_refresh_screen_drawer_renders_directories_bold_and_cyan},
        {"editor_refresh_screen_drawer_hides_selection_marker_when_unfocused",
         test_editor_refresh_screen_drawer_hides_selection_marker_when_unfocused},
        {"editor_refresh_screen_drawer_active_file_uses_inverted_background",
         test_editor_refresh_screen_drawer_active_file_uses_inverted_background},
        {"editor_refresh_screen_drawer_collapsed_renders_expand_indicator",
         test_editor_refresh_screen_drawer_collapsed_renders_expand_indicator},
        {"editor_refresh_screen_multi_pane_collapsed_reserves_toggle_area",
         test_editor_refresh_screen_multi_pane_collapsed_reserves_toggle_area},
        {"editor_refresh_screen_drawer_header_mode_buttons",
         test_editor_refresh_screen_drawer_header_mode_buttons},
        {"editor_refresh_screen_drawer_header_background_fills_wide_row",
         test_editor_refresh_screen_drawer_header_background_fills_wide_row},
        {"editor_refresh_screen_drawer_uses_nerd_font_icons_when_enabled",
         test_editor_refresh_screen_drawer_uses_nerd_font_icons_when_enabled},
        {"editor_refresh_screen_main_menu_drawer_groups_actions",
         test_editor_refresh_screen_main_menu_drawer_groups_actions},
        {"editor_refresh_screen_drawer_renders_unicode_tree_connectors",
         test_editor_refresh_screen_drawer_renders_unicode_tree_connectors},
        {"editor_refresh_screen_drawer_selected_overflow_spills_into_text_area",
         test_editor_refresh_screen_drawer_selected_overflow_spills_into_text_area},
        {"editor_refresh_screen_drawer_splitter_spans_editor_rows",
         test_editor_refresh_screen_drawer_splitter_spans_editor_rows},
        {"editor_refresh_screen_cursor_column_offsets_for_drawer",
         test_editor_refresh_screen_cursor_column_offsets_for_drawer},
        {"editor_refresh_screen_hides_cursor_when_drawer_focused",
         test_editor_refresh_screen_hides_cursor_when_drawer_focused},
        {"editor_refresh_screen_file_search_header_shows_cursor",
         test_editor_refresh_screen_file_search_header_shows_cursor},
        {"editor_refresh_screen_project_search_header_shows_cursor",
         test_editor_refresh_screen_project_search_header_shows_cursor},
        {"editor_drawer_layout_clamps_tiny_widths", test_editor_drawer_layout_clamps_tiny_widths},
        {"editor_refresh_screen_status_bar_single_row_percent",
         test_editor_refresh_screen_status_bar_single_row_percent},
        {"editor_refresh_screen_status_bar_percent_uses_viewport_top",
         test_editor_refresh_screen_status_bar_percent_uses_viewport_top},
        {"editor_refresh_screen_status_bar_wrapped_percent_uses_visual_top",
         test_editor_refresh_screen_status_bar_wrapped_percent_uses_visual_top},
        {"editor_refresh_screen_status_bar_cursor_multibyte_col",
         test_editor_refresh_screen_status_bar_cursor_multibyte_col},
        {"editor_refresh_screen_status_bar_cursor_tab_display_col",
         test_editor_refresh_screen_status_bar_cursor_tab_display_col},
        {"editor_refresh_screen_status_bar_shows_full_path_when_space_allows",
         test_editor_refresh_screen_status_bar_shows_full_path_when_space_allows},
        {"editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible",
         test_editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible},
        {"editor_refresh_screen_status_bar_input_segment",
         test_editor_refresh_screen_status_bar_input_segment},
        {"editor_refresh_screen_status_bar_input_segment_truncates",
         test_editor_refresh_screen_status_bar_input_segment_truncates},
        {"editor_refresh_screen_status_bar_fills_width_with_ahead_behind",
         test_editor_refresh_screen_status_bar_fills_width_with_ahead_behind},
        {"editor_refresh_screen_tab_labels_middle_truncate_at_25_cols",
         test_editor_refresh_screen_tab_labels_middle_truncate_at_25_cols},
};

const int g_render_chrome_test_count =
        (int)(sizeof(g_render_chrome_tests) / sizeof(g_render_chrome_tests[0]));
