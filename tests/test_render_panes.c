#include "render/popup.h"
#include "test_case.h"
#include "test_grid_snapshot.h"
#include "test_support.h"

#define EDITOR_PANE_VBORDER_UTF8 "\xe2\x94\x82"
#define EDITOR_PANE_HBORDER_UTF8 "\xe2\x94\x80"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/text_source.h"
#include "language/syntax.h"
#include "render/viewport.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "test_helpers.h"
#include "workspace/layout.h"
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

static const char *snapshot_line_start(const char *snapshot, int line_idx) {
	const char *line = snapshot;
	for (int i = 0; i < line_idx && line != NULL; i++) {
		line = strchr(line, '\n');
		if (line != NULL) {
			line++;
		}
	}
	return line;
}

static int snapshot_line_contains(const char *snapshot, int line_idx, const char *needle) {
	const char *line = snapshot_line_start(snapshot, line_idx);
	if (line == NULL || needle == NULL) {
		return 0;
	}
	const char *line_end = strchr(line, '\n');
	const char *match = strstr(line, needle);
	return match != NULL && (line_end == NULL || match < line_end);
}

static int snapshot_line_count_substrings(const char *snapshot, int line_idx, const char *needle) {
	const char *line = snapshot_line_start(snapshot, line_idx);
	if (line == NULL || needle == NULL || needle[0] == '\0') {
		return 0;
	}
	const char *line_end = strchr(line, '\n');
	if (line_end == NULL) {
		line_end = line + strlen(line);
	}
	int count = 0;
	size_t needle_len = strlen(needle);
	for (const char *cursor = line; cursor < line_end;) {
		const char *match = strstr(cursor, needle);
		if (match == NULL || match >= line_end) {
			break;
		}
		count++;
		cursor = match + needle_len;
	}
	return count;
}

static int write_repeated_temp_c_file(char path_buf[], size_t path_buf_size, const char *prefix,
                                      const char *line, int repeats) {
	if (path_buf == NULL || prefix == NULL || line == NULL || repeats < 0) {
		return 0;
	}
	int min_size = snprintf(NULL, 0, "/tmp/%sXXXXXX.c", prefix) + 1;
	if (min_size <= 0 || (size_t)min_size > path_buf_size) {
		return 0;
	}
	int written = snprintf(path_buf, path_buf_size, "/tmp/%sXXXXXX.c", prefix);
	if (written <= 0 || (size_t)written >= path_buf_size) {
		return 0;
	}

	int fd = mkstemps(path_buf, 2);
	if (fd == -1) {
		return 0;
	}

	size_t line_len = strlen(line);
	int ok = 1;
	for (int i = 0; i < repeats; i++) {
		if (write_all(fd, line, line_len) != 0) {
			ok = 0;
			break;
		}
	}
	if (close(fd) != 0) {
		ok = 0;
	}
	if (!ok) {
		(void)unlink(path_buf);
	}
	return ok;
}

static int test_editor_popup_open_select_close(void) {
	struct editorPopupItem items[3] = {
	        {.label = (char *)"alpha"},
	        {.label = (char *)"beta"},
	        {.label = (char *)"gamma"},
	};
	ASSERT_TRUE(editorPopupOpen(items, 3, 0, 0));
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(3, editorPopupItemCount());
	ASSERT_EQ_INT(0, editorPopupSelectedIndex());
	ASSERT_EQ_STR("alpha", editorPopupSelectedLabel());

	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_DOWN));
	ASSERT_EQ_INT(1, editorPopupSelectedIndex());
	ASSERT_EQ_STR("beta", editorPopupSelectedLabel());
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_DOWN));
	ASSERT_EQ_INT(2, editorPopupSelectedIndex());
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_DOWN));
	ASSERT_EQ_INT(2, editorPopupSelectedIndex());
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_UP));
	ASSERT_EQ_INT(1, editorPopupSelectedIndex());

	ASSERT_EQ_INT(EDITOR_POPUP_KEY_ACCEPTED, editorPopupHandleKey('\r'));
	ASSERT_TRUE(editorPopupIsVisible());

	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey('\x1b'));
	ASSERT_TRUE(!editorPopupIsVisible());
	ASSERT_TRUE(editorPopupSelectedLabel() == NULL);
	return 0;
}

static int test_editor_popup_other_key_dismisses_with_pass_through(void) {
	struct editorPopupItem items[1] = {{.label = (char *)"only"}};
	ASSERT_TRUE(editorPopupOpen(items, 1, 0, 0));
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_DISMISSED_PASS_THROUGH, editorPopupHandleKey('a'));
	ASSERT_TRUE(!editorPopupIsVisible());
	return 0;
}

static int test_editor_popup_renders_overlay_in_text_area(void) {
	add_row("hello world");
	E.window_rows = 6;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;

	struct editorPopupItem items[2] = {
	        {.label = (char *)"completion_alpha"},
	        {.label = (char *)"completion_beta"},
	};
	ASSERT_TRUE(editorPopupOpen(items, 2, 0, 0));

	ASSERT_GRID_EQ(
	        /* golden-start */
	        "                   │1  hello world\n"
	        "                   │  ~\n"
	        "                   │  ~ completion_alpha\n"
	        "                   │  ~ completion_beta\n"
	        " No Name] [+]                1,1    100%\n"
	        /* golden-end */
	);

	editorPopupClose();
	return 0;
}

static int test_editor_popup_close_repaints_rows_under_overlay(void) {
	add_row("hello world");
	add_row("second line");
	add_row("third line");
	E.window_rows = 6;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;

	struct editorPopupItem items[2] = {
	        {.label = (char *)"completion_xyz"},
	        {.label = (char *)"completion_abc"},
	};
	ASSERT_TRUE(editorPopupOpen(items, 2, 0, 0));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "completion_xyz") != NULL);
	free(output);

	editorPopupClose();
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "completion_xyz") == NULL);
	/*
	 * Popup is rendered one line below the cursor (cy=0), so the rows covered are
	 * "second line" and "third line". After the popup closes those rows must be
	 * repainted, not stay frozen in the terminal frame cache.
	 */
	ASSERT_TRUE(strstr(output, "second line") != NULL);
	free(output);
	return 0;
}

static int test_editor_popup_placement_below_cursor(void) {
	add_row("abc");
	add_row("def");
	add_row("ghi");
	E.window_rows = 10;
	E.window_cols = 40;
	E.cy = 1;
	E.cx = 0;

	struct editorPopupItem items[2] = {
	        {.label = (char *)"first"},
	        {.label = (char *)"second"},
	};
	ASSERT_TRUE(editorPopupOpen(items, 2, 1, 0));

	int row = 0;
	int col = 0;
	int rows = 0;
	int cols = 0;
	int above = 0;
	editorPopupComputePlacement(&row, &col, &rows, &cols, &above);
	ASSERT_EQ_INT(0, above);
	ASSERT_EQ_INT(2, rows);
	ASSERT_EQ_INT(4, row);

	editorPopupClose();
	return 0;
}

static int test_editor_popup_placement_above_when_below_overflows(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 6;
	E.window_cols = 40;
	E.cy = 5;
	E.cx = 0;

	struct editorPopupItem items[3] = {
	        {.label = (char *)"a"},
	        {.label = (char *)"b"},
	        {.label = (char *)"c"},
	};
	ASSERT_TRUE(editorPopupOpen(items, 3, 5, 0));

	int row = 0;
	int col = 0;
	int rows = 0;
	int cols = 0;
	int above = 0;
	editorPopupComputePlacement(&row, &col, &rows, &cols, &above);
	ASSERT_EQ_INT(1, above);
	ASSERT_EQ_INT(3, rows);
	ASSERT_EQ_INT(4, row);

	editorPopupClose();
	return 0;
}

static int test_editor_refresh_screen_vertical_split_renders_border(void) {
	add_row("hello world");
	E.window_rows = 6;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_GRID_EQ(
	        /* golden-start */
	        "                        │1  hello world   │1  hello world\n"
	        "                        │  ~              │  ~\n"
	        "                        │  ~              │  ~\n"
	        "                        │  ~              │  ~\n"
	        "[No Name] [+]                                    1,1    100%\n"
	        /* golden-end */
	);
	return 0;
}

static int test_editor_refresh_screen_vertical_split_renders_top_pane_strips(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/left.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/right.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 6;
	E.window_cols = 70;

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);
	left->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&left->as.leaf.view, 0));
	left->as.leaf.view.active_tab_idx = 0;
	right->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&right->as.leaf.view, 1));
	right->as.leaf.view.active_tab_idx = 1;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	const char *left_label = strstr(output, "left.txt");
	const char *right_label = strstr(output, "right.txt");
	ASSERT_TRUE(left_label != NULL);
	ASSERT_TRUE(right_label != NULL);
	const char *pane_border = strstr(left_label, EDITOR_PANE_VBORDER_UTF8);
	ASSERT_TRUE(pane_border != NULL);
	ASSERT_TRUE(left_label < pane_border);
	ASSERT_TRUE(pane_border < right_label);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_horizontal_split_renders_bottom_tab_strip(void) {
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/top.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/bottom.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 6;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(sibling != NULL);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	struct editorPaneNode *top = E.layout_root->as.split.first;
	top->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&top->as.leaf.view, 0));
	top->as.leaf.view.active_tab_idx = 0;
	sibling->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&sibling->as.leaf.view, 1));
	sibling->as.leaf.view.active_tab_idx = 1;

	size_t snapshot_len = 0;
	char *snapshot = editor_grid_snapshot(&snapshot_len);
	ASSERT_TRUE(snapshot != NULL);
	ASSERT_TRUE(snapshot_line_contains(snapshot, 2, "bottom.txt"));
	/* The strip row continues as a horizontal border to the right of the tabs
	 * so the split divider remains visually contiguous past the last tab. */
	ASSERT_TRUE(snapshot_line_contains(snapshot, 2, EDITOR_PANE_HBORDER_UTF8));
	free(snapshot);
	return 0;
}

static int test_editor_ensure_pane_occupancy_replaces_empty_pane_with_buffer(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("top-body");
	E.filename = strdup("/tmp/top.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.window_rows = 8;
	E.window_cols = 60;

	struct editorPaneNode *top = E.focused_leaf;
	struct editorPaneNode *bottom = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(bottom != NULL);
	top->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&top->as.leaf.view, 0));
	top->as.leaf.view.active_tab_idx = 0;
	bottom->as.leaf.view.pane_tab_count = 0;
	bottom->as.leaf.view.active_tab_idx = -1;
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(bottom));

	editorTabsEnsurePaneOccupancy();

	ASSERT_EQ_INT(1, bottom->as.leaf.view.pane_tab_count);
	ASSERT_TRUE(bottom->as.leaf.view.active_tab_idx >= 0);
	ASSERT_EQ_INT(bottom->as.leaf.view.active_tab_idx, bottom->as.leaf.view.pane_tabs[0]);
	return 0;
}

static int test_editor_refresh_screen_nested_split_strip_keeps_outer_vborder(void) {
	/* Layout: vertical split with the right child further split horizontally.
	 * The inner horizontal border row is now the lower-right pane's tab strip,
	 * while the outer vertical border must still cross that row. */
	ASSERT_TRUE(editorTabsInit());
	E.filename = strdup("/tmp/left.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/right-top.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabNewEmpty());
	E.filename = strdup("/tmp/right-bottom.txt");
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 12;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);
	struct editorPaneNode *right_bottom =
	        editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(right_bottom != NULL);
	ASSERT_EQ_INT(3, editorPaneTreeLeafCount(E.layout_root));
	struct editorPaneNode *right_top = right;
	left->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&left->as.leaf.view, 0));
	left->as.leaf.view.active_tab_idx = 0;
	right_top->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&right_top->as.leaf.view, 1));
	right_top->as.leaf.view.active_tab_idx = 1;
	right_bottom->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&right_bottom->as.leaf.view, 2));
	right_bottom->as.leaf.view.active_tab_idx = 2;

	size_t snapshot_len = 0;
	char *snapshot = editor_grid_snapshot(&snapshot_len);
	ASSERT_TRUE(snapshot != NULL);
	ASSERT_TRUE(snapshot_line_contains(snapshot, 5, "right-bottom.txt"));
	ASSERT_TRUE(snapshot_line_count_substrings(snapshot, 5, EDITOR_PANE_VBORDER_UTF8) >= 2);
	/* Strip's trailing area renders as the horizontal split divider. */
	ASSERT_TRUE(snapshot_line_contains(snapshot, 5, EDITOR_PANE_HBORDER_UTF8));
	free(snapshot);
	return 0;
}

static int test_editor_refresh_screen_unfocused_same_tab_pane_renders_content(void) {
	add_row("pane-x");
	E.window_rows = 6;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	/* Same-tab unfocused panes mirror the document at their own cursor.
	 * Both panes here share the active tab and the same view (split
	 * copies it), so the marker should render in both halves. */
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(original));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	const char *first = strstr(output, "pane-x");
	ASSERT_TRUE(first != NULL);
	const char *second = strstr(first + 1, "pane-x");
	ASSERT_TRUE(second != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_vertical_split_clips_left_pane_row(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL");
	E.window_rows = 6;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	add_row("right-pane-marker");
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL") == NULL);
	ASSERT_TRUE(strstr(output, "right-pane-marker") != NULL);
	free(output);
	return 0;
}

static int
test_editor_refresh_screen_vertical_split_eof_tilde_does_not_collapse_into_right_pane(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 6;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	add_row("right-pane-marker");
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_EQ_INT(0, E.active_tab);
	ASSERT_EQ_INT(1, right->as.leaf.view.active_tab_idx);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "right-pane-marker") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m~\x1b[39m\xe2\x94\x82right-pane-marker") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_same_tab_panes_keep_selection_independent(void) {
	add_row("alpha beta gamma");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *top = E.focused_leaf;
	struct editorPaneNode *bottom = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(bottom != NULL);
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(top));

	E.cx = 5;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.selection_mode_active = 1;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(1, count_substrings(output, "\x1b[7malpha"));
	ASSERT_TRUE(strstr(output, "beta gamma") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_horizontal_scrolled_panes_keep_syntax(void) {
	ASSERT_TRUE(editorTabsInit());
	char path[128];
	ASSERT_TRUE(write_repeated_temp_c_file(path, sizeof(path), "rotide-pane-scroll-",
	                                       "int value = 42;\n", 200));
	editorOpen(path);
	E.window_rows = 12;
	E.window_cols = 100;

	struct editorPaneNode *top = E.focused_leaf;
	struct editorPaneNode *bottom = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(bottom != NULL);

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(bottom));
	E.rowoff = 120;
	E.cy = 120;
	E.cx = 0;

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(top));
	E.rowoff = 40;
	E.cy = 40;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(count_substrings(output, "\x1b[96mint\x1b[39m") >= 2);
	free(output);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_horizontal_scrolled_panes_avoid_syntax_thrash(void) {
	ASSERT_TRUE(editorTabsInit());
	char path[128];
	ASSERT_TRUE(write_repeated_temp_c_file(path, sizeof(path), "rotide-pane-scroll-thrash-",
	                                       "int value = 42;\n", 300));
	editorOpen(path);
	E.window_rows = 14;
	E.window_cols = 100;

	struct editorPaneNode *top = E.focused_leaf;
	struct editorPaneNode *bottom = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(bottom != NULL);

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(bottom));
	E.rowoff = 180;
	E.cy = 180;
	E.cx = 0;

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(top));
	E.rowoff = 60;
	E.cy = 60;
	E.cx = 0;

	editorActiveTextSourceBuildTestResetCount();
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_TRUE(editorActiveTextSourceBuildTestCount() <= 24);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_unfocused_different_tab_pane_renders_content(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("left-pane-marker");
	E.window_rows = 6;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	add_row("right-pane-marker");
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_EQ_INT(0, E.active_tab);
	ASSERT_EQ_INT(1, right->as.leaf.view.active_tab_idx);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "left-pane-marker") != NULL);
	ASSERT_TRUE(strstr(output, "right-pane-marker") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_unfocused_different_tab_pane_keeps_syntax(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("left-pane-plain");
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	char path[128];
	ASSERT_TRUE(write_temp_file_with_suffix(path, sizeof(path), "rotide-pane-syntax-", ".c",
	                                        "int main(void) {\n    return 0;\n}\n"));
	editorOpen(path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_EQ_INT(0, E.active_tab);
	ASSERT_EQ_INT(1, right->as.leaf.view.active_tab_idx);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint") != NULL);
	free(output);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_unfocused_different_tab_pane_keeps_syntax_across_rows(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	add_row("left-pane-plain-0");
	add_row("left-pane-plain-1");
	add_row("left-pane-plain-2");
	add_row("left-pane-plain-3");

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(right));
	ASSERT_TRUE(editorTabNewEmpty());
	char path[128];
	ASSERT_TRUE(write_temp_file_with_suffix(path, sizeof(path), "rotide-pane-syntax-multi-",
	                                        ".c", "int a = 1;\nint b = 2;\nint c = 3;\n"));
	editorOpen(path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_EQ_INT(0, E.active_tab);
	ASSERT_EQ_INT(1, right->as.leaf.view.active_tab_idx);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(count_substrings(output, "\x1b[96mint\x1b[39m") >= 2);
	free(output);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int
test_editor_refresh_screen_unfocused_different_tab_pane_preserves_free_scroll_mode(void) {
	ASSERT_TRUE(editorTabsInit());
	E.window_rows = 8;
	E.window_cols = 100;

	add_row("left-000");
	add_row("left-001");
	add_row("left-002");
	add_row("left-003");
	add_row("left-004");
	add_row("left-005");
	add_row("left-006");
	add_row("left-007");
	add_row("left-008");
	add_row("left-009");
	add_row("left-010");
	add_row("left-011");

	struct editorPaneNode *left = E.focused_leaf;
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(right));
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("right-pane-row");
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editorLayoutSetFocusedLeaf(left));
	ASSERT_EQ_INT(0, E.active_tab);
	ASSERT_EQ_INT(1, right->as.leaf.view.active_tab_idx);

	E.cy = 0;
	E.cx = 0;
	editorViewportScrollByRows(5);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	ASSERT_EQ_INT(5, E.rowoff);
	ASSERT_TRUE(E.cy < E.rowoff);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "right-pane-row") != NULL);
	free(output);

	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	ASSERT_EQ_INT(5, E.rowoff);
	ASSERT_TRUE(E.cy < E.rowoff);

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	ASSERT_EQ_INT(5, E.rowoff);
	ASSERT_TRUE(E.cy < E.rowoff);
	return 0;
}

const struct editorTestCase g_render_panes_tests[] = {
        {"editor_popup_open_select_close", test_editor_popup_open_select_close},
        {"editor_popup_other_key_dismisses_with_pass_through",
         test_editor_popup_other_key_dismisses_with_pass_through},
        {"editor_popup_renders_overlay_in_text_area",
         test_editor_popup_renders_overlay_in_text_area},
        {"editor_popup_close_repaints_rows_under_overlay",
         test_editor_popup_close_repaints_rows_under_overlay},
        {"editor_popup_placement_below_cursor", test_editor_popup_placement_below_cursor},
        {"editor_popup_placement_above_when_below_overflows",
         test_editor_popup_placement_above_when_below_overflows},
        {"editor_refresh_screen_vertical_split_renders_border",
         test_editor_refresh_screen_vertical_split_renders_border},
        {"editor_refresh_screen_vertical_split_renders_top_pane_strips",
         test_editor_refresh_screen_vertical_split_renders_top_pane_strips},
        {"editor_refresh_screen_horizontal_split_renders_bottom_tab_strip",
         test_editor_refresh_screen_horizontal_split_renders_bottom_tab_strip},
        {"editor_ensure_pane_occupancy_replaces_empty_pane_with_buffer",
         test_editor_ensure_pane_occupancy_replaces_empty_pane_with_buffer},
        {"editor_refresh_screen_nested_split_strip_keeps_outer_vborder",
         test_editor_refresh_screen_nested_split_strip_keeps_outer_vborder},
        {"editor_refresh_screen_unfocused_same_tab_pane_renders_content",
         test_editor_refresh_screen_unfocused_same_tab_pane_renders_content},
        {"editor_refresh_screen_vertical_split_clips_left_pane_row",
         test_editor_refresh_screen_vertical_split_clips_left_pane_row},
        {"editor_refresh_screen_vertical_split_eof_tilde_does_not_collapse_into_right_pane",
         test_editor_refresh_screen_vertical_split_eof_tilde_does_not_collapse_into_right_pane},
        {"editor_refresh_screen_same_tab_panes_keep_selection_independent",
         test_editor_refresh_screen_same_tab_panes_keep_selection_independent},
        {"editor_refresh_screen_horizontal_scrolled_panes_keep_syntax",
         test_editor_refresh_screen_horizontal_scrolled_panes_keep_syntax},
        {"editor_refresh_screen_horizontal_scrolled_panes_avoid_syntax_thrash",
         test_editor_refresh_screen_horizontal_scrolled_panes_avoid_syntax_thrash},
        {"editor_refresh_screen_unfocused_different_tab_pane_renders_content",
         test_editor_refresh_screen_unfocused_different_tab_pane_renders_content},
        {"editor_refresh_screen_unfocused_different_tab_pane_keeps_syntax",
         test_editor_refresh_screen_unfocused_different_tab_pane_keeps_syntax},
        {"editor_refresh_screen_unfocused_different_tab_pane_keeps_syntax_across_rows",
         test_editor_refresh_screen_unfocused_different_tab_pane_keeps_syntax_across_rows},
        {"editor_refresh_screen_unfocused_different_tab_pane_preserves_free_scroll_mode",
         test_editor_refresh_screen_unfocused_different_tab_pane_preserves_free_scroll_mode},
};

const int g_render_panes_test_count =
        (int)(sizeof(g_render_panes_tests) / sizeof(g_render_panes_tests[0]));
