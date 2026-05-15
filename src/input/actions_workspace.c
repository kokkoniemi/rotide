#include "input/actions_workspace.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "input/prompt.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

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

void editorDrawerPromptCreateFile(void) {
	if (editorDrawerIsCollapsed() || E.drawer_root == NULL) {
		editorSetStatusMsg("Drawer is not visible");
		return;
	}
	char *name = editorPrompt("New file: %s");
	if (name == NULL) {
		return;
	}
	(void)editorDrawerCreateFileAtSelection(name, E.window_rows);
	free(name);
	E.pane_focus = EDITOR_PANE_DRAWER;
}

void editorDrawerPromptCreateFolder(void) {
	if (editorDrawerIsCollapsed() || E.drawer_root == NULL) {
		editorSetStatusMsg("Drawer is not visible");
		return;
	}
	char *name = editorPrompt("New folder: %s");
	if (name == NULL) {
		return;
	}
	(void)editorDrawerCreateFolderAtSelection(name, E.window_rows);
	free(name);
	E.pane_focus = EDITOR_PANE_DRAWER;
}

void editorDrawerPromptRename(void) {
	if (editorDrawerIsCollapsed() || E.drawer_root == NULL) {
		editorSetStatusMsg("Drawer is not visible");
		return;
	}
	if (editorDrawerSelectedIsRoot()) {
		editorSetStatusMsg("Cannot rename drawer root");
		return;
	}
	const char *path = editorDrawerSelectedPath();
	if (path == NULL) {
		editorSetStatusMsg("Select an entry to rename");
		return;
	}
	char *new_name = editorPrompt("Rename to: %s");
	if (new_name == NULL) {
		return;
	}
	(void)editorDrawerRenameSelection(new_name, E.window_rows);
	free(new_name);
	E.pane_focus = EDITOR_PANE_DRAWER;
}

void editorDrawerPromptDelete(void) {
	if (editorDrawerIsCollapsed() || E.drawer_root == NULL) {
		editorSetStatusMsg("Drawer is not visible");
		return;
	}
	if (editorDrawerSelectedIsRoot()) {
		editorSetStatusMsg("Cannot delete drawer root");
		return;
	}
	const char *path = editorDrawerSelectedPath();
	if (path == NULL) {
		editorSetStatusMsg("Select an entry to delete");
		return;
	}
	if (!editorPromptYesNo("Delete selection? [y/N] %s")) {
		editorSetStatusMsg("Delete cancelled");
		return;
	}
	(void)editorDrawerDeleteSelection(E.window_rows);
	E.pane_focus = EDITOR_PANE_DRAWER;
}

int editorOpenSelectedGitDiff(void) {
	int entry_idx = -1;
	if (!editorDrawerSelectedGitEntry(&entry_idx)) {
		return 0;
	}
	if (entry_idx < 0 || entry_idx >= E.git_entry_count) {
		return 0;
	}
	const struct editorGitEntry *entry = &E.git_entries[entry_idx];
	size_t diff_len = 0;
	char *diff_text = editorGitGenerateDiff(entry->rel_path, entry->index_status,
			entry->worktree_status, &diff_len);
	if (diff_text == NULL) {
		editorSetStatusMsg("Failed to generate git diff");
		return 0;
	}
	char title[PATH_MAX + 16];
	snprintf(title, sizeof(title), "git diff: %s", entry->rel_path);
	int ok = editorTabOpenGitDiff(title, diff_text);
	free(diff_text);
	return ok;
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
