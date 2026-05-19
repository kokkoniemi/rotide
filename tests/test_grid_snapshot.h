#ifndef TESTS_TEST_GRID_SNAPSHOT_H
#define TESTS_TEST_GRID_SNAPSHOT_H

#include <stddef.h>

/* Normalised vterm grid snapshot of the editor's current render state.
 *
 * Calls editorRefreshScreen(), feeds the captured bytes through a fresh
 * VTerm of size (E.window_rows, E.window_cols), and renders the resulting
 * cell grid into a deterministic multi-line string:
 *
 *   - One row per line, in screen order.
 *   - Each row is the visible characters; trailing spaces stripped.
 *   - Lines separated by '\n'.
 *   - No styling, no cursor markers, no escape sequences.
 *
 * Returns a malloc'd NUL-terminated string the caller frees. Returns NULL
 * on capture or VTerm failure.
 */
char *editor_grid_snapshot(size_t *len_out);

/* Compare two grid strings line-by-line and emit a unified-ish diff on
 * stderr. Returns 0 on match, 1 on diff.
 */
int editor_grid_snapshot_diff(const char *expected, const char *actual);

/* Assertion wrapper used by tests.
 *
 *   ASSERT_GRID_EQ(
 *     "~ |\n"
 *     "hello |\n"
 *     "~ |\n");
 *
 * Captures the current screen via editor_grid_snapshot(), strips the
 * common trailing-newlines, and compares. On mismatch, prints the diff
 * and returns 1 from the enclosing test function.
 *
 * Update-golden support: when the environment variable
 * ROTIDE_UPDATE_GOLDEN_STASH names a writable path, mismatches are NOT
 * reported as failures — the actual capture is appended to the stash
 * file as a JSONL row, and `tests/golden_apply --stash PATH` will
 * rewrite the expected literal in place. Wrap the literal in
 * golden-start and golden-end block-comment markers to opt the call
 * site into rewriting. See grid_snapshot_update.h for the marker
 * format and an example.
 */
#include "grid_snapshot_update.h"

#define ASSERT_GRID_EQ(expected) \
	do { \
		size_t _actual_len = 0; \
		char *_actual = editor_grid_snapshot(&_actual_len); \
		const char *_expected = (expected); \
		if (_actual == NULL) { \
			fprintf(stderr, \
				"Assertion failed in %s:%d: grid capture failed\n", \
				__func__, __LINE__); \
			return 1; \
		} \
		if (editor_grid_snapshot_check_or_stash(_expected, _actual, \
				__FILE__, __LINE__) != 0) { \
			fprintf(stderr, \
				"Assertion failed in %s:%d: grid mismatch (see diff above)\n", \
				__func__, __LINE__); \
			free(_actual); \
			return 1; \
		} \
		free(_actual); \
	} while (0)

#endif
