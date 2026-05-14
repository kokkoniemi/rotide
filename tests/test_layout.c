#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"

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
	return a.x < b.x + b.w && b.x < a.x + a.w &&
			a.y < b.y + b.h && b.y < a.y + a.h;
}

static int test_layout_single_leaf_returns_full_viewport(void) {
	struct editorPaneNode *root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (root == NULL) {
		return 1;
	}
	struct editorRect viewport = {.x = 4, .y = 1, .w = 80, .h = 24};
	struct editorLeafLayout layout = {0};
	int ok = editorLayoutComputeInto(root, viewport, &layout);
	int failed = !ok || layout.count != 1 ||
			!rects_equal(layout.rects[0].rect, viewport) ||
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
		failed = lr.x != 0 || lr.y != 0 || lr.w != 40 || lr.h != 24 ||
				rr.x != 40 || rr.y != 0 || rr.w != 40 || rr.h != 24 ||
				rects_overlap(lr, rr);
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
		failed = tr.x != 5 || tr.y != 1 || tr.w != 60 || tr.h != 10 ||
				br.x != 5 || br.y != 11 || br.w != 60 || br.h != 30 ||
				rects_overlap(tr, br);
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
	int failed = !ok || layout.count != 2 ||
			layout.rects[0].rect.w != 80 ||
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
	int failed = !ok || layout.count != 1 ||
			layout.rects[0].rect.w != 0 || layout.rects[0].rect.h != 0;
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
	int failed = !ok ||
			editorLayoutLeafAt(&layout, 10, 5) != left ||
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
		{80, 24, 0},
		{120, 40, 0},
		{40, 10, 0},
		{120, 40, 1},
		{200, 60, 1},
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
		if (viewport.x != expected_x || viewport.y != 1 ||
				viewport.w != expected_w || viewport.h != E.window_rows) {
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
	if (after.w != editorDrawerTextViewportCols(E.window_cols) ||
			after.h != E.window_rows) {
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
	int failed = sibling == NULL ||
			root == leaf ||
			!root->is_split ||
			root->as.split.first != leaf ||
			root->as.split.second != sibling ||
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
	int failed = nested == NULL ||
			editorPaneTreeLeafCount(root) != 3 ||
			editorPaneTreeFindParent(root, nested) ==
					editorPaneTreeFindParent(root, original);
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
	int failed = new_focus != sibling ||
			root != sibling ||
			root->is_split ||
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
	struct editorPaneNode *b =
			editorPaneTreeSplitLeaf(&root, a, EDITOR_SPLIT_VERTICAL, 0.5);
	struct editorPaneNode *c =
			editorPaneTreeSplitLeaf(&root, b, EDITOR_SPLIT_HORIZONTAL, 0.5);
	if (b == NULL || c == NULL) {
		editorPaneNodeFree(root);
		return 1;
	}

	struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(&root, c);
	int failed = new_focus != b ||
			editorPaneTreeLeafCount(root) != 2 ||
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

const struct editorTestCase g_layout_tests[] = {
	{"layout_single_leaf_returns_full_viewport",
			test_layout_single_leaf_returns_full_viewport},
	{"layout_vertical_split_tiles_viewport",
			test_layout_vertical_split_tiles_viewport},
	{"layout_horizontal_split_tiles_viewport",
			test_layout_horizontal_split_tiles_viewport},
	{"layout_nested_splits_no_gaps_or_overlap",
			test_layout_nested_splits_no_gaps_or_overlap},
	{"layout_ratio_clamped_to_bounds", test_layout_ratio_clamped_to_bounds},
	{"layout_zero_viewport_returns_zero_rects",
			test_layout_zero_viewport_returns_zero_rects},
	{"layout_leaf_rect_finds_specific_leaf",
			test_layout_leaf_rect_finds_specific_leaf},
	{"layout_leaf_rect_returns_zero_when_not_found",
			test_layout_leaf_rect_returns_zero_when_not_found},
	{"layout_first_leaf_descends_to_leftmost",
			test_layout_first_leaf_descends_to_leftmost},
	{"layout_leaf_at_hit_tests_correctly", test_layout_leaf_at_hit_tests_correctly},
	{"layout_editor_viewport_matches_legacy_text_viewport",
			test_layout_editor_viewport_matches_legacy_text_viewport},
	{"layout_focused_leaf_rect_after_resize",
			test_layout_focused_leaf_rect_after_resize},
	{"layout_focused_leaf_rect_handles_missing_root",
			test_layout_focused_leaf_rect_handles_missing_root},
	{"layout_split_leaf_promotes_root", test_layout_split_leaf_promotes_root},
	{"layout_split_leaf_inside_existing_split",
			test_layout_split_leaf_inside_existing_split},
	{"layout_split_leaf_clamps_ratio", test_layout_split_leaf_clamps_ratio},
	{"layout_split_rejects_unknown_leaf", test_layout_split_rejects_unknown_leaf},
	{"layout_close_leaf_promotes_sibling_to_root",
			test_layout_close_leaf_promotes_sibling_to_root},
	{"layout_close_leaf_nested", test_layout_close_leaf_nested},
	{"layout_close_last_leaf_is_no_op", test_layout_close_last_leaf_is_no_op},
	{"layout_close_rejects_unknown_leaf", test_layout_close_rejects_unknown_leaf},
	{"layout_find_parent_returns_null_for_root",
			test_layout_find_parent_returns_null_for_root},
};

const int g_layout_test_count =
		(int)(sizeof(g_layout_tests) / sizeof(g_layout_tests[0]));
