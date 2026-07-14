#include "editing/document_position.h"
#include "editing/jumplist.h"
#include "input/mouse.h"
#include "input/system_vim.h"
#include "language/lsp_protocol.h"
#include "language/lsp_transport.h"
#include "render/popup.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"

#include <sys/types.h>
#include <unistd.h>

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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
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

	char lsp_drawer[] = {' ', 'l'};
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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
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

	char lsp_drawer[] = {' ', 'l'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;
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

	char lsp_drawer[] = {' ', 'l'};
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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-fixture-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	E.cy = 5;
	E.cx = 5;

	struct editorLspLocation target = {.path = go_path, .line = 2, .character = 5};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_vim_gi_goto_implementation_jumps_to_target(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
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

	char goto_impl[] = {'g', 'i'};
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

static int test_editor_process_keypress_vim_gs_goto_symbol_jumps_to_first_symbol(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
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

	char goto_sym[] = {'g', 's'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.css_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(8, E.cx);

	ASSERT_TRUE(unlink(css_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.json_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	ASSERT_TRUE(unlink(json_path) == 0);
	return 0;
}

static int
test_editor_process_keypress_ctrl_o_goto_definition_single_location_javascript_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.javascript_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(6, E.cx);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_jsx_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.javascript_enabled = 1;

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

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);

	ASSERT_TRUE(unlink(jsx_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_cross_file_reuses_tab(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;
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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.javascript_enabled = 1;
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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;
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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;

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

	char input[] = {'g', 'd', '2', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_INT(3, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_c_header_and_implementation_picker(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;
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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;
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

	char goto_def[] = {'g', 'd'};
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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;

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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;

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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {'g', 'd'};

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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("gopls is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_c(void) {
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;

	char c_path[64];
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 0;
	E.cx = 24;

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("clangd is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_html(void) {
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;
	E.lsp_config.html_enabled = 0;

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
	                                 "<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("vscode-html-language-server is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int
test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled(void) {
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
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
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;

	strncpy(E.lsp_config.gopls_command, "true", sizeof(E.lsp_config.gopls_command) - 1);
	E.lsp_config.gopls_command[sizeof(E.lsp_config.gopls_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {'g', 'd'};
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

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("Save this C/C++ buffer before using go to definition", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_empty_clangd_command(void) {
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;
	E.lsp_config.clangd_command[0] = '\0';

	char c_path[64];
	ASSERT_TRUE(
	        write_temp_c_file(c_path, sizeof(c_path), "int main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 0;
	E.cx = 24;

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("LSP disabled: [lsp].clangd_command is empty", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_empty_html_command(void) {
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 1;
	E.lsp_config.html_enabled = 1;
	E.lsp_config.html_command[0] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
	                                 "<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char goto_def[] = {'g', 'd'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("LSP disabled: [lsp].html_command is empty", E.statusmsg);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_gopls_decline_install(void) {
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.gopls_command,
	        "exec >/dev/null; sleep 0.05; rotide_missing_gopls_decline_command",
	        sizeof(E.lsp_config.gopls_command) - 1);
	E.lsp_config.gopls_command[sizeof(E.lsp_config.gopls_command) - 1] = '\0';
	strncpy(E.lsp_config.gopls_install_command, "printf 'install skipped\\n'",
	        sizeof(E.lsp_config.gopls_install_command) - 1);
	E.lsp_config.gopls_install_command[sizeof(E.lsp_config.gopls_install_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char input[] = {'g', 'd', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_STR("gopls not installed", E.statusmsg);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_gopls_starts_install_task(void) {
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.gopls_command,
	        "exec >/dev/null; sleep 0.05; rotide_missing_gopls_install_command",
	        sizeof(E.lsp_config.gopls_command) - 1);
	E.lsp_config.gopls_command[sizeof(E.lsp_config.gopls_command) - 1] = '\0';
	strncpy(E.lsp_config.gopls_install_command, "printf 'install ok\\n'",
	        sizeof(E.lsp_config.gopls_install_command) - 1);
	E.lsp_config.gopls_install_command[sizeof(E.lsp_config.gopls_install_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char input[] = {'g', 'd', 'y', '\r'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.clangd_command,
	        "exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
	        sizeof(E.lsp_config.clangd_command) - 1);
	E.lsp_config.clangd_command[sizeof(E.lsp_config.clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(
	        c_path, sizeof(c_path),
	        "int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 1;
	E.cx = 27;

	char input[] = {'g', 'd', '\r'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.clangd_command,
	        "exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
	        sizeof(E.lsp_config.clangd_command) - 1);
	E.lsp_config.clangd_command[sizeof(E.lsp_config.clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(
	        c_path, sizeof(c_path),
	        "int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 1;
	E.cx = 27;

	char input[] = {'g', 'd', 'y', '\r'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.html_command,
	        "exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_command",
	        sizeof(E.lsp_config.html_command) - 1);
	E.lsp_config.html_command[sizeof(E.lsp_config.html_command) - 1] = '\0';
	strncpy(E.lsp_config.vscode_langservers_install_command, "printf 'install skipped\\n'",
	        sizeof(E.lsp_config.vscode_langservers_install_command) - 1);
	E.lsp_config.vscode_langservers_install_command
	        [sizeof(E.lsp_config.vscode_langservers_install_command) - 1] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
	                                 "<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char input[] = {'g', 'd', '\r'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.html_command,
	        "exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_install_command",
	        sizeof(E.lsp_config.html_command) - 1);
	E.lsp_config.html_command[sizeof(E.lsp_config.html_command) - 1] = '\0';
	strncpy(E.lsp_config.vscode_langservers_install_command, "printf 'install ok\\n'",
	        sizeof(E.lsp_config.vscode_langservers_install_command) - 1);
	E.lsp_config.vscode_langservers_install_command
	        [sizeof(E.lsp_config.vscode_langservers_install_command) - 1] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
	                                 "<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char input[] = {'g', 'd', 'y', '\r'};
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
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.javascript_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_config.javascript_command,
	        "exec >/dev/null; sleep 0.05; "
	        "rotide_missing_typescript_language_server_install_command",
	        sizeof(E.lsp_config.javascript_command) - 1);
	E.lsp_config.javascript_command[sizeof(E.lsp_config.javascript_command) - 1] = '\0';
	strncpy(E.lsp_config.javascript_install_command, "printf 'install ok\\n'",
	        sizeof(E.lsp_config.javascript_install_command) - 1);
	E.lsp_config
	        .javascript_install_command[sizeof(E.lsp_config.javascript_install_command) - 1] =
	        '\0';

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        js_path, sizeof(js_path), "rotide-test-javascript-lsp-fixture-", ".js",
	        "tests/lsp/supported/javascript/single_file_definition.js"));
	editorOpen(js_path);
	E.cy = 2;
	E.cx = 2;

	char input[] = {'g', 'd', 'y', '\r'};
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

static int lsp_nav_vim_key(int key) {
	int effects = 0;
	return editorVimHandleKey(key, &effects);
}

static int test_editor_vim_gr_goto_references_single_opens_drawer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-refs-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);
	E.cy = 5;
	E.cx = 5;

	struct editorLspLocation target = {.path = go_path, .line = 2, .character = 5};
	editorLspTestSetMockReferencesResponse(1, &target, 1);

	editorVimReset();
	(void)lsp_nav_vim_key('g');
	(void)lsp_nav_vim_key('r');

	/* References always open the drawer, even when the result is unique. */
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_INT(5, E.cy);
	ASSERT_EQ_INT(1, E.drawer_usages_count);

	struct editorDrawerEntryView view = {0};
	ASSERT_TRUE(find_drawer_entry_containing("Usages of `helper` (1)", NULL, &view));
	ASSERT_EQ_INT(1, view.depth);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.references_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_vim_gr_goto_references_multiple_populate_drawer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-refs-multi-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);
	E.cy = 5;
	E.cx = 5;

	/* Deliberately out of order to exercise the (path,line,character) sort. */
	struct editorLspLocation targets[2] = {
	        {.path = go_path, .line = 5, .character = 4},
	        {.path = go_path, .line = 2, .character = 5},
	};
	editorLspTestSetMockReferencesResponse(1, targets, 2);

	editorVimReset();
	(void)lsp_nav_vim_key('g');
	(void)lsp_nav_vim_key('r');

	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(2, E.drawer_usages_count);
	ASSERT_TRUE(find_drawer_entry_containing("Usages of `helper` (2)", NULL, NULL));

	int first_idx = -1;
	int second_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing(":3:6", &first_idx, NULL));
	ASSERT_TRUE(find_drawer_entry_containing(":6:5", &second_idx, NULL));
	ASSERT_TRUE(first_idx < second_idx);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_usages_group_navigation_and_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-usages-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	struct editorLspLocation targets[2] = {
	        {.path = go_path, .line = 2, .character = 5},
	        {.path = go_path, .line = 5, .character = 4},
	};
	ASSERT_TRUE(editorDrawerLspShowUsages(targets, 2, "helper"));
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(2, E.drawer_usages_count);

	const char *sel_path = NULL;
	int sel_line = 0;
	int sel_char = 0;
	ASSERT_TRUE(editorDrawerSelectedLspLocation(&sel_path, &sel_line, &sel_char));
	ASSERT_EQ_STR(go_path, sel_path);
	ASSERT_EQ_INT(2, sel_line);
	ASSERT_EQ_INT(5, sel_char);

	int group_idx = -1;
	ASSERT_TRUE(find_drawer_entry_containing("Usages of `helper`", &group_idx, NULL));
	ASSERT_TRUE(editorDrawerCollapseSelection(E.window_rows));
	ASSERT_EQ_INT(group_idx, E.drawer_selected_index);

	int expanded_count = editorDrawerVisibleCount();
	ASSERT_TRUE(editorDrawerCollapseSelection(E.window_rows));
	ASSERT_EQ_INT(expanded_count - 2, editorDrawerVisibleCount());

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_usages_navigation_stable_across_files(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char dir_template[] = "/tmp/rotide-test-go-usages-stable-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	/* Names chosen so a_usage.go sorts before b_usage.go. */
	char origin_path[512];
	char other_path[512];
	ASSERT_TRUE(path_join(origin_path, sizeof(origin_path), dir_path, "a_usage.go"));
	ASSERT_TRUE(path_join(other_path, sizeof(other_path), dir_path, "b_usage.go"));
	ASSERT_TRUE(write_text_file(
	        origin_path, "package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	ASSERT_TRUE(write_text_file(other_path, "package other\n\nvar helper = 0\n"));

	editorOpen(origin_path);
	E.window_rows = 40;
	E.cy = 3;
	E.cx = 13; /* on the helper() call */

	/* The origin file reports a large symbol list; the other file has none.
	 * Previewing a usage in the other file swaps the active buffer and shrinks
	 * the Symbols group, which used to drift the selection when Symbols sat
	 * above the usage rows. */
	struct editorLspSymbol many[25];
	memset(many, 0, sizeof(many));
	for (int i = 0; i < 25; i++) {
		many[i].name = "sym";
		many[i].kind = 12;
		many[i].line = 100 + i;
		many[i].character = 99;
		many[i].parent_index = -1;
		many[i].is_last_sibling = i == 24;
	}
	editorLspTestSetMockDocumentSymbolResponse(1, many, 25);

	struct editorLspLocation targets[2] = {
	        {.path = origin_path, .line = 2, .character = 5},
	        {.path = other_path, .line = 1, .character = 1},
	};
	editorLspTestSetMockReferencesResponse(1, targets, 2);

	editorVimReset();
	(void)lsp_nav_vim_key('g');
	(void)lsp_nav_vim_key('r');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(2, E.drawer_usages_count);

	/* Selection begins on the first usage (origin file). */
	const char *path = NULL;
	int line = 0;
	int character = 0;
	ASSERT_TRUE(editorDrawerSelectedLspLocation(&path, &line, &character));
	ASSERT_EQ_STR(origin_path, path);
	ASSERT_EQ_INT(2, line);
	ASSERT_EQ_INT(5, character);

	/* Arrow down previews the second usage (other file), swapping the Symbols
	 * group out from under the list. The selection must still land on that
	 * usage, not a symbol row. */
	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input_silent(arrow_down, strlen(arrow_down)) == 0);
	ASSERT_TRUE(editorDrawerSelectedLspLocation(&path, &line, &character));
	ASSERT_EQ_STR(other_path, path);
	ASSERT_EQ_INT(1, line);
	ASSERT_EQ_INT(1, character);

	ASSERT_TRUE(unlink(origin_path) == 0);
	ASSERT_TRUE(unlink(other_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_drawer_usages_scrolls_focus_into_view(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-usages-scroll-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);
	E.window_rows = 8;

	/* Force the Usages group below the viewport. */
	struct editorLspSymbol symbols[20];
	memset(symbols, 0, sizeof(symbols));
	for (int i = 0; i < 20; i++) {
		symbols[i].name = "sym";
		symbols[i].kind = 12;
		symbols[i].line = i;
		symbols[i].character = 0;
		symbols[i].parent_index = -1;
		symbols[i].is_last_sibling = i == 19;
	}
	editorLspTestSetMockDocumentSymbolResponse(1, symbols, 20);

	struct editorLspLocation targets[2] = {
	        {.path = go_path, .line = 2, .character = 5},
	        {.path = go_path, .line = 5, .character = 4},
	};
	ASSERT_TRUE(editorDrawerLspShowUsages(targets, 2, "helper"));

	ASSERT_TRUE(E.drawer_selected_index >= E.drawer_rowoff);
	ASSERT_TRUE(E.drawer_selected_index < E.drawer_rowoff + E.window_rows);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_identifier_at_offset_detects_word(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("  helper()");
	E.syntax_language = EDITOR_SYNTAX_GO;

	size_t on_word = 0;
	ASSERT_TRUE(editorBufferPosToOffset(0, 4, &on_word));
	ASSERT_EQ_INT(1, editorIdentifierAtOffset(on_word));

	size_t on_space = 0;
	ASSERT_TRUE(editorBufferPosToOffset(0, 0, &on_space));
	ASSERT_EQ_INT(0, editorIdentifierAtOffset(on_space));

	size_t on_paren = 0;
	ASSERT_TRUE(editorBufferPosToOffset(0, 8, &on_paren));
	ASSERT_EQ_INT(0, editorIdentifierAtOffset(on_paren));

	return 0;
}

static int test_editor_context_menu_show_usages_context_aware(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
	                               "package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);

	size_t on_word = 0;
	ASSERT_TRUE(editorBufferPosToOffset(2, 14, &on_word));
	ASSERT_TRUE(editorOpenEditorContextMenuAt(1, 1, 1, on_word));
	int found_usages = 0;
	for (int i = 0; i < editorPopupItemCount(); i++) {
		if (E.popup.items[i].label != NULL &&
		    strcmp(E.popup.items[i].label, "Show Usages") == 0) {
			found_usages = 1;
		}
	}
	ASSERT_TRUE(found_usages);
	editorPopupClose();

	size_t on_space = 0;
	ASSERT_TRUE(editorBufferPosToOffset(1, 0, &on_space));
	ASSERT_TRUE(editorOpenEditorContextMenuAt(1, 1, 1, on_space));
	found_usages = 0;
	int found_definition = 0;
	for (int i = 0; i < editorPopupItemCount(); i++) {
		if (E.popup.items[i].label == NULL) {
			continue;
		}
		if (strcmp(E.popup.items[i].label, "Show Usages") == 0) {
			found_usages = 1;
		}
		if (strcmp(E.popup.items[i].label, "Go to Definition") == 0) {
			found_definition = 1;
		}
	}
	ASSERT_EQ_INT(0, found_usages);
	ASSERT_EQ_INT(1, found_definition);
	editorPopupClose();

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_vim_k_hover_opens_popup(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-hover-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);
	E.cy = 5;
	E.cx = 5;

	editorLspTestSetMockHoverResponse(1, "func helper()\n\nHelper docs");

	editorVimReset();
	(void)lsp_nav_vim_key('K');
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_LSP_HOVER, E.popup.kind);
	ASSERT_EQ_INT(3, editorPopupItemCount());
	ASSERT_EQ_STR("func helper()", E.popup.items[0].label);
	ASSERT_EQ_STR("", E.popup.items[1].label);
	ASSERT_EQ_STR("Helper docs", E.popup.items[2].label);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.hover_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_vim_k_hover_empty_reports_not_found(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_config.gopls_enabled = 1;
	E.lsp_config.clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(
	        go_path, sizeof(go_path), "rotide-test-go-lsp-hover-empty-", ".go",
	        "tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);
	E.cy = 5;
	E.cx = 5;

	editorLspTestSetMockHoverResponse(1, NULL);

	editorVimReset();
	(void)lsp_nav_vim_key('K');
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_EQ_STR("Hover not found", E.statusmsg);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.hover_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int process_incoming_request_and_read_response(const char *message, char *response,
                                                      size_t response_size) {
	if (response == NULL || response_size < 2) {
		return 0;
	}
	int fds[2];
	if (pipe(fds) != 0) {
		return 0;
	}
	struct editorLspClient client = {
	        .to_server_fd = fds[1],
	        .from_server_fd = -1,
	        .server_kind = EDITOR_LSP_SERVER_TEXLAB,
	};
	int processed = editorLspProcessIncomingMessage(&client, message);
	close(fds[1]);
	ssize_t nread = read(fds[0], response, response_size - 1);
	close(fds[0]);
	if (!processed || nread <= 0) {
		return 0;
	}
	response[nread] = '\0';
	return 1;
}

static int test_editor_lsp_show_document_jumps_and_rejects_external_uris(void) {
	ASSERT_TRUE(editorTabsInit());

	char source_path[64];
	char target_path[64];
	ASSERT_TRUE(write_temp_file_with_suffix(source_path, sizeof(source_path),
	                                        "rotide-test-show-document-source-", ".tex",
	                                        "alpha\norigin\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(target_path, sizeof(target_path),
	                                        "rotide-test-show-document-target-", ".tex",
	                                        "zero\none\nsecond target\n"));
	editorOpen(source_path);
	E.cy = 1;
	E.cx = 2;
	editorJumplistResetActive();

	char *target_uri = NULL;
	ASSERT_TRUE(editorLspBuildFileUri(target_path, &target_uri));
	char message[1024];
	int written = snprintf(message, sizeof(message),
	                       "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"window/showDocument\","
	                       "\"params\":{\"uri\":\"%s\",\"selection\":{\"start\":{\"line\":2,"
	                       "\"character\":6},\"end\":{\"line\":2,\"character\":6}}}}",
	                       target_uri);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(message));

	char response[512];
	ASSERT_TRUE(
	        process_incoming_request_and_read_response(message, response, sizeof(response)));
	ASSERT_EQ_STR(target_path, E.filename);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(6, E.cx);
	ASSERT_EQ_INT(1, editorJumplistActiveCount());
	ASSERT_TRUE(strstr(response, "\"id\":41") != NULL);
	ASSERT_TRUE(strstr(response, "{\"success\":true}") != NULL);

	ASSERT_TRUE(editorTabOpenOrSwitchToFile(source_path));
	written = snprintf(message, sizeof(message),
	                   "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"window/showDocument\","
	                   "\"params\":{\"uri\":\"%s\",\"external\":true}}",
	                   target_uri);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(message));
	ASSERT_TRUE(
	        process_incoming_request_and_read_response(message, response, sizeof(response)));
	ASSERT_EQ_STR(source_path, E.filename);
	ASSERT_TRUE(strstr(response, "{\"success\":false}") != NULL);

	const char *bogus_message =
	        "{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"window/showDocument\","
	        "\"params\":{\"uri\":\"https://example.com/manual.pdf\"}}";
	ASSERT_TRUE(process_incoming_request_and_read_response(bogus_message, response,
	                                                       sizeof(response)));
	ASSERT_EQ_STR(source_path, E.filename);
	ASSERT_TRUE(strstr(response, "{\"success\":false}") != NULL);

	free(target_uri);
	ASSERT_TRUE(unlink(target_path) == 0);
	ASSERT_TRUE(unlink(source_path) == 0);
	return 0;
}

const struct editorTestCase g_lsp_navigation_tests[] = {
        {"editor_lsp_show_document_jumps_and_rejects_external_uris",
         test_editor_lsp_show_document_jumps_and_rejects_external_uris},
        {"editor_vim_gr_goto_references_single_opens_drawer",
         test_editor_vim_gr_goto_references_single_opens_drawer},
        {"editor_vim_gr_goto_references_multiple_populate_drawer",
         test_editor_vim_gr_goto_references_multiple_populate_drawer},
        {"editor_lsp_drawer_usages_group_navigation_and_location",
         test_editor_lsp_drawer_usages_group_navigation_and_location},
        {"editor_lsp_drawer_usages_navigation_stable_across_files",
         test_editor_lsp_drawer_usages_navigation_stable_across_files},
        {"editor_lsp_drawer_usages_scrolls_focus_into_view",
         test_editor_lsp_drawer_usages_scrolls_focus_into_view},
        {"editor_identifier_at_offset_detects_word", test_editor_identifier_at_offset_detects_word},
        {"editor_context_menu_show_usages_context_aware",
         test_editor_context_menu_show_usages_context_aware},
        {"editor_vim_k_hover_opens_popup", test_editor_vim_k_hover_opens_popup},
        {"editor_vim_k_hover_empty_reports_not_found",
         test_editor_vim_k_hover_empty_reports_not_found},
        {"editor_lsp_drawer_lists_document_symbols_and_jumps_to_symbol",
         test_editor_lsp_drawer_lists_document_symbols_and_jumps_to_symbol},
        {"editor_lsp_drawer_arrow_previews_symbol_centered_away_from_drawer_cursor",
         test_editor_lsp_drawer_arrow_previews_symbol_centered_away_from_drawer_cursor},
        {"editor_lsp_drawer_renders_nested_symbols_hierarchically",
         test_editor_lsp_drawer_renders_nested_symbols_hierarchically},
        {"editor_process_keypress_ctrl_o_goto_definition_single_location",
         test_editor_process_keypress_ctrl_o_goto_definition_single_location},
        {"editor_process_keypress_vim_gi_goto_implementation_jumps_to_target",
         test_editor_process_keypress_vim_gi_goto_implementation_jumps_to_target},
        {"editor_process_keypress_vim_gs_goto_symbol_jumps_to_first_symbol",
         test_editor_process_keypress_vim_gs_goto_symbol_jumps_to_first_symbol},
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
