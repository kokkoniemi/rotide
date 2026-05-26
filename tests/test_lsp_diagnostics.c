#include "test_case.h"
#include "test_support.h"
#include "test_helpers.h"

static int find_drawer_entry_containing(const char *needle, int *idx_out,
                                        struct editorDrawerEntryView *view_out) {
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view = {0};
		if (!editorDrawerVisibleEntryView(i, &view) || view.name == NULL) {
			continue;
		}
		if (strstr(view.name, needle) != NULL) {
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

static int replace_active_text_for_lsp_drawer_test(const char *text) {
	if (text == NULL || E.document == NULL) {
		return 0;
	}
	struct editorDocumentEdit edit = {.kind = EDITOR_EDIT_INSERT_TEXT,
	                                  .start_offset = 0,
	                                  .old_len = editorDocumentLength(E.document),
	                                  .new_text = text,
	                                  .new_len = strlen(text),
	                                  .before_cursor_offset = 0,
	                                  .after_cursor_offset = 0,
	                                  .before_dirty = E.dirty,
	                                  .after_dirty = E.dirty + 1};
	return editorApplyDocumentEdit(&edit);
}

static int test_editor_lsp_eslint_diagnostics_update_and_status_summary(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspDiagnostic diagnostics[2] = {
	        {.start_line = 0,
	         .start_character = 0,
	         .end_line = 0,
	         .end_character = 5,
	         .severity = 1,
	         .message = "Unexpected space"},
	        {.start_line = 1,
	         .start_character = 0,
	         .end_line = 1,
	         .end_character = 11,
	         .severity = 2,
	         .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 2);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(2, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_error_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);
	ASSERT_TRUE(strstr(E.statusmsg, "LSP: 1 error, 1 warning") != NULL);
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

static int test_editor_lsp_diagnostic_status_uses_publishing_server_label(void) {
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path), "int main( {\n"));
	editorOpen(c_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	char *uri = NULL;
	ASSERT_TRUE(editorLspBuildFileUri(c_path, &uri));

	char message[1024];
	int len = snprintf(message, sizeof(message),
	                   "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
	                   "\"params\":{\"uri\":\"%s\",\"diagnostics\":[{\"range\":{\"start\":"
	                   "{\"line\":0,\"character\":4},\"end\":{\"line\":0,\"character\":8}},"
	                   "\"severity\":1,\"message\":\"syntax error\"}]}}",
	                   uri);
	ASSERT_TRUE(len > 0 && len < (int)sizeof(message));

	struct editorLspClient clangd_client = {0};
	clangd_client.server_kind = EDITOR_LSP_SERVER_CLANGD;
	ASSERT_TRUE(editorLspProcessIncomingMessage(&clangd_client, message));
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);
	ASSERT_TRUE(strstr(E.statusmsg, "clangd: 1 error, 0 warnings") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "ESLint") == NULL);

	len = snprintf(message, sizeof(message),
	               "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
	               "\"params\":{\"uri\":\"%s\",\"diagnostics\":[]}}",
	               uri);
	ASSERT_TRUE(len > 0 && len < (int)sizeof(message));
	ASSERT_TRUE(editorLspProcessIncomingMessage(&clangd_client, message));
	ASSERT_EQ_INT(0, E.lsp_diagnostic_count);
	ASSERT_TRUE(strstr(E.statusmsg, "clangd: diagnostics cleared") != NULL);

	free(uri);
	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_lsp_diagnostics_render_error_underline_and_cursor_popdown(void) {
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(write_temp_file_with_suffix(js_path, sizeof(js_path),
	                                        "rotide-test-lsp-diagnostic-render-", ".js",
	                                        "const value = 1;\n"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());
	E.window_rows = 8;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 2;

	char long_message[] =
	        "Unexpected token because the parser found a closing brace\nnote:\tbefore the "
	        "statement ended and could not recover from the remaining expression";
	struct editorLspDiagnostic diagnostics[1] = {
	        {.start_line = 0,
	         .start_character = 0,
	         .end_line = 0,
	         .end_character = 5,
	         .severity = 1,
	         .message = long_message},
	};
	editorLspSetDiagnosticsForPath(js_path, diagnostics, 1);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[4m\x1b[58;5;1m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[24m\x1b[59m") != NULL);
	ASSERT_TRUE(strstr(output, "Unexpected token") != NULL);
	ASSERT_TRUE(strstr(output, "closing brace") != NULL);
	ASSERT_TRUE(strstr(output, "note:") != NULL);
	ASSERT_TRUE(strstr(output, "remaining") != NULL);
	ASSERT_TRUE(strstr(output, "expression") != NULL);
	ASSERT_TRUE(strstr(output, "^J") == NULL);
	ASSERT_TRUE(strstr(output, "^I") == NULL);
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(js_path);
	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspDiagnostic diagnostics[1] = {
	        {.start_line = 1,
	         .start_character = 0,
	         .end_line = 1,
	         .end_character = 11,
	         .severity = 2,
	         .message = "Missing semicolon"},
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

static int test_editor_lsp_drawer_lists_diagnostics_and_jumps_to_problem(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-drawer-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);
	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');
	int dirty_before = E.dirty;

	struct editorLspDiagnostic diagnostics[1] = {
	        {.start_line = 1,
	         .start_character = 0,
	         .end_line = 1,
	         .end_character = 11,
	         .severity = 2,
	         .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 1);
	editorLspPumpNotifications();

	char lsp_drawer[] = {'\x1b', CTRL_KEY('l')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(lsp_drawer, sizeof(lsp_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(find_drawer_entry("Symbols", NULL, NULL));

	struct editorDrawerEntryView view = {0};
	int problem_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing("Missing semicolon", &problem_idx, &view));
	ASSERT_TRUE(strstr(view.name, "Warning") != NULL);
	ASSERT_EQ_STR(js_path, view.path);
	ASSERT_EQ_INT(1, view.line);

	E.window_cols = 160;
	ASSERT_TRUE(editorDrawerSetWidthForCols(80, E.window_cols));
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Missing semicolon") != NULL);
	ASSERT_TRUE(strstr(output, "Symbols") != NULL);
	free(output);

	ASSERT_TRUE(editorDrawerSelectVisibleIndex(problem_idx, E.window_rows));
	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(enter, sizeof(enter)) == 0);
	ASSERT_EQ_STR(js_path, E.filename);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_colors_problem_severity_labels(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-drawer-colors-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	struct editorLspDiagnostic diagnostics[2] = {
	        {.start_line = 0,
	         .start_character = 0,
	         .end_line = 0,
	         .end_character = 5,
	         .severity = 1,
	         .message = "Bad parse"},
	        {.start_line = 1,
	         .start_character = 0,
	         .end_line = 1,
	         .end_character = 11,
	         .severity = 2,
	         .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 2);
	editorLspPumpNotifications();

	char lsp_drawer[] = {'\x1b', CTRL_KEY('l')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(lsp_drawer, sizeof(lsp_drawer)) == 0);
	E.window_rows = 8;
	E.window_cols = 160;
	ASSERT_TRUE(editorDrawerSetWidthForCols(120, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[31mError\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[33mWarning\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "Bad parse") != NULL);
	ASSERT_TRUE(strstr(output, "Missing semicolon") != NULL);
	free(output);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_selected_problem_spills_into_text_area(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-drawer-spill-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	const char *long_message =
	        "very_long_diagnostic_message_that_exceeds_drawer_width_tail_segment";
	struct editorLspDiagnostic diagnostics[1] = {
	        {.start_line = 0,
	         .start_character = 0,
	         .end_line = 0,
	         .end_character = 1,
	         .severity = 1,
	         .message = (char *)long_message},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 1);
	editorLspPumpNotifications();

	char lsp_drawer[] = {'\x1b', CTRL_KEY('l')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(lsp_drawer, sizeof(lsp_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	int problem_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing(long_message, &problem_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(problem_idx, E.window_rows));

	E.window_rows = 6;
	E.window_cols = 200;
	ASSERT_TRUE(editorDrawerSetWidthForCols(20, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "tail_segment") != NULL);
	free(output);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_syntax_problem_clears_and_reappears(void) {
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) {\n\treturn 0;\n}\n"));
	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());

	(void)editorDrawerLspToggle();
	ASSERT_TRUE(find_drawer_entry("Problems (0)", NULL, NULL));
	ASSERT_TRUE(!find_drawer_entry_containing("Syntax parse error", NULL, NULL));

	ASSERT_TRUE(replace_active_text_for_lsp_drawer_test("int main( {\n"));
	ASSERT_TRUE(find_drawer_entry("Problems (1)", NULL, NULL));
	ASSERT_TRUE(find_drawer_entry_containing("Syntax parse error", NULL, NULL));

	ASSERT_TRUE(replace_active_text_for_lsp_drawer_test("int main(void) {\n\treturn 0;\n}\n"));
	ASSERT_TRUE(find_drawer_entry("Problems (0)", NULL, NULL));
	ASSERT_TRUE(!find_drawer_entry_containing("Syntax parse error", NULL, NULL));

	ASSERT_TRUE(replace_active_text_for_lsp_drawer_test("int main( {\n"));
	ASSERT_TRUE(find_drawer_entry("Problems (1)", NULL, NULL));
	ASSERT_TRUE(find_drawer_entry_containing("Syntax parse error", NULL, NULL));

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_lists_syntax_parse_error(void) {
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path), "int main( {\n"));
	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxStateHasError(E.syntax_state));
	int dirty_before = E.dirty;

	(void)editorDrawerLspToggle();
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);

	struct editorDrawerEntryView view = {0};
	int problem_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing("Syntax parse error", &problem_idx, &view));
	ASSERT_EQ_STR(c_path, view.path);
	ASSERT_TRUE(view.line >= 0);
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(problem_idx, E.window_rows));

	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(enter, sizeof(enter)) == 0);
	ASSERT_EQ_STR(c_path, E.filename);
	ASSERT_EQ_INT(dirty_before, E.dirty);

	ASSERT_TRUE(unlink(c_path) == 0);
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	size_t full_text_len = 0;
	char *full_text = editorDupActiveTextSource(&full_text_len);
	ASSERT_TRUE(full_text != NULL || full_text_len == 0);
	ASSERT_TRUE(editorLspEnsureDocumentOpen(E.filename, E.syntax_language, &E.lsp_doc_open,
	                                        &E.lsp_doc_version,
	                                        full_text != NULL ? full_text : "", full_text_len));
	ASSERT_TRUE(editorLspEnsureEslintDocumentOpen(
	        E.filename, E.syntax_language, &E.lsp_eslint_doc_open, &E.lsp_eslint_doc_version,
	        full_text != NULL ? full_text : "", full_text_len));
	free(full_text);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.did_open_count);
	ASSERT_EQ_INT(1, E.lsp_doc_open);
	ASSERT_EQ_INT(1, E.lsp_eslint_doc_open);

	struct editorLspDiagnostic diagnostics[1] = {
	        {.start_line = 1,
	         .start_character = 12,
	         .end_line = 1,
	         .end_character = 15,
	         .severity = 2,
	         .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 1);
	editorLspPumpNotifications();
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);

	E.cy = 1;
	E.cx = 13;
	struct editorLspLocation target = {.path = js_path, .line = 0, .character = 6};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(6, E.cx);

	struct editorLspDiagnostic edits[1] = {
	        {.start_line = 1,
	         .start_character = 16,
	         .end_line = 1,
	         .end_character = 16,
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	struct editorLspDiagnostic edits[1] = {
	        {.start_line = 1,
	         .start_character = 16,
	         .end_line = 1,
	         .end_character = 16,
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

static int
test_editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task(void) {
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
	E.lsp_vscode_langservers_install_command[sizeof(E.lsp_vscode_langservers_install_command) -
	                                         1] = '\0';

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-js-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	char input[] = {CTRL_KEY('t'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Install vscode-langservers-extracted",
	              editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("vscode-langservers-extracted installed. Retry Ctrl-O", E.statusmsg);

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

const struct editorTestCase g_lsp_diagnostics_tests[] = {
        {"editor_lsp_eslint_diagnostics_update_and_status_summary",
         test_editor_lsp_eslint_diagnostics_update_and_status_summary},
        {"editor_lsp_diagnostic_status_uses_publishing_server_label",
         test_editor_lsp_diagnostic_status_uses_publishing_server_label},
        {"editor_lsp_diagnostics_render_error_underline_and_cursor_popdown",
         test_editor_lsp_diagnostics_render_error_underline_and_cursor_popdown},
        {"editor_lsp_eslint_diagnostics_persist_across_tab_switches",
         test_editor_lsp_eslint_diagnostics_persist_across_tab_switches},
        {"editor_lsp_drawer_lists_diagnostics_and_jumps_to_problem",
         test_editor_lsp_drawer_lists_diagnostics_and_jumps_to_problem},
        {"editor_lsp_drawer_colors_problem_severity_labels",
         test_editor_lsp_drawer_colors_problem_severity_labels},
        {"editor_lsp_drawer_selected_problem_spills_into_text_area",
         test_editor_lsp_drawer_selected_problem_spills_into_text_area},
        {"editor_lsp_drawer_syntax_problem_clears_and_reappears",
         test_editor_lsp_drawer_syntax_problem_clears_and_reappears},
        {"editor_lsp_drawer_lists_syntax_parse_error",
         test_editor_lsp_drawer_lists_syntax_parse_error},
        {"editor_lsp_javascript_definition_coexists_with_eslint_sidecar",
         test_editor_lsp_javascript_definition_coexists_with_eslint_sidecar},
        {"editor_process_keypress_eslint_fix_action_applies_mock_edits",
         test_editor_process_keypress_eslint_fix_action_applies_mock_edits},
        {"editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task",
         test_editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task},
        {"editor_task_log_read_only_search_and_copy",
         test_editor_task_log_read_only_search_and_copy},
};

const int g_lsp_diagnostics_test_count =
        (int)(sizeof(g_lsp_diagnostics_tests) / sizeof(g_lsp_diagnostics_tests[0]));
#include "editing/edit.h"
#include "language/syntax.h"
#include "rotide.h"
#include "editing/buffer_core.h"
#include "editing/edit_pipeline.h"
#include "language/lsp.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"
#include "workspace/task.h"
#include "editor_test_api.h"
#include "language/lsp_transport.h"
#include "editing/text_source.h"
#include "text/document.h"
#include "editing/selection.h"
#include "language/lsp_protocol.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
