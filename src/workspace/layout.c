#include "workspace/layout.h"

#include "editing/edit.h"
#include "rotide.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"

#include <stdlib.h>
#include <string.h>

struct editorPaneNode *editorPaneNodeNewLeaf(enum editorPaneKind kind) {
	struct editorPaneNode *node = malloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(*node));
	node->is_split = 0;
	node->as.leaf.kind = kind;
	editorPaneViewInit(&node->as.leaf.view);
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

struct editorPaneNode *editorPaneNodeFirstLeafOfKind(struct editorPaneNode *node,
                                                     enum editorPaneKind kind) {
	if (node == NULL) {
		return NULL;
	}
	if (!node->is_split) {
		return node->as.leaf.kind == kind ? node : NULL;
	}
	struct editorPaneNode *found = editorPaneNodeFirstLeafOfKind(node->as.split.first, kind);
	if (found != NULL) {
		return found;
	}
	return editorPaneNodeFirstLeafOfKind(node->as.split.second, kind);
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
	struct editorPaneNode *found = editorPaneTreeFindParent(root->as.split.first, child);
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
                                               struct editorPaneNode *leaf,
                                               enum editorSplitOrientation orientation,
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
	sibling->as.leaf.view = leaf->as.leaf.view;
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
	struct editorPaneNode *sibling =
	        parent->as.split.first == leaf ? parent->as.split.second : parent->as.split.first;

	struct editorPaneNode *grand = editorPaneTreeFindParent(*root_ptr, parent);
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

static int layoutLeafReserve(struct editorLeafLayout *out, int needed) {
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

static int layoutLeafAppend(struct editorLeafLayout *out, struct editorPaneNode *node,
                            struct editorRect rect) {
	if (!layoutLeafReserve(out, out->count + 1)) {
		return 0;
	}
	out->rects[out->count].node = node;
	out->rects[out->count].rect = rect;
	out->count++;
	return 1;
}

static void layoutSplitRects(const struct editorPaneNode *node, struct editorRect rect,
                             int border_size, struct editorRect *first_rect_out,
                             struct editorRect *second_rect_out);

static int layoutComputeRecursive(const struct editorPaneNode *node, struct editorRect rect,
                                  int border_size, struct editorLeafLayout *out) {
	if (node == NULL) {
		return 0;
	}
	if (!node->is_split) {
		return layoutLeafAppend(out, (struct editorPaneNode *)node, rect);
	}

	struct editorRect first_rect;
	struct editorRect second_rect;
	layoutSplitRects(node, rect, border_size, &first_rect, &second_rect);
	return layoutComputeRecursive(node->as.split.first, first_rect, border_size, out) &&
	       layoutComputeRecursive(node->as.split.second, second_rect, border_size, out);
}

int editorLayoutComputeBorderedInto(const struct editorPaneNode *root, struct editorRect viewport,
                                    int border_size, struct editorLeafLayout *out) {
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
	if (border_size < 0) {
		border_size = 0;
	}
	if (root == NULL) {
		return 1;
	}
	return layoutComputeRecursive(root, viewport, border_size, out);
}

int editorLayoutComputeInto(const struct editorPaneNode *root, struct editorRect viewport,
                            struct editorLeafLayout *out) {
	return editorLayoutComputeBorderedInto(root, viewport, 0, out);
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

static int layoutRectContains(struct editorRect rect, int x, int y) {
	return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

struct editorPaneNode *editorLayoutLeafAt(struct editorLeafLayout *layout, int x, int y) {
	if (layout == NULL) {
		return NULL;
	}
	for (int i = 0; i < layout->count; i++) {
		if (layoutRectContains(layout->rects[i].rect, x, y)) {
			return layout->rects[i].node;
		}
	}
	return NULL;
}

int editorLayoutPaneTabStripAt(const struct editorLeafLayout *layout, int x, int y,
                               struct editorPaneNode **leaf_out, int *local_col_out,
                               int *strip_cols_out) {
	if (layout == NULL) {
		return 0;
	}
	for (int i = 0; i < layout->count; i++) {
		struct editorPaneNode *leaf = layout->rects[i].node;
		struct editorRect r = layout->rects[i].rect;
		if (leaf == NULL || leaf->is_split ||
		    leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
			continue;
		}
		if (y != r.y - 1 || x < r.x || x >= r.x + r.w) {
			continue;
		}
		if (leaf_out != NULL) {
			*leaf_out = leaf;
		}
		if (local_col_out != NULL) {
			*local_col_out = x - r.x;
		}
		if (strip_cols_out != NULL) {
			*strip_cols_out = r.w;
		}
		return 1;
	}
	return 0;
}

static void layoutSplitRects(const struct editorPaneNode *node, struct editorRect rect,
                             int border_size, struct editorRect *first_rect_out,
                             struct editorRect *second_rect_out) {
	double ratio = node->as.split.ratio;
	if (ratio < 0.0) {
		ratio = 0.0;
	} else if (ratio > 1.0) {
		ratio = 1.0;
	}
	if (border_size < 0) {
		border_size = 0;
	}

	*first_rect_out = rect;
	*second_rect_out = rect;
	if (node->as.split.orientation == EDITOR_SPLIT_VERTICAL) {
		int available = rect.w - border_size;
		if (available < 0) {
			available = 0;
		}
		int first_w = (int)((double)available * ratio);
		if (first_w < 0) {
			first_w = 0;
		}
		if (first_w > available) {
			first_w = available;
		}
		first_rect_out->w = first_w;
		second_rect_out->x = rect.x + first_w + border_size;
		second_rect_out->w = available - first_w;
	} else {
		int available = rect.h - border_size;
		if (available < 0) {
			available = 0;
		}
		int first_h = (int)((double)available * ratio);
		if (first_h < 0) {
			first_h = 0;
		}
		if (first_h > available) {
			first_h = available;
		}
		first_rect_out->h = first_h;
		second_rect_out->y = rect.y + first_h + border_size;
		second_rect_out->h = available - first_h;
	}
}

static int layoutLeafRectRecursive(const struct editorPaneNode *node, struct editorRect rect,
                                   int border_size, const struct editorPaneNode *leaf,
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
	layoutSplitRects(node, rect, border_size, &first_rect, &second_rect);
	return layoutLeafRectRecursive(node->as.split.first, first_rect, border_size, leaf, out) ||
	       layoutLeafRectRecursive(node->as.split.second, second_rect, border_size, leaf, out);
}

int editorLayoutLeafRectBordered(const struct editorPaneNode *root, struct editorRect viewport,
                                 int border_size, const struct editorPaneNode *leaf,
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
	if (border_size < 0) {
		border_size = 0;
	}
	return layoutLeafRectRecursive(root, viewport, border_size, leaf, out);
}

static int layoutBorderListReserve(struct editorBorderList *list, int needed) {
	if (list == NULL || needed < 0) {
		return 0;
	}
	if (needed <= list->capacity) {
		return 1;
	}
	int new_capacity = list->capacity > 0 ? list->capacity : 4;
	while (new_capacity < needed) {
		if (new_capacity > 1 << 20) {
			return 0;
		}
		new_capacity *= 2;
	}
	struct editorBorderRect *grown =
	        realloc(list->rects, (size_t)new_capacity * sizeof(*list->rects));
	if (grown == NULL) {
		return 0;
	}
	list->rects = grown;
	list->capacity = new_capacity;
	return 1;
}

static int layoutBorderListAppend(struct editorBorderList *list, struct editorBorderRect br) {
	if (!layoutBorderListReserve(list, list->count + 1)) {
		return 0;
	}
	list->rects[list->count++] = br;
	return 1;
}

static int layoutCollectBordersRecursive(const struct editorPaneNode *node, struct editorRect rect,
                                         int border_size, struct editorBorderList *out) {
	if (node == NULL || !node->is_split || border_size <= 0) {
		return 1;
	}
	struct editorRect first_rect;
	struct editorRect second_rect;
	layoutSplitRects(node, rect, border_size, &first_rect, &second_rect);

	struct editorBorderRect br = {0};
	br.orientation = node->as.split.orientation;
	if (node->as.split.orientation == EDITOR_SPLIT_VERTICAL) {
		br.rect.x = first_rect.x + first_rect.w;
		br.rect.y = rect.y;
		br.rect.w = border_size;
		br.rect.h = rect.h;
	} else {
		br.rect.x = rect.x;
		br.rect.y = first_rect.y + first_rect.h;
		br.rect.w = rect.w;
		br.rect.h = border_size;
	}
	if (!layoutBorderListAppend(out, br)) {
		return 0;
	}
	return layoutCollectBordersRecursive(node->as.split.first, first_rect, border_size, out) &&
	       layoutCollectBordersRecursive(node->as.split.second, second_rect, border_size, out);
}

int editorLayoutCollectBorders(const struct editorPaneNode *root, struct editorRect viewport,
                               int border_size, struct editorBorderList *out) {
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
	if (border_size <= 0 || root == NULL) {
		return 1;
	}
	return layoutCollectBordersRecursive(root, viewport, border_size, out);
}

void editorBorderListFree(struct editorBorderList *list) {
	if (list == NULL) {
		return;
	}
	free(list->rects);
	list->rects = NULL;
	list->count = 0;
	list->capacity = 0;
}

static int layoutBorderAtRecursive(const struct editorPaneNode *node, struct editorRect rect,
                                   int border_size, int x, int y,
                                   const struct editorPaneNode **out_node,
                                   enum editorSplitOrientation *out_orientation) {
	if (node == NULL || !node->is_split || border_size <= 0) {
		return 0;
	}
	struct editorRect first_rect;
	struct editorRect second_rect;
	layoutSplitRects(node, rect, border_size, &first_rect, &second_rect);

	if (layoutBorderAtRecursive(node->as.split.first, first_rect, border_size, x, y, out_node,
	                            out_orientation)) {
		return 1;
	}
	if (layoutBorderAtRecursive(node->as.split.second, second_rect, border_size, x, y, out_node,
	                            out_orientation)) {
		return 1;
	}

	struct editorRect border;
	if (node->as.split.orientation == EDITOR_SPLIT_VERTICAL) {
		border.x = first_rect.x + first_rect.w;
		border.y = rect.y;
		border.w = border_size;
		border.h = rect.h;
		if (border.y > 0) {
			border.y--;
			border.h++;
		}
	} else {
		border.x = rect.x;
		border.y = first_rect.y + first_rect.h;
		border.w = rect.w;
		border.h = border_size;
	}
	if (layoutRectContains(border, x, y)) {
		*out_node = node;
		*out_orientation = node->as.split.orientation;
		return 1;
	}
	return 0;
}

int editorLayoutBorderAt(const struct editorPaneNode *root, struct editorRect viewport,
                         int border_size, int x, int y, struct editorPaneNode **out_node,
                         enum editorSplitOrientation *out_orientation) {
	if (out_node == NULL || out_orientation == NULL || root == NULL || border_size <= 0) {
		return 0;
	}
	if (viewport.w < 0) {
		viewport.w = 0;
	}
	if (viewport.h < 0) {
		viewport.h = 0;
	}
	const struct editorPaneNode *found = NULL;
	enum editorSplitOrientation found_orientation = EDITOR_SPLIT_HORIZONTAL;
	if (!layoutBorderAtRecursive(root, viewport, border_size, x, y, &found,
	                             &found_orientation)) {
		return 0;
	}
	*out_node = (struct editorPaneNode *)found;
	*out_orientation = found_orientation;
	return 1;
}

static int layoutSplitNodeRectRecursive(const struct editorPaneNode *node, struct editorRect rect,
                                        int border_size, const struct editorPaneNode *target,
                                        struct editorRect *out) {
	if (node == NULL) {
		return 0;
	}
	if (node == target) {
		*out = rect;
		return 1;
	}
	if (!node->is_split) {
		return 0;
	}
	struct editorRect first_rect;
	struct editorRect second_rect;
	layoutSplitRects(node, rect, border_size, &first_rect, &second_rect);
	return layoutSplitNodeRectRecursive(node->as.split.first, first_rect, border_size, target,
	                                    out) ||
	       layoutSplitNodeRectRecursive(node->as.split.second, second_rect, border_size, target,
	                                    out);
}

int editorLayoutSplitNodeRect(const struct editorPaneNode *root, struct editorRect viewport,
                              int border_size, const struct editorPaneNode *node,
                              struct editorRect *out) {
	if (out == NULL || root == NULL || node == NULL) {
		return 0;
	}
	if (viewport.w < 0) {
		viewport.w = 0;
	}
	if (viewport.h < 0) {
		viewport.h = 0;
	}
	if (border_size < 0) {
		border_size = 0;
	}
	return layoutSplitNodeRectRecursive(root, viewport, border_size, node, out);
}

int editorLayoutLeafRect(const struct editorPaneNode *root, struct editorRect viewport,
                         const struct editorPaneNode *leaf, struct editorRect *out) {
	return editorLayoutLeafRectBordered(root, viewport, 0, leaf, out);
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
	return editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                    E.focused_leaf, out);
}

void editorPaneViewInit(struct editorPaneView *view) {
	if (view == NULL) {
		return;
	}
	memset(view, 0, sizeof(*view));
	view->active_tab_idx = -1;
	view->tab_view_start = 0;
	view->pane_tab_count = 0;
	view->mru_tab_count = 0;
	view->preview_tab_idx = -1;
}

void editorPaneViewClearTabs(struct editorPaneView *view) {
	if (view == NULL) {
		return;
	}
	view->pane_tab_count = 0;
	view->mru_tab_count = 0;
	view->active_tab_idx = -1;
	view->tab_view_start = 0;
	view->preview_tab_idx = -1;
}

static int layoutPaneViewMruIndexOfTab(const struct editorPaneView *view, int tab_idx) {
	if (view == NULL) {
		return -1;
	}
	for (int i = 0; i < view->mru_tab_count; i++) {
		if (view->mru_tabs[i] == tab_idx) {
			return i;
		}
	}
	return -1;
}

static int layoutPaneViewAppendMruTab(struct editorPaneView *view, int tab_idx) {
	if (view == NULL || tab_idx < 0) {
		return 0;
	}
	if (layoutPaneViewMruIndexOfTab(view, tab_idx) >= 0) {
		return 1;
	}
	if (view->mru_tab_count >= ROTIDE_PANE_MAX_TABS) {
		return 0;
	}
	view->mru_tabs[view->mru_tab_count++] = tab_idx;
	return 1;
}

static void layoutPaneViewRemoveMruTab(struct editorPaneView *view, int tab_idx) {
	if (view == NULL) {
		return;
	}
	int idx = layoutPaneViewMruIndexOfTab(view, tab_idx);
	if (idx < 0) {
		return;
	}
	for (int i = idx; i < view->mru_tab_count - 1; i++) {
		view->mru_tabs[i] = view->mru_tabs[i + 1];
	}
	view->mru_tab_count--;
}

int editorPaneViewAddTab(struct editorPaneView *view, int tab_idx) {
	if (view == NULL || tab_idx < 0) {
		return 0;
	}
	for (int i = 0; i < view->pane_tab_count; i++) {
		if (view->pane_tabs[i] == tab_idx) {
			return layoutPaneViewAppendMruTab(view, tab_idx);
		}
	}
	if (view->pane_tab_count >= ROTIDE_PANE_MAX_TABS ||
	    view->mru_tab_count >= ROTIDE_PANE_MAX_TABS) {
		return 0;
	}
	view->pane_tabs[view->pane_tab_count++] = tab_idx;
	view->mru_tabs[view->mru_tab_count++] = tab_idx;
	return 1;
}

int editorPaneViewInsertTabAt(struct editorPaneView *view, int tab_idx, int slot) {
	if (view == NULL || tab_idx < 0) {
		return 0;
	}
	int existing = editorPaneViewIndexOfTab(view, tab_idx);
	if (existing >= 0 && layoutPaneViewMruIndexOfTab(view, tab_idx) < 0 &&
	    view->mru_tab_count >= ROTIDE_PANE_MAX_TABS) {
		return 0;
	}
	if (existing < 0 && view->mru_tab_count >= ROTIDE_PANE_MAX_TABS) {
		return 0;
	}
	if (existing >= 0) {
		for (int i = existing; i < view->pane_tab_count - 1; i++) {
			view->pane_tabs[i] = view->pane_tabs[i + 1];
		}
		view->pane_tab_count--;
		if (!layoutPaneViewAppendMruTab(view, tab_idx)) {
			return 0;
		}
	}
	if (slot < 0) {
		slot = 0;
	}
	if (slot > view->pane_tab_count) {
		slot = view->pane_tab_count;
	}
	if (view->pane_tab_count >= ROTIDE_PANE_MAX_TABS) {
		return 0;
	}
	for (int i = view->pane_tab_count; i > slot; i--) {
		view->pane_tabs[i] = view->pane_tabs[i - 1];
	}
	view->pane_tabs[slot] = tab_idx;
	view->pane_tab_count++;
	if (existing < 0) {
		view->mru_tabs[view->mru_tab_count++] = tab_idx;
	}
	return 1;
}

int editorPaneViewActivateTab(struct editorPaneView *view, int tab_idx) {
	if (!editorPaneViewAddTab(view, tab_idx)) {
		return 0;
	}
	layoutPaneViewRemoveMruTab(view, tab_idx);
	for (int i = view->mru_tab_count; i > 0; i--) {
		view->mru_tabs[i] = view->mru_tabs[i - 1];
	}
	view->mru_tabs[0] = tab_idx;
	view->mru_tab_count++;
	view->active_tab_idx = tab_idx;
	return 1;
}

int editorPaneViewMostRecentTab(const struct editorPaneView *view) {
	if (view == NULL) {
		return -1;
	}
	for (int i = 0; i < view->mru_tab_count; i++) {
		int tab_idx = view->mru_tabs[i];
		if (editorPaneViewHasTab(view, tab_idx)) {
			return tab_idx;
		}
	}
	return -1;
}

void editorPaneViewRemoveTab(struct editorPaneView *view, int tab_idx) {
	if (view == NULL) {
		return;
	}
	layoutPaneViewRemoveMruTab(view, tab_idx);
	for (int i = 0; i < view->pane_tab_count; i++) {
		if (view->pane_tabs[i] != tab_idx) {
			continue;
		}
		for (int j = i; j < view->pane_tab_count - 1; j++) {
			view->pane_tabs[j] = view->pane_tabs[j + 1];
		}
		view->pane_tab_count--;
		if (view->active_tab_idx == tab_idx) {
			view->active_tab_idx = -1;
		}
		if (view->preview_tab_idx == tab_idx) {
			view->preview_tab_idx = -1;
		}
		return;
	}
}

int editorPaneViewHasTab(const struct editorPaneView *view, int tab_idx) {
	if (view == NULL || tab_idx < 0) {
		return 0;
	}
	for (int i = 0; i < view->pane_tab_count; i++) {
		if (view->pane_tabs[i] == tab_idx) {
			return 1;
		}
	}
	return 0;
}

int editorPaneViewIndexOfTab(const struct editorPaneView *view, int tab_idx) {
	if (view == NULL) {
		return -1;
	}
	for (int i = 0; i < view->pane_tab_count; i++) {
		if (view->pane_tabs[i] == tab_idx) {
			return i;
		}
	}
	return -1;
}

void editorPaneViewShiftTabIndicesAfterClose(struct editorPaneView *view, int removed_idx) {
	if (view == NULL || removed_idx < 0) {
		return;
	}
	for (int i = 0; i < view->pane_tab_count; i++) {
		if (view->pane_tabs[i] > removed_idx) {
			view->pane_tabs[i]--;
		}
	}
	for (int i = 0; i < view->mru_tab_count;) {
		if (view->mru_tabs[i] == removed_idx) {
			for (int j = i; j < view->mru_tab_count - 1; j++) {
				view->mru_tabs[j] = view->mru_tabs[j + 1];
			}
			view->mru_tab_count--;
			continue;
		}
		if (view->mru_tabs[i] > removed_idx) {
			view->mru_tabs[i]--;
		}
		i++;
	}
	if (view->active_tab_idx == removed_idx) {
		view->active_tab_idx = -1;
	}
	if (view->active_tab_idx > removed_idx) {
		view->active_tab_idx--;
	}
	if (view->preview_tab_idx == removed_idx) {
		view->preview_tab_idx = -1;
	} else if (view->preview_tab_idx > removed_idx) {
		view->preview_tab_idx--;
	}
}

int editorPaneTreeAnyPaneHasTab(const struct editorPaneNode *root, int tab_idx) {
	if (root == NULL) {
		return 0;
	}
	if (root->is_split) {
		return editorPaneTreeAnyPaneHasTab(root->as.split.first, tab_idx) ||
		       editorPaneTreeAnyPaneHasTab(root->as.split.second, tab_idx);
	}
	return editorPaneViewHasTab(&root->as.leaf.view, tab_idx);
}

void editorPaneTreeShiftTabIndicesAfterClose(struct editorPaneNode *root, int removed_idx) {
	if (root == NULL) {
		return;
	}
	if (root->is_split) {
		editorPaneTreeShiftTabIndicesAfterClose(root->as.split.first, removed_idx);
		editorPaneTreeShiftTabIndicesAfterClose(root->as.split.second, removed_idx);
		return;
	}
	editorPaneViewShiftTabIndicesAfterClose(&root->as.leaf.view, removed_idx);
}

void editorPaneTreeClearPreviewTab(struct editorPaneNode *root, int tab_idx) {
	if (root == NULL) {
		return;
	}
	if (root->is_split) {
		editorPaneTreeClearPreviewTab(root->as.split.first, tab_idx);
		editorPaneTreeClearPreviewTab(root->as.split.second, tab_idx);
		return;
	}
	if (root->as.leaf.view.preview_tab_idx == tab_idx) {
		root->as.leaf.view.preview_tab_idx = -1;
	}
}

int editorPaneTreeAnyPanePreviewsTab(const struct editorPaneNode *root, int tab_idx) {
	if (root == NULL || tab_idx < 0) {
		return 0;
	}
	if (root->is_split) {
		return editorPaneTreeAnyPanePreviewsTab(root->as.split.first, tab_idx) ||
		       editorPaneTreeAnyPanePreviewsTab(root->as.split.second, tab_idx);
	}
	return root->as.leaf.view.preview_tab_idx == tab_idx;
}

void editorPaneViewCaptureFromState(struct editorPaneView *view) {
	if (view == NULL) {
		return;
	}
	view->active_tab_idx = E.active_tab;
	view->cx = E.cx;
	view->cy = E.cy;
	view->rx = E.rx;
	view->rowoff = E.rowoff;
	view->coloff = E.coloff;
	view->wrapoff = E.wrapoff;
	view->cursor_offset = E.cursor_offset;
	view->viewport_mode = (int)E.viewport_mode;
	view->selection_mode_active = E.selection_mode_active;
	view->selection_anchor_offset = E.selection_anchor_offset;
	view->column_select_active = E.column_select_active;
	view->column_select_anchor_cy = E.column_select_anchor_cy;
	view->column_select_anchor_rx = E.column_select_anchor_rx;
	view->column_select_cursor_rx = E.column_select_cursor_rx;
}

int editorPaneViewLoadIntoState(const struct editorPaneView *view) {
	if (view == NULL || view->active_tab_idx < 0) {
		return 0;
	}
	E.cx = view->cx;
	E.cy = view->cy;
	E.rx = view->rx;
	E.rowoff = view->rowoff;
	E.coloff = view->coloff;
	E.wrapoff = view->wrapoff;
	E.cursor_offset = view->cursor_offset;
	E.viewport_mode = (enum editorViewportMode)view->viewport_mode;
	E.selection_mode_active = view->selection_mode_active;
	E.selection_anchor_offset = view->selection_anchor_offset;
	E.column_select_active = view->column_select_active;
	E.column_select_anchor_cy = view->column_select_anchor_cy;
	E.column_select_anchor_rx = view->column_select_anchor_rx;
	E.column_select_cursor_rx = view->column_select_cursor_rx;
	return 1;
}

int editorLayoutSetFocusedLeaf(struct editorPaneNode *new_leaf) {
	if (new_leaf == NULL || new_leaf->is_split) {
		return 0;
	}
	if (E.layout_root == NULL || !editorPaneNodeContainsLeaf(E.layout_root, new_leaf)) {
		return 0;
	}
	if (E.focused_leaf == new_leaf) {
		return 1;
	}
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split) {
		editorPaneViewCaptureFromState(&E.focused_leaf->as.leaf.view);
	}
	E.focused_leaf = new_leaf;
	int target_tab = new_leaf->as.leaf.view.active_tab_idx;
	if (target_tab >= 0 && target_tab != E.active_tab) {
		(void)editorTabSwitchToIndex(target_tab);
	} else if (target_tab >= 0) {
		(void)editorPaneViewActivateTab(&new_leaf->as.leaf.view, target_tab);
	}
	(void)editorPaneViewLoadIntoState(&new_leaf->as.leaf.view);
	return 1;
}

struct editorPaneNode *editorLayoutSplitFocused(enum editorSplitOrientation orientation,
                                                double ratio) {
	if (E.layout_root == NULL || E.focused_leaf == NULL || E.focused_leaf->is_split) {
		return NULL;
	}
	/* Snapshot current cursor/scroll into the soon-to-be-split leaf so the
	 * fresh sibling inherits the live view (not a stale cache). */
	editorPaneViewCaptureFromState(&E.focused_leaf->as.leaf.view);
	struct editorPaneNode *sibling =
	        editorPaneTreeSplitLeaf(&E.layout_root, E.focused_leaf, orientation, ratio);
	if (sibling == NULL) {
		return NULL;
	}
	/* VSCode-style "open current file in new pane": the new pane shows
	 * ONLY the splitting pane's active tab. Reset the membership list
	 * the sibling inherited and re-seed it with just that one tab. */
	if (!sibling->is_split) {
		int tab_idx = sibling->as.leaf.view.active_tab_idx;
		/* Carry the splitting pane's preview status for this tab so the new
		 * pane also shows it as a preview; pinning stays per-pane. */
		int was_preview = sibling->as.leaf.view.preview_tab_idx == tab_idx;
		editorPaneViewClearTabs(&sibling->as.leaf.view);
		if (tab_idx >= 0) {
			(void)editorPaneViewActivateTab(&sibling->as.leaf.view, tab_idx);
			if (was_preview) {
				sibling->as.leaf.view.preview_tab_idx = tab_idx;
			}
		}
	}
	E.focused_leaf = sibling;
	return sibling;
}

struct editorPaneNode *editorLayoutCloseFocused(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL || E.focused_leaf->is_split) {
		return NULL;
	}
	/* A close mutates the tree (frees the parent split node), so any
	 * in-progress mouse-drag of a split border must be abandoned — its
	 * cached pointer may be about to dangle. */
	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(&E.layout_root, E.focused_leaf);
	if (new_focus == NULL) {
		return NULL;
	}
	E.focused_leaf = new_focus;
	(void)editorPaneViewLoadIntoState(&new_focus->as.leaf.view);
	return new_focus;
}

static struct editorPaneNode *layoutFirstNonFocusedLeaf(struct editorPaneNode *node) {
	if (node == NULL) {
		return NULL;
	}
	if (!node->is_split) {
		return node == E.focused_leaf ? NULL : node;
	}
	struct editorPaneNode *found = layoutFirstNonFocusedLeaf(node->as.split.first);
	if (found != NULL) {
		return found;
	}
	return layoutFirstNonFocusedLeaf(node->as.split.second);
}

int editorLayoutCloseOthers(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL || E.focused_leaf->is_split) {
		return 0;
	}
	if (!editorPaneNodeContainsLeaf(E.layout_root, E.focused_leaf)) {
		return 0;
	}
	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	int closed = 0;
	while (editorPaneTreeLeafCount(E.layout_root) > 1) {
		struct editorPaneNode *victim = layoutFirstNonFocusedLeaf(E.layout_root);
		if (victim == NULL) {
			break;
		}
		if (editorPaneTreeCloseLeaf(&E.layout_root, victim) == NULL) {
			break;
		}
		closed = 1;
	}
	(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
	return closed;
}

static int layoutRangesOverlap(int a_start, int a_len, int b_start, int b_len) {
	int a_end = a_start + a_len;
	int b_end = b_start + b_len;
	return a_start < b_end && b_start < a_end;
}

struct editorPaneNode *editorLayoutFindNeighborLeaf(const struct editorLeafLayout *layout,
                                                    const struct editorPaneNode *from_leaf,
                                                    enum editorFocusDirection direction) {
	if (layout == NULL || from_leaf == NULL) {
		return NULL;
	}
	struct editorRect source = {0};
	int found_source = 0;
	for (int i = 0; i < layout->count; i++) {
		if (layout->rects[i].node == from_leaf) {
			source = layout->rects[i].rect;
			found_source = 1;
			break;
		}
	}
	if (!found_source) {
		return NULL;
	}

	struct editorPaneNode *best = NULL;
	int best_gap = 0;
	for (int i = 0; i < layout->count; i++) {
		struct editorRect candidate = layout->rects[i].rect;
		if (layout->rects[i].node == from_leaf) {
			continue;
		}
		int gap;
		switch (direction) {
			case EDITOR_FOCUS_LEFT:
				if (candidate.x + candidate.w > source.x) {
					continue;
				}
				if (!layoutRangesOverlap(source.y, source.h, candidate.y,
				                         candidate.h)) {
					continue;
				}
				gap = source.x - (candidate.x + candidate.w);
				break;
			case EDITOR_FOCUS_RIGHT:
				if (candidate.x < source.x + source.w) {
					continue;
				}
				if (!layoutRangesOverlap(source.y, source.h, candidate.y,
				                         candidate.h)) {
					continue;
				}
				gap = candidate.x - (source.x + source.w);
				break;
			case EDITOR_FOCUS_UP:
				if (candidate.y + candidate.h > source.y) {
					continue;
				}
				if (!layoutRangesOverlap(source.x, source.w, candidate.x,
				                         candidate.w)) {
					continue;
				}
				gap = source.y - (candidate.y + candidate.h);
				break;
			case EDITOR_FOCUS_DOWN:
				if (candidate.y < source.y + source.h) {
					continue;
				}
				if (!layoutRangesOverlap(source.x, source.w, candidate.x,
				                         candidate.w)) {
					continue;
				}
				gap = candidate.y - (source.y + source.h);
				break;
			default:
				continue;
		}
		if (best == NULL || gap < best_gap) {
			best = layout->rects[i].node;
			best_gap = gap;
		}
	}
	return best;
}

static int layoutComputeForFocus(struct editorLeafLayout *out) {
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	return editorLayoutComputeBorderedInto(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                       out);
}

int editorLayoutFocusDirection(enum editorFocusDirection direction) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!layoutComputeForFocus(&layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	struct editorPaneNode *neighbor =
	        editorLayoutFindNeighborLeaf(&layout, E.focused_leaf, direction);
	editorLeafLayoutFree(&layout);
	if (neighbor == NULL) {
		return 0;
	}
	return editorLayoutSetFocusedLeaf(neighbor);
}

int editorLayoutFocusNext(int reverse) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!layoutComputeForFocus(&layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	if (layout.count <= 1) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	int idx = -1;
	for (int i = 0; i < layout.count; i++) {
		if (layout.rects[i].node == E.focused_leaf) {
			idx = i;
			break;
		}
	}
	if (idx < 0) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	int next = reverse ? (idx - 1 + layout.count) % layout.count : (idx + 1) % layout.count;
	struct editorPaneNode *target = layout.rects[next].node;
	editorLeafLayoutFree(&layout);
	return editorLayoutSetFocusedLeaf(target);
}

int editorLayoutFocusLeafAt(int x, int y) {
	if (E.layout_root == NULL) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!layoutComputeForFocus(&layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	struct editorPaneNode *hit = editorLayoutLeafAt(&layout, x, y);
	editorLeafLayoutFree(&layout);
	if (hit == NULL || hit == E.focused_leaf) {
		return 0;
	}
	return editorLayoutSetFocusedLeaf(hit);
}

int editorLayoutResizeFocused(int grow) {
	if (E.layout_root == NULL || E.focused_leaf == NULL || E.focused_leaf->is_split) {
		return 0;
	}
	struct editorPaneNode *parent = editorPaneTreeFindParent(E.layout_root, E.focused_leaf);
	if (parent == NULL) {
		return 0;
	}
	double step = ROTIDE_PANE_RESIZE_STEP;
	double signed_step = grow ? step : -step;
	if (parent->as.split.second == E.focused_leaf) {
		signed_step = -signed_step;
	}
	double new_ratio = parent->as.split.ratio + signed_step;
	double min_ratio = ROTIDE_PANE_MIN_RATIO;
	double max_ratio = 1.0 - min_ratio;
	if (new_ratio < min_ratio) {
		new_ratio = min_ratio;
	}
	if (new_ratio > max_ratio) {
		new_ratio = max_ratio;
	}
	if (new_ratio == parent->as.split.ratio) {
		return 0;
	}
	parent->as.split.ratio = new_ratio;
	return 1;
}

int editorLayoutFocusedLeafIndex(int *out_index, int *out_count) {
	if (out_count == NULL) {
		return 0;
	}
	*out_count = editorPaneTreeLeafCount(E.layout_root);
	if (out_index == NULL || E.focused_leaf == NULL) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!layoutComputeForFocus(&layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	int idx = -1;
	for (int i = 0; i < layout.count; i++) {
		if (layout.rects[i].node == E.focused_leaf) {
			idx = i;
			break;
		}
	}
	editorLeafLayoutFree(&layout);
	if (idx < 0) {
		return 0;
	}
	*out_index = idx;
	return 1;
}

void editorPaneAnnounceFocus(void) {
	int count = 0;
	int index = 0;
	if (!editorLayoutFocusedLeafIndex(&index, &count)) {
		return;
	}
	if (count <= 1) {
		return;
	}
	editorSetStatusMsg("Pane %d/%d", index + 1, count);
}

#include <stdio.h>

static size_t layoutSerializeRecursive(const struct editorPaneNode *node, char *out,
                                       size_t out_size, size_t pos) {
	if (node == NULL || pos >= out_size) {
		return 0;
	}
	if (!node->is_split) {
		/* `term` when the pane's active tab is a terminal, so terminals persist
		 * across restore; also for an unhydrated `term` placeholder leaf (kind
		 * TERMINAL), so a save mid-restore round-trips. */
		int is_terminal = node->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL ||
		                  editorPaneActiveKind(node) == EDITOR_PANE_KIND_TERMINAL;
		const char *token = is_terminal ? "term" : "leaf";
		int n = snprintf(out + pos, out_size - pos, "%s", token);
		if (n < 0 || (size_t)n >= out_size - pos) {
			return 0;
		}
		return pos + (size_t)n;
	}
	char kind = node->as.split.orientation == EDITOR_SPLIT_VERTICAL ? 'v' : 'h';
	int n = snprintf(out + pos, out_size - pos, "(%c %.4f ", kind, node->as.split.ratio);
	if (n < 0 || (size_t)n >= out_size - pos) {
		return 0;
	}
	pos += (size_t)n;
	pos = layoutSerializeRecursive(node->as.split.first, out, out_size, pos);
	if (pos == 0 || pos >= out_size) {
		return 0;
	}
	if (pos + 1 >= out_size) {
		return 0;
	}
	out[pos++] = ' ';
	pos = layoutSerializeRecursive(node->as.split.second, out, out_size, pos);
	if (pos == 0 || pos + 1 >= out_size) {
		return 0;
	}
	out[pos++] = ')';
	if (pos < out_size) {
		out[pos] = '\0';
	}
	return pos;
}

size_t editorLayoutSerialize(const struct editorPaneNode *root, char *out, size_t out_size) {
	if (out == NULL || out_size == 0) {
		return 0;
	}
	out[0] = '\0';
	size_t pos = layoutSerializeRecursive(root, out, out_size, 0);
	if (pos >= out_size) {
		return 0;
	}
	out[pos] = '\0';
	return pos;
}

static const char *layoutSkipWhitespace(const char *s) {
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	return s;
}

static struct editorPaneNode *layoutParse(const char **cursor) {
	const char *s = layoutSkipWhitespace(*cursor);
	if (*s == '\0') {
		return NULL;
	}
	if (s[0] == 'l' && s[1] == 'e' && s[2] == 'a' && s[3] == 'f') {
		*cursor = s + 4;
		return editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	}
	if (s[0] == 't' && s[1] == 'e' && s[2] == 'r' && s[3] == 'm') {
		*cursor = s + 4;
		return editorPaneNodeNewLeaf(EDITOR_PANE_KIND_TERMINAL);
	}
	if (*s != '(') {
		return NULL;
	}
	s++;
	char kind = *s;
	if (kind != 'v' && kind != 'h') {
		return NULL;
	}
	s++;
	if (*s != ' ') {
		return NULL;
	}
	s++;
	char *endptr = NULL;
	double ratio = strtod(s, &endptr);
	if (endptr == s) {
		return NULL;
	}
	s = endptr;
	s = layoutSkipWhitespace(s);
	*cursor = s;
	struct editorPaneNode *first = layoutParse(cursor);
	if (first == NULL) {
		return NULL;
	}
	s = layoutSkipWhitespace(*cursor);
	*cursor = s;
	struct editorPaneNode *second = layoutParse(cursor);
	if (second == NULL) {
		editorPaneNodeFree(first);
		return NULL;
	}
	s = layoutSkipWhitespace(*cursor);
	if (*s != ')') {
		editorPaneNodeFree(first);
		editorPaneNodeFree(second);
		return NULL;
	}
	*cursor = s + 1;

	struct editorPaneNode *split = malloc(sizeof(*split));
	if (split == NULL) {
		editorPaneNodeFree(first);
		editorPaneNodeFree(second);
		return NULL;
	}
	memset(split, 0, sizeof(*split));
	split->is_split = 1;
	split->as.split.orientation = kind == 'v' ? EDITOR_SPLIT_VERTICAL : EDITOR_SPLIT_HORIZONTAL;
	split->as.split.ratio = ratio;
	if (split->as.split.ratio < 0.0) {
		split->as.split.ratio = 0.0;
	}
	if (split->as.split.ratio > 1.0) {
		split->as.split.ratio = 1.0;
	}
	split->as.split.first = first;
	split->as.split.second = second;
	return split;
}

struct editorPaneNode *editorLayoutDeserialize(const char *s) {
	if (s == NULL) {
		return NULL;
	}
	const char *cursor = s;
	struct editorPaneNode *root = layoutParse(&cursor);
	if (root == NULL) {
		return NULL;
	}
	cursor = layoutSkipWhitespace(cursor);
	if (*cursor != '\0') {
		editorPaneNodeFree(root);
		return NULL;
	}
	return root;
}
