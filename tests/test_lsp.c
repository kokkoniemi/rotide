#include "test_case.h"
#include "test_support.h"

static int test_editor_lsp_config_defaults_and_precedence(void) {
	int gopls_enabled = 0;
	int clangd_enabled = 0;
	int html_enabled = 0;
	int css_enabled = 0;
	int json_enabled = 0;
	int javascript_enabled = 0;
	int eslint_enabled = 0;
	char gopls_command[PATH_MAX];
	char gopls_install_command[PATH_MAX];
	char clangd_command[PATH_MAX];
	char html_command[PATH_MAX];
	char css_command[PATH_MAX];
	char json_command[PATH_MAX];
	char javascript_command[PATH_MAX];
	char javascript_install_command[PATH_MAX];
	char eslint_command[PATH_MAX];
	char vscode_langservers_install_command[PATH_MAX];

	enum editorLspConfigLoadStatus defaults_status =
			editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
					&css_enabled, &json_enabled, &javascript_enabled, &eslint_enabled,
					gopls_command, sizeof(gopls_command), gopls_install_command,
					sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
					html_command, sizeof(html_command), css_command, sizeof(css_command),
					json_command, sizeof(json_command), javascript_command,
					sizeof(javascript_command), javascript_install_command,
					sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
					vscode_langservers_install_command,
					sizeof(vscode_langservers_install_command), NULL, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_OK, defaults_status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(1, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(1, json_enabled);
	ASSERT_EQ_INT(1, javascript_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls", gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", gopls_install_command);
	ASSERT_EQ_STR("clangd", clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", json_command);
	ASSERT_EQ_STR("~/.local/bin/typescript-language-server --stdio", javascript_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local typescript typescript-language-server",
			javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
			vscode_langservers_install_command);

	char dir_template[] = "/tmp/rotide-test-lsp-config-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(write_text_file(global_path,
				"[lsp]\n"
				"gopls_enabled = false\n"
				"clangd_enabled = false\n"
				"html_enabled = false\n"
				"css_enabled = false\n"
				"json_enabled = false\n"
				"javascript_enabled = false\n"
				"eslint_enabled = false\n"
				"gopls_command = \"gopls-global\"\n"
				"clangd_command = \"clangd-global\"\n"
				"html_command = \"html-global --stdio\"\n"
				"css_command = \"css-global --stdio\"\n"
				"json_command = \"json-global --stdio\"\n"
				"javascript_command = \"javascript-global --stdio\"\n"
				"javascript_install_command = \"javascript-global-install\"\n"
				"eslint_command = \"eslint-global --stdio\"\n"
				"gopls_install_command = \"global-install\"\n"
				"vscode_langservers_install_command = \"global-vscode-install\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[lsp]\n"
				"gopls_enabled = true\n"
				"clangd_enabled = false\n"
				"html_enabled = true\n"
				"css_enabled = true\n"
				"json_enabled = false\n"
				"javascript_enabled = true\n"
				"eslint_enabled = true\n"
				"gopls_command = \"gopls-project\"\n"
				"clangd_command = \"clangd-project\"\n"
				"html_command = \"html-project --stdio\"\n"
				"css_command = \"css-project --stdio\"\n"
				"json_command = \"json-project --stdio\"\n"
				"javascript_command = \"javascript-project --stdio\"\n"
				"javascript_install_command = \"javascript-project-install\"\n"
				"eslint_command = \"eslint-project --stdio\"\n"
				"gopls_install_command = \"project-install\"\n"
				"vscode_langservers_install_command = \"project-vscode-install\"\n"));

	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
					&css_enabled, &json_enabled, &javascript_enabled, &eslint_enabled,
					gopls_command, sizeof(gopls_command), gopls_install_command,
					sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
					html_command, sizeof(html_command), css_command, sizeof(css_command),
					json_command, sizeof(json_command), javascript_command,
					sizeof(javascript_command), javascript_install_command,
					sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
					vscode_langservers_install_command,
					sizeof(vscode_langservers_install_command), global_path,
					project_path);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_OK, status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(0, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(0, json_enabled);
	ASSERT_EQ_INT(1, javascript_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls-project", gopls_command);
	ASSERT_EQ_STR("global-install", gopls_install_command);
	ASSERT_EQ_STR("clangd-project", clangd_command);
	ASSERT_EQ_STR("html-project --stdio", html_command);
	ASSERT_EQ_STR("css-project --stdio", css_command);
	ASSERT_EQ_STR("json-project --stdio", json_command);
	ASSERT_EQ_STR("javascript-project --stdio", javascript_command);
	ASSERT_EQ_STR("javascript-global-install", javascript_install_command);
	ASSERT_EQ_STR("eslint-project --stdio", eslint_command);
	ASSERT_EQ_STR("global-vscode-install", vscode_langservers_install_command);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_config_invalid_values_fallback_defaults(void) {
	int gopls_enabled = 0;
	int clangd_enabled = 0;
	int html_enabled = 0;
	int css_enabled = 0;
	int json_enabled = 0;
	int javascript_enabled = 0;
	int eslint_enabled = 0;
	char gopls_command[PATH_MAX];
	char gopls_install_command[PATH_MAX];
	char clangd_command[PATH_MAX];
	char html_command[PATH_MAX];
	char css_command[PATH_MAX];
	char json_command[PATH_MAX];
	char javascript_command[PATH_MAX];
	char javascript_install_command[PATH_MAX];
	char eslint_command[PATH_MAX];
	char vscode_langservers_install_command[PATH_MAX];

	char dir_template[] = "/tmp/rotide-test-lsp-config-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[lsp]\n"
				"enabled = \"yes\"\n"));
	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
					&css_enabled, &json_enabled, &javascript_enabled, &eslint_enabled,
					gopls_command, sizeof(gopls_command), gopls_install_command,
					sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
					html_command, sizeof(html_command), css_command, sizeof(css_command),
					json_command, sizeof(json_command), javascript_command,
					sizeof(javascript_command), javascript_install_command,
					sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
					vscode_langservers_install_command,
					sizeof(vscode_langservers_install_command), global_path, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(1, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(1, json_enabled);
	ASSERT_EQ_INT(1, javascript_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls", gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", gopls_install_command);
	ASSERT_EQ_STR("clangd", clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", json_command);
	ASSERT_EQ_STR("~/.local/bin/typescript-language-server --stdio", javascript_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local typescript typescript-language-server",
			javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
			vscode_langservers_install_command);

	ASSERT_TRUE(write_text_file(global_path,
				"[lsp]\n"
				"clangd_enabled = false\n"
				"html_enabled = false\n"
				"css_enabled = false\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[lsp]\n"
				"html_command = \"\"\n"));
	status = editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
			&css_enabled, &json_enabled, &javascript_enabled, &eslint_enabled,
			gopls_command, sizeof(gopls_command), gopls_install_command,
			sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
			html_command, sizeof(html_command), css_command, sizeof(css_command),
			json_command, sizeof(json_command), javascript_command,
			sizeof(javascript_command), javascript_install_command,
			sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
			vscode_langservers_install_command,
			sizeof(vscode_langservers_install_command), global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(1, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(1, json_enabled);
	ASSERT_EQ_INT(1, javascript_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls", gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", gopls_install_command);
	ASSERT_EQ_STR("clangd", clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", json_command);
	ASSERT_EQ_STR("~/.local/bin/typescript-language-server --stdio", javascript_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local typescript typescript-language-server",
			javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
			vscode_langservers_install_command);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_parse_definition_response_handles_clangd_field_order(void) {
	const char *location_response =
			"{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[{\"range\":{\"start\":{\"character\":5,"
			"\"line\":14},\"end\":{\"character\":23,\"line\":14}},\"uri\":"
			"\"file:///home/mk/Development/rotide/src/editing/edit.h\"}]}";
	struct editorLspLocation *locations = NULL;
	int count = 0;
	ASSERT_TRUE(editorLspTestParseDefinitionResponse(location_response, &locations, &count));
	ASSERT_EQ_INT(1, count);
	ASSERT_TRUE(locations != NULL);
	ASSERT_EQ_STR("/home/mk/Development/rotide/src/editing/edit.h", locations[0].path);
	ASSERT_EQ_INT(14, locations[0].line);
	ASSERT_EQ_INT(5, locations[0].character);
	editorLspFreeLocations(locations, count);

	const char *location_link_response =
			"{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":[{\"targetSelectionRange\":{\"start\":"
			"{\"character\":5,\"line\":6},\"end\":{\"character\":26,\"line\":6}},"
			"\"targetUri\":\"file:///home/mk/Development/rotide/src/input/dispatch.h\","
			"\"targetRange\":{\"start\":{\"character\":0,\"line\":6},\"end\":{\"character\":26,"
			"\"line\":6}}}]}";
	locations = NULL;
	count = 0;
	ASSERT_TRUE(editorLspTestParseDefinitionResponse(location_link_response, &locations, &count));
	ASSERT_EQ_INT(1, count);
	ASSERT_TRUE(locations != NULL);
	ASSERT_EQ_STR("/home/mk/Development/rotide/src/input/dispatch.h", locations[0].path);
	ASSERT_EQ_INT(6, locations[0].line);
	ASSERT_EQ_INT(5, locations[0].character);
	editorLspFreeLocations(locations, count);
	return 0;
}

static int test_editor_lsp_build_initialize_request_json_is_complete(void) {
	char *request = editorLspTestBuildInitializeRequestJson(7, "file:///tmp/project", 1234);
	ASSERT_TRUE(request != NULL);
	ASSERT_TRUE(strstr(request, "\"id\":7") != NULL);
	ASSERT_TRUE(strstr(request, "\"processId\":1234") != NULL);
	ASSERT_TRUE(strstr(request, "\"rootUri\":\"file:///tmp/project\"") != NULL);
	ASSERT_TRUE(strstr(request, "\"source.fixAll.eslint\"") != NULL);
	ASSERT_EQ_INT('}', request[strlen(request) - 1]);
	free(request);
	return 0;
}

static int test_editor_lsp_lifecycle_lazy_start_and_non_go_buffers(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char txt_path[64];
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(txt_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	char goto_def_txt[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def_txt,
			sizeof(goto_def_txt)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.start_count);
	ASSERT_EQ_INT(0, stats.definition_count);

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {\n\tmain()\n}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def_go[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def_go,
			sizeof(goto_def_go)) == 0);

	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.start_count);
	ASSERT_EQ_INT(1, stats.definition_count);
	ASSERT_EQ_INT(1, stats.did_open_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restart_after_mock_crash(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {}\n"));
	editorOpen(go_path);

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.start_count);
	ASSERT_EQ_INT(1, stats.definition_count);

	editorLspTestSetMockServerAlive(0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_html_enabled = 1;

	char go_path[64];
	char c_path[64];
	char html_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));

	editorOpen(go_path);
	E.cy = 3;
	E.cx = 15;
	struct editorLspLocation go_target = {
		.path = go_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &go_target, 1);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(c_path);
	E.cy = 1;
	E.cx = 24;
	struct editorLspLocation c_target = {
		.path = c_path,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &c_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;
	struct editorLspLocation html_target = {
		.path = html_path,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &html_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(go_path);
	E.cy = 3;
	E.cx = 15;
	editorLspTestSetMockDefinitionResponse(1, &go_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(4, stats.start_count);
	ASSERT_EQ_INT(4, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	ASSERT_TRUE(unlink(c_path) == 0);
	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char root_template[] = "/tmp/rotide-test-clangd-root-XXXXXX";
	char *root_path = mkdtemp(root_template);
	ASSERT_TRUE(root_path != NULL);

	char project_a[512];
	char project_b[512];
	char marker_a[512];
	char marker_b[512];
	char file_a[512];
	char file_b[512];
	ASSERT_TRUE(path_join(project_a, sizeof(project_a), root_path, "project-a"));
	ASSERT_TRUE(path_join(project_b, sizeof(project_b), root_path, "project-b"));
	ASSERT_TRUE(path_join(marker_a, sizeof(marker_a), project_a, "compile_commands.json"));
	ASSERT_TRUE(path_join(marker_b, sizeof(marker_b), project_b, "compile_commands.json"));
	ASSERT_TRUE(path_join(file_a, sizeof(file_a), project_a, "main.c"));
	ASSERT_TRUE(path_join(file_b, sizeof(file_b), project_b, "main.c"));
	ASSERT_TRUE(make_dir(project_a));
	ASSERT_TRUE(make_dir(project_b));
	ASSERT_TRUE(write_text_file(marker_a, "[]\n"));
	ASSERT_TRUE(write_text_file(marker_b, "[]\n"));
	ASSERT_TRUE(write_text_file(file_a,
				"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	ASSERT_TRUE(write_text_file(file_b,
				"int helper(void) { return 2; }\nint main(void) { return helper(); }\n"));

	struct editorLspLocation target = {
		.path = file_a,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_a);
	E.cy = 1;
	E.cx = 24;
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	target.path = file_b;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_b);
	E.cy = 1;
	E.cx = 24;
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.definition_count);

	ASSERT_TRUE(unlink(file_a) == 0);
	ASSERT_TRUE(unlink(file_b) == 0);
	ASSERT_TRUE(unlink(marker_a) == 0);
	ASSERT_TRUE(unlink(marker_b) == 0);
	ASSERT_TRUE(rmdir(project_a) == 0);
	ASSERT_TRUE(rmdir(project_b) == 0);
	ASSERT_TRUE(rmdir(root_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restarts_when_html_workspace_root_changes(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char root_template[] = "/tmp/rotide-test-html-root-XXXXXX";
	char *root_path = mkdtemp(root_template);
	ASSERT_TRUE(root_path != NULL);

	char project_a[512];
	char project_b[512];
	char marker_a[512];
	char marker_b[512];
	char file_a[512];
	char file_b[512];
	ASSERT_TRUE(path_join(project_a, sizeof(project_a), root_path, "project-a"));
	ASSERT_TRUE(path_join(project_b, sizeof(project_b), root_path, "project-b"));
	ASSERT_TRUE(path_join(marker_a, sizeof(marker_a), project_a, "package.json"));
	ASSERT_TRUE(path_join(marker_b, sizeof(marker_b), project_b, "package.json"));
	ASSERT_TRUE(path_join(file_a, sizeof(file_a), project_a, "index.html"));
	ASSERT_TRUE(path_join(file_b, sizeof(file_b), project_b, "index.html"));
	ASSERT_TRUE(make_dir(project_a));
	ASSERT_TRUE(make_dir(project_b));
	ASSERT_TRUE(write_text_file(marker_a, "{ }\n"));
	ASSERT_TRUE(write_text_file(marker_b, "{ }\n"));
	ASSERT_TRUE(write_text_file(file_a, "<div id=\"a\"></div>\n<a href=\"#a\">jump</a>\n"));
	ASSERT_TRUE(write_text_file(file_b, "<div id=\"b\"></div>\n<a href=\"#b\">jump</a>\n"));

	struct editorLspLocation target = {
		.path = file_a,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_a);
	E.cy = 1;
	E.cx = 11;
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	target.path = file_b;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_b);
	E.cy = 1;
	E.cx = 11;
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.definition_count);

	ASSERT_TRUE(unlink(file_a) == 0);
	ASSERT_TRUE(unlink(file_b) == 0);
	ASSERT_TRUE(unlink(marker_a) == 0);
	ASSERT_TRUE(unlink(marker_b) == 0);
	ASSERT_TRUE(rmdir(project_a) == 0);
	ASSERT_TRUE(rmdir(project_b) == 0);
	ASSERT_TRUE(rmdir(root_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_go_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {\n\tprintln(\"ok\")\n}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('/');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(1, change.had_range);
	ASSERT_EQ_INT(0, change.start_line);
	ASSERT_EQ_INT(0, change.start_character);
	ASSERT_EQ_INT(2, change.version);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_c_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int main(void) {\n\treturn 0;\n}\n"));
	editorOpen(c_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('/');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(1, change.had_range);
	ASSERT_EQ_INT(0, change.start_line);
	ASSERT_EQ_INT(0, change.start_character);
	ASSERT_EQ_INT(2, change.version);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_html_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_lsp_html_language_id_routing_for_supported_extensions(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char html_path[64];
	char htm_path[64];
	char xhtml_path[64];
	ASSERT_TRUE(write_temp_file_with_suffix(html_path, sizeof(html_path),
			"rotide-test-html-route-", ".html", "<div id=\"a\"></div>\n<a href=\"#a\">jump</a>\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(htm_path, sizeof(htm_path),
			"rotide-test-html-route-", ".htm", "<div id=\"b\"></div>\n<a href=\"#b\">jump</a>\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(xhtml_path, sizeof(xhtml_path),
			"rotide-test-html-route-", ".xhtml",
			"<div id=\"c\"></div>\n<a href=\"#c\">jump</a>\n"));

	struct editorLspLocation target = {
		.path = html_path,
		.line = 0,
		.character = 9
	};
	char language_id[32];
	char goto_def[] = {CTRL_KEY('o')};

	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	target.path = htm_path;
	editorOpen(htm_path);
	E.cy = 1;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	target.path = xhtml_path;
	editorOpen(xhtml_path);
	E.cy = 1;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(htm_path) == 0);
	ASSERT_TRUE(unlink(xhtml_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_css_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char css_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(css_path, sizeof(css_path),
			"rotide-test-css-lsp-fixture-", ".css",
			"tests/lsp/supported/css/single_file_definition.css"));
	editorOpen(css_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("css", language_id);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(css_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_javascript_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;
	E.lsp_eslint_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-javascript-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/single_file_definition.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("javascript", language_id);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_language_id_routing_for_css_scss_and_json(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;
	E.lsp_json_enabled = 1;

	char css_path[64];
	char scss_path[64];
	char json_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(css_path, sizeof(css_path),
			"rotide-test-css-route-", ".css",
			"tests/lsp/supported/css/single_file_definition.css"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(scss_path, sizeof(scss_path),
			"rotide-test-scss-route-", ".scss",
			"tests/lsp/supported/css/single_file_definition.scss"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(json_path, sizeof(json_path),
			"rotide-test-json-route-", ".json",
			"tests/lsp/supported/json/single_file_definition.json"));

	struct editorLspLocation target = {
		.path = css_path,
		.line = 0,
		.character = 8
	};
	char language_id[32];
	char goto_def[] = {CTRL_KEY('o')};

	editorOpen(css_path);
	E.cy = 1;
	E.cx = 18;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("css", language_id);

	target.path = scss_path;
	editorOpen(scss_path);
	E.cy = 1;
	E.cx = 18;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("scss", language_id);

	target.path = json_path;
	target.line = 1;
	target.character = 3;
	editorOpen(json_path);
	E.cy = 2;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("json", language_id);

	ASSERT_TRUE(unlink(css_path) == 0);
	ASSERT_TRUE(unlink(scss_path) == 0);
	ASSERT_TRUE(unlink(json_path) == 0);
	return 0;
}

static int test_editor_lsp_language_id_routing_for_javascript_extensions(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;

	char js_path[64];
	char mjs_path[64];
	char cjs_path[64];
	char jsx_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-javascript-route-", ".js",
			"tests/lsp/supported/javascript/single_file_definition.js"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(mjs_path, sizeof(mjs_path),
			"rotide-test-javascript-route-", ".mjs",
			"tests/lsp/supported/javascript/single_file_definition.js"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(cjs_path, sizeof(cjs_path),
			"rotide-test-javascript-route-", ".cjs",
			"tests/lsp/supported/javascript/single_file_definition.js"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(jsx_path, sizeof(jsx_path),
			"rotide-test-javascript-route-", ".jsx",
			"tests/lsp/supported/javascript/single_file_definition.jsx"));

	struct editorLspLocation target = {
		.path = js_path,
		.line = 0,
		.character = 6
	};
	char language_id[32];
	char goto_def[] = {CTRL_KEY('o')};

	editorOpen(js_path);
	E.cy = 2;
	E.cx = 2;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("javascript", language_id);

	target.path = mjs_path;
	editorOpen(mjs_path);
	E.cy = 2;
	E.cx = 2;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("javascript", language_id);

	target.path = cjs_path;
	editorOpen(cjs_path);
	E.cy = 2;
	E.cx = 2;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("javascript", language_id);

	target.path = jsx_path;
	target.line = 0;
	target.character = 9;
	editorOpen(jsx_path);
	E.cy = 5;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("javascriptreact", language_id);

	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(mjs_path) == 0);
	ASSERT_TRUE(unlink(cjs_path) == 0);
	ASSERT_TRUE(unlink(jsx_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;

	char css_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(css_path, sizeof(css_path),
			"rotide-test-css-lsp-fixture-", ".css",
			"tests/lsp/supported/css/single_file_definition.css"));
	editorOpen(css_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 26;

	struct editorLspLocation target = {
		.path = css_path,
		.line = 0,
		.character = 8
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(8, E.cx);

	ASSERT_TRUE(unlink(css_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_json_enabled = 1;

	char json_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(json_path, sizeof(json_path),
			"rotide-test-json-lsp-fixture-", ".json",
			"tests/lsp/supported/json/single_file_definition.json"));
	editorOpen(json_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 11;

	struct editorLspLocation target = {
		.path = json_path,
		.line = 1,
		.character = 3
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	ASSERT_TRUE(unlink(json_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-javascript-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/single_file_definition.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 2;

	struct editorLspLocation target = {
		.path = js_path,
		.line = 0,
		.character = 6
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(6, E.cx);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_jsx_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;

	char jsx_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(jsx_path, sizeof(jsx_path),
			"rotide-test-javascript-lsp-fixture-", ".jsx",
			"tests/lsp/supported/javascript/single_file_definition.jsx"));
	editorOpen(jsx_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 5;
	E.cx = 11;

	struct editorLspLocation target = {
		.path = jsx_path,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);

	ASSERT_TRUE(unlink(jsx_path) == 0);
	return 0;
}

static int test_editor_lsp_eslint_diagnostics_update_and_status_summary(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspDiagnostic diagnostics[2] = {
		{.start_line = 0, .start_character = 0, .end_line = 0, .end_character = 5,
				.severity = 1, .message = "Unexpected space"},
		{.start_line = 1, .start_character = 0, .end_line = 1, .end_character = 11,
				.severity = 2, .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 2);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(2, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_error_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);
	ASSERT_TRUE(strstr(E.statusmsg, "ESLint: 1 error, 1 warning") != NULL);
	ASSERT_TRUE(strstr(output, "[E:1 W:1]") != NULL);
	free(output);

	editorLspTestSetMockDiagnostics(js_path, NULL, 0);
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, E.lsp_diagnostic_count);
	ASSERT_TRUE(strstr(E.statusmsg, "diagnostics cleared") != NULL);
	free(output);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_eslint_diagnostics_persist_across_tab_switches(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	char txt_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(js_path);
	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspDiagnostic diagnostics[1] = {
		{.start_line = 1, .start_character = 0, .end_line = 1, .end_character = 11,
				.severity = 2, .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 1);
	editorLspPumpNotifications();
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);

	ASSERT_TRUE(editorTabOpenFileAsNew(txt_path));
	ASSERT_EQ_STR(txt_path, E.filename);
	ASSERT_EQ_INT(0, E.lsp_diagnostic_count);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(js_path, E.filename);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);

	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_lsp_javascript_definition_coexists_with_eslint_sidecar(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	E.keymap.bindings[E.keymap.len].key = CTRL_KEY('t');
	E.keymap.bindings[E.keymap.len].action = EDITOR_ACTION_ESLINT_FIX;
	E.keymap.len++;

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	size_t full_text_len = 0;
	char *full_text = editorDupActiveTextSource(&full_text_len);
	ASSERT_TRUE(full_text != NULL || full_text_len == 0);
	ASSERT_TRUE(editorLspEnsureDocumentOpen(E.filename, E.syntax_language, &E.lsp_doc_open,
				&E.lsp_doc_version, full_text != NULL ? full_text : "", full_text_len));
	ASSERT_TRUE(editorLspEnsureEslintDocumentOpen(E.filename, E.syntax_language,
				&E.lsp_eslint_doc_open, &E.lsp_eslint_doc_version,
				full_text != NULL ? full_text : "", full_text_len));
	free(full_text);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.did_open_count);
	ASSERT_EQ_INT(1, E.lsp_doc_open);
	ASSERT_EQ_INT(1, E.lsp_eslint_doc_open);

	struct editorLspDiagnostic diagnostics[1] = {
		{.start_line = 1, .start_character = 12, .end_line = 1, .end_character = 15,
				.severity = 2, .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 1);
	editorLspPumpNotifications();
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);

	E.cy = 1;
	E.cx = 13;
	struct editorLspLocation target = {
		.path = js_path,
		.line = 0,
		.character = 6
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(6, E.cx);

	struct editorLspDiagnostic edits[1] = {
		{.start_line = 1, .start_character = 16, .end_line = 1, .end_character = 16,
				.message = ";"},
	};
	editorLspTestSetMockCodeActionResult(1, edits, 1);
	char fix_input[] = {CTRL_KEY('t')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(fix_input, sizeof(fix_input)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "ESLint fixes applied") != NULL);

	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);
	ASSERT_EQ_INT(1, stats.code_action_count);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "console.log(foo);") != NULL);
	free(text);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_process_keypress_eslint_fix_action_applies_mock_edits(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	E.keymap.bindings[E.keymap.len].key = CTRL_KEY('t');
	E.keymap.bindings[E.keymap.len].action = EDITOR_ACTION_ESLINT_FIX;
	E.keymap.len++;

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	struct editorLspDiagnostic edits[1] = {
		{.start_line = 1, .start_character = 16, .end_line = 1, .end_character = 16,
				.message = ";"},
	};
	editorLspTestSetMockCodeActionResult(1, edits, 1);

	char input[] = {CTRL_KEY('t')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "ESLint fixes applied") != NULL);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "console.log(foo);") != NULL);
	free(text);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	E.keymap.bindings[E.keymap.len].key = CTRL_KEY('t');
	E.keymap.bindings[E.keymap.len].action = EDITOR_ACTION_ESLINT_FIX;
	E.keymap.len++;

	strncpy(E.lsp_eslint_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_install_command",
			sizeof(E.lsp_eslint_command) - 1);
	E.lsp_eslint_command[sizeof(E.lsp_eslint_command) - 1] = '\0';
	strncpy(E.lsp_vscode_langservers_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_vscode_langservers_install_command) - 1);
	E.lsp_vscode_langservers_install_command[
			sizeof(E.lsp_vscode_langservers_install_command) - 1] = '\0';

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	char input[] = {CTRL_KEY('t'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Install vscode-langservers-extracted", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("vscode-langservers-extracted installed. Retry Ctrl-O", E.statusmsg);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_full_document_change_uses_active_source(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	ASSERT_TRUE(editorLspNotifyDidChange(E.filename, E.syntax_language,
				&E.lsp_doc_open, &E.lsp_doc_version, NULL, NULL, 0, NULL, 0));

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(0, change.had_range);
	ASSERT_TRUE(strncmp(change.text, "package main", strlen("package main")) == 0);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_ignores_non_go_buffers(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char txt_path[64];
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(txt_path);

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('x');
	editorSave();
	ASSERT_TRUE(editorTabCloseActive());

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.did_open_count);
	ASSERT_EQ_INT(0, stats.did_change_count);
	ASSERT_EQ_INT(0, stats.did_save_count);
	ASSERT_EQ_INT(0, stats.did_close_count);

	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(go_path, sizeof(go_path),
			"rotide-test-go-lsp-fixture-", ".go",
			"tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	E.cy = 5;
	E.cx = 5;

	struct editorLspLocation target = {
		.path = go_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char c_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(c_path, sizeof(c_path),
			"rotide-test-c-lsp-fixture-", ".c",
			"tests/lsp/supported/c/single_file_definition.c"));
	editorOpen(c_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {
		.path = c_path,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char cpp_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(cpp_path, sizeof(cpp_path),
			"rotide-test-cpp-lsp-fixture-", ".cpp",
			"tests/lsp/supported/cpp/single_file_definition.cpp"));
	editorOpen(cpp_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {
		.path = cpp_path,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(cpp_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char html_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(html_path, sizeof(html_path),
			"rotide-test-html-lsp-fixture-", ".html",
			"tests/lsp/supported/html/single_file_definition.html"));
	editorOpen(html_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 11;

	struct editorLspLocation target = {
		.path = html_path,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_cross_file_reuses_tab(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char src_path[64];
	char dst_path[64];
	ASSERT_TRUE(write_temp_go_file(src_path, sizeof(src_path),
			"package main\n\nfunc main() { helper() }\n"));
	ASSERT_TRUE(write_temp_go_file(dst_path, sizeof(dst_path),
			"package main\n\nfunc helper() {}\n"));

	editorOpen(src_path);
	E.cy = 2;
	E.cx = 16;

	struct editorLspLocation target = {
		.path = dst_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(dst_path, E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(src_path, E.filename);
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(dst_path, E.filename);

	ASSERT_TRUE(unlink(src_path) == 0);
	ASSERT_TRUE(unlink(dst_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_cross_file_javascript_fixture_reuses_tab(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char dir_template[] = "/tmp/rotide-test-javascript-lsp-cross-file-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char main_path[512];
	char helper_path[512];
	ASSERT_TRUE(path_join(main_path, sizeof(main_path), dir_path, "main.js"));
	ASSERT_TRUE(path_join(helper_path, sizeof(helper_path), dir_path, "helper.js"));
	ASSERT_TRUE(copy_fixture_to_path(main_path, "tests/lsp/supported/javascript/cross_file/main.js"));
	ASSERT_TRUE(copy_fixture_to_path(helper_path,
			"tests/lsp/supported/javascript/cross_file/helper.js"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());
	E.cy = 2;
	E.cx = 2;

	struct editorLspLocation target = {
		.path = helper_path,
		.line = 0,
		.character = 16
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(helper_path, E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(main_path, E.filename);
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(helper_path, E.filename);

	ASSERT_TRUE(unlink(main_path) == 0);
	ASSERT_TRUE(unlink(helper_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char dir_template[] = "/tmp/rotide-test-cpp-lsp-cross-file-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char main_path[512];
	char helper_path[512];
	char header_path[512];
	ASSERT_TRUE(path_join(main_path, sizeof(main_path), dir_path, "main.cpp"));
	ASSERT_TRUE(path_join(helper_path, sizeof(helper_path), dir_path, "helper.cpp"));
	ASSERT_TRUE(path_join(header_path, sizeof(header_path), dir_path, "helper.hpp"));
	ASSERT_TRUE(copy_fixture_to_path(main_path, "tests/lsp/supported/cpp/cross_file/main.cpp"));
	ASSERT_TRUE(copy_fixture_to_path(helper_path, "tests/lsp/supported/cpp/cross_file/helper.cpp"));
	ASSERT_TRUE(copy_fixture_to_path(header_path, "tests/lsp/supported/cpp/cross_file/helper.hpp"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {
		.path = helper_path,
		.line = 2,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(helper_path, E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(main_path, E.filename);
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(helper_path, E.filename);

	ASSERT_TRUE(unlink(main_path) == 0);
	ASSERT_TRUE(unlink(helper_path) == 0);
	ASSERT_TRUE(unlink(header_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_multi_picker_selects_choice(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc a() {}\nfunc b() {}\nfunc main() { a() }\n"));
	editorOpen(go_path);
	E.cy = 4;
	E.cx = 15;

	struct editorLspLocation targets[2] = {
		{.path = go_path, .line = 2, .character = 5},
		{.path = go_path, .line = 3, .character = 5},
	};
	editorLspTestSetMockDefinitionResponse(1, targets, 2);

	char input[] = {CTRL_KEY('o'), '2', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_INT(3, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_single_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.window_rows = 6;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorLspLocation target = {
		.path = go_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 16, 5, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(click, strlen(click)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc a() {}\nfunc b() {}\nfunc main() { a() }\n"));
	editorOpen(go_path);
	E.window_rows = 7;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;

	struct editorLspLocation targets[2] = {
		{.path = go_path, .line = 2, .character = 5},
		{.path = go_path, .line = 3, .character = 5},
	};
	editorLspTestSetMockDefinitionResponse(1, targets, 2);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	char input[40];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 16, 6, 'M'));
	int written = snprintf(input, sizeof(input), "%s2\r", click);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(input));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, (size_t)written) == 0);
	ASSERT_EQ_INT(3, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_timeout_error_and_no_result(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {CTRL_KEY('o')};

	editorLspTestSetMockDefinitionResponse(-2, NULL, 0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "timed out") != NULL);

	editorLspTestSetMockDefinitionResponse(-1, NULL, 0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "failed") != NULL);

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "not found") != NULL);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("gopls is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_c(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 0;
	E.cx = 24;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("clangd is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_html(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_html_enabled = 0;

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("vscode-html-language-server is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	add_row("plain text");
	E.window_rows = 4;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(click, strlen(click)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "Go to definition is available for Go, C, C++, HTML") != NULL);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.definition_count);
	return 0;
}

static int test_editor_process_keypress_goto_definition_requires_saved_c_buffer(void) {
	add_row("int main(void) { return helper(); }");
	E.window_rows = 4;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_C;
	free(E.filename);
	E.filename = NULL;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("Save this C/C++ buffer before using go to definition", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_empty_clangd_command(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_clangd_command[0] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 0;
	E.cx = 24;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("LSP disabled: [lsp].clangd_command is empty", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_empty_html_command(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_html_enabled = 1;
	E.lsp_html_command[0] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("LSP disabled: [lsp].html_command is empty", E.statusmsg);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_startup_failure_reports_reason(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	strncpy(E.lsp_gopls_command, "true", sizeof(E.lsp_gopls_command) - 1);
	E.lsp_gopls_command[sizeof(E.lsp_gopls_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "LSP startup failed") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "unavailable for this file") == NULL);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer(void) {
	add_row("package main");
	add_row("");
	add_row("func main() { helper() }");
	E.window_rows = 5;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_GO;
	free(E.filename);
	E.filename = NULL;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 16, 4, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(click, strlen(click)) == 0);
	ASSERT_EQ_STR("Save this Go buffer before using go to definition", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_gopls_decline_install(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_gopls_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_gopls_decline_command",
			sizeof(E.lsp_gopls_command) - 1);
	E.lsp_gopls_command[sizeof(E.lsp_gopls_command) - 1] = '\0';
	strncpy(E.lsp_gopls_install_command, "printf 'install skipped\\n'",
			sizeof(E.lsp_gopls_install_command) - 1);
	E.lsp_gopls_install_command[sizeof(E.lsp_gopls_install_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char input[] = {CTRL_KEY('o'), '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_STR("gopls not installed", E.statusmsg);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_gopls_starts_install_task(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_gopls_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_gopls_install_command",
			sizeof(E.lsp_gopls_command) - 1);
	E.lsp_gopls_command[sizeof(E.lsp_gopls_command) - 1] = '\0';
	strncpy(E.lsp_gopls_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_gopls_install_command) - 1);
	E.lsp_gopls_install_command[sizeof(E.lsp_gopls_install_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install gopls", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("gopls installed. Retry Ctrl-O", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "install ok") != NULL);
	free(text);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_clangd_declines_instructions(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_clangd_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
			sizeof(E.lsp_clangd_command) - 1);
	E.lsp_clangd_command[sizeof(E.lsp_clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 1;
	E.cx = 27;

	char input[] = {CTRL_KEY('o'), '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_STR("clangd not installed", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_clangd_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
			sizeof(E.lsp_clangd_command) - 1);
	E.lsp_clangd_command[sizeof(E.lsp_clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 1;
	E.cx = 27;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install clangd", editorActiveBufferDisplayName());
	ASSERT_EQ_STR("clangd not installed; see task log", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "clangd was not found on PATH") != NULL);
	ASSERT_TRUE(strstr(text, "https://clangd.llvm.org/installation") != NULL);
	ASSERT_TRUE(strstr(text, "compile_commands.json") != NULL);
	ASSERT_TRUE(strstr(text, "cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON") != NULL);
	ASSERT_TRUE(strstr(text, "bear -- make") != NULL);
	ASSERT_TRUE(strstr(text, "[lsp].clangd_command") != NULL);
	free(text);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_html_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_command",
			sizeof(E.lsp_html_command) - 1);
	E.lsp_html_command[sizeof(E.lsp_html_command) - 1] = '\0';
	strncpy(E.lsp_vscode_langservers_install_command, "printf 'install skipped\\n'",
			sizeof(E.lsp_vscode_langservers_install_command) - 1);
	E.lsp_vscode_langservers_install_command[
			sizeof(E.lsp_vscode_langservers_install_command) - 1] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char input[] = {CTRL_KEY('o'), '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_STR("vscode-langservers-extracted not installed", E.statusmsg);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_html_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_install_command",
			sizeof(E.lsp_html_command) - 1);
	E.lsp_html_command[sizeof(E.lsp_html_command) - 1] = '\0';
	strncpy(E.lsp_vscode_langservers_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_vscode_langservers_install_command) - 1);
	E.lsp_vscode_langservers_install_command[
			sizeof(E.lsp_vscode_langservers_install_command) - 1] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install vscode-langservers-extracted", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("vscode-langservers-extracted installed. Retry Ctrl-O", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "install ok") != NULL);
	free(text);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_javascript_server_starts_install_task(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_javascript_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_typescript_language_server_install_command",
			sizeof(E.lsp_javascript_command) - 1);
	E.lsp_javascript_command[sizeof(E.lsp_javascript_command) - 1] = '\0';
	strncpy(E.lsp_javascript_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_javascript_install_command) - 1);
	E.lsp_javascript_install_command[sizeof(E.lsp_javascript_install_command) - 1] = '\0';

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-javascript-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/single_file_definition.js"));
	editorOpen(js_path);
	E.cy = 2;
	E.cx = 2;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install typescript-language-server", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("typescript-language-server installed. Retry Ctrl-O", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "install ok") != NULL);
	free(text);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_task_log_read_only_search_and_copy(void) {
	ASSERT_TRUE(editorTabsInit());
	editorDocumentTestResetStats();
	ASSERT_TRUE(editorTaskStart("Task: Echo", "printf 'alpha\\nbeta\\n'", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Echo", editorActiveBufferDisplayName());
	ASSERT_TRUE(E.document != NULL);
	ASSERT_TRUE(editorDocumentTestFullRebuildCount() > 0);

	editorDocumentTestResetStats();
	editorActiveTextSourceDupTestResetCount();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());

	size_t before_len = 0;
	char *before = editorRowsToStr(&before_len);
	ASSERT_TRUE(before != NULL);

	char insert[] = {'x'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(insert, sizeof(insert)) == 0);
	ASSERT_EQ_STR("Task log is read-only", E.statusmsg);

	size_t after_len = 0;
	char *after = editorRowsToStr(&after_len);
	ASSERT_TRUE(after != NULL);
	ASSERT_EQ_INT((int)before_len, (int)after_len);
	ASSERT_MEM_EQ(before, after, before_len);
	free(before);
	free(after);

	char save[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(save, sizeof(save)) == 0);
	ASSERT_EQ_STR("Task logs cannot be saved", E.statusmsg);

	char find_input[] = {CTRL_KEY('f'), 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', 'd', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(find_input, sizeof(find_input)) == 0);
	ASSERT_TRUE(E.search_match_len > 0);

	editorActiveTextSourceDupTestResetCount();
	E.cy = 3;
	E.cx = 4;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(3, 0));
	char copy[] = {CTRL_KEY('c')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(copy, sizeof(copy)) == 0);

	size_t copied_len = 0;
	const char *copied = editorClipboardGet(&copied_len);
	ASSERT_EQ_INT(4, (int)copied_len);
	ASSERT_MEM_EQ("beta", copied, copied_len);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	return 0;
}

const struct editorTestCase g_lsp_tests[] = {
	{"editor_lsp_config_defaults_and_precedence", test_editor_lsp_config_defaults_and_precedence},
	{"editor_lsp_config_invalid_values_fallback_defaults", test_editor_lsp_config_invalid_values_fallback_defaults},
	{"editor_lsp_parse_definition_response_handles_clangd_field_order", test_editor_lsp_parse_definition_response_handles_clangd_field_order},
	{"editor_lsp_build_initialize_request_json_is_complete", test_editor_lsp_build_initialize_request_json_is_complete},
	{"editor_lsp_lifecycle_lazy_start_and_non_go_buffers", test_editor_lsp_lifecycle_lazy_start_and_non_go_buffers},
	{"editor_lsp_lifecycle_restart_after_mock_crash", test_editor_lsp_lifecycle_restart_after_mock_crash},
	{"editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html", test_editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html},
	{"editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes", test_editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes},
	{"editor_lsp_lifecycle_restarts_when_html_workspace_root_changes", test_editor_lsp_lifecycle_restarts_when_html_workspace_root_changes},
	{"editor_lsp_document_sync_for_go_edit_save_close", test_editor_lsp_document_sync_for_go_edit_save_close},
	{"editor_lsp_document_sync_for_c_edit_save_close", test_editor_lsp_document_sync_for_c_edit_save_close},
	{"editor_lsp_document_sync_for_html_edit_save_close", test_editor_lsp_document_sync_for_html_edit_save_close},
	{"editor_lsp_document_sync_for_css_edit_save_close", test_editor_lsp_document_sync_for_css_edit_save_close},
	{"editor_lsp_document_sync_for_javascript_edit_save_close", test_editor_lsp_document_sync_for_javascript_edit_save_close},
	{"editor_lsp_full_document_change_uses_active_source", test_editor_lsp_full_document_change_uses_active_source},
	{"editor_lsp_document_sync_ignores_non_go_buffers", test_editor_lsp_document_sync_ignores_non_go_buffers},
	{"editor_lsp_html_language_id_routing_for_supported_extensions", test_editor_lsp_html_language_id_routing_for_supported_extensions},
	{"editor_lsp_language_id_routing_for_css_scss_and_json", test_editor_lsp_language_id_routing_for_css_scss_and_json},
	{"editor_lsp_language_id_routing_for_javascript_extensions", test_editor_lsp_language_id_routing_for_javascript_extensions},
	{"editor_lsp_eslint_diagnostics_update_and_status_summary", test_editor_lsp_eslint_diagnostics_update_and_status_summary},
	{"editor_lsp_eslint_diagnostics_persist_across_tab_switches", test_editor_lsp_eslint_diagnostics_persist_across_tab_switches},
	{"editor_lsp_javascript_definition_coexists_with_eslint_sidecar", test_editor_lsp_javascript_definition_coexists_with_eslint_sidecar},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location", test_editor_process_keypress_ctrl_o_goto_definition_single_location},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer},
	{"editor_process_keypress_ctrl_o_goto_definition_single_location_jsx_buffer", test_editor_process_keypress_ctrl_o_goto_definition_single_location_jsx_buffer},
	{"editor_process_keypress_goto_definition_cross_file_reuses_tab", test_editor_process_keypress_goto_definition_cross_file_reuses_tab},
	{"editor_process_keypress_goto_definition_cross_file_javascript_fixture_reuses_tab", test_editor_process_keypress_goto_definition_cross_file_javascript_fixture_reuses_tab},
	{"editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab", test_editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab},
	{"editor_process_keypress_goto_definition_multi_picker_selects_choice", test_editor_process_keypress_goto_definition_multi_picker_selects_choice},
	{"editor_process_keypress_mouse_ctrl_click_goto_definition_single_location", test_editor_process_keypress_mouse_ctrl_click_goto_definition_single_location},
	{"editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice", test_editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice},
	{"editor_process_keypress_goto_definition_timeout_error_and_no_result", test_editor_process_keypress_goto_definition_timeout_error_and_no_result},
	{"editor_process_keypress_goto_definition_reports_lsp_disabled", test_editor_process_keypress_goto_definition_reports_lsp_disabled},
	{"editor_process_keypress_goto_definition_reports_lsp_disabled_for_c", test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_c},
	{"editor_process_keypress_goto_definition_reports_lsp_disabled_for_html", test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_html},
	{"editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled", test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled},
	{"editor_process_keypress_goto_definition_startup_failure_reports_reason", test_editor_process_keypress_goto_definition_startup_failure_reports_reason},
	{"editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer", test_editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer},
	{"editor_process_keypress_goto_definition_requires_saved_c_buffer", test_editor_process_keypress_goto_definition_requires_saved_c_buffer},
	{"editor_process_keypress_goto_definition_reports_empty_clangd_command", test_editor_process_keypress_goto_definition_reports_empty_clangd_command},
	{"editor_process_keypress_goto_definition_reports_empty_html_command", test_editor_process_keypress_goto_definition_reports_empty_html_command},
	{"editor_process_keypress_goto_definition_missing_gopls_decline_install", test_editor_process_keypress_goto_definition_missing_gopls_decline_install},
	{"editor_process_keypress_goto_definition_missing_gopls_starts_install_task", test_editor_process_keypress_goto_definition_missing_gopls_starts_install_task},
	{"editor_process_keypress_goto_definition_missing_clangd_declines_instructions", test_editor_process_keypress_goto_definition_missing_clangd_declines_instructions},
	{"editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions", test_editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions},
	{"editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install", test_editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install},
	{"editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task", test_editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task},
	{"editor_process_keypress_goto_definition_missing_javascript_server_starts_install_task", test_editor_process_keypress_goto_definition_missing_javascript_server_starts_install_task},
	{"editor_process_keypress_eslint_fix_action_applies_mock_edits", test_editor_process_keypress_eslint_fix_action_applies_mock_edits},
	{"editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task", test_editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task},
	{"editor_task_log_read_only_search_and_copy", test_editor_task_log_read_only_search_and_copy},
};

const int g_lsp_test_count =
		(int)(sizeof(g_lsp_tests) / sizeof(g_lsp_tests[0]));
