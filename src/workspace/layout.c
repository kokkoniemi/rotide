#include "workspace/layout.h"

#include <stdlib.h>
#include <string.h>

#include "rotide.h"
#include "editing/edit.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"

struct editorPaneNode *editorPaneNodeNewLeaf(enum editorPaneKind kind) {
	struct editorPaneNode *node = malloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(*node));
	node->is_split = 0;
	node->as.leaf.kind = kind;
	node->as.leaf.kind_state = NULL;
	node->as.leaf.kind_state_free = NULL;
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
	} else {
		if (node->as.leaf.kind_state_free != NULL &&
				node->as.leaf.kind_state != NULL) {
			node->as.leaf.kind_state_free(node->as.leaf.kind_state);
		}
		node->as.leaf.kind_state = NULL;
		node->as.leaf.kind_state_free = NULL;
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
		struct editorRect rect, int border_size,
		struct editorRect *first_rect_out,
		struct editorRect *second_rect_out);

static int editorLayoutComputeRecursive(const struct editorPaneNode *node,
		struct editorRect rect, int border_size,
		struct editorLeafLayout *out) {
	if (node == NULL) {
		return 0;
	}
	if (!node->is_split) {
		return editorLeafLayoutAppend(out,
				(struct editorPaneNode *)node, rect);
	}

	struct editorRect first_rect;
	struct editorRect second_rect;
	editorLayoutSplitRects(node, rect, border_size, &first_rect, &second_rect);
	return editorLayoutComputeRecursive(node->as.split.first, first_rect,
			border_size, out) &&
			editorLayoutComputeRecursive(node->as.split.second, second_rect,
					border_size, out);
}

int editorLayoutComputeBorderedInto(const struct editorPaneNode *root,
		struct editorRect viewport, int border_size,
		struct editorLeafLayout *out) {
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
	return editorLayoutComputeRecursive(root, viewport, border_size, out);
}

int editorLayoutComputeInto(const struct editorPaneNode *root,
		struct editorRect viewport, struct editorLeafLayout *out) {
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
		struct editorRect rect, int border_size,
		struct editorRect *first_rect_out,
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

static int editorLayoutLeafRectRecursive(const struct editorPaneNode *node,
		struct editorRect rect, int border_size,
		const struct editorPaneNode *leaf, struct editorRect *out) {
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
	editorLayoutSplitRects(node, rect, border_size, &first_rect, &second_rect);
	return editorLayoutLeafRectRecursive(node->as.split.first, first_rect,
			border_size, leaf, out) ||
			editorLayoutLeafRectRecursive(node->as.split.second, second_rect,
					border_size, leaf, out);
}

int editorLayoutLeafRectBordered(const struct editorPaneNode *root,
		struct editorRect viewport, int border_size,
		const struct editorPaneNode *leaf, struct editorRect *out) {
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
	return editorLayoutLeafRectRecursive(root, viewport, border_size, leaf, out);
}

static int editorBorderListReserve(struct editorBorderList *list, int needed) {
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

static int editorBorderListAppend(struct editorBorderList *list,
		struct editorBorderRect br) {
	if (!editorBorderListReserve(list, list->count + 1)) {
		return 0;
	}
	list->rects[list->count++] = br;
	return 1;
}

static int editorLayoutCollectBordersRecursive(const struct editorPaneNode *node,
		struct editorRect rect, int border_size,
		struct editorBorderList *out) {
	if (node == NULL || !node->is_split || border_size <= 0) {
		return 1;
	}
	struct editorRect first_rect;
	struct editorRect second_rect;
	editorLayoutSplitRects(node, rect, border_size, &first_rect, &second_rect);

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
	if (!editorBorderListAppend(out, br)) {
		return 0;
	}
	return editorLayoutCollectBordersRecursive(node->as.split.first, first_rect,
			border_size, out) &&
			editorLayoutCollectBordersRecursive(node->as.split.second, second_rect,
					border_size, out);
}

int editorLayoutCollectBorders(const struct editorPaneNode *root,
		struct editorRect viewport, int border_size,
		struct editorBorderList *out) {
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
	return editorLayoutCollectBordersRecursive(root, viewport, border_size, out);
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

int editorLayoutLeafRect(const struct editorPaneNode *root,
		struct editorRect viewport, const struct editorPaneNode *leaf,
		struct editorRect *out) {
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
	return editorLayoutLeafRectBordered(E.layout_root, viewport,
			ROTIDE_PANE_BORDER_SIZE, E.focused_leaf, out);
}

void editorPaneViewInit(struct editorPaneView *view) {
	if (view == NULL) {
		return;
	}
	memset(view, 0, sizeof(*view));
	view->active_tab_idx = -1;
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
	}
	(void)editorPaneViewLoadIntoState(&new_leaf->as.leaf.view);
	return 1;
}

struct editorPaneNode *editorLayoutSplitFocused(
		enum editorSplitOrientation orientation, double ratio) {
	if (E.layout_root == NULL || E.focused_leaf == NULL ||
			E.focused_leaf->is_split) {
		return NULL;
	}
	/* Snapshot current cursor/scroll into the soon-to-be-split leaf so the
	 * fresh sibling inherits the live view (not a stale cache). */
	editorPaneViewCaptureFromState(&E.focused_leaf->as.leaf.view);
	struct editorPaneNode *sibling = editorPaneTreeSplitLeaf(&E.layout_root,
			E.focused_leaf, orientation, ratio);
	if (sibling == NULL) {
		return NULL;
	}
	E.focused_leaf = sibling;
	return sibling;
}

struct editorPaneNode *editorLayoutCloseFocused(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL ||
			E.focused_leaf->is_split) {
		return NULL;
	}
	struct editorPaneNode *new_focus =
			editorPaneTreeCloseLeaf(&E.layout_root, E.focused_leaf);
	if (new_focus == NULL) {
		return NULL;
	}
	E.focused_leaf = new_focus;
	(void)editorPaneViewLoadIntoState(&new_focus->as.leaf.view);
	return new_focus;
}

static int editorRangesOverlap(int a_start, int a_len, int b_start, int b_len) {
	int a_end = a_start + a_len;
	int b_end = b_start + b_len;
	return a_start < b_end && b_start < a_end;
}

struct editorPaneNode *editorLayoutFindNeighborLeaf(
		const struct editorLeafLayout *layout,
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
			if (!editorRangesOverlap(source.y, source.h, candidate.y,
					candidate.h)) {
				continue;
			}
			gap = source.x - (candidate.x + candidate.w);
			break;
		case EDITOR_FOCUS_RIGHT:
			if (candidate.x < source.x + source.w) {
				continue;
			}
			if (!editorRangesOverlap(source.y, source.h, candidate.y,
					candidate.h)) {
				continue;
			}
			gap = candidate.x - (source.x + source.w);
			break;
		case EDITOR_FOCUS_UP:
			if (candidate.y + candidate.h > source.y) {
				continue;
			}
			if (!editorRangesOverlap(source.x, source.w, candidate.x,
					candidate.w)) {
				continue;
			}
			gap = source.y - (candidate.y + candidate.h);
			break;
		case EDITOR_FOCUS_DOWN:
			if (candidate.y < source.y + source.h) {
				continue;
			}
			if (!editorRangesOverlap(source.x, source.w, candidate.x,
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

static int editorLayoutComputeForFocus(struct editorLeafLayout *out) {
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	return editorLayoutComputeBorderedInto(E.layout_root, viewport,
			ROTIDE_PANE_BORDER_SIZE, out);
}

int editorLayoutFocusDirection(enum editorFocusDirection direction) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeForFocus(&layout)) {
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

int editorLayoutFocusLeafAt(int x, int y) {
	if (E.layout_root == NULL) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeForFocus(&layout)) {
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
	if (E.layout_root == NULL || E.focused_leaf == NULL ||
			E.focused_leaf->is_split) {
		return 0;
	}
	struct editorPaneNode *parent =
			editorPaneTreeFindParent(E.layout_root, E.focused_leaf);
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
	if (!editorLayoutComputeForFocus(&layout)) {
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
