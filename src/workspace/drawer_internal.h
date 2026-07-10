#ifndef ROTIDE_WORKSPACE_DRAWER_INTERNAL_H
#define ROTIDE_WORKSPACE_DRAWER_INTERNAL_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

struct editorDrawerEntryView;

struct editorDrawerNode {
	char *name;
	char *path;
	int is_dir;
	int is_expanded;
	int scanned;
	int scan_error;
	/* Directory identity captured at scan time so reconciliation can skip a
	 * re-readdir when the directory itself is unchanged. */
	int scan_state_known;
	dev_t scan_dev;
	ino_t scan_ino;
	struct timespec scan_mtime;
	struct timespec scan_ctime;
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
int editorDrawerReconcile(struct editorDrawerNode *node);
int editorDrawerCountVisibleFromNode(struct editorDrawerNode *node);
int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out);
struct editorDrawerNode *editorDrawerFindChildByName(struct editorDrawerNode *node,
                                                     const char *name, size_t name_len);
struct editorDrawerNode *editorDrawerFindNodeByPath(const char *abs_path);
int editorDrawerFindVisibleIndexForNode(struct editorDrawerNode *target, int *visible_idx_out);
void editorDrawerClampSelectionAndScroll(int viewport_rows);
struct editorDrawerNode *editorDrawerSelectedTreeNode(void);

int editorDrawerDapVisibleCount(void);
int editorDrawerDapVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out);
int editorDrawerDapExpandSelection(int viewport_rows);
int editorDrawerDapCollapseSelection(int viewport_rows);
int editorDrawerDapToggleSelectionExpanded(int viewport_rows);
int editorDrawerDapSelectedIsDirectory(void);

int editorDrawerLspVisibleCount(void);
int editorDrawerLspVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out);
int editorDrawerLspExpandSelection(int viewport_rows);
int editorDrawerLspCollapseSelection(int viewport_rows);
int editorDrawerLspToggleSelectionExpanded(int viewport_rows);
int editorDrawerLspSelectedIsDirectory(void);

int editorDrawerMenuVisibleCount(void);
int editorDrawerMenuVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out);
int editorDrawerMenuExpandSelection(int viewport_rows);
int editorDrawerMenuCollapseSelection(int viewport_rows);
int editorDrawerMenuToggleSelectionExpanded(int viewport_rows);
int editorDrawerMenuSelectedIsDirectory(void);

int editorDrawerGitVisibleCount(void);
int editorDrawerGitVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out);
int editorDrawerGitExpandSelection(int viewport_rows);
int editorDrawerGitCollapseSelection(int viewport_rows);
int editorDrawerGitToggleSelectionExpanded(int viewport_rows);
int editorDrawerGitSelectedIsDirectory(void);

#endif
