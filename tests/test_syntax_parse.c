#include "test_case.h"
#include "test_support.h"
#include "test_helpers.h"

static int assert_editor_syntax_parse_failed_event(int expected_detail) {
	ASSERT_TRUE(E.syntax_state != NULL);
	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(E.syntax_state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_PARSE_FAILED, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(expected_detail, event.detail);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/incremental.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
	        .start_cy = 0, .start_cx = 0, .end_cy = 1, .end_cx = 0};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_insert_newline_uses_tree_sitter_block_indent_for_c(void) {
	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path),
	                              "int main() {\n"
	                              "}\n"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	E.auto_indent_enabled = 1;
	E.indent_use_tabs = 0;
	E.indent_width = 4;
	E.dirty = 0;
	E.cy = 0;
	E.cx = editor_test_row_size(0);

	editorInsertNewline();
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "int main() {");
	ASSERT_ROW_TEXT_EQ(1, "    ");
	ASSERT_ROW_TEXT_EQ(2, "}");
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_insert_newline_uses_function_header_indent_for_wrapped_c_args(void) {
	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path),
	                              "static int foo(int lol, int32_t a,\n"
	                              "\t\tint32_t b) {\n"
	                              "\tif (a != b) {\n"
	                              "\t\treturn lol;\n"
	                              "\t}\n"
	                              "}\n"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	E.auto_indent_enabled = 1;
	E.indent_use_tabs = 1;
	E.indent_width = 4;
	E.dirty = 0;
	E.cy = 4;
	E.cx = editor_test_row_size(4);

	editorInsertNewline();
	ASSERT_EQ_INT(7, E.numrows);
	ASSERT_ROW_TEXT_EQ(4, "\t}");
	ASSERT_ROW_TEXT_EQ(5, "\t");
	ASSERT_ROW_TEXT_EQ(6, "}");
	ASSERT_EQ_INT(5, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_insert_newline_uses_tree_sitter_header_indent_for_python(void) {
	char path[512];
	ASSERT_TRUE(write_temp_file_with_suffix(path, sizeof(path), "rotide-test-indent-python-",
	                                        ".py",
	                                        "if True:\n"
	                                        "    pass\n"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());
	E.auto_indent_enabled = 1;
	E.indent_use_tabs = 0;
	E.indent_width = 4;
	E.dirty = 0;
	E.cy = 0;
	E.cx = editor_test_row_size(0);

	editorInsertNewline();
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "if True:");
	ASSERT_ROW_TEXT_EQ(1, "    ");
	ASSERT_ROW_TEXT_EQ(2, "    pass");
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_transient_parse_failure_retries_next_edit(void) {
	char path[] = "/tmp/rotide-test-syntax-retry-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/incremental.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 1;
	E.cx = 1;
	editorSyntaxTestSetParseFailureCountdowns(1, 1);
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, E.syntax_parse_failures);
	ASSERT_TRUE(assert_editor_syntax_parse_failed_event(1) == 0);

	editorSyntaxTestResetParseFailureCountdowns();
	editorInsertChar('y');
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(0, E.syntax_parse_failures);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_deactivates_after_consecutive_parse_failures(void) {
	char path[] = "/tmp/rotide-test-syntax-retry-limit-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/incremental.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 1;
	E.cx = 1;
	editorSyntaxTestSetParseFailureCountdowns(3, 1);

	editorInsertChar('a');
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(1, E.syntax_parse_failures);
	ASSERT_TRUE(assert_editor_syntax_parse_failed_event(1) == 0);

	editorInsertChar('b');
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(2, E.syntax_parse_failures);
	ASSERT_TRUE(assert_editor_syntax_parse_failed_event(2) == 0);

	editorInsertChar('c');
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_EQ_INT(0, E.syntax_parse_failures);

	editorSyntaxTestResetParseFailureCountdowns();
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_cpp_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-cpp-XXXXXX.cpp";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/cpp/incremental.cpp"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
	        .start_cy = 0, .start_cx = 0, .end_cy = 1, .end_cx = 0};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_shell_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-shell-XXXXXX.sh";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/bash/incremental.sh"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
	        .start_cy = 0, .start_cx = 0, .end_cy = 1, .end_cx = 0};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_html_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
	                                       "tests/syntax/supported/html/incremental.html"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 6;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
	        .start_cy = 1, .start_cx = 0, .end_cy = 2, .end_cx = 0};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_javascript_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/javascript/incremental.js"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_typescript_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ts-XXXXXX.ts";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/typescript/incremental.ts"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TYPESCRIPT, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_tsx_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-tsx-XXXXXX.tsx";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/tsx/incremental.tsx"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TSX, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_css_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-css-XXXXXX.css";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/css/incremental.css"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_go_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-go-XXXXXX.go";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/go/incremental.go"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 2;
	E.cx = editor_test_row_size(2);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_python_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-py-XXXXXX.py";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/python/incremental.py"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 4;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_php_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-php-XXXXXX.php";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/php/incremental.php"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PHP, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 4;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_rust_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-rs-XXXXXX.rs";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/rust/incremental.rs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUST, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 4;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_java_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-java-XXXXXX.java";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
	                                       "tests/syntax/supported/java/incremental.java"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVA, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 8;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_csharp_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-csharp-XXXXXX.cs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/csharp/incremental.cs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSHARP, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 8;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_haskell_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-haskell-XXXXXX.hs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/haskell/incremental.hs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HASKELL, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_ruby_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ruby-XXXXXX.rb";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/ruby/incremental.rb"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUBY, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 3;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_ocaml_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ocaml-XXXXXX.ml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/ocaml/incremental.ml"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_OCAML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 3;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_markdown_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-markdown-XXXXXX.md";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/markdown/incremental.md"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_MARKDOWN, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertChar('s');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_toml_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-toml-XXXXXX.toml";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
	                                       "tests/syntax/supported/toml/incremental.toml"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TOML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_yaml_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-yaml-XXXXXX.yaml";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
	                                       "tests/syntax/supported/yaml/incremental.yaml"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_YAML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_xml_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-xml-XXXXXX.xml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/xml/incremental.xml"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_XML, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 11;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_make_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-make-XXXXXX.mk";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/make/incremental.mk"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_MAKE, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 2;
	E.cx = editor_test_row_size(2);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_diff_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-diff-XXXXXX.diff";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
	                                       "tests/syntax/supported/diff/incremental.diff"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_DIFF, editorSyntaxLanguageActive());

	E.cy = 5;
	E.cx = editor_test_row_size(5);
	editorInsertChar('!');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 4;
	E.cx = editor_test_row_size(4);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_julia_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-julia-XXXXXX.jl";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/julia/incremental.jl"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JULIA, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('y');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_scala_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-scala-XXXXXX.scala";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 6,
	                                       "tests/syntax/supported/scala/incremental.scala"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SCALA, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 5;
	editorInsertChar('y');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_ejs_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ejs-XXXXXX.ejs";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/ejs/incremental.ejs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_EJS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 4;
	editorInsertChar(' ');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_erb_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-erb-XXXXXX.erb";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/erb/incremental.erb"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_ERB, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 4;
	editorInsertChar(' ');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_regex_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-regex-XXXXXX.regex";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 6,
	                                       "tests/syntax/supported/regex/incremental.regex"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_REGEX, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 3;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_provider_parse_keeps_tree_valid(void) {
	const char *before = "int value = 1;\n";
	const char *after = "int xvalue = 1;\n";
	struct editorTextSource before_source = {0};
	struct editorTextSource after_source = {0};
	editorTextSourceInitString(&before_source, before, strlen(before));
	editorTextSourceInitString(&after_source, after, strlen(after));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &before_source));

	struct editorSyntaxEdit edit = {.start_byte = 4,
	                                .old_end_byte = 4,
	                                .new_end_byte = 5,
	                                .start_point = {.row = 0, .column = 4},
	                                .old_end_point = {.row = 0, .column = 4},
	                                .new_end_point = {.row = 0, .column = 5}};
	ASSERT_TRUE(editorSyntaxStateApplyEditAndParse(state, &edit, &after_source));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));

	struct editorSyntaxCapture captures[32];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(
	        state, &after_source, 0, (uint32_t)strlen(after), captures,
	        (int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(capture_count > 0);

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_large_file_stays_enabled_in_degraded_mode(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("int value = 1;\n", 600000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len > (size_t)(8 * 1024 * 1024));
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 0, 0);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateConfigureForSourceLength(state, source_len));
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));
	ASSERT_EQ_INT(EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS,
	              editorSyntaxStatePerformanceMode(state));

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

const struct editorTestCase g_syntax_parse_tests[] = {
        {"editor_syntax_incremental_edits_keep_tree_valid",
         test_editor_syntax_incremental_edits_keep_tree_valid},
        {"editor_insert_newline_uses_tree_sitter_block_indent_for_c",
         test_editor_insert_newline_uses_tree_sitter_block_indent_for_c},
        {"editor_insert_newline_uses_function_header_indent_for_wrapped_c_args",
         test_editor_insert_newline_uses_function_header_indent_for_wrapped_c_args},
        {"editor_insert_newline_uses_tree_sitter_header_indent_for_python",
         test_editor_insert_newline_uses_tree_sitter_header_indent_for_python},
        {"editor_syntax_transient_parse_failure_retries_next_edit",
         test_editor_syntax_transient_parse_failure_retries_next_edit},
        {"editor_syntax_deactivates_after_consecutive_parse_failures",
         test_editor_syntax_deactivates_after_consecutive_parse_failures},
        {"editor_syntax_incremental_edits_keep_cpp_tree_valid",
         test_editor_syntax_incremental_edits_keep_cpp_tree_valid},
        {"editor_syntax_incremental_edits_keep_shell_tree_valid",
         test_editor_syntax_incremental_edits_keep_shell_tree_valid},
        {"editor_syntax_incremental_edits_keep_html_tree_valid",
         test_editor_syntax_incremental_edits_keep_html_tree_valid},
        {"editor_syntax_incremental_edits_keep_javascript_tree_valid",
         test_editor_syntax_incremental_edits_keep_javascript_tree_valid},
        {"editor_syntax_incremental_edits_keep_typescript_tree_valid",
         test_editor_syntax_incremental_edits_keep_typescript_tree_valid},
        {"editor_syntax_incremental_edits_keep_tsx_tree_valid",
         test_editor_syntax_incremental_edits_keep_tsx_tree_valid},
        {"editor_syntax_incremental_edits_keep_css_tree_valid",
         test_editor_syntax_incremental_edits_keep_css_tree_valid},
        {"editor_syntax_incremental_edits_keep_go_tree_valid",
         test_editor_syntax_incremental_edits_keep_go_tree_valid},
        {"editor_syntax_incremental_edits_keep_python_tree_valid",
         test_editor_syntax_incremental_edits_keep_python_tree_valid},
        {"editor_syntax_incremental_edits_keep_php_tree_valid",
         test_editor_syntax_incremental_edits_keep_php_tree_valid},
        {"editor_syntax_incremental_edits_keep_rust_tree_valid",
         test_editor_syntax_incremental_edits_keep_rust_tree_valid},
        {"editor_syntax_incremental_edits_keep_java_tree_valid",
         test_editor_syntax_incremental_edits_keep_java_tree_valid},
        {"editor_syntax_incremental_edits_keep_csharp_tree_valid",
         test_editor_syntax_incremental_edits_keep_csharp_tree_valid},
        {"editor_syntax_incremental_edits_keep_haskell_tree_valid",
         test_editor_syntax_incremental_edits_keep_haskell_tree_valid},
        {"editor_syntax_incremental_edits_keep_ruby_tree_valid",
         test_editor_syntax_incremental_edits_keep_ruby_tree_valid},
        {"editor_syntax_incremental_edits_keep_ocaml_tree_valid",
         test_editor_syntax_incremental_edits_keep_ocaml_tree_valid},
        {"editor_syntax_incremental_edits_keep_markdown_tree_valid",
         test_editor_syntax_incremental_edits_keep_markdown_tree_valid},
        {"editor_syntax_incremental_edits_keep_toml_tree_valid",
         test_editor_syntax_incremental_edits_keep_toml_tree_valid},
        {"editor_syntax_incremental_edits_keep_yaml_tree_valid",
         test_editor_syntax_incremental_edits_keep_yaml_tree_valid},
        {"editor_syntax_incremental_edits_keep_xml_tree_valid",
         test_editor_syntax_incremental_edits_keep_xml_tree_valid},
        {"editor_syntax_incremental_edits_keep_make_tree_valid",
         test_editor_syntax_incremental_edits_keep_make_tree_valid},
        {"editor_syntax_incremental_edits_keep_diff_tree_valid",
         test_editor_syntax_incremental_edits_keep_diff_tree_valid},
        {"editor_syntax_incremental_edits_keep_julia_tree_valid",
         test_editor_syntax_incremental_edits_keep_julia_tree_valid},
        {"editor_syntax_incremental_edits_keep_scala_tree_valid",
         test_editor_syntax_incremental_edits_keep_scala_tree_valid},
        {"editor_syntax_incremental_edits_keep_ejs_tree_valid",
         test_editor_syntax_incremental_edits_keep_ejs_tree_valid},
        {"editor_syntax_incremental_edits_keep_erb_tree_valid",
         test_editor_syntax_incremental_edits_keep_erb_tree_valid},
        {"editor_syntax_incremental_edits_keep_regex_tree_valid",
         test_editor_syntax_incremental_edits_keep_regex_tree_valid},
        {"editor_syntax_incremental_provider_parse_keeps_tree_valid",
         test_editor_syntax_incremental_provider_parse_keeps_tree_valid},
        {"editor_syntax_large_file_stays_enabled_in_degraded_mode",
         test_editor_syntax_large_file_stays_enabled_in_degraded_mode},
};

const int g_syntax_parse_test_count =
        (int)(sizeof(g_syntax_parse_tests) / sizeof(g_syntax_parse_tests[0]));
#include "editing/edit.h"
#include "language/syntax.h"
#include "rotide.h"
#include "editing/buffer_core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
