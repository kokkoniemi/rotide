#ifndef EDITOR_DRAWER_INTERNAL_H
#define EDITOR_DRAWER_INTERNAL_H

#include <stddef.h>

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

void editorDrawerNodeFree(struct editorDrawerNode *node);
int editorDrawerEnsureScanned(struct editorDrawerNode *node);
struct editorDrawerNode *editorDrawerFindChildByName(struct editorDrawerNode *node,
		const char *name, size_t name_len);
int editorDrawerFindVisibleIndexForNode(struct editorDrawerNode *target, int *visible_idx_out);
void editorDrawerClampSelectionAndScroll(int viewport_rows);
struct editorDrawerNode *editorDrawerSelectedTreeNode(void);

#endif
