#ifndef TESTS_EDITOR_STATE_SNAPSHOT_H
#define TESTS_EDITOR_STATE_SNAPSHOT_H

#include "rotide.h"

#include <stddef.h>

/* --validate-reset helpers. dest must be EDITOR_STATE_SNAPSHOT_SIZE bytes.
 * Compare skips byte ranges that reset_editor_state legitimately
 * re-allocates (layout_root, focused_leaf). */

#define EDITOR_STATE_SNAPSHOT_SIZE sizeof(struct editorConfig)

void rotideTestSnapshotEditor(unsigned char *dest);
int rotideTestSnapshotMatchesEditor(const unsigned char *snapshot, size_t *first_diff_out);

#endif
