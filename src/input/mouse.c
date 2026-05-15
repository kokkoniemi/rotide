#include "input/mouse.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "input/actions_workspace.h"
#include "language/lsp.h"
#include "render/viewport.h"
#include "terminal/terminal_pane.h"
#include "text/row.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <ctype.h>

enum {
	MOUSE_WHEEL_SCROLL_LINES = 3,
	MOUSE_WHEEL_SCROLL_COLS = 3,
	MOUSE_DOUBLE_CLICK_THRESHOLD_MS = 400
};

#define DRAWER_HEADER_MODE_BUTTON_COLS 3
#define DRAWER_HEADER_MODE_BUTTON_COUNT 7
#define DRAWER_HEADER_MODE_BUTTONS_MIN_COLS \
	(ROTIDE_DRAWER_COLLAPSED_WIDTH + \
			DRAWER_HEADER_MODE_BUTTON_COLS * DRAWER_HEADER_MODE_BUTTON_COUNT)

int editorDrawerHeaderModeForColumn(int mouse_col, int drawer_cols,
		enum editorDrawerMode *mode_out) {
	if (mode_out == NULL || drawer_cols < DRAWER_HEADER_MODE_BUTTONS_MIN_COLS ||
			mouse_col < ROTIDE_DRAWER_COLLAPSED_WIDTH) {
		return 0;
	}

	int mode_col = mouse_col - ROTIDE_DRAWER_COLLAPSED_WIDTH;
	int button_idx = mode_col / DRAWER_HEADER_MODE_BUTTON_COLS;
	if (mode_col < 0 || button_idx < 0 || button_idx >= DRAWER_HEADER_MODE_BUTTON_COUNT) {
		return 0;
	}

	switch (button_idx) {
	case 0:
		*mode_out = EDITOR_DRAWER_MODE_TREE;
		return 1;
	case 1:
		*mode_out = EDITOR_DRAWER_MODE_FILE_SEARCH;
		return 1;
	case 2:
		*mode_out = EDITOR_DRAWER_MODE_PROJECT_SEARCH;
		return 1;
	case 3:
		*mode_out = EDITOR_DRAWER_MODE_LSP;
		return 1;
	case 4:
		*mode_out = EDITOR_DRAWER_MODE_DAP;
		return 1;
	case 5:
		*mode_out = EDITOR_DRAWER_MODE_GIT;
		return 1;
	case 6:
		*mode_out = EDITOR_DRAWER_MODE_MAIN_MENU;
		return 1;
	default:
		return 0;
	}
}

void editorResetDrawerClickTracking(void) {
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
}

void editorResetTextClickTracking(void) {
	E.text_last_click_cy = -1;
	E.text_last_click_cx = -1;
	E.text_last_click_ms = 0;
	E.text_click_count = 0;
}

void editorResetTabClickTracking(void) {
	E.tab_last_click_idx = -1;
	E.tab_last_click_ms = 0;
}

int editorHandleMouseTopRowTabClick(const struct editorMouseEvent *event, long long now_ms) {
	if (event->y != 1) {
		return 0;
	}

	int mouse_col = event->x - 1;
	int collapsed_toggle_cols = editorDrawerIsCollapsed() ?
			editorDrawerCollapsedToggleWidthForCols(E.window_cols) : 0;
	int text_start_col = editorDrawerTextStartColForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	int tab_start_col = editorDrawerIsCollapsed() ? collapsed_toggle_cols : text_start_col;
	int tab_cols = editorDrawerIsCollapsed() ? E.window_cols - collapsed_toggle_cols : text_cols;
	if (tab_cols < 0) {
		tab_cols = 0;
	}

	int tab_col = mouse_col - tab_start_col;
	int tab_idx = editorTabOverflowHitTestColumn(tab_col, tab_cols);
	if (tab_idx >= 0) {
		(void)editorTabSwitchToIndex(tab_idx);
		editorResetTabClickTracking();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return 1;
	}

	tab_idx = editorTabHitTestColumn(tab_col, tab_cols);
	if (tab_idx >= 0) {
		int is_double_click = E.tab_last_click_idx == tab_idx &&
				E.tab_last_click_ms > 0 && now_ms > 0 &&
				now_ms - E.tab_last_click_ms <= MOUSE_DOUBLE_CLICK_THRESHOLD_MS;
		(void)editorTabSwitchToIndex(tab_idx);
		if (is_double_click) {
			if (editorActiveTabIsPreview()) {
				editorTabPinActivePreview();
				editorSetStatusMsg("Tab kept open");
			}
			editorResetTabClickTracking();
		} else {
			E.tab_last_click_idx = tab_idx;
			E.tab_last_click_ms = now_ms;
		}
	} else {
		editorResetTabClickTracking();
	}
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return 1;
}

int editorHandleMouseDrawerLeftPress(const struct editorMouseEvent *event, long long now_ms,
		int double_click_threshold_ms, editorProcessMappedActionFn process_mapped_action,
		editorMouseJumpToPathFn jump_to_path, int *effects_out) {
	int mouse_col = event->x - 1;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int collapsed_toggle_cols = editorDrawerIsCollapsed() ?
			editorDrawerCollapsedToggleWidthForCols(E.window_cols) : 0;
	int drawer_view_rows = E.window_rows;
	int drawer_row = event->y - 1;
	int effects = 0;

	if (editorDrawerIsCollapsed() && drawer_row == 0 &&
			mouse_col >= 0 && mouse_col < collapsed_toggle_cols) {
		editorResetDrawerClickTracking();
		editorExpandDrawerForFocus();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	if (drawer_row >= 0 && drawer_row < drawer_view_rows + 1 &&
			separator_cols == 1 && mouse_col == drawer_cols) {
		editorResetDrawerClickTracking();
		E.drawer_resize_active = 1;
		(void)editorDrawerSetWidthForCols(mouse_col, E.window_cols);
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	E.drawer_resize_active = 0;

	if (!(drawer_row >= 0 && drawer_row < drawer_view_rows + 1 &&
				mouse_col >= 0 && mouse_col < drawer_cols)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 0;
	}

	if (drawer_row == 0 && mouse_col < ROTIDE_DRAWER_COLLAPSED_WIDTH) {
		editorResetDrawerClickTracking();
		if (editorDrawerSetCollapsed(1)) {
			editorSetDrawerCollapseStatus(1);
		}
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	if (drawer_row == 0) {
		enum editorDrawerMode header_mode;
		if (editorDrawerHeaderModeForColumn(mouse_col, drawer_cols, &header_mode)) {
			effects = editorSwitchDrawerHeaderMode(header_mode);
			editorResetDrawerClickTracking();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			if (effects_out != NULL) {
				*effects_out = effects;
			}
			return 1;
		}
		editorResetDrawerClickTracking();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	int visible_idx = E.drawer_rowoff + drawer_row - 1;
	if (!editorDrawerSelectVisibleIndex(visible_idx, drawer_view_rows)) {
		editorResetDrawerClickTracking();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}
	if (editorDrawerSelectedIsDirectory()) {
		(void)editorDrawerToggleSelectionExpanded(drawer_view_rows);
		editorResetDrawerClickTracking();
		E.pane_focus = EDITOR_PANE_DRAWER;
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	enum editorAction menu_action = EDITOR_ACTION_COUNT;
	if (editorDrawerSelectedMenuAction(&menu_action)) {
		int mapped_effects = 0;
		editorResetDrawerClickTracking();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (process_mapped_action != NULL &&
				process_mapped_action(menu_action, &mapped_effects)) {
			effects = mapped_effects;
		} else {
			effects = mapped_effects;
		}
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		if (editorOpenSelectedGitDiff()) {
			E.pane_focus = EDITOR_PANE_TEXT;
		}
		editorResetDrawerClickTracking();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = 1;
		}
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		int should_open_location = E.drawer_last_click_visible_idx == visible_idx &&
				E.drawer_last_click_ms > 0 &&
				now_ms > 0 &&
				now_ms - E.drawer_last_click_ms <= double_click_threshold_ms;
		if (should_open_location) {
			if (editorJumpToSelectedLspDrawerLocation(0, jump_to_path)) {
				E.pane_focus = EDITOR_PANE_TEXT;
			}
			editorResetDrawerClickTracking();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			if (effects_out != NULL) {
				*effects_out = 1;
			}
			return 1;
		}
		if (editorJumpToSelectedLspDrawerLocation(1, jump_to_path)) {
			editorSetStatusMsg("LSP location previewed. Double-click to open");
		}
		E.drawer_last_click_visible_idx = visible_idx;
		E.drawer_last_click_ms = now_ms;
		E.pane_focus = EDITOR_PANE_DRAWER;
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = 1;
		}
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		int launch_idx = -1;
		int default_idx = -1;
		if (editorDrawerSelectedDapLaunch(&launch_idx)) {
			E.dap_selected_launch = launch_idx;
			(void)editorDapStartLaunch(launch_idx);
			editorResetDrawerClickTracking();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			if (effects_out != NULL) {
				*effects_out = 1;
			}
			return 1;
		}
		if (editorDrawerSelectedDapDefault(&default_idx)) {
			(void)editorDapCreateProjectLaunchFromDefault(default_idx, E.drawer_root_path);
			editorResetDrawerClickTracking();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			if (effects_out != NULL) {
				*effects_out = 1;
			}
			return 1;
		}
		int should_open_location = E.drawer_last_click_visible_idx == visible_idx &&
				E.drawer_last_click_ms > 0 &&
				now_ms > 0 &&
				now_ms - E.drawer_last_click_ms <= double_click_threshold_ms;
		if (should_open_location) {
			if (editorJumpToSelectedDapDrawerLocation(0, jump_to_path)) {
				E.pane_focus = EDITOR_PANE_TEXT;
			}
			editorResetDrawerClickTracking();
			E.mouse_left_button_down = 0;
			E.mouse_drag_started = 0;
			if (effects_out != NULL) {
				*effects_out = 1;
			}
			return 1;
		}
		if (editorJumpToSelectedDapDrawerLocation(1, jump_to_path)) {
			editorSetStatusMsg("DAP location previewed. Double-click to open");
		}
		E.drawer_last_click_visible_idx = visible_idx;
		E.drawer_last_click_ms = now_ms;
		E.pane_focus = EDITOR_PANE_DRAWER;
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		if (effects_out != NULL) {
			*effects_out = 1;
		}
		return 1;
	}

	int should_open_file = E.drawer_last_click_visible_idx == visible_idx &&
			E.drawer_last_click_ms > 0 &&
			now_ms > 0 &&
			now_ms - E.drawer_last_click_ms <= double_click_threshold_ms;
	if (should_open_file) {
		if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
			if (editorDrawerOpenSelectedFileInTab()) {
				E.pane_focus = EDITOR_PANE_DRAWER;
			}
		} else if (editorActiveTabIsPreview()) {
			editorTabPinActivePreview();
			editorSetStatusMsg("Tab kept open");
			E.pane_focus = EDITOR_PANE_TEXT;
		} else if (editorDrawerOpenSelectedFileInTab()) {
			E.pane_focus = EDITOR_PANE_TEXT;
		}
		editorResetDrawerClickTracking();
	} else {
		if (editorDrawerOpenSelectedFileInPreviewTab()) {
			editorSetStatusMsg("Preview tab opened. Double-click to keep it open");
		}
		E.drawer_last_click_visible_idx = visible_idx;
		E.drawer_last_click_ms = now_ms;
		E.pane_focus = EDITOR_PANE_DRAWER;
	}
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	if (effects_out != NULL) {
		*effects_out = effects;
	}
	return 1;
}

int editorMouseIsOverDrawer(const struct editorMouseEvent *event) {
	if (event == NULL) {
		return 0;
	}
	if (editorDrawerIsCollapsed()) {
		return 0;
	}

	int mouse_col = event->x - 1;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int drawer_view_rows = E.window_rows + 1;
	int drawer_row = event->y - 1;
	return drawer_row >= 0 && drawer_row < drawer_view_rows &&
			mouse_col >= 0 && mouse_col < drawer_cols;
}

int editorHandleMouseWheel(const struct editorMouseEvent *event) {
	int over_drawer = editorMouseIsOverDrawer(event);

	switch (event->kind) {
		case EDITOR_MOUSE_EVENT_WHEEL_UP:
			if (over_drawer) {
				(void)editorDrawerScrollBy(-MOUSE_WHEEL_SCROLL_LINES, E.window_rows);
				break;
			}
			editorViewportScrollByRows(-MOUSE_WHEEL_SCROLL_LINES);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
			if (over_drawer) {
				(void)editorDrawerScrollBy(MOUSE_WHEEL_SCROLL_LINES, E.window_rows);
				break;
			}
			editorViewportScrollByRows(MOUSE_WHEEL_SCROLL_LINES);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_LEFT:
			editorViewportScrollByCols(-MOUSE_WHEEL_SCROLL_COLS);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_RIGHT:
			editorViewportScrollByCols(MOUSE_WHEEL_SCROLL_COLS);
			break;
		default:
			return 0;
	}
	return 1;
}

int editorClearHoverLinkState(void) {
	if (!E.hover_link_active) {
		return 0;
	}
	E.hover_link_active = 0;
	E.hover_link_row = -1;
	E.hover_link_cx_start = 0;
	E.hover_link_cx_end = 0;
	return 1;
}

int editorResolveMouseToBufferOffset(const struct editorMouseEvent *event,
		int clamp_to_viewport, size_t *offset_out) {
	if (event == NULL || offset_out == NULL || E.numrows == 0) {
		return 0;
	}

	/* SGR mouse coordinates are terminal-absolute and 1-based. */
	int raw_col = event->x - 1;
	int raw_row = event->y - 1;
	struct editorRect focused_rect = {0};
	int has_focused_rect = editorLayoutFocusedLeafRect(&focused_rect);
	int pane_y = has_focused_rect ? focused_rect.y : 1;
	int pane_w = has_focused_rect ? focused_rect.w :
			editorDrawerTextViewportCols(E.window_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_w) {
		gutter_cols = pane_w;
	}
	int text_cols = pane_w - gutter_cols;
	int text_start_col = has_focused_rect ?
			focused_rect.x + gutter_cols :
			editorDrawerTextStartColForCols(E.window_cols) + gutter_cols;
	if (text_cols < 1) {
		text_cols = 1;
	}
	if (text_cols >= 3) {
		text_start_col++;
		text_cols -= 2;
	}
	int mouse_row = raw_row - pane_y;
	int mouse_col = raw_col - text_start_col;
	int pane_rows = has_focused_rect ? focused_rect.h : E.window_rows;
	if (clamp_to_viewport) {
		if (pane_rows <= 0 || text_cols <= 0) {
			return 0;
		}
		if (mouse_row < 0) {
			mouse_row = 0;
		}
		if (mouse_row >= pane_rows) {
			mouse_row = pane_rows - 1;
		}
		if (mouse_col < 0) {
			mouse_col = 0;
		}
		if (mouse_col >= text_cols) {
			mouse_col = text_cols - 1;
		}
	} else {
		/* Ignore clicks outside text rows (tab/status/message bars are excluded). */
		if (mouse_row < 0 || mouse_row >= pane_rows || mouse_col < 0 ||
				mouse_col >= text_cols) {
			return 0;
		}
	}

	int row_idx = E.rowoff + mouse_row;
	int segment_coloff = E.coloff;
	int segment_indent_cols = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferPosition(mouse_row, &row_idx, &segment_coloff,
					&segment_indent_cols)) {
			return 0;
		}
	}
	if (clamp_to_viewport) {
		if (row_idx < 0) {
			row_idx = 0;
		}
		if (row_idx >= E.numrows) {
			row_idx = E.numrows - 1;
		}
	} else {
		/* Ignore filler rows beyond the end of file. */
		if (row_idx < 0 || row_idx >= E.numrows) {
			return 0;
		}
	}

	int target_rx = segment_coloff + mouse_col - segment_indent_cols;
	if (target_rx < segment_coloff) {
		target_rx = segment_coloff;
	}

	/* Convert rendered column -> buffer byte index while respecting boundaries. */
	int cx = editorRowRxToCx(&E.rows[row_idx], target_rx);
	return editorBufferPosToOffset(row_idx, cx, offset_out);
}

static int editorMouseSetCursorFromOffset(size_t offset) {
	int cy = 0;
	int cx = 0;
	size_t normalized_offset = 0;

	if (!editorBufferOffsetToPos(offset, &cy, &cx)) {
		return 0;
	}
	if (cy < E.numrows) {
		struct erow *row = &E.rows[cy];
		cx = editorRowClampCxToCharBoundary(row, cx);
		cx = editorRowClampCxToClusterBoundary(row, cx);
	} else {
		cx = 0;
	}
	if (!editorBufferPosToOffset(cy, cx, &normalized_offset)) {
		return 0;
	}
	E.cursor_offset = normalized_offset;
	E.cy = cy;
	E.cx = cx;
	return 1;
}

int editorMoveCursorToMouse(const struct editorMouseEvent *event, int clamp_to_viewport) {
	size_t offset = 0;
	if (!editorResolveMouseToBufferOffset(event, clamp_to_viewport, &offset)) {
		return 0;
	}
	return editorMouseSetCursorFromOffset(offset);
}

static int editorMouseIsWordByte(unsigned char b) {
	return isalnum(b) || b == '_' || b >= 0x80;
}

static int editorComputeWordRangeAt(int cy, int cx, int *start_out, int *end_out) {
	if (cy < 0 || cy >= E.numrows) {
		return 0;
	}
	struct erow *row = &E.rows[cy];
	cx = editorRowClampCxToCharBoundary(row, cx);
	if (cx >= row->size) {
		return 0;
	}
	if (!editorMouseIsWordByte((unsigned char)row->chars[cx])) {
		return 0;
	}
	int start = cx;
	while (start > 0) {
		int prev = editorRowPrevCharIdx(row, start);
		if (prev >= start || !editorMouseIsWordByte((unsigned char)row->chars[prev])) {
			break;
		}
		start = prev;
	}
	int end = editorRowNextCharIdx(row, cx);
	while (end < row->size) {
		if (!editorMouseIsWordByte((unsigned char)row->chars[end])) {
			break;
		}
		int next = editorRowNextCharIdx(row, end);
		if (next <= end) {
			break;
		}
		end = next;
	}
	*start_out = start;
	*end_out = end;
	return 1;
}

int editorHandleMouseMotion(const struct editorMouseEvent *event) {
	int new_active = 0;
	int new_row = -1;
	int new_start = 0;
	int new_end = 0;

	if ((event->modifiers & EDITOR_MOUSE_MOD_CTRL) != 0 && E.pane_focus == EDITOR_PANE_TEXT &&
			editorLspFileSupportsDefinition(E.filename, E.syntax_language) &&
			editorLspFileEnabled(E.filename, E.syntax_language)) {
		size_t offset = 0;
		if (editorResolveMouseToBufferOffset(event, 0, &offset)) {
			int row = 0;
			int cx = 0;
			if (editorBufferOffsetToPos(offset, &row, &cx) &&
					editorComputeWordRangeAt(row, cx, &new_start, &new_end)) {
				new_active = 1;
				new_row = row;
			}
		}
	}

	if (new_active == E.hover_link_active && new_row == E.hover_link_row &&
			new_start == E.hover_link_cx_start && new_end == E.hover_link_cx_end) {
		return 0;
	}
	E.hover_link_active = new_active;
	E.hover_link_row = new_row;
	E.hover_link_cx_start = new_start;
	E.hover_link_cx_end = new_end;
	return 1;
}

int editorHandleMouseLeftDrag(const struct editorMouseEvent *event) {
	if (E.drawer_resize_active) {
		int mouse_col = event->x - 1;
		(void)editorDrawerSetWidthForCols(mouse_col, E.window_cols);
		return 0;
	}
	if (!E.mouse_left_button_down) {
		return 0;
	}
	if (E.column_select_active) {
		/* Resolve mouse to an rx/cy and extend the column selection there. */
		int mouse_col = event->x - 1;
		struct editorRect focused_rect = {0};
		int has_focused_rect = editorLayoutFocusedLeafRect(&focused_rect);
		int pane_w = has_focused_rect ? focused_rect.w :
				editorDrawerTextViewportCols(E.window_cols);
		int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
		if (gutter_cols > pane_w) {
			gutter_cols = pane_w;
		}
		int text_cols = pane_w - gutter_cols;
		int text_start = has_focused_rect ?
				focused_rect.x + gutter_cols :
				editorDrawerTextStartColForCols(E.window_cols) + gutter_cols;
		if (text_cols < 1) {
			text_cols = 1;
		}
		if (text_cols >= 3) {
			text_start++;
			text_cols -= 2;
		}
		int rel_col = mouse_col - text_start;
		if (rel_col < 0) {
			rel_col = 0;
		}
		if (rel_col > text_cols) {
			rel_col = text_cols;
		}
		int target_rx = E.coloff + rel_col;
		if (target_rx < 0) {
			target_rx = 0;
		}
		if (!editorMoveCursorToMouse(event, 1)) {
			return 0;
		}
		E.column_select_cursor_rx = target_rx;
		if (E.cy < E.numrows) {
			int new_cx = editorRowRxToCx(&E.rows[E.cy], target_rx);
			if (new_cx > E.rows[E.cy].size) {
				new_cx = E.rows[E.cy].size;
			}
			size_t off = 0;
			if (editorBufferPosToOffset(E.cy, new_cx, &off)) {
				(void)editorSyncCursorFromOffsetByteBoundary(off);
			}
		}
		E.mouse_drag_started = 1;
		return 1;
	}
	if (!editorMoveCursorToMouse(event, 1)) {
		return 0;
	}

	if (!E.mouse_drag_started) {
		/* A new drag always starts a fresh selection anchored at the initial press point. */
		editorColumnSelectionClear();
		E.selection_mode_active = 1;
		E.selection_anchor_offset = E.mouse_drag_anchor_offset;
		E.mouse_drag_started = 1;
	}
	return 1;
}

int editorHandleMouseLeftRelease(void) {
	E.drawer_resize_active = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return 0;
}

int editorHandleMouseEventInTerminalPane(const struct editorMouseEvent *event) {
	if (E.layout_root == NULL) {
		return 0;
	}
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeBorderedInto(E.layout_root, viewport,
				ROTIDE_PANE_BORDER_SIZE, &layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	int sx = event->x - 1;
	int sy = event->y - 1;
	struct editorPaneNode *leaf = editorLayoutLeafAt(&layout, sx, sy);
	editorLeafLayoutFree(&layout);
	if (leaf == NULL || leaf->is_split ||
			leaf->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL ||
			leaf->as.leaf.kind_state == NULL) {
		return 0;
	}
	struct editorTerminalPane *terminal =
			(struct editorTerminalPane *)leaf->as.leaf.kind_state;
	if (terminal->mouse_tracking <= 0) {
		return 0;
	}
	/* Click-to-focus for terminal panes. */
	if (event->kind == EDITOR_MOUSE_EVENT_LEFT_PRESS && leaf != E.focused_leaf) {
		(void)editorLayoutSetFocusedLeaf(leaf);
		editorPaneAnnounceFocus();
	}
	struct editorRect leaf_rect = {0};
	if (!editorLayoutLeafRectBordered(E.layout_root, viewport,
				ROTIDE_PANE_BORDER_SIZE, leaf, &leaf_rect)) {
		return 1;
	}
	int row = sy - leaf_rect.y;
	int col = sx - leaf_rect.x;
	switch (event->kind) {
	case EDITOR_MOUSE_EVENT_LEFT_PRESS:
		(void)editorTerminalPaneSendMouseButton(terminal, 1, 1, row, col,
				event->modifiers);
		break;
	case EDITOR_MOUSE_EVENT_LEFT_RELEASE:
		(void)editorTerminalPaneSendMouseButton(terminal, 1, 0, row, col,
				event->modifiers);
		break;
	case EDITOR_MOUSE_EVENT_LEFT_DRAG:
		(void)editorTerminalPaneSendMouseMove(terminal, row, col,
				event->modifiers);
		break;
	case EDITOR_MOUSE_EVENT_MOTION:
		if (terminal->mouse_tracking >= 3 /* VTERM_PROP_MOUSE_MOVE */) {
			(void)editorTerminalPaneSendMouseMove(terminal, row, col,
					event->modifiers);
		}
		break;
	case EDITOR_MOUSE_EVENT_WHEEL_UP:
		(void)editorTerminalPaneSendMouseButton(terminal, 4, 1, row, col,
				event->modifiers);
		break;
	case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
		(void)editorTerminalPaneSendMouseButton(terminal, 5, 1, row, col,
				event->modifiers);
		break;
	default:
		break;
	}
	return 1;
}
