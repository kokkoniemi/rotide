#include "test_case.h"
#include "test_support.h"
#include "config/common.h"
#include "config/dap_config.h"
#include "config/editor_config.h"
#include "config/keymap.h"
#include "config/theme_config.h"
#include "input/dispatch.h"
#include "debug/dap.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"
#include "workspace/workspace_state.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/project_search.h"

static int test_editor_dap_config_loads_global_defaults_and_project_launches(void) {
	char dir_template[] = "/tmp/rotide-test-dap-config-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(global_path,
				"[dap.adapters]\n"
				"go = \"dlv dap\"\n"
				"\n"
				"[dap.defaults.go_app]\n"
				"name = \"Go app\"\n"
				"adapter = \"go\"\n"
				"request = \"launch\"\n"
				"program = \"${workspaceFolder}/cmd/app\"\n"
				"args = [\"--port\", \"8080\"]\n"
				"stopOnEntry = false\n"
				"mode = \"debug\"\n"
				"\n"
				"[dap.defaults.go_app.env]\n"
				"APP_ENV = \"dev\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[dap.launch.project_app]\n"
				"name = \"Project app\"\n"
				"adapter = \"go\"\n"
				"request = \"launch\"\n"
				"program = \"${workspaceFolder}/main.go\"\n"
				"args = [\"one\", \"${fileBasename}\"]\n"
				"stopOnEntry = true\n"
				"port = 443\n"
				"\n"
				"[dap.launch.project_app.env]\n"
				"FILE = \"${file}\"\n"));

	enum editorDapConfigLoadStatus status =
			editorDapConfigLoadFromPaths(global_path, project_path);
	ASSERT_EQ_INT(EDITOR_DAP_CONFIG_LOAD_OK, status);
	ASSERT_EQ_INT(1, E.dap_adapter_count);
	ASSERT_EQ_STR("go", E.dap_adapters[0].id);
	ASSERT_EQ_STR("dlv dap", E.dap_adapters[0].command);
	ASSERT_EQ_INT(1, E.dap_default_count);
	ASSERT_EQ_STR("Go app", E.dap_defaults[0].name);
	ASSERT_EQ_INT(1, E.dap_launch_count);
	ASSERT_EQ_STR("Project app", E.dap_launches[0].name);
	ASSERT_EQ_INT(1, E.dap_launches[0].env_count);

	char active_file[512];
	ASSERT_TRUE(path_join(active_file, sizeof(active_file), dir_path, "main.go"));
	char *json = editorDapBuildLaunchRequestJson(4, &E.dap_launches[0], dir_path, active_file);
	ASSERT_TRUE(json != NULL);
	char expected_program[700];
	snprintf(expected_program, sizeof(expected_program), "\"program\":\"%s/main.go\"", dir_path);
	ASSERT_TRUE(strstr(json, expected_program) != NULL);
	ASSERT_TRUE(strstr(json, "\"args\":[\"one\",\"main.go\"]") != NULL);
	ASSERT_TRUE(strstr(json, "\"stopOnEntry\":true") != NULL);
	ASSERT_TRUE(strstr(json, "\"port\":443") != NULL);
	ASSERT_TRUE(strstr(json, "\"FILE\":\"") != NULL);
	ASSERT_TRUE(strstr(json, active_file) != NULL);
	free(json);

	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_dap_config_rejects_missing_adapter_and_attach(void) {
	char dir_template[] = "/tmp/rotide-test-dap-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[dap.defaults.go_app]\n"
				"name = \"Go app\"\n"
				"adapter = \"go\"\n"
				"request = \"launch\"\n"));
	enum editorDapConfigLoadStatus status =
			editorDapConfigLoadFromPaths(global_path, NULL);
	ASSERT_TRUE((status & EDITOR_DAP_CONFIG_LOAD_INVALID_GLOBAL) != 0);
	ASSERT_EQ_INT(0, E.dap_default_count);

	ASSERT_TRUE(write_text_file(global_path,
				"[dap.adapters]\n"
				"go = \"dlv dap\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[dap.launch.bad_adapter]\n"
				"name = \"Bad adapter\"\n"
				"adapter = \"missing\"\n"
				"request = \"launch\"\n"));
	status = editorDapConfigLoadFromPaths(global_path, project_path);
	ASSERT_TRUE((status & EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT) != 0);
	ASSERT_EQ_INT(0, E.dap_launch_count);

	ASSERT_TRUE(write_text_file(project_path,
				"[dap.launch.attach_app]\n"
				"name = \"Attach app\"\n"
				"adapter = \"go\"\n"
				"request = \"attach\"\n"));
	status = editorDapConfigLoadFromPaths(global_path, project_path);
	ASSERT_TRUE((status & EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT) != 0);
	ASSERT_EQ_INT(0, E.dap_launch_count);

	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_dap_launch_field_accessors(void) {
	struct editorDapLaunchConfig cfg;
	memset(&cfg, 0, sizeof(cfg));

	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "program", "/bin/echo"));
	ASSERT_EQ_INT(1, cfg.field_count);
	char value[ROTIDE_DAP_VALUE_MAX];
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "program", value, sizeof(value)));
	ASSERT_EQ_STR("/bin/echo", value);

	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "program", "/bin/true"));
	ASSERT_EQ_INT(1, cfg.field_count);
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "program", value, sizeof(value)));
	ASSERT_EQ_STR("/bin/true", value);

	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "extra", "ok"));
	ASSERT_EQ_INT(2, cfg.field_count);
	editorDapLaunchRemoveField(&cfg, "program");
	ASSERT_EQ_INT(1, cfg.field_count);
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "extra", value, sizeof(value)));
	ASSERT_EQ_STR("ok", value);
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "program", value, sizeof(value)) == 0);
	editorDapLaunchRemoveField(&cfg, "missing");
	ASSERT_EQ_INT(1, cfg.field_count);
	return 0;
}

static int test_editor_dap_prepare_terminal_console_sets_tty(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	E.dap_terminal_leaf = NULL;

	struct editorDapLaunchConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	snprintf(cfg.id, sizeof(cfg.id), "%s", "go_app");
	snprintf(cfg.adapter, sizeof(cfg.adapter), "%s", "go");
	snprintf(cfg.request, sizeof(cfg.request), "%s", "launch");
	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "console", "terminal"));

	int ok = editorDapPrepareTerminalConsole(&cfg);
	ASSERT_TRUE(ok);
	ASSERT_TRUE(E.dap_terminal_leaf != NULL);
	ASSERT_TRUE(E.dap_terminal_leaf->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL);

	/* The console field is gone, replaced by a real tty path. */
	char value[ROTIDE_DAP_VALUE_MAX];
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "console", value, sizeof(value)) == 0);
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "tty", value, sizeof(value)));
	ASSERT_TRUE(strstr(value, "/dev/pts/") != NULL || strstr(value, "/dev/tty") != NULL);

	/* Cleanup: close the owned terminal pane. */
	struct editorPaneNode *leaf = E.dap_terminal_leaf;
	E.dap_terminal_leaf = NULL;
	if (editorPaneNodeContainsLeaf(E.layout_root, leaf)) {
		(void)editorPaneTreeCloseLeaf(&E.layout_root, leaf);
		if (E.focused_leaf == leaf || E.layout_root != NULL) {
			E.focused_leaf = E.layout_root;
		}
	}
	return 0;
}

static int test_editor_dap_prepare_terminal_console_strips_non_terminal_value(void) {
	struct editorDapLaunchConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	snprintf(cfg.id, sizeof(cfg.id), "%s", "test");
	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "console", "internalConsole"));
	ASSERT_TRUE(editorDapPrepareTerminalConsole(&cfg));
	char value[ROTIDE_DAP_VALUE_MAX];
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "console", value, sizeof(value)) == 0);
	ASSERT_TRUE(E.dap_terminal_leaf == NULL);
	return 0;
}

static int test_editor_dap_drawer_prompts_and_creates_project_config_from_default(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	char global_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), env.root_dir, "global.toml"));
	ASSERT_TRUE(write_text_file(global_path,
				"[dap.adapters]\n"
				"go = \"dlv dap\"\n"
				"\n"
				"[dap.defaults.go_app]\n"
				"name = \"Go app\"\n"
				"adapter = \"go\"\n"
				"request = \"launch\"\n"
				"program = \"${workspaceFolder}\"\n"));
	ASSERT_EQ_INT(EDITOR_DAP_CONFIG_LOAD_OK, editorDapConfigLoadFromPaths(global_path, NULL));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerDapToggle());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_DAP, E.drawer_mode);
	ASSERT_EQ_INT(0, E.dap_launch_count);
	ASSERT_EQ_INT(1, E.dap_default_count);

	int found_prompt = 0;
	int found_default = 0;
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerGetVisibleEntry(i, &view));
		if (strcmp(view.name, "Create debug config from default") == 0) {
			found_prompt = 1;
		}
		if (strcmp(view.name, "+ Go app") == 0) {
			found_default = 1;
			ASSERT_TRUE(editorDrawerSelectVisibleIndex(i, E.window_rows));
		}
	}
	ASSERT_TRUE(found_prompt);
	ASSERT_TRUE(found_default);

	int default_idx = -1;
	ASSERT_TRUE(editorDrawerSelectedDapDefault(&default_idx));
	ASSERT_EQ_INT(0, default_idx);
	ASSERT_TRUE(editorDapCreateProjectLaunchFromDefault(default_idx, env.project_dir));

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), env.project_dir, ".rotide.toml"));
	size_t len = 0;
	char *content = read_file_contents(project_path, &len);
	ASSERT_TRUE(content != NULL);
	ASSERT_TRUE(strstr(content, "[dap.launch.go_app]") != NULL);
	ASSERT_TRUE(strstr(content, "program = \"${workspaceFolder}\"") != NULL);
	free(content);
	ASSERT_EQ_INT(1, E.dap_launch_count);
	ASSERT_EQ_STR("Go app", E.dap_launches[0].name);

	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(unlink(project_path) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_dap_protocol_builds_initialize_and_launch_requests(void) {
	struct editorDapLaunchConfig config = {0};
	snprintf(config.id, sizeof(config.id), "%s", "go_app");
	snprintf(config.name, sizeof(config.name), "%s", "Go app");
	snprintf(config.adapter, sizeof(config.adapter), "%s", "go");
	snprintf(config.request, sizeof(config.request), "%s", "launch");
	struct editorDapLaunchField *program = &config.fields[config.field_count++];
	snprintf(program->key, sizeof(program->key), "%s", "program");
	program->kind = EDITOR_DAP_LAUNCH_VALUE_STRING;
	snprintf(program->string_value, sizeof(program->string_value), "%s",
			"${workspaceFolder}/main.go");
	struct editorDapLaunchField *args = &config.fields[config.field_count++];
	snprintf(args->key, sizeof(args->key), "%s", "args");
	args->kind = EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY;
	args->array_count = 1;
	snprintf(args->array_values[0], sizeof(args->array_values[0]), "%s", "${fileBasename}");

	char *init = editorDapBuildInitializeRequestJson(1, "go");
	ASSERT_TRUE(init != NULL);
	ASSERT_TRUE(strstr(init, "\"command\":\"initialize\"") != NULL);
	ASSERT_TRUE(strstr(init, "\"adapterID\":\"go\"") != NULL);
	free(init);

	char *launch = editorDapBuildLaunchRequestJson(2, &config, "/tmp/project",
			"/tmp/project/main.go");
	ASSERT_TRUE(launch != NULL);
	ASSERT_TRUE(strstr(launch, "\"seq\":2") != NULL);
	ASSERT_TRUE(strstr(launch, "\"command\":\"launch\"") != NULL);
	ASSERT_TRUE(strstr(launch, "\"program\":\"/tmp/project/main.go\"") != NULL);
	ASSERT_TRUE(strstr(launch, "\"args\":[\"main.go\"]") != NULL);
	free(launch);
	return 0;
}

const struct editorTestCase g_dap_tests[] = {
	{"editor_dap_config_loads_global_defaults_and_project_launches", test_editor_dap_config_loads_global_defaults_and_project_launches},
	{"editor_dap_config_rejects_missing_adapter_and_attach", test_editor_dap_config_rejects_missing_adapter_and_attach},
	{"editor_dap_launch_field_accessors", test_editor_dap_launch_field_accessors},
	{"editor_dap_prepare_terminal_console_sets_tty", test_editor_dap_prepare_terminal_console_sets_tty},
	{"editor_dap_prepare_terminal_console_strips_non_terminal_value", test_editor_dap_prepare_terminal_console_strips_non_terminal_value},
	{"editor_dap_drawer_prompts_and_creates_project_config_from_default", test_editor_dap_drawer_prompts_and_creates_project_config_from_default},
	{"editor_dap_protocol_builds_initialize_and_launch_requests", test_editor_dap_protocol_builds_initialize_and_launch_requests},
};

const int g_dap_test_count =
		(int)(sizeof(g_dap_tests) / sizeof(g_dap_tests[0]));
