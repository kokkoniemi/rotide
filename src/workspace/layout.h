#ifndef ROTIDE_LAYOUT_H
#define ROTIDE_LAYOUT_H

#include <stddef.h>

/*
 * Pane tree for the editor area.
 *
 * The tree describes how the editor viewport (the rectangle left of the
 * drawer, between the tab bar and the status/message bars) is subdivided
 * into leaf panes. Phase 1 only supports a single leaf of kind EDITOR, so
 * the tree is always one node and every layout computation returns a
 * single rect equal to the viewport. The data model is in place for the
 * later phases that introduce splits and additional pane kinds.
 */

enum editorPaneKind {
	EDITOR_PANE_KIND_EDITOR = 0
};

enum editorSplitOrientation {
	EDITOR_SPLIT_HORIZONTAL = 0,
	EDITOR_SPLIT_VERTICAL
};

struct editorPane {
	enum editorPaneKind kind;
};

struct editorPaneNode {
	int is_split;
	union {
		struct editorPane leaf;
		struct {
			enum editorSplitOrientation orientation;
			double ratio;
			struct editorPaneNode *first;
			struct editorPaneNode *second;
		} split;
	} as;
};

struct editorRect {
	int x;
	int y;
	int w;
	int h;
};

struct editorLeafRect {
	struct editorPaneNode *node;
	struct editorRect rect;
};

struct editorLeafLayout {
	struct editorLeafRect *rects;
	int count;
	int capacity;
};

struct editorPaneNode *editorPaneNodeNewLeaf(enum editorPaneKind kind);
void editorPaneNodeFree(struct editorPaneNode *node);

int editorPaneNodeIsLeaf(const struct editorPaneNode *node);

struct editorPaneNode *editorPaneNodeFirstLeaf(struct editorPaneNode *node);
int editorPaneNodeContainsLeaf(const struct editorPaneNode *node,
		const struct editorPaneNode *leaf);

int editorLayoutComputeInto(const struct editorPaneNode *root,
		struct editorRect viewport,
		struct editorLeafLayout *out);
void editorLeafLayoutFree(struct editorLeafLayout *out);

struct editorPaneNode *editorLayoutLeafAt(struct editorLeafLayout *layout,
		int x, int y);

/*
 * Walks the tree and writes the rect of `leaf` into `*out`. Returns 1 if the
 * leaf is found under `root`, else 0. Does not allocate. Useful when the
 * caller only needs one leaf's rect and wants to avoid materializing the full
 * leaf-layout array.
 */
int editorLayoutLeafRect(const struct editorPaneNode *root,
		struct editorRect viewport,
		const struct editorPaneNode *leaf,
		struct editorRect *out);

/*
 * Glue helpers that read the current editor state (E) to derive the editor
 * viewport rect (the rectangle the layout tree subdivides) and the focused
 * leaf's rect within it. These are the integration points used by render and
 * input dispatch. Both return 0 if the precondition is missing (no layout
 * root, no focused leaf, zero-sized window). Outputs are 0-based screen
 * coordinates with y=0 at the tab bar row.
 */
int editorLayoutEditorViewport(struct editorRect *out);
int editorLayoutFocusedLeafRect(struct editorRect *out);

#endif
