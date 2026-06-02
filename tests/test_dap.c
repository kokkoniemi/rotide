#include "config/common.h"
#include "config/dap_config.h"
#include "debug/dap.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_editor_dap_config_loads_global_defaults_and_project_launches(void) {
	char dir_template[] = "/tmp/rotide-test-dap-config-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(global_path, "[dap.adapters]\n"
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
	ASSERT_TRUE(write_text_file(project_path, "[dap.launch.project_app]\n"
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
	(void)snprintf(expected_program, sizeof(expected_program), "\"program\":\"%s/main.go\"",
	               dir_path);
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

	ASSERT_TRUE(write_text_file(global_path, "[dap.defaults.go_app]\n"
	                                         "name = \"Go app\"\n"
	                                         "adapter = \"go\"\n"
	                                         "request = \"launch\"\n"));
	enum editorDapConfigLoadStatus status = editorDapConfigLoadFromPaths(global_path, NULL);
	ASSERT_TRUE((status & EDITOR_DAP_CONFIG_LOAD_INVALID_GLOBAL) != 0);
	ASSERT_EQ_INT(0, E.dap_default_count);

	ASSERT_TRUE(write_text_file(global_path, "[dap.adapters]\n"
	                                         "go = \"dlv dap\"\n"));
	ASSERT_TRUE(write_text_file(project_path, "[dap.launch.bad_adapter]\n"
	                                          "name = \"Bad adapter\"\n"
	                                          "adapter = \"missing\"\n"
	                                          "request = \"launch\"\n"));
	status = editorDapConfigLoadFromPaths(global_path, project_path);
	ASSERT_TRUE((status & EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT) != 0);
	ASSERT_EQ_INT(0, E.dap_launch_count);

	ASSERT_TRUE(write_text_file(project_path, "[dap.launch.attach_app]\n"
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
	(void)snprintf(cfg.id, sizeof(cfg.id), "%s", "go_app");
	(void)snprintf(cfg.adapter, sizeof(cfg.adapter), "%s", "go");
	(void)snprintf(cfg.request, sizeof(cfg.request), "%s", "launch");
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
	(void)snprintf(cfg.id, sizeof(cfg.id), "%s", "test");
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
	ASSERT_TRUE(write_text_file(global_path, "[dap.adapters]\n"
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
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
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
	(void)snprintf(config.id, sizeof(config.id), "%s", "go_app");
	(void)snprintf(config.name, sizeof(config.name), "%s", "Go app");
	(void)snprintf(config.adapter, sizeof(config.adapter), "%s", "go");
	(void)snprintf(config.request, sizeof(config.request), "%s", "launch");
	struct editorDapLaunchField *program = &config.fields[config.field_count++];
	(void)snprintf(program->key, sizeof(program->key), "%s", "program");
	program->kind = EDITOR_DAP_LAUNCH_VALUE_STRING;
	(void)snprintf(program->string_value, sizeof(program->string_value), "%s",
	               "${workspaceFolder}/main.go");
	struct editorDapLaunchField *args = &config.fields[config.field_count++];
	(void)snprintf(args->key, sizeof(args->key), "%s", "args");
	args->kind = EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY;
	args->array_count = 1;
	args->array_values = calloc(ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS, sizeof(*args->array_values));
	ASSERT_TRUE(args->array_values != NULL);
	(void)snprintf(args->array_values[0], sizeof(args->array_values[0]), "%s",
	               "${fileBasename}");

	char *init = editorDapBuildInitializeRequestJson(1, "go");
	ASSERT_TRUE(init != NULL);
	ASSERT_TRUE(strstr(init, "\"command\":\"initialize\"") != NULL);
	ASSERT_TRUE(strstr(init, "\"adapterID\":\"go\"") != NULL);
	free(init);

	char *launch =
	        editorDapBuildLaunchRequestJson(2, &config, "/tmp/project", "/tmp/project/main.go");
	ASSERT_TRUE(launch != NULL);
	ASSERT_TRUE(strstr(launch, "\"seq\":2") != NULL);
	ASSERT_TRUE(strstr(launch, "\"command\":\"launch\"") != NULL);
	ASSERT_TRUE(strstr(launch, "\"program\":\"/tmp/project/main.go\"") != NULL);
	ASSERT_TRUE(strstr(launch, "\"args\":[\"main.go\"]") != NULL);
	free(launch);
	editorDapLaunchConfigClear(&config);
	return 0;
}

static int test_editor_dap_launch_field_array_values_lifecycle(void) {
	struct editorDapLaunchConfig config = {0};

	/* Field 0: a STRING_ARRAY "args" populated directly. */
	struct editorDapLaunchField *args = &config.fields[config.field_count++];
	(void)snprintf(args->key, sizeof(args->key), "%s", "args");
	args->kind = EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY;
	args->array_values = calloc(ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS, sizeof(*args->array_values));
	ASSERT_TRUE(args->array_values != NULL);
	args->array_count = 2;
	(void)snprintf(args->array_values[0], sizeof(args->array_values[0]), "--foo");
	(void)snprintf(args->array_values[1], sizeof(args->array_values[1]), "--bar");

	/* Transitioning the same field STRING_ARRAY -> STRING must drop the
	 * heap block, not leak it. */
	ASSERT_TRUE(editorDapLaunchSetStringField(&config, "args", "single"));
	ASSERT_TRUE(args->array_values == NULL);
	ASSERT_EQ_INT(0, args->array_count);
	ASSERT_EQ_INT((int)EDITOR_DAP_LAUNCH_VALUE_STRING, (int)args->kind);

	/* Field 1: a STRING_ARRAY "extra_args" that we then remove. The
	 * remove path frees array_values before the slot shift; the trailing
	 * memset NULLs the vacated tail so a subsequent deep-clear can't
	 * double-free. */
	struct editorDapLaunchField *extra = &config.fields[config.field_count++];
	(void)snprintf(extra->key, sizeof(extra->key), "%s", "extra_args");
	extra->kind = EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY;
	extra->array_values =
	        calloc(ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS, sizeof(*extra->array_values));
	ASSERT_TRUE(extra->array_values != NULL);
	extra->array_count = 1;
	(void)snprintf(extra->array_values[0], sizeof(extra->array_values[0]), "--baz");

	int before_count = config.field_count;
	editorDapLaunchRemoveField(&config, "extra_args");
	ASSERT_EQ_INT(before_count - 1, config.field_count);
	ASSERT_TRUE(config.fields[config.field_count].array_values == NULL);

	editorDapLaunchConfigClear(&config);
	ASSERT_EQ_INT(0, config.field_count);
	return 0;
}

static size_t dap_drain_fd(int fd, char *buf, size_t cap) {
	size_t total = 0;
	while (total + 1 < cap) {
		ssize_t n = read(fd, buf + total, cap - 1 - total);
		if (n <= 0) {
			break;
		}
		total += (size_t)n;
	}
	buf[total] = '\0';
	return total;
}

static int test_editor_dap_handshake_sends_launch_after_initialize_response(void) {
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	int flags = fcntl(fds[0], F_GETFL, 0);
	ASSERT_TRUE(flags != -1 && fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != -1);

	E.dap_breakpoint_count = 0;
	char *launch =
	        strdup("{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{}}");
	ASSERT_TRUE(launch != NULL);
	editorDapBeginSessionForTest(fds[1], launch);
	ASSERT_EQ_INT(1, editorDapSessionStateForTest());

	/* Nothing is sent until the initialize response arrives. */
	char buf[4096];
	ASSERT_EQ_INT(0, (int)dap_drain_fd(fds[0], buf, sizeof(buf)));

	(void)editorDapProcessIncomingMessage("{\"type\":\"response\",\"command\":\"initialize\","
	                                      "\"success\":true,\"request_seq\":1}");
	ASSERT_EQ_INT(2, editorDapSessionStateForTest());
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"launch\"") != NULL);

	/* The initialized event registers breakpoints, then configurationDone. */
	E.dap_breakpoint_count = 1;
	(void)snprintf(E.dap_breakpoints[0].path, sizeof(E.dap_breakpoints[0].path), "%s",
	               "/tmp/x.c");
	E.dap_breakpoints[0].line = 10;
	(void)editorDapProcessIncomingMessage("{\"type\":\"event\",\"event\":\"initialized\"}");
	ASSERT_EQ_INT(3, editorDapSessionStateForTest());
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"setBreakpoints\"") != NULL);
	ASSERT_TRUE(strstr(buf, "/tmp/x.c") != NULL);
	ASSERT_TRUE(strstr(buf, "\"line\":11") != NULL);
	ASSERT_TRUE(strstr(buf, "\"command\":\"configurationDone\"") != NULL);

	/* A duplicate initialized event must not re-send configuration. */
	(void)editorDapProcessIncomingMessage("{\"type\":\"event\",\"event\":\"initialized\"}");
	ASSERT_EQ_INT(0, (int)dap_drain_fd(fds[0], buf, sizeof(buf)));

	editorDapEndSessionForTest();
	E.dap_breakpoint_count = 0;
	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}

static int test_editor_dap_handshake_initialize_failure_aborts(void) {
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	char *launch =
	        strdup("{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{}}");
	ASSERT_TRUE(launch != NULL);
	editorDapBeginSessionForTest(fds[1], launch);

	/* A failed initialize aborts the session (shutdown closes the write fd). */
	(void)editorDapProcessIncomingMessage("{\"type\":\"response\",\"command\":\"initialize\","
	                                      "\"success\":false,\"message\":\"boom\"}");
	ASSERT_EQ_INT(0, editorDapSessionStateForTest());
	ASSERT_EQ_INT(0, E.dap_running);

	(void)close(fds[0]);
	return 0;
}

const struct editorTestCase g_dap_tests[] = {
        {"editor_dap_config_loads_global_defaults_and_project_launches",
         test_editor_dap_config_loads_global_defaults_and_project_launches},
        {"editor_dap_config_rejects_missing_adapter_and_attach",
         test_editor_dap_config_rejects_missing_adapter_and_attach},
        {"editor_dap_launch_field_accessors", test_editor_dap_launch_field_accessors},
        {"editor_dap_prepare_terminal_console_sets_tty",
         test_editor_dap_prepare_terminal_console_sets_tty},
        {"editor_dap_prepare_terminal_console_strips_non_terminal_value",
         test_editor_dap_prepare_terminal_console_strips_non_terminal_value},
        {"editor_dap_drawer_prompts_and_creates_project_config_from_default",
         test_editor_dap_drawer_prompts_and_creates_project_config_from_default},
        {"editor_dap_protocol_builds_initialize_and_launch_requests",
         test_editor_dap_protocol_builds_initialize_and_launch_requests},
        {"editor_dap_launch_field_array_values_lifecycle",
         test_editor_dap_launch_field_array_values_lifecycle},
        {"editor_dap_handshake_sends_launch_after_initialize_response",
         test_editor_dap_handshake_sends_launch_after_initialize_response},
        {"editor_dap_handshake_initialize_failure_aborts",
         test_editor_dap_handshake_initialize_failure_aborts},
};

const int g_dap_test_count = (int)(sizeof(g_dap_tests) / sizeof(g_dap_tests[0]));
