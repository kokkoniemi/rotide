#include "render/pane_view.h"

#include "render/drawer_view.h"
#include "render/terminal_view.h"
#include "workspace/drawer.h"
#include <stdio.h>

#define VT100_CLEAR_ROW_3 "\x1b[K"
/* "│" U+2502 BOX DRAWINGS LIGHT VERTICAL (UTF-8: e2 94 82) */
#define EDITOR_PANE_VBORDER "\xe2\x94\x82"
/* "─" U+2500 BOX DRAWINGS LIGHT HORIZONTAL (UTF-8: e2 94 80) */
#define EDITOR_PANE_HBORDER "\xe2\x94\x80"

int editorAppendCursorMove(struct writeBuf *wb, int row, int col);
int editorPaneSyntaxFrameBuild(const struct editorLeafLayout *layout);
void editorPaneSyntaxFrameClear(void);
int editorDrawFocusedPaneSlice(struct writeBuf *wb, const struct editorPaneNode *leaf,
		int body_row_in_pane, int slice_cols);
int editorDrawPaneViewSlice(struct writeBuf *wb, const struct editorPaneNode *leaf,
		const struct editorPaneView *view, int body_row_in_pane, int slice_cols);
int editorDrawBlankCells(struct writeBuf *wb, int cells);

static int editorPaneLeafAt(const struct editorLeafLayout *layout, int x,
		int y, struct editorRect *out_rect) {
	for (int i = 0; i < layout->count; i++) {
		struct editorRect r = layout->rects[i].rect;
		if (r.x <= x && x < r.x + r.w && r.y <= y && y < r.y + r.h) {
			if (out_rect != NULL) {
				*out_rect = r;
			}
			return i;
		}
	}
	return -1;
}

enum editorBorderCellKind {
	EDITOR_BORDER_CELL_NONE = 0,
	EDITOR_BORDER_CELL_VERTICAL,
	EDITOR_BORDER_CELL_HORIZONTAL
};

static enum editorBorderCellKind editorBorderCellAt(int x, int screen_y,
		const struct editorBorderList *borders) {
	if (borders == NULL) {
		return EDITOR_BORDER_CELL_NONE;
	}
	for (int i = 0; i < borders->count; i++) {
		struct editorRect r = borders->rects[i].rect;
		if (x < r.x || x >= r.x + r.w) {
			continue;
		}
		if (screen_y < r.y || screen_y >= r.y + r.h) {
			continue;
		}
		return borders->rects[i].orientation == EDITOR_SPLIT_HORIZONTAL ?
				EDITOR_BORDER_CELL_HORIZONTAL : EDITOR_BORDER_CELL_VERTICAL;
	}
	return EDITOR_BORDER_CELL_NONE;
}

int editorDrawMultiPaneRows(struct writeBuf *wb,
		const struct editorLeafLayout *layout,
		const struct editorBorderList *borders,
		struct editorRect focused_rect) {
	int ok = 0;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_start_col = editorDrawerTextStartColForCols(E.window_cols);
	if (!editorPaneSyntaxFrameBuild(layout)) {
		goto cleanup;
	}

	for (int y_body = 0; y_body < E.window_rows; y_body++) {
		int screen_y = y_body + 1;
		int terminal_row = y_body + 2;
		if (!editorAppendCursorMove(wb, terminal_row, 1)) {
			goto cleanup;
		}
		if (!editorDrawDrawerRow(wb, y_body + 1, drawer_cols)) {
			goto cleanup;
		}
		if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
			goto cleanup;
		}

		int focused_intersects =
				screen_y >= focused_rect.y &&
				screen_y < focused_rect.y + focused_rect.h;
		int x = text_start_col;
		while (x < E.window_cols) {
			enum editorBorderCellKind border = editorBorderCellAt(x, screen_y, borders);
			if (border == EDITOR_BORDER_CELL_HORIZONTAL) {
				if (!wbAppend(wb, EDITOR_PANE_HBORDER, sizeof(EDITOR_PANE_HBORDER) - 1)) {
					goto cleanup;
				}
				x++;
				continue;
			}
			if (border == EDITOR_BORDER_CELL_VERTICAL) {
				if (!wbAppend(wb, EDITOR_PANE_VBORDER, sizeof(EDITOR_PANE_VBORDER) - 1)) {
					goto cleanup;
				}
				x++;
				continue;
			}
			struct editorRect leaf_rect = {0};
			int leaf_idx = editorPaneLeafAt(layout, x, screen_y, &leaf_rect);
			if (leaf_idx < 0) {
				if (!wbAppend(wb, " ", 1)) {
					goto cleanup;
				}
				x++;
				continue;
			}

			int slice_cols = leaf_rect.x + leaf_rect.w - x;
			if (slice_cols <= 0) {
				slice_cols = 1;
			}
			struct editorPaneNode *leaf_node = layout->rects[leaf_idx].node;
			if (leaf_node != NULL &&
					leaf_node->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL) {
				int row_in_pane = screen_y - leaf_rect.y;
				int col_in_pane = x - leaf_rect.x;
				if (!editorDrawTerminalCells(wb,
							(struct editorTerminalPane *)leaf_node->as.leaf.kind_state,
							row_in_pane, col_in_pane, slice_cols)) {
					goto cleanup;
				}
			} else {
				int is_focused_slice = focused_intersects &&
						leaf_node == E.focused_leaf;
				if (is_focused_slice) {
					int body_row_in_pane = screen_y - focused_rect.y;
					if (!editorDrawFocusedPaneSlice(wb, leaf_node, body_row_in_pane,
								slice_cols)) {
						goto cleanup;
					}
				} else if (leaf_node != NULL &&
						leaf_node->as.leaf.view.active_tab_idx >= 0) {
					int body_row_in_pane = screen_y - leaf_rect.y;
					if (!editorDrawPaneViewSlice(wb, leaf_node,
								&leaf_node->as.leaf.view, body_row_in_pane,
								slice_cols)) {
						goto cleanup;
					}
				} else if (!editorDrawBlankCells(wb, slice_cols)) {
					goto cleanup;
				}
			}
			x += slice_cols;
		}
		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			goto cleanup;
		}
	}

	ok = 1;
cleanup:
	editorPaneSyntaxFrameClear();
	return ok;
}
