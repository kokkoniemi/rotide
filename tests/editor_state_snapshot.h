#ifndef TESTS_EDITOR_STATE_SNAPSHOT_H
#define TESTS_EDITOR_STATE_SNAPSHOT_H

#include "rotide.h"

#include <stddef.h>

/*
 * Snapshot/compare helpers used by --validate-reset. Both the sequential
 * runner and the per-suite parallel children call into this so they share
 * the same exclude table (fields reset_editor_state legitimately
 * re-allocates).
 */

#define EDITOR_STATE_SNAPSHOT_SIZE sizeof(struct editorConfig)

/*
 * Capture the current bytes of editorConfig E into dest. dest must point
 * at EDITOR_STATE_SNAPSHOT_SIZE bytes.
 */
void rotideTestSnapshotEditor(unsigned char *dest);

/*
 * Compare a previously-captured snapshot to the current editorConfig E.
 * Returns 1 on match, 0 on diff; on diff, *first_diff_out (if non-NULL)
 * receives the offset of the first byte that differs.
 *
 * Byte ranges corresponding to fields that reset_editor_state
 * legitimately re-allocates (layout_root, focused_leaf) are skipped.
 */
int rotideTestSnapshotMatchesEditor(const unsigned char *snapshot, size_t *first_diff_out);

#endif
