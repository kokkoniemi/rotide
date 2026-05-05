#include "test_case.h"
#include "test_support.h"
#include "config/editor_config.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

static int find_drawer_entry_path(const char *path, int *idx_out,
		struct editorDrawerEntryView *view_out) {
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		if (!editorDrawerGetVisibleEntry(i, &view) || view.path == NULL) {
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
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);

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
	ASSERT_EQ_STR("dup", E.rows[0].chars);

	ASSERT_TRUE(unlink(abs_file) == 0);
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
	ASSERT_TRUE(editorDrawerGetVisibleEntry(0, &header));
	ASSERT_EQ_INT(1, header.is_search_header);
	ASSERT_EQ_STR("", header.name);

	ASSERT_TRUE(editorFileSearchAppendByte('M'));
	ASSERT_TRUE(editorFileSearchAppendByte('A'));
	ASSERT_TRUE(editorFileSearchAppendByte('I'));
	ASSERT_TRUE(editorFileSearchAppendByte('N'));
	ASSERT_EQ_STR("MAIN", editorFileSearchQuery());
	ASSERT_EQ_INT(2, editorDrawerVisibleCount());

	struct editorDrawerEntryView result;
	ASSERT_TRUE(editorDrawerGetVisibleEntry(1, &result));
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
	ASSERT_EQ_STR("beta", E.rows[0].chars);

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
	ASSERT_EQ_STR("File is unsupported", E.rows[0].chars);
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
	ASSERT_TRUE(editorDrawerGetVisibleEntry(0, &header));
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
	ASSERT_TRUE(path_join(marker_path, sizeof(marker_path), root_path, "compile_commands.json"));
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
	ASSERT_EQ_STR("stay", E.rows[0].chars);
	ASSERT_TRUE(strstr(E.statusmsg, "Tab limit reached") != NULL);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
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
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"setup_recovery_test_env(&env)");
		goto cleanup;
	}
	if (!editorTabsInit()) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"editorTabsInit()");
		goto cleanup;
	}

	add_row("quit-cleanup");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	recovery_path = editorRecoveryPath();
	if (recovery_path == NULL) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"recovery_path != NULL");
		goto cleanup;
	}
	if (access(recovery_path, F_OK) != 0) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"access(recovery_path, F_OK) == 0");
		goto cleanup;
	}

	E.dirty = 0;
	pid = fork();
	if (pid == -1) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__, "pid != -1");
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
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"wait_for_child_exit_with_timeout(pid, 1500, &status) == 0");
		goto cleanup;
	}
	pid = -1;
	if (!WIFEXITED(status)) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__, "WIFEXITED(status)");
		goto cleanup;
	}
	if (WEXITSTATUS(status) != EXIT_SUCCESS) {
		fprintf(stderr, "Assertion failed in %s:%d: expected %d, got %d\n", __func__, __LINE__,
				EXIT_SUCCESS, WEXITSTATUS(status));
		goto cleanup;
	}
	if (access(recovery_path, F_OK) != -1) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
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

static int test_editor_refresh_screen_reports_oom_without_crash(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 20;

	editorTestAllocFailAfter(0);
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);

	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	free(output);
	return 0;
}

static int test_editor_read_key_sequences(void) {
	int key = 0;
	char plain[] = "x";
	char up[] = "\x1b[A";
	char pgup[] = "\x1b[5~";
	char end_key[] = "\x1bOF";
	char plain_escape[] = "\x1b[x";

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);

	ASSERT_TRUE(editor_read_key_with_input(up, sizeof(up) - 1, &key) == 0);
	ASSERT_EQ_INT(ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(pgup, sizeof(pgup) - 1, &key) == 0);
	ASSERT_EQ_INT(PAGE_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(end_key, sizeof(end_key) - 1, &key) == 0);
	ASSERT_EQ_INT(END_KEY, key);

	ASSERT_TRUE(editor_read_key_with_input(plain_escape, sizeof(plain_escape) - 1, &key) == 0);
	ASSERT_EQ_INT('\x1b', key);
	return 0;
}

static int test_editor_read_key_alt_arrow_sequences(void) {
	int key = 0;
	const char csi_alt_left[] = "\x1b[1;3D";
	const char csi_alt_right[] = "\x1b[1;3C";
	const char csi_alt_down[] = "\x1b[1;3B";
	const char csi_alt_up[] = "\x1b[1;3A";
	const char csi_alt_shift_left[] = "\x1b[1;4D";
	const char csi_alt_shift_right[] = "\x1b[1;4C";
	const char csi_alt_shift_down[] = "\x1b[1;4B";
	const char csi_alt_shift_up[] = "\x1b[1;4A";
	const char csi_ctrl_left[] = "\x1b[1;5D";
	const char csi_ctrl_right[] = "\x1b[1;5C";
	const char csi_ctrl_down[] = "\x1b[1;5B";
	const char csi_ctrl_up[] = "\x1b[1;5A";
	const char csi_ctrl_alt_left[] = "\x1b[1;7D";
	const char csi_ctrl_alt_right[] = "\x1b[1;7C";
	const char csi_ctrl_alt_down[] = "\x1b[1;7B";
	const char csi_ctrl_alt_up[] = "\x1b[1;7A";
	const char fallback_alt_left[] = "\x1b\x1b[D";
	const char fallback_alt_right[] = "\x1b\x1b[C";
	const char fallback_alt_down[] = "\x1b\x1b[B";
	const char fallback_alt_up[] = "\x1b\x1b[A";
	const char alt_letter_lower[] = "\x1b" "a";
	const char alt_letter_upper[] = "\x1b" "A";
	const char ctrl_alt_letter[] = {'\x1b', CTRL_KEY('b')};

	ASSERT_TRUE(editor_read_key_with_input(csi_alt_left, sizeof(csi_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_right, sizeof(csi_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_down, sizeof(csi_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_up, sizeof(csi_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_UP, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_left,
				sizeof(csi_alt_shift_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_right,
				sizeof(csi_alt_shift_right) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_down,
				sizeof(csi_alt_shift_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_up,
				sizeof(csi_alt_shift_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_left, sizeof(csi_ctrl_left) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_right, sizeof(csi_ctrl_right) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_down, sizeof(csi_ctrl_down) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_up, sizeof(csi_ctrl_up) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_left, sizeof(csi_ctrl_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_right, sizeof(csi_ctrl_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_down, sizeof(csi_ctrl_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_up, sizeof(csi_ctrl_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_left, sizeof(fallback_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_right,
				sizeof(fallback_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_down, sizeof(fallback_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_up, sizeof(fallback_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(alt_letter_lower, sizeof(alt_letter_lower) - 1, &key) == 0);
	ASSERT_EQ_INT(EDITOR_ALT_LETTER_KEY('a'), key);
	ASSERT_TRUE(editor_read_key_with_input(alt_letter_upper, sizeof(alt_letter_upper) - 1, &key) == 0);
	ASSERT_EQ_INT(EDITOR_ALT_LETTER_KEY('a'), key);
	ASSERT_TRUE(editor_read_key_with_input(ctrl_alt_letter, sizeof(ctrl_alt_letter), &key) == 0);
	ASSERT_EQ_INT(EDITOR_CTRL_ALT_LETTER_KEY('b'), key);
	return 0;
}

static int test_editor_read_key_sgr_mouse_events(void) {
	int key = 0;
	struct editorMouseEvent event;
	char left_click[] = "\x1b[<0;5;3M";
	char ctrl_left_click[] = "\x1b[<16;12;6M";
	char left_drag[] = "\x1b[<32;6;4M";
	char left_release[] = "\x1b[<0;6;4m";
	char left_release_alt_cb[] = "\x1b[<3;7;4m";
	char wheel_up[] = "\x1b[<64;7;2M";
	char wheel_down[] = "\x1b[<65;4;9M";
	char wheel_left[] = "\x1b[<66;8;3M";
	char wheel_right[] = "\x1b[<67;9;3M";
	char shift_wheel_up[] = "\x1b[<68;10;5M";
	char shift_wheel_down[] = "\x1b[<69;11;5M";
	char modifier_drag_then_plain[] = "\x1b[<36;1;1MZ";
	char unsupported_then_plain[] = "\x1b[<2;1;1MY";

	ASSERT_TRUE(editor_read_key_with_input(left_click, sizeof(left_click) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(5, event.x);
	ASSERT_EQ_INT(3, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));

	ASSERT_TRUE(editor_read_key_with_input(ctrl_left_click, sizeof(ctrl_left_click) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(12, event.x);
	ASSERT_EQ_INT(6, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_CTRL, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_drag, sizeof(left_drag) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_DRAG, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_release, sizeof(left_release) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_release_alt_cb, sizeof(left_release_alt_cb) - 1, &key) ==
			0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(7, event.x);
	ASSERT_EQ_INT(4, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_up, sizeof(wheel_up) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_UP, event.kind);
	ASSERT_EQ_INT(7, event.x);
	ASSERT_EQ_INT(2, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_down, sizeof(wheel_down) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_DOWN, event.kind);
	ASSERT_EQ_INT(4, event.x);
	ASSERT_EQ_INT(9, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_left, sizeof(wheel_left) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_LEFT, event.kind);
	ASSERT_EQ_INT(8, event.x);
	ASSERT_EQ_INT(3, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_right, sizeof(wheel_right) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_RIGHT, event.kind);
	ASSERT_EQ_INT(9, event.x);
	ASSERT_EQ_INT(3, event.y);

	ASSERT_TRUE(editor_read_key_with_input(shift_wheel_up, sizeof(shift_wheel_up) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_LEFT, event.kind);
	ASSERT_EQ_INT(10, event.x);
	ASSERT_EQ_INT(5, event.y);

	ASSERT_TRUE(editor_read_key_with_input(shift_wheel_down, sizeof(shift_wheel_down) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_RIGHT, event.kind);
	ASSERT_EQ_INT(11, event.x);
	ASSERT_EQ_INT(5, event.y);

	ASSERT_TRUE(editor_read_key_with_input(modifier_drag_then_plain,
				sizeof(modifier_drag_then_plain) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_DRAG, event.kind);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_SHIFT, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(unsupported_then_plain,
				sizeof(unsupported_then_plain) - 1, &key) == 0);
	ASSERT_EQ_INT('Y', key);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));
	return 0;
}

static int test_editor_read_key_returns_input_eof_event_on_closed_stdin(void) {
	int key = 0;
	ASSERT_TRUE(editor_read_key_with_input("", 0, &key) == 0);
	ASSERT_EQ_INT(INPUT_EOF_EVENT, key);
	return 0;
}

static int test_editor_read_key_escape_parse_eof_returns_input_eof_event(void) {
	int key = 0;
	char incomplete[] = "\x1b[";
	ASSERT_TRUE(editor_read_key_with_input(incomplete, sizeof(incomplete) - 1, &key) == 0);
	ASSERT_EQ_INT(INPUT_EOF_EVENT, key);
	return 0;
}

static int test_editor_read_key_returns_resize_event_when_queued(void) {
	int key = 0;
	char plain[] = "x";

	editorQueueResizeEvent();
	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT(RESIZE_EVENT, key);

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);
	return 0;
}

static int test_read_cursor_position_and_window_size_fallback(void) {
	char response[] = "\x1b[24;80R";
	int rows = 0;
	int cols = 0;
	int saved_stdin;
	size_t stdout_len = 0;

	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);

	ASSERT_EQ_INT(0, readWindowSize(&rows, &cols));

	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(24, rows);
	ASSERT_EQ_INT(80, cols);
	ASSERT_TRUE(strstr(stdout_bytes, "\x1b[999C\x1b[999B") != NULL);
	ASSERT_TRUE(strstr(stdout_bytes, "\x1b[6n") != NULL);

	free(stdout_bytes);
	return 0;
}

static int test_read_cursor_position_rejects_malformed_responses(void) {
	struct {
		const char *response;
		size_t len;
	} cases[] = {
		{"\x1b[", sizeof("\x1b[") - 1},
		{"\x1b[R", sizeof("\x1b[R") - 1},
		{"\x1b[24;80", sizeof("\x1b[24;80") - 1},
		{"\x1b[24;R", sizeof("\x1b[24;R") - 1},
		{"24;80R", sizeof("24;80R") - 1},
		{"\x1b[24R", sizeof("\x1b[24R") - 1},
		{"\x1b[24;xxR", sizeof("\x1b[24;xxR") - 1},
		{"\x1b[;80R", sizeof("\x1b[;80R") - 1},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int rows = -1;
		int cols = -1;
		int saved_stdin;
		size_t stdout_len = 0;
		struct stdoutCapture capture;

		ASSERT_TRUE(start_stdout_capture(&capture) == 0);
		ASSERT_TRUE(setup_stdin_bytes(cases[i].response, cases[i].len, &saved_stdin) == 0);
		ASSERT_EQ_INT(-1, readCursorPosition(&rows, &cols));
		ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
		char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
		ASSERT_TRUE(stdout_bytes != NULL);
		ASSERT_TRUE(strstr(stdout_bytes, "\x1b[6n") != NULL);
		free(stdout_bytes);
	}

	return 0;
}

static int test_editor_refresh_window_size_clamps_tiny_terminal(void) {
	char response[] = "\x1b[1;5R";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 8;
	E.window_cols = 40;

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);
	ASSERT_EQ_INT(1, editorRefreshWindowSize());
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(1, E.window_rows);
	ASSERT_EQ_INT(5, E.window_cols);
	ASSERT_TRUE(strstr(stdout_bytes, "\x1b[6n") != NULL);
	free(stdout_bytes);
	return 0;
}

static int test_editor_refresh_window_size_failure_keeps_previous_dimensions(void) {
	char malformed[] = "\x1b[";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 7;
	E.window_cols = 22;

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(malformed, sizeof(malformed) - 1, &saved_stdin) == 0);
	ASSERT_EQ_INT(0, editorRefreshWindowSize());
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(7, E.window_rows);
	ASSERT_EQ_INT(22, E.window_cols);
	free(stdout_bytes);
	return 0;
}

static int test_editor_refresh_window_size_reserves_tab_status_and_message_rows(void) {
	char response[] = "\x1b[9;33R";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 8;
	E.window_cols = 40;

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);
	ASSERT_EQ_INT(1, editorRefreshWindowSize());
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(6, E.window_rows);
	ASSERT_EQ_INT(33, E.window_cols);
	free(stdout_bytes);
	return 0;
}

static int test_editor_keymap_load_valid_project_overrides_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-valid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"
				"redraw = \"ctrl+s\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_REDRAW, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('q'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_QUIT, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_unknown_action_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-bad-action-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"not_a_real_action = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_unknown_keyspec_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-bad-key-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"meta+s\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_duplicate_binding_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-dup-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"
				"quit = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('q'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_QUIT, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_malformed_toml_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-malformed-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap\n"
				"save = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_global_then_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+t\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status =
			editorKeymapLoadFromPaths(&keymap, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('t'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	if (editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action)) {
		ASSERT_TRUE(action != EDITOR_ACTION_SAVE);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_invalid_global_ignored_when_project_valid(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-invalid-global-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[keymap\n"
				"save = \"ctrl+a\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+t\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status =
			editorKeymapLoadFromPaths(&keymap, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_GLOBAL, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('t'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_configured_prefers_project_over_global(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char *original_cwd = NULL;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char global_path[512] = "";
	char project_path[512] = "";
	char root_template[] = "/tmp/rotide-test-keymap-configured-XXXXXX";

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
	if (!write_text_file(global_path,
				"[keymap]\n"
				"save = \"ctrl+t\"\n")) {
		goto cleanup;
	}
	if (!write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (chdir(root_path) != 0) {
		goto cleanup;
	}

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadConfigured(&keymap);
	if (status != EDITOR_KEYMAP_LOAD_OK) {
		goto cleanup;
	}

	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action) ||
			action != EDITOR_ACTION_SAVE) {
		goto cleanup;
	}
	if (editorKeymapLookupAction(&keymap, CTRL_KEY('t'), &action) &&
			action == EDITOR_ACTION_SAVE) {
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
		int written = snprintf(content, sizeof(content), "[editor]\ncursor_style = \"%s\"\n",
				cases[i].value);
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

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"block\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
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

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"invalid\"\n"));
	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_UNDERLINE;
	enum editorCursorStyleLoadStatus status =
			editorCursorStyleLoadFromPaths(&style, global_path, NULL);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"block\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"not-real\"\n"));
	style = EDITOR_CURSOR_STYLE_UNDERLINE;
	status = editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"bad-global\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"bad-project\"\n"));
	style = EDITOR_CURSOR_STYLE_UNDERLINE;
	status = editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(
			EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL | EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT,
			status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_load_configured_prefers_project_over_global(void) {
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
	if (!write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"block\"\n")) {
		goto cleanup;
	}
	if (!write_text_file(project_path,
				"[editor]\n"
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
	if (style != EDITOR_CURSOR_STYLE_UNDERLINE) {
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

static int test_editor_cursor_style_invalid_setting_does_not_break_keymap_loading(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-keymap-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"nope\"\n"
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
			editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_UNDERLINE;
	enum editorCursorStyleLoadStatus style_status =
			editorCursorStyleLoadFromPaths(&style, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT, style_status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_blink_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-blink-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_blink = true\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_blink = false\n"
				"cursor_style = \"underline\"\n"));

	int cursor_blink = 1;
	enum editorCursorBlinkLoadStatus status =
			editorCursorBlinkLoadFromPaths(&cursor_blink, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_BLINK_LOAD_OK, status);
	ASSERT_EQ_INT(0, cursor_blink);

	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
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

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"line_wrap = true\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
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
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
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

static int test_editor_line_wrap_invalid_setting_does_not_break_keymap_loading(void) {
	char dir_template[] = "/tmp/rotide-test-line-wrap-keymap-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"line_wrap = maybe\n"
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
			editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	int line_wrap = 1;
	enum editorLineWrapLoadStatus line_wrap_status =
			editorLineWrapLoadFromPaths(&line_wrap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_WRAP_LOAD_INVALID_PROJECT, line_wrap_status);
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

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"line_numbers = true\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"line_numbers = false\n"));

	int line_numbers = 1;
	enum editorLineNumbersLoadStatus status =
			editorLineNumbersLoadFromPaths(&line_numbers, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_NUMBERS_LOAD_OK, status);
	ASSERT_EQ_INT(0, line_numbers);

	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
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

static int test_editor_current_line_highlight_load_precedence_and_invalid_fallback(void) {
	char dir_template[] = "/tmp/rotide-test-current-line-highlight-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"current_line_highlight = false\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"current_line_highlight = true\n"));

	int current_line_highlight = 0;
	enum editorCurrentLineHighlightLoadStatus status =
			editorCurrentLineHighlightLoadFromPaths(&current_line_highlight, global_path,
					project_path);
	ASSERT_EQ_INT(EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OK, status);
	ASSERT_EQ_INT(1, current_line_highlight);

	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
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

static int test_editor_view_bool_invalid_settings_do_not_break_keymap_loading(void) {
	char dir_template[] = "/tmp/rotide-test-view-bool-keymap-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"line_numbers = maybe\n"
				"current_line_highlight = maybe\n"
				"cursor_blink = maybe\n"
				"cursor_style = \"bar\"\n"
				"[keymap]\n"
				"toggle_line_numbers = \"alt+n\"\n"
				"toggle_current_line_highlight = \"alt+h\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
			editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('n'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_LINE_NUMBERS, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('h'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT, action);

	int line_numbers = 0;
	enum editorLineNumbersLoadStatus line_numbers_status =
			editorLineNumbersLoadFromPaths(&line_numbers, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_LINE_NUMBERS_LOAD_INVALID_PROJECT, line_numbers_status);
	ASSERT_EQ_INT(1, line_numbers);

	int current_line_highlight = 0;
	enum editorCurrentLineHighlightLoadStatus current_line_highlight_status =
			editorCurrentLineHighlightLoadFromPaths(&current_line_highlight, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT,
			current_line_highlight_status);
	ASSERT_EQ_INT(1, current_line_highlight);

	int cursor_blink = 0;
	enum editorCursorBlinkLoadStatus cursor_blink_status =
			editorCursorBlinkLoadFromPaths(&cursor_blink, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_BLINK_LOAD_INVALID_PROJECT, cursor_blink_status);
	ASSERT_EQ_INT(1, cursor_blink);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_syntax_theme_load_global_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-syntax-theme-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[theme.syntax]\n"
				"comment = \"red\"\n"
				"keyword = \"blue\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[theme.syntax]\n"
				"keyword = \"bright_yellow\"\n"
				"string = \"green\"\n"
				"variable = \"white\"\n"
				"parameter = \"yellow\"\n"
				"module = \"cyan\"\n"
				"property = \"bright_magenta\"\n"));

	enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	enum editorSyntaxThemeLoadStatus status =
			editorSyntaxThemeLoadFromPaths(theme, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_THEME_LOAD_OK, status);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_RED, theme[EDITOR_SYNTAX_HL_COMMENT]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_YELLOW, theme[EDITOR_SYNTAX_HL_KEYWORD]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_GREEN, theme[EDITOR_SYNTAX_HL_STRING]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_CYAN, theme[EDITOR_SYNTAX_HL_TYPE]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_WHITE, theme[EDITOR_SYNTAX_HL_VARIABLE]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_YELLOW, theme[EDITOR_SYNTAX_HL_PARAMETER]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_CYAN, theme[EDITOR_SYNTAX_HL_MODULE]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_MAGENTA, theme[EDITOR_SYNTAX_HL_PROPERTY]);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_syntax_theme_invalid_entries_nonfatal_and_keymap_still_loads(void) {
	char dir_template[] = "/tmp/rotide-test-syntax-theme-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[theme.syntax]\n"
				"comment = \"not-a-color\"\n"
				"keyword = \"bright_blue\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[theme.syntax]\n"
				"unknown_scope = \"red\"\n"
				"string = \"green\"\n"
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));

	enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	enum editorSyntaxThemeLoadStatus theme_status =
			editorSyntaxThemeLoadFromPaths(theme, global_path, project_path);
	ASSERT_EQ_INT(
			EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL | EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT,
			theme_status);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_BLACK, theme[EDITOR_SYNTAX_HL_COMMENT]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_BLUE, theme[EDITOR_SYNTAX_HL_KEYWORD]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_GREEN, theme[EDITOR_SYNTAX_HL_STRING]);

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
			editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);
	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_modifier_combo_specs_case_insensitive(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-combos-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"next_tab = \"CTRL+ALT+RIGHT\"\n"
				"prev_tab = \"ctrl+UP\"\n"
				"toggle_drawer = \"ctrl+alt+e\"\n"
				"column_select_left = \"SHIFT+ALT+LEFT\"\n"
				"column_select_right = \"aLt+ShIfT+RiGhT\"\n"
				"move_left = \"AlT+b\"\n"
				"move_right = \"cTrL+aLt+z\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, ALT_ARROW_RIGHT, &action));
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ALT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEXT_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ARROW_UP, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_PREV_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('e'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_DRAWER, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('b'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_MOVE_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('z'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_MOVE_RIGHT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_COLUMN_SELECT_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_COLUMN_SELECT_RIGHT, action);

	char binding[24];
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_MOVE_LEFT, binding, sizeof(binding)));
	ASSERT_EQ_STR("Alt-B", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_MOVE_RIGHT, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-Z", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_PREV_TAB, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Up", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_TOGGLE_DRAWER, binding,
				sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-E", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_NEXT_TAB, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-Right", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_COLUMN_SELECT_LEFT, binding,
				sizeof(binding)));
	ASSERT_EQ_STR("Alt-Shift-Left", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_COLUMN_SELECT_RIGHT, binding,
				sizeof(binding)));
	ASSERT_EQ_STR("Alt-Shift-Right", binding);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-bad-combos-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	const char *invalid_lines[] = {
		"next_tab = \"ctrl+ctrl+right\"\n",
		"next_tab = \"shift+right\"\n",
		"next_tab = \"ctrl+shift+right\"\n",
		"next_tab = \"alt+home\"\n",
		"next_tab = \"ctrl+alt+a+b\"\n",
	};
	for (size_t i = 0; i < sizeof(invalid_lines) / sizeof(invalid_lines[0]); i++) {
		char content[256];
		int written = snprintf(content, sizeof(content), "[keymap]\n%s", invalid_lines[i]);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		struct editorKeymap keymap;
		enum editorKeymapLoadStatus status =
				editorKeymapLoadFromPaths(&keymap, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

		enum editorAction action = EDITOR_ACTION_COUNT;
		ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_ARROW_RIGHT, &action));
		ASSERT_EQ_INT(EDITOR_ACTION_NEXT_TAB, action);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
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

static int test_editor_keymap_defaults_include_tab_actions(void) {
	struct editorKeymap keymap;
	editorKeymapInitDefaults(&keymap);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('n'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEW_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('w'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_CLOSE_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEXT_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_PREV_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('e'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_FOCUS_DRAWER, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('e'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_DRAWER, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_COLUMN_SELECT_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_COLUMN_SELECT_RIGHT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_UP, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_COLUMN_SELECT_UP, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_DOWN, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_COLUMN_SELECT_DOWN, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('z'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_LINE_WRAP, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('n'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_LINE_NUMBERS, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('h'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('p'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_FIND_FILE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('f'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_PROJECT_SEARCH, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SCROLL_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SCROLL_RIGHT, action);
	return 0;
}

static int test_editor_keymap_load_accepts_toggle_line_wrap_alt_z(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-line-wrap-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"toggle_line_wrap = \"alt+z\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('z'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_LINE_WRAP, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_accepts_line_number_highlight_toggles(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-line-number-highlight-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"toggle_line_numbers = \"alt+a\"\n"
				"toggle_current_line_highlight = \"alt+b\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_LINE_NUMBERS, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('b'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_defaults_include_goto_definition_action(void) {
	struct editorKeymap keymap;
	editorKeymapInitDefaults(&keymap);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('o'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GOTO_DEFINITION, action);

	char binding[24];
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_GOTO_DEFINITION, binding,
			sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-O", binding);
	return 0;
}

static int test_editor_keymap_load_accepts_goto_definition_ctrl_o(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-gotodef-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"goto_definition = \"ctrl+o\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('o'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GOTO_DEFINITION, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_rejects_ctrl_i_binding_that_conflicts_with_tab_input(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-tab-conflict-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"goto_definition = \"ctrl+i\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('o'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GOTO_DEFINITION, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, '\t', &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_rejects_reserved_terminal_aliases_for_other_actions(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-reserved-alias-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	struct {
		const char *line;
		int default_key;
		enum editorAction default_action;
	} cases[] = {
		{"save = \"ctrl+m\"\n", CTRL_KEY('s'), EDITOR_ACTION_SAVE},
		{"save = \"ctrl+[\"\n", CTRL_KEY('s'), EDITOR_ACTION_SAVE},
		{"save = \"ctrl+h\"\n", CTRL_KEY('s'), EDITOR_ACTION_SAVE},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char content[128];
		int written = snprintf(content, sizeof(content), "[keymap]\n%s", cases[i].line);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		struct editorKeymap keymap;
		enum editorKeymapLoadStatus status =
				editorKeymapLoadFromPaths(&keymap, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

		enum editorAction action = EDITOR_ACTION_COUNT;
		ASSERT_TRUE(editorKeymapLookupAction(&keymap, cases[i].default_key, &action));
		ASSERT_EQ_INT(cases[i].default_action, action);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_accepts_reserved_terminal_aliases_for_matching_actions(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-reserved-allowed-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"newline = \"ctrl+m\"\n"
				"escape = \"ctrl+[\"\n"
				"backspace = \"ctrl+h\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, '\r', &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEWLINE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, '\x1b', &action));
	ASSERT_EQ_INT(EDITOR_ACTION_ESCAPE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('h'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_BACKSPACE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

const struct editorTestCase g_workspace_config_tests[] = {
	{"editor_drawer_root_selection_modes", test_editor_drawer_root_selection_modes},
	{"editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file", test_editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file},
	{"editor_drawer_expand_collapse_reuses_cached_children", test_editor_drawer_expand_collapse_reuses_cached_children},
	{"editor_drawer_root_is_not_collapsible", test_editor_drawer_root_is_not_collapsible},
	{"editor_drawer_open_selected_file_in_new_tab", test_editor_drawer_open_selected_file_in_new_tab},
	{"editor_drawer_open_selected_file_switches_existing_relative_path_tab", test_editor_drawer_open_selected_file_switches_existing_relative_path_tab},
	{"editor_file_search_filters_results_in_drawer", test_editor_file_search_filters_results_in_drawer},
	{"editor_file_search_preview_and_open_selected_file", test_editor_file_search_preview_and_open_selected_file},
	{"editor_file_search_previews_binary_file_as_unsupported_read_only_tab", test_editor_file_search_previews_binary_file_as_unsupported_read_only_tab},
	{"editor_project_search_finds_previews_and_opens_matches", test_editor_project_search_finds_previews_and_opens_matches},
	{"editor_path_absolute_dup_makes_relative_paths_absolute", test_editor_path_absolute_dup_makes_relative_paths_absolute},
	{"editor_path_find_marker_upward_returns_project_root", test_editor_path_find_marker_upward_returns_project_root},
	{"editor_drawer_open_selected_file_respects_tab_limit", test_editor_drawer_open_selected_file_respects_tab_limit},
	{"editor_recovery_snapshot_permissions_are_0600", test_editor_recovery_snapshot_permissions_are_0600},
	{"editor_recovery_clean_quit_removes_snapshot", test_editor_recovery_clean_quit_removes_snapshot},
	{"editor_recovery_failure_exit_keeps_snapshot", test_editor_recovery_failure_exit_keeps_snapshot},
	{"editor_read_key_sequences", test_editor_read_key_sequences},
	{"editor_read_key_alt_arrow_sequences", test_editor_read_key_alt_arrow_sequences},
	{"editor_read_key_sgr_mouse_events", test_editor_read_key_sgr_mouse_events},
	{"editor_read_key_returns_input_eof_event_on_closed_stdin", test_editor_read_key_returns_input_eof_event_on_closed_stdin},
	{"editor_read_key_escape_parse_eof_returns_input_eof_event", test_editor_read_key_escape_parse_eof_returns_input_eof_event},
	{"editor_read_key_returns_resize_event_when_queued", test_editor_read_key_returns_resize_event_when_queued},
	{"read_cursor_position_and_window_size_fallback", test_read_cursor_position_and_window_size_fallback},
	{"read_cursor_position_rejects_malformed_responses", test_read_cursor_position_rejects_malformed_responses},
	{"editor_refresh_window_size_clamps_tiny_terminal", test_editor_refresh_window_size_clamps_tiny_terminal},
	{"editor_refresh_window_size_failure_keeps_previous_dimensions", test_editor_refresh_window_size_failure_keeps_previous_dimensions},
	{"editor_refresh_window_size_reserves_tab_status_and_message_rows", test_editor_refresh_window_size_reserves_tab_status_and_message_rows},
	{"editor_keymap_load_valid_project_overrides_defaults", test_editor_keymap_load_valid_project_overrides_defaults},
	{"editor_keymap_load_unknown_action_falls_back_to_defaults", test_editor_keymap_load_unknown_action_falls_back_to_defaults},
	{"editor_keymap_load_unknown_keyspec_falls_back_to_defaults", test_editor_keymap_load_unknown_keyspec_falls_back_to_defaults},
	{"editor_keymap_load_duplicate_binding_falls_back_to_defaults", test_editor_keymap_load_duplicate_binding_falls_back_to_defaults},
	{"editor_keymap_load_malformed_toml_falls_back_to_defaults", test_editor_keymap_load_malformed_toml_falls_back_to_defaults},
	{"editor_keymap_global_then_project_precedence", test_editor_keymap_global_then_project_precedence},
	{"editor_keymap_invalid_global_ignored_when_project_valid", test_editor_keymap_invalid_global_ignored_when_project_valid},
	{"editor_keymap_load_configured_prefers_project_over_global", test_editor_keymap_load_configured_prefers_project_over_global},
	{"editor_cursor_style_load_valid_values_case_insensitive", test_editor_cursor_style_load_valid_values_case_insensitive},
	{"editor_cursor_style_global_then_project_precedence", test_editor_cursor_style_global_then_project_precedence},
	{"editor_cursor_style_invalid_values_fallback_to_bar", test_editor_cursor_style_invalid_values_fallback_to_bar},
	{"editor_cursor_style_load_configured_prefers_project_over_global", test_editor_cursor_style_load_configured_prefers_project_over_global},
	{"editor_cursor_style_invalid_setting_does_not_break_keymap_loading", test_editor_cursor_style_invalid_setting_does_not_break_keymap_loading},
	{"editor_cursor_blink_load_precedence_and_invalid_fallback", test_editor_cursor_blink_load_precedence_and_invalid_fallback},
	{"editor_line_wrap_load_valid_bool_values", test_editor_line_wrap_load_valid_bool_values},
	{"editor_line_wrap_global_then_project_precedence", test_editor_line_wrap_global_then_project_precedence},
	{"editor_line_wrap_invalid_values_fallback_to_false", test_editor_line_wrap_invalid_values_fallback_to_false},
	{"editor_line_wrap_invalid_setting_does_not_break_keymap_loading", test_editor_line_wrap_invalid_setting_does_not_break_keymap_loading},
	{"editor_line_numbers_load_precedence_and_invalid_fallback", test_editor_line_numbers_load_precedence_and_invalid_fallback},
	{"editor_current_line_highlight_load_precedence_and_invalid_fallback", test_editor_current_line_highlight_load_precedence_and_invalid_fallback},
	{"editor_view_bool_invalid_settings_do_not_break_keymap_loading", test_editor_view_bool_invalid_settings_do_not_break_keymap_loading},
	{"editor_syntax_theme_load_global_project_precedence", test_editor_syntax_theme_load_global_project_precedence},
	{"editor_syntax_theme_invalid_entries_nonfatal_and_keymap_still_loads", test_editor_syntax_theme_invalid_entries_nonfatal_and_keymap_still_loads},
	{"editor_keymap_load_modifier_combo_specs_case_insensitive", test_editor_keymap_load_modifier_combo_specs_case_insensitive},
	{"editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults", test_editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults},
	{"editor_parse_column_select_drag_modifier_value", test_editor_parse_column_select_drag_modifier_value},
	{"editor_column_select_drag_modifier_load_from_paths", test_editor_column_select_drag_modifier_load_from_paths},
	{"editor_keymap_defaults_include_tab_actions", test_editor_keymap_defaults_include_tab_actions},
	{"editor_keymap_load_accepts_toggle_line_wrap_alt_z", test_editor_keymap_load_accepts_toggle_line_wrap_alt_z},
	{"editor_keymap_load_accepts_line_number_highlight_toggles", test_editor_keymap_load_accepts_line_number_highlight_toggles},
	{"editor_keymap_defaults_include_goto_definition_action", test_editor_keymap_defaults_include_goto_definition_action},
	{"editor_keymap_load_accepts_goto_definition_ctrl_o", test_editor_keymap_load_accepts_goto_definition_ctrl_o},
	{"editor_keymap_load_rejects_ctrl_i_binding_that_conflicts_with_tab_input", test_editor_keymap_load_rejects_ctrl_i_binding_that_conflicts_with_tab_input},
	{"editor_keymap_load_rejects_reserved_terminal_aliases_for_other_actions", test_editor_keymap_load_rejects_reserved_terminal_aliases_for_other_actions},
	{"editor_keymap_load_accepts_reserved_terminal_aliases_for_matching_actions", test_editor_keymap_load_accepts_reserved_terminal_aliases_for_matching_actions},
	{"editor_refresh_screen_reports_oom_without_crash", test_editor_refresh_screen_reports_oom_without_crash},
};

const int g_workspace_config_test_count =
		(int)(sizeof(g_workspace_config_tests) / sizeof(g_workspace_config_tests[0]));
