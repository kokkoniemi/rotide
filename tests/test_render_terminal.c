#include "terminal/terminal_pane.h"
#include "test_case.h"
#include "test_support.h"
#include "vterm.h"
#include "test_helpers.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"
#include "rotide.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int test_editor_refresh_screen_renders_terminal_pane(void) {
	E.window_rows = 8;
	E.window_cols = 60;

	struct editorPaneNode *leaf = E.layout_root;
	if (leaf == NULL || leaf->is_split) {
		return 1;
	}
	int viewport_cols = editorDrawerTextViewportCols(E.window_cols);
	if (viewport_cols < 10) {
		return 1;
	}
	struct editorPaneNode *terminal_leaf = editorPaneNodeNewTerminalLeaf(
	        "printf 'rotide-screen-marker\\n'; sleep 2", viewport_cols, E.window_rows);
	if (terminal_leaf == NULL) {
		return 1;
	}
	editorPaneNodeFree(E.layout_root);
	E.layout_root = terminal_leaf;
	E.focused_leaf = terminal_leaf;

	struct editorTerminalPane *t =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	int waited = 0;
	int saw_marker = 0;
	while (waited < 2000 && !saw_marker) {
		(void)editorTerminalPanePump(t);
		char buf[4096];
		VTermRect rect = {
		        .start_row = 0, .end_row = t->rows, .start_col = 0, .end_col = t->cols};
		size_t n = vterm_screen_get_text(t->screen, buf, sizeof(buf) - 1, rect);
		if (n >= sizeof(buf)) {
			n = sizeof(buf) - 1;
		}
		buf[n] = '\0';
		if (strstr(buf, "rotide-screen-marker") != NULL) {
			saw_marker = 1;
			break;
		}
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	if (!saw_marker) {
		return 1;
	}

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	int found = strstr(output, "rotide-screen-marker") != NULL;
	free(output);
	return found ? 0 : 1;
}

static int test_editor_refresh_screen_terminal_exit_overlay(void) {
	E.window_rows = 8;
	E.window_cols = 60;

	int viewport_cols = editorDrawerTextViewportCols(E.window_cols);
	struct editorPaneNode *terminal_leaf =
	        editorPaneNodeNewTerminalLeaf("true", viewport_cols, E.window_rows);
	if (terminal_leaf == NULL) {
		return 1;
	}
	editorPaneNodeFree(E.layout_root);
	E.layout_root = terminal_leaf;
	E.focused_leaf = terminal_leaf;

	struct editorTerminalPane *t =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	int waited = 0;
	while (waited < 2000 && !t->exited) {
		(void)editorTerminalPanePump(t);
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	if (!t->exited) {
		return 1;
	}

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	int found = strstr(output, "[exited:") != NULL;
	free(output);
	return found ? 0 : 1;
}

static int test_editor_refresh_screen_closes_exited_terminal_without_keypress(void) {
	E.window_rows = 8;
	E.window_cols = 60;
	add_row("after-terminal");

	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("true", EDITOR_SPLIT_HORIZONTAL);
	ASSERT_TRUE(terminal_leaf != NULL);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	struct editorTerminalPane *t =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	ASSERT_TRUE(t != NULL);
	int waited = 0;
	while (waited < 2000 && !t->exited) {
		(void)editorTerminalPanePump(t);
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	ASSERT_TRUE(t->exited);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);

	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.focused_leaf == original);
	return 0;
}

static int test_editor_refresh_screen_terminal_cursor_uses_pane_origin(void) {
	E.window_rows = 6;
	E.window_cols = 40;
	E.line_numbers_enabled = 1;
	E.cursor_style = EDITOR_CURSOR_STYLE_BAR;
	E.cursor_blink_enabled = 1;

	int viewport_cols = editorDrawerTextViewportCols(E.window_cols);
	struct editorPaneNode *terminal_leaf =
	        editorPaneNodeNewTerminalLeaf("sleep 2", viewport_cols, E.window_rows);
	if (terminal_leaf == NULL) {
		return 1;
	}
	editorPaneNodeFree(E.layout_root);
	E.layout_root = terminal_leaf;
	E.focused_leaf = terminal_leaf;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorRect rect = {0};
	ASSERT_TRUE(editorLayoutFocusedLeafRect(&rect));
	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[%d;%dH", rect.y + 1,
	                     rect.x + 1) > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_terminal_uses_terminal_cursor_style(void) {
	E.window_rows = 8;
	E.window_cols = 60;
	E.cursor_style = EDITOR_CURSOR_STYLE_BAR;
	E.cursor_blink_enabled = 1;

	int viewport_cols = editorDrawerTextViewportCols(E.window_cols);
	struct editorPaneNode *terminal_leaf =
	        editorPaneNodeNewTerminalLeaf("sleep 2", viewport_cols, E.window_rows);
	if (terminal_leaf == NULL) {
		return 1;
	}
	editorPaneNodeFree(E.layout_root);
	E.layout_root = terminal_leaf;
	E.focused_leaf = terminal_leaf;

	struct editorTerminalPane *t =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	if (t == NULL || t->vt == NULL) {
		return 1;
	}
	/* Ask the terminal state machine for a steady block cursor and ensure
	 * screen refresh honors that instead of editor cursor settings. */
	vterm_input_write(t->vt, "\x1b[2 q", 5);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[5 q") == NULL);
	free(output);
	return 0;
}

/* libvterm REP (CSI N b) without any preceding printable character used to
 * spin forever because state->combine_width is 0. Found by fuzz target
 * `fuzz-vterm-nightly`; reproducer in tests/fuzz/vterm/corpus/rep_no_combine.
 * If this test ever hangs, the patch in vendor/libvterm/src/state.c REP
 * handler has been reverted. */
static int test_vterm_rep_without_preceding_char_does_not_hang(void) {
	VTerm *vt = vterm_new(24, 80);
	ASSERT_TRUE(vt != NULL);
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	ASSERT_TRUE(screen != NULL);
	vterm_screen_reset(screen, 1);

	const char input[] = "\x1b[51;b";
	vterm_input_write(vt, input, sizeof(input) - 1);
	vterm_screen_flush_damage(screen);

	VTermPos pos = {.row = 0, .col = 0};
	VTermScreenCell cell = {0};
	(void)vterm_screen_get_cell(screen, pos, &cell);
	ASSERT_EQ_INT(cell.chars[0], 0);

	vterm_free(vt);
	return 0;
}

const struct editorTestCase g_render_terminal_tests[] = {
        {"editor_refresh_screen_renders_terminal_pane",
         test_editor_refresh_screen_renders_terminal_pane},
        {"editor_refresh_screen_terminal_exit_overlay",
         test_editor_refresh_screen_terminal_exit_overlay},
        {"editor_refresh_screen_closes_exited_terminal_without_keypress",
         test_editor_refresh_screen_closes_exited_terminal_without_keypress},
        {"editor_refresh_screen_terminal_cursor_uses_pane_origin",
         test_editor_refresh_screen_terminal_cursor_uses_pane_origin},
        {"editor_refresh_screen_terminal_uses_terminal_cursor_style",
         test_editor_refresh_screen_terminal_uses_terminal_cursor_style},
        {"vterm_rep_without_preceding_char_does_not_hang",
         test_vterm_rep_without_preceding_char_does_not_hang},
};

const int g_render_terminal_test_count =
        (int)(sizeof(g_render_terminal_tests) / sizeof(g_render_terminal_tests[0]));
