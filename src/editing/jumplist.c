#include "editing/jumplist.h"

#include "rotide.h"
#include "support/alloc.h"
#include "workspace/layout.h"

#include <stdlib.h>
#include <string.h>

int editorJumplistInternPath(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	for (int i = 0; i < E.jump_path_pool_count; i++) {
		if (strcmp(E.jump_path_pool[i], path) == 0) {
			return i;
		}
	}
	if (E.jump_path_pool_count == E.jump_path_pool_capacity) {
		int new_cap = E.jump_path_pool_capacity == 0 ? 8 : E.jump_path_pool_capacity * 2;
		char **grown = editorRealloc(E.jump_path_pool, (size_t)new_cap * sizeof(*grown));
		if (grown == NULL) {
			return -1;
		}
		E.jump_path_pool = grown;
		E.jump_path_pool_capacity = new_cap;
	}
	size_t len = strlen(path);
	char *copy = editorMalloc(len + 1);
	if (copy == NULL) {
		return -1;
	}
	memcpy(copy, path, len + 1);
	E.jump_path_pool[E.jump_path_pool_count] = copy;
	return E.jump_path_pool_count++;
}

const char *editorJumplistResolvePath(int path_id) {
	if (path_id < 0 || path_id >= E.jump_path_pool_count) {
		return NULL;
	}
	return E.jump_path_pool[path_id];
}

void editorJumplistPoolClear(void) {
	for (int i = 0; i < E.jump_path_pool_count; i++) {
		free(E.jump_path_pool[i]);
	}
	free(E.jump_path_pool);
	E.jump_path_pool = NULL;
	E.jump_path_pool_count = 0;
	E.jump_path_pool_capacity = 0;
}

struct editorJumplist *editorJumplistActive(void) {
	if (E.focused_leaf == NULL || E.focused_leaf->is_split) {
		return NULL;
	}
	if (E.focused_leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return NULL;
	}
	return &E.focused_leaf->as.leaf.view.jumplist;
}

static struct editorJumpEntry editorJumplistCurrentEntry(void) {
	struct editorJumpEntry entry;
	entry.path_id = editorJumplistInternPath(E.filename);
	entry.cy = E.cy;
	entry.cx = E.cx;
	return entry;
}

/* Keep the index on the same logical entry when the oldest entry is evicted. */
static void editorJumplistAppendEntry(struct editorJumplist *list, struct editorJumpEntry entry) {
	if (list->count == ROTIDE_JUMPLIST_MAX) {
		for (int i = 0; i < list->count - 1; i++) {
			list->entries[i] = list->entries[i + 1];
		}
		list->count--;
		if (list->index > 0) {
			list->index--;
		}
	}
	list->entries[list->count++] = entry;
}

void editorJumplistAppendRaw(struct editorJumplist *list, int path_id, int cy, int cx) {
	if (list == NULL) {
		return;
	}
	struct editorJumpEntry entry = {path_id, cy, cx};
	editorJumplistAppendEntry(list, entry);
}

void editorJumplistResetActive(void) {
	struct editorJumplist *list = editorJumplistActive();
	if (list == NULL) {
		return;
	}
	list->count = 0;
	list->index = 0;
}

static void editorJumplistRecordEntryInternal(struct editorJumplist *list,
                                              struct editorJumpEntry cur) {
	if (list->index < list->count) {
		list->count = list->index;
	}
	for (int i = 0; i < list->count; i++) {
		if (list->entries[i].path_id == cur.path_id && list->entries[i].cy == cur.cy) {
			for (int j = i; j < list->count - 1; j++) {
				list->entries[j] = list->entries[j + 1];
			}
			list->count--;
			break;
		}
	}
	editorJumplistAppendEntry(list, cur);
	list->index = list->count;
}

void editorJumplistRecord(void) {
	struct editorJumplist *list = editorJumplistActive();
	if (list == NULL) {
		return;
	}
	editorJumplistRecordEntryInternal(list, editorJumplistCurrentEntry());
}

void editorJumplistRecordPos(int cy, int cx) {
	struct editorJumplist *list = editorJumplistActive();
	if (list == NULL) {
		return;
	}
	struct editorJumpEntry entry = {editorJumplistInternPath(E.filename), cy, cx};
	editorJumplistRecordEntryInternal(list, entry);
}

int editorJumplistStepBack(int count, struct editorJumpEntry *out) {
	struct editorJumplist *list = editorJumplistActive();
	if (list == NULL) {
		return 0;
	}
	if (count < 1) {
		count = 1;
	}
	int moved = 0;
	for (int i = 0; i < count; i++) {
		if (list->index == list->count) {
			/* At the tip: save the current live position so a forward jump
			 * can return here, then land on the newest stored entry. */
			if (list->count == 0) {
				break;
			}
			editorJumplistAppendEntry(list, editorJumplistCurrentEntry());
			list->index = list->count - 2;
			if (list->index < 0) {
				list->index = 0;
			}
			moved = 1;
		} else if (list->index > 0) {
			list->index--;
			moved = 1;
		} else {
			break;
		}
	}
	if (moved && out != NULL) {
		*out = list->entries[list->index];
	}
	return moved;
}

int editorJumplistStepForward(int count, struct editorJumpEntry *out) {
	struct editorJumplist *list = editorJumplistActive();
	if (list == NULL) {
		return 0;
	}
	if (count < 1) {
		count = 1;
	}
	int moved = 0;
	for (int i = 0; i < count; i++) {
		if (list->index < list->count - 1) {
			list->index++;
			moved = 1;
		} else {
			break;
		}
	}
	if (moved && out != NULL) {
		*out = list->entries[list->index];
	}
	return moved;
}

int editorJumplistActiveCount(void) {
	struct editorJumplist *list = editorJumplistActive();
	return list == NULL ? 0 : list->count;
}

const struct editorJumpEntry *editorJumplistActiveEntry(int i) {
	struct editorJumplist *list = editorJumplistActive();
	if (list == NULL || i < 0 || i >= list->count) {
		return NULL;
	}
	return &list->entries[i];
}

int editorJumplistActiveIndex(void) {
	struct editorJumplist *list = editorJumplistActive();
	return list == NULL ? 0 : list->index;
}
