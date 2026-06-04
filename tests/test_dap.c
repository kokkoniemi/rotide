#include "config/common.h"
#include "config/dap_config.h"
#include "config/theme_config.h"
#include "debug/dap.h"
#include "debug/dap_console.h"
#include "input/mouse.h"
#include "language/syntax.h"
#include "render/drawer_view.h"
#include "render/screen.h"
#include "render/status_bar.h"
#include "render/viewport.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
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
	ASSERT_TRUE(editorTabsInit());
	E.dap_console_leaf = NULL;

	struct editorDapLaunchConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	(void)snprintf(cfg.id, sizeof(cfg.id), "%s", "go_app");
	(void)snprintf(cfg.adapter, sizeof(cfg.adapter), "%s", "go");
	(void)snprintf(cfg.request, sizeof(cfg.request), "%s", "launch");
	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "console", "terminal"));

	char tty[256] = "";
	int ok = editorDapPrepareTerminalConsole(&cfg, tty, sizeof(tty));
	ASSERT_TRUE(ok);
	/* The panel is a normal pane hosting a Terminal tab (active) and a Debug
	 * Console tab. */
	ASSERT_TRUE(E.dap_console_leaf != NULL);
	ASSERT_EQ_INT(2, E.dap_console_leaf->as.leaf.view.pane_tab_count);
	ASSERT_TRUE(editorPaneActiveKind(E.dap_console_leaf) == EDITOR_PANE_KIND_TERMINAL);
	ASSERT_TRUE(editorTerminalPaneForPane(E.dap_console_leaf) != NULL);

	/* The console field is gone, replaced by a real tty path reported back. */
	char value[ROTIDE_DAP_VALUE_MAX];
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "console", value, sizeof(value)) == 0);
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "tty", value, sizeof(value)));
	ASSERT_TRUE(strstr(value, "/dev/pts/") != NULL || strstr(value, "/dev/tty") != NULL);
	ASSERT_TRUE(strstr(tty, "/dev/pts/") != NULL || strstr(tty, "/dev/tty") != NULL);

	/* Cleanup: close the owned panel (frees the debuggee terminal + console). */
	editorDapConsoleCloseOwnedTerminalPane();
	return 0;
}

static int test_editor_dap_prepare_terminal_console_strips_non_terminal_value(void) {
	ASSERT_TRUE(editorTabsInit());
	struct editorDapLaunchConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	(void)snprintf(cfg.id, sizeof(cfg.id), "%s", "test");
	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "console", "internalConsole"));
	char tty[256] = "x";
	ASSERT_TRUE(editorDapPrepareTerminalConsole(&cfg, tty, sizeof(tty)));
	char value[ROTIDE_DAP_VALUE_MAX];
	/* A non-"terminal" console value is stripped and hosts no debuggee tty. */
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "console", value, sizeof(value)) == 0);
	ASSERT_TRUE(editorDapLaunchGetStringField(&cfg, "tty", value, sizeof(value)) == 0);
	ASSERT_EQ_STR("", tty);
	if (E.dap_console_leaf != NULL) {
		/* Console-only panel: a single Debug Console tab, no terminal tab. */
		ASSERT_EQ_INT(1, E.dap_console_leaf->as.leaf.view.pane_tab_count);
		ASSERT_TRUE(editorPaneActiveKind(E.dap_console_leaf) ==
		            EDITOR_PANE_KIND_DEBUG_CONSOLE);
		editorDapConsoleCloseOwnedTerminalPane();
	}
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
	ASSERT_TRUE(strstr(init, "\"supportsVariableType\":true") != NULL);
	ASSERT_TRUE(strstr(init, "\"supportsMemoryReferences\":true") != NULL);
	ASSERT_TRUE(strstr(init, "\"supportsVariablePaging\":true") != NULL);
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
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"event\",\"event\":\"stopped\","
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

static int test_editor_dap_variable_child_preview_populates_arrays(void) {
	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	int flags = fcntl(fds[0], F_GETFL, 0);
	ASSERT_TRUE(flags != -1 && fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != -1);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));
	char buf[8192];

	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,"
	        "\"body\":{\"scopes\":[{\"variablesReference\":1,\"name\":\"Arguments\"},"
	        "{\"variablesReference\":2,\"name\":\"Locals\"},"
	        "{\"variablesReference\":3,\"name\":\"Registers\"}]}}");
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":1") != NULL);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":2") != NULL);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":3") != NULL);

	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":3,\"body\":{\"variables\":[{\"name\":\"numbers\","
	        "\"value\":\"0x7ffd5c2a10\",\"type\":\"int [6]\","
	        "\"variablesReference\":44,\"indexedVariables\":6},{\"name\":\"items\","
	        "\"value\":\"0x555555\",\"type\":\"struct item *\","
	        "\"variablesReference\":45,\"namedVariables\":2},{\"name\":\"broken\","
	        "\"value\":\"<optimized out>\",\"variablesReference\":46,"
	        "\"namedVariables\":1}]}}");
	ASSERT_EQ_INT(3, E.dap_variable_count);
	ASSERT_EQ_STR("numbers", E.dap_variables[0].name);
	ASSERT_EQ_STR("int [6]", E.dap_variables[0].type);
	ASSERT_TRUE(dap_drain_fd(fds[0], buf, sizeof(buf)) > 0);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":44") != NULL);
	ASSERT_TRUE(strstr(buf, "\"count\":6") != NULL);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":45") != NULL);
	ASSERT_TRUE(strstr(buf, "\"count\":2") != NULL);
	ASSERT_TRUE(strstr(buf, "\"variablesReference\":46") != NULL);
	ASSERT_TRUE(strstr(buf, "\"count\":1") != NULL);

	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":5,\"body\":{\"variables\":["
	        "{\"name\":\"[0]\",\"value\":\"3\"},{\"name\":\"[1]\",\"value\":\"5\"},"
	        "{\"name\":\"[2]\",\"value\":\"8\"},{\"name\":\"[3]\",\"value\":\"13\"},"
	        "{\"name\":\"[4]\",\"value\":\"21\"},{\"name\":\"[5]\",\"value\":\"34\"}]}}");
	ASSERT_EQ_INT(3, E.dap_variable_count);
	ASSERT_EQ_STR("{3,5,8,13,21,34}", E.dap_variables[0].preview);

	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":6,\"body\":{\"variables\":["
	        "{\"name\":\"score\",\"value\":\"3\"},{\"name\":\"next\",\"value\":\"0x0\"}]}}");
	ASSERT_EQ_INT(3, E.dap_variable_count);
	ASSERT_EQ_STR("{score=3,next=0x0}", E.dap_variables[1].preview);

	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":false,"
	        "\"request_seq\":7,\"message\":\"list index is out of range\"}");
	ASSERT_EQ_INT(3, E.dap_variable_count);
	ASSERT_TRUE(strstr(E.dap_output, "list index is out of range") == NULL);

	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":999,\"body\":{\"variables\":[{\"name\":\"stale\","
	        "\"value\":\"should not append\"}]}}");
	ASSERT_EQ_INT(3, E.dap_variable_count);

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
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"event\",\"event\":\"stopped\","
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

static int test_editor_dap_console_pane_renders_and_toggles(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 12;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;
	E.dap_console_leaf = NULL;
	const char *transcript = "line-one\nline-two\nline-three\n";
	size_t tlen = strlen(transcript);
	memcpy(E.dap_output, transcript, tlen);
	E.dap_output_len = tlen;
	E.dap_output[tlen] = '\0';

	/* Toggle opens a console-only panel (bottom split), focused, Debug Console
	 * tab active. */
	ASSERT_TRUE(editorDapConsoleToggle());
	ASSERT_TRUE(E.dap_console_leaf != NULL);
	ASSERT_TRUE(editorPaneActiveKind(E.dap_console_leaf) == EDITOR_PANE_KIND_DEBUG_CONSOLE);
	ASSERT_EQ_INT(1, E.dap_console_leaf->as.leaf.view.pane_tab_count); /* console only */
	ASSERT_TRUE(E.focused_leaf == E.dap_console_leaf);
	struct editorDapConsolePane *console = editorDapConsoleForPane(E.dap_console_leaf);
	ASSERT_TRUE(console != NULL);

	size_t n = 0;
	char *out = refresh_screen_and_capture(&n);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "Debug Console") != NULL); /* tab strip label */
	ASSERT_TRUE(strstr(out, "line-three") != NULL);    /* tail of transcript visible */
	free(out);

	/* Scroll clamps to [0, line count]. */
	editorDapConsoleScroll(console, 100);
	ASSERT_TRUE(console->scroll > 0 && console->scroll <= 3);
	editorDapConsoleScroll(console, -100);
	ASSERT_EQ_INT(0, console->scroll);

	/* A second toggle keeps the panel open and focused on the Console tab. */
	ASSERT_TRUE(editorDapConsoleToggle());
	ASSERT_TRUE(E.dap_console_leaf != NULL);
	ASSERT_TRUE(editorPaneActiveKind(E.dap_console_leaf) == EDITOR_PANE_KIND_DEBUG_CONSOLE);

	/* Cleanup: close the panel. */
	editorDapConsoleCloseOwnedTerminalPane();
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	return 0;
}

static int test_editor_dap_console_wheel_scrolls_transcript(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 10;
	E.window_cols = 80;
	char transcript[256];
	int len = 0;
	for (int i = 0; i < 20; i++) {
		len += snprintf(transcript + len, sizeof(transcript) - (size_t)len, "line-%d\n", i);
	}
	memcpy(E.dap_output, transcript, (size_t)len);
	E.dap_output_len = (size_t)len;
	E.dap_output[len] = '\0';

	E.dap_console_leaf = NULL;
	ASSERT_TRUE(editorDapConsoleToggle());
	struct editorDapConsolePane *console = editorDapConsoleForPane(E.dap_console_leaf);
	ASSERT_TRUE(console != NULL);
	ASSERT_EQ_INT(0, console->scroll);

	struct editorRect rect = {0};
	ASSERT_TRUE(editorLayoutFocusedLeafRect(&rect));
	struct editorMouseEvent wheel = {
	        .kind = EDITOR_MOUSE_EVENT_WHEEL_UP,
	        .x = rect.x + 2,
	        .y = rect.y + 2,
	        .modifiers = 0,
	};
	ASSERT_TRUE(editorHandleMouseWheel(&wheel));
	ASSERT_TRUE(console->scroll > 0); /* wheel-up scrolls toward older output */
	int scrolled = console->scroll;

	wheel.kind = EDITOR_MOUSE_EVENT_WHEEL_DOWN;
	ASSERT_TRUE(editorHandleMouseWheel(&wheel));
	ASSERT_TRUE(console->scroll < scrolled);

	editorDapConsoleCloseOwnedTerminalPane();
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	return 0;
}

static int test_editor_dap_console_cursor_sits_on_input_line(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 12;
	E.window_cols = 80;
	E.dap_console_leaf = NULL;

	ASSERT_TRUE(editorDapConsoleToggle());
	struct editorDapConsolePane *console = editorDapConsoleForPane(E.dap_console_leaf);
	ASSERT_TRUE(console != NULL);
	const char *typed = "abc";
	memcpy(console->input, typed, 3);
	console->input[3] = '\0';
	console->input_len = 3;

	/* The single hardware cursor is placed on the "> abc" input line (last pane
	 * row), after the typed text — no separate painted caret. */
	struct editorRect rect = {0};
	ASSERT_TRUE(editorLayoutFocusedLeafRect(&rect));
	char expected[32];
	int n = snprintf(expected, sizeof(expected), "\x1b[%d;%dH", rect.y + rect.h,
	                 rect.x + 2 + console->input_len + 1);
	ASSERT_TRUE(n > 0 && n < (int)sizeof(expected));

	size_t out_len = 0;
	char *out = refresh_screen_and_capture(&out_len);
	ASSERT_TRUE(out != NULL);
	int found = strstr(out, expected) != NULL;
	int has_input = strstr(out, "> abc") != NULL;
	free(out);
	ASSERT_TRUE(found);
	ASSERT_TRUE(has_input);

	editorDapConsoleCloseOwnedTerminalPane();
	return 0;
}

static int test_editor_dap_adapter_command_tty(void) {
	char out[256];
	/* gdb gets --tty appended when a debuggee tty is in use. */
	editorDapBuildAdapterCommand("gdb -i dap", "/dev/pts/7", out, sizeof(out));
	ASSERT_EQ_STR("gdb -i dap --tty=/dev/pts/7", out);
	/* No tty -> unchanged. */
	editorDapBuildAdapterCommand("gdb -i dap", "", out, sizeof(out));
	ASSERT_EQ_STR("gdb -i dap", out);
	/* Non-gdb adapters are left alone (they use the launch `tty` argument). */
	editorDapBuildAdapterCommand("lldb-dap", "/dev/pts/7", out, sizeof(out));
	ASSERT_EQ_STR("lldb-dap", out);
	return 0;
}

static int test_editor_dap_output_events_go_to_console(void) {
	/* Adapter output events (any category) land in the Debug Console transcript;
	 * the debuggee's own stdout reaches the Terminal tab via the real tty, not
	 * through output events. */
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"event\",\"event\":\"output\","
	        "\"body\":{\"category\":\"stdout\",\"output\":\"GNU gdb banner\\n\"}}");
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"event\",\"event\":\"output\","
	        "\"body\":{\"category\":\"console\",\"output\":\"adapter note\\n\"}}");
	ASSERT_TRUE(strstr(E.dap_output, "GNU gdb banner") != NULL);
	ASSERT_TRUE(strstr(E.dap_output, "adapter note") != NULL);
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	return 0;
}

static int test_editor_dap_console_panel_switches_terminal_and_console_tabs(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	ASSERT_TRUE(editorTabsInit());
	E.dap_console_leaf = NULL;

	struct editorDapLaunchConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	(void)snprintf(cfg.id, sizeof(cfg.id), "%s", "go_app");
	(void)snprintf(cfg.adapter, sizeof(cfg.adapter), "%s", "go");
	(void)snprintf(cfg.request, sizeof(cfg.request), "%s", "launch");
	ASSERT_TRUE(editorDapLaunchSetStringField(&cfg, "console", "terminal"));
	char tty[256] = "";
	ASSERT_TRUE(editorDapPrepareTerminalConsole(&cfg, tty, sizeof(tty)));
	ASSERT_TRUE(E.dap_console_leaf != NULL);

	struct editorPaneView *v = &E.dap_console_leaf->as.leaf.view;
	ASSERT_EQ_INT(2, v->pane_tab_count);
	/* Terminal tab is active on launch. */
	ASSERT_TRUE(editorPaneActiveKind(E.dap_console_leaf) == EDITOR_PANE_KIND_TERMINAL);

	/* Switching to the Debug Console tab uses the normal tab path (no bespoke
	 * dap_panel_tab); the active kind flips and the console payload is reachable. */
	int console_tab = -1;
	for (int i = 0; i < v->pane_tab_count; i++) {
		if (editorTabKindAt(v->pane_tabs[i]) == EDITOR_PANE_KIND_DEBUG_CONSOLE) {
			console_tab = v->pane_tabs[i];
		}
	}
	ASSERT_TRUE(console_tab >= 0);
	ASSERT_TRUE(editorTabSwitchToIndex(console_tab));
	ASSERT_TRUE(editorPaneActiveKind(E.dap_console_leaf) == EDITOR_PANE_KIND_DEBUG_CONSOLE);
	ASSERT_TRUE(editorDapConsoleForPane(E.dap_console_leaf) != NULL);

	editorDapConsoleCloseOwnedTerminalPane();
	return 0;
}

static int test_editor_dap_stopped_line_highlighted(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 6;
	E.window_cols = 40;
	add_row("line zero");
	add_row("line one");
	add_row("line two");
	E.filename = strdup("/tmp/rotide-stopped-line-xyz.c");
	ASSERT_TRUE(E.filename != NULL);
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	/* A distinct stopped-line tint so we can spot it in the frame. */
	struct editorThemeColor tint = {.kind = EDITOR_THEME_COLOR_RGB, .r = 60, .g = 50, .b = 10};
	E.theme.ui[EDITOR_THEME_UI_DEBUG_STOPPED_LINE_BG] = tint;

	/* Stop on the second line (1-based frame line 2 -> buffer row 1). */
	E.dap_running = 1;
	E.dap_stopped = 1;
	E.dap_stack_frame_count = 1;
	(void)snprintf(E.dap_stack_frames[0].path, sizeof(E.dap_stack_frames[0].path), "%s",
	               E.filename);
	E.dap_stack_frames[0].line = 2;

	ASSERT_TRUE(editorDebugStoppedLineHighlightApplies(1));
	ASSERT_TRUE(!editorDebugStoppedLineHighlightApplies(0));

	size_t n = 0;
	char *out = refresh_screen_and_capture(&n);
	ASSERT_TRUE(out != NULL);
	ASSERT_TRUE(strstr(out, "\x1b[48;2;60;50;10m") != NULL); /* stopped-line bg tint */
	free(out);
	return 0;
}

static int test_editor_dap_status_bar_controls_nerd_icons(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 6;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;
	E.nerd_fonts_enabled = 1;
	E.dap_running = 1;
	E.dap_stopped = 1;
	/* Accent icons draw from the theme's ANSI palette (green/yellow/red slots). */
	struct editorThemeColor green = {
	        .kind = EDITOR_THEME_COLOR_RGB, .r = 10, .g = 200, .b = 20};
	struct editorThemeColor yellow = {
	        .kind = EDITOR_THEME_COLOR_RGB, .r = 230, .g = 150, .b = 0};
	struct editorThemeColor red = {.kind = EDITOR_THEME_COLOR_RGB, .r = 220, .g = 30, .b = 30};
	E.theme.ansi[EDITOR_THEME_ANSI_GREEN] = green;
	E.theme.ansi[EDITOR_THEME_ANSI_YELLOW] = yellow;
	E.theme.ansi[EDITOR_THEME_ANSI_RED] = red;

	size_t n = 0;
	char *out = refresh_screen_and_capture(&n);
	ASSERT_TRUE(out != NULL);
	/* Step controls keep their text beside an icon; restart/stop are icon-only. */
	ASSERT_TRUE(strstr(out, "Cont") != NULL && strstr(out, "Over") != NULL &&
	            strstr(out, "Into") != NULL && strstr(out, "Out") != NULL);
	ASSERT_TRUE(strstr(out, "Restart") == NULL && strstr(out, "Stop") == NULL);
	/* The restart glyph (U+F021) is emitted. */
	ASSERT_TRUE(strstr(out, "\xEF\x80\xA1") != NULL);
	/* Accent colors resolve from the theme palette: green continue, yellow
	 * restart, red stop. */
	ASSERT_TRUE(strstr(out, "\x1b[38;2;10;200;20m") != NULL);
	ASSERT_TRUE(strstr(out, "\x1b[38;2;230;150;0m") != NULL);
	ASSERT_TRUE(strstr(out, "\x1b[38;2;220;30;30m") != NULL);
	free(out);

	/* All six controls remain clickable — the icon columns map to actions. */
	ASSERT_EQ_INT((int)(DAP_BTN_BIT(EDITOR_ACTION_DAP_CONTINUE) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STEP_OVER) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STEP_INTO) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STEP_OUT) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_RESTART) |
	                    DAP_BTN_BIT(EDITOR_ACTION_DAP_STOP)),
	              (int)dap_status_button_actions(E.window_cols));
	return 0;
}

static int test_editor_dap_stop_centers_viewport_on_current_line(void) {
	/* On a stop, the stackTrace response reveals the top frame's line in a
	 * source pane: it opens the file, moves the cursor onto the line, and
	 * centers the viewport so the line sits mid-buffer, not pinned to an edge. */
	ASSERT_TRUE(editorTabsInit());

	/* 40 numbered lines give a mid-file stop room above and below. */
	char src[1024];
	src[0] = '\0';
	for (int i = 1; i <= 40; i++) {
		char line[32];
		(void)snprintf(line, sizeof(line), "line %d\n", i);
		(void)strncat(src, line, sizeof(src) - strlen(src) - 1);
	}
	char path[64];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), src));
	ASSERT_TRUE(editorTabOpenOrSwitchToFile(path));
	ASSERT_EQ_INT(EDITOR_PANE_KIND_EDITOR, editorPaneActiveKind(E.focused_leaf));

	E.window_rows = 21;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));

	/* Stop with the top frame on (1-based) line 20 of the open file. */
	char msg[512];
	(void)snprintf(msg, sizeof(msg),
	               "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,"
	               "\"body\":{\"stackFrames\":[{\"id\":0,\"line\":20,\"column\":0,"
	               "\"name\":\"main\",\"source\":{\"path\":\"%s\"}}]}}",
	               path);
	(void)editorDapProcessIncomingMessage(msg);

	/* Cursor lands on the stopped line (0-based 19); the line is centered in
	 * the body, not at the top edge or the last row. */
	ASSERT_EQ_INT(EDITOR_PANE_KIND_EDITOR, editorPaneActiveKind(E.focused_leaf));
	ASSERT_EQ_INT(19, E.cy);
	int body_rows = editorViewportFocusedPaneBodyRows();
	int center = body_rows > 0 ? body_rows / 2 : 0;
	ASSERT_EQ_INT(center, E.cy - E.rowoff);
	ASSERT_TRUE(E.rowoff > 0);

	editorDapEndSessionForTest();
	(void)close(fds[0]);
	(void)close(fds[1]);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_dap_stop_in_missing_source_does_not_error(void) {
	/* Stepping into a frame with no local source (e.g. printf landing in glibc
	 * that is not installed) must not pop an "Unable to open file" error and
	 * must leave the current buffer/cursor untouched. */
	ASSERT_TRUE(editorTabsInit());

	char path[64];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), "int main(void) { return 0; }\n"));
	ASSERT_TRUE(editorTabOpenOrSwitchToFile(path));
	char *open_file = E.filename;
	int saved_cy = E.cy;

	E.window_rows = 21;
	E.window_cols = 80;
	(void)snprintf(E.statusmsg, sizeof(E.statusmsg), "%s", "SENTINEL");

	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));

	/* Top frame points at a libc source path that does not exist on disk. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,"
	        "\"body\":{\"stackFrames\":[{\"id\":0,\"line\":42,\"column\":0,\"name\":\"printf\","
	        "\"source\":{\"name\":\"printf.c\",\"path\":\"/nonexistent/glibc/printf.c\"}}]}}");

	/* The frame is recorded, but no navigation and no error happened. */
	ASSERT_EQ_INT(1, E.dap_stack_frame_count);
	ASSERT_EQ_STR("SENTINEL", E.statusmsg);
	ASSERT_TRUE(E.filename == open_file);
	ASSERT_EQ_INT(saved_cy, E.cy);

	editorDapEndSessionForTest();
	(void)close(fds[0]);
	(void)close(fds[1]);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int dap_var_scope_by_name(const char *name) {
	for (int i = 0; i < E.dap_variable_count; i++) {
		if (strcmp(E.dap_variables[i].name, name) == 0) {
			return E.dap_variables[i].scope_index;
		}
	}
	return -1;
}

static int dap_var_index_by_name(const char *name) {
	for (int i = 0; i < E.dap_variable_count; i++) {
		if (strcmp(E.dap_variables[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

static void dap_seed_semantic_drawer_state(void) {
	E.drawer_mode = EDITOR_DRAWER_MODE_DAP;
	E.drawer_dap_expanded = 0xFFFFFFFFu;
	E.drawer_dap_scope_collapsed = 0;
	E.window_rows = 40;
	E.window_cols = 120;
	E.drawer_width_cols = 90;

	E.dap_adapter_count = 1;
	(void)snprintf(E.dap_adapters[0].id, sizeof(E.dap_adapters[0].id), "%s", "mock");
	(void)snprintf(E.dap_adapters[0].command, sizeof(E.dap_adapters[0].command), "%s",
	               "mock-dap");
	E.dap_launch_count = 1;
	(void)snprintf(E.dap_launches[0].id, sizeof(E.dap_launches[0].id), "%s", "sample");
	(void)snprintf(E.dap_launches[0].name, sizeof(E.dap_launches[0].name), "%s",
	               "Launch sample");
	(void)snprintf(E.dap_launches[0].adapter, sizeof(E.dap_launches[0].adapter), "%s", "mock");
	(void)snprintf(E.dap_launches[0].request, sizeof(E.dap_launches[0].request), "%s",
	               "launch");

	E.dap_breakpoint_count = 2;
	memset(&E.dap_breakpoints[0], 0, sizeof(E.dap_breakpoints[0]));
	E.dap_breakpoints[0].kind = EDITOR_DAP_BREAKPOINT_LINE;
	(void)snprintf(E.dap_breakpoints[0].path, sizeof(E.dap_breakpoints[0].path), "%s",
	               "/tmp/main.c");
	E.dap_breakpoints[0].line = 3;
	memset(&E.dap_breakpoints[1], 0, sizeof(E.dap_breakpoints[1]));
	E.dap_breakpoints[1].kind = EDITOR_DAP_BREAKPOINT_CONDITIONAL;
	(void)snprintf(E.dap_breakpoints[1].path, sizeof(E.dap_breakpoints[1].path), "%s",
	               "/tmp/branch.c");
	E.dap_breakpoints[1].line = 7;

	E.dap_stack_frame_count = 1;
	memset(&E.dap_stack_frames[0], 0, sizeof(E.dap_stack_frames[0]));
	E.dap_stack_frames[0].id = 22;
	(void)snprintf(E.dap_stack_frames[0].name, sizeof(E.dap_stack_frames[0].name), "%s",
	               "main");
	(void)snprintf(E.dap_stack_frames[0].path, sizeof(E.dap_stack_frames[0].path), "%s",
	               "/tmp/main.c");
	E.dap_stack_frames[0].line = 12;
	E.dap_stack_frames[0].column = 5;

	E.dap_thread_count = 1;
	E.dap_threads[0].id = 2;
	(void)snprintf(E.dap_threads[0].name, sizeof(E.dap_threads[0].name), "%s",
	               "dap_sample.out");

	E.dap_scope_count = 2;
	(void)snprintf(E.dap_scopes[0].name, sizeof(E.dap_scopes[0].name), "%s", "Arguments");
	E.dap_scopes[0].variables_reference = 10;
	(void)snprintf(E.dap_scopes[1].name, sizeof(E.dap_scopes[1].name), "%s", "Locals");
	E.dap_scopes[1].variables_reference = 11;

	E.dap_variable_count = 5;
	memset(E.dap_variables, 0, sizeof(*E.dap_variables) * (size_t)E.dap_variable_count);
	E.dap_variables[0].scope_index = 0;
	E.dap_variables[0].variables_reference = 0;
	(void)snprintf(E.dap_variables[0].name, sizeof(E.dap_variables[0].name), "%s", "argc");
	(void)snprintf(E.dap_variables[0].type, sizeof(E.dap_variables[0].type), "%s", "int");
	(void)snprintf(E.dap_variables[0].value, sizeof(E.dap_variables[0].value), "%s", "1");
	(void)snprintf(E.dap_variables[0].memory_reference,
	               sizeof(E.dap_variables[0].memory_reference), "%s", "0x1");
	E.dap_variables[1].scope_index = 0;
	E.dap_variables[1].variables_reference = 42;
	(void)snprintf(E.dap_variables[1].name, sizeof(E.dap_variables[1].name), "%s", "argv");
	(void)snprintf(E.dap_variables[1].type, sizeof(E.dap_variables[1].type), "%s", "char**");
	(void)snprintf(E.dap_variables[1].value, sizeof(E.dap_variables[1].value), "%s",
	               "0x7ffd5c2a30");
	E.dap_variables[2].scope_index = 1;
	(void)snprintf(E.dap_variables[2].name, sizeof(E.dap_variables[2].name), "%s", "mode");
	(void)snprintf(E.dap_variables[2].type, sizeof(E.dap_variables[2].type), "%s", "enum");
	(void)snprintf(E.dap_variables[2].value, sizeof(E.dap_variables[2].value), "%s", "NORMAL");
	(void)snprintf(E.dap_variables[2].memory_reference,
	               sizeof(E.dap_variables[2].memory_reference), "%s", "0x0");
	E.dap_variables[3].scope_index = 1;
	(void)snprintf(E.dap_variables[3].name, sizeof(E.dap_variables[3].name), "%s", "numbers");
	(void)snprintf(E.dap_variables[3].type, sizeof(E.dap_variables[3].type), "%s", "int[6]");
	(void)snprintf(E.dap_variables[3].value, sizeof(E.dap_variables[3].value), "%s",
	               "0x7ffd5c2a10");
	(void)snprintf(E.dap_variables[3].preview, sizeof(E.dap_variables[3].preview), "%s",
	               "{3,5,...}");
	(void)snprintf(E.dap_variables[3].memory_reference,
	               sizeof(E.dap_variables[3].memory_reference), "%s", "0x7ffd5c2a10");
	E.dap_variables[4].scope_index = 1;
	(void)snprintf(E.dap_variables[4].name, sizeof(E.dap_variables[4].name), "%s", "items");
	(void)snprintf(E.dap_variables[4].type, sizeof(E.dap_variables[4].type), "%s", "item*");
	(void)snprintf(E.dap_variables[4].value, sizeof(E.dap_variables[4].value), "%s", "NULL");
	(void)snprintf(E.dap_variables[4].memory_reference,
	               sizeof(E.dap_variables[4].memory_reference), "%s", "0x0");
}

static int dap_render_drawer_entry(const char *name, char **row_out) {
	if (row_out == NULL) {
		return 0;
	}
	*row_out = NULL;
	int idx = -1;
	if (!find_drawer_entry(name, &idx, NULL)) {
		return 0;
	}
	struct writeBuf wb = WRITEBUF_INIT;
	if (!editorDrawDrawerRow(&wb, idx + 1, 90) || !wbAppend(&wb, "\0", 1)) {
		wbFree(&wb);
		return 0;
	}
	*row_out = wb.b;
	return 1;
}

static int test_editor_dap_drawer_top_level_order_and_collapse_semantics(void) {
	ASSERT_TRUE(editorTabsInit());
	dap_seed_semantic_drawer_state();
	E.drawer_dap_expanded = 0;

	const char *expected[] = {
	        "Configurations (1)", "Breakpoints (2)", "Variables", "Stack", "Threads", "Output"};
	ASSERT_EQ_INT(7, editorDrawerVisibleCount());
	for (int i = 0; i < 6; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i + 1, &view));
		ASSERT_EQ_STR(expected[i], view.name);
	}

	E.drawer_dap_expanded = 0xFFFFFFFFu;
	int threads_idx = -1;
	ASSERT_TRUE(find_drawer_entry("Threads", &threads_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(threads_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerCollapseSelection(E.window_rows));
	ASSERT_TRUE(!find_drawer_entry("dap_sample.out", NULL, NULL));
	ASSERT_TRUE(find_drawer_entry("main:12", NULL, NULL));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	ASSERT_TRUE(find_drawer_entry("dap_sample.out", NULL, NULL));
	return 0;
}

static int test_editor_dap_drawer_entry_view_semantics(void) {
	ASSERT_TRUE(editorTabsInit());
	dap_seed_semantic_drawer_state();

	struct editorDrawerEntryView view;
	ASSERT_TRUE(editorDrawerVisibleEntryView(0, &view));
	ASSERT_EQ_STR("Debugger", view.name);

	ASSERT_TRUE(find_drawer_entry("Launch sample", NULL, &view));
	ASSERT_EQ_INT((int)EDITOR_DRAWER_ENTRY_ICON_DAP_START, (int)view.icon_kind);
	ASSERT_EQ_INT((int)EDITOR_DRAWER_ENTRY_ICON_COLOR_DAP_START, (int)view.icon_color);

	ASSERT_TRUE(find_drawer_entry("main.c:4", NULL, &view));
	ASSERT_EQ_INT((int)EDITOR_DRAWER_ENTRY_ICON_DAP_BREAKPOINT, (int)view.icon_kind);
	ASSERT_EQ_INT((int)EDITOR_DAP_BREAKPOINT_LINE, (int)view.dap_breakpoint_kind);
	ASSERT_TRUE(view.path != NULL && strcmp(view.path, "/tmp/main.c") == 0);

	ASSERT_TRUE(find_drawer_entry("branch.c:8", NULL, &view));
	ASSERT_EQ_INT((int)EDITOR_DAP_BREAKPOINT_CONDITIONAL, (int)view.dap_breakpoint_kind);

	ASSERT_TRUE(find_drawer_entry("main:12", NULL, &view));
	ASSERT_EQ_INT((int)EDITOR_DRAWER_ENTRY_ICON_NONE, (int)view.icon_kind);
	ASSERT_TRUE(view.path != NULL && strcmp(view.path, "/tmp/main.c") == 0);

	ASSERT_TRUE(find_drawer_entry("dap_sample.out", NULL, &view));
	ASSERT_EQ_STR("dap_sample.out", view.name);
	ASSERT_EQ_STR("#2", view.prefix);
	ASSERT_TRUE(view.prefix_muted);
	ASSERT_EQ_INT((int)EDITOR_DRAWER_ENTRY_ICON_NONE, (int)view.icon_kind);

	ASSERT_TRUE(find_drawer_entry("argv", NULL, &view));
	ASSERT_EQ_INT((int)EDITOR_DRAWER_ENTRY_ICON_NONE, (int)view.icon_kind);
	ASSERT_EQ_STR("char**", view.detail_type);
	ASSERT_EQ_STR("0x7ffd5c2a30", view.detail_value);
	ASSERT_EQ_INT(42, view.variable_reference);

	ASSERT_TRUE(find_drawer_entry("argc", NULL, &view));
	ASSERT_EQ_STR("int", view.detail_type);
	ASSERT_EQ_STR("1", view.detail_value);
	ASSERT_EQ_STR("0x1", view.detail_address);

	ASSERT_TRUE(find_drawer_entry("numbers", NULL, &view));
	ASSERT_EQ_STR("int[6]", view.detail_type);
	ASSERT_EQ_STR("{3,5,...}", view.detail_preview);
	ASSERT_EQ_STR("0x7ffd5c2a10", view.detail_value);
	return 0;
}

static int test_editor_dap_drawer_renders_semantic_decorations(void) {
	ASSERT_TRUE(editorTabsInit());
	dap_seed_semantic_drawer_state();
	E.nerd_fonts_enabled = 1;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.theme.ansi[EDITOR_THEME_ANSI_GREEN] =
	        (struct editorThemeColor){.kind = EDITOR_THEME_COLOR_RGB, .r = 1, .g = 180, .b = 2};
	E.theme.ui[EDITOR_THEME_UI_BREAKPOINT] = (struct editorThemeColor){
	        .kind = EDITOR_THEME_COLOR_RGB, .r = 200, .g = 10, .b = 30};
	E.theme.ui[EDITOR_THEME_UI_DRAWER_CONNECTOR] =
	        (struct editorThemeColor){.kind = EDITOR_THEME_COLOR_RGB, .r = 9, .g = 8, .b = 7};
	E.theme.syntax[EDITOR_SYNTAX_HL_TYPE] =
	        (struct editorThemeColor){.kind = EDITOR_THEME_COLOR_RGB, .r = 3, .g = 4, .b = 5};
	E.theme.syntax[EDITOR_SYNTAX_HL_NUMBER] =
	        (struct editorThemeColor){.kind = EDITOR_THEME_COLOR_RGB, .r = 6, .g = 7, .b = 8};

	char *row = NULL;
	ASSERT_TRUE(dap_render_drawer_entry("Launch sample", &row));
	ASSERT_TRUE(strstr(row, "\x1b[38;2;1;180;2m") != NULL);
	ASSERT_TRUE(strstr(row, "\xEF\x81\x8B") != NULL);
	free(row);

	ASSERT_TRUE(dap_render_drawer_entry("main.c:4", &row));
	ASSERT_TRUE(strstr(row, "\x1b[38;2;200;10;30m") != NULL);
	ASSERT_TRUE(strstr(row, "\xE2\x97\x8F") != NULL);
	ASSERT_TRUE(strstr(row, "\xEF\x87\x89") == NULL);
	free(row);

	ASSERT_TRUE(dap_render_drawer_entry("main:12", &row));
	ASSERT_TRUE(strstr(row, "\xEF\x87\x89") == NULL);
	ASSERT_TRUE(strstr(row, "\xEF\x85\x9B") == NULL);
	free(row);

	ASSERT_TRUE(dap_render_drawer_entry("dap_sample.out", &row));
	ASSERT_TRUE(strstr(row, "\x1b[38;2;9;8;7m#2\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(row, "2 dap_sample.out") == NULL);
	free(row);

	ASSERT_TRUE(dap_render_drawer_entry("argv", &row));
	ASSERT_TRUE(strstr(row, "\xEF\x87\x89") == NULL);
	ASSERT_TRUE(strstr(row, "\xEF\x85\x9B") == NULL);
	ASSERT_TRUE(strstr(row, "\x1b[38;2;3;4;5mchar**") != NULL);
	ASSERT_TRUE(strstr(row, "->") != NULL);
	ASSERT_TRUE(strstr(row, "0x7ffd5c2a30") != NULL);
	free(row);

	ASSERT_TRUE(dap_render_drawer_entry("argc", &row));
	ASSERT_TRUE(strstr(row, "\x1b[38;2;3;4;5mint") != NULL);
	ASSERT_TRUE(strstr(row, "\x1b[38;2;6;7;8m1") != NULL);
	ASSERT_TRUE(strstr(row, "0x1") != NULL);
	free(row);

	ASSERT_TRUE(dap_render_drawer_entry("numbers", &row));
	ASSERT_TRUE(strstr(row, "\x1b[38;2;3;4;5mint[6]") != NULL);
	ASSERT_TRUE(strstr(row, "{3,5,...}") != NULL);
	ASSERT_TRUE(strstr(row, "0x7ffd5c2a10") != NULL);
	free(row);
	return 0;
}

static int test_editor_dap_drawer_groups_variables_by_scope(void) {
	/* Variables must be tagged with (and rendered under) their scope, so the
	 * drawer shows Arguments / Locals / Registers as separate collapsible
	 * categories rather than one flat dump with registers mixed in. */
	ASSERT_TRUE(editorTabsInit());

	int fds[2];
	ASSERT_TRUE(pipe(fds) == 0);
	editorDapBeginSessionForTest(fds[1], strdup("{}"));

	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,"
	        "\"body\":{\"scopes\":[{\"variablesReference\":1,\"name\":\"Arguments\"},"
	        "{\"variablesReference\":2,\"name\":\"Locals\"},"
	        "{\"variablesReference\":3,\"name\":\"Registers\"}]}}");
	ASSERT_EQ_INT(3, E.dap_scope_count);

	/* begin-for-test starts next_seq at 2, so the three variables requests use
	 * seq 2 (Arguments), 3 (Locals), 4 (Registers). Replies arrive out of order
	 * but request_seq keeps each variable under the right scope. */
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":4,\"body\":{\"variables\":[{\"name\":\"rax\",\"value\":\"0\"},"
	        "{\"name\":\"rbx\",\"value\":\"0\"}]}}");
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":2,\"body\":{\"variables\":[{\"name\":\"argc\",\"value\":\"1\","
	        "\"type\":\"int\",\"memoryReference\":\"0x1\"}]}}");
	(void)editorDapProcessIncomingMessage(
	        "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,"
	        "\"request_seq\":3,\"body\":{\"variables\":[{\"name\":\"total\",\"value\":\"84\"},"
	        "{\"name\":\"fib\",\"value\":\"144\"}]}}");
	ASSERT_EQ_INT(5, E.dap_variable_count);

	ASSERT_EQ_INT(2, dap_var_scope_by_name("rax"));
	ASSERT_EQ_INT(2, dap_var_scope_by_name("rbx"));
	ASSERT_EQ_INT(0, dap_var_scope_by_name("argc"));
	ASSERT_EQ_INT(1, dap_var_scope_by_name("total"));
	ASSERT_EQ_INT(1, dap_var_scope_by_name("fib"));
	int argc_idx = dap_var_index_by_name("argc");
	ASSERT_TRUE(argc_idx >= 0);
	ASSERT_EQ_STR("int", E.dap_variables[argc_idx].type);
	ASSERT_EQ_STR("0x1", E.dap_variables[argc_idx].memory_reference);

	/* Register scope auto-collapses once; Arguments/Locals stay expanded. */
	ASSERT_TRUE((E.drawer_dap_scope_collapsed & (1ull << 2)) != 0);
	ASSERT_TRUE((E.drawer_dap_scope_collapsed & (1ull << 0)) == 0);
	ASSERT_TRUE((E.drawer_dap_scope_collapsed & (1ull << 1)) == 0);

	E.drawer_mode = EDITOR_DRAWER_MODE_DAP;
	E.drawer_dap_expanded = 0xFFFFFFFFu; /* expand every top-level group */
	E.window_rows = 40;

	int saw_args = 0;
	int saw_locals = 0;
	int saw_regs = 0;
	int saw_argc = 0;
	int saw_total = 0;
	int saw_rax = 0;
	int args_depth = -1;
	int argc_depth = -1;
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
		if (strcmp(view.name, "Arguments (1)") == 0) {
			saw_args = 1;
			args_depth = view.depth;
			ASSERT_TRUE(view.is_dir && view.is_expanded);
		} else if (strcmp(view.name, "Locals (2)") == 0) {
			saw_locals = 1;
		} else if (strcmp(view.name, "Registers (2)") == 0) {
			saw_regs = 1;
			ASSERT_TRUE(view.is_dir && !view.is_expanded);
		} else if (strcmp(view.name, "argc") == 0) {
			saw_argc = 1;
			argc_depth = view.depth;
			ASSERT_EQ_STR("1", view.detail_value);
			ASSERT_EQ_STR("int", view.detail_type);
		} else if (strcmp(view.name, "total") == 0) {
			saw_total = 1;
		} else if (strcmp(view.name, "rax") == 0) {
			saw_rax = 1;
		}
	}
	ASSERT_TRUE(saw_args && saw_locals && saw_regs);
	ASSERT_TRUE(saw_argc && saw_total);
	ASSERT_EQ_INT(0, saw_rax); /* collapsed Registers hides its variables */
	ASSERT_EQ_INT(args_depth + 1, argc_depth);

	/* Expanding Registers reveals its variables. */
	E.drawer_dap_scope_collapsed &= ~(1ull << 2);
	saw_rax = 0;
	visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
		if (strcmp(view.name, "rax") == 0) {
			saw_rax = 1;
		}
	}
	ASSERT_TRUE(saw_rax);

	editorDapEndSessionForTest();
	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}

static int test_editor_dap_drawer_scope_toggle_collapses(void) {
	/* A scope header behaves like a collapsible directory for the shared
	 * keyboard/mouse toggle path. */
	ASSERT_TRUE(editorTabsInit());
	E.dap_running = 1;
	E.dap_stopped = 1;
	E.dap_scope_count = 2;
	memset(E.dap_scopes, 0, sizeof(*E.dap_scopes) * 2);
	(void)snprintf(E.dap_scopes[0].name, sizeof(E.dap_scopes[0].name), "%s", "Arguments");
	(void)snprintf(E.dap_scopes[1].name, sizeof(E.dap_scopes[1].name), "%s", "Locals");
	E.dap_variable_count = 2;
	memset(&E.dap_variables[0], 0, sizeof(E.dap_variables[0]));
	memset(&E.dap_variables[1], 0, sizeof(E.dap_variables[1]));
	(void)snprintf(E.dap_variables[0].name, sizeof(E.dap_variables[0].name), "%s", "argc");
	E.dap_variables[0].scope_index = 0;
	(void)snprintf(E.dap_variables[1].name, sizeof(E.dap_variables[1].name), "%s", "x");
	E.dap_variables[1].scope_index = 1;
	E.drawer_dap_scope_collapsed = 0;
	E.drawer_mode = EDITOR_DRAWER_MODE_DAP;
	E.drawer_dap_expanded = 0xFFFFFFFFu;
	E.window_rows = 40;

	int locals_idx = -1;
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		ASSERT_TRUE(editorDrawerVisibleEntryView(i, &view));
		if (strcmp(view.name, "Locals (1)") == 0) {
			locals_idx = i;
		}
	}
	ASSERT_TRUE(locals_idx >= 0);
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(locals_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerSelectedIsDirectory());

	ASSERT_TRUE(editorDrawerToggleSelectionExpanded(E.window_rows));
	ASSERT_TRUE((E.drawer_dap_scope_collapsed & (1ull << 1)) != 0); /* Locals collapsed */
	ASSERT_TRUE((E.drawer_dap_scope_collapsed & (1ull << 0)) == 0); /* Arguments untouched */

	ASSERT_TRUE(editorDrawerToggleSelectionExpanded(E.window_rows));
	ASSERT_TRUE((E.drawer_dap_scope_collapsed & (1ull << 1)) == 0); /* expanded again */

	E.dap_running = 0;
	E.dap_stopped = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
	E.drawer_dap_scope_collapsed = 0;
	return 0;
}

const struct editorTestCase g_dap_tests[] = {
        {"editor_dap_console_panel_switches_terminal_and_console_tabs",
         test_editor_dap_console_panel_switches_terminal_and_console_tabs},
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
        {"editor_dap_stopped_chain_populates_state", test_editor_dap_stopped_chain_populates_state},
        {"editor_dap_variable_child_preview_populates_arrays",
         test_editor_dap_variable_child_preview_populates_arrays},
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
        {"editor_dap_status_bar_controls_nerd_icons",
         test_editor_dap_status_bar_controls_nerd_icons},
        {"editor_dap_stopped_line_highlighted", test_editor_dap_stopped_line_highlighted},
        {"editor_dap_stop_centers_viewport_on_current_line",
         test_editor_dap_stop_centers_viewport_on_current_line},
        {"editor_dap_stop_in_missing_source_does_not_error",
         test_editor_dap_stop_in_missing_source_does_not_error},
        {"editor_dap_drawer_top_level_order_and_collapse_semantics",
         test_editor_dap_drawer_top_level_order_and_collapse_semantics},
        {"editor_dap_drawer_entry_view_semantics", test_editor_dap_drawer_entry_view_semantics},
        {"editor_dap_drawer_renders_semantic_decorations",
         test_editor_dap_drawer_renders_semantic_decorations},
        {"editor_dap_drawer_groups_variables_by_scope",
         test_editor_dap_drawer_groups_variables_by_scope},
        {"editor_dap_drawer_scope_toggle_collapses", test_editor_dap_drawer_scope_toggle_collapses},
        {"editor_dap_evaluate_request_builder", test_editor_dap_evaluate_request_builder},
        {"editor_dap_evaluate_repl_flow", test_editor_dap_evaluate_repl_flow},
        {"editor_dap_console_pane_renders_and_toggles",
         test_editor_dap_console_pane_renders_and_toggles},
        {"editor_dap_console_wheel_scrolls_transcript",
         test_editor_dap_console_wheel_scrolls_transcript},
        {"editor_dap_console_cursor_sits_on_input_line",
         test_editor_dap_console_cursor_sits_on_input_line},
        {"editor_dap_adapter_command_tty", test_editor_dap_adapter_command_tty},
        {"editor_dap_output_events_go_to_console", test_editor_dap_output_events_go_to_console},
};

const int g_dap_test_count = (int)(sizeof(g_dap_tests) / sizeof(g_dap_tests[0]));
