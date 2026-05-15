#include "render/viewport.h"

#include "editing/buffer_core.h"
#include "render/wrap.h"
#include "text/row.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include <limits.h>

static int editorPaneTextBodyViewportColsForWidth(int pane_cols) {
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_cols) {
		gutter_cols = pane_cols;
	}
	int text_cols = pane_cols - gutter_cols;
	if (text_cols < 1) {
		text_cols = 1;
	}
	if (text_cols >= 3) {
		return text_cols - 2;
	}
	return text_cols;
}

int editorViewportFocusedPaneBodyRows(void) {
	struct editorRect rect = {0};
	if (editorLayoutFocusedLeafRect(&rect) && rect.h > 0) {
		return rect.h;
	}
	return E.window_rows > 0 ? E.window_rows : 1;
}

int editorViewportFocusedPaneTextBodyCols(void) {
	struct editorRect rect = {0};
	if (editorLayoutFocusedLeafRect(&rect) && rect.w > 0) {
		return editorPaneTextBodyViewportColsForWidth(rect.w);
	}
	return editorTextBodyViewportCols(E.window_cols);
}

int editorViewportTextScreenRowToBufferRow(int screen_row, int *row_idx_out,
		int *segment_coloff_out) {
	return editorViewportTextScreenRowToBufferPosition(screen_row, row_idx_out, segment_coloff_out,
			NULL);
}

int editorViewportTextScreenRowToBufferPosition(int screen_row, int *row_idx_out,
		int *segment_coloff_out, int *segment_indent_cols_out) {
	if (row_idx_out == NULL || segment_coloff_out == NULL || screen_row < 0) {
		return 0;
	}
	if (!E.line_wrap_enabled) {
		*row_idx_out = E.rowoff + screen_row;
		*segment_coloff_out = E.coloff;
		if (segment_indent_cols_out != NULL) {
			*segment_indent_cols_out = 0;
		}
		return 1;
	}

	int row = E.rowoff;
	int segment = E.wrapoff;
	int body_cols = editorWrapBodyCols();
	for (int y = 0; y < screen_row; y++) {
		editorWrappedAdvancePosition(&row, &segment, body_cols);
	}
	*row_idx_out = row;
	int available_cols = 0;
	int indent_cols = 0;
	if (row >= 0 && row < E.numrows) {
		editorWrapSegmentInfo(&E.rows[row], segment, body_cols, segment_coloff_out,
				&available_cols, &indent_cols);
	} else {
		*segment_coloff_out = 0;
	}
	if (segment_indent_cols_out != NULL) {
		*segment_indent_cols_out = indent_cols;
	}
	return 1;
}

static void editorClampViewportOffsets(void) {
	if (E.line_wrap_enabled) {
		editorWrappedClampViewportOffsets();
		return;
	}
	if (E.rowoff < 0) {
		E.rowoff = 0;
	}
	int max_rowoff = E.numrows > 0 ? E.numrows - 1 : 0;
	if (E.rowoff > max_rowoff) {
		E.rowoff = max_rowoff;
	}
	if (E.coloff < 0) {
		E.coloff = 0;
	}
	if (E.coloff > 0) {
		int max_coloff = editorBufferMaxRenderCols();
		if (max_coloff > 0) {
			max_coloff--;
		}
		if (E.coloff > max_coloff) {
			E.coloff = max_coloff;
		}
	}
	E.wrapoff = 0;
}

static void editorUpdateRenderXFromCursor(void) {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}
}

static void editorFollowCursorViewport(void) {
	int text_cols = editorViewportFocusedPaneTextBodyCols();
	if (text_cols < 1) {
		text_cols = 1;
	}
	int body_rows = editorViewportFocusedPaneBodyRows();

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = E.cy < E.numrows ?
				editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols) : 0;
		E.coloff = 0;

		if (editorWrappedPositionBefore(E.cy, cursor_segment, E.rowoff, E.wrapoff)) {
			E.rowoff = E.cy;
			E.wrapoff = cursor_segment;
			return;
		}

		int distance = 0;
		if (!editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
					body_rows > 0 ? body_rows - 1 : 0, body_cols, &distance)) {
			int top_row = E.cy;
			int top_segment = cursor_segment;
			int back_count = body_rows > 0 ? body_rows - 1 : 0;
			for (int i = 0; i < back_count; i++) {
				editorWrappedMoveBackPosition(&top_row, &top_segment, body_cols);
			}
			E.rowoff = top_row;
			E.wrapoff = top_segment;
		}
		return;
	}

	// Keep the cursor visible vertically and horizontally by moving
	// the window origin just enough to include the current position.
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	} else if (E.cy >= E.rowoff + body_rows) {
		E.rowoff = E.cy - body_rows + 1;
	}

	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	} else if (E.rx >= E.coloff + text_cols) {
		E.coloff = E.rx - text_cols + 1;
	}
}

void editorViewportSetMode(enum editorViewportMode mode) {
	if (mode == EDITOR_VIEWPORT_FREE_SCROLL) {
		E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
	} else {
		E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	}
}

void editorViewportScrollByRows(int delta_rows) {
	if (delta_rows == 0) {
		return;
	}

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		editorWrappedClampViewportOffsets();
		if (delta_rows > 0) {
			for (int i = 0; i < delta_rows; i++) {
				int old_row = E.rowoff;
				int old_segment = E.wrapoff;
				editorWrappedAdvancePosition(&E.rowoff, &E.wrapoff, body_cols);
				if (E.rowoff >= E.numrows) {
					E.rowoff = old_row;
					E.wrapoff = old_segment;
					break;
				}
			}
		} else {
			for (int i = 0; i > delta_rows; i--) {
				int old_row = E.rowoff;
				int old_segment = E.wrapoff;
				editorWrappedMoveBackPosition(&E.rowoff, &E.wrapoff, body_cols);
				if (E.rowoff == old_row && E.wrapoff == old_segment) {
					break;
				}
			}
		}
		E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
		editorWrappedClampViewportOffsets();
		return;
	}

	long long target = (long long)E.rowoff + (long long)delta_rows;
	if (target < 0) {
		target = 0;
	}
	long long max_rowoff = E.numrows > 0 ? (long long)E.numrows - 1 : 0;
	if (target > max_rowoff) {
		target = max_rowoff;
	}
	E.rowoff = (int)target;
	E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
	editorClampViewportOffsets();
}

void editorViewportScrollByCols(int delta_cols) {
	if (delta_cols == 0) {
		return;
	}

	if (E.line_wrap_enabled) {
		E.coloff = 0;
		editorWrappedClampViewportOffsets();
		return;
	}

	long long target = (long long)E.coloff + (long long)delta_cols;
	if (target < 0) {
		target = 0;
	}
	if (target > INT_MAX) {
		target = INT_MAX;
	}
	E.coloff = (int)target;
	E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
	editorClampViewportOffsets();
}

void editorViewportEnsureCursorVisible(void) {
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	editorUpdateRenderXFromCursor();
	editorFollowCursorViewport();
	editorClampViewportOffsets();
}

static int editorViewportCenteredScreenRowAvoidingDrawerSelection(int desired_screen_row) {
	if (desired_screen_row < 0 || E.pane_focus != EDITOR_PANE_DRAWER ||
			E.drawer_selected_index < 0 || E.window_rows <= 1) {
		return desired_screen_row;
	}
	int drawer_screen_row = E.drawer_selected_index - E.drawer_rowoff;
	if (drawer_screen_row != desired_screen_row) {
		return desired_screen_row;
	}
	if (desired_screen_row + 1 < E.window_rows) {
		return desired_screen_row + 1;
	}
	if (desired_screen_row > 0) {
		return desired_screen_row - 1;
	}
	return desired_screen_row;
}

void editorViewportCenterCursor(void) {
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	editorUpdateRenderXFromCursor();

	int body_rows = editorViewportFocusedPaneBodyRows();
	int target_screen_row = body_rows > 0 ? body_rows / 2 : 0;
	target_screen_row = editorViewportCenteredScreenRowAvoidingDrawerSelection(target_screen_row);

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = E.cy < E.numrows ?
				editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols) : 0;
		E.coloff = 0;
		int top_row = E.cy;
		int top_segment = cursor_segment;
		for (int i = 0; i < target_screen_row; i++) {
			editorWrappedMoveBackPosition(&top_row, &top_segment, body_cols);
		}
		E.rowoff = top_row;
		E.wrapoff = top_segment;
	} else {
		int target_rowoff = E.cy - target_screen_row;
		if (target_rowoff < 0) {
			target_rowoff = 0;
		}
		E.rowoff = target_rowoff;

		int text_cols = editorViewportFocusedPaneTextBodyCols();
		if (text_cols < 1) {
			text_cols = 1;
		}
		if (E.rx < E.coloff) {
			E.coloff = E.rx;
		} else if (E.rx >= E.coloff + text_cols) {
			E.coloff = E.rx - text_cols + 1;
		}
	}
	editorClampViewportOffsets();
}

void editorViewportUpdateForFrame(void) {
	editorUpdateRenderXFromCursor();
	if (E.viewport_mode == EDITOR_VIEWPORT_FOLLOW_CURSOR) {
		editorFollowCursorViewport();
	}
	editorClampViewportOffsets();
}
