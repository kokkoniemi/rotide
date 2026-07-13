#include "config/theme_config.h"
#include "editor_test_api.h"
#include "language/syntax.h"
#include "render/status_bar.h"
#include "rotide.h"
#include "support/alloc.h"
#include "test_case.h"
#include "test_grid_snapshot.h"
#include "test_helpers.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
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

static int test_git_view_build_diff_strips_prefixes_and_tracks_kinds(void) {
	const char *patch = "diff --git a/src/app.c b/src/app.c\n"
	                    "index 1111111..2222222 100644\n"
	                    "--- a/src/app.c\n"
	                    "+++ b/src/app.c\n"
	                    "@@ -101,3 +201,3 @@\n"
	                    "-int old_value = 1;\n"
	                    "+int new_value = 2;\n"
	                    " int kept_value = 3;\n"
	                    "\\ No newline at end of file\n";
	unsigned char *kinds = NULL;
	int *line_numbers = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text = editorGitViewBuildDiffDup(patch, strlen(patch), &kinds, &line_numbers,
	                                       &kind_count, &source_path);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_STR("diff --git a/src/app.c b/src/app.c\n"
	              "@@ -101,3 +201,3 @@\n"
	              "int old_value = 1;\n"
	              "int new_value = 2;\n"
	              "int kept_value = 3;\n",
	              text);
	ASSERT_EQ_INT(5, kind_count);
	ASSERT_EQ_INT(EDITOR_GIT_VIEW_LINE_HEADER, kinds[0]);
	ASSERT_EQ_INT(EDITOR_GIT_VIEW_LINE_HEADER, kinds[1]);
	ASSERT_EQ_INT(EDITOR_GIT_VIEW_LINE_REMOVED, kinds[2]);
	ASSERT_EQ_INT(EDITOR_GIT_VIEW_LINE_ADDED, kinds[3]);
	ASSERT_EQ_INT(EDITOR_GIT_VIEW_LINE_TEXT, kinds[4]);
	ASSERT_EQ_INT(0, line_numbers[0]);
	ASSERT_EQ_INT(0, line_numbers[1]);
	ASSERT_EQ_INT(101, line_numbers[2]);
	ASSERT_EQ_INT(201, line_numbers[3]);
	ASSERT_EQ_INT(202, line_numbers[4]);
	ASSERT_TRUE(source_path != NULL);
	ASSERT_EQ_STR("src/app.c", source_path);
	free(text);
	free(kinds);
	free(line_numbers);
	free(source_path);
	return 0;
}

static int test_git_view_build_diff_multi_file_has_no_source_path(void) {
	const char *patch = "diff --git a/one.c b/one.c\n"
	                    "--- a/one.c\n"
	                    "+++ b/one.c\n"
	                    "@@ -1 +1 @@\n"
	                    "-a\n"
	                    "+b\n"
	                    "diff --git a/two.c b/two.c\n"
	                    "--- a/two.c\n"
	                    "+++ b/two.c\n"
	                    "@@ -1 +1 @@\n"
	                    "-c\n"
	                    "+d\n";
	unsigned char *kinds = NULL;
	int *line_numbers = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text = editorGitViewBuildDiffDup(patch, strlen(patch), &kinds, &line_numbers,
	                                       &kind_count, &source_path);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(source_path == NULL);
	ASSERT_EQ_INT(8, kind_count);
	ASSERT_TRUE(strstr(text, "diff --git a/two.c b/two.c") != NULL);
	free(text);
	free(kinds);
	free(line_numbers);
	return 0;
}

static int test_git_view_build_diff_untracked_uses_new_side_path(void) {
	const char *patch = "diff --git a/new.py b/new.py\n"
	                    "new file mode 100644\n"
	                    "index 0000000..1111111\n"
	                    "--- /dev/null\n"
	                    "+++ b/new.py\n"
	                    "@@ -0,0 +1 @@\n"
	                    "+print(\"hi\")\n";
	unsigned char *kinds = NULL;
	int *line_numbers = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text = editorGitViewBuildDiffDup(patch, strlen(patch), &kinds, &line_numbers,
	                                       &kind_count, &source_path);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(source_path != NULL);
	ASSERT_EQ_STR("new.py", source_path);
	ASSERT_TRUE(strstr(text, "print(\"hi\")") != NULL);
	ASSERT_EQ_INT(EDITOR_GIT_VIEW_LINE_ADDED, kinds[kind_count - 1]);
	free(text);
	free(kinds);
	free(line_numbers);
	free(source_path);
	return 0;
}

static int test_git_view_row_bg_color_for_diff_and_headers(void) {
	ASSERT_TRUE(editorTabsInit());
	const char *text = "diff --git a/x.c b/x.c\n"
	                   "old\n"
	                   "new\n"
	                   "kept\n";
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, "git diff: x.c", text));
	static unsigned char kinds[] = {EDITOR_GIT_VIEW_LINE_HEADER, EDITOR_GIT_VIEW_LINE_REMOVED,
	                                EDITOR_GIT_VIEW_LINE_ADDED, EDITOR_GIT_VIEW_LINE_TEXT};
	E.git_view_line_kinds = editorRealloc(NULL, sizeof(kinds));
	ASSERT_TRUE(E.git_view_line_kinds != NULL);
	memcpy(E.git_view_line_kinds, kinds, sizeof(kinds));
	E.git_view_line_kind_count = 4;

	struct editorThemeColor color;
	ASSERT_TRUE(editorGitViewRowBgColor(0, &color));
	ASSERT_TRUE(editorGitViewRowBgColor(1, &color));
	struct editorThemeColor removed = editorThemeGitDiffBgColor(&E.theme, 0);
	ASSERT_EQ_INT(removed.kind, color.kind);
	ASSERT_EQ_INT(removed.value, color.value);
	ASSERT_TRUE(editorGitViewRowBgColor(2, &color));
	struct editorThemeColor added = editorThemeGitDiffBgColor(&E.theme, 1);
	ASSERT_EQ_INT(added.kind, color.kind);
	ASSERT_EQ_INT(added.value, color.value);
	ASSERT_TRUE(!editorGitViewRowBgColor(3, &color));
	return 0;
}

static int test_git_view_row_spans_colorize_views(void) {
	ASSERT_TRUE(editorTabsInit());
	const char *text = "# branches · Enter checkout\n"
	                   "* main  ↑1  origin/main  3d\n"
	                   "  feature/x  2w\n";
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_BRANCHES, "git branches", text));

	struct editorRowSyntaxSpan spans[8];
	int count = 0;
	ASSERT_TRUE(editorGitViewRowSyntaxSpans(0, spans, 8, &count));
	ASSERT_EQ_INT(1, count);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_COMMENT, spans[0].highlight_class);

	ASSERT_TRUE(editorGitViewRowSyntaxSpans(1, spans, 8, &count));
	ASSERT_TRUE(count >= 2);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_KEYWORD, spans[1].highlight_class);

	ASSERT_TRUE(editorGitViewRowSyntaxSpans(2, spans, 8, &count));
	ASSERT_TRUE(count >= 2);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_FUNCTION, spans[1].highlight_class);
	return 0;
}

/* Re-renders the status bar and reports whether a button dispatches action. */
static int git_view_status_bar_has_action(enum editorAction action) {
	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	if (out == NULL) {
		return -1;
	}
	free(out);
	for (int col = 0; col < E.window_cols; col++) {
		int found = 0;
		if (editorStatusBarButtonAt(col, &found) && found == (int)action) {
			return 1;
		}
	}
	return 0;
}

/* Selects the first non-empty git drawer group header matching staged. */
static int git_view_select_group(int staged) {
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		E.drawer_selected_index = idx;
		int group_staged = 0;
		int items = 0;
		if (editorDrawerGitSelectedGroup(&group_staged, &items) && group_staged == staged &&
		    items > 0) {
			return 1;
		}
	}
	E.drawer_selected_index = -1;
	return 0;
}

/* Selects the first git drawer file row matching staged_group; returns 1. */
static int git_view_select_drawer_file(int staged_group) {
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		E.drawer_selected_index = idx;
		int entry_idx = 0;
		int in_staged = 0;
		if (editorDrawerGitSelectedFile(&entry_idx, &in_staged) &&
		    in_staged == staged_group) {
			return 1;
		}
	}
	E.drawer_selected_index = -1;
	return 0;
}

static int test_git_view_status_bar_git_drawer_buttons(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	E.filename = strdup("visible-name.c");
	ASSERT_TRUE(E.filename != NULL);
	E.window_rows = 12;
	E.window_cols = 120;
	/* One staged and one unstaged entry (porcelain v2 fixture). */
	static const char status[] = "1 M. N... 100644 100644 100644 aaa bbb staged.c\0"
	                             "1 .M N... 100644 100644 100644 aaa bbb dirty.c\0";
	ASSERT_TRUE(editorGitTestParseStatus(status, sizeof(status) - 1, NULL, NULL));
	ASSERT_TRUE(editorDrawerGitToggle());

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	/* Git drawer focus: the action buttons replace the tab name in the
	 * status bar (the tab bar still shows it once). */
	ASSERT_TRUE(strstr(out, "Branches") != NULL);
	const char *first_name = strstr(out, "visible-name.c");
	ASSERT_TRUE(first_name != NULL);
	ASSERT_TRUE(strstr(first_name + 1, "visible-name.c") == NULL);
	free(out);

	/* No file selected: no per-file buttons; commit applies (staged exists). */
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_STAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_UNSTAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_DISCARD));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_STAGE_ALL));
	ASSERT_EQ_INT(1, git_view_status_bar_has_action(EDITOR_ACTION_GIT_COMMIT));

	/* Group headers get a group-wide stage or unstage button. */
	ASSERT_TRUE(git_view_select_group(0));
	ASSERT_EQ_INT(1, git_view_status_bar_has_action(EDITOR_ACTION_GIT_STAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_UNSTAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_DISCARD));
	ASSERT_TRUE(git_view_select_group(1));
	ASSERT_EQ_INT(1, git_view_status_bar_has_action(EDITOR_ACTION_GIT_UNSTAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_STAGE));

	/* Unstaged file selected: stage + discard, no unstage. */
	ASSERT_TRUE(git_view_select_drawer_file(0));
	ASSERT_EQ_INT(1, git_view_status_bar_has_action(EDITOR_ACTION_GIT_STAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_UNSTAGE));
	ASSERT_EQ_INT(1, git_view_status_bar_has_action(EDITOR_ACTION_GIT_DISCARD));

	/* Staged file selected: unstage, no stage. */
	ASSERT_TRUE(git_view_select_drawer_file(1));
	ASSERT_EQ_INT(1, git_view_status_bar_has_action(EDITOR_ACTION_GIT_UNSTAGE));
	ASSERT_EQ_INT(0, git_view_status_bar_has_action(EDITOR_ACTION_GIT_STAGE));

	/* Back in a plain file tab the buttons disappear. */
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	first_name = strstr(out, "visible-name.c");
	ASSERT_TRUE(first_name != NULL);
	ASSERT_TRUE(strstr(first_name + 1, "visible-name.c") != NULL);
	int action = 0;
	ASSERT_TRUE(!editorStatusBarButtonAt(1, &action));
	free(out);
	editorGitFree();
	return 0;
}

static int test_git_view_drawer_colors_files_by_change_type(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("body");
	E.filename = strdup("visible.c");
	ASSERT_TRUE(E.filename != NULL);
	/* Pin the ANSI terminal theme so status colors are the 16-color escapes. */
	editorThemeInitDefault(&E.theme);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 40;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;

	/* Staged add (A.), unstaged delete (.D), unstaged modify (.M). Toggle the
	 * git drawer while there is no repo root so the toggle's refresh does not
	 * overwrite the injected entries; then point at a repo root so the drawer
	 * colors the rows. */
	static const char status[] = "1 A. N... 100644 100644 100644 aaa bbb added.c\0"
	                             "1 .D N... 100644 100644 100644 aaa bbb deleted.c\0"
	                             "1 .M N... 100644 100644 100644 aaa bbb modified.c\0";
	ASSERT_TRUE(editorGitTestParseStatus(status, sizeof(status) - 1, NULL, NULL));
	ASSERT_TRUE(E.git_repo_root == NULL);
	ASSERT_TRUE(editorDrawerGitToggle());
	E.git_repo_root = strdup("/repo");
	ASSERT_TRUE(E.git_repo_root != NULL);
	/* Keep rows non-inverted so the git status color is emitted. */
	E.drawer_selected_index = -1;

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	/* Added → green, deleted → red, modified → yellow (not all yellow). */
	ASSERT_TRUE(strstr(out, "\x1b[32mA added.c") != NULL);
	ASSERT_TRUE(strstr(out, "\x1b[31mD deleted.c") != NULL);
	ASSERT_TRUE(strstr(out, "\x1b[33mM modified.c") != NULL);
	free(out);
	editorGitFree();
	return 0;
}

static int test_git_view_status_bar_view_context_buttons(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(
	        editorTabOpenGenerated(EDITOR_TAB_GIT_BRANCHES, "git branches", "* main  3d\n"));
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 10;
	E.window_cols = 120;

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "Checkout") != NULL);
	ASSERT_TRUE(strstr(out, "Delete") != NULL);
	/* Hotkeys inherit the theme's status foreground and use italics for emphasis. */
	ASSERT_TRUE(strstr(out, "Checkout \x1b[3menter\x1b[23m") != NULL);
	free(out);

	int action = 0;
	int found_checkout = 0;
	for (int col = 0; col < E.window_cols; col++) {
		if (editorStatusBarButtonAt(col, &action) &&
		    action == (int)EDITOR_ACTION_GIT_VIEW_ACTIVATE) {
			found_checkout = 1;
			break;
		}
	}
	ASSERT_TRUE(found_checkout);
	return 0;
}

static int test_git_view_drawer_action_rows_have_nerd_icons(void) {
	ASSERT_TRUE(editorTabsInit());
	E.nerd_fonts_enabled = 1;
	ASSERT_TRUE(editorDrawerGitToggle());

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	/* Branch (U+F126), history (U+F1DA), and upload (U+F093) glyphs mark the
	 * Branches / Commit log / Push action rows. */
	ASSERT_TRUE(strstr(out, "\xEF\x84\xA6") != NULL);
	ASSERT_TRUE(strstr(out, "\xEF\x87\x9A") != NULL);
	ASSERT_TRUE(strstr(out, "\xEF\x82\x93") != NULL);
	free(out);
	E.nerd_fonts_enabled = 0;
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
	        " NORMAL  Checkout enter   New n    1,1    100%\n"
	        /* golden-end */
	);
	return 0;
}

/* Installs a freshly built single-file diff onto the editor state so the cursor
 * resolver sees a live git diff tab. Takes ownership of the builder outputs. */
static void install_diff_state(const char *patch, const char *repo_root) {
	unsigned char *kinds = NULL;
	int *line_numbers = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text = editorGitViewBuildDiffDup(patch, strlen(patch), &kinds, &line_numbers,
	                                       &kind_count, &source_path);
	free(text);
	free(E.git_view_line_kinds);
	free(E.git_view_line_numbers);
	free(E.git_view_source_path);
	free(E.git_repo_root);
	E.git_view_line_kinds = kinds;
	E.git_view_line_numbers = line_numbers;
	E.git_view_line_kind_count = kind_count;
	E.git_view_source_path = source_path;
	E.git_repo_root = repo_root != NULL ? strdup(repo_root) : NULL;
	E.tab_kind = EDITOR_TAB_GIT_DIFF;
}

static void clear_diff_state(void) {
	free(E.git_view_line_kinds);
	free(E.git_view_line_numbers);
	free(E.git_view_source_path);
	free(E.git_repo_root);
	E.git_view_line_kinds = NULL;
	E.git_view_line_numbers = NULL;
	E.git_view_line_kind_count = 0;
	E.git_view_source_path = NULL;
	E.git_repo_root = NULL;
	E.tab_kind = EDITOR_TAB_FILE;
	E.cy = 0;
}

/* Rows: 0 header, 1 @@, 2 removed(old 101), 3 added(new 201), 4 context(new 202). */
static const char k_resolver_patch[] = "diff --git a/src/app.c b/src/app.c\n"
                                       "--- a/src/app.c\n"
                                       "+++ b/src/app.c\n"
                                       "@@ -101,3 +201,3 @@\n"
                                       "-int old_value = 1;\n"
                                       "+int new_value = 2;\n"
                                       " int kept_value = 3;\n";

static int test_git_view_cursor_source_target_maps_rows(void) {
	install_diff_state(k_resolver_patch, "/repo");
	char *path = NULL;
	int line0 = -1;

	E.cy = 2;
	ASSERT_TRUE(editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_EQ_STR("/repo/src/app.c", path);
	ASSERT_EQ_INT(100, line0);
	free(path);

	E.cy = 3;
	ASSERT_TRUE(editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_EQ_INT(200, line0);
	free(path);

	E.cy = 4;
	ASSERT_TRUE(editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_EQ_INT(201, line0);
	free(path);

	E.cy = 0;
	ASSERT_TRUE(editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_EQ_INT(100, line0);
	free(path);

	clear_diff_state();
	return 0;
}

static int test_git_view_cursor_source_target_relative_without_repo_root(void) {
	install_diff_state(k_resolver_patch, NULL);
	char *path = NULL;
	int line0 = -1;
	E.cy = 3;
	ASSERT_TRUE(editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_EQ_STR("src/app.c", path);
	ASSERT_EQ_INT(200, line0);
	free(path);
	clear_diff_state();
	return 0;
}

static int test_git_view_cursor_source_target_guards(void) {
	char *path = (char *)0x1;
	int line0 = -1;

	install_diff_state(k_resolver_patch, "/repo");

	E.tab_kind = EDITOR_TAB_FILE;
	E.cy = 3;
	path = (char *)0x1;
	ASSERT_TRUE(!editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_TRUE(path == NULL);
	E.tab_kind = EDITOR_TAB_GIT_DIFF;

	free(E.git_view_source_path);
	E.git_view_source_path = NULL;
	path = (char *)0x1;
	ASSERT_TRUE(!editorGitViewCursorSourceTarget(&path, &line0));
	ASSERT_TRUE(path == NULL);

	clear_diff_state();
	return 0;
}

static int test_git_view_status_bar_diff_context_jump_buttons(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, "src/app.c", "context\n"));
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.window_rows = 10;
	E.window_cols = 120;

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "Jump to file") != NULL);
	ASSERT_TRUE(strstr(out, "Open in split") != NULL);
	free(out);

	int action = 0;
	int found_jump = 0;
	int found_split = 0;
	for (int col = 0; col < E.window_cols; col++) {
		if (editorStatusBarButtonAt(col, &action)) {
			if (action == (int)EDITOR_ACTION_GIT_DIFF_JUMP_TO_FILE) {
				found_jump = 1;
			} else if (action == (int)EDITOR_ACTION_GIT_DIFF_OPEN_IN_SPLIT) {
				found_split = 1;
			}
		}
	}
	ASSERT_TRUE(found_jump);
	ASSERT_TRUE(found_split);
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
        {"git_view_build_diff_strips_prefixes_and_tracks_kinds",
         test_git_view_build_diff_strips_prefixes_and_tracks_kinds},
        {"git_view_build_diff_multi_file_has_no_source_path",
         test_git_view_build_diff_multi_file_has_no_source_path},
        {"git_view_build_diff_untracked_uses_new_side_path",
         test_git_view_build_diff_untracked_uses_new_side_path},
        {"git_view_row_bg_color_for_diff_and_headers",
         test_git_view_row_bg_color_for_diff_and_headers},
        {"git_view_row_spans_colorize_views", test_git_view_row_spans_colorize_views},
        {"git_view_drawer_colors_files_by_change_type",
         test_git_view_drawer_colors_files_by_change_type},
        {"git_view_status_bar_shows_ahead_behind", test_git_view_status_bar_shows_ahead_behind},
        {"git_view_status_bar_git_drawer_buttons", test_git_view_status_bar_git_drawer_buttons},
        {"git_view_status_bar_view_context_buttons", test_git_view_status_bar_view_context_buttons},
        {"git_view_drawer_action_rows_have_nerd_icons",
         test_git_view_drawer_action_rows_have_nerd_icons},
        {"git_view_branches_render_golden", test_git_view_branches_render_golden},
        {"git_view_cursor_source_target_maps_rows", test_git_view_cursor_source_target_maps_rows},
        {"git_view_cursor_source_target_relative_without_repo_root",
         test_git_view_cursor_source_target_relative_without_repo_root},
        {"git_view_cursor_source_target_guards", test_git_view_cursor_source_target_guards},
        {"git_view_status_bar_diff_context_jump_buttons",
         test_git_view_status_bar_diff_context_jump_buttons},
};

const int g_git_view_test_count = (int)(sizeof(g_git_view_tests) / sizeof(g_git_view_tests[0]));
