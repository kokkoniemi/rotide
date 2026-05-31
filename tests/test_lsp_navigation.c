#include "render/popup.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

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

static int test_editor_lsp_drawer_lists_document_symbols_and_jumps_to_symbol(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(
	        go_path, sizeof(go_path),
	        "package main\n\nfunc helper() {}\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);

	struct editorLspSymbol symbols[2] = {
	        {.name = "helper", .kind = 12, .line = 2, .character = 5},
	        {.name = "main", .kind = 12, .line = 4, .character = 5},
	};
	editorLspTestSetMockDocumentSymbolResponse(1, symbols, 2);

	char lsp_drawer[] = {'\x1b', CTRL_KEY('l')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(lsp_drawer, sizeof(lsp_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.document_symbol_count);
	ASSERT_EQ_INT(2, E.lsp_symbol_count);

	struct editorDrawerEntryView view = {0};
	int helper_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing("helper", &helper_idx, &view));
	ASSERT_TRUE(strstr(view.name, "Function") != NULL);
	ASSERT_EQ_INT(2, view.line);

	int main_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing("main", &main_idx, NULL));

	ASSERT_TRUE(editorDrawerSelectVisibleIndex(helper_idx, E.window_rows));
	char enter[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(enter, sizeof(enter)) == 0);
	ASSERT_EQ_STR(go_path, E.filename);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_arrow_previews_symbol_centered_away_from_drawer_cursor(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n"
	                               "// 1\n"
	                               "// 2\n"
	                               "// 3\n"
	                               "// 4\n"
	                               "// 5\n"
	                               "// 6\n"
	                               "// 7\n"
	                               "// 8\n"
	                               "// 9\n"
	                               "// 10\n"
	                               "// 11\n"
	                               "// 12\n"
	                               "// 13\n"
	                               "// 14\n"
	                               "// 15\n"
	                               "// 16\n"
	                               "// 17\n"
	                               "// 18\n"
	                               "// 19\n"
	                               "func target() {}\n"
	                               "func tail() {}\n"));
	editorOpen(go_path);
	E.window_rows = 9;
	E.window_cols = 80;

	struct editorLspSymbol symbols[1] = {
	        {.name = "target", .kind = 12, .line = 20, .character = 5},
	};
	editorLspTestSetMockDocumentSymbolResponse(1, symbols, 1);

	char lsp_drawer[] = {'\x1b', CTRL_KEY('l')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(lsp_drawer, sizeof(lsp_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	const char arrow_down[] = "\x1b[B";
	for (int i = 0; i < 5; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input_silent(arrow_down,
		                                                      strlen(arrow_down)) == 0);
	}

	ASSERT_EQ_INT(20, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_INT(E.window_rows / 2, E.drawer_selected_index - E.drawer_rowoff);
	ASSERT_TRUE(E.cy - E.rowoff != E.drawer_selected_index - E.drawer_rowoff);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_renders_nested_symbols_hierarchically(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path), "struct Outer { int a; int b; };\n"));
	editorOpen(c_path);

	struct editorLspSymbol symbols[3] = {
	        {.name = "Outer",
	         .kind = 23,
	         .line = 0,
	         .character = 7,
	         .depth = 0,
	         .parent_index = -1,
	         .is_last_sibling = 1},
	        {.name = "a",
	         .kind = 8,
	         .line = 0,
	         .character = 19,
	         .depth = 1,
	         .parent_index = 0,
	         .is_last_sibling = 0},
	        {.name = "b",
	         .kind = 8,
	         .line = 0,
	         .character = 26,
	         .depth = 1,
	         .parent_index = 0,
	         .is_last_sibling = 1},
	};
	editorLspTestSetMockDocumentSymbolResponse(1, symbols, 3);

	char lsp_drawer[] = {'\x1b', CTRL_KEY('l')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(lsp_drawer, sizeof(lsp_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(3, E.lsp_symbol_count);

	int outer_idx = -1;
	struct editorDrawerEntryView outer_view = {0};
	ASSERT_TRUE(find_drawer_entry_containing("Outer", &outer_idx, &outer_view));
	ASSERT_EQ_INT(2, outer_view.depth);

	int field_a_idx = -1;
	struct editorDrawerEntryView field_a = {0};
	ASSERT_TRUE(find_drawer_entry_containing("Field a:1", &field_a_idx, &field_a));
	ASSERT_EQ_INT(3, field_a.depth);
	ASSERT_EQ_INT(outer_idx, field_a.parent_visible_idx);
	ASSERT_EQ_INT(0, field_a.is_last_sibling);

	int field_b_idx = -1;
	struct editorDrawerEntryView field_b = {0};
	ASSERT_TRUE(find_drawer_entry_containing("Field b:1", &field_b_idx, &field_b));
	ASSERT_EQ_INT(3, field_b.depth);
	ASSERT_EQ_INT(outer_idx, field_b.parent_visible_idx);
	ASSERT_EQ_INT(1, field_b.is_last_sibling);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-fixture-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	E.cy = 5;
	E.cx = 5;

	struct editorLspLocation target = {.path = go_path, .line = 2, .character = 5};
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

static int test_editor_process_keypress_alt_i_goto_implementation_jumps_to_target(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-impl-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	E.cy = 5;
	E.cx = 5;

	struct editorLspLocation target = {.path = go_path, .line = 2, .character = 5};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_impl[] = {'\x1b', 'i'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_impl, sizeof(goto_impl)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.implementation_count);
	ASSERT_EQ_INT(0, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_alt_s_goto_symbol_jumps_to_first_symbol(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-sym-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	E.cy = 0;
	E.cx = 0;

	struct editorLspSymbol symbols[1] = {
	        {.name = "main", .kind = 12, .line = 4, .character = 0},
	};
	editorLspTestSetMockDocumentSymbolResponse(1, symbols, 1);

	char goto_sym[] = {'\x1b', 's'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_sym, sizeof(goto_sym)) == 0);
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.document_symbol_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char c_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        c_path, sizeof(c_path), "rotide-test-c-lsp-fixture-", ".c",
	        "tests/lsp/supported/c/single_file_definition.c"));
	editorOpen(c_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {.path = c_path, .line = 0, .character = 4};
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        cpp_path, sizeof(cpp_path), "rotide-test-cpp-lsp-fixture-", ".cpp",
	        "tests/lsp/supported/cpp/single_file_definition.cpp"));
	editorOpen(cpp_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {.path = cpp_path, .line = 0, .character = 4};
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        html_path, sizeof(html_path), "rotide-test-html-lsp-fixture-", ".html",
	        "tests/lsp/supported/html/single_file_definition.html"));
	editorOpen(html_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 11;

	struct editorLspLocation target = {.path = html_path, .line = 0, .character = 9};
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

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;

	char css_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        css_path, sizeof(css_path), "rotide-test-css-lsp-fixture-", ".css",
	        "tests/lsp/supported/css/single_file_definition.css"));
	editorOpen(css_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 26;

	struct editorLspLocation target = {.path = css_path, .line = 0, .character = 8};
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        json_path, sizeof(json_path), "rotide-test-json-lsp-fixture-", ".json",
	        "tests/lsp/supported/json/single_file_definition.json"));
	editorOpen(json_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JSON, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 11;

	struct editorLspLocation target = {.path = json_path, .line = 1, .character = 3};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	ASSERT_TRUE(unlink(json_path) == 0);
	return 0;
}

static int
test_editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-javascript-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/single_file_definition.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 2;

	struct editorLspLocation target = {.path = js_path, .line = 0, .character = 6};
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
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        jsx_path, sizeof(jsx_path), "rotide-test-javascript-lsp-fixture-", ".jsx",
	        "tests/lsp/supported/javascript/single_file_definition.jsx"));
	editorOpen(jsx_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 5;
	E.cx = 11;

	struct editorLspLocation target = {.path = jsx_path, .line = 0, .character = 9};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);

	ASSERT_TRUE(unlink(jsx_path) == 0);
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

	struct editorLspLocation target = {.path = dst_path, .line = 2, .character = 5};
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

static int
test_editor_process_keypress_goto_definition_cross_file_javascript_fixture_reuses_tab(void) {
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
	ASSERT_TRUE(copy_fixture_to_path(main_path,
	                                 "tests/lsp/supported/javascript/cross_file/main.js"));
	ASSERT_TRUE(copy_fixture_to_path(helper_path,
	                                 "tests/lsp/supported/javascript/cross_file/helper.js"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());
	E.cy = 2;
	E.cx = 2;

	struct editorLspLocation target = {.path = helper_path, .line = 0, .character = 16};
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
	ASSERT_TRUE(
	        copy_fixture_to_path(helper_path, "tests/lsp/supported/cpp/cross_file/helper.cpp"));
	ASSERT_TRUE(
	        copy_fixture_to_path(header_path, "tests/lsp/supported/cpp/cross_file/helper.hpp"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {.path = helper_path, .line = 2, .character = 4};
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
	ASSERT_TRUE(write_temp_go_file(
	        go_path, sizeof(go_path),
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

static int test_editor_process_keypress_goto_definition_c_header_and_implementation_picker(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char dir_template[] = "/tmp/rotide-test-lsp-def-impl-picker-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char main_path[512];
	char header_path[512];
	char impl_path[512];
	ASSERT_TRUE(path_join(main_path, sizeof(main_path), dir_path, "main.cpp"));
	ASSERT_TRUE(path_join(header_path, sizeof(header_path), dir_path, "helper.hpp"));
	ASSERT_TRUE(path_join(impl_path, sizeof(impl_path), dir_path, "helper.cpp"));
	ASSERT_TRUE(
	        write_text_file(main_path, "#include \"helper.hpp\"\nint main() { helper(); }\n"));
	ASSERT_TRUE(write_text_file(header_path, "int helper();\n"));
	ASSERT_TRUE(write_text_file(impl_path, "int helper() { return 42; }\n"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	E.cy = 1;
	E.cx = 13;

	struct editorLspLocation definition = {.path = header_path, .line = 0, .character = 4};
	struct editorLspLocation implementation = {.path = impl_path, .line = 0, .character = 4};
	editorLspTestSetMockDefinitionResponse(1, &definition, 1);
	editorLspTestSetMockImplementationResponse(1, &implementation, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_LSP_LOCATION_MENU, E.popup.kind);
	ASSERT_EQ_INT(2, editorPopupItemCount());
	ASSERT_EQ_STR("Declaration helper.hpp:1", E.popup.items[0].label);
	ASSERT_EQ_STR("Implementation helper.cpp:1", E.popup.items[1].label);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);
	ASSERT_EQ_INT(1, stats.implementation_count);

	char choose_second[] = {'2', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(choose_second,
	                                                      sizeof(choose_second)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_EQ_STR(impl_path, E.filename);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_EQ_STR("Implementation: helper.cpp:1", E.statusmsg);

	ASSERT_TRUE(unlink(main_path) == 0);
	ASSERT_TRUE(unlink(header_path) == 0);
	ASSERT_TRUE(unlink(impl_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_header_only_reports_declaration(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char dir_template[] = "/tmp/rotide-test-lsp-header-only-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char main_path[512];
	char header_path[512];
	ASSERT_TRUE(path_join(main_path, sizeof(main_path), dir_path, "main.cpp"));
	ASSERT_TRUE(path_join(header_path, sizeof(header_path), dir_path, "helper.hpp"));
	ASSERT_TRUE(
	        write_text_file(main_path, "#include \"helper.hpp\"\nint main() { helper(); }\n"));
	ASSERT_TRUE(write_text_file(header_path, "int helper();\n"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	E.cy = 1;
	E.cx = 13;

	struct editorLspLocation definition = {.path = header_path, .line = 0, .character = 4};
	editorLspTestSetMockDefinitionResponse(1, &definition, 1);
	editorLspTestSetMockImplementationResponse(1, NULL, 0);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_EQ_STR(header_path, E.filename);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_EQ_STR("Declaration: helper.hpp:1", E.statusmsg);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);
	ASSERT_EQ_INT(1, stats.implementation_count);

	ASSERT_TRUE(unlink(main_path) == 0);
	ASSERT_TRUE(unlink(header_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_single_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(
	        write_temp_go_file(go_path, sizeof(go_path),
	                           "package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.window_rows = 6;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorLspLocation target = {.path = go_path, .line = 2, .character = 5};
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

static int
test_editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(
	        go_path, sizeof(go_path),
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
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) { return helper(); }\n"));
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

static int
test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled(void) {
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
	ASSERT_TRUE(strstr(E.statusmsg, "Go to definition is available for Go, C, C++, HTML") !=
	            NULL);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.definition_count);
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

static int
test_editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer(void) {
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
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) { return helper(); }\n"));
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

	strncpy(E.lsp_clangd_command, "exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
	        sizeof(E.lsp_clangd_command) - 1);
	E.lsp_clangd_command[sizeof(E.lsp_clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(
	        c_path, sizeof(c_path),
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

static int
test_editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_clangd_command, "exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
	        sizeof(E.lsp_clangd_command) - 1);
	E.lsp_clangd_command[sizeof(E.lsp_clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(
	        c_path, sizeof(c_path),
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

static int
test_editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install(void) {
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
	E.lsp_vscode_langservers_install_command[sizeof(E.lsp_vscode_langservers_install_command) -
	                                         1] = '\0';

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

static int
test_editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task(void) {
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
	E.lsp_vscode_langservers_install_command[sizeof(E.lsp_vscode_langservers_install_command) -
	                                         1] = '\0';

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
	ASSERT_EQ_STR("Task: Install vscode-langservers-extracted",
	              editorActiveBufferDisplayName());
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

static int
test_editor_process_keypress_goto_definition_missing_javascript_server_starts_install_task(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_javascript_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_javascript_command,
	        "exec >/dev/null; sleep 0.05; "
	        "rotide_missing_typescript_language_server_install_command",
	        sizeof(E.lsp_javascript_command) - 1);
	E.lsp_javascript_command[sizeof(E.lsp_javascript_command) - 1] = '\0';
	strncpy(E.lsp_javascript_install_command, "printf 'install ok\\n'",
	        sizeof(E.lsp_javascript_install_command) - 1);
	E.lsp_javascript_install_command[sizeof(E.lsp_javascript_install_command) - 1] = '\0';

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-javascript-lsp-fixture-", ".js",
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

const struct editorTestCase g_lsp_navigation_tests[] = {
        {"editor_lsp_drawer_lists_document_symbols_and_jumps_to_symbol",
         test_editor_lsp_drawer_lists_document_symbols_and_jumps_to_symbol},
        {"editor_lsp_drawer_arrow_previews_symbol_centered_away_from_drawer_cursor",
         test_editor_lsp_drawer_arrow_previews_symbol_centered_away_from_drawer_cursor},
        {"editor_lsp_drawer_renders_nested_symbols_hierarchically",
         test_editor_lsp_drawer_renders_nested_symbols_hierarchically},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location},
        {"editor_process_keypress_alt_i_goto_implementation_jumps_to_target",
         test_editor_process_keypress_alt_i_goto_implementation_jumps_to_target},
        {"editor_process_keypress_alt_s_goto_symbol_jumps_to_first_symbol",
         test_editor_process_keypress_alt_s_goto_symbol_jumps_to_first_symbol},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location_jsx_buffer",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location_jsx_buffer},
        {"editor_process_keypress_goto_definition_cross_file_reuses_tab",
         test_editor_process_keypress_goto_definition_cross_file_reuses_tab},
        {"editor_process_keypress_goto_definition_cross_file_javascript_fixture_reuses_tab",
         test_editor_process_keypress_goto_definition_cross_file_javascript_fixture_reuses_tab},
        {"editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab",
         test_editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab},
        {"editor_process_keypress_goto_definition_multi_picker_selects_choice",
         test_editor_process_keypress_goto_definition_multi_picker_selects_choice},
        {"editor_process_keypress_goto_definition_c_header_and_implementation_picker",
         test_editor_process_keypress_goto_definition_c_header_and_implementation_picker},
        {"editor_process_keypress_goto_definition_header_only_reports_declaration",
         test_editor_process_keypress_goto_definition_header_only_reports_declaration},
        {"editor_process_keypress_mouse_ctrl_click_goto_definition_single_location",
         test_editor_process_keypress_mouse_ctrl_click_goto_definition_single_location},
        {"editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice",
         test_editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice},
        {"editor_process_keypress_goto_definition_timeout_error_and_no_result",
         test_editor_process_keypress_goto_definition_timeout_error_and_no_result},
        {"editor_process_keypress_goto_definition_reports_lsp_disabled",
         test_editor_process_keypress_goto_definition_reports_lsp_disabled},
        {"editor_process_keypress_goto_definition_reports_lsp_disabled_for_c",
         test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_c},
        {"editor_process_keypress_goto_definition_reports_lsp_disabled_for_html",
         test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_html},
        {"editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled",
         test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled},
        {"editor_process_keypress_goto_definition_startup_failure_reports_reason",
         test_editor_process_keypress_goto_definition_startup_failure_reports_reason},
        {"editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer",
         test_editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer},
        {"editor_process_keypress_goto_definition_requires_saved_c_buffer",
         test_editor_process_keypress_goto_definition_requires_saved_c_buffer},
        {"editor_process_keypress_goto_definition_reports_empty_clangd_command",
         test_editor_process_keypress_goto_definition_reports_empty_clangd_command},
        {"editor_process_keypress_goto_definition_reports_empty_html_command",
         test_editor_process_keypress_goto_definition_reports_empty_html_command},
        {"editor_process_keypress_goto_definition_missing_gopls_decline_install",
         test_editor_process_keypress_goto_definition_missing_gopls_decline_install},
        {"editor_process_keypress_goto_definition_missing_gopls_starts_install_task",
         test_editor_process_keypress_goto_definition_missing_gopls_starts_install_task},
        {"editor_process_keypress_goto_definition_missing_clangd_declines_instructions",
         test_editor_process_keypress_goto_definition_missing_clangd_declines_instructions},
        {"editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions",
         test_editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions},
        {"editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install",
         test_editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install},
        {"editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task",
         test_editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task},
        {"editor_process_keypress_goto_definition_missing_javascript_server_starts_install_task",
         test_editor_process_keypress_goto_definition_missing_javascript_server_starts_install_task},
};

const int g_lsp_navigation_test_count =
        (int)(sizeof(g_lsp_navigation_tests) / sizeof(g_lsp_navigation_tests[0]));
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "rotide.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"
#include "workspace/task.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
