#include "render/screen.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "vterm.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* Replaces the root pane's tabs with a single active TERMINAL tab and returns the
 * terminal so the test can drive it (terminals are tabs, not leaf kinds). */
static struct editorTerminalPane *open_terminal_tab_in_root(const char *command) {
	if (!editorTabsInit() || E.layout_root == NULL || E.layout_root->is_split) {
		return NULL;
	}
	int cols = editorDrawerTextViewportCols(E.window_cols);
	if (cols < 1) {
		cols = 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneCreate(command, cols, E.window_rows);
	if (t == NULL) {
		return NULL;
	}
	int idx = editorTabCreateWidget(EDITOR_PANE_KIND_TERMINAL, t, editorTerminalPaneFree);
	if (idx < 0) {
		editorTerminalPaneFree(t);
		return NULL;
	}
	editorPaneViewClearTabs(&E.layout_root->as.leaf.view);
	(void)editorPaneViewActivateTab(&E.layout_root->as.leaf.view, idx);
	E.active_tab = idx;
	return t;
}

static int count_substrings(const char *haystack, const char *needle) {
	int count = 0;
	size_t needle_len = strlen(needle);
	const char *cursor = haystack;

	if (needle_len == 0) {
		return 0;
	}
	while ((cursor = strstr(cursor, needle)) != NULL) {
		count++;
		cursor += needle_len;
	}
	return count;
}

static int render_test_seed_blame_cache(int one_based_line, const char *author,
                                        time_t author_time) {
	editorGitBlameCacheClear(&E.active_buffer);
	E.git_blame_line = malloc(sizeof(*E.git_blame_line));
	ASSERT_TRUE(E.git_blame_line != NULL);
	memset(E.git_blame_line, 0, sizeof(*E.git_blame_line));
	E.git_blame_line->commit_sha = strdup("abcdef1234567890abcdef1234567890abcdef12");
	E.git_blame_line->short_sha = strdup("abcdef123456");
	E.git_blame_line->author_name = strdup(author);
	E.git_blame_line->author_email = strdup("alice@example.com");
	E.git_blame_line->author_time = author_time;
	E.git_blame_line->summary = strdup("Test commit");
	E.git_blame_line->filename = strdup("tracked.txt");
	ASSERT_TRUE(E.git_blame_line->commit_sha != NULL);
	ASSERT_TRUE(E.git_blame_line->short_sha != NULL);
	ASSERT_TRUE(E.git_blame_line->author_name != NULL);
	ASSERT_TRUE(E.git_blame_line->author_email != NULL);
	ASSERT_TRUE(E.git_blame_line->summary != NULL);
	ASSERT_TRUE(E.git_blame_line->filename != NULL);
	E.git_blame_line_number = one_based_line;
	E.git_blame_line_miss = 0;
	E.git_blame_filename = strdup(E.filename);
	E.git_blame_repo_root = strdup(E.git_repo_root);
	E.git_blame_branch = E.git_branch != NULL ? strdup(E.git_branch) : NULL;
	E.git_blame_head = E.git_head != NULL ? strdup(E.git_head) : NULL;
	E.git_blame_disk_state = E.disk_state;
	ASSERT_TRUE(E.git_blame_filename != NULL);
	ASSERT_TRUE(E.git_blame_repo_root != NULL);
	if (E.git_branch != NULL) {
		ASSERT_TRUE(E.git_blame_branch != NULL);
	}
	if (E.git_head != NULL) {
		ASSERT_TRUE(E.git_blame_head != NULL);
	}
	return 0;
}

static int render_test_setup_blame_file(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/rotide-blame/tracked.txt");
	E.git_repo_root = strdup("/tmp/rotide-blame");
	E.git_branch = strdup("main");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(E.git_repo_root != NULL);
	ASSERT_TRUE(E.git_branch != NULL);
	return 0;
}

static int test_editor_refresh_screen_renders_current_line_git_blame_indicator(void) {
	E.window_rows = 6;
	E.window_cols = 100;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("alpha");
	add_row("beta");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;
	E.rx = 0;
	ASSERT_TRUE(render_test_seed_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Alice 2 weeks ago") != NULL);
	ASSERT_ROW_TEXT_EQ(0, "alpha");
	ASSERT_ROW_TEXT_EQ(1, "beta");
	ASSERT_EQ_INT(0, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_renders_git_blame_indicator_only_on_current_line(void) {
	E.window_rows = 6;
	E.window_cols = 100;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("alpha");
	add_row("beta");
	E.dirty = 0;
	E.cy = 1;
	E.cx = 0;
	E.rx = 0;
	ASSERT_TRUE(render_test_seed_blame_cache(2, "Bob", time(NULL) - 2 * 86400) == 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(1, count_substrings(output, "Bob 2 days ago"));
	ASSERT_TRUE(strstr(output, "Alice") == NULL);
	ASSERT_ROW_TEXT_EQ(0, "alpha");
	ASSERT_ROW_TEXT_EQ(1, "beta");
	ASSERT_EQ_INT(0, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_git_blame_indicator_right_aligns_to_body_edge(void) {
	E.window_rows = 6;
	E.window_cols = 100;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("alpha");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;
	E.rx = 0;
	ASSERT_TRUE(render_test_seed_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Alice 2 weeks ago") != NULL);

	int file_origin = editorTextBodyStartColForCols(E.window_cols) +
	                  editorLineNumberGutterColsForCols(E.window_cols);
	int start_col = -1;
	int end_col = -1;
	ASSERT_TRUE(editorGitBlameIndicatorTestRange(NULL, &start_col, &end_col, NULL));
	/* The label is pushed well past the 5-column "alpha" line, not rendered
	 * immediately after it. */
	ASSERT_TRUE(start_col > file_origin + 1 + 5);
	ASSERT_TRUE(end_col > start_col);
	ASSERT_ROW_TEXT_EQ(0, "alpha");
	ASSERT_EQ_INT(0, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_git_blame_indicator_truncates_in_narrow_viewport(void) {
	E.window_rows = 6;
	E.window_cols = 40;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("alpha");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;
	E.rx = 0;
	ASSERT_TRUE(render_test_seed_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Alice") != NULL);
	ASSERT_TRUE(strstr(output, "Alice 2 weeks ago") == NULL);
	ASSERT_ROW_TEXT_EQ(0, "alpha");
	ASSERT_EQ_INT(0, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_git_blame_indicator_respects_horizontal_scroll(void) {
	E.window_rows = 6;
	E.window_cols = 100;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("prefix-alpha");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 7;
	E.rx = 7;
	E.coloff = 7;
	ASSERT_TRUE(render_test_seed_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "alpha") != NULL);
	ASSERT_TRUE(strstr(output, "Alice 2 weeks ago") != NULL);
	ASSERT_ROW_TEXT_EQ(0, "prefix-alpha");
	ASSERT_EQ_INT(7, E.coloff);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(0, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_git_blame_indicator_respects_soft_wrap(void) {
	E.window_rows = 6;
	E.window_cols = 50;
	E.line_wrap_enabled = 1;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("alpha beta gamma delta");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 18;
	E.rx = 18;
	ASSERT_TRUE(render_test_seed_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Alice") != NULL);
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma delta");
	ASSERT_EQ_INT(18, E.cx);
	ASSERT_EQ_INT(0, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_hides_git_blame_indicator_for_dirty_buffer(void) {
	E.window_rows = 6;
	E.window_cols = 100;
	ASSERT_TRUE(render_test_setup_blame_file() == 0);
	add_row("alpha");
	E.dirty = 1;
	E.cy = 0;
	E.cx = 0;
	E.rx = 0;
	ASSERT_TRUE(render_test_seed_blame_cache(1, "Alice", time(NULL) - 14 * 86400) == 0);
	E.dirty = 1;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Alice 2 weeks ago") == NULL);
	ASSERT_ROW_TEXT_EQ(0, "alpha");
	ASSERT_EQ_INT(1, E.dirty);
	free(output);
	editorGitFree();
	return 0;
}

static int test_editor_refresh_screen_renders_terminal_pane(void) {
	E.window_rows = 8;
	E.window_cols = 60;

	struct editorTerminalPane *t =
	        open_terminal_tab_in_root("printf 'rotide-screen-marker\\n'; sleep 2");
	if (t == NULL) {
		return 1;
	}
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

/* Regression for the removed host-screen clean-row shortcut: a second frame
 * with no terminal change must still composite the whole slice from libvterm.
 * The old row_dirty path marked unchanged rows "clean" and emitted a cursor-
 * forward instead of the content, which left stale cells whenever the pane
 * origin moved. Here the content must appear in both consecutive frames. */
static int test_editor_refresh_screen_terminal_repaints_unchanged_slice(void) {
	E.window_rows = 8;
	E.window_cols = 60;

	struct editorTerminalPane *t =
	        open_terminal_tab_in_root("printf 'rotide-stale-marker\\n'; sleep 3");
	if (t == NULL) {
		return 1;
	}
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
		saw_marker = strstr(buf, "rotide-stale-marker") != NULL;
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	if (!saw_marker) {
		return 1;
	}

	size_t first_len = 0;
	char *first = refresh_screen_and_capture(&first_len);
	ASSERT_TRUE(first != NULL);
	int in_first = strstr(first, "rotide-stale-marker") != NULL;
	free(first);

	/* No pump / no change between frames: under the old clean-row skip the
	 * second frame would omit the content. */
	size_t second_len = 0;
	char *second = refresh_screen_and_capture(&second_len);
	ASSERT_TRUE(second != NULL);
	int in_second = strstr(second, "rotide-stale-marker") != NULL;
	free(second);

	return (in_first && in_second) ? 0 : 1;
}

static int test_editor_refresh_screen_terminal_exit_overlay(void) {
	E.window_rows = 8;
	E.window_cols = 60;

	struct editorTerminalPane *t = open_terminal_tab_in_root("true");
	if (t == NULL) {
		return 1;
	}
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
	/* Production always has an editor tab open; the terminal split then hosts a
	 * TERMINAL tab whose close lets the now-empty split pane collapse. */
	ASSERT_TRUE(editorTabsInit());
	add_row("after-terminal");

	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("true", EDITOR_SPLIT_HORIZONTAL);
	ASSERT_TRUE(terminal_leaf != NULL);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	/* The terminal is a TERMINAL tab in the split pane, not a leaf kind_state. */
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
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

	if (open_terminal_tab_in_root("sleep 2") == NULL) {
		return 1;
	}
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

	struct editorTerminalPane *t = open_terminal_tab_in_root("sleep 2");
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
        {"editor_refresh_screen_renders_current_line_git_blame_indicator",
         test_editor_refresh_screen_renders_current_line_git_blame_indicator},
        {"editor_refresh_screen_renders_git_blame_indicator_only_on_current_line",
         test_editor_refresh_screen_renders_git_blame_indicator_only_on_current_line},
        {"editor_refresh_screen_git_blame_indicator_right_aligns_to_body_edge",
         test_editor_refresh_screen_git_blame_indicator_right_aligns_to_body_edge},
        {"editor_refresh_screen_git_blame_indicator_truncates_in_narrow_viewport",
         test_editor_refresh_screen_git_blame_indicator_truncates_in_narrow_viewport},
        {"editor_refresh_screen_git_blame_indicator_respects_horizontal_scroll",
         test_editor_refresh_screen_git_blame_indicator_respects_horizontal_scroll},
        {"editor_refresh_screen_git_blame_indicator_respects_soft_wrap",
         test_editor_refresh_screen_git_blame_indicator_respects_soft_wrap},
        {"editor_refresh_screen_hides_git_blame_indicator_for_dirty_buffer",
         test_editor_refresh_screen_hides_git_blame_indicator_for_dirty_buffer},
        {"editor_refresh_screen_renders_terminal_pane",
         test_editor_refresh_screen_renders_terminal_pane},
        {"editor_refresh_screen_terminal_repaints_unchanged_slice",
         test_editor_refresh_screen_terminal_repaints_unchanged_slice},
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
