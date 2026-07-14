#include "language/lsp_protocol.h"
#include "language/lsp_transport.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

static int test_editor_lsp_config_defaults_and_precedence(void) {
	struct editorLspConfig config = {0};

	enum editorLspConfigLoadStatus defaults_status =
	        editorLspConfigLoadFromPaths(&config, NULL, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_OK, defaults_status);
	ASSERT_EQ_INT(1, config.gopls_enabled);
	ASSERT_EQ_INT(1, config.clangd_enabled);
	ASSERT_EQ_INT(1, config.html_enabled);
	ASSERT_EQ_INT(1, config.css_enabled);
	ASSERT_EQ_INT(1, config.json_enabled);
	ASSERT_EQ_INT(1, config.javascript_enabled);
	ASSERT_EQ_INT(1, config.eslint_enabled);
	ASSERT_EQ_STR("gopls", config.gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", config.gopls_install_command);
	ASSERT_EQ_STR("clangd", config.clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", config.html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", config.css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", config.json_command);
	ASSERT_EQ_STR("~/.local/bin/typescript-language-server --stdio", config.javascript_command);
	ASSERT_EQ_STR(
	        "npm install --global --prefix ~/.local typescript typescript-language-server",
	        config.javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", config.eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
	              config.vscode_langservers_install_command);
	ASSERT_EQ_INT(1, config.texlab_enabled);
	ASSERT_EQ_STR("~/.local/bin/texlab", config.texlab_command);
	ASSERT_EQ_STR("", config.texlab_install_command);
	ASSERT_EQ_STR("", config.texlab_pdf_viewer);
	ASSERT_EQ_STR("", config.texlab_forward_search_command);
	ASSERT_EQ_STR("", config.texlab_build_command);
	ASSERT_EQ_STR("", config.texlab_aux_directory);
	ASSERT_EQ_STR("", config.texlab_pdf_directory);
	ASSERT_EQ_INT(0, config.texlab_build_on_save);
	ASSERT_EQ_INT(0, config.texlab_forward_search_after_build);

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
	                     "texlab_command = \"texlab-global\"\n"
	                     "texlab_install_command = \"texlab-global-install\"\n"
	                     "texlab_pdf_viewer = \"okular\"\n"
	                     "texlab_forward_search_command = \"viewer-global --line %l %p\"\n"
	                     "texlab_build_command = \"build-global %f\"\n"
	                     "texlab_build_on_save = true\n"
	                     "texlab_forward_search_after_build = false\n"
	                     "texlab_aux_directory = \"global-aux\"\n"
	                     "texlab_pdf_directory = \"global-pdf\"\n"
	                     "gopls_install_command = \"global-install\"\n"
	                     "vscode_langservers_install_command = \"global-vscode-install\"\n"));
	ASSERT_TRUE(write_text_file(
	        project_path,
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
	        "texlab_command = \"texlab-project\"\n"
	        "texlab_install_command = \"texlab-project-install\"\n"
	        "texlab_pdf_viewer = \"zathura\"\n"
	        "texlab_forward_search_command = \"viewer-project \\\"two words\\\" %p\"\n"
	        "texlab_build_command = \"\"\n"
	        "texlab_build_on_save = false\n"
	        "texlab_forward_search_after_build = true\n"
	        "texlab_aux_directory = \"project-aux\"\n"
	        "texlab_pdf_directory = \"\"\n"
	        "gopls_install_command = \"project-install\"\n"
	        "vscode_langservers_install_command = \"project-vscode-install\"\n"));

	enum editorLspConfigLoadStatus status =
	        editorLspConfigLoadFromPaths(&config, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_OK, status);
	ASSERT_EQ_INT(0, config.gopls_enabled);
	ASSERT_EQ_INT(0, config.clangd_enabled);
	ASSERT_EQ_INT(0, config.html_enabled);
	ASSERT_EQ_INT(0, config.css_enabled);
	ASSERT_EQ_INT(0, config.json_enabled);
	ASSERT_EQ_INT(0, config.javascript_enabled);
	ASSERT_EQ_INT(0, config.eslint_enabled);
	ASSERT_EQ_STR("gopls-global", config.gopls_command);
	ASSERT_EQ_STR("global-install", config.gopls_install_command);
	ASSERT_EQ_STR("clangd-global", config.clangd_command);
	ASSERT_EQ_STR("html-global --stdio", config.html_command);
	ASSERT_EQ_STR("css-global --stdio", config.css_command);
	ASSERT_EQ_STR("json-global --stdio", config.json_command);
	ASSERT_EQ_STR("javascript-global --stdio", config.javascript_command);
	ASSERT_EQ_STR("javascript-global-install", config.javascript_install_command);
	ASSERT_EQ_STR("eslint-global --stdio", config.eslint_command);
	ASSERT_EQ_STR("global-vscode-install", config.vscode_langservers_install_command);
	ASSERT_EQ_STR("texlab-global", config.texlab_command);
	/* Install command is global-only: the project value must not override it. */
	ASSERT_EQ_STR("texlab-global-install", config.texlab_install_command);

	ASSERT_EQ_STR("zathura", config.texlab_pdf_viewer);
	ASSERT_EQ_STR("viewer-global --line %l %p", config.texlab_forward_search_command);
	ASSERT_EQ_STR("build-global %f", config.texlab_build_command);
	ASSERT_EQ_STR("project-aux", config.texlab_aux_directory);
	ASSERT_EQ_STR("", config.texlab_pdf_directory);
	ASSERT_EQ_INT(1, config.texlab_build_on_save);
	ASSERT_EQ_INT(1, config.texlab_forward_search_after_build);
	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_config_invalid_values_fallback_defaults(void) {
	struct editorLspConfig config = {0};

	char dir_template[] = "/tmp/rotide-test-lsp-config-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path, "[lsp]\n"
	                                         "enabled = \"yes\"\n"));
	enum editorLspConfigLoadStatus status =
	        editorLspConfigLoadFromPaths(&config, global_path, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_INT(1, config.gopls_enabled);
	ASSERT_EQ_INT(1, config.clangd_enabled);
	ASSERT_EQ_INT(1, config.html_enabled);
	ASSERT_EQ_INT(1, config.css_enabled);
	ASSERT_EQ_INT(1, config.json_enabled);
	ASSERT_EQ_INT(1, config.javascript_enabled);
	ASSERT_EQ_INT(1, config.eslint_enabled);
	ASSERT_EQ_STR("gopls", config.gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", config.gopls_install_command);
	ASSERT_EQ_STR("clangd", config.clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", config.html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", config.css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", config.json_command);
	ASSERT_EQ_STR("~/.local/bin/typescript-language-server --stdio", config.javascript_command);
	ASSERT_EQ_STR(
	        "npm install --global --prefix ~/.local typescript typescript-language-server",
	        config.javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", config.eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
	              config.vscode_langservers_install_command);

	ASSERT_TRUE(write_text_file(global_path, "[lsp]\n"
	                                         "texlab_pdf_viewer = \"sumatra\"\n"));
	status = editorLspConfigLoadFromPaths(&config, global_path, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_STR("", config.texlab_pdf_viewer);

	ASSERT_TRUE(write_text_file(global_path, "[lsp]\n"
	                                         "clangd_enabled = false\n"
	                                         "html_enabled = false\n"
	                                         "css_enabled = false\n"));
	ASSERT_TRUE(write_text_file(project_path, "[lsp]\n"
	                                          "texlab_pdf_viewer = \"sumatra\"\n"));
	status = editorLspConfigLoadFromPaths(&config, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(1, config.gopls_enabled);
	ASSERT_EQ_INT(1, config.clangd_enabled);
	ASSERT_EQ_INT(1, config.html_enabled);
	ASSERT_EQ_INT(1, config.css_enabled);
	ASSERT_EQ_INT(1, config.json_enabled);
	ASSERT_EQ_INT(1, config.javascript_enabled);
	ASSERT_EQ_INT(1, config.eslint_enabled);
	ASSERT_EQ_STR("gopls", config.gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", config.gopls_install_command);
	ASSERT_EQ_STR("clangd", config.clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", config.html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", config.css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", config.json_command);
	ASSERT_EQ_STR("~/.local/bin/typescript-language-server --stdio", config.javascript_command);
	ASSERT_EQ_STR(
	        "npm install --global --prefix ~/.local typescript typescript-language-server",
	        config.javascript_install_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", config.eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
	              config.vscode_langservers_install_command);

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
	char *request = editorLspTestBuildInitializeRequestJson(7, "file:///tmp/project", 1234,
	                                                        EDITOR_LSP_SERVER_GOPLS);
	ASSERT_TRUE(request != NULL);
	ASSERT_TRUE(strstr(request, "\"id\":7") != NULL);
	ASSERT_TRUE(strstr(request, "\"processId\":1234") != NULL);
	ASSERT_TRUE(strstr(request, "\"rootUri\":\"file:///tmp/project\"") != NULL);
	ASSERT_TRUE(strstr(request, "\"source.fixAll.eslint\"") != NULL);
	ASSERT_TRUE(strstr(request, "\"hierarchicalDocumentSymbolSupport\":true") != NULL);
	ASSERT_TRUE(strstr(request, "\"completion\"") != NULL);
	ASSERT_TRUE(strstr(request, "\"snippetSupport\":false") != NULL);
	ASSERT_TRUE(strstr(request, "\"showDocument\":{\"support\":true}") != NULL);
	ASSERT_TRUE(strstr(request, "\"workspace\":{\"applyEdit\":true,\"configuration\":true}") !=
	            NULL);
	ASSERT_EQ_STR("true}}}}}", request + strlen(request) - 9);
	ASSERT_TRUE(strstr(request, "\"initializationOptions\"") == NULL);
	free(request);
	return 0;
}

static int test_editor_lsp_build_text_document_requests(void) {
	char *forward = editorLspBuildTextDocumentRequestJson(11, "textDocument/forwardSearch",
	                                                      "file:///tmp/main.tex", 4, 7, 1);
	ASSERT_TRUE(forward != NULL);
	ASSERT_EQ_STR("{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/forwardSearch\","
	              "\"params\":{\"textDocument\":{\"uri\":\"file:///tmp/main.tex\"},"
	              "\"position\":{\"line\":4,\"character\":7}}}",
	              forward);
	free(forward);

	char *build = editorLspBuildTextDocumentRequestJson(12, "textDocument/build",
	                                                    "file:///tmp/main.tex", 0, 0, 0);
	ASSERT_TRUE(build != NULL);
	ASSERT_EQ_STR("{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/build\","
	              "\"params\":{\"textDocument\":{\"uri\":\"file:///tmp/main.tex\"}}}",
	              build);
	free(build);
	return 0;
}

static int test_editor_lsp_texlab_initialize_options(void) {
	editorLspConfigInitDefaults(&E.lsp_config);

	char *request = editorLspTestBuildInitializeRequestJson(8, "file:///tmp/latex-project",
	                                                        1234, EDITOR_LSP_SERVER_TEXLAB);
	ASSERT_TRUE(request != NULL);
	ASSERT_TRUE(strstr(request, "\"initializationOptions\":{}") != NULL);
	ASSERT_TRUE(strstr(request, "\"forwardSearch\"") == NULL);
	ASSERT_TRUE(strstr(request, "\"build\"") == NULL);
	free(request);

	(void)snprintf(E.lsp_config.texlab_pdf_viewer, sizeof(E.lsp_config.texlab_pdf_viewer), "%s",
	               "okular");
	(void)snprintf(E.lsp_config.texlab_build_command, sizeof(E.lsp_config.texlab_build_command),
	               "%s", "distrobox enter latex -- latexmk \"-jobname=paper draft\" %f");
	(void)snprintf(E.lsp_config.texlab_aux_directory, sizeof(E.lsp_config.texlab_aux_directory),
	               "%s", "build");
	(void)snprintf(E.lsp_config.texlab_pdf_directory, sizeof(E.lsp_config.texlab_pdf_directory),
	               "%s", "out dir");
	E.lsp_config.texlab_build_on_save = 0;
	E.lsp_config.texlab_forward_search_after_build = 1;

	request = editorLspTestBuildInitializeRequestJson(9, "file:///tmp/latex-project", 1234,
	                                                  EDITOR_LSP_SERVER_TEXLAB);
	ASSERT_TRUE(request != NULL);
	ASSERT_TRUE(strstr(request, "\"hierarchicalDocumentSymbolSupport\":true}}},"
	                            "\"initializationOptions\":") != NULL);
	ASSERT_TRUE(strstr(request, "\"forwardSearch\":{\"executable\":\"okular\","
	                            "\"args\":[\"--unique\",\"file:%p#src:%l%f\"]}") != NULL);
	ASSERT_TRUE(strstr(request, "\"build\":{\"executable\":\"distrobox\","
	                            "\"args\":[\"enter\",\"latex\",\"--\",\"latexmk\","
	                            "\"-jobname=paper draft\",\"%f\"]") != NULL);
	ASSERT_TRUE(strstr(request, "\"onSave\":false") != NULL);
	ASSERT_TRUE(strstr(request, "\"forwardSearchAfter\":true") != NULL);
	ASSERT_TRUE(strstr(request, "\"auxDirectory\":\"build\"") != NULL);
	ASSERT_TRUE(strstr(request,
	                   "\"forwardSearchAfter\":true,"
	                   "\"pdfDirectory\":\"out dir\"},\"auxDirectory\":\"build\"") != NULL);
	free(request);

	(void)snprintf(E.lsp_config.texlab_forward_search_command,
	               sizeof(E.lsp_config.texlab_forward_search_command), "%s",
	               "\"my viewer\" --source \"%l %f\" %p");
	E.lsp_config.texlab_build_command[0] = '\0';
	E.lsp_config.texlab_aux_directory[0] = '\0';
	E.lsp_config.texlab_pdf_directory[0] = '\0';
	E.lsp_config.texlab_build_on_save = 0;
	E.lsp_config.texlab_forward_search_after_build = 0;

	request = editorLspTestBuildInitializeRequestJson(10, "file:///tmp/latex-project", 1234,
	                                                  EDITOR_LSP_SERVER_TEXLAB);
	ASSERT_TRUE(request != NULL);
	ASSERT_TRUE(strstr(request, "\"forwardSearch\":{\"executable\":\"my viewer\","
	                            "\"args\":[\"--source\",\"%l %f\",\"%p\"]}") != NULL);
	ASSERT_TRUE(strstr(request, "\"executable\":\"okular\"") == NULL);
	free(request);
	return 0;
}

static int test_editor_lsp_texlab_workspace_configuration_response(void) {
	editorLspConfigInitDefaults(&E.lsp_config);
	(void)snprintf(E.lsp_config.texlab_pdf_viewer, sizeof(E.lsp_config.texlab_pdf_viewer), "%s",
	               "okular");
	(void)snprintf(E.lsp_config.texlab_build_command, sizeof(E.lsp_config.texlab_build_command),
	               "%s", "distrobox enter latex -- make build");
	(void)snprintf(E.lsp_config.texlab_aux_directory, sizeof(E.lsp_config.texlab_aux_directory),
	               "%s", "aux");
	(void)snprintf(E.lsp_config.texlab_pdf_directory, sizeof(E.lsp_config.texlab_pdf_directory),
	               "%s", "pdf");
	E.lsp_config.texlab_build_on_save = 1;
	E.lsp_config.texlab_forward_search_after_build = 1;

	int fds[2];
	ASSERT_EQ_INT(0, pipe(fds));
	struct editorLspClient client = {
	        .to_server_fd = fds[1],
	        .from_server_fd = -1,
	        .server_kind = EDITOR_LSP_SERVER_TEXLAB,
	};
	const char *message =
	        "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"workspace/configuration\","
	        "\"params\":{\"items\":[{\"section\":\"texlab\"}]}}";
	ASSERT_TRUE(editorLspProcessIncomingMessage(&client, message));
	close(fds[1]);

	char response[8192];
	ssize_t nread = read(fds[0], response, sizeof(response) - 1);
	close(fds[0]);
	ASSERT_TRUE(nread > 0);
	response[nread] = '\0';
	ASSERT_TRUE(strstr(response, "\"id\":17") != NULL);
	ASSERT_TRUE(strstr(response, "\"result\":[{\"forwardSearch\":{\"executable\":\"okular\"") !=
	            NULL);
	ASSERT_TRUE(strstr(response,
	                   "\"build\":{\"executable\":\"distrobox\","
	                   "\"args\":[\"enter\",\"latex\",\"--\",\"make\",\"build\"]") != NULL);
	ASSERT_TRUE(strstr(response, "\"onSave\":true") != NULL);
	ASSERT_TRUE(strstr(response, "\"forwardSearchAfter\":true") != NULL);
	ASSERT_TRUE(strstr(response, "\"pdfDirectory\":\"pdf\"") != NULL);
	ASSERT_TRUE(strstr(response, "\"auxDirectory\":\"aux\"") != NULL);
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

static int test_editor_lsp_parse_hover_response_handles_markup_content(void) {
	const char *response =
	        "{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"contents\":{\"kind\":\"markdown\","
	        "\"value\":\"```c\\nint helper(void)\\n```\\nDocs\"}}}";
	char *text = NULL;
	ASSERT_TRUE(editorLspTestParseHoverResponse(response, &text));
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_STR("```c\nint helper(void)\n```\nDocs", text);
	free(text);
	return 0;
}

static int test_editor_lsp_parse_hover_response_handles_marked_string_array(void) {
	const char *response = "{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"contents\":["
	                       "{\"language\":\"c\",\"value\":\"int helper(void)\"},\"Docs\"]}}";
	char *text = NULL;
	ASSERT_TRUE(editorLspTestParseHoverResponse(response, &text));
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_STR("int helper(void)\n\nDocs", text);
	free(text);
	return 0;
}

static int test_editor_lsp_parse_hover_response_handles_null(void) {
	char *text = (char *)1;
	ASSERT_TRUE(editorLspTestParseHoverResponse("{\"jsonrpc\":\"2.0\",\"id\":4,"
	                                            "\"result\":null}",
	                                            &text));
	ASSERT_TRUE(text == NULL);
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

static int test_editor_lsp_texlab_file_mapping(void) {
	ASSERT_EQ_INT(EDITOR_LSP_SERVER_TEXLAB,
	              editorLspServerKindForFile("paper.tex", EDITOR_SYNTAX_LATEX));
	ASSERT_EQ_STR("texlab", editorLspServerNameForFile("paper.tex", EDITOR_SYNTAX_LATEX));
	ASSERT_EQ_STR("latex", editorLspLanguageIdForFile("paper.tex", EDITOR_SYNTAX_LATEX));
	ASSERT_EQ_INT(1, editorLspFileSupportsDefinition("paper.tex", EDITOR_SYNTAX_LATEX));

	ASSERT_EQ_INT(EDITOR_LSP_SERVER_TEXLAB,
	              editorLspServerKindForFile("refs.bib", EDITOR_SYNTAX_BIBTEX));
	ASSERT_EQ_STR("texlab", editorLspServerNameForFile("refs.bib", EDITOR_SYNTAX_BIBTEX));
	ASSERT_EQ_STR("bibtex", editorLspLanguageIdForFile("refs.bib", EDITOR_SYNTAX_BIBTEX));
	return 0;
}

static int test_editor_lsp_texlab_prebuilt_asset_mapping(void) {
	ASSERT_EQ_STR("texlab-x86_64-linux.tar.gz",
	              editorLanguageTexlabAssetFor("Linux", "x86_64", 0));
	ASSERT_EQ_STR("texlab-x86_64-alpine.tar.gz",
	              editorLanguageTexlabAssetFor("Linux", "x86_64", 1));
	ASSERT_EQ_STR("texlab-aarch64-linux.tar.gz",
	              editorLanguageTexlabAssetFor("Linux", "aarch64", 0));
	ASSERT_EQ_STR("texlab-armv7hf-linux.tar.gz",
	              editorLanguageTexlabAssetFor("Linux", "armv7l", 0));
	ASSERT_EQ_STR("texlab-x86_64-macos.tar.gz",
	              editorLanguageTexlabAssetFor("Darwin", "x86_64", 0));
	ASSERT_EQ_STR("texlab-aarch64-macos.tar.gz",
	              editorLanguageTexlabAssetFor("Darwin", "arm64", 0));
	ASSERT_TRUE(editorLanguageTexlabAssetFor("SunOS", "sparc", 0) == NULL);
	ASSERT_TRUE(editorLanguageTexlabAssetFor("Linux", "riscv64", 0) == NULL);
	ASSERT_TRUE(editorLanguageTexlabAssetFor(NULL, "x86_64", 0) == NULL);
	return 0;
}

static int test_editor_language_server_name_is_installable(void) {
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("gopls"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("clangd"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("texlab"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("typescript-language-server"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("javascript"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("vscode-langservers-extracted"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("html"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("css"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("json"));
	ASSERT_EQ_INT(1, editorLanguageServerNameIsInstallable("eslint"));
	/* "LSP" is the fallback name for a buffer with no language server. */
	ASSERT_EQ_INT(0, editorLanguageServerNameIsInstallable("LSP"));
	ASSERT_EQ_INT(0, editorLanguageServerNameIsInstallable("nonsense"));
	ASSERT_EQ_INT(0, editorLanguageServerNameIsInstallable(""));
	ASSERT_EQ_INT(0, editorLanguageServerNameIsInstallable(NULL));
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
        {"editor_lsp_build_text_document_requests", test_editor_lsp_build_text_document_requests},
        {"editor_lsp_texlab_initialize_options", test_editor_lsp_texlab_initialize_options},
        {"editor_lsp_texlab_workspace_configuration_response",
         test_editor_lsp_texlab_workspace_configuration_response},
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
        {"editor_lsp_parse_hover_response_handles_markup_content",
         test_editor_lsp_parse_hover_response_handles_markup_content},
        {"editor_lsp_parse_hover_response_handles_marked_string_array",
         test_editor_lsp_parse_hover_response_handles_marked_string_array},
        {"editor_lsp_parse_hover_response_handles_null",
         test_editor_lsp_parse_hover_response_handles_null},
        {"editor_lsp_parse_document_symbols_handles_document_symbol_array",
         test_editor_lsp_parse_document_symbols_handles_document_symbol_array},
        {"editor_lsp_parse_document_symbols_handles_symbol_information_array",
         test_editor_lsp_parse_document_symbols_handles_symbol_information_array},
        {"editor_lsp_parse_document_symbols_flattens_children",
         test_editor_lsp_parse_document_symbols_flattens_children},
        {"editor_lsp_parse_document_symbols_uses_top_level_name_when_children_appear_first",
         test_editor_lsp_parse_document_symbols_uses_top_level_name_when_children_appear_first},
        {"editor_lsp_texlab_file_mapping", test_editor_lsp_texlab_file_mapping},
        {"editor_lsp_texlab_prebuilt_asset_mapping", test_editor_lsp_texlab_prebuilt_asset_mapping},
        {"editor_language_server_name_is_installable",
         test_editor_language_server_name_is_installable},
};

const int g_lsp_protocol_test_count =
        (int)(sizeof(g_lsp_protocol_tests) / sizeof(g_lsp_protocol_tests[0]));
#include "config/lsp_config.h"
#include "input/actions_language.h"
#include "language/lsp.h"
#include "language/lsp_responses.h"
#include "language/lsp_transport.h"
#include "language/syntax.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
