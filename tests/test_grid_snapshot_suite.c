#include "test_case.h"
#include "test_grid_snapshot.h"
#include "test_helpers.h"

#include "editing/edit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Smoke-tests for the grid snapshot helper itself. Render-tier suites
 * use ASSERT_GRID_EQ; these tests cover the helper's own contract. */

static int test_grid_snapshot_captures_text_lines(void) {
	add_row("hello");
	add_row("world");
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 6;
	E.window_cols = 24;

	size_t len = 0;
	char *grid = editor_grid_snapshot(&len);
	ASSERT_TRUE(grid != NULL);
	ASSERT_TRUE(len > 0);
	/* The exact layout depends on chrome (gutter, status bar). We assert
	 * the two file lines appear somewhere in the grid. */
	ASSERT_TRUE(strstr(grid, "hello") != NULL);
	ASSERT_TRUE(strstr(grid, "world") != NULL);
	free(grid);
	return 0;
}

static int test_grid_snapshot_strips_trailing_spaces(void) {
	add_row("hi");
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 4;
	E.window_cols = 30;

	size_t len = 0;
	char *grid = editor_grid_snapshot(&len);
	ASSERT_TRUE(grid != NULL);
	/* No row may end in a space before the newline. */
	const char *p = grid;
	while (*p != '\0') {
		const char *nl = strchr(p, '\n');
		if (nl == NULL) {
			break;
		}
		if (nl > p && nl[-1] == ' ') {
			fprintf(stderr,
				"row ends in space, full snapshot follows:\n%s", grid);
			free(grid);
			return 1;
		}
		p = nl + 1;
	}
	free(grid);
	return 0;
}

static int test_grid_snapshot_diff_reports_match_and_mismatch(void) {
	int rc = editor_grid_snapshot_diff("foo\nbar\n", "foo\nbar\n");
	ASSERT_EQ_INT(0, rc);
	rc = editor_grid_snapshot_diff("foo\nbar\n", "foo\nbaz\n");
	ASSERT_EQ_INT(1, rc);
	rc = editor_grid_snapshot_diff("foo\n", "foo\nextra\n");
	ASSERT_EQ_INT(1, rc);
	return 0;
}

static int test_grid_snapshot_assert_macro_passes_on_match(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 4;
	E.window_cols = 12;

	/* Capture once so we can use it as the expected value. The assertion
	 * macro must then accept the same string. */
	size_t len = 0;
	char *grid = editor_grid_snapshot(&len);
	ASSERT_TRUE(grid != NULL);
	ASSERT_GRID_EQ(grid);
	free(grid);
	return 0;
}

/* Demonstrates ASSERT_GRID_EQ against a hand-written golden string. If a
 * future change to the chrome (gutter format, tildes-on-empty-rows,
 * status-bar layout) breaks this assertion, the diff makes the
 * regression visible at a glance instead of requiring a byte-by-byte
 * audit of escape-laden output. */
static int test_grid_snapshot_matches_baked_chrome_layout(void) {
	add_row("hello");
	add_row("world");
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 6;
	E.window_cols = 24;
	editorSetStatusMsg("ready");

	ASSERT_GRID_EQ(
		"           │1  hello\n"
		"           │2  world\n"
		"           │  ~\n"
		"           │  ~\n"
		"[No Name] [+]1,1    100%\n"
		"ready\n");
	return 0;
}

const struct editorTestCase g_grid_snapshot_tests[] = {
	{"grid_snapshot_captures_text_lines", test_grid_snapshot_captures_text_lines},
	{"grid_snapshot_strips_trailing_spaces", test_grid_snapshot_strips_trailing_spaces},
	{"grid_snapshot_diff_reports_match_and_mismatch", test_grid_snapshot_diff_reports_match_and_mismatch},
	{"grid_snapshot_assert_macro_passes_on_match", test_grid_snapshot_assert_macro_passes_on_match},
	{"grid_snapshot_matches_baked_chrome_layout", test_grid_snapshot_matches_baked_chrome_layout},
};

const int g_grid_snapshot_test_count =
	(int)(sizeof(g_grid_snapshot_tests) / sizeof(g_grid_snapshot_tests[0]));
