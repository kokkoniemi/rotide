#include "config/common.h"
#include "editing/edit.h"
#include "language/lsp.h"
#include "rotide.h"
#include "support/file_io.h"
#include "terminal/terminal_pane.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/workspace_state.h"

#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int find_drawer_entry_path(const char *path, int *idx_out,
                                  struct editorDrawerEntryView *view_out) {
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		if (!editorDrawerVisibleEntryView(i, &view) || view.path == NULL) {
			continue;
		}
		if (strcmp(view.path, path) == 0) {
			if (idx_out != NULL) {
				*idx_out = i;
			}
			if (view_out != NULL) {
				*view_out = view;
			}
			return 1;
		}
	}
	return 0;
}

static int inject_git_entry(const char *rel_path, enum editorGitStatus status, char index_status,
                            char worktree_status) {
	int new_count = E.git_entry_count + 1;
	struct editorGitEntry *grown =
	        realloc(E.git_entries, (size_t)new_count * sizeof(struct editorGitEntry));
	if (grown == NULL) {
		return 0;
	}
	E.git_entries = grown;
	E.git_entry_capacity = new_count;
	char *path_dup = strdup(rel_path);
	if (path_dup == NULL) {
		return 0;
	}
	E.git_entries[E.git_entry_count].rel_path = path_dup;
	E.git_entries[E.git_entry_count].status = status;
	E.git_entries[E.git_entry_count].index_status = index_status;
	E.git_entries[E.git_entry_count].worktree_status = worktree_status;
	E.git_entry_count = new_count;
	return 1;
}

static int test_editor_drawer_root_selection_modes(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	const char *root_path = editorDrawerRootPath();
	ASSERT_TRUE(root_path != NULL);
	ASSERT_EQ_STR(env.project_dir, root_path);

	char src_dir[512];
	char cli_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(path_join(cli_file, sizeof(cli_file), src_dir, "main.c"));
	ASSERT_TRUE(write_text_file(cli_file, "int main(void) { return 0; }\n"));

	char *argv[] = {"rotide", cli_file, NULL};
	ASSERT_TRUE(editorDrawerInitForStartup(2, argv, 0));
	root_path = editorDrawerRootPath();
	ASSERT_TRUE(root_path != NULL);
	ASSERT_EQ_STR(src_dir, root_path);

	ASSERT_TRUE(editorDrawerInitForStartup(2, argv, 1));
	root_path = editorDrawerRootPath();
	ASSERT_TRUE(root_path != NULL);
	ASSERT_EQ_STR(env.project_dir, root_path);

	ASSERT_TRUE(unlink(cli_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char env_file[512];
	char ignore_file[512];
	char main_file[512];
	char link_path[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(env_file, sizeof(env_file), env.project_dir, ".env"));
	ASSERT_TRUE(path_join(ignore_file, sizeof(ignore_file), env.project_dir, ".gitignore"));
	ASSERT_TRUE(path_join(main_file, sizeof(main_file), env.project_dir, "main.c"));
	ASSERT_TRUE(path_join(link_path, sizeof(link_path), env.project_dir, "link_to_src"));

	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(env_file, "A=1\n"));
	ASSERT_TRUE(write_text_file(ignore_file, "*.o\n"));
	ASSERT_TRUE(write_text_file(main_file, "int x;\n"));
	ASSERT_TRUE(symlink("src", link_path) == 0);

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	int src_idx = -1;
	int env_idx = -1;
	int ignore_idx = -1;
	int main_idx = -1;
	int link_idx = -1;
	struct editorDrawerEntryView link_view;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(find_drawer_entry(".env", &env_idx, NULL));
	ASSERT_TRUE(find_drawer_entry(".gitignore", &ignore_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("main.c", &main_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("link_to_src", &link_idx, &link_view));

	ASSERT_TRUE(src_idx >= 0);
	ASSERT_TRUE(src_idx < env_idx);
	ASSERT_TRUE(env_idx < ignore_idx);
	ASSERT_TRUE(ignore_idx < link_idx);
	ASSERT_TRUE(link_idx < main_idx);
	ASSERT_EQ_INT(0, link_view.is_dir);

	ASSERT_TRUE(unlink(link_path) == 0);
	ASSERT_TRUE(unlink(main_file) == 0);
	ASSERT_TRUE(unlink(ignore_file) == 0);
	ASSERT_TRUE(unlink(env_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_expand_collapse_reuses_cached_children(void) {
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
	ASSERT_EQ_INT(src_idx, E.drawer_selected_index);

	int collapsed_count = editorDrawerVisibleCount();
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int expanded_count = editorDrawerVisibleCount();
	ASSERT_TRUE(expanded_count > collapsed_count);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(editorDrawerCollapseSelection(E.window_rows));
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(src_idx, E.drawer_selected_index);

	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	ASSERT_EQ_INT(expanded_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(src_idx, E.drawer_selected_index);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_root_is_not_collapsible(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	int visible_before = editorDrawerVisibleCount();
	ASSERT_TRUE(visible_before >= 2);

	ASSERT_EQ_INT(0, editorDrawerCollapseSelection(E.window_rows));
	ASSERT_EQ_INT(visible_before, editorDrawerVisibleCount());
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	ASSERT_EQ_INT(0, editorDrawerToggleSelectionExpanded(E.window_rows));
	ASSERT_EQ_INT(visible_before, editorDrawerVisibleCount());
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	ASSERT_EQ_INT(1, editorDrawerExpandSelection(E.window_rows));
	ASSERT_EQ_INT(visible_before, editorDrawerVisibleCount());
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_git_file_status_returns_status_for_known_path(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	E.git_repo_root = strdup(env.project_dir);
	ASSERT_TRUE(E.git_repo_root != NULL);
	// Entries must be sorted alphabetically for binary search.
	ASSERT_TRUE(inject_git_entry("a/changed.c", EDITOR_GIT_STATUS_MODIFIED, ' ', 'M'));
	ASSERT_TRUE(inject_git_entry("a/staged.c", EDITOR_GIT_STATUS_MODIFIED, 'M', ' '));
	ASSERT_TRUE(inject_git_entry("b/conflict.c", EDITOR_GIT_STATUS_CONFLICT, 'U', 'U'));
	ASSERT_TRUE(inject_git_entry("c/new.c", EDITOR_GIT_STATUS_UNTRACKED, '?', '?'));

	char path[512];
	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "a/changed.c"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_MODIFIED, editorGitFileStatus(path));

	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "b/conflict.c"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_CONFLICT, editorGitFileStatus(path));

	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "c/new.c"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_UNTRACKED, editorGitFileStatus(path));

	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "missing.c"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_CLEAN, editorGitFileStatus(path));

	editorGitFree();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_git_dir_status_aggregates_worst_descendant(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	E.git_repo_root = strdup(env.project_dir);
	ASSERT_TRUE(E.git_repo_root != NULL);
	ASSERT_TRUE(inject_git_entry("clean_dir/keep.c", EDITOR_GIT_STATUS_MODIFIED, ' ', 'M'));
	ASSERT_TRUE(inject_git_entry("mixed/conflict.c", EDITOR_GIT_STATUS_CONFLICT, 'U', 'U'));
	ASSERT_TRUE(inject_git_entry("mixed/edit.c", EDITOR_GIT_STATUS_MODIFIED, ' ', 'M'));
	ASSERT_TRUE(inject_git_entry("only_new/added.c", EDITOR_GIT_STATUS_UNTRACKED, '?', '?'));

	char path[512];
	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "clean_dir"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_MODIFIED, editorGitDirStatus(path));

	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "mixed"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_CONFLICT, editorGitDirStatus(path));

	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "only_new"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_UNTRACKED, editorGitDirStatus(path));

	ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, "untouched"));
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_CLEAN, editorGitDirStatus(path));

	editorGitFree();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_git_file_status_returns_clean_outside_repo(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	E.git_repo_root = strdup(env.project_dir);
	ASSERT_TRUE(E.git_repo_root != NULL);
	ASSERT_TRUE(inject_git_entry("inside.c", EDITOR_GIT_STATUS_MODIFIED, ' ', 'M'));

	ASSERT_EQ_INT(EDITOR_GIT_STATUS_CLEAN, editorGitFileStatus("/tmp/somewhere/else.c"));

	editorGitFree();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_git_mode_groups_entries_by_status(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	// Inject in alphabetical order; refresh would sort but we bypass it.
	ASSERT_TRUE(inject_git_entry("both.c", EDITOR_GIT_STATUS_MODIFIED, 'M', 'M'));
	ASSERT_TRUE(inject_git_entry("changed.c", EDITOR_GIT_STATUS_MODIFIED, ' ', 'M'));
	ASSERT_TRUE(inject_git_entry("conflict.c", EDITOR_GIT_STATUS_CONFLICT, 'U', 'U'));
	ASSERT_TRUE(inject_git_entry("new.c", EDITOR_GIT_STATUS_UNTRACKED, '?', '?'));
	ASSERT_TRUE(inject_git_entry("staged.c", EDITOR_GIT_STATUS_MODIFIED, 'M', ' '));

	ASSERT_TRUE(editorDrawerGitToggle());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_GIT, E.drawer_mode);

	int visible = editorDrawerVisibleCount();
	// 1 root + 4 groups + 2 staged + 2 changes + 1 untracked + 1 conflict = 11.
	ASSERT_EQ_INT(11, visible);

	struct editorDrawerEntryView view;
	ASSERT_TRUE(editorDrawerVisibleEntryView(0, &view));
	ASSERT_EQ_STR("Git", view.name);
	ASSERT_TRUE(view.is_root);

	ASSERT_TRUE(editorDrawerVisibleEntryView(1, &view));
	ASSERT_EQ_STR("Staged", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(2, &view));
	ASSERT_EQ_STR("M both.c", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(3, &view));
	ASSERT_EQ_STR("M staged.c", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(4, &view));
	ASSERT_EQ_STR("Changes", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(5, &view));
	ASSERT_EQ_STR("M both.c", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(6, &view));
	ASSERT_EQ_STR("M changed.c", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(7, &view));
	ASSERT_EQ_STR("Untracked", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(8, &view));
	ASSERT_EQ_STR("? new.c", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(9, &view));
	ASSERT_EQ_STR("Conflicts", view.name);
	ASSERT_TRUE(editorDrawerVisibleEntryView(10, &view));
	ASSERT_EQ_STR("U conflict.c", view.name);

	editorGitFree();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_git_mode_collapses_group(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(inject_git_entry("a.c", EDITOR_GIT_STATUS_UNTRACKED, '?', '?'));
	ASSERT_TRUE(inject_git_entry("b.c", EDITOR_GIT_STATUS_UNTRACKED, '?', '?'));

	ASSERT_TRUE(editorDrawerGitToggle());
	int expanded_count = editorDrawerVisibleCount();
	// 1 root + 4 groups + 0 staged-placeholder + 0 changes-placeholder + 2 untracked + 0
	// conflicts-placeholder Each empty group with expansion shows a placeholder row (= 1).
	ASSERT_EQ_INT(1 + 4 + 1 + 1 + 2 + 1, expanded_count);

	int untracked_idx = -1;
	for (int i = 0; i < expanded_count; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
		if (strcmp(view.name, "Untracked") == 0) {
			untracked_idx = i;
			break;
		}
	}
	ASSERT_TRUE(untracked_idx > 0);

	ASSERT_TRUE(editorDrawerSelectVisibleIndex(untracked_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerCollapseSelection(E.window_rows + 1));
	int collapsed_count = editorDrawerVisibleCount();
	ASSERT_EQ_INT(expanded_count - 2, collapsed_count);

	editorGitFree();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_git_mode_selects_file_entry(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(inject_git_entry("only.c", EDITOR_GIT_STATUS_UNTRACKED, '?', '?'));

	ASSERT_TRUE(editorDrawerGitToggle());

	int file_idx = -1;
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
		if (strcmp(view.name, "? only.c") == 0) {
			file_idx = i;
			break;
		}
	}
	ASSERT_TRUE(file_idx > 0);

	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));
	int entry_idx = -1;
	ASSERT_TRUE(editorDrawerSelectedGitEntry(&entry_idx));
	ASSERT_EQ_INT(0, entry_idx);

	editorGitFree();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_create_file_under_selected_directory(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));

	ASSERT_TRUE(editorDrawerCreateFileAtSelection("new.txt", E.window_rows + 1));

	char created_path[512];
	ASSERT_TRUE(path_join(created_path, sizeof(created_path), src_dir, "new.txt"));
	struct stat st;
	ASSERT_TRUE(stat(created_path, &st) == 0);
	ASSERT_TRUE(S_ISREG(st.st_mode));

	int new_idx = -1;
	ASSERT_TRUE(find_drawer_entry("new.txt", &new_idx, NULL));
	ASSERT_EQ_INT(new_idx, E.drawer_selected_index);

	ASSERT_EQ_INT(0, editorDrawerCreateFileAtSelection("new.txt", E.window_rows + 1));
	ASSERT_EQ_INT(0, editorDrawerCreateFileAtSelection("bad/name", E.window_rows + 1));

	ASSERT_TRUE(unlink(created_path) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_create_folder_creates_sibling_when_file_selected(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char anchor_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(path_join(anchor_file, sizeof(anchor_file), src_dir, "anchor.txt"));
	ASSERT_TRUE(write_text_file(anchor_file, "anchor\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int anchor_idx = -1;
	ASSERT_TRUE(find_drawer_entry("anchor.txt", &anchor_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(anchor_idx, E.window_rows + 1));

	ASSERT_TRUE(editorDrawerCreateFolderAtSelection("subdir", E.window_rows + 1));

	char created_dir[512];
	ASSERT_TRUE(path_join(created_dir, sizeof(created_dir), src_dir, "subdir"));
	struct stat st;
	ASSERT_TRUE(stat(created_dir, &st) == 0);
	ASSERT_TRUE(S_ISDIR(st.st_mode));

	int sub_idx = -1;
	ASSERT_TRUE(find_drawer_entry("subdir", &sub_idx, NULL));
	ASSERT_EQ_INT(sub_idx, E.drawer_selected_index);

	ASSERT_TRUE(rmdir(created_dir) == 0);
	ASSERT_TRUE(unlink(anchor_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_rename_selection_updates_path_and_selection(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char old_file[512];
	ASSERT_TRUE(path_join(old_file, sizeof(old_file), env.project_dir, "old.txt"));
	ASSERT_TRUE(write_text_file(old_file, "stuff\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int old_idx = -1;
	ASSERT_TRUE(find_drawer_entry("old.txt", &old_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(old_idx, E.window_rows + 1));

	ASSERT_TRUE(editorDrawerRenameSelection("renamed.txt", E.window_rows + 1));

	char new_file[512];
	ASSERT_TRUE(path_join(new_file, sizeof(new_file), env.project_dir, "renamed.txt"));
	struct stat st;
	ASSERT_TRUE(stat(new_file, &st) == 0);
	ASSERT_EQ_INT(0, find_drawer_entry("old.txt", NULL, NULL));
	int new_idx = -1;
	ASSERT_TRUE(find_drawer_entry("renamed.txt", &new_idx, NULL));
	ASSERT_EQ_INT(new_idx, E.drawer_selected_index);

	ASSERT_TRUE(unlink(new_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_rename_selection_rejects_root(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, editorDrawerRenameSelection("anything", E.window_rows + 1));

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_delete_selection_removes_directory_recursively(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char dir_path[512];
	char nested_file[512];
	ASSERT_TRUE(path_join(dir_path, sizeof(dir_path), env.project_dir, "doomed"));
	ASSERT_TRUE(make_dir(dir_path));
	ASSERT_TRUE(path_join(nested_file, sizeof(nested_file), dir_path, "leaf.txt"));
	ASSERT_TRUE(write_text_file(nested_file, "leaf\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int dir_idx = -1;
	ASSERT_TRUE(find_drawer_entry("doomed", &dir_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(dir_idx, E.window_rows + 1));

	ASSERT_TRUE(editorDrawerDeleteSelection(E.window_rows + 1));

	struct stat st;
	ASSERT_TRUE(stat(dir_path, &st) != 0);
	ASSERT_EQ_INT(0, find_drawer_entry("doomed", NULL, NULL));
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_delete_selection_rejects_root(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, editorDrawerDeleteSelection(E.window_rows + 1));

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_open_selected_file_in_new_tab(void) {
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
	ASSERT_TRUE(editorDrawerOpenSelectedFileInTab());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
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

static int test_editor_drawer_open_selected_file_switches_existing_relative_path_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char abs_file[512];
	ASSERT_TRUE(path_join(abs_file, sizeof(abs_file), env.project_dir, "dup.txt"));
	ASSERT_TRUE(write_text_file(abs_file, "dup\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("dup.txt", &file_idx, NULL));

	ASSERT_TRUE(editorTabOpenFileAsNew("dup.txt"));
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("dup.txt", E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInTab());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("dup.txt", E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "dup");

	ASSERT_TRUE(unlink(abs_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_open_selected_file_respects_tab_limit(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "limit.txt"));
	ASSERT_TRUE(write_text_file(open_file, "limit\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("limit.txt", &file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));

	for (int i = 1; i < ROTIDE_MAX_TABS; i++) {
		ASSERT_TRUE(editorTabNewEmpty());
	}
	ASSERT_EQ_INT(ROTIDE_MAX_TABS, editorTabCount());
	add_row("stay");
	int active_before = editorTabActiveIndex();
	int numrows_before = E.numrows;

	ASSERT_TRUE(!editorDrawerOpenSelectedFileInTab());
	ASSERT_EQ_INT(ROTIDE_MAX_TABS, editorTabCount());
	ASSERT_EQ_INT(active_before, editorTabActiveIndex());
	ASSERT_EQ_INT(numrows_before, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "stay");
	ASSERT_TRUE(strstr(E.statusmsg, "Tab limit reached") != NULL);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_file_search_filters_results_in_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char docs_dir[512];
	char main_file[512];
	char readme_file[512];
	char guide_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(docs_dir, sizeof(docs_dir), env.project_dir, "docs"));
	ASSERT_TRUE(path_join(main_file, sizeof(main_file), src_dir, "main.c"));
	ASSERT_TRUE(path_join(readme_file, sizeof(readme_file), env.project_dir, "README.md"));
	ASSERT_TRUE(path_join(guide_file, sizeof(guide_file), docs_dir, "guide.md"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(make_dir(docs_dir));
	ASSERT_TRUE(write_text_file(main_file, "int main(void) { return 0; }\n"));
	ASSERT_TRUE(write_text_file(readme_file, "# Rotide\n"));
	ASSERT_TRUE(write_text_file(guide_file, "guide\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorFileSearchEnter());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);

	struct editorDrawerEntryView header;
	ASSERT_TRUE(editorDrawerVisibleEntryView(0, &header));
	ASSERT_EQ_INT(1, header.is_search_header);
	ASSERT_EQ_STR("", header.name);

	ASSERT_TRUE(editorFileSearchAppendByte('M'));
	ASSERT_TRUE(editorFileSearchAppendByte('A'));
	ASSERT_TRUE(editorFileSearchAppendByte('I'));
	ASSERT_TRUE(editorFileSearchAppendByte('N'));
	ASSERT_EQ_STR("MAIN", editorFileSearchQuery());
	ASSERT_EQ_INT(2, editorDrawerVisibleCount());

	struct editorDrawerEntryView result;
	ASSERT_TRUE(editorDrawerVisibleEntryView(1, &result));
	ASSERT_EQ_STR("src/main.c", result.name);
	ASSERT_EQ_STR(main_file, result.path);
	ASSERT_EQ_INT(1, result.is_selected);

	editorFileSearchExit(0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);

	ASSERT_TRUE(unlink(guide_file) == 0);
	ASSERT_TRUE(unlink(readme_file) == 0);
	ASSERT_TRUE(unlink(main_file) == 0);
	ASSERT_TRUE(rmdir(docs_dir) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_file_search_preview_and_open_selected_file(void) {
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
	ASSERT_TRUE(editorFileSearchEnter());
	ASSERT_TRUE(editorFileSearchAppendByte('b'));
	ASSERT_TRUE(editorFileSearchAppendByte('e'));

	ASSERT_TRUE(editorFileSearchPreviewSelection());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);
	ASSERT_ROW_TEXT_EQ(0, "beta");

	ASSERT_TRUE(editorFileSearchOpenSelectedFileInTab());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
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

static int test_editor_file_search_previews_binary_file_as_unsupported_read_only_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char binary_file[512];
	const char bytes[] = {'r', 'o', 't', 'i', 'd', 'e', '\0', 'b', 'i', 'n'};
	ASSERT_TRUE(path_join(binary_file, sizeof(binary_file), env.project_dir, "rotide"));
	int fd = open(binary_file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, bytes, sizeof(bytes)) == 0);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorFileSearchEnter());
	ASSERT_TRUE(editorFileSearchAppendByte('r'));

	ASSERT_EQ_INT(1, editorFileSearchPreviewSelection());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(binary_file, E.filename);
	ASSERT_TRUE(E.numrows > 0);
	ASSERT_ROW_TEXT_EQ(0, "File is unsupported");
	ASSERT_TRUE(E.is_preview);
	ASSERT_TRUE(editorActiveTabIsUnsupportedFile());
	ASSERT_TRUE(editorActiveTabIsReadOnly());
	ASSERT_TRUE(strstr(E.statusmsg, "Binary files are not supported") == NULL);

	editorSave();
	ASSERT_TRUE(strstr(E.statusmsg, "Unsupported files cannot be saved") != NULL);
	size_t content_len = 0;
	char *contents = read_file_contents(binary_file, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT((int)sizeof(bytes), (int)content_len);
	ASSERT_MEM_EQ(bytes, contents, sizeof(bytes));
	free(contents);

	editorFileSearchExit(0);
	ASSERT_TRUE(unlink(binary_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_file_search_lists_recent_non_active_files_first_and_persists_order(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	char beta_file[512];
	char gamma_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(path_join(gamma_file, sizeof(gamma_file), env.project_dir, "gamma.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));
	ASSERT_TRUE(write_text_file(gamma_file, "gamma\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenFileAsNew(beta_file));
	ASSERT_TRUE(editorTabOpenFileAsNew(alpha_file));
	ASSERT_TRUE(editorWorkspaceStateSave());

	editorWorkspaceStateShutdown();
	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorFileSearchEnter());

	struct editorDrawerEntryView first;
	struct editorDrawerEntryView second;
	struct editorDrawerEntryView third;
	ASSERT_TRUE(editorDrawerVisibleEntryView(1, &first));
	ASSERT_TRUE(editorDrawerVisibleEntryView(2, &second));
	ASSERT_TRUE(editorDrawerVisibleEntryView(3, &third));
	ASSERT_EQ_STR("beta.txt", first.name);
	ASSERT_EQ_STR(beta_file, first.path);
	ASSERT_EQ_STR("alpha.txt", second.name);
	ASSERT_EQ_STR(alpha_file, second.path);
	ASSERT_EQ_STR("gamma.txt", third.name);
	ASSERT_EQ_STR(gamma_file, third.path);

	editorFileSearchExit(0);
	ASSERT_TRUE(unlink(gamma_file) == 0);
	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_project_search_finds_previews_and_opens_matches(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char alpha_file[512];
	char beta_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), src_dir, "beta.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(alpha_file, "intro\nneedle alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "zero needle beta\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorProjectSearchEnter());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);

	struct editorDrawerEntryView header;
	ASSERT_TRUE(editorDrawerVisibleEntryView(0, &header));
	ASSERT_EQ_INT(1, header.is_search_header);
	ASSERT_EQ_STR("", header.name);

	const char *query = "needle";
	for (size_t i = 0; query[i] != '\0'; i++) {
		ASSERT_TRUE(editorProjectSearchAppendByte(query[i]));
	}
	ASSERT_EQ_STR("needle", editorProjectSearchQuery());
	ASSERT_EQ_INT(3, editorDrawerVisibleCount());

	int alpha_idx = -1;
	int beta_idx = -1;
	struct editorDrawerEntryView alpha_view;
	struct editorDrawerEntryView beta_view;
	ASSERT_TRUE(find_drawer_entry_path(alpha_file, &alpha_idx, &alpha_view));
	ASSERT_TRUE(find_drawer_entry_path(beta_file, &beta_idx, &beta_view));
	ASSERT_TRUE(strstr(alpha_view.name, "alpha.txt:2:1: needle alpha") != NULL);
	ASSERT_TRUE(strstr(beta_view.name, "beta.txt:1:6: zero needle beta") != NULL);

	ASSERT_TRUE(editorProjectSearchSelectVisibleIndex(alpha_idx, E.window_rows + 1));
	ASSERT_TRUE(editorProjectSearchPreviewSelection());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(alpha_file, E.filename);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT((int)strlen(query), E.search_match_len);

	ASSERT_TRUE(editorProjectSearchMoveSelectionBy(beta_idx - alpha_idx, E.window_rows + 1));
	ASSERT_TRUE(editorProjectSearchPreviewSelection());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	ASSERT_TRUE(editorProjectSearchOpenSelectedFileInTab());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(0, editorActiveTabIsPreview());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(beta_file, E.filename);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	int beta_drawer_idx = -1;
	ASSERT_TRUE(find_drawer_entry("beta.txt", &beta_drawer_idx, NULL));
	ASSERT_EQ_INT(beta_drawer_idx, E.drawer_selected_index);

	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_path_absolute_dup_makes_relative_paths_absolute(void) {
	int failed = 1;
	char *original_cwd = getcwd(NULL, 0);
	char root_template[] = "/tmp/rotide-test-abs-path-XXXXXX";
	char *root_path = mkdtemp(root_template);
	char nested_dir[512] = "";
	char nested_file[512] = "";
	char *absolute = NULL;

	if (original_cwd == NULL || root_path == NULL ||
	    !path_join(nested_dir, sizeof(nested_dir), root_path, "nested") ||
	    !make_dir(nested_dir) ||
	    !path_join(nested_file, sizeof(nested_file), nested_dir, "file.c") ||
	    !write_text_file(nested_file, "int main(void) { return 0; }\n") ||
	    chdir(root_path) != 0) {
		goto cleanup;
	}

	absolute = editorPathAbsoluteDup("nested/file.c");
	if (absolute == NULL || strcmp(nested_file, absolute) != 0) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (original_cwd != NULL) {
		if (chdir(original_cwd) != 0) {
			failed = 1;
		}
	}
	free(absolute);
	if (nested_file[0] != '\0') {
		(void)unlink(nested_file);
	}
	if (nested_dir[0] != '\0') {
		(void)rmdir(nested_dir);
	}
	if (root_path != NULL) {
		(void)rmdir(root_path);
	}
	free(original_cwd);
	return failed;
}

static int test_editor_path_find_marker_upward_returns_project_root(void) {
	char root_template[] = "/tmp/rotide-test-path-marker-XXXXXX";
	char *root_path = mkdtemp(root_template);
	ASSERT_TRUE(root_path != NULL);

	char src_dir[512];
	char nested_dir[512];
	char marker_path[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), root_path, "src"));
	ASSERT_TRUE(path_join(nested_dir, sizeof(nested_dir), src_dir, "inner"));
	ASSERT_TRUE(
	        path_join(marker_path, sizeof(marker_path), root_path, "compile_commands.json"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(make_dir(nested_dir));
	ASSERT_TRUE(write_text_file(marker_path, "[]\n"));

	static const char *const markers[] = {"compile_commands.json", ".git"};
	char *workspace_root = editorPathFindMarkerUpward(nested_dir, markers,
	                                                  sizeof(markers) / sizeof(markers[0]));
	ASSERT_TRUE(workspace_root != NULL);
	ASSERT_EQ_STR(root_path, workspace_root);

	free(workspace_root);
	ASSERT_TRUE(unlink(marker_path) == 0);
	ASSERT_TRUE(rmdir(nested_dir) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	ASSERT_TRUE(rmdir(root_path) == 0);
	return 0;
}

static int test_editor_recovery_snapshot_permissions_are_0600(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("perm-check");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	struct stat st;
	ASSERT_TRUE(stat(recovery_path, &st) == 0);
	ASSERT_EQ_INT(0600, st.st_mode & 0777);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_clean_quit_removes_snapshot(void) {
	struct recoveryTestEnv env;
	const char *recovery_path = NULL;
	pid_t pid = -1;
	int status = 0;
	int result = 1;

	if (!setup_recovery_test_env(&env)) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "setup_recovery_test_env(&env)");
		goto cleanup;
	}
	if (!editorTabsInit()) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "editorTabsInit()");
		goto cleanup;
	}

	add_row("quit-cleanup");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	recovery_path = editorRecoveryPath();
	if (recovery_path == NULL) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "recovery_path != NULL");
		goto cleanup;
	}
	if (access(recovery_path, F_OK) != 0) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "access(recovery_path, F_OK) == 0");
		goto cleanup;
	}

	E.dirty = 0;
	pid = fork();
	if (pid == -1) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "pid != -1");
		goto cleanup;
	}
	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(151);
		}
		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(152);
		}
		_exit(153);
	}

	if (wait_for_child_exit_with_timeout(pid, 1500, &status) != 0) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "wait_for_child_exit_with_timeout(pid, 1500, &status) == 0");
		goto cleanup;
	}
	pid = -1;
	if (!WIFEXITED(status)) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "WIFEXITED(status)");
		goto cleanup;
	}
	if (WEXITSTATUS(status) != EXIT_SUCCESS) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: expected %d, got %d\n", __func__,
		              __LINE__, EXIT_SUCCESS, WEXITSTATUS(status));
		goto cleanup;
	}
	if (access(recovery_path, F_OK) != -1) {
		(void)fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
		              "access(recovery_path, F_OK) == -1");
		goto cleanup;
	}

	result = 0;

cleanup:
	if (pid > 0) {
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	cleanup_recovery_test_env(&env);
	return result;
}

static int test_editor_recovery_failure_exit_keeps_snapshot(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("keep-on-failure");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);
	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(161);
		}
		if (editor_process_keypress_with_input("", 0) == -1) {
			_exit(162);
		}
		_exit(163);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_autosave_persists_workspace_state(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;

	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);
	ASSERT_TRUE(access(state_path, F_OK) == -1);

	E.drawer_mode = EDITOR_DRAWER_MODE_GIT;
	E.drawer_collapsed = 1;

	/* No dirty buffers — the activity hook should still persist workspace
	 * state so layout/drawer changes survive a crash. */
	E.dirty = 0;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	ASSERT_TRUE(access(state_path, F_OK) == 0);

	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	E.drawer_collapsed = 0;

	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_GIT, (int)E.drawer_mode);
	ASSERT_EQ_INT(1, E.drawer_collapsed);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_persists_drawer_state(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorWorkspaceStatePath() != NULL);

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;

	ASSERT_TRUE(editorDrawerSetWidthForCols(42, E.window_cols) || E.drawer_width_cols == 42);
	(void)editorDrawerSetCollapsed(1);
	E.drawer_mode = EDITOR_DRAWER_MODE_GIT;

	ASSERT_TRUE(editorWorkspaceStateSave());

	E.drawer_width_cols = 20;
	E.drawer_width_user_set = 0;
	E.drawer_collapsed = 0;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;

	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));

	ASSERT_EQ_INT(42, E.drawer_width_cols);
	ASSERT_EQ_INT(1, E.drawer_collapsed);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_GIT, (int)E.drawer_mode);

	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_persists_drawer_expanded_masks(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;
	E.drawer_menu_expanded = 0x5u;
	E.drawer_git_expanded = 0x2u;
	E.drawer_lsp_expanded = 0x3u;

	ASSERT_TRUE(editorWorkspaceStateSave());

	E.drawer_menu_expanded = 0;
	E.drawer_git_expanded = 0;
	E.drawer_lsp_expanded = 0;

	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));

	ASSERT_EQ_INT(0x5, (int)E.drawer_menu_expanded);
	ASSERT_EQ_INT(0x2, (int)E.drawer_git_expanded);
	ASSERT_EQ_INT(0x3, (int)E.drawer_lsp_expanded);

	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_ignores_search_modes_on_save(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;

	E.drawer_mode = EDITOR_DRAWER_MODE_FILE_SEARCH;
	ASSERT_TRUE(editorWorkspaceStateSave());

	E.drawer_mode = EDITOR_DRAWER_MODE_GIT;
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, (int)E.drawer_mode);

	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_rejects_unknown_future_version(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;
	E.drawer_width_cols = 50;
	E.drawer_collapsed = 0;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;

	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);
	ASSERT_TRUE(write_text_file(state_path, "version=9999\n"
	                                        "drawer_width_cols=12\n"
	                                        "drawer_collapsed=1\n"
	                                        "drawer_mode=git\n"));

	int loaded = editorWorkspaceStateLoadAndApply(E.window_cols, 0);
	ASSERT_EQ_INT(0, loaded);
	/* Bailing out must leave the in-memory state untouched. */
	ASSERT_EQ_INT(50, E.drawer_width_cols);
	ASSERT_EQ_INT(0, E.drawer_collapsed);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, (int)E.drawer_mode);

	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_load_missing_is_noop(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;
	E.drawer_width_cols = 35;
	E.drawer_collapsed = 0;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;

	int loaded = editorWorkspaceStateLoadAndApply(E.window_cols, 0);
	ASSERT_EQ_INT(0, loaded);
	ASSERT_EQ_INT(35, E.drawer_width_cols);

	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_restores_open_tabs_with_cursor(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	char beta_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\nsecond\nthird\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta line\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenFileAsNew(alpha_file));
	E.cy = 1;
	E.cx = 3;
	ASSERT_TRUE(editorTabOpenFileAsNew(beta_file));
	E.cy = 0;
	E.cx = 4;
	ASSERT_TRUE(editorWorkspaceStateSave());

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR(beta_file, editorTabFilenameAt(E.active_tab));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);

	int alpha_idx = -1;
	for (int i = 0; i < editorTabCount(); i++) {
		const char *path = editorTabFilenameAt(i);
		if (path != NULL && strcmp(path, alpha_file) == 0) {
			alpha_idx = i;
			break;
		}
	}
	ASSERT_TRUE(alpha_idx >= 0);
	ASSERT_TRUE(editorTabSwitchToIndex(alpha_idx));
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_restore_defers_lsp_for_inactive_tabs(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_c[512];
	char beta_c[512];
	ASSERT_TRUE(path_join(alpha_c, sizeof(alpha_c), env.project_dir, "alpha.c"));
	ASSERT_TRUE(path_join(beta_c, sizeof(beta_c), env.project_dir, "beta.c"));
	ASSERT_TRUE(write_text_file(alpha_c, "int main(void) { return 0; }\n"));
	ASSERT_TRUE(write_text_file(beta_c, "void helper(void) {}\n"));

	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenFileAsNew(alpha_c));
	ASSERT_TRUE(editorTabOpenFileAsNew(beta_c));
	ASSERT_TRUE(editorWorkspaceStateSave());

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());
	ASSERT_EQ_INT(2, editorTabCount());

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_STR(beta_c, editorTabFilenameAt(E.active_tab));

	int alpha_idx = -1;
	for (int i = 0; i < editorTabCount(); i++) {
		const char *path = editorTabFilenameAt(i);
		if (path != NULL && strcmp(path, alpha_c) == 0) {
			alpha_idx = i;
			break;
		}
	}
	ASSERT_TRUE(alpha_idx >= 0);
	ASSERT_TRUE(editorTabSwitchToIndex(alpha_idx));
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.did_open_count);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	ASSERT_TRUE(unlink(beta_c) == 0);
	ASSERT_TRUE(unlink(alpha_c) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_restores_tabs_to_split_panes(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	char beta_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());

	/* Open alpha in the bootstrap leaf, split, then open beta in the new leaf
	 * so the two panes hold different active tabs. */
	ASSERT_TRUE(editorTabOpenFileAsNew(alpha_file));
	struct editorPaneNode *first_leaf = E.focused_leaf;
	ASSERT_TRUE(first_leaf != NULL);

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);
	ASSERT_TRUE(E.focused_leaf == sibling);

	ASSERT_TRUE(editorTabOpenFileAsNew(beta_file));
	ASSERT_EQ_INT(2, editorTabCount());

	/* Sanity-check pre-save layout: first leaf has alpha only, sibling has
	 * alpha + beta with beta active. */
	int alpha_idx = -1;
	int beta_idx = -1;
	for (int i = 0; i < editorTabCount(); i++) {
		const char *path = editorTabFilenameAt(i);
		if (path == NULL) {
			continue;
		}
		if (strcmp(path, alpha_file) == 0) {
			alpha_idx = i;
		} else if (strcmp(path, beta_file) == 0) {
			beta_idx = i;
		}
	}
	ASSERT_TRUE(alpha_idx >= 0 && beta_idx >= 0);
	ASSERT_EQ_INT(1, first_leaf->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(alpha_idx, first_leaf->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(2, sibling->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(beta_idx, sibling->as.leaf.view.active_tab_idx);

	ASSERT_TRUE(editorWorkspaceStateSave());

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	editorPaneNodeFree(E.layout_root);
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	ASSERT_TRUE(E.layout_root != NULL);
	E.focused_leaf = E.layout_root;

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	int restored_alpha = -1;
	int restored_beta = -1;
	for (int i = 0; i < editorTabCount(); i++) {
		const char *path = editorTabFilenameAt(i);
		if (path == NULL) {
			continue;
		}
		if (strcmp(path, alpha_file) == 0) {
			restored_alpha = i;
		} else if (strcmp(path, beta_file) == 0) {
			restored_beta = i;
		}
	}
	ASSERT_TRUE(restored_alpha >= 0 && restored_beta >= 0);

	ASSERT_TRUE(E.layout_root != NULL && E.layout_root->is_split);
	struct editorPaneNode *restored_first = E.layout_root->as.split.first;
	struct editorPaneNode *restored_second = E.layout_root->as.split.second;
	ASSERT_TRUE(restored_first != NULL && !restored_first->is_split);
	ASSERT_TRUE(restored_second != NULL && !restored_second->is_split);

	ASSERT_EQ_INT(1, restored_first->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(restored_alpha, restored_first->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(restored_alpha, restored_first->as.leaf.view.active_tab_idx);

	ASSERT_EQ_INT(2, restored_second->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(restored_beta, restored_second->as.leaf.view.active_tab_idx);

	ASSERT_TRUE(E.focused_leaf == restored_second);
	ASSERT_EQ_STR(beta_file, editorTabFilenameAt(E.active_tab));

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_restores_three_leaf_split_assignment(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	char beta_file[512];
	char gamma_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(path_join(gamma_file, sizeof(gamma_file), env.project_dir, "gamma.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));
	ASSERT_TRUE(write_text_file(gamma_file, "gamma\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());

	ASSERT_TRUE(editorTabOpenFileAsNew(alpha_file));
	struct editorPaneNode *first_leaf = E.focused_leaf;
	ASSERT_TRUE(first_leaf != NULL);
	struct editorPaneNode *second = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(second != NULL);
	ASSERT_TRUE(editorTabOpenFileAsNew(beta_file));
	struct editorPaneNode *third = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(third != NULL);
	ASSERT_TRUE(editorTabOpenFileAsNew(gamma_file));

	ASSERT_EQ_INT(3, editorPaneTreeLeafCount(E.layout_root));
	int gamma_idx = -1;
	for (int i = 0; i < editorTabCount(); i++) {
		const char *path = editorTabFilenameAt(i);
		if (path != NULL && strcmp(path, gamma_file) == 0) {
			gamma_idx = i;
		}
	}
	ASSERT_TRUE(gamma_idx >= 0);

	/* Focus the second (middle) leaf before saving so we can verify focused_pane= */
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(second));

	ASSERT_TRUE(editorWorkspaceStateSave());

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	editorPaneNodeFree(E.layout_root);
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	ASSERT_TRUE(E.layout_root != NULL);
	E.focused_leaf = E.layout_root;

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());

	ASSERT_EQ_INT(3, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.focused_leaf != NULL);
	const char *focused_name = editorTabFilenameAt(E.focused_leaf->as.leaf.view.active_tab_idx);
	ASSERT_TRUE(focused_name != NULL);
	ASSERT_EQ_STR(beta_file, focused_name);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(gamma_file) == 0);
	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_clamps_focused_pane_out_of_range(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	char beta_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);

	/* Hand-craft a state file that claims a 2-leaf layout but a focused_pane
	 * that doesn't exist. Restore should clamp focus to leaf 0 instead of
	 * crashing or leaving E.focused_leaf NULL. */
	char buf[1024];
	int n = snprintf(buf, sizeof(buf),
	                 "version=1\n"
	                 "tab=0|0|%s\n"
	                 "tab=0|0|%s\n"
	                 "layout=(v 0.5 leaf leaf)\n"
	                 "pane_tab=0|1|%s\n"
	                 "pane_tab=1|1|%s\n"
	                 "focused_pane=7\n",
	                 alpha_file, beta_file, alpha_file, beta_file);
	ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
	ASSERT_TRUE(write_text_file(state_path, buf));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());

	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root != NULL && E.layout_root->is_split);
	ASSERT_TRUE(E.focused_leaf == E.layout_root->as.split.first);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_ignores_orphan_pane_tab_paths(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	char beta_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(path_join(beta_file, sizeof(beta_file), env.project_dir, "beta.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));
	ASSERT_TRUE(write_text_file(beta_file, "beta\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);

	/* pane_tab references a path that has no matching tab= line. The orphan
	 * must be skipped silently without affecting the other pane's assignment. */
	char buf[1024];
	int n = snprintf(buf, sizeof(buf),
	                 "version=1\n"
	                 "tab=0|0|%s\n"
	                 "tab=0|0|%s\n"
	                 "layout=(v 0.5 leaf leaf)\n"
	                 "pane_tab=0|1|%s\n"
	                 "pane_tab=1|1|/nonexistent/ghost.txt\n"
	                 "pane_tab=1|0|%s\n"
	                 "focused_pane=1\n",
	                 alpha_file, beta_file, alpha_file, beta_file);
	ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
	ASSERT_TRUE(write_text_file(state_path, buf));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	struct editorPaneNode *second_leaf = E.layout_root->as.split.second;
	ASSERT_TRUE(second_leaf != NULL && !second_leaf->is_split);
	/* The orphan was dropped; only the beta entry survived on leaf 1. */
	ASSERT_EQ_INT(1, second_leaf->as.leaf.view.pane_tab_count);
	const char *focused_name = editorTabFilenameAt(second_leaf->as.leaf.view.active_tab_idx);
	ASSERT_TRUE(focused_name != NULL);
	ASSERT_EQ_STR(beta_file, focused_name);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(beta_file) == 0);
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static char *read_whole_file(const char *path, size_t *len_out) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		return NULL;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		(void)fclose(fp);
		return NULL;
	}
	long sz = ftell(fp);
	if (sz < 0) {
		(void)fclose(fp);
		return NULL;
	}
	rewind(fp);
	char *buf = malloc((size_t)sz + 1);
	if (buf == NULL) {
		(void)fclose(fp);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)sz, fp);
	(void)fclose(fp);
	buf[got] = '\0';
	if (len_out != NULL) {
		*len_out = got;
	}
	return buf;
}

static int test_editor_workspace_state_save_emits_term_token_and_version_two(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.window_cols = 100;
	E.window_rows = 40;

	/* Drive the production path: a horizontal terminal split off the
	 * focused (editor) leaf produces exactly (h 0.5 leaf term). */
	struct editorPaneNode *term_leaf =
	        editorTerminalPaneOpenSplit("sleep 2", EDITOR_SPLIT_HORIZONTAL);
	ASSERT_TRUE(term_leaf != NULL);

	ASSERT_TRUE(editorWorkspaceStateSave());

	size_t file_len = 0;
	char *contents = read_whole_file(editorWorkspaceStatePath(), &file_len);
	ASSERT_TRUE(contents != NULL);
	int has_version = strstr(contents, "version=2\n") != NULL;
	int has_term = strstr(contents, "layout=(h ") != NULL && strstr(contents, "term") != NULL;
	free(contents);
	ASSERT_TRUE(has_version);
	ASSERT_TRUE(has_term);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_restore_hydrates_terminal_placeholder(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);

	char buf[1024];
	int n = snprintf(buf, sizeof(buf),
	                 "version=2\n"
	                 "tab=0|0|%s\n"
	                 "layout=(h 0.5 leaf term)\n"
	                 "pane_tab=0|1|%s\n"
	                 "focused_pane=0\n",
	                 alpha_file, alpha_file);
	ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
	ASSERT_TRUE(write_text_file(state_path, buf));

	E.window_cols = 100;
	E.window_rows = 40;
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	ASSERT_TRUE(editorWorkspaceStateRestoreTabs());

	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root != NULL && E.layout_root->is_split);
	struct editorPaneNode *term_leaf = E.layout_root->as.split.second;
	ASSERT_TRUE(term_leaf != NULL && !term_leaf->is_split);
	ASSERT_EQ_INT(EDITOR_PANE_KIND_TERMINAL, (int)term_leaf->as.leaf.kind);
	ASSERT_TRUE(term_leaf->as.leaf.kind_state != NULL);
	ASSERT_TRUE(term_leaf->as.leaf.kind_state_free == editorTerminalPaneFree);
	/* Pumping a freshly hydrated terminal must not crash. */
	(void)editorTerminalPanePumpAll(E.layout_root);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_restore_terminal_only_layout(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);
	ASSERT_TRUE(write_text_file(state_path, "version=2\n"
	                                        "layout=term\n"));

	E.window_cols = 100;
	E.window_rows = 40;
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 0));
	/* RestoreTabs returns opened_any; with zero tab= lines it returns 0 but
	 * must still hydrate the terminal leaf via the early-return path. */
	(void)editorWorkspaceStateRestoreTabs();

	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root != NULL && !E.layout_root->is_split);
	ASSERT_EQ_INT(EDITOR_PANE_KIND_TERMINAL, (int)E.layout_root->as.leaf.kind);
	ASSERT_TRUE(E.layout_root->as.leaf.kind_state != NULL);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_workspace_state_reset_panes_skips_saved_layout(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char alpha_file[512];
	ASSERT_TRUE(path_join(alpha_file, sizeof(alpha_file), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(alpha_file, "alpha\n"));

	ASSERT_TRUE(editorWorkspaceStateInitForCurrentDir());
	const char *state_path = editorWorkspaceStatePath();
	ASSERT_TRUE(state_path != NULL);

	char buf[1024];
	int n = snprintf(buf, sizeof(buf),
	                 "version=2\n"
	                 "drawer_width_cols=20\n"
	                 "tab=0|0|%s\n"
	                 "layout=(h 0.5 leaf term)\n"
	                 "pane_tab=0|1|%s\n"
	                 "focused_pane=0\n",
	                 alpha_file, alpha_file);
	ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
	ASSERT_TRUE(write_text_file(state_path, buf));

	E.window_cols = 100;
	E.window_rows = 40;
	ASSERT_TRUE(editorTabsInit());

	/* reset_panes=1 simulates the "file argument on startup" path: the
	 * fresh single-leaf layout must survive, otherwise we'd resurrect an
	 * unhydrated terminal placeholder and keystrokes would silently land on
	 * the editor leaf instead. */
	ASSERT_TRUE(editorWorkspaceStateLoadAndApply(E.window_cols, 1));

	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root != NULL && !E.layout_root->is_split);
	ASSERT_EQ_INT(EDITOR_PANE_KIND_EDITOR, (int)E.layout_root->as.leaf.kind);
	/* Drawer settings are still applied — only layout/tab/pane state is
	 * skipped under reset_panes. */
	ASSERT_EQ_INT(20, E.drawer_width_cols);

	editorTabsFreeAll();
	editorWorkspaceStateShutdown();
	ASSERT_TRUE(unlink(alpha_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

const struct editorTestCase g_workspace_persistence_tests[] = {
        {"editor_drawer_root_selection_modes", test_editor_drawer_root_selection_modes},
        {"editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file",
         test_editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file},
        {"editor_drawer_expand_collapse_reuses_cached_children",
         test_editor_drawer_expand_collapse_reuses_cached_children},
        {"editor_drawer_root_is_not_collapsible", test_editor_drawer_root_is_not_collapsible},
        {"editor_git_file_status_returns_status_for_known_path",
         test_editor_git_file_status_returns_status_for_known_path},
        {"editor_git_dir_status_aggregates_worst_descendant",
         test_editor_git_dir_status_aggregates_worst_descendant},
        {"editor_git_file_status_returns_clean_outside_repo",
         test_editor_git_file_status_returns_clean_outside_repo},
        {"editor_drawer_git_mode_groups_entries_by_status",
         test_editor_drawer_git_mode_groups_entries_by_status},
        {"editor_drawer_git_mode_collapses_group", test_editor_drawer_git_mode_collapses_group},
        {"editor_drawer_git_mode_selects_file_entry",
         test_editor_drawer_git_mode_selects_file_entry},
        {"editor_drawer_create_file_under_selected_directory",
         test_editor_drawer_create_file_under_selected_directory},
        {"editor_drawer_create_folder_creates_sibling_when_file_selected",
         test_editor_drawer_create_folder_creates_sibling_when_file_selected},
        {"editor_drawer_rename_selection_updates_path_and_selection",
         test_editor_drawer_rename_selection_updates_path_and_selection},
        {"editor_drawer_rename_selection_rejects_root",
         test_editor_drawer_rename_selection_rejects_root},
        {"editor_drawer_delete_selection_removes_directory_recursively",
         test_editor_drawer_delete_selection_removes_directory_recursively},
        {"editor_drawer_delete_selection_rejects_root",
         test_editor_drawer_delete_selection_rejects_root},
        {"editor_drawer_open_selected_file_in_new_tab",
         test_editor_drawer_open_selected_file_in_new_tab},
        {"editor_drawer_open_selected_file_switches_existing_relative_path_tab",
         test_editor_drawer_open_selected_file_switches_existing_relative_path_tab},
        {"editor_drawer_open_selected_file_respects_tab_limit",
         test_editor_drawer_open_selected_file_respects_tab_limit},
        {"editor_file_search_filters_results_in_drawer",
         test_editor_file_search_filters_results_in_drawer},
        {"editor_file_search_preview_and_open_selected_file",
         test_editor_file_search_preview_and_open_selected_file},
        {"editor_file_search_previews_binary_file_as_unsupported_read_only_tab",
         test_editor_file_search_previews_binary_file_as_unsupported_read_only_tab},
        {"editor_file_search_lists_recent_non_active_files_first_and_persists_order",
         test_editor_file_search_lists_recent_non_active_files_first_and_persists_order},
        {"editor_project_search_finds_previews_and_opens_matches",
         test_editor_project_search_finds_previews_and_opens_matches},
        {"editor_path_absolute_dup_makes_relative_paths_absolute",
         test_editor_path_absolute_dup_makes_relative_paths_absolute},
        {"editor_path_find_marker_upward_returns_project_root",
         test_editor_path_find_marker_upward_returns_project_root},
        {"editor_recovery_snapshot_permissions_are_0600",
         test_editor_recovery_snapshot_permissions_are_0600},
        {"editor_recovery_clean_quit_removes_snapshot",
         test_editor_recovery_clean_quit_removes_snapshot},
        {"editor_recovery_failure_exit_keeps_snapshot",
         test_editor_recovery_failure_exit_keeps_snapshot},
        {"editor_recovery_autosave_persists_workspace_state",
         test_editor_recovery_autosave_persists_workspace_state},
        {"editor_workspace_state_persists_drawer_state",
         test_editor_workspace_state_persists_drawer_state},
        {"editor_workspace_state_persists_drawer_expanded_masks",
         test_editor_workspace_state_persists_drawer_expanded_masks},
        {"editor_workspace_state_ignores_search_modes_on_save",
         test_editor_workspace_state_ignores_search_modes_on_save},
        {"editor_workspace_state_load_missing_is_noop",
         test_editor_workspace_state_load_missing_is_noop},
        {"editor_workspace_state_rejects_unknown_future_version",
         test_editor_workspace_state_rejects_unknown_future_version},
        {"editor_workspace_state_restores_open_tabs_with_cursor",
         test_editor_workspace_state_restores_open_tabs_with_cursor},
        {"editor_workspace_state_restore_defers_lsp_for_inactive_tabs",
         test_editor_workspace_state_restore_defers_lsp_for_inactive_tabs},
        {"editor_workspace_state_restores_tabs_to_split_panes",
         test_editor_workspace_state_restores_tabs_to_split_panes},
        {"editor_workspace_state_restores_three_leaf_split_assignment",
         test_editor_workspace_state_restores_three_leaf_split_assignment},
        {"editor_workspace_state_clamps_focused_pane_out_of_range",
         test_editor_workspace_state_clamps_focused_pane_out_of_range},
        {"editor_workspace_state_ignores_orphan_pane_tab_paths",
         test_editor_workspace_state_ignores_orphan_pane_tab_paths},
        {"editor_workspace_state_save_emits_term_token_and_version_two",
         test_editor_workspace_state_save_emits_term_token_and_version_two},
        {"editor_workspace_state_restore_hydrates_terminal_placeholder",
         test_editor_workspace_state_restore_hydrates_terminal_placeholder},
        {"editor_workspace_state_restore_terminal_only_layout",
         test_editor_workspace_state_restore_terminal_only_layout},
        {"editor_workspace_state_reset_panes_skips_saved_layout",
         test_editor_workspace_state_reset_panes_skips_saved_layout},
};

const int g_workspace_persistence_test_count =
        (int)(sizeof(g_workspace_persistence_tests) / sizeof(g_workspace_persistence_tests[0]));
