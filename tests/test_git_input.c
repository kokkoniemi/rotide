#include "config/keymap.h"
#include "editing/text_source.h"
#include "editor_test_api.h"
#include "input/actions_file_tab.h"
#include "input/actions_workspace.h"
#include "input/input_system.h"
#include "render/popup.h"
#include "render/status_bar.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "text/document.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
#include "workspace/git_ops.h"
#include "workspace/git_view.h"
#include "workspace/tabs.h"
#include "workspace/task.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h> // NOLINT(misc-include-cleaner)
#include <unistd.h>

/* End-to-end key tests for the Git drawer: raw bytes go through
 * editor_process_keypress_with_input so the dispatch hook, prompts, and both
 * input systems are exercised. Skips when no git binary is available. */

static int git_input_run_cmd(const char *fmt, ...) {
	char cmd[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	if (n <= 0 || n >= (int)sizeof(cmd)) {
		return 0;
	}
	int status = system(cmd);
	return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int git_input_git_available(void) {
	static int checked = 0;
	static int available = 0;
	if (!checked) {
		checked = 1;
		available = git_input_run_cmd("git --version >/dev/null 2>&1");
	}
	return available;
}

#define SKIP_WITHOUT_GIT()                                                                         \
	do {                                                                                       \
		if (!git_input_git_available()) {                                                  \
			(void)fprintf(stderr, "%s: skipped (no git binary)\n", __func__);          \
			return 0;                                                                  \
		}                                                                                  \
	} while (0)

static int git_input_write_file(const char *repo, const char *rel_path, const char *content) {
	char path[512];
	int n = snprintf(path, sizeof(path), "%s/%s", repo, rel_path);
	if (n <= 0 || n >= (int)sizeof(path)) {
		return 0;
	}
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return 0;
	}
	int ok = fputs(content, f) >= 0;
	ok = (fclose(f) == 0) && ok;
	return ok;
}

static char *git_input_repo_create(void) {
	setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
	setenv("GIT_CONFIG_NOSYSTEM", "1", 1);

	char dir_template[] = "/tmp/rotide-test-git-input-XXXXXX";
	char *dir = mkdtemp(dir_template);
	if (dir == NULL) {
		return NULL;
	}
	if (!git_input_run_cmd("git -C '%s' init -q", dir) ||
	    !git_input_run_cmd("git -C '%s' symbolic-ref HEAD refs/heads/main", dir) ||
	    !git_input_run_cmd("git -C '%s' config user.name test", dir) ||
	    !git_input_run_cmd("git -C '%s' config user.email test@test", dir) ||
	    !git_input_write_file(dir, "a.txt", "one\n") ||
	    !git_input_run_cmd("git -C '%s' add a.txt", dir) ||
	    !git_input_run_cmd("git -C '%s' commit -q -m initial", dir)) {
		(void)git_input_run_cmd("rm -rf '%s'", dir);
		return NULL;
	}
	free(E.git_repo_root);
	E.git_repo_root = strdup(dir);
	editorGitRefresh();
	return strdup(dir);
}

static void git_input_repo_destroy(char *repo) {
	if (repo != NULL && strncmp(repo, "/tmp/rotide-test-git-input-", 27) == 0) {
		(void)git_input_run_cmd("rm -rf '%s'", repo);
	}
	free(repo);
	editorGitFree();
}

static const struct editorGitEntry *git_input_find_entry(const char *rel_path) {
	for (int i = 0; i < E.git_entry_count; i++) {
		if (strcmp(E.git_entries[i].rel_path, rel_path) == 0) {
			return &E.git_entries[i];
		}
	}
	return NULL;
}

/* Walks the git drawer's visible rows and selects the row for rel_path. */
static int git_input_select_file_row(const char *rel_path) {
	int visible_count = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible_count; idx++) {
		E.drawer_selected_index = idx;
		int entry_idx = 0;
		if (editorDrawerGitSelectedFile(&entry_idx, NULL) &&
		    strcmp(E.git_entries[entry_idx].rel_path, rel_path) == 0) {
			return 1;
		}
	}
	E.drawer_selected_index = -1;
	return 0;
}

/* Selects the git drawer group header row with the given name. */
static int git_input_select_group_row(const char *name) {
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		struct editorDrawerEntryView view;
		if (!editorDrawerVisibleEntryView(idx, &view)) {
			continue;
		}
		if (view.is_dir && strcmp(view.name, name) == 0) {
			E.drawer_selected_index = idx;
			return 1;
		}
	}
	E.drawer_selected_index = -1;
	return 0;
}

static int git_input_select_action_row(const char *name) {
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		struct editorDrawerEntryView view;
		if (!editorDrawerVisibleEntryView(idx, &view)) {
			continue;
		}
		if (!view.is_dir && strcmp(view.name, name) == 0) {
			E.drawer_selected_index = idx;
			return 1;
		}
	}
	E.drawer_selected_index = -1;
	return 0;
}

/* Feeds decoded keys straight to the active input system: raw ESC bytes in a
 * stdin buffer would be decoded as escape-sequence prefixes, not Esc. */
static int git_input_feed_keys(const char *keys) {
	const struct editorInputSystem *system = editorInputSystemActive();
	if (system == NULL || system->handle_key == NULL) {
		return 0;
	}
	for (const char *p = keys; *p != '\0'; p++) {
		int effects = 0;
		(void)system->handle_key((unsigned char)*p, &effects);
	}
	return 1;
}

static int git_input_setup(const char *system_id, char **repo_out) {
	if (!editorInputSystemActivate(system_id)) {
		return 0;
	}
	if (!editorTabsInit()) {
		return 0;
	}
	char *repo = git_input_repo_create();
	if (repo == NULL) {
		return 0;
	}
	*repo_out = repo;
	return 1;
}

static int git_input_stage_toggle_with_system(const char *system_id) {
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup(system_id, &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));

	ASSERT_TRUE(editor_process_keypress_with_input("s", 1) == 0);
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);

	/* The file moved to the Staged group; stage-toggle there unstages. */
	ASSERT_TRUE(git_input_select_file_row("a.txt"));
	ASSERT_TRUE(editor_process_keypress_with_input("s", 1) == 0);
	entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);
	ASSERT_TRUE(entry->index_status != 'M');

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_stage_toggle_vim(void) {
	SKIP_WITHOUT_GIT();
	return git_input_stage_toggle_with_system("vim");
}

static int test_git_input_stage_toggle_cua(void) {
	SKIP_WITHOUT_GIT();
	return git_input_stage_toggle_with_system("cua");
}

static int test_git_input_group_header_stage_and_unstage(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(git_input_write_file(repo, "b.txt", "new\n"));
	ASSERT_TRUE(editorDrawerGitToggle());

	/* Stage on the Changes header stages that group only. */
	ASSERT_TRUE(git_input_select_group_row("Changes"));
	ASSERT_TRUE(editor_process_keypress_with_input("s", 1) == 0);
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);
	entry = git_input_find_entry("b.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_UNTRACKED, entry->status);

	/* Stage on the Untracked header stages the untracked file too. */
	ASSERT_TRUE(git_input_select_group_row("Untracked"));
	ASSERT_TRUE(editor_process_keypress_with_input("s", 1) == 0);
	entry = git_input_find_entry("b.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('A', entry->index_status);

	/* Unstage on the Staged header unstages everything in it. */
	ASSERT_TRUE(git_input_select_group_row("Staged"));
	ASSERT_TRUE(editor_process_keypress_with_input("u", 1) == 0);
	entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);
	entry = git_input_find_entry("b.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_UNTRACKED, entry->status);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_group_header_menu_stages_group(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(git_input_select_group_row("Changes"));
	ASSERT_TRUE(editorDrawerGitOpenSelectionMenu(2, 2));
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_STR("Stage all", editorPopupSelectedLabel());
	editorDrawerContextMenuActivate();
	ASSERT_TRUE(!editorPopupIsVisible());
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);

	/* Empty groups and Actions rows offer no menu. */
	ASSERT_TRUE(git_input_select_group_row("Conflicts"));
	ASSERT_TRUE(!editorDrawerGitOpenSelectionMenu(2, 2));
	ASSERT_TRUE(git_input_select_group_row("Actions"));
	ASSERT_TRUE(!editorDrawerGitOpenSelectionMenu(2, 2));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_discard_confirm_and_cancel(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "dirty\n"));
	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));

	ASSERT_TRUE(editor_process_keypress_with_input("dn\r", 3) == 0);
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_STR("Discard cancelled", E.statusmsg);

	ASSERT_TRUE(git_input_select_file_row("a.txt"));
	ASSERT_TRUE(editor_process_keypress_with_input("dy\r", 3) == 0);
	ASSERT_TRUE(git_input_find_entry("a.txt") == NULL);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_keys_inert_in_tree_mode(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	editorGitRefresh();

	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	ASSERT_TRUE(editor_process_keypress_with_input("s", 1) == 0);
	editorGitRefresh();
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_TRUE(entry->index_status != 'M');

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_push_without_upstream_can_cancel(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));

	char bare[600];
	(void)snprintf(bare, sizeof(bare), "%s-bare", repo);
	ASSERT_TRUE(git_input_run_cmd("git init -q --bare '%s'", bare));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' remote add origin '%s'", repo, bare));

	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_action_row("Push"));
	ASSERT_TRUE(editor_process_keypress_with_input("\rn\r", 3) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_STR("Push cancelled", E.statusmsg);
	ASSERT_TRUE(!git_input_run_cmd(
	        "git -c safe.bareRepository=all -C '%s' rev-parse -q --verify main >/dev/null",
	        bare));

	ASSERT_TRUE(git_input_run_cmd("rm -rf '%s'", bare));
	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_push_key_runs_task_to_bare_remote(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));

	char bare[600];
	(void)snprintf(bare, sizeof(bare), "%s-bare", repo);
	ASSERT_TRUE(git_input_run_cmd("git init -q --bare '%s'", bare));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' remote add origin '%s'", repo, bare));

	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_action_row("Push"));
	ASSERT_TRUE(editor_process_keypress_with_input("\ry\r", 3) == 0);
	ASSERT_TRUE(editorTaskIsRunning());

	for (int i = 0; i < 1000 && editorTaskIsRunning(); i++) {
		(void)editorTaskPoll();
		usleep(10000);
	}
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_STR("Push finished", E.statusmsg);
	/* -c safe.bareRepository=all so the check works even where the host git
	 * config refuses direct operations on a bare repo. */
	ASSERT_TRUE(git_input_run_cmd(
	        "git -c safe.bareRepository=all -C '%s' rev-parse -q --verify main >/dev/null",
	        bare));
	ASSERT_TRUE(git_input_run_cmd(
	        "test \"$(git -C '%s' rev-parse --abbrev-ref --symbolic-full-name '@{u}')\" "
	        "= origin/main",
	        repo));

	ASSERT_TRUE(git_input_run_cmd("rm -rf '%s'", bare));
	git_input_repo_destroy(repo);
	return 0;
}

/* Returns the first buffer row whose text contains needle, or -1. */
static int git_input_view_row_containing(const char *needle) {
	for (int row = 0; row < E.numrows; row++) {
		size_t line_len = 0;
		char *line = editorDocumentLineDup(E.document, row, &line_len);
		if (line == NULL) {
			continue;
		}
		int match = strstr(line, needle) != NULL;
		free(line);
		if (match) {
			return row;
		}
	}
	return -1;
}

static int git_input_view_contains(const char *needle) {
	return git_input_view_row_containing(needle) >= 0;
}

static int test_git_input_branches_view_checkout_and_new(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorGitOpsBranchCreate("feat"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("B", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_BRANCHES, E.tab_kind);
	ASSERT_TRUE(git_input_view_contains("* feat"));

	int main_row = git_input_view_row_containing("  main");
	ASSERT_TRUE(main_row >= 0);
	E.cy = main_row;
	ASSERT_TRUE(editor_process_keypress_with_input("\r", 1) == 0);
	editorGitRefresh();
	ASSERT_EQ_STR("main", editorGitBranch());
	ASSERT_TRUE(git_input_view_contains("* main"));

	ASSERT_TRUE(editor_process_keypress_with_input("nfeat2\r", 7) == 0);
	editorGitRefresh();
	ASSERT_EQ_STR("feat2", editorGitBranch());
	ASSERT_TRUE(git_input_view_contains("* feat2"));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_branches_view_delete_with_confirm(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorGitOpsBranchCreate("feat"));
	ASSERT_TRUE(editorGitOpsCheckout("main"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("B", 1) == 0);
	int feat_row = git_input_view_row_containing("  feat");
	ASSERT_TRUE(feat_row >= 0);
	E.cy = feat_row;
	ASSERT_TRUE(editor_process_keypress_with_input("dy\r", 3) == 0);
	ASSERT_TRUE(!git_input_view_contains("feat"));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_log_view_tag_and_show(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("L", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_LOG, E.tab_kind);
	int commit_row = git_input_view_row_containing("initial");
	ASSERT_TRUE(commit_row >= 0);
	E.cy = commit_row;

	ASSERT_TRUE(editor_process_keypress_with_input("tv1\r", 4) == 0);
	ASSERT_TRUE(git_input_view_contains("tag: v1"));

	commit_row = git_input_view_row_containing("initial");
	ASSERT_TRUE(commit_row >= 0);
	E.cy = commit_row;
	ASSERT_TRUE(editor_process_keypress_with_input("\r", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);
	ASSERT_TRUE(git_input_view_contains("initial"));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_stash_view_apply_and_drop(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "stash me\n"));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' stash push -q -m wip", repo));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("S", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_STASH, E.tab_kind);
	int stash_row = git_input_view_row_containing("stash@{0}");
	ASSERT_TRUE(stash_row >= 0);
	E.cy = stash_row;

	ASSERT_TRUE(editor_process_keypress_with_input("a", 1) == 0);
	ASSERT_TRUE(git_input_find_entry("a.txt") != NULL);

	stash_row = git_input_view_row_containing("stash@{0}");
	ASSERT_TRUE(stash_row >= 0);
	E.cy = stash_row;
	ASSERT_TRUE(editor_process_keypress_with_input("dy\r", 3) == 0);
	ASSERT_TRUE(git_input_view_contains("(no stashes)"));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_view_readonly_and_header_noop(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorDrawerGitToggle());

	/* The stash view's placeholder row has no actionable entity. */
	ASSERT_TRUE(editor_process_keypress_with_input("S", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_STASH, E.tab_kind);
	E.cy = 0;
	ASSERT_TRUE(editor_process_keypress_with_input("\r", 1) == 0);
	ASSERT_EQ_STR("No entry on this line", E.statusmsg);

	/* Mutating keys are rejected in the read-only view. */
	ASSERT_TRUE(editor_process_keypress_with_input("x", 1) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_diff_tabs_reuse_preview_slot(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(git_input_write_file(repo, "b.txt", "new\n"));
	editorGitRefresh();

	ASSERT_TRUE(editorGitViewOpenDiffForEntry("a.txt", ' ', 'M', 0));
	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);
	ASSERT_TRUE(editorActiveTabIsPreview());
	int tabs_after_first = editorTabCount();

	ASSERT_TRUE(editorGitViewOpenDiffForEntry("b.txt", '?', '?', 0));
	ASSERT_EQ_INT(tabs_after_first, editorTabCount());
	ASSERT_TRUE(strstr(E.tab_title, "b.txt") != NULL);
	ASSERT_TRUE(git_input_view_contains("new"));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_diff_toggle_whole_file(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt",
	                                 "line1\nline2\nline3\nline4\nline5\nline6\nline7\n"
	                                 "line8\nline9\nline10\nline11\nline12\n"));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' add a.txt", repo));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' commit -q -m lines", repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt",
	                                 "line1\nline2\nline3\nline4\nline5\nline6\nline7\n"
	                                 "line8\nline9\nline10\nline11\nCHANGED\n"));
	editorGitRefresh();

	ASSERT_TRUE(editorGitViewOpenDiffForEntry("a.txt", ' ', 'M', 0));
	/* Hunks-only: line2 is outside the -U3 context radius. */
	ASSERT_TRUE(git_input_view_row_containing("line2") == -1);
	ASSERT_TRUE(git_input_view_contains("CHANGED"));

	ASSERT_TRUE(editor_process_keypress_with_input("z", 1) == 0);
	ASSERT_TRUE(git_input_view_row_containing("line2") >= 0);
	ASSERT_TRUE(git_input_view_contains("CHANGED"));

	ASSERT_TRUE(editor_process_keypress_with_input("z", 1) == 0);
	ASSERT_TRUE(git_input_view_row_containing("line2") == -1);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_enter_on_file_row_opens_diff(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	E.window_rows = 20;
	E.window_cols = 120;
	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));

	/* The mouse context-menu path remains available on the same row. */
	struct editorMouseEvent event = {.x = 2,
	                                 .y = E.drawer_selected_index - E.drawer_rowoff + 2};
	ASSERT_TRUE(editorDrawerOpenContextMenuAt(&event, E.window_rows));
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_STR("Open Diff", editorPopupSelectedLabel());
	editorPopupClose();

	ASSERT_TRUE(editor_process_keypress_with_input("\r", 1) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_TRUE(git_input_view_contains("two"));

	git_input_repo_destroy(repo);
	return 0;
}

/* Counts git drawer file rows highlighted as the open diff; reports via
 * rel_active_out whether the row for rel_path is among them. */
static int git_input_active_file_row_count(const char *rel_path, int *rel_active_out) {
	if (rel_active_out != NULL) {
		*rel_active_out = 0;
	}
	int count = 0;
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		struct editorDrawerEntryView view;
		if (!editorDrawerVisibleEntryView(idx, &view) || !view.is_active_file) {
			continue;
		}
		count++;
		if (rel_active_out != NULL && rel_path != NULL && view.name != NULL &&
		    strstr(view.name, rel_path) != NULL) {
			*rel_active_out = 1;
		}
	}
	return count;
}

/* Visible index of the git drawer file row for rel_path, or -1; leaves the
 * selection unchanged. */
static int git_input_file_row_index(const char *rel_path) {
	int saved = E.drawer_selected_index;
	int result = -1;
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		E.drawer_selected_index = idx;
		int entry_idx = 0;
		if (editorDrawerGitSelectedFile(&entry_idx, NULL) &&
		    strcmp(E.git_entries[entry_idx].rel_path, rel_path) == 0) {
			result = idx;
			break;
		}
	}
	E.drawer_selected_index = saved;
	return result;
}

static int test_git_input_open_diff_highlights_file_row(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	/* a.txt: tracked + worktree-modified (Changes); c.txt: untracked. */
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "changed\n"));
	ASSERT_TRUE(git_input_write_file(repo, "c.txt", "brand new\n"));
	editorGitRefresh();
	E.window_rows = 20;
	E.window_cols = 120;
	ASSERT_TRUE(editorDrawerGitToggle());

	/* No diff open yet: nothing highlighted. */
	ASSERT_EQ_INT(0, git_input_active_file_row_count(NULL, NULL));

	/* Opening a.txt's diff highlights only a.txt's row. */
	ASSERT_TRUE(editorGitViewOpenDiffForEntry("a.txt", ' ', 'M', 0));
	int a_active = 0;
	ASSERT_EQ_INT(1, git_input_active_file_row_count("a.txt", &a_active));
	ASSERT_TRUE(a_active);

	/* Opening c.txt's diff moves the highlight to c.txt. */
	ASSERT_TRUE(editorGitViewOpenDiffForEntry("c.txt", '?', '?', 0));
	int c_active = 0;
	ASSERT_EQ_INT(1, git_input_active_file_row_count("c.txt", &c_active));
	ASSERT_TRUE(c_active);

	/* A non-diff tab clears the highlight. */
	editorGitViewOpenBranches();
	ASSERT_EQ_INT(EDITOR_TAB_GIT_BRANCHES, E.tab_kind);
	ASSERT_EQ_INT(0, git_input_active_file_row_count(NULL, NULL));

	git_input_repo_destroy(repo);
	return 0;
}

/* Selects the git drawer file row for rel_path in the Staged (staged_group=1) or
 * Changes (staged_group=0) group; a partially-staged file has one row in each. */
static int git_input_select_row_in_group(const char *rel_path, int staged_group) {
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		E.drawer_selected_index = idx;
		int entry_idx = 0;
		int sg = 0;
		if (editorDrawerGitSelectedFile(&entry_idx, &sg) && sg == staged_group &&
		    strcmp(E.git_entries[entry_idx].rel_path, rel_path) == 0) {
			return 1;
		}
	}
	E.drawer_selected_index = -1;
	return 0;
}

/* 1/0 whether the group's row for rel_path renders as the active diff; -1 if the
 * row is absent. Restores the drawer selection. */
static int git_input_row_active_in_group(const char *rel_path, int staged_group) {
	int saved = E.drawer_selected_index;
	int result = -1;
	int visible = editorDrawerVisibleCount();
	for (int idx = 0; idx < visible; idx++) {
		E.drawer_selected_index = idx;
		int entry_idx = 0;
		int sg = 0;
		if (!editorDrawerGitSelectedFile(&entry_idx, &sg) || sg != staged_group ||
		    strcmp(E.git_entries[entry_idx].rel_path, rel_path) != 0) {
			continue;
		}
		struct editorDrawerEntryView view;
		if (editorDrawerVisibleEntryView(idx, &view)) {
			result = view.is_active_file ? 1 : 0;
		}
		break;
	}
	E.drawer_selected_index = saved;
	return result;
}

/* A partially-staged file opens a distinct diff per drawer group: the Staged row
 * shows the cached index diff, the Changes row the worktree diff, and only the
 * row whose side is currently active highlights. */
static int test_git_input_partially_staged_diffs_are_distinct(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	/* Stage a first change, then add a further worktree change so a.txt lists
	 * under both Staged (index M) and Changes (worktree M). */
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "STAGED\n"));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' add a.txt", repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "STAGED\nWORKTREE\n"));
	editorGitRefresh();
	E.window_rows = 20;
	E.window_cols = 120;
	ASSERT_TRUE(editorDrawerGitToggle());

	/* Staged row -> cached diff, labelled (staged); only that row highlights. */
	ASSERT_TRUE(git_input_select_row_in_group("a.txt", 1));
	ASSERT_TRUE(editorOpenSelectedGitDiff());
	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);
	ASSERT_EQ_INT(EDITOR_GIT_OPS_PATCH_DIFF_CACHED, E.git_view_regen_kind);
	ASSERT_TRUE(strstr(E.tab_title, "(staged)") != NULL);
	ASSERT_TRUE(git_input_view_contains("STAGED"));
	ASSERT_EQ_INT(1, git_input_row_active_in_group("a.txt", 1));
	ASSERT_EQ_INT(0, git_input_row_active_in_group("a.txt", 0));

	/* Pin the staged diff so opening the Changes diff does not reuse the slot. */
	editorTabPinActivePreview();
	int tabs_before = editorTabCount();

	/* Changes row -> worktree diff, labelled (unstaged), in a separate tab; now
	 * only the Changes row highlights. */
	ASSERT_TRUE(git_input_select_row_in_group("a.txt", 0));
	ASSERT_TRUE(editorOpenSelectedGitDiff());
	ASSERT_EQ_INT(EDITOR_GIT_OPS_PATCH_DIFF_WORKTREE, E.git_view_regen_kind);
	ASSERT_TRUE(strstr(E.tab_title, "(unstaged)") != NULL);
	ASSERT_TRUE(git_input_view_contains("WORKTREE"));
	ASSERT_EQ_INT(tabs_before + 1, editorTabCount());
	ASSERT_EQ_INT(1, git_input_row_active_in_group("a.txt", 0));
	ASSERT_EQ_INT(0, git_input_row_active_in_group("a.txt", 1));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_arrow_nav_previews_diff(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	/* Two tracked files, both worktree-modified, so they sit as adjacent rows
	 * under Changes (porcelain lists paths sorted: a.txt then d.txt). */
	ASSERT_TRUE(git_input_write_file(repo, "d.txt", "orig\n"));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' add d.txt", repo));
	ASSERT_TRUE(git_input_run_cmd("git -C '%s' commit -q -m add-d", repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "AAA\n"));
	ASSERT_TRUE(git_input_write_file(repo, "d.txt", "DDD\n"));
	editorGitRefresh();
	E.window_rows = 20;
	E.window_cols = 120;
	ASSERT_TRUE(editorDrawerGitToggle());

	int a_idx = git_input_file_row_index("a.txt");
	int d_idx = git_input_file_row_index("d.txt");
	ASSERT_TRUE(a_idx >= 0 && d_idx >= 0);
	ASSERT_EQ_INT(1, d_idx - a_idx);

	/* Select a.txt then arrow down onto d.txt: the preview follows the
	 * selection, focus stays in the drawer, and the diff tab is reused. */
	E.drawer_selected_index = a_idx;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	ASSERT_TRUE(editor_process_keypress_with_input("\x1b[B", 3) == 0);
	ASSERT_EQ_INT(d_idx, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(E.git_view_regen_arg != NULL && strcmp(E.git_view_regen_arg, "d.txt") == 0);
	ASSERT_TRUE(git_input_view_contains("DDD"));

	/* Arrow back up onto a.txt: the preview follows back. */
	ASSERT_TRUE(editor_process_keypress_with_input("\x1b[A", 3) == 0);
	ASSERT_EQ_INT(a_idx, E.drawer_selected_index);
	ASSERT_TRUE(E.git_view_regen_arg != NULL && strcmp(E.git_view_regen_arg, "a.txt") == 0);
	ASSERT_TRUE(git_input_view_contains("AAA"));

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_context_menu_stage_and_unstage(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));

	/* The right-click menu's second item is Stage for an unstaged file. */
	const char down_enter[] = "\x1b[B\r";
	ASSERT_TRUE(editorDrawerGitOpenSelectionMenu(2, 2));
	ASSERT_TRUE(editor_process_keypress_with_input(down_enter, sizeof(down_enter) - 1) == 0);
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);

	ASSERT_TRUE(git_input_select_file_row("a.txt"));
	ASSERT_TRUE(editorDrawerGitOpenSelectionMenu(2, 2));
	ASSERT_TRUE(editor_process_keypress_with_input(down_enter, sizeof(down_enter) - 1) == 0);
	entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);
	ASSERT_TRUE(entry->index_status != 'M');

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_actions_row_opens_commit_tab(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorDrawerGitToggle());

	int commit_row = -1;
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
		if (strstr(view.name, "Commit staged") != NULL) {
			commit_row = i;
			break;
		}
	}
	ASSERT_TRUE(commit_row > 0);
	E.drawer_selected_index = commit_row;
	ASSERT_TRUE(editor_process_keypress_with_input("\r", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_status_bar_button_click_stages_file(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));

	/* Render once so the button spans are recorded, then click Stage. */
	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	const char *mode = strstr(out, "NORMAL");
	const char *stage = strstr(out, "Stage");
	ASSERT_TRUE(mode != NULL);
	ASSERT_TRUE(stage != NULL);
	ASSERT_TRUE(mode < stage);
	free(out);
	int stage_col = -1;
	for (int col = 0; col < E.window_cols; col++) {
		int action = 0;
		if (editorStatusBarButtonAt(col, &action) &&
		    action == (int)EDITOR_ACTION_GIT_STAGE) {
			stage_col = col;
			break;
		}
	}
	/* " NORMAL " occupies the first eight columns; the Git segment's leading
	 * space puts Stage at column 9. Its clickable span must use that absolute
	 * position rather than the segment-local column 1. */
	ASSERT_EQ_INT(9, stage_col);

	char click[32];
	int written =
	        snprintf(click, sizeof(click), "\x1b[<0;%d;%dM", stage_col + 1, E.window_rows + 2);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(click));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);

	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_commit_via_vim_write(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("c", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	ASSERT_EQ_INT(0, E.dirty);
	size_t text_len = 0;
	char *text = editorDupActiveTextSource(&text_len);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "# Staged changes:") != NULL);
	ASSERT_TRUE(strstr(text, "a.txt") != NULL);
	free(text);

	ASSERT_TRUE(git_input_feed_keys("istaged via vim\x1b"));
	ASSERT_TRUE(editor_process_keypress_with_input(":w\r", 3) == 0);
	ASSERT_TRUE(E.tab_kind != EDITOR_TAB_GIT_COMMIT);
	ASSERT_TRUE(strstr(E.statusmsg, "committed") != NULL);
	char *last = editorGitOpsLastCommitMessageDup();
	ASSERT_TRUE(last != NULL);
	ASSERT_TRUE(strstr(last, "staged via vim") != NULL);
	free(last);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_commit_via_cua_ctrl_s(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("cua", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("c", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);

	const char keys[] = {'s', 't', 'a', 'g', 'e', 'd', ' ', 'c', 'u', 'a', CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input(keys, sizeof(keys)) == 0);
	ASSERT_TRUE(E.tab_kind != EDITOR_TAB_GIT_COMMIT);
	char *last = editorGitOpsLastCommitMessageDup();
	ASSERT_TRUE(last != NULL);
	ASSERT_TRUE(strstr(last, "staged cua") != NULL);
	free(last);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_commit_amend_prefills_last_message(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("A", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	size_t text_len = 0;
	char *text = editorDupActiveTextSource(&text_len);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "initial") != NULL);
	free(text);

	ASSERT_TRUE(editor_process_keypress_with_input(":w\r", 3) == 0);
	ASSERT_TRUE(E.tab_kind != EDITOR_TAB_GIT_COMMIT);
	size_t log_len = 0;
	char *log = editorGitOpsLogRawDup(10, &log_len);
	ASSERT_TRUE(log != NULL);
	int newline_count = 0;
	for (size_t i = 0; i < log_len; i++) {
		if (log[i] == '\n') {
			newline_count++;
		}
	}
	ASSERT_EQ_INT(1, newline_count);
	free(log);

	git_input_repo_destroy(repo);
	return 0;
}

/* Regression: invoking a normal commit while an amend commit tab is already
 * open must produce a normal-mode commit tab, not silently reuse the amend tab.
 * Otherwise the save path would amend when the user asked for a new commit. */
static int test_git_input_commit_mode_tracks_last_invocation(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorDrawerGitToggle());

	/* Amend: prefilled with the last message; amend flag set. */
	ASSERT_TRUE(editor_process_keypress_with_input("A", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	ASSERT_EQ_INT(1, E.git_view_commit_amend);
	size_t amend_len = 0;
	char *amend_text = editorDupActiveTextSource(&amend_len);
	ASSERT_TRUE(amend_text != NULL);
	ASSERT_TRUE(strstr(amend_text, "initial") != NULL);
	free(amend_text);

	/* Normal commit while the amend tab is open: a distinct normal-mode tab
	 * whose message body is empty (no amend prefill). */
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	ASSERT_TRUE(editor_process_keypress_with_input("c", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	ASSERT_EQ_INT(0, E.git_view_commit_amend);
	size_t normal_len = 0;
	char *normal_text = editorDupActiveTextSource(&normal_len);
	ASSERT_TRUE(normal_text != NULL);
	ASSERT_TRUE(strstr(normal_text, "initial") == NULL);
	free(normal_text);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_commit_empty_message_keeps_tab(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("c", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	ASSERT_TRUE(editor_process_keypress_with_input(":w\r", 3) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	ASSERT_TRUE(strstr(E.statusmsg, "empty commit message") != NULL);
	char *last = editorGitOpsLastCommitMessageDup();
	ASSERT_TRUE(last != NULL);
	ASSERT_TRUE(strstr(last, "initial") != NULL);
	free(last);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_commit_close_without_save_aborts(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("c", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	ASSERT_TRUE(git_input_feed_keys("iabandoned\x1b"));
	ASSERT_TRUE(E.dirty != 0);

	editorActionCloseTab();
	ASSERT_EQ_INT(EDITOR_TAB_GIT_COMMIT, E.tab_kind);
	editorActionCloseTab();
	ASSERT_TRUE(E.tab_kind != EDITOR_TAB_GIT_COMMIT);
	ASSERT_EQ_STR("Commit aborted", E.statusmsg);
	char *last = editorGitOpsLastCommitMessageDup();
	ASSERT_TRUE(last != NULL);
	ASSERT_TRUE(strstr(last, "abandoned") == NULL);
	free(last);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_commit_refused_with_clean_tree(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorDrawerGitToggle());

	ASSERT_TRUE(editor_process_keypress_with_input("c", 1) == 0);
	ASSERT_TRUE(E.tab_kind != EDITOR_TAB_GIT_COMMIT);
	ASSERT_EQ_STR("No staged changes to commit", E.statusmsg);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_leader_and_ex_open_views(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));

	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(editor_process_keypress_with_input("B", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_BRANCHES, E.tab_kind);

	ASSERT_TRUE(editor_process_keypress_with_input(":git log\r", 9) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_LOG, E.tab_kind);

	char *completed = vimSystemExCompletionTest("gi", 0);
	ASSERT_TRUE(completed != NULL);
	ASSERT_EQ_STR("git", completed);
	free(completed);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_pending_vim_sequence_precedes_drawer_hotkey(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(git_input_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));

	/* The d belongs to <leader>d; it must not become the Git discard hotkey. */
	ASSERT_TRUE(editor_process_keypress_with_input(" d", 2) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_DAP, E.drawer_mode);
	ASSERT_TRUE(!editorPopupIsVisible());
	const struct editorGitEntry *entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);

	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(git_input_select_file_row("a.txt"));
	/* A register name is also owned by Vim, even when it matches a Git key. */
	ASSERT_TRUE(editor_process_keypress_with_input("\"d", 2) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());
	entry = git_input_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);

	editorGitViewOpenBranches();
	ASSERT_EQ_INT(EDITOR_TAB_GIT_BRANCHES, E.tab_kind);
	ASSERT_TRUE(editor_process_keypress_with_input(" d", 2) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_DAP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	git_input_repo_destroy(repo);
	return 0;
}

static int test_git_input_keymap_cua_accepts_git_names(void) {
	char dir_template[] = "/tmp/rotide-test-git-keymap-XXXXXX";
	char *dir = mkdtemp(dir_template);
	ASSERT_TRUE(dir != NULL);
	char config_path[512];
	ASSERT_TRUE(path_join(config_path, sizeof(config_path), dir, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(config_path, "[keymap.cua]\n"
	                                         "git_branches = \"alt+j\"\n"
	                                         "git_stage = \"alt+u\"\n"));

	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK,
	              editorKeymapLoadFromPaths(&E.keymap, NULL, config_path));
	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&E.keymap, EDITOR_ALT_LETTER_KEY('j'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GIT_BRANCHES, action);
	ASSERT_TRUE(editorKeymapLookupAction(&E.keymap, EDITOR_ALT_LETTER_KEY('u'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GIT_STAGE, action);

	ASSERT_TRUE(unlink(config_path) == 0);
	ASSERT_TRUE(rmdir(dir) == 0);
	return 0;
}

static int test_git_input_mouse_double_click_checks_out_branch(void) {
	SKIP_WITHOUT_GIT();
	char *repo = NULL;
	ASSERT_TRUE(git_input_setup("vim", &repo));
	ASSERT_TRUE(editorGitOpsBranchCreate("feat"));

	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_TRUE(editor_process_keypress_with_input("B", 1) == 0);
	ASSERT_EQ_INT(EDITOR_TAB_GIT_BRANCHES, E.tab_kind);
	int main_row = git_input_view_row_containing("  main");
	ASSERT_TRUE(main_row >= 0);
	ASSERT_TRUE(main_row < E.rowoff + E.window_rows);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	int click_y = main_row - E.rowoff + 2;
	char click[32];
	int written = snprintf(click, sizeof(click), "\x1b[<0;%d;%dM", text_start + 2, click_y);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(click));

	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(main_row, E.cy);
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);

	editorGitRefresh();
	ASSERT_EQ_STR("main", editorGitBranch());

	git_input_repo_destroy(repo);
	return 0;
}

const struct editorTestCase g_git_input_tests[] = {
        {"git_input_stage_toggle_vim", test_git_input_stage_toggle_vim},
        {"git_input_stage_toggle_cua", test_git_input_stage_toggle_cua},
        {"git_input_group_header_stage_and_unstage", test_git_input_group_header_stage_and_unstage},
        {"git_input_group_header_menu_stages_group", test_git_input_group_header_menu_stages_group},
        {"git_input_discard_confirm_and_cancel", test_git_input_discard_confirm_and_cancel},
        {"git_input_keys_inert_in_tree_mode", test_git_input_keys_inert_in_tree_mode},
        {"git_input_push_without_upstream_can_cancel",
         test_git_input_push_without_upstream_can_cancel},
        {"git_input_push_key_runs_task_to_bare_remote",
         test_git_input_push_key_runs_task_to_bare_remote},
        {"git_input_commit_via_vim_write", test_git_input_commit_via_vim_write},
        {"git_input_commit_via_cua_ctrl_s", test_git_input_commit_via_cua_ctrl_s},
        {"git_input_commit_amend_prefills_last_message",
         test_git_input_commit_amend_prefills_last_message},
        {"git_input_commit_mode_tracks_last_invocation",
         test_git_input_commit_mode_tracks_last_invocation},
        {"git_input_commit_empty_message_keeps_tab", test_git_input_commit_empty_message_keeps_tab},
        {"git_input_commit_close_without_save_aborts",
         test_git_input_commit_close_without_save_aborts},
        {"git_input_commit_refused_with_clean_tree", test_git_input_commit_refused_with_clean_tree},
        {"git_input_branches_view_checkout_and_new", test_git_input_branches_view_checkout_and_new},
        {"git_input_branches_view_delete_with_confirm",
         test_git_input_branches_view_delete_with_confirm},
        {"git_input_log_view_tag_and_show", test_git_input_log_view_tag_and_show},
        {"git_input_stash_view_apply_and_drop", test_git_input_stash_view_apply_and_drop},
        {"git_input_view_readonly_and_header_noop", test_git_input_view_readonly_and_header_noop},
        {"git_input_diff_tabs_reuse_preview_slot", test_git_input_diff_tabs_reuse_preview_slot},
        {"git_input_diff_toggle_whole_file", test_git_input_diff_toggle_whole_file},
        {"git_input_enter_on_file_row_opens_diff", test_git_input_enter_on_file_row_opens_diff},
        {"git_input_open_diff_highlights_file_row", test_git_input_open_diff_highlights_file_row},
        {"git_input_partially_staged_diffs_are_distinct",
         test_git_input_partially_staged_diffs_are_distinct},
        {"git_input_arrow_nav_previews_diff", test_git_input_arrow_nav_previews_diff},
        {"git_input_context_menu_stage_and_unstage", test_git_input_context_menu_stage_and_unstage},
        {"git_input_actions_row_opens_commit_tab", test_git_input_actions_row_opens_commit_tab},
        {"git_input_status_bar_button_click_stages_file",
         test_git_input_status_bar_button_click_stages_file},
        {"git_input_leader_and_ex_open_views", test_git_input_leader_and_ex_open_views},
        {"git_input_pending_vim_sequence_precedes_drawer_hotkey",
         test_git_input_pending_vim_sequence_precedes_drawer_hotkey},
        {"git_input_keymap_cua_accepts_git_names", test_git_input_keymap_cua_accepts_git_names},
        {"git_input_mouse_double_click_checks_out_branch",
         test_git_input_mouse_double_click_checks_out_branch},
};

const int g_git_input_test_count = (int)(sizeof(g_git_input_tests) / sizeof(g_git_input_tests[0]));
