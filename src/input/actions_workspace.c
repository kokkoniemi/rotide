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
