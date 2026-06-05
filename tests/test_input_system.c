#include "config/input_config.h"
#include "config/keymap.h"
#include "config/runtime_config.h"
#include "editor_test_api.h"
#include "input/input_system.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_input_system_default_active_after_init(void) {
	ASSERT_TRUE(strcmp(editorInputSystemActiveId(), "cua") == 0);
	return 0;
}

static int test_input_system_lookup_and_activate_round_trip(void) {
	ASSERT_TRUE(editorInputSystemById("cua") == editorInputSystemActive());
	ASSERT_TRUE(editorInputSystemActivate("cua"));
	ASSERT_TRUE(strcmp(editorInputSystemActiveId(), "cua") == 0);
	return 0;
}

static int test_input_system_unknown_id_rejected(void) {
	const struct editorInputSystem *before = editorInputSystemActive();

	ASSERT_TRUE(!editorInputSystemActivate("missing"));
	ASSERT_TRUE(editorInputSystemActive() == before);
	ASSERT_TRUE(strcmp(editorInputSystemActiveId(), "cua") == 0);
	return 0;
}

static int test_input_system_cua_resolves_existing_action_names(void) {
	const struct editorInputSystem *system = editorInputSystemById("cua");
	int command_id = -1;

	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->resolve_command != NULL);
	ASSERT_TRUE(system->resolve_command("save", &command_id));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, command_id);
	ASSERT_TRUE(!system->resolve_command("vim_only_for_now", &command_id));
	return 0;
}

static int test_input_system_cua_bind_key_uses_keymap(void) {
	const struct editorInputSystem *system = editorInputSystemById("cua");
	enum editorAction action = EDITOR_ACTION_COUNT;

	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->bind_key != NULL);
	ASSERT_TRUE(system->bind_key(NULL, "save", CTRL_KEY('u')));
	ASSERT_TRUE(editorKeymapLookupAction(&E.keymap, CTRL_KEY('u'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!system->bind_key("normal", "save", CTRL_KEY('s')));
	return 0;
}

static int test_input_config_loads_vim_system(void) {
	char dir_template[] = "/tmp/rotide-test-input-config-vim-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char config_path[512];
	ASSERT_TRUE(path_join(config_path, sizeof(config_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(config_path, "[input]\n"
	                                         "system = \"vim\"\n"));

	char system[32];
	enum editorInputConfigLoadStatus status =
	        editorInputConfigLoadFromPaths(system, sizeof(system), NULL, config_path);
	ASSERT_EQ_INT(EDITOR_INPUT_CONFIG_LOAD_OK, status);
	ASSERT_EQ_STR("vim", system);

	ASSERT_TRUE(unlink(config_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_input_config_project_overrides_global(void) {
	char dir_template[] = "/tmp/rotide-test-input-config-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(write_text_file(global_path, "[input]\n"
	                                         "system = \"cua\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[input]\n"
	                                          "system = \"vim\"\n"));

	char system[32];
	enum editorInputConfigLoadStatus status =
	        editorInputConfigLoadFromPaths(system, sizeof(system), global_path, project_path);
	ASSERT_EQ_INT(EDITOR_INPUT_CONFIG_LOAD_OK, status);
	ASSERT_EQ_STR("vim", system);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_input_config_invalid_falls_back_to_cua(void) {
	char dir_template[] = "/tmp/rotide-test-input-config-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char config_path[512];
	ASSERT_TRUE(path_join(config_path, sizeof(config_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(config_path, "[input]\n"
	                                         "system = \"not-real\"\n"));

	char system[32];
	enum editorInputConfigLoadStatus status =
	        editorInputConfigLoadFromPaths(system, sizeof(system), NULL, config_path);
	ASSERT_TRUE((status & EDITOR_INPUT_CONFIG_LOAD_INVALID_PROJECT) != 0);
	ASSERT_EQ_STR("cua", system);

	ASSERT_TRUE(unlink(config_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_input_config_reload_activates_configured_system(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char *original_cwd = NULL;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char project_path[512] = "";
	char root_template[] = "/tmp/rotide-test-input-config-runtime-XXXXXX";

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
	if (!path_join(project_path, sizeof(project_path), root_path, ".rotide.toml")) {
		goto cleanup;
	}
	if (!write_text_file(project_path, "[input]\n"
	                                   "system = \"vim\"\n")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (chdir(root_path) != 0) {
		goto cleanup;
	}

	editorConfigApplyConfiguredSettings(EDITOR_CONFIG_BOOTSTRAP_OK, NULL);
	if (strcmp(editorInputSystemActiveId(), "vim") != 0) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	(void)editorInputSystemActivate("cua");
	if (original_cwd != NULL && chdir(original_cwd) != 0) {
		failed = 1;
	}
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (project_path[0] != '\0') {
		(void)unlink(project_path);
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

const struct editorTestCase g_input_system_tests[] = {
        {"input_system_default_active_after_init", test_input_system_default_active_after_init},
        {"input_system_lookup_and_activate_round_trip",
         test_input_system_lookup_and_activate_round_trip},
        {"input_system_unknown_id_rejected", test_input_system_unknown_id_rejected},
        {"input_system_cua_resolves_existing_action_names",
         test_input_system_cua_resolves_existing_action_names},
        {"input_system_cua_bind_key_uses_keymap", test_input_system_cua_bind_key_uses_keymap},
        {"input_config_loads_vim_system", test_input_config_loads_vim_system},
        {"input_config_project_overrides_global", test_input_config_project_overrides_global},
        {"input_config_invalid_falls_back_to_cua", test_input_config_invalid_falls_back_to_cua},
        {"input_config_reload_activates_configured_system",
         test_input_config_reload_activates_configured_system},
};

const int g_input_system_test_count =
        (int)(sizeof(g_input_system_tests) / sizeof(g_input_system_tests[0]));
