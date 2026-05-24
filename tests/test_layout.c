#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rects_equal(struct editorRect a, struct editorRect b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static int rects_overlap(struct editorRect a, struct editorRect b) {
	if (a.w == 0 || a.h == 0 || b.w == 0 || b.h == 0) {
		return 0;
	}
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static int test_layout_single_leaf_returns_full_viewport(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorRect viewport = {.x = 4, .y = 1, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 1 || !rects_equal(layout.rects[0].rect, viewport) ||
	             layout.rects[0].node != root;
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_vertical_split_tiles_viewport(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || left == NULL || right == NULL) {
		free(root);
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = left;
	root->as.split.second = right;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 2;
	if (!failed) {
		struct editorRect lr = layout.rects[0].rect;
		struct editorRect rr = layout.rects[1].rect;
		failed = lr.x != 0 || lr.y != 0 || lr.w != 40 || lr.h != 24 || rr.x != 40 ||
		         rr.y != 0 || rr.w != 40 || rr.h != 24 || rects_overlap(lr, rr);
	}
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_horizontal_split_tiles_viewport(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *top = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *bottom = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || top == NULL || bottom == NULL) {
		free(root);
		editorPaneNodeFree(top);
		editorPaneNodeFree(bottom);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_HORIZONTAL;
	root->as.split.ratio = 0.25;
	root->as.split.first = top;
	root->as.split.second = bottom;

	struct editorRect viewport = {.x = 5, .y = 1, .w = 60, .h = 40};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 2;
	if (!failed) {
		struct editorRect tr = layout.rects[0].rect;
		struct editorRect br = layout.rects[1].rect;
		failed = tr.x != 5 || tr.y != 1 || tr.w != 60 || tr.h != 10 || br.x != 5 ||
		         br.y != 11 || br.w != 60 || br.h != 30 || rects_overlap(tr, br);
	}
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_nested_splits_no_gaps_or_overlap(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *right = malloc(sizeof(*root));
	struct editorPaneNode *a = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *b = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *c = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || right == NULL || a == NULL || b == NULL || c == NULL) {
		free(root);
		free(right);
		editorPaneNodeFree(a);
		editorPaneNodeFree(b);
		editorPaneNodeFree(c);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	memset(right, 0, sizeof(*right));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = a;
	root->as.split.second = right;
	right->is_split = 1;
	right->as.split.orientation = EDITOR_SPLIT_HORIZONTAL;
	right->as.split.ratio = 0.5;
	right->as.split.first = b;
	right->as.split.second = c;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 100, .h = 50};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 3;
	long long covered = 0;
	if (!failed) {
		for (int i = 0; i < layout.count && !failed; i++) {
			covered += (long long)layout.rects[i].rect.w *
			           (long long)layout.rects[i].rect.h;
			for (int j = i + 1; j < layout.count && !failed; j++) {
				if (rects_overlap(layout.rects[i].rect, layout.rects[j].rect)) {
					failed = 1;
				}
			}
		}
		if (!failed && covered != 100LL * 50LL) {
			failed = 1;
		}
	}
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_ratio_clamped_to_bounds(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || left == NULL || right == NULL) {
		free(root);
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 1.5;
	root->as.split.first = left;
	root->as.split.second = right;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 2 || layout.rects[0].rect.w != 80 ||
	             layout.rects[1].rect.w != 0;
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_zero_viewport_returns_zero_rects(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 0, .h = 0};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 1 || layout.rects[0].rect.w != 0 ||
	             layout.rects[0].rect.h != 0;
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_leaf_rect_finds_specific_leaf(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || left == NULL || right == NULL) {
		free(root);
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = left;
	root->as.split.second = right;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorRect out = {0};
	int ok = editorLayoutLeafRect(root, viewport, right, &out);
	int failed = !ok || out.x != 40 || out.y != 0 || out.w != 40 || out.h != 24;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_leaf_rect_returns_zero_when_not_found(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *orphan = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || orphan == NULL) {
		editorPaneNodeFree(root);
		editorPaneNodeFree(orphan);
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorRect out = {0};
	int found = editorLayoutLeafRect(root, viewport, orphan, &out);
	editorPaneNodeFree(root);
	editorPaneNodeFree(orphan);
	return found != 0;
}

static int test_layout_first_leaf_descends_to_leftmost(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || left == NULL || right == NULL) {
		free(root);
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = left;
	root->as.split.second = right;

	int failed = editorPaneNodeFirstLeaf(root) != left ||
	             editorPaneNodeContainsLeaf(root, right) == 0 ||
	             editorPaneNodeContainsLeaf(left, right) != 0;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_leaf_at_hit_tests_correctly(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || left == NULL || right == NULL) {
		free(root);
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = left;
	root->as.split.second = right;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || editorLayoutLeafAt(&layout, 10, 5) != left ||
	             editorLayoutLeafAt(&layout, 60, 5) != right ||
	             editorLayoutLeafAt(&layout, 39, 23) != left ||
	             editorLayoutLeafAt(&layout, 40, 0) != right ||
	             editorLayoutLeafAt(&layout, 200, 5) != NULL;
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_editor_viewport_matches_legacy_text_viewport(void) {
	const struct {
		int window_cols;
		int window_rows;
		int drawer_collapsed;
	} cases[] = {
	        {80, 24, 0}, {120, 40, 0}, {40, 10, 0}, {120, 40, 1}, {200, 60, 1},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		E.window_cols = cases[i].window_cols;
		E.window_rows = cases[i].window_rows;
		E.drawer_collapsed = cases[i].drawer_collapsed;

		struct editorRect viewport = {0};
		if (!editorLayoutEditorViewport(&viewport)) {
			return 1;
		}
		int expected_x = editorDrawerTextStartColForCols(E.window_cols);
		int expected_w = editorDrawerTextViewportCols(E.window_cols);
		if (viewport.x != expected_x || viewport.y != 1 || viewport.w != expected_w ||
		    viewport.h != E.window_rows) {
			return 1;
		}
	}
	return 0;
}

static int test_layout_focused_leaf_rect_after_resize(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *original_leaf = E.focused_leaf;

	E.window_cols = 80;
	E.window_rows = 24;
	struct editorRect before = {0};
	if (!editorLayoutFocusedLeafRect(&before)) {
		return 1;
	}

	E.window_cols = 160;
	E.window_rows = 48;
	struct editorRect after = {0};
	if (!editorLayoutFocusedLeafRect(&after)) {
		return 1;
	}

	if (E.focused_leaf != original_leaf) {
		return 1;
	}
	if (after.w == before.w && after.h == before.h) {
		return 1;
	}
	if (after.w != editorDrawerTextViewportCols(E.window_cols) || after.h != E.window_rows) {
		return 1;
	}
	return 0;
}

static int test_layout_focused_leaf_rect_handles_missing_root(void) {
	editorPaneNodeFree(E.layout_root);
	E.layout_root = NULL;
	E.focused_leaf = NULL;
	struct editorRect out = {0};
	int found = editorLayoutFocusedLeafRect(&out);
	/* Reinstall a leaf so the next test isn't tripped up by reset ordering. */
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	E.focused_leaf = E.layout_root;
	return found != 0;
}

static int test_layout_split_leaf_promotes_root(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorPaneNode *leaf = root;
	struct editorPaneNode *sibling =
	        editorPaneTreeSplitLeaf(&root, leaf, EDITOR_SPLIT_VERTICAL, 0.5);
	int failed = sibling == NULL || root == leaf || !root->is_split ||
	             root->as.split.first != leaf || root->as.split.second != sibling ||
	             editorPaneTreeLeafCount(root) != 2 ||
	             editorPaneTreeFindParent(root, leaf) != root ||
	             editorPaneTreeFindParent(root, sibling) != root;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_split_leaf_inside_existing_split(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorPaneNode *original = root;
	struct editorPaneNode *first_sibling =
	        editorPaneTreeSplitLeaf(&root, original, EDITOR_SPLIT_VERTICAL, 0.5);
	if (first_sibling == NULL) {
		editorPaneNodeFree(root);
		return 1;
	}
	struct editorPaneNode *nested =
	        editorPaneTreeSplitLeaf(&root, first_sibling, EDITOR_SPLIT_HORIZONTAL, 0.3);
	int failed =
	        nested == NULL || editorPaneTreeLeafCount(root) != 3 ||
	        editorPaneTreeFindParent(root, nested) == editorPaneTreeFindParent(root, original);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_split_leaf_clamps_ratio(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorPaneNode *leaf = root;
	struct editorPaneNode *sibling =
	        editorPaneTreeSplitLeaf(&root, leaf, EDITOR_SPLIT_VERTICAL, 5.0);
	int failed = sibling == NULL || root->as.split.ratio != 1.0;
	editorPaneNodeFree(root);

	root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	leaf = root;
	sibling = editorPaneTreeSplitLeaf(&root, leaf, EDITOR_SPLIT_VERTICAL, -0.5);
	failed = failed || sibling == NULL || root->as.split.ratio != 0.0;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_split_rejects_unknown_leaf(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *orphan = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || orphan == NULL) {
		editorPaneNodeFree(root);
		editorPaneNodeFree(orphan);
		return 1;
	}
	struct editorPaneNode *sibling =
	        editorPaneTreeSplitLeaf(&root, orphan, EDITOR_SPLIT_VERTICAL, 0.5);
	int failed = sibling != NULL;
	editorPaneNodeFree(root);
	editorPaneNodeFree(orphan);
	return failed;
}

static int test_layout_close_leaf_promotes_sibling_to_root(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorPaneNode *original = root;
	struct editorPaneNode *sibling =
	        editorPaneTreeSplitLeaf(&root, original, EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		editorPaneNodeFree(root);
		return 1;
	}
	struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(&root, original);
	int failed = new_focus != sibling || root != sibling || root->is_split ||
	             editorPaneTreeLeafCount(root) != 1;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_close_leaf_nested(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorPaneNode *a = root;
	struct editorPaneNode *b = editorPaneTreeSplitLeaf(&root, a, EDITOR_SPLIT_VERTICAL, 0.5);
	struct editorPaneNode *c = editorPaneTreeSplitLeaf(&root, b, EDITOR_SPLIT_HORIZONTAL, 0.5);
	if (b == NULL || c == NULL) {
		editorPaneNodeFree(root);
		return 1;
	}

	struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(&root, c);
	int failed = new_focus != b || editorPaneTreeLeafCount(root) != 2 ||
	             editorPaneTreeFindParent(root, b) != root ||
	             editorPaneTreeFindParent(root, a) != root;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_close_last_leaf_is_no_op(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorPaneNode *retained = root;
	struct editorPaneNode *result = editorPaneTreeCloseLeaf(&root, root);
	int failed = result != NULL || root != retained;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_close_rejects_unknown_leaf(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *orphan = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || orphan == NULL) {
		editorPaneNodeFree(root);
		editorPaneNodeFree(orphan);
		return 1;
	}
	struct editorPaneNode *retained = root;
	struct editorPaneNode *result = editorPaneTreeCloseLeaf(&root, orphan);
	int failed = result != NULL || root != retained;
	editorPaneNodeFree(root);
	editorPaneNodeFree(orphan);
	return failed;
}

static int test_layout_find_parent_returns_null_for_root(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	int failed = editorPaneTreeFindParent(root, root) != NULL;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_pane_view_capture_records_active_tab(void) {
	struct editorPaneView view;
	editorPaneViewInit(&view);
	if (view.active_tab_idx != -1) {
		return 1;
	}
	E.active_tab = 3;
	E.cx = 7;
	E.cy = 11;
	E.rx = 7;
	E.rowoff = 4;
	E.coloff = 2;
	E.wrapoff = 0;
	E.cursor_offset = 42;
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	editorPaneViewCaptureFromState(&view);
	return view.active_tab_idx != 3 || view.cx != 7 || view.cy != 11 || view.rowoff != 4 ||
	       view.cursor_offset != 42;
}

static int test_layout_pane_view_load_skips_uninitialized(void) {
	struct editorPaneView view;
	editorPaneViewInit(&view);
	/* active_tab_idx == -1 (uninitialized): load must skip so a fresh
	 * leaf doesn't clobber E with zeros. */
	view.cx = 99;
	view.cy = 99;
	view.cursor_offset = 999;
	E.active_tab = 2;
	E.cx = 1;
	E.cy = 1;
	E.cursor_offset = 1;
	int loaded = editorPaneViewLoadIntoState(&view);
	return loaded != 0 || E.cx != 1 || E.cy != 1 || E.cursor_offset != 1;
}

static int test_layout_pane_view_load_applies_state_regardless_of_tab(void) {
	struct editorPaneView view;
	editorPaneViewInit(&view);
	E.active_tab = 4;
	/* Load applies the cursor unconditionally as long as the view is
	 * initialized. Tab switching is the caller's job (handled by
	 * editorLayoutSetFocusedLeaf). */
	view.active_tab_idx = 7;
	view.cx = 12;
	view.cy = 34;
	view.rx = 12;
	view.rowoff = 6;
	view.coloff = 1;
	view.wrapoff = 0;
	view.cursor_offset = 200;
	view.viewport_mode = (int)EDITOR_VIEWPORT_FREE_SCROLL;
	int loaded = editorPaneViewLoadIntoState(&view);
	return loaded != 1 || E.cx != 12 || E.cy != 34 || E.rowoff != 6 || E.cursor_offset != 200 ||
	       E.viewport_mode != EDITOR_VIEWPORT_FREE_SCROLL;
}

static int test_layout_split_focused_inherits_view(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.active_tab = 0;
	E.cx = 5;
	E.cy = 8;
	E.rx = 5;
	E.rowoff = 2;
	E.coloff = 0;
	E.wrapoff = 0;
	E.cursor_offset = 17;

	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	int failed = E.focused_leaf != sibling || sibling->as.leaf.view.cx != 5 ||
	             sibling->as.leaf.view.cy != 8 || sibling->as.leaf.view.cursor_offset != 17 ||
	             sibling->as.leaf.view.active_tab_idx != 0 || original->as.leaf.view.cx != 5 ||
	             original->as.leaf.view.cursor_offset != 17 ||
	             editorPaneTreeLeafCount(E.layout_root) != 2;
	return failed;
}

static int test_layout_close_focused_restores_sibling_view(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.active_tab = 0;
	E.cx = 3;
	E.cy = 4;
	E.cursor_offset = 30;

	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL || E.focused_leaf != sibling) {
		return 1;
	}
	/* Diverge the new (focused) sibling's cursor. */
	E.cx = 50;
	E.cy = 60;
	E.cursor_offset = 600;

	/* Move focus back to original, then close it. Sibling should become
	 * focused and its captured view (cx=50, cy=60) should be loaded. */
	if (!editorLayoutSetFocusedLeaf(original)) {
		return 1;
	}
	/* original was captured at (3,4); load brought us back to (3,4). */
	if (E.cx != 3 || E.cy != 4 || E.cursor_offset != 30) {
		return 1;
	}
	struct editorPaneNode *new_focus = editorLayoutCloseFocused();
	if (new_focus != sibling || E.focused_leaf != sibling) {
		return 1;
	}
	return E.cx != 50 || E.cy != 60 || E.cursor_offset != 600;
}

static int test_layout_close_focused_no_op_for_single_leaf(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *only = E.focused_leaf;
	struct editorPaneNode *result = editorLayoutCloseFocused();
	return result != NULL || E.focused_leaf != only || E.layout_root != only;
}

static int test_layout_find_neighbor_horizontal(void) {
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || left == NULL || right == NULL) {
		free(root);
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = left;
	root->as.split.second = right;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeInto(root, viewport, &layout)) {
		editorLeafLayoutFree(&layout);
		editorPaneNodeFree(root);
		return 1;
	}
	int failed = editorLayoutFindNeighborLeaf(&layout, left, EDITOR_FOCUS_RIGHT) != right ||
	             editorLayoutFindNeighborLeaf(&layout, right, EDITOR_FOCUS_LEFT) != left ||
	             editorLayoutFindNeighborLeaf(&layout, left, EDITOR_FOCUS_LEFT) != NULL ||
	             editorLayoutFindNeighborLeaf(&layout, left, EDITOR_FOCUS_UP) != NULL ||
	             editorLayoutFindNeighborLeaf(&layout, left, EDITOR_FOCUS_DOWN) != NULL;
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_find_neighbor_picks_nearest_among_many(void) {
	/* Build:
	 *   left | top-right
	 *        | bottom-right
	 * via a vertical split with the right child being a horizontal split.
	 */
	struct editorPaneNode *root = malloc(sizeof(*root));
	struct editorPaneNode *right = malloc(sizeof(*root));
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *top_right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *bot_right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL || right == NULL || left == NULL || top_right == NULL ||
	    bot_right == NULL) {
		free(root);
		free(right);
		editorPaneNodeFree(left);
		editorPaneNodeFree(top_right);
		editorPaneNodeFree(bot_right);
		return 1;
	}
	memset(root, 0, sizeof(*root));
	memset(right, 0, sizeof(*right));
	root->is_split = 1;
	root->as.split.orientation = EDITOR_SPLIT_VERTICAL;
	root->as.split.ratio = 0.5;
	root->as.split.first = left;
	root->as.split.second = right;
	right->is_split = 1;
	right->as.split.orientation = EDITOR_SPLIT_HORIZONTAL;
	right->as.split.ratio = 0.5;
	right->as.split.first = top_right;
	right->as.split.second = bot_right;

	struct editorRect viewport = {.x = 0, .y = 0, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeInto(root, viewport, &layout)) {
		editorLeafLayoutFree(&layout);
		editorPaneNodeFree(root);
		return 1;
	}
	/* From left, FOCUS_RIGHT should reach top_right (smallest gap among
	 * right-side candidates whose y range overlaps left). Tied y-overlap
	 * with bot_right; tie-break by smallest x falls to top_right since
	 * both have the same x but top_right comes first in layout order. */
	int failed =
	        editorLayoutFindNeighborLeaf(&layout, left, EDITOR_FOCUS_RIGHT) != top_right ||
	        editorLayoutFindNeighborLeaf(&layout, top_right, EDITOR_FOCUS_DOWN) != bot_right ||
	        editorLayoutFindNeighborLeaf(&layout, bot_right, EDITOR_FOCUS_UP) != top_right ||
	        editorLayoutFindNeighborLeaf(&layout, top_right, EDITOR_FOCUS_LEFT) != left ||
	        editorLayoutFindNeighborLeaf(&layout, bot_right, EDITOR_FOCUS_LEFT) != left;
	editorLeafLayoutFree(&layout);
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_focus_direction_swaps_view(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	E.active_tab = 0;
	E.cx = 5;
	E.cy = 5;
	E.cursor_offset = 25;

	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	/* Set sibling's cursor distinctly so we can verify the swap. */
	E.cx = 30;
	E.cy = 12;
	E.cursor_offset = 360;

	/* Move focus left → should go to original. */
	int moved = editorLayoutFocusDirection(EDITOR_FOCUS_LEFT);
	if (!moved || E.focused_leaf != original) {
		return 1;
	}
	if (E.cx != 5 || E.cy != 5 || E.cursor_offset != 25) {
		return 1;
	}
	/* Move focus right → back to sibling. */
	moved = editorLayoutFocusDirection(EDITOR_FOCUS_RIGHT);
	if (!moved || E.focused_leaf != sibling) {
		return 1;
	}
	return E.cx != 30 || E.cy != 12 || E.cursor_offset != 360;
}

static int test_layout_focus_direction_no_neighbor_is_noop(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *only = E.focused_leaf;
	int moved = editorLayoutFocusDirection(EDITOR_FOCUS_LEFT) ||
	            editorLayoutFocusDirection(EDITOR_FOCUS_RIGHT) ||
	            editorLayoutFocusDirection(EDITOR_FOCUS_UP) ||
	            editorLayoutFocusDirection(EDITOR_FOCUS_DOWN);
	return moved != 0 || E.focused_leaf != only;
}

static int test_layout_resize_focused_grows_first_child(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	/* Refocus original (the "first" child of the split). */
	if (!editorLayoutSetFocusedLeaf(original)) {
		return 1;
	}
	int changed = editorLayoutResizeFocused(1);
	struct editorPaneNode *parent = editorPaneTreeFindParent(E.layout_root, original);
	if (!changed || parent == NULL ||
	    parent->as.split.ratio <= 0.5 + 1e-9 - ROTIDE_PANE_RESIZE_STEP) {
		return 1;
	}
	double after_grow = parent->as.split.ratio;
	changed = editorLayoutResizeFocused(0);
	if (!changed || parent->as.split.ratio >= after_grow) {
		return 1;
	}
	return 0;
}

static int test_layout_resize_focused_grows_second_child(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL || E.focused_leaf != sibling) {
		return 1;
	}
	/* sibling is parent.second; growing it should DECREASE ratio. */
	struct editorPaneNode *parent = editorPaneTreeFindParent(E.layout_root, sibling);
	if (parent == NULL) {
		return 1;
	}
	double before = parent->as.split.ratio;
	if (!editorLayoutResizeFocused(1)) {
		return 1;
	}
	return parent->as.split.ratio >= before;
}

static int test_layout_resize_focused_clamps_to_min(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	if (!editorLayoutSetFocusedLeaf(original)) {
		return 1;
	}
	/* Shrink many times: ratio should clamp at min_ratio, not go below. */
	for (int i = 0; i < 100; i++) {
		(void)editorLayoutResizeFocused(0);
	}
	struct editorPaneNode *parent = editorPaneTreeFindParent(E.layout_root, original);
	if (parent == NULL) {
		return 1;
	}
	return parent->as.split.ratio < ROTIDE_PANE_MIN_RATIO - 1e-9 ||
	       parent->as.split.ratio > ROTIDE_PANE_MIN_RATIO + 1e-9;
}

static int test_layout_resize_focused_root_is_noop(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	int changed = editorLayoutResizeFocused(1);
	return changed != 0;
}

static int test_layout_focus_leaf_at_changes_focus(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	E.active_tab = 0;
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		return 1;
	}
	int left_x = viewport.x + 1;
	int right_x = viewport.x + viewport.w - 2;
	int row_y = viewport.y + 1;
	int moved = editorLayoutFocusLeafAt(left_x, row_y);
	if (!moved || E.focused_leaf != original) {
		return 1;
	}
	moved = editorLayoutFocusLeafAt(right_x, row_y);
	if (!moved || E.focused_leaf != sibling) {
		return 1;
	}
	/* Re-click in the focused pane: returns 0 (no change). */
	moved = editorLayoutFocusLeafAt(right_x, row_y);
	return moved != 0 || E.focused_leaf != sibling;
}

static int test_layout_focused_leaf_index_reports_position(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	int idx = 99;
	int count = 99;
	int ok = editorLayoutFocusedLeafIndex(&idx, &count);
	if (!ok || count != 1 || idx != 0) {
		return 1;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	ok = editorLayoutFocusedLeafIndex(&idx, &count);
	/* sibling is rightmost → index 1, count 2. */
	return !ok || count != 2 || idx != 1;
}

static int test_layout_focus_switch_swaps_active_tab(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	if (E.tab_count != 2) {
		return 1;
	}
	(void)editorTabSwitchToIndex(0);

	editorPaneNodeFree(E.layout_root);
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (E.layout_root == NULL) {
		return 1;
	}
	E.focused_leaf = E.layout_root;

	struct editorPaneNode *pane_a = E.focused_leaf;
	struct editorPaneNode *pane_b = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (pane_b == NULL || E.focused_leaf != pane_b) {
		return 1;
	}

	/* Simulate the user switching tabs inside pane B (Ctrl+Tab). Both
	 * E.active_tab and the live pane state move to tab 1. */
	(void)editorTabSwitchToIndex(1);

	/* Move focus to A: capture B (records tab 1 in its view), switch to
	 * A's recorded tab (still tab 0), load A's cursor. */
	if (!editorLayoutSetFocusedLeaf(pane_a)) {
		return 1;
	}
	if (E.active_tab != 0) {
		return 1;
	}

	/* Move focus back to B: B's view now records tab 1, so the switch
	 * brings the active tab back to 1. */
	if (!editorLayoutSetFocusedLeaf(pane_b)) {
		return 1;
	}
	return E.active_tab != 1;
}

static int test_layout_focus_switch_preserves_per_pane_cursor(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	(void)editorTabSwitchToIndex(0);

	editorPaneNodeFree(E.layout_root);
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (E.layout_root == NULL) {
		return 1;
	}
	E.focused_leaf = E.layout_root;

	/* Place pane A on tab 0 with cursor (cx=5, cy=8). */
	E.cx = 5;
	E.cy = 8;
	E.cursor_offset = 24;

	struct editorPaneNode *pane_a = E.focused_leaf;
	struct editorPaneNode *pane_b = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (pane_b == NULL || E.focused_leaf != pane_b) {
		return 1;
	}

	/* Pane B (currently focused) switches to tab 1 and moves its cursor. */
	(void)editorTabSwitchToIndex(1);
	E.cx = 20;
	E.cy = 30;
	E.cursor_offset = 600;

	/* Focus pane A: capture B (its view now records tab 1, cursor 20/30),
	 * switch back to A's tab (0), load A's cursor (5/8). */
	if (!editorLayoutSetFocusedLeaf(pane_a)) {
		return 1;
	}
	if (E.active_tab != 0 || E.cx != 5 || E.cy != 8) {
		return 1;
	}

	/* Focus pane B again: should restore tab 1 and B's stored cursor. */
	if (!editorLayoutSetFocusedLeaf(pane_b)) {
		return 1;
	}
	return E.active_tab != 1 || E.cx != 20 || E.cy != 30 || E.cursor_offset != 600;
}

static int test_layout_pane_view_tab_membership_helpers(void) {
	struct editorPaneView view;
	editorPaneViewInit(&view);
	if (view.pane_tab_count != 0) {
		return 1;
	}
	if (!editorPaneViewAddTab(&view, 3) || view.pane_tab_count != 1) {
		return 1;
	}
	/* Adding the same tab again is a no-op. */
	if (!editorPaneViewAddTab(&view, 3) || view.pane_tab_count != 1) {
		return 1;
	}
	if (!editorPaneViewAddTab(&view, 5) || view.pane_tab_count != 2) {
		return 1;
	}
	if (!editorPaneViewHasTab(&view, 3) || !editorPaneViewHasTab(&view, 5)) {
		return 1;
	}
	if (editorPaneViewHasTab(&view, 4)) {
		return 1;
	}
	if (editorPaneViewIndexOfTab(&view, 3) != 0 || editorPaneViewIndexOfTab(&view, 5) != 1 ||
	    editorPaneViewIndexOfTab(&view, 7) != -1) {
		return 1;
	}
	editorPaneViewRemoveTab(&view, 3);
	if (view.pane_tab_count != 1 || view.pane_tabs[0] != 5) {
		return 1;
	}
	view.active_tab_idx = 5;
	editorPaneViewShiftTabIndicesAfterClose(&view, 3);
	if (view.pane_tabs[0] != 4 || view.active_tab_idx != 4) {
		return 1;
	}
	return 0;
}

static int test_layout_tab_layout_for_pane_filters_membership(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());

	struct editorPaneView view;
	editorPaneViewInit(&view);
	view.active_tab_idx = 2;
	ASSERT_TRUE(editorPaneViewAddTab(&view, 2));
	ASSERT_TRUE(editorPaneViewAddTab(&view, 0));

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForPane(&view, 80, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_EQ_INT(2, layout_count);
	ASSERT_EQ_INT(2, layout[0].tab_idx);
	ASSERT_EQ_INT(1, layout[0].is_active);
	ASSERT_EQ_INT(0, layout[1].tab_idx);
	ASSERT_EQ_INT(0, layout[1].is_active);
	return 0;
}

static int test_layout_tab_layout_for_pane_active_is_view_local(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	E.active_tab = 0;

	struct editorPaneView view;
	editorPaneViewInit(&view);
	view.active_tab_idx = 2;
	ASSERT_TRUE(editorPaneViewAddTab(&view, 1));
	ASSERT_TRUE(editorPaneViewAddTab(&view, 2));

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForPane(&view, 80, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_EQ_INT(2, layout_count);
	ASSERT_EQ_INT(0, layout[0].is_active);
	ASSERT_EQ_INT(1, layout[1].is_active);
	ASSERT_EQ_INT(0, E.active_tab);
	return 0;
}

static int test_layout_tab_overflow_for_pane_uses_filtered_slots(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());

	struct editorPaneView view;
	editorPaneViewInit(&view);
	view.active_tab_idx = 2;
	view.tab_view_start = 1;
	ASSERT_TRUE(editorPaneViewAddTab(&view, 0));
	ASSERT_TRUE(editorPaneViewAddTab(&view, 2));
	ASSERT_TRUE(editorPaneViewAddTab(&view, 4));

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForPane(&view, 15, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_EQ_INT(1, layout_count);
	ASSERT_EQ_INT(2, layout[0].tab_idx);
	ASSERT_EQ_INT(1, layout[0].show_left_overflow);
	ASSERT_EQ_INT(1, layout[0].show_right_overflow);
	ASSERT_EQ_INT(0, editorTabOverflowHitTestColumnForPane(&view, 0, 15));
	ASSERT_EQ_INT(4, editorTabOverflowHitTestColumnForPane(&view, 14, 15));
	return 0;
}

static int test_layout_tab_hit_test_for_pane_returns_member_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());

	struct editorPaneView view;
	editorPaneViewInit(&view);
	view.active_tab_idx = 2;
	view.tab_view_start = 1;
	ASSERT_TRUE(editorPaneViewAddTab(&view, 0));
	ASSERT_TRUE(editorPaneViewAddTab(&view, 2));

	ASSERT_EQ_INT(2, editorTabHitTestColumnForPane(&view, 1, 15));
	return 0;
}

static int test_layout_split_focused_inherits_active_tab_only(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_TRUE(editorTabNewEmpty());
	if (E.tab_count != 3) {
		return 1;
	}
	(void)editorTabSwitchToIndex(1);

	editorPaneNodeFree(E.layout_root);
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (E.layout_root == NULL) {
		return 1;
	}
	E.focused_leaf = E.layout_root;
	/* Re-seed the new root pane with all 3 tabs to simulate "user has
	 * three tabs open in pane A". */
	editorPaneViewAddTab(&E.layout_root->as.leaf.view, 0);
	editorPaneViewAddTab(&E.layout_root->as.leaf.view, 1);
	editorPaneViewAddTab(&E.layout_root->as.leaf.view, 2);
	E.layout_root->as.leaf.view.active_tab_idx = 1;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	/* Original pane unchanged. */
	struct editorPaneNode *original = NULL;
	if (E.layout_root != NULL && E.layout_root->is_split) {
		original = E.layout_root->as.split.first;
	}
	if (original == NULL || original->as.leaf.view.pane_tab_count != 3) {
		return 1;
	}
	/* New sibling has only the splitting pane's active tab. */
	if (sibling->as.leaf.view.pane_tab_count != 1 || sibling->as.leaf.view.pane_tabs[0] != 1 ||
	    sibling->as.leaf.view.active_tab_idx != 1) {
		return 1;
	}
	return 0;
}

static int test_layout_serialize_single_leaf(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	char buf[64];
	size_t n = editorLayoutSerialize(root, buf, sizeof(buf));
	int failed = n == 0 || strcmp(buf, "leaf") != 0;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_serialize_deserialize_roundtrip(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	(void)editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.3);
	if (editorPaneTreeLeafCount(E.layout_root) != 3) {
		return 1;
	}
	char buf[256];
	size_t n = editorLayoutSerialize(E.layout_root, buf, sizeof(buf));
	if (n == 0) {
		return 1;
	}
	/* Quick sanity: serialized form contains both split markers. */
	if (strstr(buf, "(v") == NULL || strstr(buf, "(h") == NULL) {
		return 1;
	}
	struct editorPaneNode *restored = editorLayoutDeserialize(buf);
	if (restored == NULL) {
		return 1;
	}
	int leaf_count = editorPaneTreeLeafCount(restored);
	editorPaneNodeFree(restored);
	return leaf_count != 3;
}

static int test_layout_deserialize_rejects_garbage(void) {
	if (editorLayoutDeserialize("garbage") != NULL) {
		return 1;
	}
	if (editorLayoutDeserialize("(v 0.5 leaf") != NULL) {
		return 1;
	}
	if (editorLayoutDeserialize("(v 0.5 leaf leaf)leaf") != NULL) {
		return 1;
	}
	if (editorLayoutDeserialize("") != NULL) {
		return 1;
	}
	return 0;
}

static int test_layout_set_focused_leaf_rejects_non_leaf(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	struct editorPaneNode *internal_node = E.layout_root;
	if (!internal_node->is_split) {
		return 1;
	}
	int ok = editorLayoutSetFocusedLeaf(internal_node);
	return ok != 0 || E.focused_leaf != sibling ||
	       editorPaneTreeLeafCount(E.layout_root) != 2 ||
	       !editorPaneNodeContainsLeaf(E.layout_root, original);
}

static struct editorPaneNode *make_split(enum editorSplitOrientation orientation, double ratio,
                                         struct editorPaneNode *first,
                                         struct editorPaneNode *second) {
	struct editorPaneNode *node = malloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	memset(node, 0, sizeof(*node));
	node->is_split = 1;
	node->as.split.orientation = orientation;
	node->as.split.ratio = ratio;
	node->as.split.first = first;
	node->as.split.second = second;
	return node;
}

static int test_layout_border_at_single_leaf_returns_zero(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 40, .h = 20};
	struct editorPaneNode *hit = (struct editorPaneNode *)0x1;
	enum editorSplitOrientation orientation = EDITOR_SPLIT_VERTICAL;
	int got = editorLayoutBorderAt(root, viewport, 1, 10, 10, &hit, &orientation);
	editorPaneNodeFree(root);
	return got != 0 || hit != (struct editorPaneNode *)0x1;
}

static int test_layout_border_at_vertical_split_finds_gap(void) {
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *root = make_split(EDITOR_SPLIT_VERTICAL, 0.5, left, right);
	if (left == NULL || right == NULL || root == NULL) {
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		free(root);
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 41, .h = 20};
	/* w=41, border_size=1, available=40, first_w=20 → border at x=20. */
	struct editorPaneNode *hit = NULL;
	enum editorSplitOrientation orientation = EDITOR_SPLIT_HORIZONTAL;
	int ok = editorLayoutBorderAt(root, viewport, 1, 20, 5, &hit, &orientation);
	int failed = !ok || hit != root || orientation != EDITOR_SPLIT_VERTICAL;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_border_at_horizontal_split_finds_gap(void) {
	struct editorPaneNode *top = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *bottom = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *root = make_split(EDITOR_SPLIT_HORIZONTAL, 0.5, top, bottom);
	if (top == NULL || bottom == NULL || root == NULL) {
		editorPaneNodeFree(top);
		editorPaneNodeFree(bottom);
		free(root);
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 40, .h = 21};
	/* h=21, border_size=1, available=20, first_h=10 → border at y=10. */
	struct editorPaneNode *hit = NULL;
	enum editorSplitOrientation orientation = EDITOR_SPLIT_VERTICAL;
	int ok = editorLayoutBorderAt(root, viewport, 1, 5, 10, &hit, &orientation);
	int failed = !ok || hit != root || orientation != EDITOR_SPLIT_HORIZONTAL;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_border_at_nested_prefers_inner(void) {
	/* Outer vertical split: left leaf, right is a horizontal split. The
	 * inner horizontal border should win when both are hit-tested. */
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *inner_top = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *inner_bottom = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *inner =
	        make_split(EDITOR_SPLIT_HORIZONTAL, 0.5, inner_top, inner_bottom);
	struct editorPaneNode *root = make_split(EDITOR_SPLIT_VERTICAL, 0.5, left, inner);
	if (left == NULL || inner_top == NULL || inner_bottom == NULL || inner == NULL ||
	    root == NULL) {
		editorPaneNodeFree(left);
		editorPaneNodeFree(inner_top);
		editorPaneNodeFree(inner_bottom);
		free(inner);
		free(root);
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 41, .h = 21};
	/* Outer: available=40, first_w=20 → outer gap at x=20.
	 * Inner right rect: x=21, w=20, full height. Inner h=21, available=20,
	 * first_h=10 → inner horizontal gap at y=10 inside that rect. */
	struct editorPaneNode *hit = NULL;
	enum editorSplitOrientation orientation = EDITOR_SPLIT_VERTICAL;
	int ok = editorLayoutBorderAt(root, viewport, 1, 25, 10, &hit, &orientation);
	int failed = !ok || hit != inner || orientation != EDITOR_SPLIT_HORIZONTAL;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_border_at_off_by_one_misses(void) {
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *root = make_split(EDITOR_SPLIT_VERTICAL, 0.5, left, right);
	if (left == NULL || right == NULL || root == NULL) {
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		free(root);
		return 1;
	}
	struct editorRect viewport = {.x = 0, .y = 0, .w = 41, .h = 20};
	struct editorPaneNode *hit = (struct editorPaneNode *)0x1;
	enum editorSplitOrientation orientation = EDITOR_SPLIT_HORIZONTAL;
	/* Border lives at x=20. x=19 is in the left leaf; x=21 is in the right
	 * leaf; both should miss. */
	int got_left = editorLayoutBorderAt(root, viewport, 1, 19, 5, &hit, &orientation);
	int got_right = editorLayoutBorderAt(root, viewport, 1, 21, 5, &hit, &orientation);
	int failed = got_left != 0 || got_right != 0;
	editorPaneNodeFree(root);
	return failed;
}

static int test_layout_close_focused_clears_split_resize_state(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL) {
		return 1;
	}
	/* Simulate an in-progress drag of the parent split node. */
	E.split_resize_active = 1;
	E.split_resize_node = editorPaneTreeFindParent(E.layout_root, sibling);
	if (E.split_resize_node == NULL) {
		return 1;
	}
	(void)original;
	struct editorPaneNode *promoted = editorLayoutCloseFocused();
	if (promoted == NULL) {
		return 1;
	}
	return E.split_resize_active != 0 || E.split_resize_node != NULL;
}

static int test_layout_split_node_rect_returns_parent_rect(void) {
	struct editorPaneNode *left = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *right = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	struct editorPaneNode *root = make_split(EDITOR_SPLIT_VERTICAL, 0.5, left, right);
	if (left == NULL || right == NULL || root == NULL) {
		editorPaneNodeFree(left);
		editorPaneNodeFree(right);
		free(root);
		return 1;
	}
	struct editorRect viewport = {.x = 4, .y = 1, .w = 41, .h = 20};
	struct editorRect out = {0};
	int ok = editorLayoutSplitNodeRect(root, viewport, 1, root, &out);
	int failed = !ok || out.x != viewport.x || out.y != viewport.y || out.w != viewport.w ||
	             out.h != viewport.h;
	if (!failed) {
		/* Nested: rect of the inner node is the right child's rect. */
		struct editorPaneNode *inner_a = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
		struct editorPaneNode *inner_b = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
		struct editorPaneNode *inner =
		        make_split(EDITOR_SPLIT_HORIZONTAL, 0.5, inner_a, inner_b);
		if (inner_a == NULL || inner_b == NULL || inner == NULL) {
			editorPaneNodeFree(inner_a);
			editorPaneNodeFree(inner_b);
			free(inner);
			editorPaneNodeFree(root);
			return 1;
		}
		editorPaneNodeFree(right);
		root->as.split.second = inner;
		struct editorRect inner_rect = {0};
		ok = editorLayoutSplitNodeRect(root, viewport, 1, inner, &inner_rect);
		/* w=41, border=1, available=40, first_w=20 → right rect: x=4+20+1=25,
		 * y=1, w=20, h=20. */
		failed = !ok || inner_rect.x != 25 || inner_rect.y != 1 || inner_rect.w != 20 ||
		         inner_rect.h != 20;
	}
	editorPaneNodeFree(root);
	return failed;
}

const struct editorTestCase g_layout_tests[] = {
        {"layout_single_leaf_returns_full_viewport", test_layout_single_leaf_returns_full_viewport},
        {"layout_vertical_split_tiles_viewport", test_layout_vertical_split_tiles_viewport},
        {"layout_horizontal_split_tiles_viewport", test_layout_horizontal_split_tiles_viewport},
        {"layout_nested_splits_no_gaps_or_overlap", test_layout_nested_splits_no_gaps_or_overlap},
        {"layout_ratio_clamped_to_bounds", test_layout_ratio_clamped_to_bounds},
        {"layout_zero_viewport_returns_zero_rects", test_layout_zero_viewport_returns_zero_rects},
        {"layout_leaf_rect_finds_specific_leaf", test_layout_leaf_rect_finds_specific_leaf},
        {"layout_leaf_rect_returns_zero_when_not_found",
         test_layout_leaf_rect_returns_zero_when_not_found},
        {"layout_first_leaf_descends_to_leftmost", test_layout_first_leaf_descends_to_leftmost},
        {"layout_leaf_at_hit_tests_correctly", test_layout_leaf_at_hit_tests_correctly},
        {"layout_editor_viewport_matches_legacy_text_viewport",
         test_layout_editor_viewport_matches_legacy_text_viewport},
        {"layout_focused_leaf_rect_after_resize", test_layout_focused_leaf_rect_after_resize},
        {"layout_focused_leaf_rect_handles_missing_root",
         test_layout_focused_leaf_rect_handles_missing_root},
        {"layout_split_leaf_promotes_root", test_layout_split_leaf_promotes_root},
        {"layout_split_leaf_inside_existing_split", test_layout_split_leaf_inside_existing_split},
        {"layout_split_leaf_clamps_ratio", test_layout_split_leaf_clamps_ratio},
        {"layout_split_rejects_unknown_leaf", test_layout_split_rejects_unknown_leaf},
        {"layout_close_leaf_promotes_sibling_to_root",
         test_layout_close_leaf_promotes_sibling_to_root},
        {"layout_close_leaf_nested", test_layout_close_leaf_nested},
        {"layout_close_last_leaf_is_no_op", test_layout_close_last_leaf_is_no_op},
        {"layout_close_rejects_unknown_leaf", test_layout_close_rejects_unknown_leaf},
        {"layout_find_parent_returns_null_for_root", test_layout_find_parent_returns_null_for_root},
        {"layout_pane_view_capture_records_active_tab",
         test_layout_pane_view_capture_records_active_tab},
        {"layout_pane_view_load_skips_uninitialized",
         test_layout_pane_view_load_skips_uninitialized},
        {"layout_pane_view_load_applies_state_regardless_of_tab",
         test_layout_pane_view_load_applies_state_regardless_of_tab},
        {"layout_split_focused_inherits_view", test_layout_split_focused_inherits_view},
        {"layout_close_focused_restores_sibling_view",
         test_layout_close_focused_restores_sibling_view},
        {"layout_close_focused_no_op_for_single_leaf",
         test_layout_close_focused_no_op_for_single_leaf},
        {"layout_set_focused_leaf_rejects_non_leaf", test_layout_set_focused_leaf_rejects_non_leaf},
        {"layout_focus_switch_swaps_active_tab", test_layout_focus_switch_swaps_active_tab},
        {"layout_focus_switch_preserves_per_pane_cursor",
         test_layout_focus_switch_preserves_per_pane_cursor},
        {"layout_pane_view_tab_membership_helpers", test_layout_pane_view_tab_membership_helpers},
        {"layout_tab_layout_for_pane_filters_membership",
         test_layout_tab_layout_for_pane_filters_membership},
        {"layout_tab_layout_for_pane_active_is_view_local",
         test_layout_tab_layout_for_pane_active_is_view_local},
        {"layout_tab_overflow_for_pane_uses_filtered_slots",
         test_layout_tab_overflow_for_pane_uses_filtered_slots},
        {"layout_tab_hit_test_for_pane_returns_member_tab",
         test_layout_tab_hit_test_for_pane_returns_member_tab},
        {"layout_split_focused_inherits_active_tab_only",
         test_layout_split_focused_inherits_active_tab_only},
        {"layout_serialize_single_leaf", test_layout_serialize_single_leaf},
        {"layout_serialize_deserialize_roundtrip", test_layout_serialize_deserialize_roundtrip},
        {"layout_deserialize_rejects_garbage", test_layout_deserialize_rejects_garbage},
        {"layout_find_neighbor_horizontal", test_layout_find_neighbor_horizontal},
        {"layout_find_neighbor_picks_nearest_among_many",
         test_layout_find_neighbor_picks_nearest_among_many},
        {"layout_focus_direction_swaps_view", test_layout_focus_direction_swaps_view},
        {"layout_focus_direction_no_neighbor_is_noop",
         test_layout_focus_direction_no_neighbor_is_noop},
        {"layout_resize_focused_grows_first_child", test_layout_resize_focused_grows_first_child},
        {"layout_resize_focused_grows_second_child", test_layout_resize_focused_grows_second_child},
        {"layout_resize_focused_clamps_to_min", test_layout_resize_focused_clamps_to_min},
        {"layout_resize_focused_root_is_noop", test_layout_resize_focused_root_is_noop},
        {"layout_focus_leaf_at_changes_focus", test_layout_focus_leaf_at_changes_focus},
        {"layout_focused_leaf_index_reports_position",
         test_layout_focused_leaf_index_reports_position},
        {"layout_border_at_single_leaf_returns_zero",
         test_layout_border_at_single_leaf_returns_zero},
        {"layout_border_at_vertical_split_finds_gap",
         test_layout_border_at_vertical_split_finds_gap},
        {"layout_border_at_horizontal_split_finds_gap",
         test_layout_border_at_horizontal_split_finds_gap},
        {"layout_border_at_nested_prefers_inner", test_layout_border_at_nested_prefers_inner},
        {"layout_border_at_off_by_one_misses", test_layout_border_at_off_by_one_misses},
        {"layout_split_node_rect_returns_parent_rect",
         test_layout_split_node_rect_returns_parent_rect},
        {"layout_close_focused_clears_split_resize_state",
         test_layout_close_focused_clears_split_resize_state},
};

const int g_layout_test_count = (int)(sizeof(g_layout_tests) / sizeof(g_layout_tests[0]));
