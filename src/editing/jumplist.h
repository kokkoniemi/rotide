#ifndef ROTIDE_EDITING_JUMPLIST_H
#define ROTIDE_EDITING_JUMPLIST_H

#define ROTIDE_JUMPLIST_MAX 100

/* Entries own no paths, so pane views remain safe to copy by value. */
struct editorJumpEntry {
	int path_id;
	int cy;
	int cx;
};

struct editorJumplist {
	struct editorJumpEntry entries[ROTIDE_JUMPLIST_MAX];
	int count;
	int index; /* count denotes the live cursor position */
};

/* Returns a stable path id, or -1 for no path or allocation failure. */
int editorJumplistInternPath(const char *path);
const char *editorJumplistResolvePath(int path_id);
void editorJumplistPoolClear(void);

struct editorJumplist *editorJumplistActive(void);
void editorJumplistResetActive(void);

void editorJumplistRecord(void);
/* Records an origin after a preview has already moved the live cursor. */
void editorJumplistRecordPos(int cy, int cx);

/* Leaves *out untouched when no step is possible. */
int editorJumplistStepBack(int count, struct editorJumpEntry *out);
int editorJumplistStepForward(int count, struct editorJumpEntry *out);

int editorJumplistActiveCount(void);
const struct editorJumpEntry *editorJumplistActiveEntry(int i);
int editorJumplistActiveIndex(void);

/* Appends without history truncation or deduplication. */
void editorJumplistAppendRaw(struct editorJumplist *list, int path_id, int cy, int cx);

#endif
