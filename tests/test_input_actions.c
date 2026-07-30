#include "editing/buffer_core.h"
#include "editor_test_api.h"
#include "input/actions_workspace.h"
#include "input/dispatch.h"
#include "input/system_vim.h"
#include "input/text_pairs.h"
#include "language/syntax.h"
#include "render/popup.h"
#include "rotide.h"
#include "support/terminal.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"
#include "workspace/task.h"

#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int input_actions_dispatch(enum editorAction action) {
	int effects = 0;
	return editorDispatchProcessMappedAction(action, &effects);
}

static int input_actions_enter_insert(void) {
	return editor_process_single_key('i');
}

static int input_actions_vim_key(int key) {
	int effects = 0;
	return editorVimHandleKey(key, &effects);
}

static int input_actions_seed_git_blame_cache(int one_based_line, const char *author,
                                              time_t author_time) {
	editorGitBlameCacheClear(&E.active_buffer);
	E.git_blame_line = malloc(sizeof(*E.git_blame_line));
	ASSERT_TRUE(E.git_blame_line != NULL);
	memset(E.git_blame_line, 0, sizeof(*E.git_blame_line));
	E.git_blame_line->commit_sha = strdup("abcdef1234567890abcdef1234567890abcdef12");
	E.git_blame_line->short_sha = strdup("abcdef123456");
	E.git_blame_line->author_name = strdup(author);
	E.git_blame_line->author_email = strdup("alice@example.com");
	E.git_blame_line->author_time = author_time;
	E.git_blame_line->committer_name = strdup("Carol");
	E.git_blame_line->committer_time = author_time + 60;
	E.git_blame_line->summary = strdup("Action blame");
	E.git_blame_line->filename = strdup("tracked.txt");
	E.git_blame_line->original_path = strdup("old/tracked.txt");
	E.git_blame_line->original_line = 3;
	E.git_blame_line->final_line = one_based_line;
	ASSERT_TRUE(E.git_blame_line->commit_sha != NULL);
	ASSERT_TRUE(E.git_blame_line->short_sha != NULL);
	ASSERT_TRUE(E.git_blame_line->author_name != NULL);
	ASSERT_TRUE(E.git_blame_line->author_email != NULL);
	ASSERT_TRUE(E.git_blame_line->committer_name != NULL);
	ASSERT_TRUE(E.git_blame_line->summary != NULL);
	ASSERT_TRUE(E.git_blame_line->filename != NULL);
	ASSERT_TRUE(E.git_blame_line->original_path != NULL);
	E.git_blame_line_number = one_based_line;
	E.git_blame_line_miss = 0;
	E.git_blame_filename = strdup(E.filename);
	E.git_blame_repo_root = strdup(E.git_repo_root);
	E.git_blame_branch = E.git_branch != NULL ? strdup(E.git_branch) : NULL;
	E.git_blame_head = E.git_head != NULL ? strdup(E.git_head) : NULL;
	E.git_blame_disk_state = E.disk_state;
	ASSERT_TRUE(E.git_blame_filename != NULL);
	ASSERT_TRUE(E.git_blame_repo_root != NULL);
	if (E.git_branch != NULL) {
		ASSERT_TRUE(E.git_blame_branch != NULL);
	}
	if (E.git_head != NULL) {
		ASSERT_TRUE(E.git_blame_head != NULL);
	}
	return 0;
}

static int test_editor_process_keypress_alt_b_git_blame_details_popup_behaviors(void) {
	ASSERT_TRUE(editorTabsInit());
	editorVimReset();
	add_row("alpha");
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;
	E.rx = 0;
	E.dirty = 0;
	E.filename = strdup("/tmp/rotide-blame/tracked.txt");
	E.git_repo_root = strdup("/tmp/rotide-blame");
	E.git_branch = strdup("main");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(E.git_repo_root != NULL);
	ASSERT_TRUE(E.git_branch != NULL);
	ASSERT_TRUE(input_actions_seed_git_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);
	ASSERT_TRUE(editorGitBlameActiveLine(1) != NULL);
	ASSERT_TRUE(editorDispatchOpenGitBlameDetailsAt(0, 0, 1));
	ASSERT_EQ_STR("Git blame", E.statusmsg);
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_GIT_BLAME, E.popup.kind);
	ASSERT_EQ_INT(0, E.popup.anchor_row);
	ASSERT_EQ_INT(0, E.popup.anchor_col);
	ASSERT_TRUE(E.popup.item_count >= 7);
	ASSERT_TRUE(strstr(E.popup.items[0].label, "commit abcdef") != NULL);
	ASSERT_TRUE(strstr(E.popup.items[1].label, "Action blame") != NULL);

	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_DOWN));
	ASSERT_TRUE(E.popup.row_offset > 0);
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(PAGE_DOWN));
	ASSERT_TRUE(E.popup.row_offset > 0);
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey('\x1b'));
	ASSERT_TRUE(!editorPopupIsVisible());

	ASSERT_TRUE(editorDispatchOpenGitBlameDetailsAt(0, 0, 1));
	ASSERT_TRUE(editorPopupIsVisible());
	char insert[] = {'i'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert, sizeof(insert)) == 0);
	char x[] = {'x'};
	ASSERT_TRUE(editor_process_keypress_with_input(x, sizeof(x)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_ROW_TEXT_EQ(0, "xalpha");
	ASSERT_EQ_INT(1, E.dirty);

	editorGitFree();
	return 0;
}

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
	ASSERT_TRUE(editorTaskStart("Task: Mixed", "printf 'out\\n'; printf 'err\\n' 1>&2; sleep 1",
	                            NULL, NULL));
	ASSERT_TRUE(editorTaskIsRunning());

	(void)input_actions_dispatch(EDITOR_ACTION_SAFE_CLOSE_TAB);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(0, E.close_confirmed);
	ASSERT_TRUE(strstr(E.statusmsg, "Press Alt-D again") != NULL);

	(void)input_actions_dispatch(EDITOR_ACTION_SAFE_CLOSE_TAB);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(editorTaskStart("Task: Mixed Output", "printf 'out\\n'; printf 'err\\n' 1>&2",
	                            NULL, NULL));
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
	ASSERT_TRUE(editorTaskStart("Task: Large Output", "yes 1234567890 | head -c 150000", NULL,
	                            NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(3000));

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(textlen <= ROTIDE_TASK_LOG_MAX_BYTES + 256);
	ASSERT_TRUE(strstr(text, "[output truncated]") != NULL);
	free(text);
	return 0;
}

static int setup_move_tab_split(enum editorSplitOrientation orientation,
                                struct editorPaneNode **first_out,
                                struct editorPaneNode **second_out) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(4, editorTabCount());
	E.window_rows = 10;
	E.window_cols = 80;

	struct editorPaneNode *first = E.focused_leaf;
	ASSERT_TRUE(first != NULL);
	struct editorPaneNode *second = editorLayoutSplitFocused(orientation, 0.5);
	ASSERT_TRUE(second != NULL);

	first->as.leaf.view.pane_tab_count = 0;
	first->as.leaf.view.active_tab_idx = -1;
	second->as.leaf.view.pane_tab_count = 0;
	second->as.leaf.view.active_tab_idx = -1;
	ASSERT_TRUE(editorPaneViewAddTab(&first->as.leaf.view, 0));
	ASSERT_TRUE(editorPaneViewAddTab(&first->as.leaf.view, 1));
	first->as.leaf.view.active_tab_idx = 1;
	ASSERT_TRUE(editorPaneViewAddTab(&second->as.leaf.view, 2));
	ASSERT_TRUE(editorPaneViewAddTab(&second->as.leaf.view, 3));
	second->as.leaf.view.active_tab_idx = 2;

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(first));
	ASSERT_TRUE(editorTabSwitchToIndex(1));
	*first_out = first;
	*second_out = second;
	return 0;
}

static int test_editor_action_move_active_tab_right_pane_moves_and_focuses_right(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);

	ASSERT_TRUE(editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_RIGHT));

	ASSERT_TRUE(E.focused_leaf == right);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, left->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, left->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(0, left->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(3, right->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, right->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, right->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(1, right->as.leaf.view.pane_tabs[2]);
	ASSERT_EQ_INT(1, right->as.leaf.view.active_tab_idx);
	ASSERT_TRUE(strstr(E.statusmsg, "Pane ") != NULL);
	return 0;
}

static int test_editor_action_move_active_tab_left_pane_moves_and_focuses_left(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(right));
	ASSERT_TRUE(editorTabSwitchToIndex(2));

	ASSERT_TRUE(editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_LEFT));

	ASSERT_TRUE(E.focused_leaf == left);
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	ASSERT_EQ_INT(3, left->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, left->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(1, left->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(2, left->as.leaf.view.pane_tabs[2]);
	ASSERT_EQ_INT(2, left->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(1, right->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(3, right->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, right->as.leaf.view.active_tab_idx);
	return 0;
}

static int test_editor_action_move_active_tab_down_pane_moves_and_focuses_down(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);

	ASSERT_TRUE(editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_DOWN));

	ASSERT_TRUE(E.focused_leaf == bottom);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, bottom->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(1, bottom->as.leaf.view.pane_tabs[2]);
	ASSERT_EQ_INT(1, bottom->as.leaf.view.active_tab_idx);
	return 0;
}

static int test_editor_action_move_active_tab_up_pane_moves_and_focuses_up(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(bottom));
	ASSERT_TRUE(editorTabSwitchToIndex(2));

	ASSERT_TRUE(editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_UP));

	ASSERT_TRUE(E.focused_leaf == top);
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	ASSERT_EQ_INT(3, top->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(2, top->as.leaf.view.pane_tabs[2]);
	ASSERT_EQ_INT(1, bottom->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.active_tab_idx);
	return 0;
}

static int test_editor_action_move_active_tab_no_neighbor_is_no_op(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);

	ASSERT_TRUE(!editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_LEFT));

	ASSERT_TRUE(E.focused_leaf == left);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(2, left->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, left->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(1, left->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(2, right->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, right->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, right->as.leaf.view.pane_tabs[1]);
	return 0;
}

static int test_editor_action_move_last_tab_replaces_source_with_empty_buffer(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);
	left->as.leaf.view.pane_tab_count = 0;
	left->as.leaf.view.active_tab_idx = -1;
	ASSERT_TRUE(editorPaneViewAddTab(&left->as.leaf.view, 0));
	left->as.leaf.view.active_tab_idx = 0;
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	int tab_count_before = editorTabCount();

	ASSERT_TRUE(editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_RIGHT));

	ASSERT_TRUE(E.focused_leaf == right);
	ASSERT_EQ_INT(tab_count_before + 1, editorTabCount());
	ASSERT_EQ_INT(1, left->as.leaf.view.pane_tab_count);
	ASSERT_TRUE(left->as.leaf.view.active_tab_idx >= 0);
	ASSERT_TRUE(left->as.leaf.view.active_tab_idx != 0);
	ASSERT_TRUE(left->as.leaf.view.pane_tabs[0] == left->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(3, right->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, right->as.leaf.view.active_tab_idx);
	return 0;
}

static int test_editor_action_move_active_tab_empty_pane_is_no_op(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);
	left->as.leaf.view.pane_tab_count = 0;
	left->as.leaf.view.active_tab_idx = -1;

	ASSERT_TRUE(!editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_RIGHT));

	ASSERT_TRUE(E.focused_leaf == left);
	ASSERT_EQ_INT(0, left->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, right->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, right->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, right->as.leaf.view.pane_tabs[1]);
	return 0;
}

static int test_editor_tabs_ensure_pane_occupancy_backfills_empty_leaves(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_move_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);
	left->as.leaf.view.pane_tab_count = 0;
	left->as.leaf.view.active_tab_idx = -1;
	int tab_count_before = editorTabCount();

	editorTabsEnsurePaneOccupancy();

	ASSERT_EQ_INT(tab_count_before + 1, editorTabCount());
	ASSERT_EQ_INT(1, left->as.leaf.view.pane_tab_count);
	ASSERT_TRUE(left->as.leaf.view.active_tab_idx >= 0);
	ASSERT_EQ_INT(left->as.leaf.view.pane_tabs[0], left->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(2, right->as.leaf.view.pane_tab_count);
	return 0;
}

static int test_editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	(void)input_actions_dispatch(EDITOR_ACTION_TOGGLE_DRAWER);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_EQ_STR("Drawer collapsed", E.statusmsg);

	(void)input_actions_dispatch(EDITOR_ACTION_TOGGLE_DRAWER);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	ASSERT_TRUE(editorDrawerSetCollapsed(1));
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	(void)input_actions_dispatch(EDITOR_ACTION_FOCUS_DRAWER);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);
	(void)input_actions_dispatch(EDITOR_ACTION_FOCUS_DRAWER);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_search_escape_restores_previous_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	add_row("body");

	/* Open the Main Menu tab, then drop into file search on top of it. */
	ASSERT_EQ_INT(0, editorSwitchDrawerHeaderMode(EDITOR_DRAWER_MODE_MAIN_MENU));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);

	editorOpenFileSearchDrawer();
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_TRUE(editorHandleDrawerSearchMappedAction(EDITOR_ACTION_ESCAPE, NULL, NULL));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_TRUE(!editorDrawerIsCollapsed());

	/* Project search restores the Main Menu tab the same way. */
	editorOpenProjectSearchDrawer();
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_TRUE(editorHandleDrawerSearchMappedAction(EDITOR_ACTION_ESCAPE, NULL, NULL));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_TRUE(!editorDrawerIsCollapsed());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_search_escape_recollapses_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	add_row("body");

	ASSERT_TRUE(editorDrawerSetCollapsed(1));
	ASSERT_TRUE(editorDrawerIsCollapsed());

	/* File search force-expands; ESC re-collapses and falls back to the tree. */
	editorOpenFileSearchDrawer();
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_TRUE(editorHandleDrawerSearchMappedAction(EDITOR_ACTION_ESCAPE, NULL, NULL));
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);

	editorOpenProjectSearchDrawer();
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_TRUE(editorHandleDrawerSearchMappedAction(EDITOR_ACTION_ESCAPE, NULL, NULL));
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_search_escape_carries_mode_across_switch(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	add_row("body");

	ASSERT_EQ_INT(0, editorSwitchDrawerHeaderMode(EDITOR_DRAWER_MODE_MAIN_MENU));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);

	/* Switching file -> project search must not lose the original tab. */
	editorOpenFileSearchDrawer();
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	editorOpenProjectSearchDrawer();
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);

	ASSERT_TRUE(editorHandleDrawerSearchMappedAction(EDITOR_ACTION_ESCAPE, NULL, NULL));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_TRUE(!editorDrawerIsCollapsed());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_search_escape_keeps_tree_expanded(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	add_row("body");

	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_TRUE(!editorDrawerIsCollapsed());

	editorOpenFileSearchDrawer();
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_TRUE(editorHandleDrawerSearchMappedAction(EDITOR_ACTION_ESCAPE, NULL, NULL));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_TRUE(!editorDrawerIsCollapsed());

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

	const char menu[] = {' ', 'm'};
	ASSERT_TRUE(editor_process_keypress_with_input(menu, sizeof(menu)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_EQ_INT(-1, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_STR("Main menu opened", E.statusmsg);

	int find_file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("Find File", &find_file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(find_file_idx, E.window_rows));

	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter, sizeof(enter)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	editorFileSearchExit(1);
	ASSERT_TRUE(unlink(match_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_main_menu_project_files_opens_tree(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	const char menu[] = {' ', 'm'};
	ASSERT_TRUE(editor_process_keypress_with_input(menu, sizeof(menu)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);

	int project_files_idx = -1;
	ASSERT_TRUE(find_drawer_entry("Project Files", &project_files_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(project_files_idx, E.window_rows));

	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter, sizeof(enter)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	return 0;
}

static int test_editor_process_keypress_context_menu_runs_split_action(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	E.window_rows = 6;
	E.window_cols = 50;
	E.cy = 0;
	E.cx = 2;

	(void)input_actions_dispatch(EDITOR_ACTION_CONTEXT_MENU);
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_EDITOR_CONTEXT_MENU, E.popup.kind);
	ASSERT_EQ_STR("Split Vertically", E.popup.items[0].label);

	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter, sizeof(enter)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_TRUE(E.layout_root != NULL);
	ASSERT_TRUE(E.layout_root->is_split);
	ASSERT_EQ_INT(EDITOR_SPLIT_VERTICAL, E.layout_root->as.split.orientation);
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
	ASSERT_ROW_TEXT_EQ(0, "tab-zero");
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("zero", E.search_query);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "tab-one");
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("one", E.search_query);
	return 0;
}

static int test_editor_tab_close_uses_pane_activation_history_repeatedly(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("tab-zero");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("tab-one");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("tab-two");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("tab-three");

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorTabSwitchToIndex(3));
	ASSERT_TRUE(editorTabSwitchToIndex(2));

	ASSERT_TRUE(editorTabCloseActive());
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "tab-three");

	ASSERT_TRUE(editorTabCloseActive());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "tab-one");
	return 0;
}

static int test_editor_tab_close_mru_survives_global_index_shift(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("tab-zero");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("tab-one");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("tab-two");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("tab-three");

	ASSERT_TRUE(editorTabSwitchToIndex(3));
	ASSERT_TRUE(editorTabSwitchToIndex(1));

	ASSERT_TRUE(editorTabCloseActive());
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "tab-three");
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

static int test_editor_tab_close_last_in_pane_closes_pane_when_other_panes_exist(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/left.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("left content");
	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/right.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("right content");

	/* Split the layout: each pane owns exactly one of the two tabs. */
	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);
	left->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&left->as.leaf.view, 0));
	left->as.leaf.view.active_tab_idx = 0;
	right->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&right->as.leaf.view, 1));
	right->as.leaf.view.active_tab_idx = 1;

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_TRUE(editorTabCloseActive());

	/* Layout collapsed to a single pane (right), which is now focused and
	 * still shows its own tab. The closed tab was globally removed. */
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(E.focused_leaf != NULL && !E.focused_leaf->is_split);
	ASSERT_EQ_INT(1, E.focused_leaf->as.leaf.view.pane_tab_count);
	int local_tab = E.focused_leaf->as.leaf.view.pane_tabs[0];
	ASSERT_EQ_INT(local_tab, editorTabActiveIndex());
	ASSERT_TRUE(editorTabFilenameAt(local_tab) != NULL);
	ASSERT_EQ_STR("/tmp/right.txt", editorTabFilenameAt(local_tab));
	return 0;
}

static int test_editor_tab_close_last_in_single_pane_keeps_empty_buffer(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/only.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("only content");
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_TRUE(editorTabCloseActive());

	/* Single-pane layout cannot close the only pane; falls back to a fresh
	 * empty buffer in place. */
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_TRUE(E.filename == NULL);
	return 0;
}

static int test_editor_process_keypress_ctrl_w_dirty_requires_second_press(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	(void)input_actions_dispatch(EDITOR_ACTION_CLOSE_TAB);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);

	(void)input_actions_dispatch(EDITOR_ACTION_CLOSE_TAB);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_editor_process_keypress_close_tab_confirmation_resets_on_other_action(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	(void)input_actions_dispatch(EDITOR_ACTION_CLOSE_TAB);
	ASSERT_EQ_INT(1, E.numrows);

	const char move_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(move_right, sizeof(move_right) - 1) == 0);

	(void)input_actions_dispatch(EDITOR_ACTION_CLOSE_TAB);
	ASSERT_EQ_INT(1, E.numrows);

	(void)input_actions_dispatch(EDITOR_ACTION_CLOSE_TAB);
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

	(void)input_actions_dispatch(EDITOR_ACTION_QUIT);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);
	return 0;
}

static int test_editor_process_keypress_tab_actions_new_next_prev(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("left");

	(void)input_actions_dispatch(EDITOR_ACTION_NEW_TAB);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	add_row("right");
	const char alt_left[] = "\x1b[1;3D";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_left, sizeof(alt_left) - 1) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());

	const char alt_j[] = "\x1bj";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_j, sizeof(alt_j) - 1) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "left");

	const char alt_right_fallback[] = "\x1b\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_right_fallback,
	                                               sizeof(alt_right_fallback) - 1) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());

	const char alt_k[] = "\x1bk";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_k, sizeof(alt_k) - 1) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "right");
	return 0;
}

static int test_editor_safe_close_follows_focused_pane_mru(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("zero");
	E.dirty = 0;
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("one");
	E.dirty = 0;
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("two");
	E.dirty = 0;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorTabSwitchToIndex(1));
	const char alt_d[] = "\x1b"
	                     "d";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_d, sizeof(alt_d) - 1) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_ROW_TEXT_EQ(0, "zero");

	ASSERT_TRUE(editor_process_keypress_with_input(alt_d, sizeof(alt_d) - 1) == 0);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_ROW_TEXT_EQ(0, "two");
	return 0;
}

static int test_editor_new_tab_inserts_beside_active(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabSwitchToIndex(0));

	(void)input_actions_dispatch(EDITOR_ACTION_NEW_TAB);
	int inserted = editorTabActiveIndex();
	ASSERT_EQ_INT(3, inserted);
	ASSERT_EQ_INT(4, editorTabCount());

	(void)input_actions_dispatch(EDITOR_ACTION_PREV_TAB);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	(void)input_actions_dispatch(EDITOR_ACTION_NEXT_TAB);
	ASSERT_EQ_INT(inserted, editorTabActiveIndex());
	(void)input_actions_dispatch(EDITOR_ACTION_NEXT_TAB);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	return 0;
}

static int test_editor_alt_first_last_stay_in_focused_pane(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	struct editorPaneNode *second = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(second != NULL);
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(3, editorTabActiveIndex());

	const char alt_h[] = "\x1bh";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_h, sizeof(alt_h) - 1) == 0);
	ASSERT_EQ_INT(2, editorTabActiveIndex());

	const char alt_l[] = "\x1bl";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_l, sizeof(alt_l) - 1) == 0);
	ASSERT_EQ_INT(3, editorTabActiveIndex());
	return 0;
}

static int test_editor_process_keypress_alt_t_creates_terminal_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	const char alt_t[] = "\x1bt";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_t, sizeof(alt_t) - 1) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(EDITOR_PANE_KIND_TERMINAL, editorTabActiveKind());
	return 0;
}

static int test_editor_safe_close_dirty_requires_consecutive_second_press(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	const char alt_d[] = "\x1b"
	                     "d";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_d, sizeof(alt_d) - 1) == 0);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_INT(0, E.close_confirmed);
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "Press Alt-D again") != NULL);

	const char move_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(move_right, sizeof(move_right) - 1) == 0);

	ASSERT_TRUE(editor_process_keypress_with_input(alt_d, sizeof(alt_d) - 1) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_TRUE(strstr(E.statusmsg, "Press Alt-D again") != NULL);

	ASSERT_TRUE(editor_process_keypress_with_input(alt_d, sizeof(alt_d) - 1) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_INT(0, E.close_confirmed);
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
	ASSERT_ROW_TEXT_EQ(0, "opened");

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
	ASSERT_ROW_TEXT_EQ(0, "opened");

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "keep");

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

	(void)input_actions_dispatch(EDITOR_ACTION_FOCUS_DRAWER);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	(void)input_actions_dispatch(EDITOR_ACTION_FOCUS_DRAWER);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	(void)input_actions_dispatch(EDITOR_ACTION_FOCUS_DRAWER);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

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
	ASSERT_TRUE(editor_process_keypress_with_input_silent(esc_input, sizeof(esc_input) - 1) ==
	            0);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_drawer_modes_arrow_navigation_keeps_text_cursor(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("one");
	add_row("two");
	E.window_rows = 8;
	E.window_cols = 80;
	E.cy = 1;
	E.cx = 2;
	int initial_cy = E.cy;
	int initial_cx = E.cx;
	const char arrow_down[] = "\x1b[B";

	ASSERT_TRUE(editorDrawerMainMenuToggle());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_TRUE(E.drawer_selected_index >= 0);
	ASSERT_EQ_INT(initial_cy, E.cy);
	ASSERT_EQ_INT(initial_cx, E.cx);

	ASSERT_TRUE(editorDrawerLspToggle());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_TRUE(E.drawer_selected_index >= 0);
	ASSERT_EQ_INT(initial_cy, E.cy);
	ASSERT_EQ_INT(initial_cx, E.cx);

	ASSERT_TRUE(editorDrawerDapToggle());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_DAP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_TRUE(E.drawer_selected_index >= 0);
	ASSERT_EQ_INT(initial_cy, E.cy);
	ASSERT_EQ_INT(initial_cx, E.cx);

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

	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_TRUE(editorDrawerVisibleCount() > collapsed_count);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "opened");

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "keep");

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_insert_move_and_backspace(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;
	char insert_mode[] = {'i'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_mode, sizeof(insert_mode)) == 0);

	char backspace[] = {BACKSPACE};
	ASSERT_TRUE(editor_process_keypress_with_input(backspace, sizeof(backspace)) == 0);
	ASSERT_ROW_TEXT_EQ(0, "a");
	ASSERT_EQ_INT(1, E.cx);

	char insert_z[] = {'Z'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_z, sizeof(insert_z)) == 0);
	ASSERT_ROW_TEXT_EQ(0, "aZ");
	ASSERT_EQ_INT(2, E.cx);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(1, E.cx);

	char home_key[] = "\x1b[H";
	ASSERT_TRUE(editor_process_keypress_with_input(home_key, sizeof(home_key) - 1) == 0);
	ASSERT_EQ_INT(0, E.cx);

	char end_key[] = "\x1b[F";
	ASSERT_TRUE(editor_process_keypress_with_input(end_key, sizeof(end_key) - 1) == 0);
	ASSERT_EQ_INT(editor_test_row_size(0), E.cx);
	return 0;
}

static int test_editor_process_keypress_alt_c_toggles_line_comment(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	E.syntax_language = EDITOR_SYNTAX_C;

	const char alt_c[] = "\x1b"
	                     "c";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "// hello");

	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hello");
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_alt_c_toggles_python_comment(void) {
	add_row("foo()");
	E.cy = 0;
	E.cx = 0;
	E.syntax_language = EDITOR_SYNTAX_PYTHON;

	const char alt_c[] = "\x1b"
	                     "c";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "# foo()");

	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "foo()");
	return 0;
}

static int test_editor_process_keypress_alt_c_toggles_comment_for_selection(void) {
	add_row("first");
	add_row("second");
	add_row("third");
	E.syntax_language = EDITOR_SYNTAX_C;

	E.cy = 0;
	E.cx = 0;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 2;
	E.cx = editor_test_row_size(2);

	const char alt_c[] = "\x1b"
	                     "c";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "// first");
	ASSERT_ROW_TEXT_EQ(1, "// second");
	ASSERT_ROW_TEXT_EQ(2, "// third");

	// Toggle off — selection cleared by the previous edit, so re-establish it.
	E.cy = 0;
	E.cx = 0;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 2;
	E.cx = editor_test_row_size(2);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "first");
	ASSERT_ROW_TEXT_EQ(1, "second");
	ASSERT_ROW_TEXT_EQ(2, "third");
	return 0;
}

static int test_editor_process_keypress_alt_c_no_op_for_unsupported_language(void) {
	add_row("plain");
	E.cy = 0;
	E.cx = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	int dirty_before = E.dirty;

	const char alt_c[] = "\x1b"
	                     "c";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_c, sizeof(alt_c) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "plain");
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_STR("No line comment for this language", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_opening_pair_autocloses_and_undoes_together(void) {
	add_row("a");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key('(') == 0);
	ASSERT_ROW_TEXT_EQ(0, "a()");
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);

	ASSERT_TRUE(input_actions_vim_key('\x1b') == 0);
	ASSERT_TRUE(input_actions_vim_key('u') == 0);
	ASSERT_ROW_TEXT_EQ(0, "a");
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_quote_pair_autocloses(void) {
	add_row("");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key('"') == 0);
	ASSERT_ROW_TEXT_EQ(0, "\"\"");
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_closing_pair_skips_existing_byte(void) {
	add_row("()");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key(')') == 0);
	ASSERT_ROW_TEXT_EQ(0, "()");
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_opening_pair_before_word_inserts_literal(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key('(') == 0);
	ASSERT_ROW_TEXT_EQ(0, "a(b");
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_vim_percent_jumps_to_matching_bracket(void) {
	add_row("a(b[c]d)e");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key('%') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);

	ASSERT_TRUE(editor_process_single_key('%') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_editor_process_keypress_vim_percent_reports_missing_match(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key('%') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_editor_process_keypress_tab_indents_selection(void) {
	add_row("alpha");
	add_row("beta");
	add_row("gamma");
	E.cy = 0;
	E.cx = 0;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 1;
	E.cx = editor_test_row_size(1);
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key('\t') == 0);
	ASSERT_ROW_TEXT_EQ(0, "\talpha");
	ASSERT_ROW_TEXT_EQ(1, "\tbeta");
	ASSERT_ROW_TEXT_EQ(2, "gamma");
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_tab_indents_selection_drops_terminating_zero_column(void) {
	add_row("one");
	add_row("two");
	add_row("three");
	E.cy = 0;
	E.cx = 0;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	// Selection ending at column 0 of the next row should not indent that row.
	E.cy = 2;
	E.cx = 0;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key('\t') == 0);
	ASSERT_ROW_TEXT_EQ(0, "\tone");
	ASSERT_ROW_TEXT_EQ(1, "\ttwo");
	ASSERT_ROW_TEXT_EQ(2, "three");
	return 0;
}

static int test_editor_process_keypress_alt_arrow_up_moves_line_up(void) {
	add_row("first");
	add_row("second");
	add_row("third");
	E.cy = 1;
	E.cx = 2;

	const char alt_up[] = "\x1b[1;3A";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_up, sizeof(alt_up) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "second");
	ASSERT_ROW_TEXT_EQ(1, "first");
	ASSERT_ROW_TEXT_EQ(2, "third");
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_alt_arrow_down_moves_line_down(void) {
	add_row("first");
	add_row("second");
	add_row("third");
	E.cy = 0;
	E.cx = 3;

	const char alt_down[] = "\x1b[1;3B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_down, sizeof(alt_down) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "second");
	ASSERT_ROW_TEXT_EQ(1, "first");
	ASSERT_ROW_TEXT_EQ(2, "third");
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_alt_arrow_up_at_top_no_op(void) {
	add_row("first");
	add_row("second");
	E.cy = 0;
	E.cx = 0;
	int dirty_before = E.dirty;

	const char alt_up[] = "\x1b[1;3A";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_up, sizeof(alt_up) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "first");
	ASSERT_ROW_TEXT_EQ(1, "second");
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_editor_process_keypress_backspace_deletes_active_selection(void) {
	add_row("hello world");
	E.cy = 0;
	E.cx = 5;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	char backspace[] = {BACKSPACE};
	ASSERT_TRUE(editor_process_keypress_with_input(backspace, sizeof(backspace)) == 0);
	ASSERT_ROW_TEXT_EQ(0, " world");
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_delete_deletes_active_selection(void) {
	add_row("hello world");
	E.cy = 0;
	E.cx = 5;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	const char del_key[] = "\x1b[3~";
	ASSERT_TRUE(editor_process_keypress_with_input(del_key, sizeof(del_key) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, " world");
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_ctrl_j_does_not_insert_newline(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('j')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "ab");
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_tab_inserts_literal_tab(void) {
	add_row("");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	ASSERT_TRUE(editor_process_single_key('\t') == 0);
	ASSERT_EQ_INT(1, editor_test_row_size(0));
	ASSERT_ROW_TEXT_EQ(0, "\t");
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
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	for (size_t i = 0; i < sizeof(input); i++) {
		ASSERT_TRUE(editor_process_single_key((char)input[i]) == 0);
	}

	ASSERT_EQ_INT((int)sizeof(input), editor_test_row_size(0));
	{
		char *_row = editor_test_row_text(0);
		ASSERT_TRUE(_row != NULL);
		ASSERT_MEM_EQ(expected, _row, sizeof(expected));
		free(_row);
	}
	ASSERT_EQ_INT((int)sizeof(input), E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_delete_key(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(input_actions_enter_insert() == 0);

	char del_key[] = "\x1b[3~";
	ASSERT_TRUE(editor_process_keypress_with_input(del_key, sizeof(del_key) - 1) == 0);
	ASSERT_ROW_TEXT_EQ(0, "acd");
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

	(void)input_actions_dispatch(EDITOR_ACTION_SAVE);
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

static int test_editor_process_keypress_alt_z_no_longer_toggles_line_wrap(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 10;
	E.line_wrap_enabled = 0;
	E.dirty = 7;
	E.coloff = 4;
	E.wrapoff = 2;

	const char alt_z[] = "\x1bz";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_z, sizeof(alt_z) - 1) == 0);
	ASSERT_EQ_INT(0, E.line_wrap_enabled);
	ASSERT_EQ_INT(4, E.coloff);
	ASSERT_EQ_INT(7, E.dirty);
	return 0;
}

static int test_editor_process_keypress_alt_n_creates_editor_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 20;
	E.line_numbers_enabled = 1;
	E.dirty = 7;

	const char alt_n[] = "\x1bn";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_n, sizeof(alt_n) - 1) == 0);
	ASSERT_EQ_INT(1, E.line_numbers_enabled);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_editor_process_keypress_alt_h_switches_to_first_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 20;
	E.current_line_highlight_enabled = 1;
	E.dirty = 0;
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabActiveIndex());

	const char alt_h[] = "\x1bh";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_h, sizeof(alt_h) - 1) == 0);
	ASSERT_EQ_INT(1, E.current_line_highlight_enabled);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "line");

	const char alt_l[] = "\x1bl";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_l, sizeof(alt_l) - 1) == 0);
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	return 0;
}

static int test_editor_process_keypress_arrow_scrolls_created_pane_width(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	E.window_rows = 6;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;
	E.coloff = 0;

	struct editorPaneNode *created = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(created != NULL);
	ASSERT_TRUE(E.focused_leaf == created);

	struct editorRect rect = {0};
	ASSERT_TRUE(editorLayoutFocusedLeafRect(&rect));
	int pane_body_cols = rect.w >= 3 ? rect.w - 2 : rect.w;
	ASSERT_TRUE(pane_body_cols > 4);
	int full_body_cols = editorTextBodyViewportCols(E.window_cols);
	ASSERT_TRUE(pane_body_cols + 5 < full_body_cols);

	const char right[] = "\x1b[C";
	for (int i = 0; i < pane_body_cols + 5; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(right, sizeof(right) - 1) == 0);
	}

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_TRUE(E.coloff > 0);
	ASSERT_TRUE(E.cx > pane_body_cols);
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
	ASSERT_ROW_TEXT_EQ(0, "second");

	ASSERT_TRUE(unlink(first_file) == 0);
	ASSERT_TRUE(unlink(second_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_arrow_navigation_opens_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char first_file[512];
	char second_file[512];
	ASSERT_TRUE(path_join(first_file, sizeof(first_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(second_file, sizeof(second_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(first_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(second_file, "beta\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int alpha_idx = -1;
	int beta_idx = -1;
	ASSERT_TRUE(find_drawer_entry("alpha.txt", &alpha_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("beta.txt", &beta_idx, NULL));

	int start_idx = alpha_idx < beta_idx ? alpha_idx : beta_idx;
	int start_visible = start_idx - 1;
	ASSERT_TRUE(start_visible >= 0);
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(start_visible, E.window_rows + 1));

	(void)input_actions_dispatch(EDITOR_ACTION_FOCUS_DRAWER);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	int tab_count_before = editorTabCount();
	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_INT(start_idx, E.drawer_selected_index);
	ASSERT_TRUE(E.filename != NULL);
	const char *first_preview_path = alpha_idx < beta_idx ? first_file : second_file;
	ASSERT_EQ_STR(first_preview_path, E.filename);
	int tab_count_after_first = editorTabCount();
	ASSERT_TRUE(tab_count_after_first >= tab_count_before);

	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	const char *second_preview_path = alpha_idx < beta_idx ? second_file : first_file;
	ASSERT_EQ_STR(second_preview_path, E.filename);
	ASSERT_EQ_INT(tab_count_after_first, editorTabCount());

	ASSERT_TRUE(unlink(first_file) == 0);
	ASSERT_TRUE(unlink(second_file) == 0);
	cleanup_recovery_test_env(&env);
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

	(void)input_actions_dispatch(EDITOR_ACTION_PAGE_DOWN);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	(void)input_actions_dispatch(EDITOR_ACTION_PAGE_UP);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(4, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	return 0;
}

static int test_editor_bracket_match_same_line_forward(void) {
	add_row("foo(a + b)bar");
	E.cy = 0;
	E.cx = 3; /* on '(' */

	int rows[2] = {-1, -1};
	int cols[2] = {-1, -1};
	ASSERT_TRUE(editorBracketMatchComputeForCursor(rows, cols));
	ASSERT_EQ_INT(0, rows[0]);
	ASSERT_EQ_INT(3, cols[0]);
	ASSERT_EQ_INT(0, rows[1]);
	ASSERT_EQ_INT(9, cols[1]); /* the ')' */
	return 0;
}

static int test_editor_bracket_match_same_line_backward(void) {
	add_row("foo(a + b)bar");
	E.cy = 0;
	E.cx = 9; /* on ')' */

	int rows[2] = {-1, -1};
	int cols[2] = {-1, -1};
	ASSERT_TRUE(editorBracketMatchComputeForCursor(rows, cols));
	ASSERT_EQ_INT(0, rows[0]);
	ASSERT_EQ_INT(9, cols[0]);
	ASSERT_EQ_INT(0, rows[1]);
	ASSERT_EQ_INT(3, cols[1]); /* the '(' */
	return 0;
}

static int test_editor_bracket_match_nested_picks_correct_pair(void) {
	add_row("((x))");
	E.cy = 0;
	E.cx = 0; /* outer '(' */

	int rows[2] = {-1, -1};
	int cols[2] = {-1, -1};
	ASSERT_TRUE(editorBracketMatchComputeForCursor(rows, cols));
	ASSERT_EQ_INT(0, cols[0]);
	ASSERT_EQ_INT(4, cols[1]); /* outer ')' */

	E.cx = 1; /* inner '(' */
	ASSERT_TRUE(editorBracketMatchComputeForCursor(rows, cols));
	ASSERT_EQ_INT(1, cols[0]);
	ASSERT_EQ_INT(3, cols[1]); /* inner ')' */
	return 0;
}

static int test_editor_bracket_match_across_lines(void) {
	add_row("func() {");
	add_row("    body();");
	add_row("}");
	E.cy = 0;
	E.cx = 7; /* on '{' */

	int rows[2] = {-1, -1};
	int cols[2] = {-1, -1};
	ASSERT_TRUE(editorBracketMatchComputeForCursor(rows, cols));
	ASSERT_EQ_INT(0, rows[0]);
	ASSERT_EQ_INT(7, cols[0]);
	ASSERT_EQ_INT(2, rows[1]); /* '}' on third line */
	ASSERT_EQ_INT(0, cols[1]);
	return 0;
}

static int test_editor_bracket_match_inactive_when_not_on_bracket(void) {
	add_row("foo(a + b)bar");
	E.cy = 0;
	E.cx = 0; /* on 'f' */

	int rows[2] = {-1, -1};
	int cols[2] = {-1, -1};
	ASSERT_TRUE(!editorBracketMatchComputeForCursor(rows, cols));

	E.cx = 13; /* end of line, no char under cursor */
	ASSERT_TRUE(!editorBracketMatchComputeForCursor(rows, cols));
	return 0;
}

static int test_editor_bracket_match_inactive_when_unbalanced(void) {
	add_row("foo(a + b");
	E.cy = 0;
	E.cx = 3; /* on '(' with no closing */

	int rows[2] = {-1, -1};
	int cols[2] = {-1, -1};
	ASSERT_TRUE(!editorBracketMatchComputeForCursor(rows, cols));
	return 0;
}

static int test_editor_process_keypress_ctrl_arrow_up_down_scroll_viewport(void) {
	for (int i = 0; i < 20; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 20;
	E.cy = 10;
	E.cx = 2;
	E.rowoff = 8;

	(void)input_actions_dispatch(EDITOR_ACTION_SCROLL_DOWN);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	(void)input_actions_dispatch(EDITOR_ACTION_SCROLL_UP);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(8, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	return 0;
}

static int test_editor_process_keypress_ctrl_arrow_moves_by_word(void) {
	add_row("alpha beta.gamma");
	add_row("  delta");
	E.window_rows = 5;
	E.window_cols = 30;
	E.cy = 0;
	E.cx = 0;
	E.coloff = 0;

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_RIGHT);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_RIGHT);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(10, E.cx);

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_RIGHT);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(16, E.cx);

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_RIGHT);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(7, E.cx);

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_LEFT);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_LEFT);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(11, E.cx);

	(void)input_actions_dispatch(EDITOR_ACTION_MOVE_WORD_LEFT);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(6, E.cx);
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

	const char insert_char[] = {'i', 'x'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_char, sizeof(insert_char)) == 0);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FOLLOW_CURSOR, E.viewport_mode);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(1, E.cx);
	{
		char *_row = editor_test_row_text(0);
		ASSERT_TRUE(_row != NULL && _row[0] == 'x');
		free(_row);
	}
	return 0;
}

static int test_editor_goto_line_action_jumps_to_line_and_sets_col_zero(void) {
	add_row("one");
	add_row("two");
	add_row("three");
	E.cy = 2;
	E.cx = 4;

	const char input[] = {'2', '\r'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, input,
	                                              sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_editor_goto_line_action_clamps_to_last_line(void) {
	add_row("first");
	add_row("last");
	E.cy = 0;
	E.cx = 2;

	const char input[] = {'9', '\r'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, input,
	                                              sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_editor_goto_line_action_rejects_invalid_input(void) {
	add_row("alpha");
	add_row("beta");
	E.cy = 1;
	E.cx = 2;

	const char letters[] = {'a', 'b', 'c', '\r'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, letters,
	                                              sizeof(letters)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	const char zero[] = {'0', '\r'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, zero,
	                                              sizeof(zero)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	char overflow[65];
	for (size_t i = 0; i < sizeof(overflow) - 1; i++) {
		overflow[i] = '9';
	}
	overflow[sizeof(overflow) - 1] = '\r';
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, overflow,
	                                              sizeof(overflow)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	return 0;
}

static int test_editor_goto_line_action_escape_cancels(void) {
	add_row("alpha");
	add_row("beta");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {'1', '2', '\x1b', '[', 'x'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, input,
	                                              sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_editor_goto_line_action_empty_buffer_sets_status(void) {
	E.cy = 0;
	E.cx = 0;

	const char input[] = {'1', '\r'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, input,
	                                              sizeof(input)) == 0);

	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_STR("Buffer is empty", E.statusmsg);
	return 0;
}

static int test_editor_goto_line_action_breaks_undo_typed_run_group(void) {
	ASSERT_TRUE(input_actions_enter_insert() == 0);
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_ROW_TEXT_EQ(0, "ab");

	const char goto_first_line[] = {'1', '\r'};
	ASSERT_TRUE(editor_dispatch_action_with_input(EDITOR_ACTION_GOTO_LINE, goto_first_line,
	                                              sizeof(goto_first_line)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(editor_process_single_key('z') == 0);
	ASSERT_ROW_TEXT_EQ(0, "zab");

	(void)input_actions_dispatch(EDITOR_ACTION_UNDO);
	ASSERT_ROW_TEXT_EQ(0, "ab");

	(void)input_actions_dispatch(EDITOR_ACTION_UNDO);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_vim_ex_q_exits_promptly(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(91);
		}

		char quit[] = {':', 'q', '\r'};
		if (editor_process_keypress_with_input(quit, sizeof(quit)) == -1) {
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

static int test_editor_process_keypress_vim_ex_q_restores_cursor_shape(void) {
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

		char quit[] = {':', 'q', '\r'};
		if (editor_process_keypress_with_input_to_stdout(quit, sizeof(quit)) == -1) {
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
	ASSERT_TRUE(strstr(output, "\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[0 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]112\x07") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_vim_ex_q_dirty_requires_force(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(101);
		}

		add_row("unsaved");
		E.dirty = 1;

		char quit[] = {':', 'q', '\r'};
		if (editor_process_keypress_with_input(quit, sizeof(quit)) == -1) {
			_exit(102);
		}
		if (strcmp(E.statusmsg, "No write since last change (add ! to override)") != 0) {
			_exit(103);
		}
		char force_quit[] = {':', 'q', '!', '\r'};
		if (editor_process_keypress_with_input(force_quit, sizeof(force_quit)) == -1) {
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

		if (editor_process_keypress_with_input_to_stdout("", 0) == -1) {
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
	ASSERT_TRUE(strstr(output, "\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[0 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]112\x07") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2J") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[H") != NULL);
	free(output);
	return 0;
}

static int test_editor_find_action_prompt_eof_exits_with_failure(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(141);
		}

		if (editor_dispatch_action_with_input(EDITOR_ACTION_FIND, "", 0) == -1) {
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

static int test_editor_open_file_in_split_new_file_shows_only_file(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "a\nb\nc\n"));

	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 20;
	E.window_cols = 80;
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, "origin", "diff\n"));
	int origin_tab = editorTabActiveIndex();
	struct editorPaneNode *origin = E.focused_leaf;
	int tab_count_before = editorTabCount();

	struct editorPaneNode *pane =
	        editorTabOpenFileInSplit(EDITOR_SPLIT_VERTICAL, 0.5, open_file);
	ASSERT_TRUE(pane != NULL);
	ASSERT_TRUE(pane == E.focused_leaf && pane != origin);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_EQ_INT(1, pane->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(EDITOR_TAB_FILE, E.tab_kind);
	ASSERT_TRUE(E.filename != NULL && strcmp(E.filename, open_file) == 0);
	ASSERT_TRUE(!editorPaneViewHasTab(&pane->as.leaf.view, origin_tab));

	ASSERT_EQ_INT(tab_count_before + 1, editorTabCount());
	ASSERT_TRUE(editorPaneViewHasTab(&origin->as.leaf.view, origin_tab));

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_open_file_in_split_reuses_already_open_tab(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "a\nb\nc\n"));

	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 20;
	E.window_cols = 80;
	ASSERT_TRUE(editorTabOpenFileAsNew(open_file));
	int file_tab = editorTabActiveIndex();
	struct editorPaneNode *origin = E.focused_leaf;
	int tab_count_before = editorTabCount();

	struct editorPaneNode *pane =
	        editorTabOpenFileInSplit(EDITOR_SPLIT_VERTICAL, 0.5, open_file);
	ASSERT_TRUE(pane != NULL && pane != origin);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_EQ_INT(1, pane->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(file_tab, pane->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(tab_count_before, editorTabCount());
	ASSERT_EQ_INT(EDITOR_TAB_FILE, E.tab_kind);
	ASSERT_TRUE(E.filename != NULL && strcmp(E.filename, open_file) == 0);

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_open_file_in_split_unopenable_file_does_not_split(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 20;
	E.window_cols = 80;
	int leaves_before = editorPaneTreeLeafCount(E.layout_root);
	int tab_count_before = editorTabCount();

	struct editorPaneNode *pane = editorTabOpenFileInSplit(
	        EDITOR_SPLIT_VERTICAL, 0.5, "/nonexistent-rotide-dir-zzz/missing.txt");
	ASSERT_TRUE(pane == NULL);
	ASSERT_EQ_INT(leaves_before, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_INT(tab_count_before, editorTabCount());
	return 0;
}

static int install_git_diff_tab(const char *open_file) {
	int diff_idx = editorTabActiveIndex();
	int *line_numbers = malloc(2 * sizeof(int));
	if (line_numbers == NULL) {
		return -1;
	}
	line_numbers[0] = 5;
	line_numbers[1] = 6;
	free(E.git_view_line_numbers);
	free(E.git_view_source_path);
	E.git_view_line_numbers = line_numbers;
	E.git_view_line_kind_count = 2;
	E.git_view_source_path = strdup(open_file);
	E.cy = 0;
	return diff_idx;
}

static int test_editor_git_diff_open_in_split_shows_only_file(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "l1\nl2\nl3\nl4\nl5\nl6\n"));

	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 20;
	E.window_cols = 80;
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, open_file, "diff\ncontext\n"));
	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);

	int diff_idx = install_git_diff_tab(open_file);
	ASSERT_TRUE(diff_idx >= 0);
	struct editorPaneNode *original = E.focused_leaf;
	ASSERT_TRUE(original != NULL);

	(void)input_actions_dispatch(EDITOR_ACTION_GIT_DIFF_OPEN_IN_SPLIT);

	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	struct editorPaneNode *sibling = E.focused_leaf;
	ASSERT_TRUE(sibling != NULL && sibling != original && !sibling->is_split);

	ASSERT_EQ_INT(EDITOR_TAB_FILE, E.tab_kind);
	ASSERT_TRUE(E.filename != NULL && strcmp(E.filename, open_file) == 0);
	ASSERT_EQ_INT(4, E.cy);
	int file_idx = editorTabActiveIndex();
	ASSERT_TRUE(file_idx != diff_idx);
	ASSERT_EQ_INT(1, sibling->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(file_idx, sibling->as.leaf.view.pane_tabs[0]);
	ASSERT_TRUE(!editorPaneViewHasTab(&sibling->as.leaf.view, diff_idx));

	ASSERT_TRUE(editorPaneViewHasTab(&original->as.leaf.view, diff_idx));

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_git_diff_jump_to_file_replaces_current_pane(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "l1\nl2\nl3\nl4\nl5\nl6\n"));

	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 20;
	E.window_cols = 80;
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, open_file, "diff\ncontext\n"));
	int diff_idx = install_git_diff_tab(open_file);
	ASSERT_TRUE(diff_idx >= 0);

	(void)input_actions_dispatch(EDITOR_ACTION_GIT_DIFF_JUMP_TO_FILE);

	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_INT(EDITOR_TAB_FILE, E.tab_kind);
	ASSERT_TRUE(E.filename != NULL && strcmp(E.filename, open_file) == 0);
	ASSERT_EQ_INT(4, E.cy);

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

const struct editorTestCase g_input_actions_tests[] = {
        {"editor_open_file_in_split_new_file_shows_only_file",
         test_editor_open_file_in_split_new_file_shows_only_file},
        {"editor_open_file_in_split_reuses_already_open_tab",
         test_editor_open_file_in_split_reuses_already_open_tab},
        {"editor_open_file_in_split_unopenable_file_does_not_split",
         test_editor_open_file_in_split_unopenable_file_does_not_split},
        {"editor_git_diff_open_in_split_shows_only_file",
         test_editor_git_diff_open_in_split_shows_only_file},
        {"editor_git_diff_jump_to_file_replaces_current_pane",
         test_editor_git_diff_jump_to_file_replaces_current_pane},
        {"editor_process_keypress_alt_b_git_blame_details_popup_behaviors",
         test_editor_process_keypress_alt_b_git_blame_details_popup_behaviors},
        {"editor_task_log_document_stays_authoritative",
         test_editor_task_log_document_stays_authoritative},
        {"editor_task_log_streams_output_while_inactive",
         test_editor_task_log_streams_output_while_inactive},
        {"editor_task_runner_merges_stderr_and_close_requires_confirmation",
         test_editor_task_runner_merges_stderr_and_close_requires_confirmation},
        {"editor_task_runner_truncates_large_output",
         test_editor_task_runner_truncates_large_output},
        {"editor_action_move_active_tab_right_pane_moves_and_focuses_right",
         test_editor_action_move_active_tab_right_pane_moves_and_focuses_right},
        {"editor_action_move_active_tab_left_pane_moves_and_focuses_left",
         test_editor_action_move_active_tab_left_pane_moves_and_focuses_left},
        {"editor_action_move_active_tab_down_pane_moves_and_focuses_down",
         test_editor_action_move_active_tab_down_pane_moves_and_focuses_down},
        {"editor_action_move_active_tab_up_pane_moves_and_focuses_up",
         test_editor_action_move_active_tab_up_pane_moves_and_focuses_up},
        {"editor_action_move_active_tab_no_neighbor_is_no_op",
         test_editor_action_move_active_tab_no_neighbor_is_no_op},
        {"editor_action_move_active_tab_empty_pane_is_no_op",
         test_editor_action_move_active_tab_empty_pane_is_no_op},
        {"editor_action_move_last_tab_replaces_source_with_empty_buffer",
         test_editor_action_move_last_tab_replaces_source_with_empty_buffer},
        {"editor_tabs_ensure_pane_occupancy_backfills_empty_leaves",
         test_editor_tabs_ensure_pane_occupancy_backfills_empty_leaves},
        {"editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands",
         test_editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands},
        {"editor_process_keypress_search_escape_restores_previous_tab",
         test_editor_process_keypress_search_escape_restores_previous_tab},
        {"editor_process_keypress_search_escape_recollapses_drawer",
         test_editor_process_keypress_search_escape_recollapses_drawer},
        {"editor_process_keypress_search_escape_carries_mode_across_switch",
         test_editor_process_keypress_search_escape_carries_mode_across_switch},
        {"editor_process_keypress_search_escape_keeps_tree_expanded",
         test_editor_process_keypress_search_escape_keeps_tree_expanded},
        {"editor_process_keypress_main_menu_runs_selected_action",
         test_editor_process_keypress_main_menu_runs_selected_action},
        {"editor_process_keypress_main_menu_project_files_opens_tree",
         test_editor_process_keypress_main_menu_project_files_opens_tree},
        {"editor_process_keypress_context_menu_runs_split_action",
         test_editor_process_keypress_context_menu_runs_split_action},
        {"editor_tabs_switch_restores_per_tab_state",
         test_editor_tabs_switch_restores_per_tab_state},
        {"editor_tab_close_uses_pane_activation_history_repeatedly",
         test_editor_tab_close_uses_pane_activation_history_repeatedly},
        {"editor_tab_close_mru_survives_global_index_shift",
         test_editor_tab_close_mru_survives_global_index_shift},
        {"editor_tab_close_last_tab_keeps_one_empty_tab",
         test_editor_tab_close_last_tab_keeps_one_empty_tab},
        {"editor_tab_close_last_in_pane_closes_pane_when_other_panes_exist",
         test_editor_tab_close_last_in_pane_closes_pane_when_other_panes_exist},
        {"editor_tab_close_last_in_single_pane_keeps_empty_buffer",
         test_editor_tab_close_last_in_single_pane_keeps_empty_buffer},
        {"editor_process_keypress_ctrl_w_dirty_requires_second_press",
         test_editor_process_keypress_ctrl_w_dirty_requires_second_press},
        {"editor_process_keypress_close_tab_confirmation_resets_on_other_action",
         test_editor_process_keypress_close_tab_confirmation_resets_on_other_action},
        {"editor_process_keypress_ctrl_q_checks_dirty_tabs_globally",
         test_editor_process_keypress_ctrl_q_checks_dirty_tabs_globally},
        {"editor_process_keypress_tab_actions_new_next_prev",
         test_editor_process_keypress_tab_actions_new_next_prev},
        {"editor_safe_close_follows_focused_pane_mru",
         test_editor_safe_close_follows_focused_pane_mru},
        {"editor_new_tab_inserts_beside_active", test_editor_new_tab_inserts_beside_active},
        {"editor_alt_first_last_stay_in_focused_pane",
         test_editor_alt_first_last_stay_in_focused_pane},
        {"editor_process_keypress_alt_t_creates_terminal_tab",
         test_editor_process_keypress_alt_t_creates_terminal_tab},
        {"editor_safe_close_dirty_requires_consecutive_second_press",
         test_editor_safe_close_dirty_requires_consecutive_second_press},
        {"editor_tab_open_file_reuses_active_clean_empty_buffer",
         test_editor_tab_open_file_reuses_active_clean_empty_buffer},
        {"editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive",
         test_editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive},
        {"editor_process_keypress_focus_drawer_and_arrow_navigation",
         test_editor_process_keypress_focus_drawer_and_arrow_navigation},
        {"editor_process_keypress_drawer_modes_arrow_navigation_keeps_text_cursor",
         test_editor_process_keypress_drawer_modes_arrow_navigation_keeps_text_cursor},
        {"editor_process_keypress_drawer_enter_toggles_directory",
         test_editor_process_keypress_drawer_enter_toggles_directory},
        {"editor_process_keypress_drawer_enter_opens_file_in_new_tab",
         test_editor_process_keypress_drawer_enter_opens_file_in_new_tab},
        {"editor_process_keypress_insert_move_and_backspace",
         test_editor_process_keypress_insert_move_and_backspace},
        {"editor_process_keypress_alt_c_toggles_line_comment",
         test_editor_process_keypress_alt_c_toggles_line_comment},
        {"editor_process_keypress_alt_c_toggles_python_comment",
         test_editor_process_keypress_alt_c_toggles_python_comment},
        {"editor_process_keypress_alt_c_toggles_comment_for_selection",
         test_editor_process_keypress_alt_c_toggles_comment_for_selection},
        {"editor_process_keypress_alt_c_no_op_for_unsupported_language",
         test_editor_process_keypress_alt_c_no_op_for_unsupported_language},
        {"editor_process_keypress_opening_pair_autocloses_and_undoes_together",
         test_editor_process_keypress_opening_pair_autocloses_and_undoes_together},
        {"editor_process_keypress_quote_pair_autocloses",
         test_editor_process_keypress_quote_pair_autocloses},
        {"editor_process_keypress_closing_pair_skips_existing_byte",
         test_editor_process_keypress_closing_pair_skips_existing_byte},
        {"editor_process_keypress_opening_pair_before_word_inserts_literal",
         test_editor_process_keypress_opening_pair_before_word_inserts_literal},
        {"editor_process_keypress_vim_percent_jumps_to_matching_bracket",
         test_editor_process_keypress_vim_percent_jumps_to_matching_bracket},
        {"editor_process_keypress_vim_percent_reports_missing_match",
         test_editor_process_keypress_vim_percent_reports_missing_match},
        {"editor_process_keypress_tab_indents_selection",
         test_editor_process_keypress_tab_indents_selection},
        {"editor_process_keypress_tab_indents_selection_drops_terminating_zero_column",
         test_editor_process_keypress_tab_indents_selection_drops_terminating_zero_column},
        {"editor_process_keypress_alt_arrow_up_moves_line_up",
         test_editor_process_keypress_alt_arrow_up_moves_line_up},
        {"editor_process_keypress_alt_arrow_down_moves_line_down",
         test_editor_process_keypress_alt_arrow_down_moves_line_down},
        {"editor_process_keypress_alt_arrow_up_at_top_no_op",
         test_editor_process_keypress_alt_arrow_up_at_top_no_op},
        {"editor_process_keypress_backspace_deletes_active_selection",
         test_editor_process_keypress_backspace_deletes_active_selection},
        {"editor_process_keypress_delete_deletes_active_selection",
         test_editor_process_keypress_delete_deletes_active_selection},
        {"editor_process_keypress_ctrl_j_does_not_insert_newline",
         test_editor_process_keypress_ctrl_j_does_not_insert_newline},
        {"editor_process_keypress_tab_inserts_literal_tab",
         test_editor_process_keypress_tab_inserts_literal_tab},
        {"editor_process_keypress_utf8_bytes_insert_verbatim",
         test_editor_process_keypress_utf8_bytes_insert_verbatim},
        {"editor_process_keypress_delete_key", test_editor_process_keypress_delete_key},
        {"editor_process_keypress_arrow_down_keeps_visual_column",
         test_editor_process_keypress_arrow_down_keeps_visual_column},
        {"editor_process_keypress_ctrl_s_saves_file",
         test_editor_process_keypress_ctrl_s_saves_file},
        {"editor_process_keypress_resize_event_updates_window_size",
         test_editor_process_keypress_resize_event_updates_window_size},
        {"editor_process_keypress_alt_z_no_longer_toggles_line_wrap",
         test_editor_process_keypress_alt_z_no_longer_toggles_line_wrap},
        {"editor_process_keypress_alt_n_creates_editor_tab",
         test_editor_process_keypress_alt_n_creates_editor_tab},
        {"editor_process_keypress_alt_h_switches_to_first_tab",
         test_editor_process_keypress_alt_h_switches_to_first_tab},
        {"editor_process_keypress_arrow_scrolls_created_pane_width",
         test_editor_process_keypress_arrow_scrolls_created_pane_width},
        {"editor_drawer_open_selected_file_in_preview_reuses_preview_tab",
         test_editor_drawer_open_selected_file_in_preview_reuses_preview_tab},
        {"editor_drawer_arrow_navigation_opens_preview_tab",
         test_editor_drawer_arrow_navigation_opens_preview_tab},
        {"editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor",
         test_editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor},
        {"editor_bracket_match_same_line_forward", test_editor_bracket_match_same_line_forward},
        {"editor_bracket_match_same_line_backward", test_editor_bracket_match_same_line_backward},
        {"editor_bracket_match_nested_picks_correct_pair",
         test_editor_bracket_match_nested_picks_correct_pair},
        {"editor_bracket_match_across_lines", test_editor_bracket_match_across_lines},
        {"editor_bracket_match_inactive_when_not_on_bracket",
         test_editor_bracket_match_inactive_when_not_on_bracket},
        {"editor_bracket_match_inactive_when_unbalanced",
         test_editor_bracket_match_inactive_when_unbalanced},
        {"editor_process_keypress_ctrl_arrow_up_down_scroll_viewport",
         test_editor_process_keypress_ctrl_arrow_up_down_scroll_viewport},
        {"editor_process_keypress_ctrl_arrow_moves_by_word",
         test_editor_process_keypress_ctrl_arrow_moves_by_word},
        {"editor_process_keypress_free_scroll_can_leave_cursor_offscreen",
         test_editor_process_keypress_free_scroll_can_leave_cursor_offscreen},
        {"editor_process_keypress_cursor_move_resyncs_follow_scroll",
         test_editor_process_keypress_cursor_move_resyncs_follow_scroll},
        {"editor_process_keypress_edit_resyncs_follow_scroll",
         test_editor_process_keypress_edit_resyncs_follow_scroll},
        {"editor_goto_line_action_jumps_to_line_and_sets_col_zero",
         test_editor_goto_line_action_jumps_to_line_and_sets_col_zero},
        {"editor_goto_line_action_clamps_to_last_line",
         test_editor_goto_line_action_clamps_to_last_line},
        {"editor_goto_line_action_rejects_invalid_input",
         test_editor_goto_line_action_rejects_invalid_input},
        {"editor_goto_line_action_escape_cancels", test_editor_goto_line_action_escape_cancels},
        {"editor_goto_line_action_empty_buffer_sets_status",
         test_editor_goto_line_action_empty_buffer_sets_status},
        {"editor_goto_line_action_breaks_undo_typed_run_group",
         test_editor_goto_line_action_breaks_undo_typed_run_group},
        {"editor_process_keypress_vim_ex_q_exits_promptly",
         test_editor_process_keypress_vim_ex_q_exits_promptly},
        {"editor_process_keypress_vim_ex_q_restores_cursor_shape",
         test_editor_process_keypress_vim_ex_q_restores_cursor_shape},
        {"editor_process_keypress_vim_ex_q_dirty_requires_force",
         test_editor_process_keypress_vim_ex_q_dirty_requires_force},
        {"editor_process_keypress_eof_exits_promptly_with_failure",
         test_editor_process_keypress_eof_exits_promptly_with_failure},
        {"editor_process_keypress_eof_restores_terminal_visual_state",
         test_editor_process_keypress_eof_restores_terminal_visual_state},
        {"editor_find_action_prompt_eof_exits_with_failure",
         test_editor_find_action_prompt_eof_exits_with_failure},
        {"process_terminates_promptly_on_sigterm", test_process_terminates_promptly_on_sigterm},
};

const size_t g_input_actions_test_count =
        sizeof(g_input_actions_tests) / sizeof(g_input_actions_tests[0]);
