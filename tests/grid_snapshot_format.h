#ifndef TESTS_GRID_SNAPSHOT_FORMAT_H
#define TESTS_GRID_SNAPSHOT_FORMAT_H

#include <stdio.h>

/* Write `text` as one or more concatenated C string literals, one
 * literal per source line (split at '\n'), each prefixed by `indent`.
 * Used by both the source rewriter (tests/golden_apply) and by the
 * editor-side stash helper that emits ROTIDE_UPDATE_GOLDEN_STASH rows.
 *
 * Pure function with no editor or test-runtime dependencies, so it can
 * link into the standalone golden_apply / golden_diff_report binaries.
 */
void editor_grid_snapshot_emit_c_string(const char *text, const char *indent, FILE *out);

#endif
