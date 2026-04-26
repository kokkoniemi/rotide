#include "test_case.h"
#include "test_support.h"

static int test_editor_refresh_screen_highlights_active_search_match(void) {
	add_row("prefix alpha suffix");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(set_active_search_match(0, 7, 5));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7malpha\x1b[m") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_c_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"txt\"\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_cpp_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-cpp-XXXXXX.cpp";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/cpp/highlight.cpp"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	E.window_rows = 12;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mnamespace\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mclass\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mpublic\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"txt\"\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_cpp_raw_string_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-inject-cpp-XXXXXX.cpp";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/cpp/injections.cpp"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96msection\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_repo_buffer_c_stays_highlighted(void) {
	char *path = testResolveRepoPath("src/editing/buffer_core.c");
	ASSERT_TRUE(path != NULL);

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	int target_row = -1;
	for (int row_idx = 0; row_idx < E.numrows; row_idx++) {
		if (strstr(E.rows[row_idx].chars, "\"Out of memory\"") != NULL) {
			target_row = row_idx;
			break;
		}
	}
	ASSERT_TRUE(target_row >= 0);

	E.window_rows = 8;
	E.window_cols = 120;
	E.rowoff = target_row > 0 ? target_row - 1 : 0;
	E.cy = target_row;
	E.cx = 0;
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, 4));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"Out of memory\"\x1b[39m") != NULL);
	free(output);
	free(path);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/highlight.sh"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_if = 0;
	int has_myfn = 0;
	int has_flag = 0;
	int has_comment = 0;
	int has_pipe = 0;
	ASSERT_TRUE(output != NULL);
	has_if = strstr(output, "\x1b[94mif\x1b[39m") != NULL;
	has_myfn = strstr(output, "\x1b[93mmyfn\x1b[39m") != NULL;
	has_flag = strstr(output, "\x1b[95m-n\x1b[39m") != NULL;
	has_comment = strstr(output, "\x1b[90m# comment\x1b[39m") != NULL;
	has_pipe = strstr(output, "\x1b[97m|\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_if);
	ASSERT_TRUE(has_myfn);
	ASSERT_TRUE(has_flag);
	ASSERT_TRUE(has_comment);
	ASSERT_TRUE(has_pipe);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/highlight.html"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_div = 0;
	int has_class = 0;
	int has_const = 0;
	int has_number = 0;
	int has_document = 0;
	int has_color = 0;
	ASSERT_TRUE(output != NULL);
	has_div = strstr(output, "\x1b[96mdiv\x1b[39m") != NULL;
	has_class = strstr(output, "\x1b[91mclass\x1b[39m") != NULL;
	has_const = strstr(output, "\x1b[94mconst\x1b[39m") != NULL;
	has_number = strstr(output, "\x1b[35m42\x1b[39m") != NULL;
	has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	has_color = strstr(output, "\x1b[95mcolor\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_div);
	ASSERT_TRUE(has_class);
	ASSERT_TRUE(has_const);
	ASSERT_TRUE(has_number);
	ASSERT_TRUE(has_document);
	ASSERT_TRUE(has_color);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_html_text_apostrophe_not_javascript_string(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-html-apostrophe-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/apostrophe.html"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Let's keep this plain text.") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32mLet's keep this plain text.\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m's keep this plain text.") == NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_nested_jsdoc_in_html_script(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-html-jsdoc-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/nested_jsdoc.html"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@returns\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mnumber\x1b[90m}") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mconst\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/highlight.js"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfunction\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_javascript_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-inject-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/injections.js"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 140;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@param\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mnumber\x1b[90m} count") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[97m+\x1b") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96msection\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_jsdoc_highlighting_for_javascript(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-jsdoc-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/jsdoc.js"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@param\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mnumber\x1b[90m} left") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@returns\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mPromise<number>\x1b[90m}") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_typescript_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-ts-XXXXXX.ts";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/typescript/highlight.ts"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfunction\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mnumber\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_jsdoc_highlighting_for_typescript(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-jsdoc-ts-XXXXXX.ts";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/typescript/jsdoc.ts"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@param\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mnumber\x1b[90m} left") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mnumber\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_python_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-py-XXXXXX.py";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/python/highlight.py"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mdef\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_php_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-php-XXXXXX.php";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/php/highlight.php"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfunction\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_php_html_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-inject-php-XXXXXX.php";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/php/injections.php"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96msection\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclass") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_rust_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-rs-XXXXXX.rs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/rust/highlight.rs"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mi32\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_java_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-java-XXXXXX.java";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/java/highlight.java"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mclass\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mMain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_csharp_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-csharp-XXXXXX.cs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/csharp/highlight.cs"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mclass\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mMain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mRun\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_haskell_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-haskell-XXXXXX.hs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/haskell/highlight.hs"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mmodule\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mwhere\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m-- comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mInt\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_haskell_quasiquote_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-inject-haskell-XXXXXX.hs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/haskell/injections.hs"));

	editorOpen(path);
	E.window_rows = 12;
	E.window_cols = 160;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mmodule\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96msection\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclass\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mconst\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mnumber\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"answer\"\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_ruby_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-ruby-XXXXXX.rb";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/ruby/highlight.rb"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mdef\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mend\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_ocaml_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-ocaml-XXXXXX.ml";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/ocaml/highlight.ml"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mlet\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m(* comment *)\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_ejs_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-ejs-XXXXXX.ejs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/ejs/highlight.ejs"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m<%#\x1b[90m greeting \x1b[94m%>") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m<%=\x1b[39m name \x1b[94m%>") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96msection\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclass\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mif\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mconst\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mdocument\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_erb_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-erb-XXXXXX.erb";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/erb/highlight.erb"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m<%#\x1b[90m greeting \x1b[94m%>") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m<%=\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96msection\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclass\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mif\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mupcase\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mconst\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mdocument\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_scala_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-scala-XXXXXX.scala";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 6,
			"tests/syntax/supported/scala/highlight.scala"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mdef\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_julia_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-julia-XXXXXX.jl";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/julia/highlight.jl"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfunction\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_julia_literal_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-inject-julia-XXXXXX.jl";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/julia/injections.jl"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[97m+\x1b") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_regex_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-regex-XXXXXX.regex";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 6,
			"tests/syntax/supported/regex/highlight.regex"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32ma\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mb\x1b[97m-\x1b[95mz\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[97m+\x1b[32mc\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m2\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m3\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_css_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-css-XXXXXX.css";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/css/highlight.css"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mbox\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_go_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-go-XXXXXX.go";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/go/highlight.go"));

	editorOpen(path);
	E.window_rows = 12;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mpackage\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mpayload\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"txt\"\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_javascript_predicates_and_locals(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-js-pred-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/predicates.js"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_document = 0;
	int has_window = 0;
	int has_upper = 0;
	int has_lower = 0;
	ASSERT_TRUE(output != NULL);
	has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	has_window = strstr(output, "\x1b[95mwindow\x1b[39m") != NULL;
	has_upper = strstr(output, "\x1b[96mUpper\x1b[39m") != NULL;
	has_lower = strstr(output, "\x1b[96mlower\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_document);
	ASSERT_TRUE(!has_window);
	ASSERT_TRUE(has_upper);
	ASSERT_TRUE(!has_lower);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_javascript_predicates_repeat_refresh(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-js-repeat-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/repeat_refresh.js"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	editorOutputTestResetFrameCache();
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int first_has_document = 0;
	int first_has_window = 0;
	ASSERT_TRUE(output != NULL);
	first_has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	first_has_window = strstr(output, "\x1b[95mwindow\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(first_has_document);
	ASSERT_TRUE(!first_has_window);

	editorOutputTestResetFrameCache();
	output = refresh_screen_and_capture(&output_len);
	int second_has_document = 0;
	int second_has_window = 0;
	ASSERT_TRUE(output != NULL);
	second_has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	second_has_window = strstr(output, "\x1b[95mwindow\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(second_has_document);
	ASSERT_TRUE(!second_has_window);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_reports_query_budget_throttle_status(void) {
	char path[] = "/tmp/rotide-test-syntax-budget-query-XXXXXX.js";
	int fd = mkstemps(path, 3);
	ASSERT_TRUE(fd != -1);

	size_t source_len = 0;
	char *source = build_repeated_text("const value = document + window;\n", 800, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(write_all(fd, source, source_len) == 0);
	free(source);
	ASSERT_TRUE(close(fd) == 0);

	editorSyntaxTestSetBudgetOverrides(1, 1, 0, 2000000000ULL);
	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter highlight throttled (budget)") != NULL ||
			strstr(E.statusmsg, "Tree-sitter throttled (parse/query budget)") != NULL);

	editorSyntaxTestResetBudgetOverrides();
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_plain_text_file_has_no_syntax_highlighting(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-txt-XXXXXX.txt";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") == NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_shell_selection_and_search_override_syntax_colors(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-priority-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/priority.sh"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(set_active_search_match(0, 19, 2));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_selected_if = 0;
	int has_selected_if_syntax = 0;
	int has_flag = 0;
	ASSERT_TRUE(output != NULL);
	has_selected_if = strstr(output, "\x1b[7mif\x1b[m") != NULL;
	has_selected_if_syntax = strstr(output, "\x1b[7m\x1b[94m") != NULL;
	has_flag = strstr(output, "\x1b[95m-n\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_selected_if);
	ASSERT_TRUE(!has_selected_if_syntax);
	ASSERT_TRUE(has_flag);

	E.selection_mode_active = 0;
	ASSERT_TRUE(set_active_search_match(0, 0, 2));

	editorOutputTestResetFrameCache();
	output = refresh_screen_and_capture(&output_len);
	int has_search_if = 0;
	int has_search_flag = 0;
	ASSERT_TRUE(output != NULL);
	has_search_if = strstr(output, "\x1b[7mif\x1b[m") != NULL;
	has_search_flag = strstr(output, "\x1b[95m-n\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_search_if);
	ASSERT_TRUE(has_search_flag);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_selection_and_search_override_syntax_colors(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-priority-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/priority.c"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 0;
	E.cx = 6;
	ASSERT_TRUE(set_active_search_match(0, 7, 2));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mreturn\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m\x1b[94m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	E.selection_mode_active = 0;
	ASSERT_TRUE(set_active_search_match(0, 0, 6));

	editorOutputTestResetFrameCache();
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mreturn\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_highlight_alignment_with_escaped_controls(void) {
	const char text[] = "A\x1b" "BC";
	add_row_bytes(text, sizeof(text) - 1);
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(set_active_search_match(0, 2, 1));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "A^[\x1b[7mB\x1b[mC") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_escapes_filename_controls(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("bad\x1b[31mname.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "bad^[[31mname.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[31m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_escapes_status_controls(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;
	editorSetStatusMsg("warn:\x1b]52;c;Zm9v\a");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "warn:^[]52;c;Zm9v^G") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]52;c;") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_escapes_file_content_controls(void) {
	const char text[] = "A\x1b[2JB";
	add_row_bytes(text, sizeof(text) - 1);
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "A^[[2JB") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2J") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/a\x1b" "[31m.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/beta.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/gamma.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 3;
	E.window_cols = 50;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "* a") != NULL);
	ASSERT_TRUE(strstr(output, "   >\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "beta.txt") == NULL);
	ASSERT_TRUE(strstr(output, "gamma.txt") == NULL);
	ASSERT_TRUE(strstr(output, ">") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_tab_labels_middle_truncate_at_25_cols(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/aaaaaaaaaaabbbbbbbbbbbccccccccccc");
	ASSERT_TRUE(E.filename != NULL);
	E.window_rows = 3;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "aaaaaaaaaaa...ccccccccccc") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_preview_tab_label_uses_italics(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/sticky.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/preview.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.is_preview = 1;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 3;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3mpreview.txt\x1b[23m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3msticky.txt\x1b[23m") == NULL);
	free(output);
	return 0;
}

static int test_editor_tab_layout_width_includes_right_label_padding(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/ab.txt");
	ASSERT_TRUE(E.filename != NULL);

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(80, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_EQ_INT(1, layout_count);
	ASSERT_EQ_INT(6 + (int)strlen("ab.txt"), layout[0].width_cols);
	return 0;
}

static int test_editor_tabs_align_view_keeps_active_visible_with_variable_widths(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/first_tab_with_a_long_name_001.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/second_tab_with_a_long_name_002.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/third_tab_with_a_long_name_003.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/fourth_tab_with_a_long_name_004.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabSwitchToIndex(3));
	E.window_cols = 46;
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(text_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count >= 1);
	ASSERT_TRUE(E.tab_view_start > 0);

	int active_visible = 0;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 3) {
			active_visible = 1;
			break;
		}
	}
	ASSERT_TRUE(active_visible);
	return 0;
}

static int test_editor_refresh_screen_renders_drawer_entries_and_selection(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	E.pane_focus = EDITOR_PANE_DRAWER;
	E.window_rows = 4;
	E.window_cols = 40;
	add_row("body");
	struct editorDrawerEntryView root_view;
	ASSERT_TRUE(editorDrawerGetVisibleEntry(0, &root_view));
	char expected_root_bold[256];
	ASSERT_TRUE(snprintf(expected_root_bold, sizeof(expected_root_bold),
				"\x1b[1m\x1b[37m%s\x1b[39m\x1b[22m",
				root_view.name) > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_root_bold) != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m \xE2\x96\xBE src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x9C src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94 src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xBE") != NULL);
	ASSERT_TRUE(strstr(output, "src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x80") != NULL);
	ASSERT_TRUE(strstr(output, "child.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x82 body") != NULL);
	free(output);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_hides_selection_marker_when_unfocused(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));

	E.window_rows = 4;
	E.window_cols = 40;
	add_row("body");

	E.pane_focus = EDITOR_PANE_DRAWER;
	size_t focused_len = 0;
	char *focused = refresh_screen_and_capture(&focused_len);
	ASSERT_TRUE(focused != NULL);
	ASSERT_TRUE(strstr(focused, "\x1b[7m \xE2\x96\xB8 src") != NULL);
	free(focused);

	E.pane_focus = EDITOR_PANE_TEXT;
	size_t unfocused_len = 0;
	char *unfocused = refresh_screen_and_capture(&unfocused_len);
	ASSERT_TRUE(unfocused != NULL);
	ASSERT_TRUE(strstr(unfocused, "\xE2\x97\x8F") == NULL);
	ASSERT_TRUE(strstr(unfocused, "\x1b[7m \xE2\x96\xB8 src") == NULL);
	free(unfocused);

	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_active_file_uses_inverted_background(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char active_file[512];
	ASSERT_TRUE(path_join(active_file, sizeof(active_file), env.project_dir, "active.txt"));
	ASSERT_TRUE(write_text_file(active_file, "active\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenFileAsNew(active_file));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	E.pane_focus = EDITOR_PANE_TEXT;
	E.window_rows = 6;
	E.window_cols = 60;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m active.txt") != NULL);
	ASSERT_TRUE(strstr(output, "active.txt\x1b[m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[m\xE2\x94\x82") != NULL);
	free(output);

	ASSERT_TRUE(unlink(active_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_collapsed_renders_expand_indicator(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	add_row("body");
	E.window_rows = 4;
	E.window_cols = 20;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "[>]") != NULL);
	ASSERT_TRUE(strstr(output, "[<]") == NULL);
	ASSERT_EQ_INT(ROTIDE_DRAWER_COLLAPSED_WIDTH, editorDrawerWidthForCols(E.window_cols));
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_renders_unicode_tree_connectors(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char zzz_dir[512];
	char alloc_file[512];
	char rotide_file[512];
	char helpers_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(zzz_dir, sizeof(zzz_dir), env.project_dir, "zzz"));
	ASSERT_TRUE(path_join(alloc_file, sizeof(alloc_file), src_dir, "alloc_test_hooks.c"));
	ASSERT_TRUE(path_join(rotide_file, sizeof(rotide_file), src_dir, "rotide_tests.c"));
	ASSERT_TRUE(path_join(helpers_file, sizeof(helpers_file), src_dir, "test_helpers.c"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(make_dir(zzz_dir));
	ASSERT_TRUE(write_text_file(alloc_file, "a\n"));
	ASSERT_TRUE(write_text_file(rotide_file, "b\n"));
	ASSERT_TRUE(write_text_file(helpers_file, "c\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	E.window_rows = 8;
	E.window_cols = 80;
	(void)editorDrawerSetWidthForCols(40, E.window_cols);
	E.pane_focus = EDITOR_PANE_TEXT;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xBE src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xB8 zzz") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x9C src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94 src") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x9C\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x94\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x80\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "alloc_test_hooks.c") != NULL);
	ASSERT_TRUE(strstr(output, "rotide_tests.c") != NULL);
	ASSERT_TRUE(strstr(output, "test_helpers.c") != NULL);
	free(output);

	ASSERT_TRUE(unlink(alloc_file) == 0);
	ASSERT_TRUE(unlink(rotide_file) == 0);
	ASSERT_TRUE(unlink(helpers_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	ASSERT_TRUE(rmdir(zzz_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_selected_overflow_spills_into_text_area(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char long_dir[512];
	const char *dirname = "drawer_item_with_overflow_tail_segment";
	ASSERT_TRUE(path_join(long_dir, sizeof(long_dir), env.project_dir, dirname));
	ASSERT_TRUE(make_dir(long_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	int long_idx = -1;
	ASSERT_TRUE(find_drawer_entry(dirname, &long_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(long_idx, E.window_rows + 1));

	E.pane_focus = EDITOR_PANE_DRAWER;
	E.window_rows = 4;
	E.window_cols = 60;
	ASSERT_TRUE(editorDrawerSetWidthForCols(12, E.window_cols));
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "overflow_tail_segment") != NULL);

	int highlighted_tail = 0;
	const char *scan = output;
	while ((scan = strstr(scan, "\x1b[7m")) != NULL) {
		const char *normal = strstr(scan + 4, "\x1b[m");
		if (normal == NULL) {
			break;
		}
		const char *tail = strstr(scan, "overflow_tail_segment");
		if (tail != NULL && tail < normal) {
			highlighted_tail = 1;
			break;
		}
		scan = normal + 3;
	}
	ASSERT_TRUE(highlighted_tail);

	free(output);
	ASSERT_TRUE(rmdir(long_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_splitter_spans_editor_rows(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	E.window_rows = 4;
	E.window_cols = 40;
	E.drawer_width_cols = 10;
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);

	const char *separator = "\xE2\x94\x82";
	size_t separator_len = strlen(separator);
	int separator_count = 0;
	const char *cursor = output;
	while ((cursor = strstr(cursor, separator)) != NULL) {
		separator_count++;
		cursor += separator_len;
	}
	ASSERT_EQ_INT(E.window_rows + 1, separator_count);

	free(output);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_cursor_column_offsets_for_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	E.rowoff = 0;
	E.coloff = 0;
	E.pane_focus = EDITOR_PANE_TEXT;

	int expected_col = editorTextBodyStartColForCols(E.window_cols) + 2;
	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[2;%dH", expected_col) > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_hides_cursor_when_drawer_focused(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	E.rowoff = 0;
	E.coloff = 0;
	E.pane_focus = EDITOR_PANE_DRAWER;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") == NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll(void) {
	add_row("line1");
	add_row("line2");
	add_row("line3");
	add_row("line4");
	add_row("line5");
	add_row("line6");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 4;
	E.coloff = 0;
	E.pane_focus = EDITOR_PANE_TEXT;
	editorViewportSetMode(EDITOR_VIEWPORT_FREE_SCROLL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") == NULL);
	free(output);
	return 0;
}

static int test_editor_drawer_layout_clamps_tiny_widths(void) {
	ASSERT_EQ_INT(0, editorDrawerWidthForCols(1));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(1));
	ASSERT_EQ_INT(0, editorTextBodyStartColForCols(1));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(1));
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(2));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(2));
	ASSERT_EQ_INT(1, editorTextBodyStartColForCols(2));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(2));
	ASSERT_EQ_INT(0, editorDrawerSeparatorWidthForCols(2));

	E.drawer_width_cols = 24;
	E.drawer_width_user_set = 0;
	ASSERT_EQ_INT(4, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerSeparatorWidthForCols(10));
	ASSERT_EQ_INT(5, editorDrawerTextViewportCols(10));
	ASSERT_EQ_INT(6, editorTextBodyStartColForCols(10));
	ASSERT_EQ_INT(3, editorTextBodyViewportCols(10));

	ASSERT_TRUE(editorDrawerSetWidthForCols(24, 10));
	ASSERT_EQ_INT(8, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(10));
	ASSERT_EQ_INT(9, editorTextBodyStartColForCols(10));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(10));

	ASSERT_TRUE(editorDrawerSetWidthForCols(3, 10));
	ASSERT_EQ_INT(3, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(6, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerResizeByDeltaForCols(-10, 10));
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(8, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerResizeByDeltaForCols(50, 10));
	ASSERT_EQ_INT(8, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(10));
	return 0;
}

static int test_editor_refresh_screen_contains_expected_sequences(void) {
	add_row("first line");
	add_row("second line");
	E.cy = 1;
	E.cx = 3;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 4;
	E.window_cols = 30;
	E.filename = strdup("sample.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	editorSetStatusMsg("status message");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[?25l") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[6 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "first line") != NULL);
	ASSERT_TRUE(strstr(output, "status message") != NULL);

	free(output);
	return 0;
}

static int test_editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows(void) {
	add_row("alpha");
	add_row("beta");
	add_row("gamma");
	E.window_rows = 4;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;

	editorOutputTestResetFrameCache();
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(E.window_rows, editorOutputTestLastRefreshFileRowDrawCount());

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(0, editorOutputTestLastRefreshFileRowDrawCount());

	E.cy = 1;
	E.cx = 2;
	editorInsertChar('X');
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);

	int changed_rows = editorOutputTestLastRefreshFileRowDrawCount();
	ASSERT_TRUE(changed_rows > 0);
	ASSERT_TRUE(changed_rows < E.window_rows);
	return 0;
}

static int test_editor_refresh_screen_uses_configured_cursor_style(void) {
	add_row("cursor style");
	E.window_rows = 4;
	E.window_cols = 30;

	E.cursor_style = EDITOR_CURSOR_STYLE_BLOCK;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2 q") != NULL);
	free(output);

	E.cursor_style = EDITOR_CURSOR_STYLE_UNDERLINE;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[4 q") != NULL);
	free(output);

	return 0;
}

static int test_editor_refresh_screen_hides_expired_message(void) {
	add_row("line one");
	add_row("line two");
	E.window_rows = 4;
	E.window_cols = 30;
	strncpy(E.statusmsg, "old message", sizeof(E.statusmsg) - 1);
	E.statusmsg[sizeof(E.statusmsg) - 1] = '\0';
	E.statusmsg_time = time(NULL) - 10;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "old message") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_shows_right_overflow_indicator(void) {
	add_row("0123456789abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_shows_left_overflow_indicator(void) {
	add_row("0123456789");
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_shows_both_horizontal_overflow_indicators(void) {
	add_row("0123456789abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators(void) {
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 4;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_out_of_buffer_tildes_are_gray(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m~\x1b[39m") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_updates_horizontal_scroll(void) {
	add_row("01234567890123456789");
	add_row("second");
	E.window_rows = 3;
	E.window_cols = 5;
	E.cy = 0;
	E.cx = 15;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(15, E.rx);
	ASSERT_TRUE(E.coloff > 0);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_slice_after_multibyte_scroll(void) {
	add_row("\xC3\xB6XYZ");
	E.window_rows = 3;
	E.window_cols = 10;
	E.cy = 0;
	E.cx = 2;
	E.coloff = 1;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "XYZ") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_cursor_sequence_not_truncated_by_window_width(void) {
	add_row("x");
	E.window_rows = 3;
	E.window_cols = 1;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2;1H") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_single_row_percent(void) {
	add_row("single");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("single.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_cursor_multibyte_col(void) {
	add_row("\xC3\xB6" "a");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 2;
	E.filename = strdup("multi.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,2    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_cursor_tab_display_col(void) {
	add_row("a\tb");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 2;
	E.filename = strdup("tabs.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,9    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_shows_full_path_when_space_allows(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 110;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("/project/src/modules/editor/very_long_filename.c");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "/project/src/modules/editor/very_long_filename.c") != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 45;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("/very/long/prefix/that/keeps/growing/path/target_file_name.c");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "target_file_name.c") != NULL);
	ASSERT_TRUE(strstr(output, "...") != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

const struct editorTestCase g_render_terminal_tests[] = {
	{"editor_refresh_screen_contains_expected_sequences", test_editor_refresh_screen_contains_expected_sequences},
	{"editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows", test_editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows},
	{"editor_refresh_screen_uses_configured_cursor_style", test_editor_refresh_screen_uses_configured_cursor_style},
	{"editor_refresh_screen_highlights_active_search_match", test_editor_refresh_screen_highlights_active_search_match},
	{"editor_refresh_screen_applies_syntax_highlighting_for_c_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_c_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_cpp_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_cpp_tokens},
	{"editor_refresh_screen_applies_cpp_raw_string_injections", test_editor_refresh_screen_applies_cpp_raw_string_injections},
	{"editor_refresh_screen_repo_buffer_c_stays_highlighted", test_editor_refresh_screen_repo_buffer_c_stays_highlighted},
	{"editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections", test_editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections},
	{"editor_refresh_screen_html_text_apostrophe_not_javascript_string", test_editor_refresh_screen_html_text_apostrophe_not_javascript_string},
	{"editor_refresh_screen_applies_nested_jsdoc_in_html_script", test_editor_refresh_screen_applies_nested_jsdoc_in_html_script},
	{"editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens},
	{"editor_refresh_screen_applies_javascript_injections", test_editor_refresh_screen_applies_javascript_injections},
	{"editor_refresh_screen_applies_jsdoc_highlighting_for_javascript", test_editor_refresh_screen_applies_jsdoc_highlighting_for_javascript},
	{"editor_refresh_screen_applies_syntax_highlighting_for_typescript_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_typescript_tokens},
	{"editor_refresh_screen_applies_jsdoc_highlighting_for_typescript", test_editor_refresh_screen_applies_jsdoc_highlighting_for_typescript},
	{"editor_refresh_screen_applies_syntax_highlighting_for_python_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_python_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_php_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_php_tokens},
	{"editor_refresh_screen_applies_php_html_injections", test_editor_refresh_screen_applies_php_html_injections},
	{"editor_refresh_screen_applies_syntax_highlighting_for_rust_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_rust_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_java_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_java_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_csharp_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_csharp_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_haskell_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_haskell_tokens},
	{"editor_refresh_screen_applies_haskell_quasiquote_injections", test_editor_refresh_screen_applies_haskell_quasiquote_injections},
	{"editor_refresh_screen_applies_syntax_highlighting_for_ruby_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_ruby_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_ocaml_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_ocaml_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_julia_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_julia_tokens},
	{"editor_refresh_screen_applies_julia_literal_injections", test_editor_refresh_screen_applies_julia_literal_injections},
	{"editor_refresh_screen_applies_syntax_highlighting_for_scala_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_scala_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_ejs_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_ejs_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_erb_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_erb_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_regex_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_regex_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_css_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_css_tokens},
	{"editor_refresh_screen_applies_syntax_highlighting_for_go_tokens", test_editor_refresh_screen_applies_syntax_highlighting_for_go_tokens},
	{"editor_refresh_screen_javascript_predicates_and_locals", test_editor_refresh_screen_javascript_predicates_and_locals},
	{"editor_refresh_screen_javascript_predicates_repeat_refresh", test_editor_refresh_screen_javascript_predicates_repeat_refresh},
	{"editor_refresh_screen_reports_query_budget_throttle_status", test_editor_refresh_screen_reports_query_budget_throttle_status},
	{"editor_refresh_screen_plain_text_file_has_no_syntax_highlighting", test_editor_refresh_screen_plain_text_file_has_no_syntax_highlighting},
	{"editor_refresh_screen_selection_and_search_override_syntax_colors", test_editor_refresh_screen_selection_and_search_override_syntax_colors},
	{"editor_refresh_screen_shell_selection_and_search_override_syntax_colors", test_editor_refresh_screen_shell_selection_and_search_override_syntax_colors},
	{"editor_refresh_screen_highlight_alignment_with_escaped_controls", test_editor_refresh_screen_highlight_alignment_with_escaped_controls},
	{"editor_refresh_screen_escapes_filename_controls", test_editor_refresh_screen_escapes_filename_controls},
	{"editor_refresh_screen_escapes_status_controls", test_editor_refresh_screen_escapes_status_controls},
	{"editor_refresh_screen_escapes_file_content_controls", test_editor_refresh_screen_escapes_file_content_controls},
	{"editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels", test_editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels},
	{"editor_refresh_screen_tab_labels_middle_truncate_at_25_cols", test_editor_refresh_screen_tab_labels_middle_truncate_at_25_cols},
	{"editor_refresh_screen_preview_tab_label_uses_italics", test_editor_refresh_screen_preview_tab_label_uses_italics},
	{"editor_tab_layout_width_includes_right_label_padding", test_editor_tab_layout_width_includes_right_label_padding},
	{"editor_tabs_align_view_keeps_active_visible_with_variable_widths", test_editor_tabs_align_view_keeps_active_visible_with_variable_widths},
	{"editor_refresh_screen_renders_drawer_entries_and_selection", test_editor_refresh_screen_renders_drawer_entries_and_selection},
	{"editor_refresh_screen_drawer_hides_selection_marker_when_unfocused", test_editor_refresh_screen_drawer_hides_selection_marker_when_unfocused},
	{"editor_refresh_screen_drawer_active_file_uses_inverted_background", test_editor_refresh_screen_drawer_active_file_uses_inverted_background},
	{"editor_refresh_screen_drawer_collapsed_renders_expand_indicator", test_editor_refresh_screen_drawer_collapsed_renders_expand_indicator},
	{"editor_refresh_screen_drawer_renders_unicode_tree_connectors", test_editor_refresh_screen_drawer_renders_unicode_tree_connectors},
	{"editor_refresh_screen_drawer_selected_overflow_spills_into_text_area", test_editor_refresh_screen_drawer_selected_overflow_spills_into_text_area},
	{"editor_refresh_screen_drawer_splitter_spans_editor_rows", test_editor_refresh_screen_drawer_splitter_spans_editor_rows},
	{"editor_refresh_screen_cursor_column_offsets_for_drawer", test_editor_refresh_screen_cursor_column_offsets_for_drawer},
	{"editor_refresh_screen_hides_cursor_when_drawer_focused", test_editor_refresh_screen_hides_cursor_when_drawer_focused},
	{"editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll", test_editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll},
	{"editor_drawer_layout_clamps_tiny_widths", test_editor_drawer_layout_clamps_tiny_widths},
	{"editor_refresh_screen_hides_expired_message", test_editor_refresh_screen_hides_expired_message},
	{"editor_refresh_screen_shows_right_overflow_indicator", test_editor_refresh_screen_shows_right_overflow_indicator},
	{"editor_refresh_screen_shows_left_overflow_indicator", test_editor_refresh_screen_shows_left_overflow_indicator},
	{"editor_refresh_screen_shows_both_horizontal_overflow_indicators", test_editor_refresh_screen_shows_both_horizontal_overflow_indicators},
	{"editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators", test_editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators},
	{"editor_refresh_screen_out_of_buffer_tildes_are_gray", test_editor_refresh_screen_out_of_buffer_tildes_are_gray},
	{"editor_refresh_screen_updates_horizontal_scroll", test_editor_refresh_screen_updates_horizontal_scroll},
	{"editor_refresh_screen_slice_after_multibyte_scroll", test_editor_refresh_screen_slice_after_multibyte_scroll},
	{"editor_refresh_screen_cursor_sequence_not_truncated_by_window_width", test_editor_refresh_screen_cursor_sequence_not_truncated_by_window_width},
	{"editor_refresh_screen_status_bar_single_row_percent", test_editor_refresh_screen_status_bar_single_row_percent},
	{"editor_refresh_screen_status_bar_cursor_multibyte_col", test_editor_refresh_screen_status_bar_cursor_multibyte_col},
	{"editor_refresh_screen_status_bar_cursor_tab_display_col", test_editor_refresh_screen_status_bar_cursor_tab_display_col},
	{"editor_refresh_screen_status_bar_shows_full_path_when_space_allows", test_editor_refresh_screen_status_bar_shows_full_path_when_space_allows},
	{"editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible", test_editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible},
};

const int g_render_terminal_test_count =
		(int)(sizeof(g_render_terminal_tests) / sizeof(g_render_terminal_tests[0]));
