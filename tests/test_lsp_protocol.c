#include "test_case.h"
#include "test_helpers.h"
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
	int autocomplete_enabled = 0;
	int autocomplete_max_items = 0;

	enum editorLspConfigLoadStatus defaults_status = editorLspConfigLoadFromPaths(
	        &gopls_enabled, &clangd_enabled, &html_enabled, &css_enabled, &json_enabled,
	        &javascript_enabled, &eslint_enabled, gopls_command, sizeof(gopls_command),
	        gopls_install_command, sizeof(gopls_install_command), clangd_command,
	        sizeof(clangd_command), html_command, sizeof(html_command), css_command,
	        sizeof(css_command), json_command, sizeof(json_command), javascript_command,
	        sizeof(javascript_command), javascript_install_command,
	        sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
	        vscode_langservers_install_command, sizeof(vscode_langservers_install_command),
	        &autocomplete_enabled, &autocomplete_max_items, NULL, NULL);
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
	ASSERT_EQ_STR(
	        "npm install --global --prefix ~/.local typescript typescript-language-server",
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
	ASSERT_TRUE(write_text_file(
	        global_path, "[lsp]\n"
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
	ASSERT_TRUE(write_text_file(
	        project_path, "[lsp]\n"
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

	enum editorLspConfigLoadStatus status = editorLspConfigLoadFromPaths(
	        &gopls_enabled, &clangd_enabled, &html_enabled, &css_enabled, &json_enabled,
	        &javascript_enabled, &eslint_enabled, gopls_command, sizeof(gopls_command),
	        gopls_install_command, sizeof(gopls_install_command), clangd_command,
	        sizeof(clangd_command), html_command, sizeof(html_command), css_command,
	        sizeof(css_command), json_command, sizeof(json_command), javascript_command,
	        sizeof(javascript_command), javascript_install_command,
	        sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
	        vscode_langservers_install_command, sizeof(vscode_langservers_install_command),
	        &autocomplete_enabled, &autocomplete_max_items, global_path, project_path);
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
	int autocomplete_enabled = 0;
	int autocomplete_max_items = 0;

	char dir_template[] = "/tmp/rotide-test-lsp-config-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[lsp]\n"
	                                         "enabled = \"yes\"\n"));
	enum editorLspConfigLoadStatus status = editorLspConfigLoadFromPaths(
	        &gopls_enabled, &clangd_enabled, &html_enabled, &css_enabled, &json_enabled,
	        &javascript_enabled, &eslint_enabled, gopls_command, sizeof(gopls_command),
	        gopls_install_command, sizeof(gopls_install_command), clangd_command,
	        sizeof(clangd_command), html_command, sizeof(html_command), css_command,
	        sizeof(css_command), json_command, sizeof(json_command), javascript_command,
	        sizeof(javascript_command), javascript_install_command,
	        sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
	        vscode_langservers_install_command, sizeof(vscode_langservers_install_command),
	        &autocomplete_enabled, &autocomplete_max_items, global_path, NULL);
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
	ASSERT_EQ_STR(
	        "npm install --global --prefix ~/.local typescript typescript-language-server",
	        javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
	              vscode_langservers_install_command);

	ASSERT_TRUE(write_text_file(global_path, "[lsp]\n"
	                                         "clangd_enabled = false\n"
	                                         "html_enabled = false\n"
	                                         "css_enabled = false\n"));
	ASSERT_TRUE(write_text_file(project_path, "[lsp]\n"
	                                          "html_command = \"\"\n"));
	status = editorLspConfigLoadFromPaths(
	        &gopls_enabled, &clangd_enabled, &html_enabled, &css_enabled, &json_enabled,
	        &javascript_enabled, &eslint_enabled, gopls_command, sizeof(gopls_command),
	        gopls_install_command, sizeof(gopls_install_command), clangd_command,
	        sizeof(clangd_command), html_command, sizeof(html_command), css_command,
	        sizeof(css_command), json_command, sizeof(json_command), javascript_command,
	        sizeof(javascript_command), javascript_install_command,
	        sizeof(javascript_install_command), eslint_command, sizeof(eslint_command),
	        vscode_langservers_install_command, sizeof(vscode_langservers_install_command),
	        &autocomplete_enabled, &autocomplete_max_items, global_path, project_path);
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
	ASSERT_EQ_STR(
	        "npm install --global --prefix ~/.local typescript typescript-language-server",
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
	ASSERT_TRUE(
	        editorLspTestParseDefinitionResponse(location_link_response, &locations, &count));
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
	ASSERT_TRUE(strstr(request, "\"hierarchicalDocumentSymbolSupport\":true") != NULL);
	ASSERT_TRUE(strstr(request, "\"completion\"") != NULL);
	ASSERT_TRUE(strstr(request, "\"snippetSupport\":false") != NULL);
	ASSERT_EQ_INT('}', request[strlen(request) - 1]);
	free(request);
	return 0;
}

static int test_editor_lsp_parse_completion_provider_extracts_trigger_chars(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"capabilities\":{"
	                       "\"completionProvider\":{\"resolveProvider\":true,"
	                       "\"triggerCharacters\":[\".\",\":\",\">\"]}}}}";
	int supported = 0;
	char *trigger_chars = NULL;
	int ok = editorLspParseCompletionProviderInResponse(response, &supported, &trigger_chars);
	ASSERT_EQ_INT(1, ok);
	ASSERT_EQ_INT(1, supported);
	ASSERT_TRUE(trigger_chars != NULL);
	ASSERT_EQ_STR(".:>", trigger_chars);
	free(trigger_chars);
	return 0;
}

static int test_editor_lsp_parse_completion_provider_handles_missing(void) {
	int supported = 0;
	char *trigger_chars = NULL;
	int ok = editorLspParseCompletionProviderInResponse(
	        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"capabilities\":{}}}", &supported,
	        &trigger_chars);
	ASSERT_EQ_INT(1, ok);
	ASSERT_EQ_INT(0, supported);
	ASSERT_TRUE(trigger_chars == NULL);
	return 0;
}

static int test_editor_lsp_parse_completion_response_handles_items_array(void) {
	const char *response =
	        "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{"
	        "\"isIncomplete\":false,\"items\":["
	        "{\"label\":\"alpha\"},"
	        "{\"label\":\"beta\",\"insertText\":\"beta_(\"},"
	        "{\"label\":\"gamma\",\"textEdit\":{\"range\":{\"start\":{\"line\":1,"
	        "\"character\":2},\"end\":{\"line\":1,\"character\":5}},"
	        "\"newText\":\"gammaX\"}}"
	        "]}}";
	struct editorLspCompletionItem *items = NULL;
	int count = 0;
	ASSERT_TRUE(editorLspTestParseCompletionResponse(response, &items, &count));
	ASSERT_EQ_INT(3, count);
	ASSERT_TRUE(items != NULL);
	ASSERT_EQ_STR("alpha", items[0].label);
	ASSERT_TRUE(items[0].insert_text == NULL);
	ASSERT_EQ_INT(0, items[0].has_text_edit);
	ASSERT_EQ_STR("beta", items[1].label);
	ASSERT_EQ_STR("beta_(", items[1].insert_text);
	ASSERT_EQ_STR("gamma", items[2].label);
	ASSERT_EQ_INT(1, items[2].has_text_edit);
	ASSERT_EQ_INT(1, items[2].text_edit_start_line);
	ASSERT_EQ_INT(2, items[2].text_edit_start_character);
	ASSERT_EQ_INT(1, items[2].text_edit_end_line);
	ASSERT_EQ_INT(5, items[2].text_edit_end_character);
	ASSERT_EQ_STR("gammaX", items[2].text_edit_new_text);
	editorLspFreeCompletionItems(items, count);
	return 0;
}

static int test_editor_lsp_parse_completion_response_handles_plain_array(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":["
	                       "{\"label\":\"foo\"},{\"label\":\"bar\"}]}";
	struct editorLspCompletionItem *items = NULL;
	int count = 0;
	ASSERT_TRUE(editorLspTestParseCompletionResponse(response, &items, &count));
	ASSERT_EQ_INT(2, count);
	ASSERT_EQ_STR("foo", items[0].label);
	ASSERT_EQ_STR("bar", items[1].label);
	editorLspFreeCompletionItems(items, count);
	return 0;
}

static int test_editor_lsp_parse_completion_response_handles_null(void) {
	struct editorLspCompletionItem *items = NULL;
	int count = 0;
	ASSERT_TRUE(editorLspTestParseCompletionResponse(
	        "{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":null}", &items, &count));
	ASSERT_EQ_INT(0, count);
	ASSERT_TRUE(items == NULL);
	return 0;
}

static int test_editor_lsp_parse_document_symbols_handles_document_symbol_array(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":["
	                       "{\"name\":\"main\",\"kind\":12,"
	                       "\"range\":{\"start\":{\"line\":4,\"character\":0},"
	                       "\"end\":{\"line\":6,\"character\":1}},"
	                       "\"selectionRange\":{\"start\":{\"line\":4,\"character\":5},"
	                       "\"end\":{\"line\":4,\"character\":9}}},"
	                       "{\"name\":\"helper\",\"kind\":12,"
	                       "\"range\":{\"start\":{\"line\":8,\"character\":0},"
	                       "\"end\":{\"line\":10,\"character\":1}}}"
	                       "]}";
	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	ASSERT_EQ_INT(1, editorLspTestParseDocumentSymbolResponse(response, &symbols, &count));
	ASSERT_EQ_INT(2, count);
	ASSERT_EQ_STR("main", symbols[0].name);
	ASSERT_EQ_INT(12, symbols[0].kind);
	ASSERT_EQ_INT(4, symbols[0].line);
	ASSERT_EQ_STR("helper", symbols[1].name);
	ASSERT_EQ_INT(8, symbols[1].line);
	editorLspFreeSymbols(symbols, count);
	return 0;
}

static int test_editor_lsp_parse_document_symbols_handles_symbol_information_array(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":["
	                       "{\"name\":\"Foo\",\"kind\":5,"
	                       "\"location\":{\"uri\":\"file:///tmp/x.go\","
	                       "\"range\":{\"start\":{\"line\":1,\"character\":7},"
	                       "\"end\":{\"line\":1,\"character\":10}}}}"
	                       "]}";
	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	ASSERT_EQ_INT(1, editorLspTestParseDocumentSymbolResponse(response, &symbols, &count));
	ASSERT_EQ_INT(1, count);
	ASSERT_EQ_STR("Foo", symbols[0].name);
	ASSERT_EQ_INT(5, symbols[0].kind);
	ASSERT_EQ_INT(1, symbols[0].line);
	ASSERT_EQ_INT(7, symbols[0].character);
	editorLspFreeSymbols(symbols, count);
	return 0;
}

static int test_editor_lsp_parse_document_symbols_flattens_children(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":["
	                       "{\"name\":\"Outer\",\"kind\":5,"
	                       "\"range\":{\"start\":{\"line\":0,\"character\":0},"
	                       "\"end\":{\"line\":10,\"character\":1}},"
	                       "\"children\":["
	                       "{\"name\":\"first\",\"kind\":6,"
	                       "\"range\":{\"start\":{\"line\":2,\"character\":2},"
	                       "\"end\":{\"line\":3,\"character\":3}}},"
	                       "{\"name\":\"second\",\"kind\":6,"
	                       "\"range\":{\"start\":{\"line\":4,\"character\":2},"
	                       "\"end\":{\"line\":5,\"character\":3}}}"
	                       "]}"
	                       "]}";
	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	ASSERT_EQ_INT(1, editorLspTestParseDocumentSymbolResponse(response, &symbols, &count));
	ASSERT_EQ_INT(3, count);
	ASSERT_EQ_STR("Outer", symbols[0].name);
	ASSERT_EQ_INT(0, symbols[0].depth);
	ASSERT_EQ_INT(-1, symbols[0].parent_index);
	ASSERT_EQ_INT(1, symbols[0].is_last_sibling);

	ASSERT_EQ_STR("first", symbols[1].name);
	ASSERT_EQ_INT(1, symbols[1].depth);
	ASSERT_EQ_INT(0, symbols[1].parent_index);
	ASSERT_EQ_INT(0, symbols[1].is_last_sibling);

	ASSERT_EQ_STR("second", symbols[2].name);
	ASSERT_EQ_INT(1, symbols[2].depth);
	ASSERT_EQ_INT(0, symbols[2].parent_index);
	ASSERT_EQ_INT(1, symbols[2].is_last_sibling);
	editorLspFreeSymbols(symbols, count);
	return 0;
}

static int
test_editor_lsp_parse_document_symbols_uses_top_level_name_when_children_appear_first(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":["
	                       "{\"children\":["
	                       "{\"name\":\"enabled\",\"kind\":8,"
	                       "\"range\":{\"start\":{\"line\":22,\"character\":1},"
	                       "\"end\":{\"line\":22,\"character\":12}}}"
	                       "],"
	                       "\"name\":\"editorLspMockState\",\"kind\":23,"
	                       "\"range\":{\"start\":{\"line\":21,\"character\":7},"
	                       "\"end\":{\"line\":40,\"character\":1}}}"
	                       "]}";
	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	ASSERT_EQ_INT(1, editorLspTestParseDocumentSymbolResponse(response, &symbols, &count));
	ASSERT_EQ_INT(2, count);
	ASSERT_EQ_STR("editorLspMockState", symbols[0].name);
	ASSERT_EQ_INT(23, symbols[0].kind);
	ASSERT_EQ_INT(21, symbols[0].line);
	ASSERT_EQ_INT(0, symbols[0].depth);
	ASSERT_EQ_STR("enabled", symbols[1].name);
	ASSERT_EQ_INT(8, symbols[1].kind);
	ASSERT_EQ_INT(22, symbols[1].line);
	ASSERT_EQ_INT(1, symbols[1].depth);
	ASSERT_EQ_INT(0, symbols[1].parent_index);
	editorLspFreeSymbols(symbols, count);
	return 0;
}

const struct editorTestCase g_lsp_protocol_tests[] = {
        {"editor_lsp_config_defaults_and_precedence",
         test_editor_lsp_config_defaults_and_precedence},
        {"editor_lsp_config_invalid_values_fallback_defaults",
         test_editor_lsp_config_invalid_values_fallback_defaults},
        {"editor_lsp_parse_definition_response_handles_clangd_field_order",
         test_editor_lsp_parse_definition_response_handles_clangd_field_order},
        {"editor_lsp_build_initialize_request_json_is_complete",
         test_editor_lsp_build_initialize_request_json_is_complete},
        {"editor_lsp_parse_completion_provider_extracts_trigger_chars",
         test_editor_lsp_parse_completion_provider_extracts_trigger_chars},
        {"editor_lsp_parse_completion_provider_handles_missing",
         test_editor_lsp_parse_completion_provider_handles_missing},
        {"editor_lsp_parse_completion_response_handles_items_array",
         test_editor_lsp_parse_completion_response_handles_items_array},
        {"editor_lsp_parse_completion_response_handles_plain_array",
         test_editor_lsp_parse_completion_response_handles_plain_array},
        {"editor_lsp_parse_completion_response_handles_null",
         test_editor_lsp_parse_completion_response_handles_null},
        {"editor_lsp_parse_document_symbols_handles_document_symbol_array",
         test_editor_lsp_parse_document_symbols_handles_document_symbol_array},
        {"editor_lsp_parse_document_symbols_handles_symbol_information_array",
         test_editor_lsp_parse_document_symbols_handles_symbol_information_array},
        {"editor_lsp_parse_document_symbols_flattens_children",
         test_editor_lsp_parse_document_symbols_flattens_children},
        {"editor_lsp_parse_document_symbols_uses_top_level_name_when_children_appear_first",
         test_editor_lsp_parse_document_symbols_uses_top_level_name_when_children_appear_first},
};

const int g_lsp_protocol_test_count =
        (int)(sizeof(g_lsp_protocol_tests) / sizeof(g_lsp_protocol_tests[0]));
#include "config/lsp_config.h"
#include "language/lsp.h"
#include "language/lsp_responses.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
