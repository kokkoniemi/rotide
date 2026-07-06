#include "config/theme_config.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "render/screen.h"
#include "render/viewport.h"
#include "rotide.h"
#include "test_case.h"
#include "test_grid_snapshot.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/git_view.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TEST_HEADER_BG "\x1b[48;5;236m"
#define TEST_HEADER_ACTIVE "\x1b[7m"
#define TEST_HEADER_RESET "\x1b[m"
#define TEST_DRAWER_COLLAPSE_SYMBOL "\xE2\x80\xB9"
#define TEST_DRAWER_EXPAND_SYMBOL "\xE2\x80\xBA"
#define TEST_DRAWER_EXPLORER_SYMBOL "E"
#define TEST_DRAWER_FILE_SEARCH_SYMBOL "F"
#define TEST_DRAWER_PROJECT_SEARCH_SYMBOL "/"
#define TEST_DRAWER_LSP_SYMBOL "L"
#define TEST_DRAWER_DAP_SYMBOL "D"
#define TEST_DRAWER_GIT_SYMBOL "\xE2\x91\x82"
#define TEST_DRAWER_MAIN_MENU_SYMBOL "\xE2\x89\xA1"
#define TEST_NERD_FOLDER "\xEF\x81\xBB"
#define TEST_NERD_FOLDER_OPEN "\xEF\x81\xBC"
#define TEST_NERD_FILE_TEXT "\xEF\x85\x9C"
#define TEST_NERD_FILE_CODE "\xEF\x87\x89"
#define TEST_NERD_SEARCH "\xEF\x80\x82"
#define TEST_NERD_BRANCH "\xEF\x84\xA6"
#define TEST_NERD_BARS "\xEF\x83\x89"
#define TEST_NERD_TERMINAL "\xEF\x84\xA0"
#define TEST_DRAWER_COLLAPSE_CELL                                                                  \
	TEST_HEADER_BG " " TEST_DRAWER_COLLAPSE_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_EXPAND_CELL TEST_HEADER_BG " " TEST_DRAWER_EXPAND_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_EXPLORER_CELL                                                                  \
	TEST_HEADER_BG " " TEST_DRAWER_EXPLORER_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_FILE_SEARCH_CELL                                                               \
	TEST_HEADER_BG " " TEST_DRAWER_FILE_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_PROJECT_SEARCH_CELL                                                            \
	TEST_HEADER_BG " " TEST_DRAWER_PROJECT_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_LSP_CELL TEST_HEADER_BG " " TEST_DRAWER_LSP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_DAP_CELL TEST_HEADER_BG " " TEST_DRAWER_DAP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_GIT_CELL TEST_HEADER_BG " " TEST_DRAWER_GIT_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_MAIN_MENU_CELL                                                                 \
	TEST_HEADER_BG " " TEST_DRAWER_MAIN_MENU_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_EXPLORER_CELL                                                           \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_EXPLORER_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_FILE_SEARCH_CELL                                                        \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_FILE_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_PROJECT_SEARCH_CELL                                                     \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_PROJECT_SEARCH_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_LSP_CELL                                                                \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_LSP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_DAP_CELL                                                                \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_DAP_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_GIT_CELL                                                                \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_GIT_SYMBOL " " TEST_HEADER_RESET
#define TEST_DRAWER_ACTIVE_MAIN_MENU_CELL                                                          \
	TEST_HEADER_ACTIVE " " TEST_DRAWER_MAIN_MENU_SYMBOL " " TEST_HEADER_RESET

static int count_substrings(const char *haystack, const char *needle) {
	int count = 0;
	size_t needle_len = strlen(needle);
	if (needle_len == 0) {
		return 0;
	}
	const char *cursor = haystack;
	while ((cursor = strstr(cursor, needle)) != NULL) {
		count++;
		cursor += needle_len;
	}
	return count;
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
	ASSERT_TRUE(strstr(output, "\x1b]12;white\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[5 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	free(output);

	ASSERT_GRID_EQ(
	        /* golden-start */
	        "              │1  first line\n"
	        "              │2  second line\n"
	        "sample.txt [+]     2,4    100%\n"
	        "status message\n"
	        /* golden-end */
	);
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
	ASSERT_TRUE(strstr(output, "\x1b[1 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]112\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;white\a") == NULL);
	free(output);

	E.cursor_style = EDITOR_CURSOR_STYLE_UNDERLINE;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3 q") != NULL);
	free(output);

	E.cursor_style = EDITOR_CURSOR_STYLE_BAR;
	E.cursor_blink_enabled = 0;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[6 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;white\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]112\a") == NULL);
	free(output);

	E.cursor_style = EDITOR_CURSOR_STYLE_BLOCK;
	output = refresh_screen_and_capture(&output_len);
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
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

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

static int test_editor_refresh_screen_applies_modus_operandi_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-modus-operandi-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "modus-operandi"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:00/00/00\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;255;255;255m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;0;95;95mint") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;114;16;69mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;89;89;89m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;218;229;236m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_github_light_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-github-light-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "github-light"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:1f/23/28\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;255;255;255m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;26;127;55mint") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;149;56;0ms") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;130;80;223mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;87;96;106m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;244;246;248m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_github_dark_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-github-dark-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "github-dark"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:e6/ed/f3\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;13;17;23m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;126;231;135mint") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;255;166;87ms") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;210;168;255mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;139;148;158m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;23;28;35m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_acme_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-acme-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "acme"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:00/00/00\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;255;255;234m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;80;80;80m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;175;95;0mreturn") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;252;252;206m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_silentium_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-silentium-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "silentium"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:e6/e6/e6\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;20;20;20m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;255;126;107mreturn") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;166;166;166m\"txt\"") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;115;115;115m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;255;126;107mmain") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;40;40;40m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_molokai_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-molokai-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "molokai"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:f8/f8/f2\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;27;29;30m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;102;217;239mint") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;166;226;46mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;126;142;145m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;230;219;116m\"txt\"") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;249;38;114mreturn") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;174;129;255m42") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;41;55;57m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_kanagawa_wave_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-kanagawa-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "kanagawa-wave"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:dc/d7/ba\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;31;31;40m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;122;168;159mint") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;126;156;216mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;114;113;105m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;152;187;108m\"txt\"") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;149;127;184mreturn") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;210;126;153m42") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;42;42;55m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_custom_theme_roles(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-custom-theme-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	E.theme.syntax[EDITOR_SYNTAX_HL_STRING] = editorThemeRgbColor(0x01, 0x02, 0x03);
	E.theme.ui[EDITOR_THEME_UI_LINE_NUMBER] = editorThemeRgbColor(0x04, 0x05, 0x06);
	E.theme.ui[EDITOR_THEME_UI_CURRENT_LINE_BG] = editorThemeRgbColor(0x07, 0x08, 0x09);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;1;2;3m\"txt\"") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;4;5;6m1 ") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;7;8;9m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_cpp_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-cpp-XXXXXX.cpp";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/cpp/highlight.cpp"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/cpp/injections.cpp"));

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
		char *_row = editor_test_row_text(row_idx);
		if (_row != NULL && strstr(_row, "\"Out of memory\"") != NULL) {
			target_row = row_idx;
			free(_row);
			break;
		}
		free(_row);
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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/bash/highlight.sh"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/html/highlight.html"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/html/apostrophe.html"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/javascript/jsdoc.js"));

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

static int test_editor_refresh_screen_applies_syntax_highlighting_for_tsx_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-tsx-XXXXXX.tsx";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/tsx/highlight.tsx"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TSX, editorSyntaxLanguageActive());
	E.window_rows = 14;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94minterface\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mstring\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mdiv\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mspan\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclassName") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mdata-active") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mtitle") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[37mactive") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[37mlabel") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_jsdoc_highlighting_for_tsx(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-jsdoc-tsx-XXXXXX.tsx";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4, "tests/syntax/supported/tsx/jsdoc.tsx"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@param\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mstring\x1b[90m} name") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m * \x1b[94m@returns\x1b[90m") != NULL);
	ASSERT_TRUE(strstr(output, "{\x1b[96mJSX.Element\x1b[90m}") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_jsdoc_highlighting_for_typescript(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-jsdoc-ts-XXXXXX.ts";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/typescript/jsdoc.ts"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/python/highlight.py"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/php/highlight.php"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/php/injections.php"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/rust/highlight.rs"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/java/highlight.java"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/csharp/highlight.cs"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/haskell/highlight.hs"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/ruby/highlight.rb"));

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

static int test_editor_refresh_screen_bolds_matching_bracket_under_cursor(void) {
	char path[] = "/tmp/rotide-test-bracket-highlight-XXXXXX.c";
	int fd = mkstemps(path, 2);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(write_text_file(path, "int main(void) { return 0; }\n"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 15; /* on the '{' */

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[1m{\x1b[22m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[1m}\x1b[22m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_no_bracket_bold_inside_comment(void) {
	char path[] = "/tmp/rotide-test-bracket-comment-XXXXXX.c";
	int fd = mkstemps(path, 2);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(write_text_file(path, "int x; /* (a) */\n"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 10; /* on the '(' inside the comment */

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[1m(\x1b[22m") == NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_ocaml_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-ocaml-XXXXXX.ml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/ocaml/highlight.ml"));

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

static int test_editor_refresh_screen_applies_syntax_highlighting_for_julia_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-julia-XXXXXX.jl";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/julia/highlight.jl"));

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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/julia/injections.jl"));

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

static int test_editor_refresh_screen_applies_syntax_highlighting_for_markdown_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-markdown-XXXXXX.md";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/markdown/highlight.md"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* Upstream highlights captures only the (inline) inside an atx_heading
	 * via @text.title -> @keyword (theme bright blue, ANSI \x1b[94m). */
	ASSERT_TRUE(strstr(output, "\x1b[94mHeading\x1b[39m") != NULL);
	/* code_span (the injected markdown_inline grammar's match for the
	 * `inline code` span) is highlighted via @text.literal -> @string
	 * (theme green, ANSI \x1b[32m). The code_span_delimiter backticks
	 * render as @punctuation.delimiter (default color) and over-paint the
	 * span ends, so we assert the green escape appears around the inner
	 * span text. This proves block-to-inline injection routes through. */
	ASSERT_TRUE(strstr(output, "\x1b[32minline code") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_toml_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-toml-XXXXXX.toml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/toml/highlight.toml"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mtitle ") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"RotIDE\"\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95m true\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mtheme\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_yaml_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-yaml-XXXXXX.yaml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/yaml/highlight.yaml"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"RotIDE\"\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mtrue\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_xml_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-xml-XXXXXX.xml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/xml/highlight.xml"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mxml\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m<!-- catalog comment -->\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mcatalog\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_make_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-make-XXXXXX.mk";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/make/highlight.mk"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32mCC") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mall") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94minclude\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32mconfig.mk\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_tints_and_highlights_git_diff_tab(void) {
	const char *patch = "diff --git a/src/app.c b/src/app.c\n"
	                    "index 1111111..2222222 100644\n"
	                    "--- a/src/app.c\n"
	                    "+++ b/src/app.c\n"
	                    "@@ -1,3 +1,3 @@\n"
	                    "-int old_value = 1;\n"
	                    "+int new_value = 2;\n"
	                    " int kept_value = 3;\n";

	unsigned char *kinds = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text =
	        editorGitViewBuildDiffDup(patch, strlen(patch), &kinds, &kind_count, &source_path);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, "git diff: src/app.c", text));
	free(text);
	free(E.git_view_line_kinds);
	E.git_view_line_kinds = kinds;
	E.git_view_line_kind_count = kind_count;
	free(E.git_view_source_path);
	E.git_view_source_path = source_path;
	ASSERT_TRUE(editorSyntaxParseFullActive());
	E.window_rows = 10;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* Added/removed lines carry the default theme's 256-color tints, and the
	 * stripped content is highlighted as C (primitive type `int`). */
	ASSERT_TRUE(strstr(output, "\x1b[48;5;22m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;5;52m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint") != NULL);
	ASSERT_TRUE(strstr(output, "old line") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_markdown_list_code_spans_stay_highlighted(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-markdown-list-code-XXXXXX.md";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/markdown/list_code.md"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32mmake") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32mmake test") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_markdown_code_fence_injection(void) {
	char path[] = "/tmp/rotide-test-syntax-inject-markdown-XXXXXX.md";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/markdown/injections.md"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* The whole (fenced_code_block) is captured as @text.literal -> @string
	 * (theme green, ANSI \x1b[32m); the inner (language) node inherits it,
	 * so the language tag shows up in green. */
	ASSERT_TRUE(strstr(output, "\x1b[32mpython") != NULL);
	/* (code_fence_content) is @none in upstream, deferring to the python
	 * injection. The integer literals inside render as @number (theme
	 * magenta, ANSI \x1b[35m), proving the python parser ran on the
	 * code-fence content. The host @text.literal green resumes after each
	 * injected token, so we only assert the opening number escape. */
	ASSERT_TRUE(strstr(output, "\x1b[35m1") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m2") != NULL);
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

static int test_editor_refresh_screen_applies_syntax_highlighting_for_ejs_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-ejs-XXXXXX.ejs";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/ejs/highlight.ejs"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m<%#\x1b[90m greeting \x1b[94m%>") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m<%=\x1b[39m \x1b[37mname\x1b[39m \x1b[94m%>") != NULL);
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
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/erb/highlight.erb"));

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

static int test_editor_refresh_screen_applies_syntax_highlighting_for_kotlin_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-kotlin-XXXXXX.kt";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 3, "tests/syntax/supported/kotlin/highlight.kt"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mval") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfun") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"world\"") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_svelte_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-svelte-XXXXXX.svelte";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 7,
	                                       "tests/syntax/supported/svelte/highlight.svelte"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* HTML markup Svelte extends: tag name and attribute name. */
	ASSERT_TRUE(strstr(output, "\x1b[96mh1\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclass\x1b[39m") != NULL);
	/* Svelte-specific control-flow keyword. */
	ASSERT_TRUE(strstr(output, "\x1b[94mif\x1b[39m") != NULL);
	/* JavaScript injected into <script>. */
	ASSERT_TRUE(strstr(output, "\x1b[94mconst\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	/* CSS injected into <style>. */
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_vue_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-vue-XXXXXX.vue";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/vue/highlight.vue"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	/* HTML markup Vue extends: tag name and attribute name. */
	ASSERT_TRUE(strstr(output, "\x1b[96mh1\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[91mclass\x1b[39m") != NULL);
	/* Vue-specific directive name. */
	ASSERT_TRUE(strstr(output, "\x1b[94mv-if\x1b[39m") != NULL);
	/* JavaScript injected into <script>. */
	ASSERT_TRUE(strstr(output, "\x1b[94mconst\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	/* CSS injected into <style>. */
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_glsl_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-glsl-XXXXXX.glsl";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/glsl/highlight.glsl"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96muniform") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mfloat") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mvoid") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mgl_Position") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m1.0\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_lua_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-lua-XXXXXX.lua";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/lua/highlight.lua"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m-- comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mlocal") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mif") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"positive\"\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_hcl_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-hcl-XXXXXX.hcl";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/hcl/highlight.hcl"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m# comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mvariable") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m3\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_helm_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-helm-XXXXXX.tpl";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/helm/highlight.helm"));

	editorOpen(path);
	E.window_rows = 12;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m{{/*") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95m.Values") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mquote") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m3\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mif") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_bibtex_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-bibtex-XXXXXX.bib";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/bibtex/highlight.bib"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m@comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m@string") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mTOG") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m@article") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m1984") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_latex_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-latex-XXXXXX.tex";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/latex/highlight.tex"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m% comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m\\documentclass\x1b[32m{article}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m\\usepackage\x1b[32m{amsmath}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m\\section\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93m\\label\x1b[95m{sec:intro}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93m\\ref\x1b[95m{sec:intro}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93m\\cite\x1b[95m{knuth84}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m\\begin\x1b[96m{equation}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94m\\end\x1b[96m{equation}\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[97m^\x1b[37m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_css_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-css-XXXXXX.css";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 4, "tests/syntax/supported/css/highlight.css"));

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
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3, "tests/syntax/supported/go/highlight.go"));

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
	ASSERT_TRUE(strstr(output, "\x1b[37mmain\x1b[39m") != NULL);
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
	ASSERT_TRUE(write_fixture_to_temp_path(
	        path, 3, "tests/syntax/supported/javascript/repeat_refresh.js"));

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
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4, "tests/syntax/supported/c/activation.c"));

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

static int test_editor_refresh_screen_selection_and_search_override_syntax_colors(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-priority-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/priority.c"));

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

static int test_editor_refresh_screen_shell_selection_and_search_override_syntax_colors(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-priority-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3, "tests/syntax/supported/bash/priority.sh"));

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

static int test_editor_refresh_screen_highlight_alignment_with_escaped_controls(void) {
	const char text[] = "A\x1b"
	                    "BC";
	add_row_bytes(text, sizeof(text) - 1);
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(set_active_search_match(0, 2, 1));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "A^[\x1b[7mB\x1b[m" TEST_HEADER_BG "C") != NULL);
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

static int test_editor_line_number_gutter_width_and_absolute_numbers(void) {
	for (int i = 0; i < 12; i++) {
		add_row("x");
	}
	E.window_rows = 12;
	E.window_cols = 40;
	E.line_numbers_enabled = 1;
	E.current_line_highlight_enabled = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	ASSERT_EQ_INT(3, editorLineNumberGutterColsForCols(E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m 1 \x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m12 \x1b[39m") != NULL);
	free(output);
	return 0;
}

static int test_editor_line_numbers_disabled_removes_gutter(void) {
	add_row("alpha");
	E.window_rows = 3;
	E.window_cols = 24;
	E.line_numbers_enabled = 0;
	E.current_line_highlight_enabled = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	ASSERT_EQ_INT(0, editorLineNumberGutterColsForCols(E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "alpha") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m1 \x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_highlights_current_line(void) {
	add_row("first");
	add_row("second");
	E.window_rows = 3;
	E.window_cols = 30;
	E.cy = 1;
	E.cx = 0;
	E.current_line_highlight_enabled = 1;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(2, count_substrings(output, TEST_HEADER_BG));
	ASSERT_TRUE(strstr(output, "second") != NULL);
	free(output);

	E.current_line_highlight_enabled = 0;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(1, count_substrings(output, TEST_HEADER_BG));
	free(output);
	return 0;
}

static int test_editor_viewport_center_cursor_centers_target_row(void) {
	for (int i = 0; i < 100; i++) {
		add_row("line");
	}
	E.window_rows = 11;
	E.window_cols = 40;
	E.cy = 50;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.line_wrap_enabled = 0;

	editorViewportCenterCursor();

	ASSERT_EQ_INT(50 - 11 / 2, E.rowoff);
	return 0;
}

static int test_editor_viewport_center_cursor_clamps_near_top(void) {
	for (int i = 0; i < 20; i++) {
		add_row("line");
	}
	E.window_rows = 11;
	E.window_cols = 40;
	E.cy = 1;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.line_wrap_enabled = 0;

	editorViewportCenterCursor();

	ASSERT_EQ_INT(0, E.rowoff);
	return 0;
}

static int
test_editor_refresh_screen_current_line_highlight_visible_when_drawer_focus_previewing(void) {
	add_row("first");
	add_row("second");
	E.window_rows = 3;
	E.window_cols = 30;
	E.cy = 1;
	E.cx = 0;
	E.current_line_highlight_enabled = 1;
	E.is_preview = 1;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(2, count_substrings(output, TEST_HEADER_BG));
	free(output);

	E.is_preview = 0;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(1, count_substrings(output, TEST_HEADER_BG));
	free(output);

	E.filename = strdup("/tmp/current-line-highlight-lsp.c");
	ASSERT_TRUE(E.filename != NULL);
	E.drawer_mode = EDITOR_DRAWER_MODE_LSP;
	E.drawer_lsp_expanded = 3;
	E.drawer_selected_index = 4;
	E.lsp_symbols = calloc(1, sizeof(*E.lsp_symbols));
	ASSERT_TRUE(E.lsp_symbols != NULL);
	E.lsp_symbol_count = 1;
	E.lsp_symbols[0].name = strdup("second");
	ASSERT_TRUE(E.lsp_symbols[0].name != NULL);
	E.lsp_symbols[0].kind = 12;
	E.lsp_symbols[0].line = 1;
	E.lsp_symbols[0].character = 0;
	E.lsp_symbols[0].parent_index = -1;
	E.lsp_symbols[0].is_last_sibling = 1;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(2, count_substrings(output, TEST_HEADER_BG));
	free(output);
	return 0;
}

static int test_editor_refresh_screen_current_line_highlight_continues_after_selection(void) {
	add_row("prefix alpha suffix");
	E.window_rows = 3;
	E.window_cols = 50;
	E.cy = 0;
	E.cx = 12;
	E.current_line_highlight_enabled = 1;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 7));
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7malpha\x1b[m\x1b[48;5;236m suffix") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_wrap_continuation_does_not_repeat_line_number(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 14;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 1;
	E.current_line_highlight_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(1, count_substrings(output, "\x1b[90m1 \x1b[39m"));
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39m") != NULL);
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

static int test_editor_refresh_screen_wraps_long_line_with_continuation_marker(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.coloff = 4;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, E.coloff);
	ASSERT_TRUE(strstr(output, "abcdef") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39mghijkl") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39mmn") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_wrap_exact_width_has_no_continuation_marker(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "abcdef") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x86\xB3") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_wrap_cursor_uses_visual_segment(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.cy = 0;
	E.cx = 8;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int expected_col = editorTextBodyStartColForCols(E.window_cols) + 3;
	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[3;%dH", expected_col) >
	            0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_wrap_prefers_punctuation_breaks(void) {
	add_row("    alpha.beta/gamma(delta)");
	E.window_rows = 5;
	E.window_cols = 18;
	E.line_wrap_enabled = 1;
	E.rowoff = 0;
	E.wrapoff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "    alpha.") != NULL);
	ASSERT_TRUE(strstr(output, "alpha.beta") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39m    beta/") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_wrap_cursor_honors_continuation_indent(void) {
	add_row("    alpha.beta/gamma(delta)");
	E.window_rows = 5;
	E.window_cols = 18;
	E.line_wrap_enabled = 1;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.cy = 0;
	E.cx = 11;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int expected_col = editorTextBodyStartColForCols(E.window_cols) + 6;
	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[3;%dH", expected_col) >
	            0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_wrap_handles_resize_after_render(void) {
	add_row("abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 8;
	E.window_cols = 12;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);

	E.window_cols = 20;
	(void)editorDrawerSetWidthForCols(1, E.window_cols);

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "abcdefghijklmnop") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39mqrstuvwxyz") != NULL);
	free(output);

	E.window_cols = 12;
	(void)editorDrawerSetWidthForCols(1, E.window_cols);

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "abcdefgh") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39mijklmnop") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\xB3\x1b[39mqrstuvwx") != NULL);
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
	E.line_numbers_enabled = 0;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	editorViewportSetMode(EDITOR_VIEWPORT_FREE_SCROLL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_applies_a11y_dark_truecolor_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-a11y-dark-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "a11y-dark"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;rgb:f8/f8/f2\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;102;221;236mint") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;255;215;0mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;212;208;171m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;2;58;58;58m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;43;43;43m\x1b[48;2;248;248;242m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_256noir_theme(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-256noir-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "256noir"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]12;white\a") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;5;16m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;5;255mint") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;5;255mmain") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;5;245m\"txt\"") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;5;240m// comment") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;5;196m42") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[48;5;233m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_a11y_selection_overrides_syntax(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-a11y-selection-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	ASSERT_TRUE(editorThemeInitBuiltin(&E.theme, "a11y-dark"));
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 3;
	E.cx = 0;
	ASSERT_TRUE(set_active_search_match(3, 1, 6));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;43;43;43m\x1b[48;2;255;215;0mreturn") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[38;2;107;190;255mreturn") == NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

const struct editorTestCase g_render_frame_tests[] = {
        {"editor_refresh_screen_contains_expected_sequences",
         test_editor_refresh_screen_contains_expected_sequences},
        {"editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows",
         test_editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows},
        {"editor_refresh_screen_uses_configured_cursor_style",
         test_editor_refresh_screen_uses_configured_cursor_style},
        {"editor_refresh_screen_highlights_active_search_match",
         test_editor_refresh_screen_highlights_active_search_match},
        {"editor_refresh_screen_applies_syntax_highlighting_for_c_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_c_tokens},
        {"editor_refresh_screen_bolds_matching_bracket_under_cursor",
         test_editor_refresh_screen_bolds_matching_bracket_under_cursor},
        {"editor_refresh_screen_no_bracket_bold_inside_comment",
         test_editor_refresh_screen_no_bracket_bold_inside_comment},
        {"editor_refresh_screen_applies_modus_operandi_theme",
         test_editor_refresh_screen_applies_modus_operandi_theme},
        {"editor_refresh_screen_applies_github_light_theme",
         test_editor_refresh_screen_applies_github_light_theme},
        {"editor_refresh_screen_applies_github_dark_theme",
         test_editor_refresh_screen_applies_github_dark_theme},
        {"editor_refresh_screen_applies_acme_theme", test_editor_refresh_screen_applies_acme_theme},
        {"editor_refresh_screen_applies_silentium_theme",
         test_editor_refresh_screen_applies_silentium_theme},
        {"editor_refresh_screen_applies_molokai_theme",
         test_editor_refresh_screen_applies_molokai_theme},
        {"editor_refresh_screen_applies_kanagawa_wave_theme",
         test_editor_refresh_screen_applies_kanagawa_wave_theme},
        {"editor_refresh_screen_applies_custom_theme_roles",
         test_editor_refresh_screen_applies_custom_theme_roles},
        {"editor_refresh_screen_applies_syntax_highlighting_for_cpp_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_cpp_tokens},
        {"editor_refresh_screen_applies_cpp_raw_string_injections",
         test_editor_refresh_screen_applies_cpp_raw_string_injections},
        {"editor_refresh_screen_repo_buffer_c_stays_highlighted",
         test_editor_refresh_screen_repo_buffer_c_stays_highlighted},
        {"editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections",
         test_editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections},
        {"editor_refresh_screen_html_text_apostrophe_not_javascript_string",
         test_editor_refresh_screen_html_text_apostrophe_not_javascript_string},
        {"editor_refresh_screen_applies_nested_jsdoc_in_html_script",
         test_editor_refresh_screen_applies_nested_jsdoc_in_html_script},
        {"editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens},
        {"editor_refresh_screen_applies_javascript_injections",
         test_editor_refresh_screen_applies_javascript_injections},
        {"editor_refresh_screen_applies_jsdoc_highlighting_for_javascript",
         test_editor_refresh_screen_applies_jsdoc_highlighting_for_javascript},
        {"editor_refresh_screen_applies_syntax_highlighting_for_typescript_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_typescript_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_tsx_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_tsx_tokens},
        {"editor_refresh_screen_applies_jsdoc_highlighting_for_tsx",
         test_editor_refresh_screen_applies_jsdoc_highlighting_for_tsx},
        {"editor_refresh_screen_applies_jsdoc_highlighting_for_typescript",
         test_editor_refresh_screen_applies_jsdoc_highlighting_for_typescript},
        {"editor_refresh_screen_applies_syntax_highlighting_for_python_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_python_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_php_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_php_tokens},
        {"editor_refresh_screen_applies_php_html_injections",
         test_editor_refresh_screen_applies_php_html_injections},
        {"editor_refresh_screen_applies_syntax_highlighting_for_rust_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_rust_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_java_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_java_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_csharp_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_csharp_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_haskell_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_haskell_tokens},
        {"editor_refresh_screen_applies_haskell_quasiquote_injections",
         test_editor_refresh_screen_applies_haskell_quasiquote_injections},
        {"editor_refresh_screen_applies_syntax_highlighting_for_ruby_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_ruby_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_ocaml_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_ocaml_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_julia_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_julia_tokens},
        {"editor_refresh_screen_applies_julia_literal_injections",
         test_editor_refresh_screen_applies_julia_literal_injections},
        {"editor_refresh_screen_applies_syntax_highlighting_for_markdown_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_markdown_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_toml_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_toml_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_yaml_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_yaml_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_xml_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_xml_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_make_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_make_tokens},
        {"editor_refresh_screen_tints_and_highlights_git_diff_tab",
         test_editor_refresh_screen_tints_and_highlights_git_diff_tab},
        {"editor_refresh_screen_markdown_list_code_spans_stay_highlighted",
         test_editor_refresh_screen_markdown_list_code_spans_stay_highlighted},
        {"editor_refresh_screen_applies_markdown_code_fence_injection",
         test_editor_refresh_screen_applies_markdown_code_fence_injection},
        {"editor_refresh_screen_applies_syntax_highlighting_for_scala_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_scala_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_ejs_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_ejs_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_erb_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_erb_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_regex_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_regex_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_latex_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_latex_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_bibtex_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_bibtex_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_hcl_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_hcl_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_helm_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_helm_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_lua_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_lua_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_glsl_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_glsl_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_kotlin_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_kotlin_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_svelte_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_svelte_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_vue_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_vue_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_css_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_css_tokens},
        {"editor_refresh_screen_applies_syntax_highlighting_for_go_tokens",
         test_editor_refresh_screen_applies_syntax_highlighting_for_go_tokens},
        {"editor_refresh_screen_javascript_predicates_and_locals",
         test_editor_refresh_screen_javascript_predicates_and_locals},
        {"editor_refresh_screen_javascript_predicates_repeat_refresh",
         test_editor_refresh_screen_javascript_predicates_repeat_refresh},
        {"editor_refresh_screen_reports_query_budget_throttle_status",
         test_editor_refresh_screen_reports_query_budget_throttle_status},
        {"editor_refresh_screen_plain_text_file_has_no_syntax_highlighting",
         test_editor_refresh_screen_plain_text_file_has_no_syntax_highlighting},
        {"editor_refresh_screen_selection_and_search_override_syntax_colors",
         test_editor_refresh_screen_selection_and_search_override_syntax_colors},
        {"editor_refresh_screen_shell_selection_and_search_override_syntax_colors",
         test_editor_refresh_screen_shell_selection_and_search_override_syntax_colors},
        {"editor_refresh_screen_highlight_alignment_with_escaped_controls",
         test_editor_refresh_screen_highlight_alignment_with_escaped_controls},
        {"editor_refresh_screen_escapes_filename_controls",
         test_editor_refresh_screen_escapes_filename_controls},
        {"editor_refresh_screen_escapes_status_controls",
         test_editor_refresh_screen_escapes_status_controls},
        {"editor_refresh_screen_escapes_file_content_controls",
         test_editor_refresh_screen_escapes_file_content_controls},
        {"editor_line_number_gutter_width_and_absolute_numbers",
         test_editor_line_number_gutter_width_and_absolute_numbers},
        {"editor_line_numbers_disabled_removes_gutter",
         test_editor_line_numbers_disabled_removes_gutter},
        {"editor_refresh_screen_highlights_current_line",
         test_editor_refresh_screen_highlights_current_line},
        {"editor_viewport_center_cursor_centers_target_row",
         test_editor_viewport_center_cursor_centers_target_row},
        {"editor_viewport_center_cursor_clamps_near_top",
         test_editor_viewport_center_cursor_clamps_near_top},
        {"editor_refresh_screen_current_line_highlight_visible_when_drawer_focus_previewing",
         test_editor_refresh_screen_current_line_highlight_visible_when_drawer_focus_previewing},
        {"editor_refresh_screen_current_line_highlight_continues_after_selection",
         test_editor_refresh_screen_current_line_highlight_continues_after_selection},
        {"editor_refresh_screen_wrap_continuation_does_not_repeat_line_number",
         test_editor_refresh_screen_wrap_continuation_does_not_repeat_line_number},
        {"editor_refresh_screen_hides_expired_message",
         test_editor_refresh_screen_hides_expired_message},
        {"editor_refresh_screen_shows_right_overflow_indicator",
         test_editor_refresh_screen_shows_right_overflow_indicator},
        {"editor_refresh_screen_shows_left_overflow_indicator",
         test_editor_refresh_screen_shows_left_overflow_indicator},
        {"editor_refresh_screen_shows_both_horizontal_overflow_indicators",
         test_editor_refresh_screen_shows_both_horizontal_overflow_indicators},
        {"editor_refresh_screen_wraps_long_line_with_continuation_marker",
         test_editor_refresh_screen_wraps_long_line_with_continuation_marker},
        {"editor_refresh_screen_wrap_exact_width_has_no_continuation_marker",
         test_editor_refresh_screen_wrap_exact_width_has_no_continuation_marker},
        {"editor_refresh_screen_wrap_cursor_uses_visual_segment",
         test_editor_refresh_screen_wrap_cursor_uses_visual_segment},
        {"editor_refresh_screen_wrap_prefers_punctuation_breaks",
         test_editor_refresh_screen_wrap_prefers_punctuation_breaks},
        {"editor_refresh_screen_wrap_cursor_honors_continuation_indent",
         test_editor_refresh_screen_wrap_cursor_honors_continuation_indent},
        {"editor_refresh_screen_wrap_handles_resize_after_render",
         test_editor_refresh_screen_wrap_handles_resize_after_render},
        {"editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators",
         test_editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators},
        {"editor_refresh_screen_out_of_buffer_tildes_are_gray",
         test_editor_refresh_screen_out_of_buffer_tildes_are_gray},
        {"editor_refresh_screen_updates_horizontal_scroll",
         test_editor_refresh_screen_updates_horizontal_scroll},
        {"editor_refresh_screen_slice_after_multibyte_scroll",
         test_editor_refresh_screen_slice_after_multibyte_scroll},
        {"editor_refresh_screen_cursor_sequence_not_truncated_by_window_width",
         test_editor_refresh_screen_cursor_sequence_not_truncated_by_window_width},
        {"editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll",
         test_editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll},
        {"editor_refresh_screen_applies_a11y_dark_truecolor_theme",
         test_editor_refresh_screen_applies_a11y_dark_truecolor_theme},
        {"editor_refresh_screen_applies_256noir_theme",
         test_editor_refresh_screen_applies_256noir_theme},
        {"editor_refresh_screen_a11y_selection_overrides_syntax",
         test_editor_refresh_screen_a11y_selection_overrides_syntax},
};

const int g_render_frame_test_count =
        (int)(sizeof(g_render_frame_tests) / sizeof(g_render_frame_tests[0]));
