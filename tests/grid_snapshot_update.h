#ifndef TESTS_GRID_SNAPSHOT_UPDATE_H
#define TESTS_GRID_SNAPSHOT_UPDATE_H

#include <stddef.h>
#include <stdio.h>

/* --update-golden support for ASSERT_GRID_EQ.
 *
 * When ROTIDE_UPDATE_GOLDEN_STASH points at a writable path, mismatches
 * are NOT reported as failures — instead, a JSONL row capturing the
 * actual grid is appended to that path:
 *
 *   {"file":"tests/...c","line":123,"actual":"...escaped..."}
 *
 * After the test run, `tests/golden_apply` walks the stash and rewrites
 * source files in place, replacing the block delimited by the
 * `golden-start` / `golden-end` block-comment markers that follow each
 * recorded (file, line). `tests/golden_diff_report` produces a preview
 * without applying.
 *
 * Marker convention (note the spaces inserted in this doc to avoid
 * nested block comments — write them without the spaces in real code):
 *
 *     ASSERT_GRID_EQ(
 *         /\* golden-start *\/
 *         "row 1\n"
 *         "row 2\n"
 *         /\* golden-end *\/
 *     );
 *
 * Called from ASSERT_GRID_EQ. Returns 0 if the macro should treat the
 * assertion as passing (match, OR mismatch in update mode); returns 1
 * if the macro should fail the test (mismatch with no update mode).
 */
int editor_grid_snapshot_check_or_stash(const char *expected, const char *actual,
		const char *file, int line);

#endif
