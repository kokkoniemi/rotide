#ifndef EDITOR_DRAWER_INTERNAL_H
#define EDITOR_DRAWER_INTERNAL_H

#include <stddef.h>

struct editorDrawerEntryView;

struct editorDrawerNode {
	char *name;
	char *path;
	int is_dir;
	int is_expanded;
	int scanned;
	int scan_error;
	struct editorDrawerNode *parent;
	struct editorDrawerNode **children;
	int child_count;
};

struct editorDrawerLookup {
	struct editorDrawerNode *node;
	int depth;
	int visible_idx;
	int parent_visible_idx;
};

struct editorDrawerNode *editorDrawerNodeNew(const char *name, const char *path, int is_dir,
		struct editorDrawerNode *parent);
void editorDrawerNodeFree(struct editorDrawerNode *node);
int editorDrawerEnsureScanned(struct editorDrawerNode *node);
int editorDrawerCountVisibleFromNode(struct editorDrawerNode *node);
int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out);
struct editorDrawerNode *editorDrawerFindChildByName(struct editorDrawerNode *node,
		const char *name, size_t name_len);
int editorDrawerFindVisibleIndexForNode(struct editorDrawerNode *target, int *visible_idx_out);
void editorDrawerClampSelectionAndScroll(int viewport_rows);
struct editorDrawerNode *editorDrawerSelectedTreeNode(void);

int editorDrawerDapVisibleCount(void);
int editorDrawerDapGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out);
int editorDrawerDapExpandSelection(int viewport_rows);
int editorDrawerDapCollapseSelection(int viewport_rows);
int editorDrawerDapSelectedIsDirectory(void);

int editorDrawerLspVisibleCount(void);
int editorDrawerLspGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out);
int editorDrawerLspExpandSelection(int viewport_rows);
int editorDrawerLspCollapseSelection(int viewport_rows);
int editorDrawerLspToggleSelectionExpanded(int viewport_rows);
int editorDrawerLspSelectedIsDirectory(void);

#endif
