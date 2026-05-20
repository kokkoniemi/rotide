#include "test_case.h"
#include "test_support.h"

static int test_editor_lsp_lifecycle_lazy_start_and_non_go_buffers(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char txt_path[64];
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(txt_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	char goto_def_txt[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def_txt, sizeof(goto_def_txt)) ==
	            0);

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
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def_go, sizeof(goto_def_go)) ==
	            0);

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
	ASSERT_TRUE(
	        write_temp_go_file(go_path, sizeof(go_path), "package main\n\nfunc main() {}\n"));
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
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	char c_path[64];
	char html_path[64];
	ASSERT_TRUE(
	        write_temp_go_file(go_path, sizeof(go_path),
	                           "package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	ASSERT_TRUE(write_temp_c_file(
	        c_path, sizeof(c_path),
	        "int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
	                                 "<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));

	editorOpen(go_path);
	E.cy = 3;
	E.cx = 15;
	struct editorLspLocation go_target = {.path = go_path, .line = 2, .character = 5};
	editorLspTestSetMockDefinitionResponse(1, &go_target, 1);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(c_path);
	E.cy = 1;
	E.cx = 24;
	struct editorLspLocation c_target = {.path = c_path, .line = 0, .character = 4};
	editorLspTestSetMockDefinitionResponse(1, &c_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;
	struct editorLspLocation html_target = {.path = html_path, .line = 0, .character = 9};
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
	ASSERT_TRUE(editorTabsInit());

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
	ASSERT_TRUE(write_text_file(
	        file_a, "int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	ASSERT_TRUE(write_text_file(
	        file_b, "int helper(void) { return 2; }\nint main(void) { return helper(); }\n"));

	struct editorLspLocation target = {.path = file_a, .line = 0, .character = 4};
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
	ASSERT_TRUE(editorTabsInit());

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

	struct editorLspLocation target = {.path = file_a, .line = 0, .character = 9};
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
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) {\n\treturn 0;\n}\n"));
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

static int test_editor_lsp_document_did_open_sent_on_file_open(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) {\n\treturn 0;\n}\n"));
	editorOpen(c_path);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.start_count);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(0, stats.did_change_count);
	ASSERT_EQ_INT(1, E.lsp_doc_open);
	ASSERT_EQ_INT(1, E.lsp_doc_version);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_lsp_did_change_without_syntax_edit_sends_full_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(
	        write_temp_go_file(go_path, sizeof(go_path), "package main\n\nfunc main() {}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(1, E.lsp_doc_open);

	editorSyntaxBackgroundSetEnabledForTests(0);
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('x');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_change_count);

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(0, change.had_range);
	ASSERT_TRUE(strncmp(change.text, "xpackage main", strlen("xpackage main")) == 0);

	ASSERT_TRUE(unlink(go_path) == 0);
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

static int test_editor_lsp_document_sync_for_css_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char css_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        css_path, sizeof(css_path), "rotide-test-css-lsp-fixture-", ".css",
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-javascript-lsp-fixture-", ".js",
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

static int test_editor_lsp_full_document_change_uses_active_source(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(
	        write_temp_go_file(go_path, sizeof(go_path), "package main\n\nfunc main() {}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	ASSERT_TRUE(editorLspNotifyDidChange(E.filename, E.syntax_language, &E.lsp_doc_open,
	                                     &E.lsp_doc_version, NULL, NULL, 0, NULL, 0));

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

static int test_editor_lsp_html_language_id_routing_for_supported_extensions(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char html_path[64];
	char htm_path[64];
	char xhtml_path[64];
	ASSERT_TRUE(write_temp_file_with_suffix(html_path, sizeof(html_path),
	                                        "rotide-test-html-route-", ".html",
	                                        "<div id=\"a\"></div>\n<a href=\"#a\">jump</a>\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(htm_path, sizeof(htm_path),
	                                        "rotide-test-html-route-", ".htm",
	                                        "<div id=\"b\"></div>\n<a href=\"#b\">jump</a>\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(xhtml_path, sizeof(xhtml_path),
	                                        "rotide-test-html-route-", ".xhtml",
	                                        "<div id=\"c\"></div>\n<a href=\"#c\">jump</a>\n"));

	struct editorLspLocation target = {.path = html_path, .line = 0, .character = 9};
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        css_path, sizeof(css_path), "rotide-test-css-route-", ".css",
	        "tests/lsp/supported/css/single_file_definition.css"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        scss_path, sizeof(scss_path), "rotide-test-scss-route-", ".scss",
	        "tests/lsp/supported/css/single_file_definition.scss"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        json_path, sizeof(json_path), "rotide-test-json-route-", ".json",
	        "tests/lsp/supported/json/single_file_definition.json"));

	struct editorLspLocation target = {.path = css_path, .line = 0, .character = 8};
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-javascript-route-", ".js",
	        "tests/lsp/supported/javascript/single_file_definition.js"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        mjs_path, sizeof(mjs_path), "rotide-test-javascript-route-", ".mjs",
	        "tests/lsp/supported/javascript/single_file_definition.js"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        cjs_path, sizeof(cjs_path), "rotide-test-javascript-route-", ".cjs",
	        "tests/lsp/supported/javascript/single_file_definition.js"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        jsx_path, sizeof(jsx_path), "rotide-test-javascript-route-", ".jsx",
	        "tests/lsp/supported/javascript/single_file_definition.jsx"));

	struct editorLspLocation target = {.path = js_path, .line = 0, .character = 6};
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

const struct editorTestCase g_lsp_lifecycle_tests[] = {
        {"editor_lsp_lifecycle_lazy_start_and_non_go_buffers",
         test_editor_lsp_lifecycle_lazy_start_and_non_go_buffers},
        {"editor_lsp_lifecycle_restart_after_mock_crash",
         test_editor_lsp_lifecycle_restart_after_mock_crash},
        {"editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html",
         test_editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html},
        {"editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes",
         test_editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes},
        {"editor_lsp_lifecycle_restarts_when_html_workspace_root_changes",
         test_editor_lsp_lifecycle_restarts_when_html_workspace_root_changes},
        {"editor_lsp_document_sync_for_go_edit_save_close",
         test_editor_lsp_document_sync_for_go_edit_save_close},
        {"editor_lsp_document_sync_for_c_edit_save_close",
         test_editor_lsp_document_sync_for_c_edit_save_close},
        {"editor_lsp_document_did_open_sent_on_file_open",
         test_editor_lsp_document_did_open_sent_on_file_open},
        {"editor_lsp_did_change_without_syntax_edit_sends_full_buffer",
         test_editor_lsp_did_change_without_syntax_edit_sends_full_buffer},
        {"editor_lsp_document_sync_for_html_edit_save_close",
         test_editor_lsp_document_sync_for_html_edit_save_close},
        {"editor_lsp_document_sync_for_css_edit_save_close",
         test_editor_lsp_document_sync_for_css_edit_save_close},
        {"editor_lsp_document_sync_for_javascript_edit_save_close",
         test_editor_lsp_document_sync_for_javascript_edit_save_close},
        {"editor_lsp_full_document_change_uses_active_source",
         test_editor_lsp_full_document_change_uses_active_source},
        {"editor_lsp_document_sync_ignores_non_go_buffers",
         test_editor_lsp_document_sync_ignores_non_go_buffers},
        {"editor_lsp_html_language_id_routing_for_supported_extensions",
         test_editor_lsp_html_language_id_routing_for_supported_extensions},
        {"editor_lsp_language_id_routing_for_css_scss_and_json",
         test_editor_lsp_language_id_routing_for_css_scss_and_json},
        {"editor_lsp_language_id_routing_for_javascript_extensions",
         test_editor_lsp_language_id_routing_for_javascript_extensions},
};

const int g_lsp_lifecycle_test_count =
        (int)(sizeof(g_lsp_lifecycle_tests) / sizeof(g_lsp_lifecycle_tests[0]));
