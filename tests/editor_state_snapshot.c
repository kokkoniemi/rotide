#include "editor_state_snapshot.h"

#include "runner_support.h"
#include "rotide.h"

#include <stddef.h>
#include <string.h>

#define EXCLUDE_FIELD(field)                                                                       \
	{ offsetof(struct editorConfig, field), sizeof(((struct editorConfig *)0)->field) }

static const struct snapshotExcludeRange k_excludes[] = {
        EXCLUDE_FIELD(layout_root),   // NOLINT(bugprone-sizeof-expression)
        EXCLUDE_FIELD(focused_leaf),  // NOLINT(bugprone-sizeof-expression)
};

#define K_EXCLUDE_COUNT ((int)(sizeof(k_excludes) / sizeof(k_excludes[0])))

void rotideTestSnapshotEditor(unsigned char *dest) {
	memcpy(dest, &E, EDITOR_STATE_SNAPSHOT_SIZE);
}

int rotideTestSnapshotMatchesEditor(const unsigned char *snapshot, size_t *first_diff_out) {
	return runnerSnapshotCompare(snapshot, (const unsigned char *)&E,
	                             EDITOR_STATE_SNAPSHOT_SIZE, k_excludes, K_EXCLUDE_COUNT,
	                             first_diff_out);
}
