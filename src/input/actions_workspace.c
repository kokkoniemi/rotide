#include "input/actions_workspace.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

void editorSetDrawerCollapseStatus(int collapsed) {
	editorSetStatusMsg(collapsed ? "Drawer collapsed" : "Drawer expanded");
}

void editorExpandDrawerForFocus(void) {
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
}

void editorToggleDrawerFocus(void) {
	if (E.pane_focus == EDITOR_PANE_DRAWER) {
		E.pane_focus = EDITOR_PANE_TEXT;
		return;
	}
	editorExpandDrawerForFocus();
}

void editorOpenFileSearchDrawer(void) {
	editorHistoryBreakGroup();
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(0);
	}
	if (!editorFileSearchEnter()) {
		return;
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
	(void)editorFileSearchPreviewSelection();
}

void editorOpenProjectSearchDrawer(void) {
	editorHistoryBreakGroup();
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	if (editorFileSearchIsActive()) {
		editorFileSearchExit(0);
	}
	if (!editorProjectSearchEnter()) {
		return;
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
}

int editorHandleDrawerSearchMappedAction(enum editorAction action, int *cursor_or_edit_out,
		void (*project_replace_from_search)(void)) {
	int cursor_or_edit = 0;
	if (!editorDrawerIsCollapsed() && editorFileSearchIsActive()) {
		switch (action) {
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorOpenProjectSearchDrawer();
			break;
		case EDITOR_ACTION_FIND_FILE:
			editorOpenFileSearchDrawer();
			break;
		case EDITOR_ACTION_MOVE_UP:
			if (editorFileSearchMoveSelectionBy(-1, E.window_rows)) {
				(void)editorFileSearchPreviewSelection();
			}
			break;
		case EDITOR_ACTION_MOVE_DOWN:
			if (editorFileSearchMoveSelectionBy(1, E.window_rows)) {
				(void)editorFileSearchPreviewSelection();
			}
			break;
		case EDITOR_ACTION_NEWLINE:
			if (editorFileSearchOpenSelectedFileInTab()) {
				E.pane_focus = EDITOR_PANE_DRAWER;
				cursor_or_edit = 1;
			}
			break;
		case EDITOR_ACTION_ESCAPE:
			editorFileSearchExit(1);
			E.pane_focus = EDITOR_PANE_TEXT;
			break;
		case EDITOR_ACTION_BACKSPACE:
		case EDITOR_ACTION_DELETE_CHAR:
			if (editorFileSearchBackspace()) {
				(void)editorFileSearchPreviewSelection();
			}
			break;
		case EDITOR_ACTION_TOGGLE_DRAWER:
			if (editorDrawerSetCollapsed(1)) {
				editorSetDrawerCollapseStatus(1);
			}
			break;
		case EDITOR_ACTION_MAIN_MENU:
			(void)editorDrawerMainMenuToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU ?
					"Main menu opened" : "Project drawer shown");
			break;
		case EDITOR_ACTION_GIT_DRAWER:
			(void)editorDrawerGitToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_GIT ?
					(E.git_repo_root != NULL ? "Git changes shown" :
					"Not in a git repository") :
					"Project drawer shown");
			break;
		case EDITOR_ACTION_LSP_DRAWER:
			(void)editorDrawerLspToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_LSP ?
					"LSP drawer shown" : "Project drawer shown");
			break;
		case EDITOR_ACTION_DAP_DRAWER:
			(void)editorDrawerDapToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_DAP ?
					"DAP drawer shown" : "Project drawer shown");
			break;
		default:
			break;
		}
		if (cursor_or_edit_out != NULL) {
			*cursor_or_edit_out = cursor_or_edit;
		}
		return 1;
	}

	if (!editorDrawerIsCollapsed() && editorProjectSearchIsActive()) {
		switch (action) {
		case EDITOR_ACTION_FIND_FILE:
			editorOpenFileSearchDrawer();
			break;
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorOpenProjectSearchDrawer();
			break;
		case EDITOR_ACTION_FIND_REPLACE:
			if (project_replace_from_search != NULL) {
				project_replace_from_search();
				cursor_or_edit = 1;
			}
			break;
		case EDITOR_ACTION_MOVE_UP:
			if (editorProjectSearchMoveSelectionBy(-1, E.window_rows)) {
				(void)editorProjectSearchPreviewSelection();
			}
			break;
		case EDITOR_ACTION_MOVE_DOWN:
			if (editorProjectSearchMoveSelectionBy(1, E.window_rows)) {
				(void)editorProjectSearchPreviewSelection();
			}
			break;
		case EDITOR_ACTION_NEWLINE:
			if (editorProjectSearchOpenSelectedFileInTab()) {
				E.pane_focus = EDITOR_PANE_DRAWER;
				cursor_or_edit = 1;
			}
			break;
		case EDITOR_ACTION_ESCAPE:
			editorProjectSearchExit(1);
			E.pane_focus = EDITOR_PANE_TEXT;
			break;
		case EDITOR_ACTION_BACKSPACE:
		case EDITOR_ACTION_DELETE_CHAR:
			if (editorProjectSearchBackspace()) {
				(void)editorProjectSearchPreviewSelection();
			}
			break;
		case EDITOR_ACTION_TOGGLE_DRAWER:
			if (editorDrawerSetCollapsed(1)) {
				editorSetDrawerCollapseStatus(1);
			}
			break;
		case EDITOR_ACTION_MAIN_MENU:
			(void)editorDrawerMainMenuToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU ?
					"Main menu opened" : "Project drawer shown");
			break;
		case EDITOR_ACTION_GIT_DRAWER:
			(void)editorDrawerGitToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_GIT ?
					(E.git_repo_root != NULL ? "Git changes shown" :
					"Not in a git repository") :
					"Project drawer shown");
			break;
		case EDITOR_ACTION_LSP_DRAWER:
			(void)editorDrawerLspToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_LSP ?
					"LSP drawer shown" : "Project drawer shown");
			break;
		case EDITOR_ACTION_DAP_DRAWER:
			(void)editorDrawerDapToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_DAP ?
					"DAP drawer shown" : "Project drawer shown");
			break;
		default:
			break;
		}
		if (cursor_or_edit_out != NULL) {
			*cursor_or_edit_out = cursor_or_edit;
		}
		return 1;
	}

	return 0;
}

static enum editorDrawerMode editorActiveDrawerHeaderMode(void) {
	if (editorFileSearchIsActive()) {
		return EDITOR_DRAWER_MODE_FILE_SEARCH;
	}
	if (editorProjectSearchIsActive()) {
		return EDITOR_DRAWER_MODE_PROJECT_SEARCH;
	}
	return E.drawer_mode;
}

int editorSwitchDrawerHeaderMode(enum editorDrawerMode mode) {
	if (editorActiveDrawerHeaderMode() == mode) {
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 0;
	}

	switch (mode) {
	case EDITOR_DRAWER_MODE_TREE:
		editorHistoryBreakGroup();
		if (editorFileSearchIsActive()) {
			editorFileSearchExit(1);
		}
		if (editorProjectSearchIsActive()) {
			editorProjectSearchExit(1);
		}
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		(void)editorDrawerSetCollapsed(0);
		E.pane_focus = EDITOR_PANE_DRAWER;
		editorSetStatusMsg("Project drawer shown");
		return 0;
	case EDITOR_DRAWER_MODE_FILE_SEARCH:
		editorOpenFileSearchDrawer();
		return 0;
	case EDITOR_DRAWER_MODE_PROJECT_SEARCH:
		editorOpenProjectSearchDrawer();
		return 0;
	case EDITOR_DRAWER_MODE_LSP:
		editorHistoryBreakGroup();
		if (E.drawer_mode != EDITOR_DRAWER_MODE_LSP || editorFileSearchIsActive() ||
				editorProjectSearchIsActive()) {
			(void)editorDrawerLspToggle();
			editorSetStatusMsg("LSP drawer shown");
		}
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 0;
	case EDITOR_DRAWER_MODE_DAP:
		editorHistoryBreakGroup();
		if (E.drawer_mode != EDITOR_DRAWER_MODE_DAP || editorFileSearchIsActive() ||
				editorProjectSearchIsActive()) {
			(void)editorDrawerDapToggle();
			editorSetStatusMsg("DAP drawer shown");
		}
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 0;
	case EDITOR_DRAWER_MODE_MAIN_MENU:
		editorHistoryBreakGroup();
		if (E.drawer_mode != EDITOR_DRAWER_MODE_MAIN_MENU || editorFileSearchIsActive() ||
				editorProjectSearchIsActive()) {
			(void)editorDrawerMainMenuToggle();
			editorSetStatusMsg("Main menu opened");
		}
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 0;
	case EDITOR_DRAWER_MODE_GIT:
		editorHistoryBreakGroup();
		if (E.drawer_mode != EDITOR_DRAWER_MODE_GIT || editorFileSearchIsActive() ||
				editorProjectSearchIsActive()) {
			(void)editorDrawerGitToggle();
			editorSetStatusMsg(E.git_repo_root != NULL ? "Git changes shown" :
					"Not in a git repository");
		}
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 0;
	default:
		return 0;
	}
}
