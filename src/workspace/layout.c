#include "workspace/layout.h"

#include <stdlib.h>
#include <string.h>

#include "rotide.h"
#include "workspace/drawer.h"

struct editorPaneNode *editorPaneNodeNewLeaf(enum editorPaneKind kind) {
	struct editorPaneNode *node = malloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(*node));
	node->is_split = 0;
	node->as.leaf.kind = kind;
	return node;
}

void editorPaneNodeFree(struct editorPaneNode *node) {
	if (node == NULL) {
		return;
	}
	if (node->is_split) {
		editorPaneNodeFree(node->as.split.first);
		editorPaneNodeFree(node->as.split.second);
	}
	free(node);
}

int editorPaneNodeIsLeaf(const struct editorPaneNode *node) {
	return node != NULL && !node->is_split;
}

struct editorPaneNode *editorPaneNodeFirstLeaf(struct editorPaneNode *node) {
	if (node == NULL) {
		return NULL;
	}
	while (node->is_split) {
		node = node->as.split.first;
		if (node == NULL) {
			return NULL;
		}
	}
	return node;
}

int editorPaneNodeContainsLeaf(const struct editorPaneNode *node,
		const struct editorPaneNode *leaf) {
	if (node == NULL || leaf == NULL) {
		return 0;
	}
	if (!node->is_split) {
		return node == leaf;
	}
	return editorPaneNodeContainsLeaf(node->as.split.first, leaf) ||
			editorPaneNodeContainsLeaf(node->as.split.second, leaf);
}

struct editorPaneNode *editorPaneTreeFindParent(struct editorPaneNode *root,
		const struct editorPaneNode *child) {
	if (root == NULL || child == NULL || !root->is_split) {
		return NULL;
	}
	if (root->as.split.first == child || root->as.split.second == child) {
		return root;
	}
	struct editorPaneNode *found =
			editorPaneTreeFindParent(root->as.split.first, child);
	if (found != NULL) {
		return found;
	}
	return editorPaneTreeFindParent(root->as.split.second, child);
}

int editorPaneTreeLeafCount(const struct editorPaneNode *root) {
	if (root == NULL) {
		return 0;
	}
	if (!root->is_split) {
		return 1;
	}
	return editorPaneTreeLeafCount(root->as.split.first) +
			editorPaneTreeLeafCount(root->as.split.second);
}

struct editorPaneNode *editorPaneTreeSplitLeaf(struct editorPaneNode **root_ptr,
		struct editorPaneNode *leaf, enum editorSplitOrientation orientation,
		double ratio) {
	if (root_ptr == NULL || *root_ptr == NULL || leaf == NULL || leaf->is_split) {
		return NULL;
	}
	if (!editorPaneNodeContainsLeaf(*root_ptr, leaf)) {
		return NULL;
	}
	struct editorPaneNode *sibling = editorPaneNodeNewLeaf(leaf->as.leaf.kind);
	if (sibling == NULL) {
		return NULL;
	}
	struct editorPaneNode *split = malloc(sizeof(*split));
	if (split == NULL) {
		editorPaneNodeFree(sibling);
		return NULL;
	}
	memset(split, 0, sizeof(*split));
	split->is_split = 1;
	split->as.split.orientation = orientation;
	split->as.split.ratio = ratio < 0.0 ? 0.0 : (ratio > 1.0 ? 1.0 : ratio);
	split->as.split.first = leaf;
	split->as.split.second = sibling;

	struct editorPaneNode *parent = editorPaneTreeFindParent(*root_ptr, leaf);
	if (parent == NULL) {
		*root_ptr = split;
	} else if (parent->as.split.first == leaf) {
		parent->as.split.first = split;
	} else {
		parent->as.split.second = split;
	}
	return sibling;
}

struct editorPaneNode *editorPaneTreeCloseLeaf(struct editorPaneNode **root_ptr,
		struct editorPaneNode *leaf) {
	if (root_ptr == NULL || *root_ptr == NULL || leaf == NULL || leaf->is_split) {
		return NULL;
	}
	if (!editorPaneNodeContainsLeaf(*root_ptr, leaf)) {
		return NULL;
	}
	struct editorPaneNode *parent = editorPaneTreeFindParent(*root_ptr, leaf);
	if (parent == NULL) {
		/* Leaf is the root: close-last-leaf is a no-op. */
		return NULL;
	}
	struct editorPaneNode *sibling = parent->as.split.first == leaf
			? parent->as.split.second
			: parent->as.split.first;

	struct editorPaneNode *grand =
			editorPaneTreeFindParent(*root_ptr, parent);
	if (grand == NULL) {
		*root_ptr = sibling;
	} else if (grand->as.split.first == parent) {
		grand->as.split.first = sibling;
	} else {
		grand->as.split.second = sibling;
	}

	/* Detach so editorPaneNodeFree on parent doesn't recurse into sibling. */
	parent->as.split.first = NULL;
	parent->as.split.second = NULL;
	editorPaneNodeFree(parent);
	editorPaneNodeFree(leaf);
	return editorPaneNodeFirstLeaf(sibling);
}

static int editorLeafLayoutReserve(struct editorLeafLayout *out, int needed) {
	if (out == NULL || needed < 0) {
		return 0;
	}
	if (needed <= out->capacity) {
		return 1;
	}
	int new_capacity = out->capacity > 0 ? out->capacity : 4;
	while (new_capacity < needed) {
		if (new_capacity > 1 << 20) {
			return 0;
		}
		new_capacity *= 2;
	}
	struct editorLeafRect *grown =
			realloc(out->rects, (size_t)new_capacity * sizeof(*out->rects));
	if (grown == NULL) {
		return 0;
	}
	out->rects = grown;
	out->capacity = new_capacity;
	return 1;
}

static int editorLeafLayoutAppend(struct editorLeafLayout *out,
		struct editorPaneNode *node, struct editorRect rect) {
	if (!editorLeafLayoutReserve(out, out->count + 1)) {
		return 0;
	}
	out->rects[out->count].node = node;
	out->rects[out->count].rect = rect;
	out->count++;
	return 1;
}

static void editorLayoutSplitRects(const struct editorPaneNode *node,
		struct editorRect rect,
		struct editorRect *first_rect_out,
		struct editorRect *second_rect_out);

static int editorLayoutComputeRecursive(const struct editorPaneNode *node,
		struct editorRect rect, struct editorLeafLayout *out) {
	if (node == NULL) {
		return 0;
	}
	if (!node->is_split) {
		return editorLeafLayoutAppend(out,
				(struct editorPaneNode *)node, rect);
	}

	struct editorRect first_rect;
	struct editorRect second_rect;
	editorLayoutSplitRects(node, rect, &first_rect, &second_rect);
	return editorLayoutComputeRecursive(node->as.split.first, first_rect, out) &&
			editorLayoutComputeRecursive(node->as.split.second, second_rect, out);
}

int editorLayoutComputeInto(const struct editorPaneNode *root,
		struct editorRect viewport, struct editorLeafLayout *out) {
	if (out == NULL) {
		return 0;
	}
	out->count = 0;
	if (viewport.w < 0) {
		viewport.w = 0;
	}
	if (viewport.h < 0) {
		viewport.h = 0;
	}
	if (root == NULL) {
		return 1;
	}
	return editorLayoutComputeRecursive(root, viewport, out);
}

void editorLeafLayoutFree(struct editorLeafLayout *out) {
	if (out == NULL) {
		return;
	}
	free(out->rects);
	out->rects = NULL;
	out->count = 0;
	out->capacity = 0;
}

static int editorRectContains(struct editorRect rect, int x, int y) {
	return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

struct editorPaneNode *editorLayoutLeafAt(struct editorLeafLayout *layout,
		int x, int y) {
	if (layout == NULL) {
		return NULL;
	}
	for (int i = 0; i < layout->count; i++) {
		if (editorRectContains(layout->rects[i].rect, x, y)) {
			return layout->rects[i].node;
		}
	}
	return NULL;
}

static void editorLayoutSplitRects(const struct editorPaneNode *node,
		struct editorRect rect,
		struct editorRect *first_rect_out,
		struct editorRect *second_rect_out) {
	double ratio = node->as.split.ratio;
	if (ratio < 0.0) {
		ratio = 0.0;
	} else if (ratio > 1.0) {
		ratio = 1.0;
	}

	*first_rect_out = rect;
	*second_rect_out = rect;
	if (node->as.split.orientation == EDITOR_SPLIT_VERTICAL) {
		int first_w = (int)((double)rect.w * ratio);
		if (first_w < 0) {
			first_w = 0;
		}
		if (first_w > rect.w) {
			first_w = rect.w;
		}
		first_rect_out->w = first_w;
		second_rect_out->x = rect.x + first_w;
		second_rect_out->w = rect.w - first_w;
	} else {
		int first_h = (int)((double)rect.h * ratio);
		if (first_h < 0) {
			first_h = 0;
		}
		if (first_h > rect.h) {
			first_h = rect.h;
		}
		first_rect_out->h = first_h;
		second_rect_out->y = rect.y + first_h;
		second_rect_out->h = rect.h - first_h;
	}
}

static int editorLayoutLeafRectRecursive(const struct editorPaneNode *node,
		struct editorRect rect, const struct editorPaneNode *leaf,
		struct editorRect *out) {
	if (node == NULL || leaf == NULL) {
		return 0;
	}
	if (!node->is_split) {
		if (node == leaf) {
			*out = rect;
			return 1;
		}
		return 0;
	}
	struct editorRect first_rect;
	struct editorRect second_rect;
	editorLayoutSplitRects(node, rect, &first_rect, &second_rect);
	return editorLayoutLeafRectRecursive(node->as.split.first, first_rect, leaf, out) ||
			editorLayoutLeafRectRecursive(node->as.split.second, second_rect, leaf, out);
}

int editorLayoutLeafRect(const struct editorPaneNode *root,
		struct editorRect viewport, const struct editorPaneNode *leaf,
		struct editorRect *out) {
	if (out == NULL) {
		return 0;
	}
	if (viewport.w < 0) {
		viewport.w = 0;
	}
	if (viewport.h < 0) {
		viewport.h = 0;
	}
	return editorLayoutLeafRectRecursive(root, viewport, leaf, out);
}

int editorLayoutEditorViewport(struct editorRect *out) {
	if (out == NULL || E.window_cols <= 0 || E.window_rows <= 0) {
		return 0;
	}
	out->x = editorDrawerTextStartColForCols(E.window_cols);
	out->y = 1;
	out->w = editorDrawerTextViewportCols(E.window_cols);
	out->h = E.window_rows;
	return 1;
}

int editorLayoutFocusedLeafRect(struct editorRect *out) {
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 0;
	}
	return editorLayoutLeafRect(E.layout_root, viewport, E.focused_leaf, out);
}
