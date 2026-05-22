#include "input/actions_workspace.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "input/prompt.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
}

void editorToggleDrawerFocus(void) {
	if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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

int editorJumpToSelectedLspDrawerLocation(int preview, editorJumpToPathLocationFn jump_fn) {
	const char *path = NULL;
	int line = 0;
	int character = 0;
	if (!editorDrawerSelectedLspLocation(&path, &line, &character) || jump_fn == NULL) {
		return 0;
	}
	return jump_fn(path, line, character, preview, 1);
}

int editorJumpToSelectedDapDrawerLocation(int preview, editorJumpToPathLocationFn jump_fn) {
	const char *path = NULL;
	int line = 0;
	int character = 0;
	if (!editorDrawerSelectedDapLocation(&path, &line, &character) || jump_fn == NULL) {
		return 0;
	}
	return jump_fn(path, line, character, preview, 1);
}

void editorDrawerPreviewSelectionAfterMove(editorJumpToPathLocationFn jump_fn) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		(void)editorJumpToSelectedLspDrawerLocation(1, jump_fn);
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
		return;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		(void)editorJumpToSelectedDapDrawerLocation(1, jump_fn);
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
		return;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_TREE) {
		(void)editorDrawerOpenSelectedFileInPreviewTab();
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	}
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
					E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
					cursor_or_edit = 1;
				}
				break;
			case EDITOR_ACTION_ESCAPE:
				editorFileSearchExit(1);
				E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
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
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU
				                           ? "Main menu opened"
				                           : "Project drawer shown");
				break;
			case EDITOR_ACTION_GIT_DRAWER:
				(void)editorDrawerGitToggle();
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_GIT
				                           ? (E.git_repo_root != NULL
				                                      ? "Git changes shown"
				                                      : "Not in a git repository")
				                           : "Project drawer shown");
				break;
			case EDITOR_ACTION_LSP_DRAWER:
				(void)editorDrawerLspToggle();
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_LSP
				                           ? "LSP drawer shown"
				                           : "Project drawer shown");
				break;
			case EDITOR_ACTION_DAP_DRAWER:
				(void)editorDrawerDapToggle();
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_DAP
				                           ? "DAP drawer shown"
				                           : "Project drawer shown");
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
					E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
					cursor_or_edit = 1;
				}
				break;
			case EDITOR_ACTION_ESCAPE:
				editorProjectSearchExit(1);
				E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
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
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU
				                           ? "Main menu opened"
				                           : "Project drawer shown");
				break;
			case EDITOR_ACTION_GIT_DRAWER:
				(void)editorDrawerGitToggle();
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_GIT
				                           ? (E.git_repo_root != NULL
				                                      ? "Git changes shown"
				                                      : "Not in a git repository")
				                           : "Project drawer shown");
				break;
			case EDITOR_ACTION_LSP_DRAWER:
				(void)editorDrawerLspToggle();
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_LSP
				                           ? "LSP drawer shown"
				                           : "Project drawer shown");
				break;
			case EDITOR_ACTION_DAP_DRAWER:
				(void)editorDrawerDapToggle();
				editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_DAP
				                           ? "DAP drawer shown"
				                           : "Project drawer shown");
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

static enum editorDrawerMode actionsWorkspaceActiveDrawerHeaderMode(void) {
	if (editorFileSearchIsActive()) {
		return EDITOR_DRAWER_MODE_FILE_SEARCH;
	}
	if (editorProjectSearchIsActive()) {
		return EDITOR_DRAWER_MODE_PROJECT_SEARCH;
	}
	return E.drawer_mode;
}

int editorSwitchDrawerHeaderMode(enum editorDrawerMode mode) {
	if (actionsWorkspaceActiveDrawerHeaderMode() == mode) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
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
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
			return 0;
		case EDITOR_DRAWER_MODE_DAP:
			editorHistoryBreakGroup();
			if (E.drawer_mode != EDITOR_DRAWER_MODE_DAP || editorFileSearchIsActive() ||
			    editorProjectSearchIsActive()) {
				(void)editorDrawerDapToggle();
				editorSetStatusMsg("DAP drawer shown");
			}
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
			return 0;
		case EDITOR_DRAWER_MODE_MAIN_MENU:
			editorHistoryBreakGroup();
			if (E.drawer_mode != EDITOR_DRAWER_MODE_MAIN_MENU ||
			    editorFileSearchIsActive() || editorProjectSearchIsActive()) {
				(void)editorDrawerMainMenuToggle();
				editorSetStatusMsg("Main menu opened");
			}
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
			return 0;
		case EDITOR_DRAWER_MODE_GIT:
			editorHistoryBreakGroup();
			if (E.drawer_mode != EDITOR_DRAWER_MODE_GIT || editorFileSearchIsActive() ||
			    editorProjectSearchIsActive()) {
				(void)editorDrawerGitToggle();
				editorSetStatusMsg(E.git_repo_root != NULL
				                           ? "Git changes shown"
				                           : "Not in a git repository");
			}
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
			return 0;
		default:
			return 0;
	}
}

int editorActionMoveActiveTabToNeighborPane(enum editorFocusDirection direction) {
	if (E.layout_root == NULL || E.focused_leaf == NULL || E.focused_leaf->is_split ||
	    E.focused_leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return 0;
	}

	struct editorPaneNode *source = E.focused_leaf;
	struct editorPaneView *source_view = &source->as.leaf.view;
	if (source_view->pane_tab_count <= 0) {
		return 0;
	}
	int tab_idx = source_view->active_tab_idx;
	if (tab_idx < 0) {
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
	struct editorPaneNode *target = editorLayoutFindNeighborLeaf(&layout, source, direction);
	editorLeafLayoutFree(&layout);
	if (target == NULL || target->is_split || target->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return 0;
	}

	if (!editorPaneMoveTab(source, target, tab_idx, target->as.leaf.view.pane_tab_count)) {
		return 0;
	}
	editorPaneAnnounceFocus();
	return 1;
}

int editorHandleWorkspaceMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
                                      editorWorkspaceProcessMappedActionFn process_mapped_action,
                                      editorJumpToPathLocationFn jump_fn, int *effects_io) {
	int effects = effects_io != NULL ? *effects_io : 0;
	switch (action) {
		case EDITOR_ACTION_FOCUS_DRAWER:
			editorHistoryBreakGroup();
			editorToggleDrawerFocus();
			return 1;
		case EDITOR_ACTION_TOGGLE_DRAWER:
			editorHistoryBreakGroup();
			if (editorDrawerToggleCollapsed()) {
				editorSetDrawerCollapseStatus(editorDrawerIsCollapsed());
				if (!editorDrawerIsCollapsed()) {
					E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
				}
			}
			return 1;
		case EDITOR_ACTION_MAIN_MENU:
			editorHistoryBreakGroup();
			(void)editorDrawerMainMenuToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU
			                           ? "Main menu opened"
			                           : "Project drawer shown");
			return 1;
		case EDITOR_ACTION_GIT_DRAWER:
			editorHistoryBreakGroup();
			(void)editorDrawerGitToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_GIT
			                           ? (E.git_repo_root != NULL
			                                      ? "Git changes shown"
			                                      : "Not in a git repository")
			                           : "Project drawer shown");
			return 1;
		case EDITOR_ACTION_LSP_DRAWER:
			editorHistoryBreakGroup();
			(void)editorDrawerLspToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_LSP
			                           ? "LSP drawer shown"
			                           : "Project drawer shown");
			return 1;
		case EDITOR_ACTION_DAP_DRAWER:
			editorHistoryBreakGroup();
			(void)editorDrawerDapToggle();
			editorSetStatusMsg(E.drawer_mode == EDITOR_DRAWER_MODE_DAP
			                           ? "DAP drawer shown"
			                           : "Project drawer shown");
			return 1;
		case EDITOR_ACTION_SPLIT_HORIZONTAL:
			editorHistoryBreakGroup();
			if (editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5) != NULL) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_SPLIT_VERTICAL:
			editorHistoryBreakGroup();
			if (editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5) != NULL) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_CLOSE_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutCloseFocused() != NULL) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_FOCUS_LEFT_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_LEFT)) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_FOCUS_RIGHT_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_RIGHT)) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_FOCUS_UP_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_UP)) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_FOCUS_DOWN_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutFocusDirection(EDITOR_FOCUS_DOWN)) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_MOVE_TAB_LEFT_PANE:
			editorHistoryBreakGroup();
			(void)editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_LEFT);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_RIGHT_PANE:
			editorHistoryBreakGroup();
			(void)editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_RIGHT);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_UP_PANE:
			editorHistoryBreakGroup();
			(void)editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_UP);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_DOWN_PANE:
			editorHistoryBreakGroup();
			(void)editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_DOWN);
			return 1;
		case EDITOR_ACTION_PANE_GROW:
			editorHistoryBreakGroup();
			(void)editorLayoutResizeFocused(1);
			return 1;
		case EDITOR_ACTION_PANE_SHRINK:
			editorHistoryBreakGroup();
			(void)editorLayoutResizeFocused(0);
			return 1;
		case EDITOR_ACTION_RESIZE_DRAWER_NARROW:
			editorHistoryBreakGroup();
			if (editorDrawerIsCollapsed()) {
				(void)editorDrawerSetCollapsed(0);
			}
			(void)editorDrawerResizeByDeltaForCols(-1, E.window_cols);
			return 1;
		case EDITOR_ACTION_RESIZE_DRAWER_WIDEN:
			editorHistoryBreakGroup();
			if (editorDrawerIsCollapsed()) {
				(void)editorDrawerSetCollapsed(0);
			}
			(void)editorDrawerResizeByDeltaForCols(1, E.window_cols);
			return 1;
		case EDITOR_ACTION_FIND_FILE:
			editorOpenFileSearchDrawer();
			return 1;
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorOpenProjectSearchDrawer();
			return 1;
		default:
			break;
	}

	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
		return 0;
	}

	switch (action) {
		case EDITOR_ACTION_COLUMN_SELECT_LEFT:
			editorHistoryBreakGroup();
			if (editorDrawerIsCollapsed()) {
				(void)editorDrawerSetCollapsed(0);
			}
			(void)editorDrawerResizeByDeltaForCols(-1, E.window_cols);
			return 1;
		case EDITOR_ACTION_COLUMN_SELECT_RIGHT:
			editorHistoryBreakGroup();
			if (editorDrawerIsCollapsed()) {
				(void)editorDrawerSetCollapsed(0);
			}
			(void)editorDrawerResizeByDeltaForCols(1, E.window_cols);
			return 1;
		case EDITOR_ACTION_MOVE_UP:
			editorHistoryBreakGroup();
			if (editorDrawerMoveSelectionBy(-1, E.window_rows)) {
				editorDrawerPreviewSelectionAfterMove(jump_fn);
			}
			return 1;
		case EDITOR_ACTION_MOVE_DOWN:
			editorHistoryBreakGroup();
			if (editorDrawerMoveSelectionBy(1, E.window_rows)) {
				editorDrawerPreviewSelectionAfterMove(jump_fn);
			}
			return 1;
		case EDITOR_ACTION_MOVE_LEFT:
		case EDITOR_ACTION_MOVE_WORD_LEFT:
			editorHistoryBreakGroup();
			(void)editorDrawerCollapseSelection(E.window_rows);
			return 1;
		case EDITOR_ACTION_MOVE_RIGHT:
		case EDITOR_ACTION_MOVE_WORD_RIGHT:
			editorHistoryBreakGroup();
			(void)editorDrawerExpandSelection(E.window_rows);
			return 1;
		case EDITOR_ACTION_NEWLINE: {
			editorHistoryBreakGroup();
			E.drawer_last_click_visible_idx = -1;
			E.drawer_last_click_ms = 0;
			if (editorDrawerSelectedIsDirectory()) {
				(void)editorDrawerToggleSelectionExpanded(E.window_rows);
			} else if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
				if (editorOpenSelectedGitDiff()) {
					E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
					effects |= cursor_or_edit_effect_bit;
				}
			} else if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
				if (editorJumpToSelectedLspDrawerLocation(0, jump_fn)) {
					E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
					effects |= cursor_or_edit_effect_bit;
				}
			} else if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
				int launch_idx = -1;
				int default_idx = -1;
				if (editorDrawerSelectedDapLaunch(&launch_idx)) {
					E.dap_selected_launch = launch_idx;
					if (editorDapStartLaunch(launch_idx)) {
						effects |= cursor_or_edit_effect_bit;
					}
				} else if (editorDrawerSelectedDapDefault(&default_idx)) {
					if (editorDapCreateProjectLaunchFromDefault(
					            default_idx, E.drawer_root_path)) {
						effects |= cursor_or_edit_effect_bit;
					}
				} else if (editorJumpToSelectedDapDrawerLocation(0, jump_fn)) {
					E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
					effects |= cursor_or_edit_effect_bit;
				}
			} else {
				enum editorAction menu_action = EDITOR_ACTION_COUNT;
				if (editorDrawerSelectedMenuAction(&menu_action)) {
					int mapped_effects = 0;
					if (process_mapped_action != NULL &&
					    process_mapped_action(menu_action, &mapped_effects)) {
						effects |= mapped_effects;
					}
					effects |= mapped_effects;
				} else if (editorDrawerOpenSelectedFileInTab()) {
					E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
				}
			}
			if (effects_io != NULL) {
				*effects_io = effects;
			}
			return 1;
		}
		default:
			return 0;
	}
}
