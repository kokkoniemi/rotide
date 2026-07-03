#include "rotide.h"
#include "test_case.h"
#include "test_grid_snapshot.h"
#include "test_helpers.h"
#include "workspace/git_view.h"
#include "workspace/tabs.h"

#include <stdlib.h>
#include <string.h>

static int test_git_view_clean_message_strips_comments(void) {
	char *cleaned = editorGitViewCleanCommitMessageDup(
	        "my subject\n\n# Save to commit\n# ignored line\nbody line\n");
	ASSERT_TRUE(cleaned != NULL);
	ASSERT_EQ_STR("my subject\n\nbody line", cleaned);
	free(cleaned);
	return 0;
}

static int test_git_view_clean_message_all_comments_is_empty(void) {
	char *cleaned = editorGitViewCleanCommitMessageDup("# one\n#\n# two\n");
	ASSERT_TRUE(cleaned != NULL);
	ASSERT_EQ_STR("", cleaned);
	free(cleaned);
	return 0;
}

static int test_git_view_clean_message_empty_input(void) {
	char *cleaned = editorGitViewCleanCommitMessageDup("");
	ASSERT_TRUE(cleaned != NULL);
	ASSERT_EQ_STR("", cleaned);
	free(cleaned);

	cleaned = editorGitViewCleanCommitMessageDup("\n\n\n");
	ASSERT_TRUE(cleaned != NULL);
	ASSERT_EQ_STR("", cleaned);
	free(cleaned);
	return 0;
}

static int test_git_view_clean_message_strips_cr_and_trailing_blank(void) {
	char *cleaned = editorGitViewCleanCommitMessageDup("subject\r\n\r\n# comment\r\n\n  \n");
	ASSERT_TRUE(cleaned != NULL);
	ASSERT_EQ_STR("subject", cleaned);
	free(cleaned);
	return 0;
}

static int test_git_view_clean_message_preserves_hash_mid_line(void) {
	char *cleaned = editorGitViewCleanCommitMessageDup("fix issue #42\n");
	ASSERT_TRUE(cleaned != NULL);
	ASSERT_EQ_STR("fix issue #42", cleaned);
	free(cleaned);
	return 0;
}

/* One day in seconds; ages below stay deterministic via a fixed `now`. */
#define GIT_VIEW_TEST_DAY 86400

static int test_git_view_format_branches_groups_and_tracks(void) {
	const char *raw = "*\trefs/heads/main\tmain\torigin/main\t[ahead 2, behind 1]\t1000000\n"
	                  " \trefs/heads/feature/x\tfeature/x\t\t\t1000000\n"
	                  " \trefs/remotes/origin/main\torigin/main\t\t\t1000000\n";
	char *text = editorGitViewFormatBranchesDup(raw, 1000000 + 3 * GIT_VIEW_TEST_DAY);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "# branches") != NULL);
	ASSERT_TRUE(strstr(text, "* main  ↑2↓1  origin/main") != NULL);
	ASSERT_TRUE(strstr(text, "\n  feature/x") != NULL);
	ASSERT_TRUE(strstr(text, "# remotes\n") != NULL);
	ASSERT_TRUE(strstr(text, "\n  origin/main") != NULL);
	free(text);
	return 0;
}

static int test_git_view_format_branches_empty_list(void) {
	char *text = editorGitViewFormatBranchesDup("", 1000000);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "(no branches)") != NULL);
	free(text);
	return 0;
}

static int test_git_view_format_log_with_decorations(void) {
	const char *raw = "abc1234\t (HEAD -> main, tag: v1)\tfix the bug\tAlice\t1000000\n"
	                  "def5678\t\tsecond subject\tBob\t1000000\n";
	char *text = editorGitViewFormatLogDup(raw, 1000000 + GIT_VIEW_TEST_DAY);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "# commits") != NULL);
	ASSERT_TRUE(strstr(text, "abc1234  (HEAD -> main, tag: v1) fix the bug — Alice") != NULL);
	ASSERT_TRUE(strstr(text, "def5678  second subject — Bob") != NULL);
	free(text);

	text = editorGitViewFormatLogDup("", 1000000);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "(no commits)") != NULL);
	free(text);
	return 0;
}

static int test_git_view_format_stash_lists_and_empty(void) {
	char *text = editorGitViewFormatStashDup("stash@{0}: WIP on main: quick fix\n");
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "# stashes") != NULL);
	ASSERT_TRUE(strstr(text, "stash@{0}: WIP on main: quick fix") != NULL);
	free(text);

	text = editorGitViewFormatStashDup("");
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "(no stashes)") != NULL);
	free(text);
	return 0;
}

static int test_git_view_line_entity_branches(void) {
	char entity[64];
	ASSERT_TRUE(editorGitViewLineEntity(
	        EDITOR_TAB_GIT_BRANCHES, "* main  ↑2↓1  origin/main  3d", entity, sizeof(entity)));
	ASSERT_EQ_STR("main", entity);
	ASSERT_TRUE(editorGitViewLineEntity(EDITOR_TAB_GIT_BRANCHES, "  feature/x  2w", entity,
	                                    sizeof(entity)));
	ASSERT_EQ_STR("feature/x", entity);
	ASSERT_TRUE(!editorGitViewLineEntity(EDITOR_TAB_GIT_BRANCHES, "# branches · Enter", entity,
	                                     sizeof(entity)));
	ASSERT_TRUE(!editorGitViewLineEntity(EDITOR_TAB_GIT_BRANCHES, "(no branches)", entity,
	                                     sizeof(entity)));
	ASSERT_TRUE(!editorGitViewLineEntity(EDITOR_TAB_GIT_BRANCHES, "", entity, sizeof(entity)));
	return 0;
}

static int test_git_view_line_entity_log_and_stash(void) {
	char entity[64];
	ASSERT_TRUE(editorGitViewLineEntity(EDITOR_TAB_GIT_LOG,
	                                    "abc1234  (tag: v1) subject — Alice, 3d", entity,
	                                    sizeof(entity)));
	ASSERT_EQ_STR("abc1234", entity);
	ASSERT_TRUE(!editorGitViewLineEntity(EDITOR_TAB_GIT_LOG, "# commits · Enter show", entity,
	                                     sizeof(entity)));
	ASSERT_TRUE(!editorGitViewLineEntity(EDITOR_TAB_GIT_LOG, "(no commits)", entity,
	                                     sizeof(entity)));

	ASSERT_TRUE(editorGitViewLineEntity(EDITOR_TAB_GIT_STASH, "stash@{2}: WIP on main: things",
	                                    entity, sizeof(entity)));
	ASSERT_EQ_STR("stash@{2}", entity);
	ASSERT_TRUE(!editorGitViewLineEntity(EDITOR_TAB_GIT_STASH, "(no stashes)", entity,
	                                     sizeof(entity)));
	return 0;
}

static int test_git_view_status_bar_shows_ahead_behind(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	E.git_branch = strdup("main");
	ASSERT_TRUE(E.git_branch != NULL);
	E.git_ahead = 3;
	E.git_behind = 1;

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "main ↑3↓1") != NULL);
	free(out);

	free(E.git_branch);
	E.git_branch = NULL;
	return 0;
}

static int test_git_view_branches_render_golden(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 6;
	E.window_cols = 46;
	const char *text = "# branches · Enter checkout · n new · d delete\n"
	                   "* main  ↑1  origin/main  3d\n"
	                   "  feature/x  2w\n"
	                   "# remotes\n"
	                   "  origin/main  3d\n";
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_BRANCHES, "git branches", text));
	E.drawer_collapsed = 1;

	ASSERT_GRID_EQ(
	        /* golden-start */
	        "   1  # branches · Enter checkout · n new · d\n"
	        "   2  * main  ↑1  origin/main  3d\n"
	        "   3    feature/x  2w\n"
	        "   4  # remotes\n"
	        "git branches                       1,1    100%\n"
	        /* golden-end */
	);
	return 0;
}

const struct editorTestCase g_git_view_tests[] = {
        {"git_view_clean_message_strips_comments", test_git_view_clean_message_strips_comments},
        {"git_view_clean_message_all_comments_is_empty",
         test_git_view_clean_message_all_comments_is_empty},
        {"git_view_clean_message_empty_input", test_git_view_clean_message_empty_input},
        {"git_view_clean_message_strips_cr_and_trailing_blank",
         test_git_view_clean_message_strips_cr_and_trailing_blank},
        {"git_view_clean_message_preserves_hash_mid_line",
         test_git_view_clean_message_preserves_hash_mid_line},
        {"git_view_format_branches_groups_and_tracks",
         test_git_view_format_branches_groups_and_tracks},
        {"git_view_format_branches_empty_list", test_git_view_format_branches_empty_list},
        {"git_view_format_log_with_decorations", test_git_view_format_log_with_decorations},
        {"git_view_format_stash_lists_and_empty", test_git_view_format_stash_lists_and_empty},
        {"git_view_line_entity_branches", test_git_view_line_entity_branches},
        {"git_view_line_entity_log_and_stash", test_git_view_line_entity_log_and_stash},
        {"git_view_status_bar_shows_ahead_behind", test_git_view_status_bar_shows_ahead_behind},
        {"git_view_branches_render_golden", test_git_view_branches_render_golden},
};

const int g_git_view_test_count = (int)(sizeof(g_git_view_tests) / sizeof(g_git_view_tests[0]));
