#include "input/mouse.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "debug/dap_console.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "input/actions_workspace.h"
#include "input/dispatch.h"
#include "language/lsp.h"
#include "render/popup.h"
#include "render/screen.h"
#include "render/status_bar.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/terminal.h"
#include "terminal/terminal_pane.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
	MOUSE_WHEEL_SCROLL_LINES = 3,
	MOUSE_WHEEL_SCROLL_COLS = 3,
	MOUSE_DOUBLE_CLICK_THRESHOLD_MS = 400,
	MOUSE_TAB_DRAG_THRESHOLD_COLS = 1
};

#define DRAWER_HEADER_MODE_BUTTON_COLS 3
#define DRAWER_HEADER_MODE_BUTTON_COUNT 7
#define DRAWER_HEADER_MODE_BUTTONS_MIN_COLS                                                        \
	(ROTIDE_DRAWER_COLLAPSED_WIDTH +                                                           \
	 DRAWER_HEADER_MODE_BUTTON_COLS * DRAWER_HEADER_MODE_BUTTON_COUNT)

static void mouseClearSelectionMode(void);
static void mouseSelectWordAtCursor(void);
static void mouseSelectLineAtCursor(void);
static void mouseClearTabDrag(void);
static int mouseEventInFocusedPaneTextArea(const struct editorMouseEvent *event);
static int mouseSetCursorToBufferEnd(void);

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

static void mouseClearTabDrag(void) {
	E.tab_drag_armed = 0;
	E.tab_drag_active = 0;
	E.tab_drag_source_leaf = NULL;
	E.tab_drag_source_tab_idx = -1;
	E.tab_drag_start_x = 0;
	E.tab_drag_start_y = 0;
}

static void mouseArmTabDrag(struct editorPaneNode *leaf, int tab_idx,
                            const struct editorMouseEvent *event) {
	E.tab_drag_armed = 1;
	E.tab_drag_active = 0;
	E.tab_drag_source_leaf = leaf;
	E.tab_drag_source_tab_idx = tab_idx;
	E.tab_drag_start_x = event->x - 1;
	E.tab_drag_start_y = event->y - 1;
}

enum { MOUSE_DRAWER_DRAG_THRESHOLD_COLS = 2, MOUSE_DRAWER_DRAG_THRESHOLD_ROWS = 1 };

static void mouseClearDrawerDrag(void) {
	E.drawer_drag_armed = 0;
	E.drawer_drag_active = 0;
	E.drawer_drag_source_visible_idx = -1;
	free(E.drawer_drag_source_path);
	E.drawer_drag_source_path = NULL;
	E.drawer_drag_just_opened_preview = 0;
	E.drawer_drag_start_x = 0;
	E.drawer_drag_start_y = 0;
}

static void mouseArmDrawerDrag(int visible_idx, int just_opened_preview,
                               const struct editorMouseEvent *event) {
	if (E.drawer_mode != EDITOR_DRAWER_MODE_TREE || editorFileSearchIsActive() ||
	    editorProjectSearchIsActive() || editorDrawerSelectedIsRoot()) {
		return;
	}
	const char *path = editorDrawerSelectedPath();
	if (path == NULL || path[0] == '\0') {
		return;
	}
	mouseClearDrawerDrag();
	E.drawer_drag_armed = 1;
	E.drawer_drag_source_visible_idx = visible_idx;
	E.drawer_drag_source_path = strdup(path);
	if (E.drawer_drag_source_path == NULL) {
		mouseClearDrawerDrag();
		return;
	}
	E.drawer_drag_just_opened_preview = just_opened_preview;
	E.drawer_drag_start_x = event->x - 1;
	E.drawer_drag_start_y = event->y - 1;
}

static int mouseResolveDrawerDropTargetDir(const struct editorMouseEvent *event, char **out_path) {
	*out_path = NULL;
	int mouse_col = event->x - 1;
	int drawer_row = event->y - 1;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int drawer_view_rows = E.window_rows;
	if (mouse_col < 0 || mouse_col >= drawer_cols) {
		return 0;
	}
	if (drawer_row < 1 || drawer_row > drawer_view_rows + 1) {
		return 0;
	}
	int visible_idx = E.drawer_rowoff + drawer_row - 1;
	struct editorDrawerEntryView view = {0};
	if (!editorDrawerVisibleEntryView(visible_idx, &view) || view.path == NULL) {
		return 0;
	}
	if (view.is_dir || view.is_root) {
		*out_path = strdup(view.path);
		return *out_path != NULL;
	}
	const char *slash = strrchr(view.path, '/');
	if (slash == NULL || slash == view.path) {
		if (E.drawer_root_path == NULL) {
			return 0;
		}
		*out_path = strdup(E.drawer_root_path);
		return *out_path != NULL;
	}
	size_t parent_len = (size_t)(slash - view.path);
	*out_path = malloc(parent_len + 1);
	if (*out_path == NULL) {
		return 0;
	}
	memcpy(*out_path, view.path, parent_len);
	(*out_path)[parent_len] = '\0';
	return 1;
}

static int mouseFinishDrawerDrag(const struct editorMouseEvent *event) {
	if (E.drawer_drag_source_path == NULL || E.drawer_drag_source_visible_idx < 0) {
		return 0;
	}
	char *target_dir = NULL;
	if (!mouseResolveDrawerDropTargetDir(event, &target_dir)) {
		editorSetStatusMsg("Drop target is not a folder");
		return 0;
	}
	/* Re-select the source row in case motion changed the cursor's hover. */
	if (!editorDrawerSelectVisibleIndex(E.drawer_drag_source_visible_idx, E.window_rows)) {
		free(target_dir);
		return 0;
	}
	/* If this click opened a preview tab, close it: the drag overrode the click. */
	if (E.drawer_drag_just_opened_preview && editorActiveTabIsPreview() && E.filename != NULL &&
	    strcmp(E.filename, E.drawer_drag_source_path) == 0) {
		(void)editorTabCloseActive();
	}
	int ok = editorDrawerMoveSelectionToDir(target_dir, E.window_rows);
	free(target_dir);
	return ok;
}

static int mousePaneTabSlotForColumn(struct editorPaneView *view, int col, int cols) {
	if (view == NULL) {
		return -1;
	}
	if (view->pane_tab_count == 0) {
		return 0;
	}
	int tab_idx = editorTabOverflowHitTestColumnForPane(view, col, cols);
	if (tab_idx < 0) {
		tab_idx = editorTabHitTestColumnForPane(view, col, cols);
	}
	if (tab_idx >= 0) {
		return editorPaneViewIndexOfTab(view, tab_idx);
	}
	return view->pane_tab_count;
}

static int mouseResolvePaneTabStripCell(const struct editorMouseEvent *event,
                                        struct editorPaneNode **leaf_out, int *local_col_out,
                                        int *strip_cols_out) {
	if (event == NULL || E.layout_root == NULL) {
		return 0;
	}
	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeBorderedInto(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                     &layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	int mouse_col = event->x - 1;
	int mouse_row = event->y - 1;
	int ok = editorLayoutPaneTabStripAt(&layout, mouse_col, mouse_row, leaf_out, local_col_out,
	                                    strip_cols_out);
	editorLeafLayoutFree(&layout);
	return ok;
}

static int mouseResolvePaneTabStripTarget(const struct editorMouseEvent *event,
                                          struct editorPaneNode **leaf_out, int *tab_idx_out) {
	struct editorPaneNode *leaf = NULL;
	int tab_col = 0;
	int tab_cols = 0;
	if (!mouseResolvePaneTabStripCell(event, &leaf, &tab_col, &tab_cols) || leaf == NULL ||
	    leaf->is_split) {
		return 0;
	}

	int mouse_col = event->x - 1;
	int mouse_row = event->y - 1;
	if (mouse_row == 0 && editorDrawerIsCollapsed() &&
	    editorPaneTreeLeafCount(E.layout_root) <= 1) {
		int toggle_cols = editorDrawerCollapsedToggleWidthForCols(E.window_cols);
		if (mouse_col < toggle_cols) {
			return 0;
		}
		tab_col = mouse_col - toggle_cols;
		tab_cols = E.window_cols - toggle_cols;
	}

	struct editorPaneView *view = &leaf->as.leaf.view;
	int tab_idx = editorTabOverflowHitTestColumnForPane(view, tab_col, tab_cols);
	if (tab_idx < 0) {
		tab_idx = editorTabHitTestColumnForPane(view, tab_col, tab_cols);
	}
	if (leaf_out != NULL) {
		*leaf_out = leaf;
	}
	if (tab_idx_out != NULL) {
		*tab_idx_out = tab_idx;
	}
	return 1;
}

static int mouseFinishTabDrag(const struct editorMouseEvent *event) {
	struct editorPaneNode *source = E.tab_drag_source_leaf;
	int tab_idx = E.tab_drag_source_tab_idx;
	if (source == NULL || source->is_split || tab_idx < 0 || E.layout_root == NULL ||
	    !editorPaneNodeContainsLeaf(E.layout_root, source)) {
		return 0;
	}

	struct editorPaneNode *target = NULL;
	int target_col = 0;
	int target_cols = 0;
	if (!mouseResolvePaneTabStripCell(event, &target, &target_col, &target_cols) ||
	    target == NULL || target->is_split) {
		return 0;
	}

	int target_slot = mousePaneTabSlotForColumn(&target->as.leaf.view, target_col, target_cols);
	if (target_slot < 0) {
		return 0;
	}
	return editorPaneMoveTab(source, target, tab_idx, target_slot);
}

int editorHandleMousePaneTabStripClick(const struct editorMouseEvent *event, long long now_ms) {
	if (event == NULL || E.layout_root == NULL) {
		return 0;
	}

	int mouse_col = event->x - 1;
	int mouse_row = event->y - 1;
	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeBorderedInto(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                     &layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}

	struct editorPaneNode *leaf = NULL;
	int tab_col = 0;
	int tab_cols = 0;
	int on_strip = editorLayoutPaneTabStripAt(&layout, mouse_col, mouse_row, &leaf, &tab_col,
	                                          &tab_cols);
	editorLeafLayoutFree(&layout);
	if (!on_strip || leaf == NULL || leaf->is_split) {
		return 0;
	}
	if (mouse_row == 0 && editorDrawerIsCollapsed() &&
	    editorPaneTreeLeafCount(E.layout_root) <= 1) {
		int toggle_cols = editorDrawerCollapsedToggleWidthForCols(E.window_cols);
		if (mouse_col < toggle_cols) {
			return 0;
		}
		tab_col = mouse_col - toggle_cols;
		tab_cols = E.window_cols - toggle_cols;
	}

	struct editorPaneView *view = &leaf->as.leaf.view;
	int tab_idx = editorTabOverflowHitTestColumnForPane(view, tab_col, tab_cols);
	if (tab_idx >= 0) {
		(void)editorLayoutSetFocusedLeaf(leaf);
		(void)editorTabSwitchToIndex(tab_idx);
		mouseArmTabDrag(leaf, tab_idx, event);
		editorResetTabClickTracking();
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return 1;
	}

	tab_idx = editorTabHitTestColumnForPane(view, tab_col, tab_cols);
	if (tab_idx >= 0) {
		int is_double_click =
		        E.tab_last_click_idx == tab_idx && E.tab_last_click_ms > 0 && now_ms > 0 &&
		        now_ms - E.tab_last_click_ms <= MOUSE_DOUBLE_CLICK_THRESHOLD_MS;
		(void)editorLayoutSetFocusedLeaf(leaf);
		(void)editorTabSwitchToIndex(tab_idx);
		mouseArmTabDrag(leaf, tab_idx, event);
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
		return 0;
	}
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return 1;
}

static void mouseDrawerFinishPress(int *effects_out, int effects) {
	editorResetDrawerClickTracking();
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	if (effects_out != NULL) {
		*effects_out = effects;
	}
}

static void mouseDrawerRememberPreviewClick(int visible_idx, long long now_ms, int *effects_out) {
	E.drawer_last_click_visible_idx = visible_idx;
	E.drawer_last_click_ms = now_ms;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	if (effects_out != NULL) {
		*effects_out = 1;
	}
}

static int mouseDrawerIsDoubleClickOnSame(int visible_idx, long long now_ms,
                                          int double_click_threshold_ms) {
	return E.drawer_last_click_visible_idx == visible_idx && E.drawer_last_click_ms > 0 &&
	       now_ms > 0 && now_ms - E.drawer_last_click_ms <= double_click_threshold_ms;
}

static int mouseDrawerHandleHeaderRow(int mouse_col, int drawer_cols, int *effects_out) {
	if (mouse_col < ROTIDE_DRAWER_COLLAPSED_WIDTH) {
		if (editorDrawerSetCollapsed(1)) {
			editorSetDrawerCollapseStatus(1);
		}
		mouseDrawerFinishPress(effects_out, 0);
		return 1;
	}
	enum editorDrawerMode header_mode;
	int effects = 0;
	if (editorDrawerHeaderModeForColumn(mouse_col, drawer_cols, &header_mode)) {
		effects = editorSwitchDrawerHeaderMode(header_mode);
	}
	mouseDrawerFinishPress(effects_out, effects);
	return 1;
}

static int mouseDrawerHandleDirectoryToggle(int visible_idx, int drawer_view_rows,
                                            const struct editorMouseEvent *event,
                                            int *effects_out) {
	(void)editorDrawerToggleSelectionExpanded(drawer_view_rows);
	editorResetDrawerClickTracking();
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	mouseArmDrawerDrag(visible_idx, 0, event);
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	if (effects_out != NULL) {
		*effects_out = 0;
	}
	return 1;
}

static int mouseDrawerHandleMenuAction(editorProcessMappedActionFn process_mapped_action,
                                       int *effects_out) {
	enum editorAction menu_action = EDITOR_ACTION_COUNT;
	if (!editorDrawerSelectedMenuAction(&menu_action)) {
		return 0;
	}
	int mapped_effects = 0;
	if (process_mapped_action != NULL) {
		(void)process_mapped_action(menu_action, &mapped_effects);
	}
	mouseDrawerFinishPress(effects_out, mapped_effects);
	return 1;
}

static int mouseDrawerHandleGitMode(editorProcessMappedActionFn process_mapped_action,
                                    int *effects_out) {
	enum editorAction git_action = EDITOR_ACTION_COUNT;
	if (editorDrawerGitSelectedAction(&git_action)) {
		if (process_mapped_action != NULL) {
			int mapped = 0;
			(void)process_mapped_action(git_action, &mapped);
		}
		mouseDrawerFinishPress(effects_out, 1);
		return 1;
	}
	if (editorOpenSelectedGitDiff()) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
	mouseDrawerFinishPress(effects_out, 1);
	return 1;
}

static int mouseDrawerHandleLspMode(int visible_idx, long long now_ms,
                                    int double_click_threshold_ms,
                                    editorMouseJumpToPathFn jump_to_path, int *effects_out) {
	if (mouseDrawerIsDoubleClickOnSame(visible_idx, now_ms, double_click_threshold_ms)) {
		if (editorJumpToSelectedLspDrawerLocation(0, jump_to_path)) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		}
		mouseDrawerFinishPress(effects_out, 1);
		return 1;
	}
	if (editorJumpToSelectedLspDrawerLocation(1, jump_to_path)) {
		editorSetStatusMsg("LSP location previewed. Double-click to open");
	}
	mouseDrawerRememberPreviewClick(visible_idx, now_ms, effects_out);
	return 1;
}

static int mouseDrawerHandleDapMode(int visible_idx, long long now_ms,
                                    int double_click_threshold_ms,
                                    editorMouseJumpToPathFn jump_to_path, int *effects_out) {
	int launch_idx = -1;
	int default_idx = -1;
	if (editorDrawerSelectedDapLaunch(&launch_idx)) {
		E.dap_selected_launch = launch_idx;
		(void)editorDapStartLaunch(launch_idx);
		mouseDrawerFinishPress(effects_out, 1);
		return 1;
	}
	if (editorDrawerSelectedDapDefault(&default_idx)) {
		(void)editorDapCreateProjectLaunchFromDefault(default_idx, E.drawer_root_path);
		mouseDrawerFinishPress(effects_out, 1);
		return 1;
	}
	if (mouseDrawerIsDoubleClickOnSame(visible_idx, now_ms, double_click_threshold_ms)) {
		if (editorJumpToSelectedDapDrawerLocation(0, jump_to_path)) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		}
		mouseDrawerFinishPress(effects_out, 1);
		return 1;
	}
	if (editorJumpToSelectedDapDrawerLocation(1, jump_to_path)) {
		editorSetStatusMsg("DAP location previewed. Double-click to open");
	}
	mouseDrawerRememberPreviewClick(visible_idx, now_ms, effects_out);
	return 1;
}

static void mouseDrawerOpenFileOnDoubleClick(void) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		if (editorDrawerOpenSelectedFileInTab()) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
		}
	} else if (editorActiveTabIsPreview()) {
		editorTabPinActivePreview();
		editorSetStatusMsg("Tab kept open");
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	} else if (editorDrawerOpenSelectedFileInTab()) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
}

static int mouseDrawerHandleFileMode(int visible_idx, long long now_ms,
                                     int double_click_threshold_ms,
                                     const struct editorMouseEvent *event, int *effects_out) {
	if (mouseDrawerIsDoubleClickOnSame(visible_idx, now_ms, double_click_threshold_ms)) {
		mouseDrawerOpenFileOnDoubleClick();
		editorResetDrawerClickTracking();
	} else {
		int opened_preview = editorDrawerOpenSelectedFileInPreviewTab();
		if (opened_preview) {
			editorSetStatusMsg("Preview tab opened. Double-click to keep it open");
		}
		E.drawer_last_click_visible_idx = visible_idx;
		E.drawer_last_click_ms = now_ms;
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
		mouseArmDrawerDrag(visible_idx, opened_preview, event);
	}
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	if (effects_out != NULL) {
		*effects_out = 0;
	}
	return 1;
}

int editorHandleMouseDrawerLeftPress(const struct editorMouseEvent *event, long long now_ms,
                                     int double_click_threshold_ms,
                                     editorProcessMappedActionFn process_mapped_action,
                                     editorMouseJumpToPathFn jump_to_path, int *effects_out) {
	int mouse_col = event->x - 1;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int collapsed_toggle_cols = editorDrawerIsCollapsed()
	                                    ? editorDrawerCollapsedToggleWidthForCols(E.window_cols)
	                                    : 0;
	int drawer_view_rows = E.window_rows;
	int drawer_row = event->y - 1;

	if (editorDrawerIsCollapsed() && drawer_row == 0 && mouse_col >= 0 &&
	    mouse_col < collapsed_toggle_cols) {
		editorExpandDrawerForFocus();
		mouseDrawerFinishPress(effects_out, 0);
		return 1;
	}

	if (drawer_row >= 0 && drawer_row < drawer_view_rows + 1 && separator_cols == 1 &&
	    mouse_col == drawer_cols) {
		E.drawer_resize_active = 1;
		(void)editorDrawerSetWidthForCols(mouse_col, E.window_cols);
		mouseDrawerFinishPress(effects_out, 0);
		return 1;
	}
	E.drawer_resize_active = 0;

	if (!(drawer_row >= 0 && drawer_row < drawer_view_rows + 1 && mouse_col >= 0 &&
	      mouse_col < drawer_cols)) {
		if (effects_out != NULL) {
			*effects_out = 0;
		}
		return 0;
	}

	if (drawer_row == 0) {
		return mouseDrawerHandleHeaderRow(mouse_col, drawer_cols, effects_out);
	}

	int visible_idx = E.drawer_rowoff + drawer_row - 1;
	if (!editorDrawerSelectVisibleIndex(visible_idx, drawer_view_rows)) {
		mouseDrawerFinishPress(effects_out, 0);
		return 1;
	}
	if (editorDrawerSelectedIsDirectory()) {
		return mouseDrawerHandleDirectoryToggle(visible_idx, drawer_view_rows, event,
		                                        effects_out);
	}
	if (mouseDrawerHandleMenuAction(process_mapped_action, effects_out)) {
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return mouseDrawerHandleGitMode(process_mapped_action, effects_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return mouseDrawerHandleLspMode(visible_idx, now_ms, double_click_threshold_ms,
		                                jump_to_path, effects_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return mouseDrawerHandleDapMode(visible_idx, now_ms, double_click_threshold_ms,
		                                jump_to_path, effects_out);
	}
	return mouseDrawerHandleFileMode(visible_idx, now_ms, double_click_threshold_ms, event,
	                                 effects_out);
}

/*
 * Maps a press to the buffer row whose line-number gutter it lands on, or
 * returns 0 if the press is outside the focused pane's gutter columns. Used so
 * a gutter click toggles a breakpoint instead of moving the cursor.
 */
static int mouseGutterRowAt(const struct editorMouseEvent *event, int *row_out) {
	if (event == NULL || row_out == NULL || E.numrows == 0) {
		return 0;
	}
	int raw_col = event->x - 1;
	int raw_row = event->y - 1;
	struct editorRect rect = {0};
	int has_rect = editorLayoutFocusedLeafRect(&rect);
	int pane_x = has_rect ? rect.x : editorDrawerTextStartColForCols(E.window_cols);
	int pane_y = has_rect ? rect.y : 1;
	int pane_w = has_rect ? rect.w : editorDrawerTextViewportCols(E.window_cols);
	int pane_rows = has_rect ? rect.h : E.window_rows;
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_w) {
		gutter_cols = pane_w;
	}
	if (gutter_cols <= 0 || raw_col < pane_x || raw_col >= pane_x + gutter_cols) {
		return 0;
	}
	int mouse_row = raw_row - pane_y;
	if (mouse_row < 0 || mouse_row >= pane_rows) {
		return 0;
	}
	int row_idx = E.rowoff + mouse_row;
	int segment_coloff = E.coloff;
	int segment_indent_cols = 0;
	if (E.line_wrap_enabled &&
	    !editorViewportTextScreenRowToBufferPosition(mouse_row, &row_idx, &segment_coloff,
	                                                 &segment_indent_cols)) {
		return 0;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}
	*row_out = row_idx;
	return 1;
}

int editorHandleMouseTextLeftPress(const struct editorMouseEvent *event, long long now_ms,
                                   int multi_click_threshold_ms,
                                   editorMouseActionFn goto_definition, int *effects_out) {
	int mouse_col = event->x - 1;
	int effects = 0;
	int apply_mouse_state = 0;
	int left_button_down = 0;
	int drag_started = 0;
	int set_drag_anchor = 0;
	int reset_click_tracking = 0;
	int run_goto_definition = 0;

	if (editorHandleMousePaneTabStripClick(event, now_ms)) {
		goto out;
	}

	/* Border-press arms a drag-resize; mirrors the drawer-separator path. */
	if (E.layout_root != NULL) {
		struct editorRect viewport = {0};
		if (editorLayoutEditorViewport(&viewport)) {
			struct editorPaneNode *border_node = NULL;
			enum editorSplitOrientation border_orientation = EDITOR_SPLIT_VERTICAL;
			if (editorLayoutBorderAt(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
			                         mouse_col, event->y - 1, &border_node,
			                         &border_orientation)) {
				E.split_resize_active = 1;
				E.split_resize_node = border_node;
				apply_mouse_state = 1;
				left_button_down = 0;
				drag_started = 0;
				reset_click_tracking = 1;
				goto out;
			}
		}
	}

	if (editorLayoutFocusLeafAt(mouse_col, event->y - 1)) {
		editorPaneAnnounceFocus();
	}

	int gutter_row = 0;
	if (event->modifiers == EDITOR_MOUSE_MOD_NONE && mouseGutterRowAt(event, &gutter_row)) {
		(void)editorDapToggleBreakpointAtLine(gutter_row);
		/* Move the cursor to the start of the toggled row so the follow-cursor
		 * viewport settles on the (visible) breakpoint line instead of snapping
		 * back to the previous, possibly off-screen, cursor. */
		size_t bp_offset = 0;
		if (editorBufferPosToOffset(gutter_row, 0, &bp_offset)) {
			mouseClearSelectionMode();
			E.cursor_offset = bp_offset;
			E.cy = gutter_row;
			E.cx = 0;
		}
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		apply_mouse_state = 1;
		left_button_down = 0;
		drag_started = 0;
		reset_click_tracking = 1;
		effects = 1;
		goto out;
	}

	if (!editorMoveCursorToMouse(event, 0)) {
		/* A press below the last line focuses the pane and drops the cursor at
		 * end of buffer rather than being ignored, as most editors do. */
		if (!mouseEventInFocusedPaneTextArea(event) || !mouseSetCursorToBufferEnd()) {
			apply_mouse_state = 1;
			left_button_down = 0;
			drag_started = 0;
			reset_click_tracking = 1;
			goto out;
		}
	}

	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	mouseClearSelectionMode();
	if (event->modifiers == EDITOR_MOUSE_MOD_CTRL) {
		apply_mouse_state = 1;
		left_button_down = 0;
		drag_started = 0;
		reset_click_tracking = 1;
		run_goto_definition = 1;
		effects = 1;
		goto out;
	}

	int column_modifier = E.column_select_drag_modifier;
	if (column_modifier != 0 && event->modifiers == column_modifier) {
		E.column_select_active = 1;
		E.column_select_anchor_cy = E.cy;
		E.column_select_anchor_rx = 0;
		if (E.cy < E.numrows) {
			struct editorLineView line = {0};
			if (editorDocumentLineView(E.document, E.cy, &line)) {
				E.column_select_anchor_rx =
				        editorBytesCxToRx(line.data, line.size, E.cx);
				editorLineViewRelease(&line);
			}
		}
		E.column_select_cursor_rx = E.column_select_anchor_rx;
		apply_mouse_state = 1;
		left_button_down = 1;
		drag_started = 0;
		set_drag_anchor = 1;
		reset_click_tracking = 1;
		effects = 1;
		goto out;
	}

	int click_count = 1;
	if (event->modifiers == EDITOR_MOUSE_MOD_NONE && E.text_last_click_ms > 0 && now_ms > 0 &&
	    now_ms - E.text_last_click_ms <= multi_click_threshold_ms &&
	    E.text_last_click_cy == E.cy && E.text_last_click_cx == E.cx) {
		click_count = E.text_click_count + 1;
		if (click_count > 3) {
			click_count = 1;
		}
	}
	E.text_click_count = click_count;
	E.text_last_click_cy = E.cy;
	E.text_last_click_cx = E.cx;
	E.text_last_click_ms = now_ms;

	if (click_count == 2) {
		/* In git views double-click activates the row (checkout / show)
		 * instead of selecting a word. */
		if (E.tab_kind == EDITOR_TAB_GIT_BRANCHES || E.tab_kind == EDITOR_TAB_GIT_LOG ||
		    E.tab_kind == EDITOR_TAB_GIT_STASH) {
			int mapped = 0;
			(void)editorDispatchProcessMappedAction(EDITOR_ACTION_GIT_VIEW_ACTIVATE,
			                                        &mapped);
		} else {
			mouseSelectWordAtCursor();
		}
		apply_mouse_state = 1;
		left_button_down = 0;
		drag_started = 0;
		effects = 1;
		goto out;
	}
	if (click_count == 3) {
		mouseSelectLineAtCursor();
		apply_mouse_state = 1;
		left_button_down = 0;
		drag_started = 0;
		effects = 1;
		goto out;
	}

	apply_mouse_state = 1;
	left_button_down = 1;
	drag_started = 0;
	set_drag_anchor = 1;
	effects = 1;

out:
	if (apply_mouse_state) {
		E.mouse_left_button_down = left_button_down;
		if (set_drag_anchor) {
			E.mouse_drag_anchor_offset = E.cursor_offset;
		}
		E.mouse_drag_started = drag_started;
	}
	if (reset_click_tracking) {
		editorResetTextClickTracking();
	}
	if (run_goto_definition && goto_definition != NULL) {
		goto_definition();
	}
	if (effects_out != NULL) {
		*effects_out = effects;
	}
	return 1;
}

static long long mouseMonotonicMillis(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

#define MOUSE_EDITOR_CONTEXT_MAX_ITEMS 8
#define MOUSE_TAB_CONTEXT_MAX_ITEMS 2

static enum editorAction g_mouse_editor_context_actions[MOUSE_EDITOR_CONTEXT_MAX_ITEMS];
static int g_mouse_editor_context_action_count;
static int g_mouse_editor_context_has_offset;
static size_t g_mouse_editor_context_offset;
static enum editorAction g_mouse_tab_context_actions[MOUSE_TAB_CONTEXT_MAX_ITEMS];
static int g_mouse_tab_context_action_count;
static struct editorPaneNode *g_mouse_tab_context_leaf;
static int g_mouse_tab_context_tab_idx;

static int mouseSetCursorFromOffset(size_t offset);
static void mouseClearSelectionMode(void);

static int mouseHasSelection(void) {
	if (E.column_select_active) {
		struct editorColumnSelectionRect rect;
		return editorColumnSelectionGetRect(&rect);
	}
	struct editorSelectionRange range;
	return editorGetSelectionRange(&range);
}

static void mouseAppendEditorContextItem(struct editorPopupItem *items, int *count_io,
                                         const char *label, enum editorAction action) {
	int idx = *count_io;
	if (idx >= MOUSE_EDITOR_CONTEXT_MAX_ITEMS) {
		return;
	}
	items[idx].label = (char *)label;
	items[idx].detail = NULL;
	g_mouse_editor_context_actions[idx] = action;
	*count_io = idx + 1;
}

int editorOpenEditorContextMenuAt(int screen_row, int screen_col, int has_context_offset,
                                  size_t context_offset) {
	struct editorPopupItem items[MOUSE_EDITOR_CONTEXT_MAX_ITEMS];
	int count = 0;
	int has_selection = mouseHasSelection();
	int writable = !editorActiveTabIsReadOnly();
	size_t clip_len = 0;
	(void)editorClipboardGet(&clip_len);

	if (editorLspFileSupportsDefinition(E.filename, E.syntax_language)) {
		mouseAppendEditorContextItem(items, &count, "Go to Definition",
		                             EDITOR_ACTION_GOTO_DEFINITION);
	}
	if (has_selection) {
		mouseAppendEditorContextItem(items, &count, "Copy", EDITOR_ACTION_COPY_SELECTION);
		if (writable) {
			mouseAppendEditorContextItem(items, &count, "Cut",
			                             EDITOR_ACTION_CUT_SELECTION);
		}
	}
	if (clip_len > 0 && writable) {
		mouseAppendEditorContextItem(items, &count, "Paste", EDITOR_ACTION_PASTE);
	}
	mouseAppendEditorContextItem(items, &count, "Split Vertically",
	                             EDITOR_ACTION_SPLIT_VERTICAL);
	mouseAppendEditorContextItem(items, &count, "Split Horizontally",
	                             EDITOR_ACTION_SPLIT_HORIZONTAL);
	mouseAppendEditorContextItem(items, &count, "Open Terminal", EDITOR_ACTION_TERMINAL_OPEN);
	if (E.filename != NULL && E.filename[0] != '\0') {
		mouseAppendEditorContextItem(items, &count, "Toggle Breakpoint",
		                             EDITOR_ACTION_DAP_TOGGLE_BREAKPOINT);
	}

	g_mouse_editor_context_action_count = count;
	g_mouse_editor_context_has_offset = has_context_offset;
	g_mouse_editor_context_offset = context_offset;
	if (!editorPopupOpenMenuKind(EDITOR_POPUP_KIND_EDITOR_CONTEXT_MENU, items, count,
	                             screen_row, screen_col)) {
		g_mouse_editor_context_action_count = 0;
		g_mouse_editor_context_has_offset = 0;
		g_mouse_editor_context_offset = 0;
		return 0;
	}
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	return 1;
}

static int mouseEditorContextActionUsesOffset(enum editorAction action) {
	return action == EDITOR_ACTION_GOTO_DEFINITION || action == EDITOR_ACTION_PASTE ||
	       action == EDITOR_ACTION_DAP_TOGGLE_BREAKPOINT;
}

int editorEditorContextMenuActivate(editorProcessMappedActionFn process_mapped_action,
                                    int *effects_out) {
	int idx = editorPopupSelectedIndex();
	int count = g_mouse_editor_context_action_count;
	if (idx < 0 || idx >= count) {
		editorPopupClose();
		g_mouse_editor_context_action_count = 0;
		return 0;
	}
	enum editorAction action = g_mouse_editor_context_actions[idx];
	int has_context_offset = g_mouse_editor_context_has_offset;
	size_t context_offset = g_mouse_editor_context_offset;
	editorPopupClose();
	g_mouse_editor_context_action_count = 0;
	g_mouse_editor_context_has_offset = 0;
	g_mouse_editor_context_offset = 0;
	if (process_mapped_action == NULL) {
		return 0;
	}
	if (has_context_offset && mouseEditorContextActionUsesOffset(action)) {
		(void)mouseSetCursorFromOffset(context_offset);
		mouseClearSelectionMode();
	}
	return process_mapped_action(action, effects_out);
}

static void mouseAppendTabContextItem(struct editorPopupItem *items, int *count_io,
                                      const char *label, enum editorAction action) {
	int idx = *count_io;
	if (idx >= MOUSE_TAB_CONTEXT_MAX_ITEMS) {
		return;
	}
	items[idx].label = (char *)label;
	items[idx].detail = NULL;
	g_mouse_tab_context_actions[idx] = action;
	*count_io = idx + 1;
}

static void mouseClearTabContextState(void) {
	g_mouse_tab_context_action_count = 0;
	g_mouse_tab_context_leaf = NULL;
	g_mouse_tab_context_tab_idx = -1;
}

int editorOpenTabContextMenuAt(int screen_row, int screen_col, struct editorPaneNode *leaf,
                               int tab_idx) {
	if (leaf == NULL || leaf->is_split || leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return 0;
	}

	struct editorPopupItem items[MOUSE_TAB_CONTEXT_MAX_ITEMS];
	int count = 0;
	if (tab_idx >= 0 && editorPaneViewHasTab(&leaf->as.leaf.view, tab_idx)) {
		mouseAppendTabContextItem(items, &count, "Close Tab", EDITOR_ACTION_CLOSE_TAB);
	}
	mouseAppendTabContextItem(items, &count, "New Tab", EDITOR_ACTION_NEW_TAB);

	g_mouse_tab_context_action_count = count;
	g_mouse_tab_context_leaf = leaf;
	g_mouse_tab_context_tab_idx = tab_idx;
	if (!editorPopupOpenMenuKind(EDITOR_POPUP_KIND_TAB_CONTEXT_MENU, items, count, screen_row,
	                             screen_col)) {
		mouseClearTabContextState();
		return 0;
	}
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	return 1;
}

int editorTabContextMenuActivate(editorProcessMappedActionFn process_mapped_action,
                                 int *effects_out) {
	int idx = editorPopupSelectedIndex();
	int count = g_mouse_tab_context_action_count;
	if (idx < 0 || idx >= count) {
		editorPopupClose();
		mouseClearTabContextState();
		return 0;
	}

	enum editorAction action = g_mouse_tab_context_actions[idx];
	struct editorPaneNode *leaf = g_mouse_tab_context_leaf;
	int tab_idx = g_mouse_tab_context_tab_idx;
	editorPopupClose();
	mouseClearTabContextState();
	if (process_mapped_action == NULL || leaf == NULL || leaf->is_split ||
	    E.layout_root == NULL || !editorPaneNodeContainsLeaf(E.layout_root, leaf)) {
		return 0;
	}
	if (!editorLayoutSetFocusedLeaf(leaf)) {
		return 0;
	}
	if (action == EDITOR_ACTION_CLOSE_TAB) {
		if (!editorPaneViewHasTab(&leaf->as.leaf.view, tab_idx) ||
		    !editorTabSwitchToIndex(tab_idx)) {
			return 0;
		}
	}
	return process_mapped_action(action, effects_out);
}

static int mouseHandleLeftPressOnMenu(const struct editorMouseEvent *event,
                                      editorProcessMappedActionFn process_mapped_action,
                                      int *effects_out) {
	if (!editorPopupIsVisible() || !editorPopupKindIsMenu(E.popup.kind)) {
		return 0;
	}
	int item = -1;
	if (editorPopupMenuHitTest(event->y, event->x, &item)) {
		E.popup.selected_index = item;
		if (E.popup.kind == EDITOR_POPUP_KIND_DRAWER_MENU) {
			editorDrawerContextMenuActivate();
		} else if (E.popup.kind == EDITOR_POPUP_KIND_EDITOR_CONTEXT_MENU) {
			(void)editorEditorContextMenuActivate(process_mapped_action, effects_out);
		} else if (E.popup.kind == EDITOR_POPUP_KIND_TAB_CONTEXT_MENU) {
			(void)editorTabContextMenuActivate(process_mapped_action, effects_out);
		} else if (E.popup.kind == EDITOR_POPUP_KIND_LSP_LOCATION_MENU) {
			(void)editorLspLocationMenuActivate();
		} else {
			editorPopupClose();
		}
	} else {
		editorPopupClose();
	}
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return 1;
}

static struct editorPaneNode *mouseEditorLeafAt(const struct editorMouseEvent *event) {
	if (event == NULL || E.layout_root == NULL) {
		return NULL;
	}
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return NULL;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeBorderedInto(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                     &layout)) {
		editorLeafLayoutFree(&layout);
		return NULL;
	}
	struct editorPaneNode *leaf = editorLayoutLeafAt(&layout, event->x - 1, event->y - 1);
	editorLeafLayoutFree(&layout);
	if (leaf == NULL || leaf->is_split || leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return NULL;
	}
	return leaf;
}

static int mouseHandleRightPress(const struct editorMouseEvent *event) {
	int popup_was_open = editorPopupIsVisible() && editorPopupKindIsMenu(E.popup.kind);
	if (popup_was_open) {
		editorPopupClose();
	}
	if (editorMouseIsOverDrawer(event)) {
		if (editorDrawerOpenContextMenuAt(event, E.window_rows)) {
			return 1;
		}
		return popup_was_open;
	}

	struct editorPaneNode *tab_leaf = NULL;
	int tab_idx = -1;
	if (mouseResolvePaneTabStripTarget(event, &tab_leaf, &tab_idx)) {
		struct editorPaneNode *prev_focus = E.focused_leaf;
		if (!editorLayoutSetFocusedLeaf(tab_leaf)) {
			return popup_was_open;
		}
		if (E.focused_leaf != prev_focus) {
			editorPaneAnnounceFocus();
		}
		if (editorOpenTabContextMenuAt(event->y, event->x, tab_leaf, tab_idx)) {
			return 1;
		}
		return popup_was_open;
	}

	struct editorPaneNode *leaf = mouseEditorLeafAt(event);
	if (leaf != NULL) {
		struct editorPaneNode *prev_focus = E.focused_leaf;
		if (!editorLayoutSetFocusedLeaf(leaf)) {
			return popup_was_open;
		}
		if (E.focused_leaf != prev_focus) {
			editorPaneAnnounceFocus();
		}
		size_t offset = 0;
		if (editorResolveMouseToBufferOffset(event, 0, &offset) &&
		    editorOpenEditorContextMenuAt(event->y, event->x, 1, offset)) {
			return 1;
		}
	}
	return popup_was_open;
}

/* A left-press on a status-bar debug-control button dispatches its DAP action.
 * The status bar is the row just below the text area (E.window_rows + 2,
 * 1-based). Returns 1 if the press hit a button. */
static int mouseHandleLeftPressOnStatusBar(const struct editorMouseEvent *event,
                                           editorProcessMappedActionFn process_mapped_action) {
	if (event->y != E.window_rows + 2) {
		return 0;
	}
	int action = 0;
	if (!editorStatusBarDebugButtonAt(event->x - 1, &action)) {
		return 0;
	}
	int effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	(void)process_mapped_action((enum editorAction)action, &effects);
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return 1;
}

static int mouseHandleLeftPress(const struct editorMouseEvent *event,
                                int drawer_double_click_threshold_ms,
                                int text_multi_click_threshold_ms,
                                editorProcessMappedActionFn process_mapped_action,
                                editorMouseJumpToPathFn jump_to_path,
                                editorMouseActionFn goto_definition) {
	int menu_effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	if (mouseHandleLeftPressOnMenu(event, process_mapped_action, &menu_effects)) {
		if ((menu_effects & EDITOR_MOUSE_DISPATCH_EFFECT_VIEWPORT_SCROLL) != 0) {
			return EDITOR_MOUSE_DISPATCH_EFFECT_VIEWPORT_SCROLL;
		}
		return EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT;
	}
	if (mouseHandleLeftPressOnStatusBar(event, process_mapped_action)) {
		return EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT;
	}
	long long now_ms = mouseMonotonicMillis();
	mouseClearTabDrag();
	mouseClearDrawerDrag();
	int drawer_effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	if (editorHandleMouseDrawerLeftPress(event, now_ms, drawer_double_click_threshold_ms,
	                                     process_mapped_action, jump_to_path,
	                                     &drawer_effects)) {
		if ((drawer_effects & EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT) != 0) {
			return EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT;
		}
		return EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	}

	editorResetDrawerClickTracking();
	int text_effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	if (editorHandleMouseTextLeftPress(event, now_ms, text_multi_click_threshold_ms,
	                                   goto_definition, &text_effects)) {
		return text_effects ? EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT
		                    : EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	}
	return EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
}

int editorHandleMouseEventDispatch(int drawer_double_click_threshold_ms,
                                   int text_multi_click_threshold_ms,
                                   editorProcessMappedActionFn process_mapped_action,
                                   editorMouseJumpToPathFn jump_to_path,
                                   editorMouseActionFn goto_definition, int *effects_out) {
	int effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
	struct editorMouseEvent event;
	// terminal.c queues one decoded event per MOUSE_EVENT keycode.
	if (!editorConsumeMouseEvent(&event)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	if (event.kind != EDITOR_MOUSE_EVENT_MOTION && editorClearHoverLinkState()) {
		// Fall through and dispatch; render will pick up the cleared hover state.
	}
	if ((event.kind == EDITOR_MOUSE_EVENT_LEFT_PRESS ||
	     event.kind == EDITOR_MOUSE_EVENT_RIGHT_PRESS) &&
	    editorPopupIsVisible() && !editorPopupKindIsMenu(E.popup.kind)) {
		editorPopupClose();
	}

	if (editorHandleMouseEventInTerminalPane(&event)) {
		if (effects_out != NULL) {
			*effects_out = effects;
		}
		return 1;
	}

	switch (event.kind) {
		case EDITOR_MOUSE_EVENT_LEFT_PRESS:
			effects = mouseHandleLeftPress(&event, drawer_double_click_threshold_ms,
			                               text_multi_click_threshold_ms,
			                               process_mapped_action, jump_to_path,
			                               goto_definition);
			break;
		case EDITOR_MOUSE_EVENT_RIGHT_PRESS:
			effects = mouseHandleRightPress(&event)
			                  ? EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT
			                  : EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
			break;
		case EDITOR_MOUSE_EVENT_LEFT_DRAG:
			effects = editorHandleMouseLeftDrag(&event)
			                  ? EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT
			                  : EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
			break;
		case EDITOR_MOUSE_EVENT_LEFT_RELEASE:
			effects = editorHandleMouseLeftRelease(&event)
			                  ? EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT
			                  : EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
			break;
		case EDITOR_MOUSE_EVENT_MOTION:
			effects = editorHandleMouseMotion(&event)
			                  ? EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT
			                  : EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_UP:
		case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
		case EDITOR_MOUSE_EVENT_WHEEL_LEFT:
		case EDITOR_MOUSE_EVENT_WHEEL_RIGHT:
			effects = editorHandleMouseWheel(&event)
			                  ? EDITOR_MOUSE_DISPATCH_EFFECT_VIEWPORT_SCROLL
			                  : EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
			break;
		default:
			effects = EDITOR_MOUSE_DISPATCH_EFFECT_NONE;
			break;
	}

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
	return drawer_row >= 0 && drawer_row < drawer_view_rows && mouse_col >= 0 &&
	       mouse_col < drawer_cols;
}

int editorHandleMouseWheel(const struct editorMouseEvent *event) {
	int over_drawer = editorMouseIsOverDrawer(event);
	/* Over a Debug Console pane the wheel scrolls the transcript, not the (empty)
	 * editor buffer. Terminal panes are handled earlier in the dispatch. */
	struct editorDapConsolePane *console =
	        over_drawer ? NULL : editorDapConsoleForPane(mouseEditorLeafAt(event));

	switch (event->kind) {
		case EDITOR_MOUSE_EVENT_WHEEL_UP:
			if (over_drawer) {
				(void)editorDrawerScrollBy(-MOUSE_WHEEL_SCROLL_LINES,
				                           E.window_rows);
				break;
			}
			if (console != NULL) {
				editorDapConsoleScroll(console, MOUSE_WHEEL_SCROLL_LINES);
				break;
			}
			editorViewportScrollByRows(-MOUSE_WHEEL_SCROLL_LINES);
			break;
		case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
			if (over_drawer) {
				(void)editorDrawerScrollBy(MOUSE_WHEEL_SCROLL_LINES, E.window_rows);
				break;
			}
			if (console != NULL) {
				editorDapConsoleScroll(console, -MOUSE_WHEEL_SCROLL_LINES);
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

/* Terminal-absolute (0-based) geometry of the focused pane's text area. */
struct mousePaneTextMetrics {
	int pane_y;
	int pane_rows;
	int text_start_col;
	int text_cols;
};

static void mouseFocusedPaneTextMetrics(struct mousePaneTextMetrics *out) {
	struct editorRect focused_rect = {0};
	int has_focused_rect = editorLayoutFocusedLeafRect(&focused_rect);
	int pane_w =
	        has_focused_rect ? focused_rect.w : editorDrawerTextViewportCols(E.window_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_w) {
		gutter_cols = pane_w;
	}
	int text_cols = pane_w - gutter_cols;
	int text_start_col = has_focused_rect
	                             ? focused_rect.x + gutter_cols
	                             : editorDrawerTextStartColForCols(E.window_cols) + gutter_cols;
	if (text_cols < 1) {
		text_cols = 1;
	}
	if (text_cols >= 3) {
		text_start_col++;
		text_cols -= 2;
	}
	out->pane_y = has_focused_rect ? focused_rect.y : 1;
	out->pane_rows = has_focused_rect ? focused_rect.h : E.window_rows;
	out->text_start_col = text_start_col;
	out->text_cols = text_cols;
}

int editorResolveMouseToBufferOffset(const struct editorMouseEvent *event, int clamp_to_viewport,
                                     size_t *offset_out) {
	if (event == NULL || offset_out == NULL || E.numrows == 0) {
		return 0;
	}

	/* SGR mouse coordinates are terminal-absolute and 1-based. */
	int raw_col = event->x - 1;
	int raw_row = event->y - 1;
	struct mousePaneTextMetrics metrics;
	mouseFocusedPaneTextMetrics(&metrics);
	int pane_y = metrics.pane_y;
	int text_cols = metrics.text_cols;
	int text_start_col = metrics.text_start_col;
	int mouse_row = raw_row - pane_y;
	int mouse_col = raw_col - text_start_col;
	int pane_rows = metrics.pane_rows;
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
		if (!editorViewportTextScreenRowToBufferPosition(
		            mouse_row, &row_idx, &segment_coloff, &segment_indent_cols)) {
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
	int cx = 0;
	struct editorLineView line = {0};
	if (editorDocumentLineView(E.document, row_idx, &line)) {
		cx = editorBytesRxToCx(line.data, line.size, target_rx);
		editorLineViewRelease(&line);
	}
	return editorBufferPosToOffset(row_idx, cx, offset_out);
}

static int mouseSetCursorFromOffset(size_t offset) {
	int cy = 0;
	int cx = 0;
	size_t normalized_offset = 0;

	if (!editorBufferOffsetToPos(offset, &cy, &cx)) {
		return 0;
	}
	if (cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
			cx = editorBytesClampCxToClusterBoundary(line.data, line.size, cx);
			editorLineViewRelease(&line);
		}
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
	return mouseSetCursorFromOffset(offset);
}

/* Unlike editorResolveMouseToBufferOffset, this accepts the "~" filler rows
 * below the last line, so callers can tell a below-content press apart from a
 * press outside the pane. */
static int mouseEventInFocusedPaneTextArea(const struct editorMouseEvent *event) {
	if (event == NULL) {
		return 0;
	}
	struct mousePaneTextMetrics metrics;
	mouseFocusedPaneTextMetrics(&metrics);
	int mouse_row = (event->y - 1) - metrics.pane_y;
	int mouse_col = (event->x - 1) - metrics.text_start_col;
	return mouse_row >= 0 && mouse_row < metrics.pane_rows && mouse_col >= 0 &&
	       mouse_col < metrics.text_cols;
}

static int mouseSetCursorToBufferEnd(void) {
	if (E.numrows <= 0) {
		return 0;
	}
	int last_row = E.numrows - 1;
	int last_len = (int)editorDocumentLineLength(E.document, last_row);
	size_t end_offset = 0;
	if (!editorBufferPosToOffset(last_row, last_len, &end_offset)) {
		return 0;
	}
	E.cy = last_row;
	E.cx = last_len;
	E.cursor_offset = end_offset;
	return 1;
}

static int mouseIsWordByte(unsigned char b) {
	return isalnum(b) || b == '_' || b >= 0x80;
}

static void mouseClearSelectionMode(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	editorColumnSelectionClear();
}

static void mouseSelectWordAtCursor(void) {
	if (E.cy < 0 || E.cy >= E.numrows) {
		return;
	}
	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return;
	}
	int cx = editorBytesClampCxToCharBoundary(line.data, line.size, E.cx);
	if (cx >= line.size) {
		cx = editorBytesPrevCharIdx(line.data, line.size, line.size);
		if (cx < 0) {
			editorLineViewRelease(&line);
			return;
		}
	}
	if (!mouseIsWordByte((unsigned char)line.data[cx])) {
		editorLineViewRelease(&line);
		return;
	}

	int start = cx;
	while (start > 0) {
		int prev = editorBytesPrevCharIdx(line.data, line.size, start);
		if (prev >= start || !mouseIsWordByte((unsigned char)line.data[prev])) {
			break;
		}
		start = prev;
	}
	int end = editorBytesNextCharIdx(line.data, line.size, cx);
	while (end < line.size) {
		if (!mouseIsWordByte((unsigned char)line.data[end])) {
			break;
		}
		int next = editorBytesNextCharIdx(line.data, line.size, end);
		if (next <= end) {
			break;
		}
		end = next;
	}
	editorLineViewRelease(&line);

	size_t anchor_offset = 0;
	if (!editorBufferPosToOffset(E.cy, start, &anchor_offset)) {
		return;
	}
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = anchor_offset;
	E.cx = end;
	size_t cursor_offset = 0;
	if (editorBufferPosToOffset(E.cy, end, &cursor_offset)) {
		E.cursor_offset = cursor_offset;
	}
}

static void mouseSelectLineAtCursor(void) {
	if (E.cy < 0 || E.cy >= E.numrows) {
		return;
	}
	size_t anchor_offset = 0;
	if (!editorBufferPosToOffset(E.cy, 0, &anchor_offset)) {
		return;
	}
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = anchor_offset;

	int end_cy = E.cy;
	int end_cx = (int)editorDocumentLineLength(E.document, E.cy);
	if (E.cy + 1 < E.numrows) {
		end_cy = E.cy + 1;
		end_cx = 0;
	}
	E.cy = end_cy;
	E.cx = end_cx;
	size_t cursor_offset = 0;
	if (editorBufferPosToOffset(end_cy, end_cx, &cursor_offset)) {
		E.cursor_offset = cursor_offset;
	}
}

static int mouseComputeWordRangeAt(int cy, int cx, int *start_out, int *end_out) {
	if (cy < 0 || cy >= E.numrows) {
		return 0;
	}
	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
	if (cx >= line.size || !mouseIsWordByte((unsigned char)line.data[cx])) {
		editorLineViewRelease(&line);
		return 0;
	}
	int start = cx;
	while (start > 0) {
		int prev = editorBytesPrevCharIdx(line.data, line.size, start);
		if (prev >= start || !mouseIsWordByte((unsigned char)line.data[prev])) {
			break;
		}
		start = prev;
	}
	int end = editorBytesNextCharIdx(line.data, line.size, cx);
	while (end < line.size) {
		if (!mouseIsWordByte((unsigned char)line.data[end])) {
			break;
		}
		int next = editorBytesNextCharIdx(line.data, line.size, end);
		if (next <= end) {
			break;
		}
		end = next;
	}
	editorLineViewRelease(&line);
	*start_out = start;
	*end_out = end;
	return 1;
}

int editorHandleMouseMotion(const struct editorMouseEvent *event) {
	int new_active = 0;
	int new_row = -1;
	int new_start = 0;
	int new_end = 0;
	int blame_row = -1;
	int blame_anchor_col = 0;

	if (event->modifiers == EDITOR_MOUSE_MOD_NONE &&
	    editorGitBlameIndicatorHitTest(event->y - 1, event->x - 1, &blame_row,
	                                   &blame_anchor_col)) {
		int changed = editorClearHoverLinkState();
		if (editorPopupIsVisible() && E.popup.kind == EDITOR_POPUP_KIND_GIT_BLAME &&
		    E.popup.anchor_row == blame_row && E.popup.anchor_col == blame_anchor_col) {
			return changed;
		}
		return editorDispatchOpenGitBlameDetailsAt(blame_row, blame_anchor_col, 0) ||
		       changed;
	}

	if ((event->modifiers & EDITOR_MOUSE_MOD_CTRL) != 0 &&
	    E.primary_focus == EDITOR_PRIMARY_FOCUS_TEXT &&
	    editorLspFileSupportsDefinition(E.filename, E.syntax_language) &&
	    editorLspFileEnabled(E.filename, E.syntax_language)) {
		size_t offset = 0;
		if (editorResolveMouseToBufferOffset(event, 0, &offset)) {
			int row = 0;
			int cx = 0;
			if (editorBufferOffsetToPos(offset, &row, &cx) &&
			    mouseComputeWordRangeAt(row, cx, &new_start, &new_end)) {
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

/* Returns 1 if a drawer drag is in progress (handled). 0 to fall through. */
static int mouseDragDrawerArmed(const struct editorMouseEvent *event) {
	if (!E.drawer_drag_armed) {
		return 0;
	}
	int dx = event->x - 1 - E.drawer_drag_start_x;
	int dy = event->y - 1 - E.drawer_drag_start_y;
	if (!E.drawer_drag_active &&
	    (dx > MOUSE_DRAWER_DRAG_THRESHOLD_COLS || dx < -MOUSE_DRAWER_DRAG_THRESHOLD_COLS ||
	     dy > MOUSE_DRAWER_DRAG_THRESHOLD_ROWS || dy < -MOUSE_DRAWER_DRAG_THRESHOLD_ROWS)) {
		E.drawer_drag_active = 1;
	}
	return E.drawer_drag_active ? 1 : 0;
}

/* Returns 1 if a tab drag was handled (or armed but not yet active), -1 if the
 * tab drag was invalidated and cleared (caller should return 0). 0 to fall through. */
static int mouseDragTabArmed(const struct editorMouseEvent *event) {
	if (!E.tab_drag_armed) {
		return 0;
	}
	if (E.tab_drag_source_leaf == NULL || E.tab_drag_source_leaf->is_split ||
	    E.layout_root == NULL ||
	    !editorPaneNodeContainsLeaf(E.layout_root, E.tab_drag_source_leaf)) {
		mouseClearTabDrag();
		return -1;
	}
	int dx = event->x - 1 - E.tab_drag_start_x;
	int dy = event->y - 1 - E.tab_drag_start_y;
	if (!E.tab_drag_active && (dx > MOUSE_TAB_DRAG_THRESHOLD_COLS ||
	                           dx < -MOUSE_TAB_DRAG_THRESHOLD_COLS || dy != 0)) {
		E.tab_drag_active = 1;
	}
	return E.tab_drag_active ? 1 : -1;
}

/* Returns 1 on successful resize, 0 if the resize could not be applied (no change
 * or out-of-viewport). Caller treats both as "handled, nothing else to do". */
static int mouseDragSplitResize(const struct editorMouseEvent *event) {
	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	struct editorRect node_rect = {0};
	if (!editorLayoutSplitNodeRect(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                               E.split_resize_node, &node_rect)) {
		return 0;
	}
	double new_ratio;
	if (E.split_resize_node->as.split.orientation == EDITOR_SPLIT_VERTICAL) {
		int available = node_rect.w - ROTIDE_PANE_BORDER_SIZE;
		if (available <= 0) {
			return 0;
		}
		new_ratio = (double)(event->x - 1 - node_rect.x) / (double)available;
	} else {
		int available = node_rect.h - ROTIDE_PANE_BORDER_SIZE;
		if (available <= 0) {
			return 0;
		}
		new_ratio = (double)(event->y - 1 - node_rect.y) / (double)available;
	}
	double min_ratio = ROTIDE_PANE_MIN_RATIO;
	double max_ratio = 1.0 - ROTIDE_PANE_MIN_RATIO;
	if (new_ratio < min_ratio) {
		new_ratio = min_ratio;
	}
	if (new_ratio > max_ratio) {
		new_ratio = max_ratio;
	}
	if (new_ratio == E.split_resize_node->as.split.ratio) {
		return 0;
	}
	E.split_resize_node->as.split.ratio = new_ratio;
	/* Keep child vterm/pty sizes in sync with the new split ratio so terminal panes
	 * don't render with stale dimensions (causes border artifacts and prevents
	 * shrinking back down). */
	editorTerminalPaneResizeAllToLayout(E.layout_root);
	return 1;
}

static int mouseDragColumnSelectTargetRx(const struct editorMouseEvent *event) {
	int mouse_col = event->x - 1;
	struct editorRect focused_rect = {0};
	int has_focused_rect = editorLayoutFocusedLeafRect(&focused_rect);
	int pane_w =
	        has_focused_rect ? focused_rect.w : editorDrawerTextViewportCols(E.window_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_w) {
		gutter_cols = pane_w;
	}
	int text_cols = pane_w - gutter_cols;
	int text_start = has_focused_rect
	                         ? focused_rect.x + gutter_cols
	                         : editorDrawerTextStartColForCols(E.window_cols) + gutter_cols;
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
	return target_rx;
}

static void mouseDragColumnSelectAlignCursorToRx(int target_rx) {
	if (E.cy >= E.numrows) {
		return;
	}
	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return;
	}
	int new_cx = editorBytesRxToCx(line.data, line.size, target_rx);
	if (new_cx > line.size) {
		new_cx = line.size;
	}
	editorLineViewRelease(&line);
	size_t off = 0;
	if (editorBufferPosToOffset(E.cy, new_cx, &off)) {
		(void)editorSyncCursorFromOffsetByteBoundary(off);
	}
}

static int mouseDragColumnSelect(const struct editorMouseEvent *event) {
	int target_rx = mouseDragColumnSelectTargetRx(event);
	if (!editorMoveCursorToMouse(event, 1)) {
		return 0;
	}
	E.column_select_cursor_rx = target_rx;
	mouseDragColumnSelectAlignCursorToRx(target_rx);
	E.mouse_drag_started = 1;
	return 1;
}

int editorHandleMouseLeftDrag(const struct editorMouseEvent *event) {
	int drawer_handled = mouseDragDrawerArmed(event);
	if (drawer_handled) {
		return 1;
	}
	int tab_handled = mouseDragTabArmed(event);
	if (tab_handled != 0) {
		return tab_handled > 0 ? 1 : 0;
	}
	if (E.split_resize_active && E.split_resize_node != NULL && E.split_resize_node->is_split) {
		return mouseDragSplitResize(event);
	}
	if (E.drawer_resize_active) {
		int mouse_col = event->x - 1;
		(void)editorDrawerSetWidthForCols(mouse_col, E.window_cols);
		return 0;
	}
	if (!E.mouse_left_button_down) {
		return 0;
	}
	if (E.column_select_active) {
		return mouseDragColumnSelect(event);
	}
	if (!editorMoveCursorToMouse(event, 1)) {
		return 0;
	}
	if (!E.mouse_drag_started) {
		/* A new drag always starts a fresh selection anchored at the initial press point.
		 */
		editorColumnSelectionClear();
		E.selection_mode_active = 1;
		E.selection_anchor_offset = E.mouse_drag_anchor_offset;
		E.mouse_drag_started = 1;
	}
	return 1;
}

int editorHandleMouseLeftRelease(const struct editorMouseEvent *event) {
	int changed = 0;
	if (E.drawer_drag_armed) {
		if (E.drawer_drag_active) {
			changed = mouseFinishDrawerDrag(event);
		}
		mouseClearDrawerDrag();
		E.drawer_resize_active = 0;
		E.split_resize_active = 0;
		E.split_resize_node = NULL;
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return changed;
	}
	if (E.tab_drag_armed) {
		if (E.tab_drag_active) {
			changed = mouseFinishTabDrag(event);
		}
		mouseClearTabDrag();
		E.drawer_resize_active = 0;
		E.split_resize_active = 0;
		E.split_resize_node = NULL;
		E.mouse_left_button_down = 0;
		E.mouse_drag_started = 0;
		return changed;
	}
	E.drawer_resize_active = 0;
	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	E.mouse_left_button_down = 0;
	E.mouse_drag_started = 0;
	return 0;
}

int editorHandleMouseEventInTerminalPane(const struct editorMouseEvent *event) {
	if (E.layout_root == NULL) {
		return 0;
	}
	/* An open menu overlays the panes; let its click handler run instead of
	 * forwarding the click to a terminal it happens to sit over (otherwise a
	 * tab context menu on a terminal pane could never be activated). */
	if (editorPopupIsVisible() && editorPopupKindIsMenu(E.popup.kind)) {
		return 0;
	}
	/* Defer to the normal mouse handlers while a split-border drag is in
	 * progress; otherwise, when the user drags toward a terminal pane the
	 * drag event lands inside the terminal's leaf rect and gets hijacked
	 * as a terminal selection, freezing the border and preventing shrink. */
	if (E.split_resize_active && (event->kind == EDITOR_MOUSE_EVENT_LEFT_DRAG ||
	                              event->kind == EDITOR_MOUSE_EVENT_LEFT_RELEASE)) {
		return 0;
	}
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeBorderedInto(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                     &layout)) {
		editorLeafLayoutFree(&layout);
		return 0;
	}
	int sx = event->x - 1;
	int sy = event->y - 1;
	struct editorPaneNode *leaf = editorLayoutLeafAt(&layout, sx, sy);
	editorLeafLayoutFree(&layout);
	struct editorTerminalPane *terminal = editorTerminalPaneForPane(leaf);
	if (terminal == NULL) {
		return 0;
	}
	struct editorRect leaf_rect = {0};
	if (!editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE, leaf,
	                                  &leaf_rect)) {
		return terminal->mouse_tracking > 0 ? 1 : 0;
	}
	int row = sy - leaf_rect.y;
	int col = sx - leaf_rect.x;
	/* When the child isn't tracking the mouse, the wheel and left-drag are
	 * ours: wheel scrolls scrollback, left-drag selects, release+copy lands
	 * in the clipboard. */
	if (terminal->mouse_tracking <= 0) {
		if (event->kind == EDITOR_MOUSE_EVENT_LEFT_PRESS && leaf != E.focused_leaf) {
			(void)editorLayoutSetFocusedLeaf(leaf);
			editorPaneAnnounceFocus();
		}
		int log_row = row - terminal->scroll_offset;
		switch (event->kind) {
			case EDITOR_MOUSE_EVENT_WHEEL_UP:
				(void)editorTerminalPaneScrollBy(terminal,
				                                 MOUSE_WHEEL_SCROLL_LINES);
				return 1;
			case EDITOR_MOUSE_EVENT_WHEEL_DOWN:
				(void)editorTerminalPaneScrollBy(terminal,
				                                 -MOUSE_WHEEL_SCROLL_LINES);
				return 1;
			case EDITOR_MOUSE_EVENT_LEFT_PRESS:
				editorTerminalPaneSelectionBegin(terminal, log_row, col);
				return 1;
			case EDITOR_MOUSE_EVENT_LEFT_DRAG:
				editorTerminalPaneSelectionUpdate(terminal, log_row, col);
				return 1;
			case EDITOR_MOUSE_EVENT_LEFT_RELEASE:
				editorTerminalPaneSelectionUpdate(terminal, log_row, col);
				if (terminal->sel_active &&
				    (terminal->sel_anchor_row != terminal->sel_cursor_row ||
				     terminal->sel_anchor_col != terminal->sel_cursor_col)) {
					(void)editorTerminalPaneCopySelection(terminal);
				}
				return 1;
			default:
				return 0;
		}
	}
	/* Click-to-focus for terminal panes. */
	if (event->kind == EDITOR_MOUSE_EVENT_LEFT_PRESS && leaf != E.focused_leaf) {
		(void)editorLayoutSetFocusedLeaf(leaf);
		editorPaneAnnounceFocus();
	}
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
			(void)editorTerminalPaneSendMouseMove(terminal, row, col, event->modifiers);
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
