#include "input/mouse.h"

#include "render/viewport.h"
#include "terminal/terminal_pane.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"

enum {
	MOUSE_WHEEL_SCROLL_LINES = 3,
	MOUSE_WHEEL_SCROLL_COLS = 3
};

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
