#include "config/common.h"
#include "config/dap_config.h"
#include "debug/dap.h"
#include "render/screen.h"
#include "render/status_bar.h"
#include "render/write_buf.h"
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
	/* Frees the string-array fields (e.g. `args`) loaded into the globals. */
	editorDapConfigInitDefaults();
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

	/* A failed initialize aborts the session (shutdown closes the write fd) and
	 * surfaces the adapter's message. */
	E.statusmsg[0] = '\0';
	(void)editorDapProcessIncomingMessage("{\"type\":\"response\",\"command\":\"initialize\","
	                                      "\"success\":false,\"message\":\"boom\"}");
	ASSERT_EQ_INT(0, editorDapSessionStateForTest());
	ASSERT_EQ_INT(0, E.dap_running);
	ASSERT_TRUE(strstr(E.statusmsg, "boom") != NULL);

	(void)close(fds[0]);
	return 0;
}

static int test_editor_dap_failed_response_surfaces_message(void) {
	E.statusmsg[0] = '\0';
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	/* A failed request prefers the detailed body.error.format over `message`,
	 * shown in the status bar and appended to the console output. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":false,"
	        "\"message\":\"cannotInspect\","
	        "\"body\":{\"error\":{\"format\":\"Cannot inspect while running\"}}}");
	ASSERT_TRUE(strstr(E.statusmsg, "stackTrace") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "Cannot inspect while running") != NULL);
	ASSERT_TRUE(strstr(E.dap_output, "Cannot inspect while running") != NULL);
	E.statusmsg[0] = '\0';
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	return 0;
}

static int test_editor_dap_configuration_done_failure_is_benign(void) {
	/* gdb answers configurationDone with notStopped once the program is already
	 * running; that must not nag the user or end the session. */
	E.dap_running = 1;
	(void)snprintf(E.statusmsg, sizeof(E.statusmsg), "%s", "SENTINEL");
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"configurationDone\",\"success\":false,"
	        "\"message\":\"notStopped\"}");
	ASSERT_EQ_STR("SENTINEL", E.statusmsg);
	ASSERT_EQ_INT(1, E.dap_running);
	E.dap_running = 0;
	E.statusmsg[0] = '\0';
	return 0;
}

static int test_editor_dap_stopped_chain_populates_state(void) {
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	int flags = fcntl(fds[0], F_GETFL, 0);
	ASSERT_TRUE(flags != -1 && fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != -1);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));
	char buf[8192];

	/* stopped event captures the thread, clears stale state, asks for threads. */
	E.dap_thread_count = 5;
	(void)editorDapProcessIncomingMessage("{\"type\":\"event\",\"event\":\"stopped\","
	                                      "\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}");
	ASSERT_EQ_INT(1, E.dap_stopped);
	ASSERT_EQ_INT(0, E.dap_thread_count);
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"threads\"") != NULL);

	/* threads response populates threads and asks for the stopped thread's stack. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"threads\",\"success\":true,"
	        "\"body\":{\"threads\":[{\"id\":1,\"name\":\"dap_sample.out\"}]}}");
	ASSERT_EQ_INT(1, E.dap_thread_count);
	ASSERT_EQ_INT(1, E.dap_threads[0].id);
	ASSERT_EQ_STR("dap_sample.out", E.dap_threads[0].name);
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"stackTrace\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"threadId\":1") != NULL);

	/* stackTrace response populates frames (incl. nested source.path) and asks
	 * for the top frame's scopes. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,"
	        "\"body\":{\"stackFrames\":["
	        "{\"id\":0,\"line\":61,\"column\":0,\"name\":\"parse_mode\","
	        "\"source\":{\"name\":\"dap_sample.c\",\"path\":\"/x/dap_sample.c\"}},"
	        "{\"id\":1,\"line\":186,\"name\":\"main\","
	        "\"source\":{\"path\":\"/x/dap_sample.c\"}}]}}");
	ASSERT_EQ_INT(2, E.dap_stack_frame_count);
	ASSERT_EQ_STR("parse_mode", E.dap_stack_frames[0].name);
	ASSERT_EQ_INT(61, E.dap_stack_frames[0].line);
	ASSERT_EQ_STR("/x/dap_sample.c", E.dap_stack_frames[0].path);
	ASSERT_EQ_STR("main", E.dap_stack_frames[1].name);
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"scopes\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"frameId\":0") != NULL);

	/* scopes response populates scopes and asks for variables of each ref. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,"
	        "\"body\":{\"scopes\":[{\"variablesReference\":1,\"name\":\"Arguments\"},"
	        "{\"variablesReference\":2,\"name\":\"Registers\"}]}}");
	ASSERT_EQ_INT(2, E.dap_scope_count);
	ASSERT_EQ_STR("Arguments", E.dap_scopes[0].name);
	ASSERT_EQ_INT(1, E.dap_scopes[0].variables_reference);
	ASSERT_EQ_INT(0, E.dap_variable_count);
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"variables\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":1") != NULL);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":2") != NULL);

	/* variables responses append into one flat list across scopes. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"body\":{\"variables\":[{\"variablesReference\":0,\"name\":\"arg\","
	        "\"value\":\"0x1 \\\"hi\\\"\"}]}}");
	ASSERT_EQ_INT(1, E.dap_variable_count);
	ASSERT_EQ_STR("arg", E.dap_variables[0].name);
	ASSERT_TRUE(strstr(E.dap_variables[0].value, "hi") != NULL);
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"body\":{\"variables\":[{\"variablesReference\":0,\"name\":\"argc\","
	        "\"value\":\"1\"}]}}");
	ASSERT_EQ_INT(2, E.dap_variable_count);
	ASSERT_EQ_STR("argc", E.dap_variables[1].name);

	editorDapEndSessionForTest();
	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}

static int test_editor_dap_output_event_reads_body_output(void) {
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"event\",\"event\":\"output\","
	        "\"body\":{\"category\":\"stdout\",\"output\":\"hello\\nworld\"}}");
	ASSERT_TRUE(strstr(E.dap_output, "hello\nworld") != NULL);
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	return 0;
}

static int test_editor_dap_continued_clears_inspection_state(void) {
	E.dap_stopped = 1;
	E.dap_thread_count = 3;
	E.dap_stack_frame_count = 2;
	E.dap_scope_count = 1;
	E.dap_variable_count = 4;
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"event\",\"event\":\"continued\",\"body\":{\"threadId\":1}}");
	ASSERT_EQ_INT(0, E.dap_stopped);
	ASSERT_EQ_INT(0, E.dap_thread_count);
	ASSERT_EQ_INT(0, E.dap_stack_frame_count);
	ASSERT_EQ_INT(0, E.dap_scope_count);
	ASSERT_EQ_INT(0, E.dap_variable_count);
	return 0;
}

static int test_editor_dap_response_parsing_tolerates_malformed(void) {
	/* Truncated/garbage payloads must not crash the parsers or leave state
	 * populated from a half-read body. */
	static const char *const messages[] = {
	        "",
	        "{",
	        "{\"type\":\"response\",\"command\":\"threads\",\"success\":true,\"body\":{",
	        "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,"
	        "\"body\":{\"stackFrames\":[{\"id\":0,\"source\":{\"path\":",
	        "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,"
	        "\"body\":{\"scopes\":[{}]}}",
	        "{\"type\":\"event\",\"event\":\"output\",\"body\":{\"output\":}",
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,\"body\":{}}",
	};
	E.dap_thread_count = 0;
	E.dap_stack_frame_count = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
	for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
		(void)editorDapProcessIncomingMessage(messages[i]);
	}
	ASSERT_EQ_INT(0, E.dap_thread_count);
	ASSERT_EQ_INT(0, E.dap_stack_frame_count);
	ASSERT_EQ_INT(0, E.dap_variable_count);
	return 0;
}

static int dap_bytes_contain(const char *hay, size_t n, const char *needle) {
	size_t m = strlen(needle);
	if (m == 0 || n < m) {
		return 0;
	}
	for (size_t i = 0; i + m <= n; i++) {
		if (memcmp(hay + i, needle, m) == 0) {
			return 1;
		}
	}
	return 0;
}

static int test_editor_dap_breakpoint_toggle_at_line_and_stopped_predicate(void) {
	char *saved_file = E.filename;
	E.filename = "/tmp/rotide-bp-test.c";
	E.dap_running = 0;
	E.dap_breakpoint_count = 0;

	ASSERT_TRUE(editorDapToggleBreakpointAtLine(10));
	ASSERT_EQ_INT(1, E.dap_breakpoint_count);
	ASSERT_TRUE(editorDapHasBreakpoint(E.filename, 10) >= 0);
	ASSERT_TRUE(editorDapToggleBreakpointAtLine(10)); /* toggles back off */
	ASSERT_EQ_INT(0, E.dap_breakpoint_count);
	ASSERT_EQ_INT(0, editorDapToggleBreakpointAtLine(-1)); /* invalid line ignored */

	/* Stopped-line predicate: top frame's path+line (1-based) vs 0-based row. */
	E.dap_stopped = 0;
	E.dap_stack_frame_count = 0;
	ASSERT_EQ_INT(0, editorDapIsStoppedLine(E.filename, 4));
	E.dap_stopped = 1;
	E.dap_stack_frame_count = 1;
	(void)snprintf(E.dap_stack_frames[0].path, sizeof(E.dap_stack_frames[0].path), "%s",
	               E.filename);
	E.dap_stack_frames[0].line = 5;
	ASSERT_TRUE(editorDapIsStoppedLine(E.filename, 4));
	ASSERT_EQ_INT(0, editorDapIsStoppedLine(E.filename, 3));
	ASSERT_EQ_INT(0, editorDapIsStoppedLine("/some/other.c", 4));

	E.dap_stopped = 0;
	E.dap_stack_frame_count = 0;
	E.dap_breakpoint_count = 0;
	E.filename = saved_file;
	return 0;
}

static int test_editor_dap_gutter_renders_markers(void) {
	static const char breakpoint_glyph[] = "\xE2\x97\x8F";
	static const char stopped_glyph[] = "\xE2\x96\xB6";
	int saved_numrows = E.numrows;
	char *saved_file = E.filename;
	E.numrows = 20;
	E.filename = "/tmp/rotide-gutter.c";
	E.dap_running = 0;
	E.dap_stopped = 0;
	E.dap_stack_frame_count = 0;
	E.dap_breakpoint_count = 1;
	(void)snprintf(E.dap_breakpoints[0].path, sizeof(E.dap_breakpoints[0].path), "%s",
	               E.filename);
	E.dap_breakpoints[0].line = 3;

	/* The breakpoint row shows the marker; a plain row does not. */
	struct writeBuf wb = WRITEBUF_INIT;
	ASSERT_TRUE(editorDrawLineNumberGutter(&wb, 3, 0, 6));
	ASSERT_TRUE(dap_bytes_contain(wb.b, wb.len, breakpoint_glyph));
	wbFree(&wb);

	struct writeBuf wb_plain = WRITEBUF_INIT;
	ASSERT_TRUE(editorDrawLineNumberGutter(&wb_plain, 4, 0, 6));
	ASSERT_TRUE(!dap_bytes_contain(wb_plain.b, wb_plain.len, breakpoint_glyph));
	wbFree(&wb_plain);

	/* When stopped on that row, the stopped marker wins over the breakpoint. */
	E.dap_stopped = 1;
	E.dap_stack_frame_count = 1;
	(void)snprintf(E.dap_stack_frames[0].path, sizeof(E.dap_stack_frames[0].path), "%s",
	               E.filename);
	E.dap_stack_frames[0].line = 4;
	struct writeBuf wb_stopped = WRITEBUF_INIT;
	ASSERT_TRUE(editorDrawLineNumberGutter(&wb_stopped, 3, 0, 6));
	ASSERT_TRUE(dap_bytes_contain(wb_stopped.b, wb_stopped.len, stopped_glyph));
	ASSERT_TRUE(!dap_bytes_contain(wb_stopped.b, wb_stopped.len, breakpoint_glyph));
	wbFree(&wb_stopped);

	E.dap_stopped = 0;
	E.dap_stack_frame_count = 0;
	E.dap_breakpoint_count = 0;
	E.numrows = saved_numrows;
	E.filename = saved_file;
	return 0;
}

static int test_editor_dap_control_requests_include_thread_id(void) {
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	int flags = fcntl(fds[0], F_GETFL, 0);
	ASSERT_TRUE(flags != -1 && fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != -1);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));
	char buf[4096];

	/* With no stop yet, control falls back to the main thread (1). */
	ASSERT_TRUE(editorDapStepOver());
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"next\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"threadId\":1") != NULL);

	/* After stopping on thread 7, controls target thread 7. */
	(void)editorDapProcessIncomingMessage("{\"type\":\"event\",\"event\":\"stopped\","
	                                      "\"body\":{\"reason\":\"breakpoint\",\"threadId\":7}}");
	(void)dap_drain_fd(fds[0], buf, sizeof(buf)); /* discard the auto threads request */

	ASSERT_TRUE(editorDapStepInto());
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"stepIn\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"threadId\":7") != NULL);

	ASSERT_TRUE(editorDapStepOut());
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"stepOut\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"threadId\":7") != NULL);

	/* continue clears the stopped flag and targets the stopped thread. */
	E.dap_stopped = 1;
	ASSERT_TRUE(editorDapContinue());
	ASSERT_EQ_INT(0, E.dap_stopped);
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"continue\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"threadId\":7") != NULL);

	ASSERT_TRUE(editorDapPause());
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"pause\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"threadId\":7") != NULL);

	editorDapEndSessionForTest();
	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}

#define DAP_BTN_BIT(a) (1u << (unsigned int)((a) - EDITOR_ACTION_DAP_DRAWER))

static unsigned int dap_status_button_actions(int window_cols) {
	unsigned int mask = 0;
	for (int c = 0; c < window_cols; c++) {
		int action = 0;
		if (editorStatusBarDebugButtonAt(c, &action)) {
			mask |= DAP_BTN_BIT(action);
		}
	}
	return mask;
}

static int test_editor_dap_status_bar_controls(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 6;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;
	size_t n = 0;

	/* Stopped: PAUSED badge + full step controls; clickable spans resolve. */
	E.dap_running = 1;
	E.dap_stopped = 1;
	char *out = refresh_screen_and_capture(&n);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "PAUSED") != NULL);
	ASSERT_TRUE(strstr(out, "Cont") != NULL && strstr(out, "Over") != NULL &&
	            strstr(out, "Stop") != NULL);
	ASSERT_TRUE(strstr(out, "RUNNING") == NULL);
	free(out);
	ASSERT_EQ_INT((int)(DAP_BTN_BIT(EDITOR_ACTION_DAP_CONTINUE) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STEP_OVER) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STEP_INTO) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STEP_OUT) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_RESTART) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STOP)),
	              (int)dap_status_button_actions(E.window_cols));

	/* Running: RUNNING badge + Pause/Restart/Stop; no step controls. */
	E.dap_stopped = 0;
	out = refresh_screen_and_capture(&n);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "RUNNING") != NULL && strstr(out, "Pause") != NULL);
	ASSERT_TRUE(strstr(out, "Cont") == NULL && strstr(out, "Over") == NULL);
	free(out);
	ASSERT_EQ_INT((int)(DAP_BTN_BIT(EDITOR_ACTION_DAP_PAUSE) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_RESTART) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STOP)),
	              (int)dap_status_button_actions(E.window_cols));

	/* No session: no debug segment, no clickable buttons. */
	E.dap_running = 0;
	E.dap_stopped = 0;
	out = refresh_screen_and_capture(&n);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "PAUSED") == NULL && strstr(out, "RUNNING") == NULL);
	free(out);
	ASSERT_EQ_INT(0, (int)dap_status_button_actions(E.window_cols));
	return 0;
}

static int test_editor_dap_evaluate_request_builder(void) {
	char *scoped = editorDapBuildEvaluateRequestJson(9, "argc + 1", 3, "repl");
	ASSERT_TRUE(scoped != NULL);
	ASSERT_TRUE(strstr(scoped, "\"command\":\"evaluate\"") != NULL);
	ASSERT_TRUE(strstr(scoped, "\"expression\":\"argc + 1\"") != NULL);
	ASSERT_TRUE(strstr(scoped, "\"context\":\"repl\"") != NULL);
	ASSERT_TRUE(strstr(scoped, "\"frameId\":3") != NULL);
	free(scoped);
	char *global = editorDapBuildEvaluateRequestJson(10, "x", 0, "repl");
	ASSERT_TRUE(global != NULL);
	ASSERT_TRUE(strstr(global, "\"frameId\"") == NULL); /* omitted with no frame */
	free(global);
	return 0;
}

static int test_editor_dap_evaluate_repl_flow(void) {
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	int flags = fcntl(fds[0], F_GETFL, 0);
	ASSERT_TRUE(flags != -1 && fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != -1);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));
	E.dap_stopped = 1;
	E.dap_stack_frame_count = 1;
	E.dap_stack_frames[0].id = 3;
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';

	/* Evaluate echoes the expression to the console and scopes to the top frame. */
	ASSERT_TRUE(editorDapEvaluate("argc + 1"));
	char buf[2048];
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"command\":\"evaluate\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"expression\":\"argc + 1\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"frameId\":3") != NULL);
	ASSERT_TRUE(strstr(buf, "\"context\":\"repl\"") != NULL);
	ASSERT_TRUE(strstr(E.dap_output, "> argc + 1") != NULL);

	/* The result is appended to the console and surfaced in the status bar. */
	E.statusmsg[0] = '\0';
	(void)editorDapProcessIncomingMessage("{\"type\":\"response\",\"command\":\"evaluate\","
	                                      "\"success\":true,\"body\":{\"result\":\"2\"}}");
	ASSERT_TRUE(strstr(E.dap_output, "= 2") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "2") != NULL);

	editorDapEndSessionForTest();
	E.dap_stopped = 0;
	E.dap_stack_frame_count = 0;
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	(void)close(fds[0]);
	(void)close(fds[1]);

	/* No session: evaluate is rejected with a clear status. */
	E.dap_running = 0;
	E.statusmsg[0] = '\0';
	ASSERT_EQ_INT(0, editorDapEvaluate("x"));
	ASSERT_TRUE(strstr(E.statusmsg, "No DAP session") != NULL);
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
        {"editor_dap_failed_response_surfaces_message",
         test_editor_dap_failed_response_surfaces_message},
        {"editor_dap_configuration_done_failure_is_benign",
         test_editor_dap_configuration_done_failure_is_benign},
        {"editor_dap_stopped_chain_populates_state",
         test_editor_dap_stopped_chain_populates_state},
        {"editor_dap_output_event_reads_body_output",
         test_editor_dap_output_event_reads_body_output},
        {"editor_dap_continued_clears_inspection_state",
         test_editor_dap_continued_clears_inspection_state},
        {"editor_dap_response_parsing_tolerates_malformed",
         test_editor_dap_response_parsing_tolerates_malformed},
        {"editor_dap_breakpoint_toggle_at_line_and_stopped_predicate",
         test_editor_dap_breakpoint_toggle_at_line_and_stopped_predicate},
        {"editor_dap_gutter_renders_markers", test_editor_dap_gutter_renders_markers},
        {"editor_dap_control_requests_include_thread_id",
         test_editor_dap_control_requests_include_thread_id},
        {"editor_dap_status_bar_controls", test_editor_dap_status_bar_controls},
        {"editor_dap_evaluate_request_builder", test_editor_dap_evaluate_request_builder},
        {"editor_dap_evaluate_repl_flow", test_editor_dap_evaluate_repl_flow},
};

const int g_dap_test_count = (int)(sizeof(g_dap_tests) / sizeof(g_dap_tests[0]));
