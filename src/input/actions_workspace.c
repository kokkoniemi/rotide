#include "input/actions_workspace.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "input/prompt.h"
#include "render/popup.h"
#include "rotide.h"
#include "support/file_io.h"
#include "terminal/terminal_pane.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git_view.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int actionsWorkspaceSearchShouldRestoreCollapsed(void) {
	if (editorFileSearchIsActive() && E.drawer_search_restore_collapsed) {
		return 1;
	}
	if (editorProjectSearchIsActive() && E.drawer_project_search_restore_collapsed) {
		return 1;
	}
	return editorDrawerIsCollapsed();
}

static enum editorDrawerMode actionsWorkspaceSearchPreviousMode(void) {
	if (editorFileSearchIsActive()) {
		return E.drawer_search_mode_before;
	}
	if (editorProjectSearchIsActive()) {
		return E.drawer_project_search_mode_before;
	}
	return E.drawer_mode;
}

void editorOpenFileSearchDrawer(void) {
	int restore_collapsed = actionsWorkspaceSearchShouldRestoreCollapsed();
	enum editorDrawerMode mode_before = actionsWorkspaceSearchPreviousMode();
	editorHistoryBreakGroup();
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(0);
	}
	if (!editorFileSearchEnter()) {
		if (restore_collapsed) {
			(void)editorDrawerSetCollapsed(1);
		}
		return;
	}
	E.drawer_search_restore_collapsed = restore_collapsed;
	E.drawer_search_mode_before = mode_before;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	(void)editorFileSearchPreviewSelection();
}

void editorOpenProjectSearchDrawer(void) {
	int restore_collapsed = actionsWorkspaceSearchShouldRestoreCollapsed();
	enum editorDrawerMode mode_before = actionsWorkspaceSearchPreviousMode();
	editorHistoryBreakGroup();
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	if (editorFileSearchIsActive()) {
		editorFileSearchExit(0);
	}
	if (!editorProjectSearchEnter()) {
		if (restore_collapsed) {
			(void)editorDrawerSetCollapsed(1);
		}
		return;
	}
	E.drawer_project_search_restore_collapsed = restore_collapsed;
	E.drawer_project_search_mode_before = mode_before;
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

static int actionsWorkspaceMatchCmp(const void *a, const void *b) {
	const char *const *as = a;
	const char *const *bs = b;
	return strcmp(*as, *bs);
}

static char *actionsWorkspaceBuildCompletion(const char *user_prefix, const char *name,
                                             int append_slash) {
	size_t up_len = strlen(user_prefix);
	size_t name_len = strlen(name);
	size_t total = up_len + name_len + (append_slash ? 1 : 0) + 1;
	char *result = malloc(total);
	if (result == NULL) {
		return NULL;
	}
	memcpy(result, user_prefix, up_len);
	memcpy(result + up_len, name, name_len);
	if (append_slash) {
		result[up_len + name_len] = '/';
		result[up_len + name_len + 1] = '\0';
	} else {
		result[up_len + name_len] = '\0';
	}
	return result;
}

/*
 * Parse `input` into (abs_prefix, partial, user_prefix). `partial` points into
 * `input`; the other two are freshly allocated.
 */
static int actionsWorkspaceSplitCompletionInput(const char *input, char **abs_prefix_out,
                                                char **user_prefix_out, const char **partial_out) {
	*abs_prefix_out = NULL;
	*user_prefix_out = NULL;
	*partial_out = NULL;
	const char *last_slash = strrchr(input, '/');
	if (last_slash == NULL) {
		if (editorDrawerRootPath() == NULL) {
			return 0;
		}
		*abs_prefix_out = strdup(editorDrawerRootPath());
		*user_prefix_out = strdup("");
		*partial_out = input;
	} else {
		size_t prefix_len = (size_t)(last_slash - input);
		*partial_out = last_slash + 1;
		*user_prefix_out = strndup(input, prefix_len + 1);
		if (input[0] == '/') {
			*abs_prefix_out =
			        (prefix_len == 0) ? strdup("/") : strndup(input, prefix_len);
		} else if (editorDrawerRootPath() != NULL) {
			char *user_dir = strndup(input, prefix_len);
			if (user_dir != NULL) {
				*abs_prefix_out = editorPathJoin(editorDrawerRootPath(), user_dir);
				free(user_dir);
			}
		}
	}
	if (*abs_prefix_out == NULL || *user_prefix_out == NULL) {
		free(*abs_prefix_out);
		free(*user_prefix_out);
		*abs_prefix_out = NULL;
		*user_prefix_out = NULL;
		return 0;
	}
	return 1;
}

static char *actionsWorkspaceCompleteMovePath(const char *current, const char *anchor, void *ctx,
                                              int tab_iteration) {
	(void)ctx;
	char *result = NULL;
	char *abs_prefix = NULL;
	char *user_prefix = NULL;
	char **matches = NULL;
	int match_count = 0;
	int match_capacity = 0;
	DIR *dir = NULL;
	const char *partial = NULL;

	if (current == NULL || anchor == NULL) {
		goto cleanup;
	}

	/*
	 * Anchor the match-set parsing to the buffer at the start of the Tab cycle;
	 * subsequent Tabs preserve the original partial.
	 */
	if (!actionsWorkspaceSplitCompletionInput(anchor, &abs_prefix, &user_prefix, &partial)) {
		goto cleanup;
	}

	dir = opendir(abs_prefix);
	if (dir == NULL) {
		goto cleanup;
	}
	size_t partial_len = strlen(partial);
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (strncmp(entry->d_name, partial, partial_len) != 0) {
			continue;
		}
		char *child_path = editorPathJoin(abs_prefix, entry->d_name);
		if (child_path == NULL) {
			continue;
		}
		struct stat st;
		int is_dir = (lstat(child_path, &st) == 0 && S_ISDIR(st.st_mode));
		free(child_path);
		if (!is_dir) {
			continue;
		}
		if (match_count == match_capacity) {
			int new_capacity = match_capacity == 0 ? 8 : match_capacity * 2;
			char **grown = realloc(matches, (size_t)new_capacity * sizeof(*grown));
			if (grown == NULL) {
				goto cleanup;
			}
			matches = grown;
			match_capacity = new_capacity;
		}
		matches[match_count] = strdup(entry->d_name);
		if (matches[match_count] == NULL) {
			goto cleanup;
		}
		match_count++;
	}
	(void)closedir(dir);
	dir = NULL;

	if (match_count == 0) {
		goto cleanup;
	}

	qsort(matches, (size_t)match_count, sizeof(*matches), actionsWorkspaceMatchCmp);

	/*
	 * Repeated Tab: cycle through the anchored match set.
	 */
	if (tab_iteration > 0) {
		int cycle_idx = -1;
		size_t up_len = strlen(user_prefix);
		if (strncmp(current, user_prefix, up_len) == 0) {
			const char *suffix = current + up_len;
			size_t slen = strlen(suffix);
			for (int i = 0; i < match_count; i++) {
				size_t mn = strlen(matches[i]);
				if ((slen == mn && memcmp(suffix, matches[i], mn) == 0) ||
				    (slen == mn + 1 && memcmp(suffix, matches[i], mn) == 0 &&
				     suffix[mn] == '/')) {
					cycle_idx = i;
					break;
				}
			}
		}
		int next_idx = (cycle_idx >= 0) ? (cycle_idx + 1) % match_count : 0;
		result = actionsWorkspaceBuildCompletion(user_prefix, matches[next_idx], 1);
		goto cleanup;
	}

	if (match_count == 1) {
		result = actionsWorkspaceBuildCompletion(user_prefix, matches[0], 1);
		goto cleanup;
	}

	/* Multiple matches on first Tab: extend to the longest common prefix. */
	size_t common_len = strlen(matches[0]);
	for (int i = 1; i < match_count; i++) {
		size_t j = 0;
		while (j < common_len && matches[i][j] != '\0' && matches[0][j] == matches[i][j]) {
			j++;
		}
		common_len = j;
	}
	if (common_len > partial_len) {
		char saved = matches[0][common_len];
		matches[0][common_len] = '\0';
		result = actionsWorkspaceBuildCompletion(user_prefix, matches[0], 0);
		matches[0][common_len] = saved;
	}

cleanup:
	if (dir != NULL) {
		(void)closedir(dir);
	}
	if (matches != NULL) {
		for (int i = 0; i < match_count; i++) {
			free(matches[i]);
		}
		free(matches);
	}
	free(abs_prefix);
	free(user_prefix);
	return result;
}

char *editorDrawerMovePathCompletionTest(const char *current, const char *anchor,
                                         int tab_iteration) {
	return actionsWorkspaceCompleteMovePath(current, anchor, NULL, tab_iteration);
}

void editorDrawerPromptMove(void) {
	if (editorDrawerIsCollapsed() || E.drawer_root == NULL) {
		editorSetStatusMsg("Drawer is not visible");
		return;
	}
	if (editorDrawerSelectedIsRoot()) {
		editorSetStatusMsg("Cannot move drawer root");
		return;
	}
	const char *path = editorDrawerSelectedPath();
	if (path == NULL) {
		editorSetStatusMsg("Select an entry to move");
		return;
	}
	char *dest = editorPromptWithCompletion("Move to folder: %s", 0,
	                                        actionsWorkspaceCompleteMovePath, NULL);
	if (dest == NULL) {
		return;
	}
	/* Resolve relative paths against the drawer root so users can type "src/utils". */
	char *dest_abs = NULL;
	if (dest[0] == '/') {
		dest_abs = strdup(dest);
	} else if (editorDrawerRootPath() != NULL) {
		dest_abs = editorPathJoin(editorDrawerRootPath(), dest);
	} else {
		dest_abs = strdup(dest);
	}
	free(dest);
	if (dest_abs == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	(void)editorDrawerMoveSelectionToDir(dest_abs, E.window_rows);
	free(dest_abs);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
}

enum actionsWorkspaceCtxOp {
	ACTIONS_WORKSPACE_CTX_RENAME,
	ACTIONS_WORKSPACE_CTX_MOVE,
	ACTIONS_WORKSPACE_CTX_DELETE,
	ACTIONS_WORKSPACE_CTX_NEW_FILE,
	ACTIONS_WORKSPACE_CTX_NEW_FOLDER,
	ACTIONS_WORKSPACE_CTX_GIT_OPEN_DIFF,
	ACTIONS_WORKSPACE_CTX_GIT_STAGE,
	ACTIONS_WORKSPACE_CTX_GIT_UNSTAGE,
	ACTIONS_WORKSPACE_CTX_GIT_DISCARD
};

#define ACTIONS_WORKSPACE_CTX_MAX_ITEMS 5

static enum actionsWorkspaceCtxOp g_actionsWorkspace_ctx_ops[ACTIONS_WORKSPACE_CTX_MAX_ITEMS];
static int g_actionsWorkspace_ctx_op_count;

static void actionsWorkspaceCtxAppend(struct editorPopupItem *items, int *count_io,
                                      const char *label, enum actionsWorkspaceCtxOp op) {
	int idx = *count_io;
	if (idx >= ACTIONS_WORKSPACE_CTX_MAX_ITEMS) {
		return;
	}
	items[idx].label = (char *)label;
	items[idx].detail = NULL;
	g_actionsWorkspace_ctx_ops[idx] = op;
	*count_io = idx + 1;
}

/* Right-click menu for the selected Git drawer file row: open diff,
 * stage/unstage by the selected group, discard. */
int editorDrawerGitOpenSelectionMenu(int anchor_row, int anchor_col) {
	struct editorPopupItem items[ACTIONS_WORKSPACE_CTX_MAX_ITEMS];
	int count = 0;
	int entry_idx = 0;
	int staged_group = 0;
	int group_items = 0;
	if (editorDrawerGitSelectedFile(&entry_idx, &staged_group)) {
		actionsWorkspaceCtxAppend(items, &count, "Open Diff",
		                          ACTIONS_WORKSPACE_CTX_GIT_OPEN_DIFF);
		actionsWorkspaceCtxAppend(items, &count, staged_group ? "Unstage" : "Stage",
		                          staged_group ? ACTIONS_WORKSPACE_CTX_GIT_UNSTAGE
		                                       : ACTIONS_WORKSPACE_CTX_GIT_STAGE);
		actionsWorkspaceCtxAppend(items, &count, "Discard…",
		                          ACTIONS_WORKSPACE_CTX_GIT_DISCARD);
	} else if (editorDrawerGitSelectedGroup(&staged_group, &group_items) && group_items > 0) {
		/* Group header: one group-wide entry (Staged unstages, the others
		 * stage); the stage/unstage handler resolves the group itself. */
		actionsWorkspaceCtxAppend(items, &count, staged_group ? "Unstage all" : "Stage all",
		                          staged_group ? ACTIONS_WORKSPACE_CTX_GIT_UNSTAGE
		                                       : ACTIONS_WORKSPACE_CTX_GIT_STAGE);
	} else {
		return 0;
	}
	g_actionsWorkspace_ctx_op_count = count;
	if (!editorPopupOpenMenu(items, count, anchor_row, anchor_col)) {
		g_actionsWorkspace_ctx_op_count = 0;
		return 0;
	}
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	return 1;
}

int editorDrawerOpenContextMenuAt(const struct editorMouseEvent *event, int viewport_rows) {
	if (event == NULL) {
		return 0;
	}
	if (editorDrawerIsCollapsed()) {
		return 0;
	}
	if (E.drawer_mode != EDITOR_DRAWER_MODE_TREE && E.drawer_mode != EDITOR_DRAWER_MODE_GIT) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_TREE && E.drawer_root == NULL) {
		return 0;
	}
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int mouse_col = event->x - 1;
	int drawer_row = event->y - 1;
	if (mouse_col < 0 || mouse_col >= drawer_cols) {
		return 0;
	}
	/* Header row is reserved for mode-switch buttons; only entry rows open the menu. */
	if (drawer_row < 1 || drawer_row >= viewport_rows + 1) {
		return 0;
	}
	int visible_idx = E.drawer_rowoff + drawer_row - 1;
	if (!editorDrawerSelectVisibleIndex(visible_idx, viewport_rows)) {
		return 0;
	}

	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitOpenSelectionMenu(event->y, event->x);
	}

	struct editorPopupItem items[ACTIONS_WORKSPACE_CTX_MAX_ITEMS];
	int count = 0;
	int is_dir = editorDrawerSelectedIsDirectory();
	int is_root = editorDrawerSelectedIsRoot();
	if (is_dir) {
		actionsWorkspaceCtxAppend(items, &count, "New File",
		                          ACTIONS_WORKSPACE_CTX_NEW_FILE);
		actionsWorkspaceCtxAppend(items, &count, "New Folder",
		                          ACTIONS_WORKSPACE_CTX_NEW_FOLDER);
	}
	if (!is_root) {
		actionsWorkspaceCtxAppend(items, &count, "Rename", ACTIONS_WORKSPACE_CTX_RENAME);
		actionsWorkspaceCtxAppend(items, &count, "Move", ACTIONS_WORKSPACE_CTX_MOVE);
		actionsWorkspaceCtxAppend(items, &count, "Delete", ACTIONS_WORKSPACE_CTX_DELETE);
	}
	if (count == 0) {
		return 0;
	}
	g_actionsWorkspace_ctx_op_count = count;
	if (!editorPopupOpenMenu(items, count, event->y, event->x)) {
		g_actionsWorkspace_ctx_op_count = 0;
		return 0;
	}
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	return 1;
}

void editorDrawerContextMenuActivate(void) {
	int idx = editorPopupSelectedIndex();
	int count = g_actionsWorkspace_ctx_op_count;
	if (idx < 0 || idx >= count) {
		editorPopupClose();
		g_actionsWorkspace_ctx_op_count = 0;
		return;
	}
	enum actionsWorkspaceCtxOp op = g_actionsWorkspace_ctx_ops[idx];
	editorPopupClose();
	g_actionsWorkspace_ctx_op_count = 0;
	switch (op) {
		case ACTIONS_WORKSPACE_CTX_RENAME:
			editorDrawerPromptRename();
			break;
		case ACTIONS_WORKSPACE_CTX_MOVE:
			editorDrawerPromptMove();
			break;
		case ACTIONS_WORKSPACE_CTX_DELETE:
			editorDrawerPromptDelete();
			break;
		case ACTIONS_WORKSPACE_CTX_NEW_FILE:
			editorDrawerPromptCreateFile();
			break;
		case ACTIONS_WORKSPACE_CTX_NEW_FOLDER:
			editorDrawerPromptCreateFolder();
			break;
		case ACTIONS_WORKSPACE_CTX_GIT_OPEN_DIFF:
			if (editorOpenSelectedGitDiff()) {
				E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
			}
			break;
		case ACTIONS_WORKSPACE_CTX_GIT_STAGE:
			(void)editorGitViewHandleMappedAction(EDITOR_ACTION_GIT_STAGE);
			break;
		case ACTIONS_WORKSPACE_CTX_GIT_UNSTAGE:
			(void)editorGitViewHandleMappedAction(EDITOR_ACTION_GIT_UNSTAGE);
			break;
		case ACTIONS_WORKSPACE_CTX_GIT_DISCARD:
			(void)editorGitViewHandleMappedAction(EDITOR_ACTION_GIT_DISCARD);
			break;
	}
}

int editorOpenSelectedGitDiff(void) {
	int entry_idx = -1;
	int staged_group = 0;
	if (!editorDrawerGitSelectedFile(&entry_idx, &staged_group)) {
		return 0;
	}
	if (entry_idx < 0 || entry_idx >= E.git_entry_count) {
		return 0;
	}
	const struct editorGitEntry *entry = &E.git_entries[entry_idx];
	return editorGitViewOpenDiffForEntry(entry->rel_path, entry->index_status,
	                                     entry->worktree_status, staged_group);
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
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		if (editorOpenSelectedGitDiff()) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
		}
		return;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_TREE) {
		(void)editorDrawerOpenSelectedFileInPreviewTab();
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	}
}

static int actionsWorkspaceToggleDrawerHeaderMode(enum editorDrawerMode mode);

/* Handles actions shared between the file-search and project-search drawer modes.
 * Returns 1 if the action was consumed. */
static int actionsWorkspaceHandleSearchSharedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_FIND_FILE:
			editorOpenFileSearchDrawer();
			return 1;
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorOpenProjectSearchDrawer();
			return 1;
		case EDITOR_ACTION_TOGGLE_DRAWER:
			if (editorDrawerSetCollapsed(1)) {
				editorSetDrawerCollapseStatus(1);
			}
			return 1;
		case EDITOR_ACTION_EXPLORER_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_TREE);
			return 1;
		case EDITOR_ACTION_MAIN_MENU:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_MAIN_MENU);
			return 1;
		case EDITOR_ACTION_GIT_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_GIT);
			return 1;
		case EDITOR_ACTION_LSP_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_LSP);
			return 1;
		case EDITOR_ACTION_DAP_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_DAP);
			return 1;
		default:
			return 0;
	}
}

static void actionsWorkspaceHandleFileSearchAction(enum editorAction action, int *cursor_or_edit) {
	switch (action) {
		case EDITOR_ACTION_MOVE_UP:
			if (editorFileSearchMoveSelectionBy(-1, E.window_rows)) {
				(void)editorFileSearchPreviewSelection();
			}
			return;
		case EDITOR_ACTION_MOVE_DOWN:
			if (editorFileSearchMoveSelectionBy(1, E.window_rows)) {
				(void)editorFileSearchPreviewSelection();
			}
			return;
		case EDITOR_ACTION_NEWLINE:
			if (editorFileSearchOpenSelectedFileInTab()) {
				E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
				*cursor_or_edit = 1;
			}
			return;
		case EDITOR_ACTION_ESCAPE: {
			enum editorDrawerMode restore_mode = E.drawer_search_mode_before;
			int restore_collapsed = E.drawer_search_restore_collapsed;
			editorFileSearchExit(1);
			E.drawer_mode = restore_mode;
			if (restore_collapsed) {
				(void)editorDrawerSetCollapsed(1);
			}
			E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
			return;
		}
		case EDITOR_ACTION_BACKSPACE:
		case EDITOR_ACTION_DELETE_CHAR:
			if (editorFileSearchBackspace()) {
				(void)editorFileSearchPreviewSelection();
			}
			return;
		default:
			(void)actionsWorkspaceHandleSearchSharedAction(action);
			return;
	}
}

static void actionsWorkspaceHandleProjectSearchAction(enum editorAction action,
                                                      void (*project_replace_from_search)(void),
                                                      int *cursor_or_edit) {
	switch (action) {
		case EDITOR_ACTION_FIND_REPLACE:
			if (project_replace_from_search != NULL) {
				project_replace_from_search();
				*cursor_or_edit = 1;
			}
			return;
		case EDITOR_ACTION_MOVE_UP:
			if (editorProjectSearchMoveSelectionBy(-1, E.window_rows)) {
				(void)editorProjectSearchPreviewSelection();
			}
			return;
		case EDITOR_ACTION_MOVE_DOWN:
			if (editorProjectSearchMoveSelectionBy(1, E.window_rows)) {
				(void)editorProjectSearchPreviewSelection();
			}
			return;
		case EDITOR_ACTION_NEWLINE:
			if (editorProjectSearchOpenSelectedFileInTab()) {
				E.primary_focus = editorDrawerIsCollapsed()
				                          ? EDITOR_PRIMARY_FOCUS_TEXT
				                          : EDITOR_PRIMARY_FOCUS_DRAWER;
				*cursor_or_edit = 1;
			}
			return;
		case EDITOR_ACTION_ESCAPE: {
			enum editorDrawerMode restore_mode = E.drawer_project_search_mode_before;
			int restore_collapsed = E.drawer_project_search_restore_collapsed;
			editorProjectSearchExit(1);
			E.drawer_mode = restore_mode;
			if (restore_collapsed) {
				(void)editorDrawerSetCollapsed(1);
			}
			E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
			return;
		}
		case EDITOR_ACTION_BACKSPACE:
		case EDITOR_ACTION_DELETE_CHAR:
			if (editorProjectSearchBackspace()) {
				(void)editorProjectSearchPreviewSelection();
			}
			return;
		default:
			(void)actionsWorkspaceHandleSearchSharedAction(action);
			return;
	}
}

int editorHandleDrawerSearchMappedAction(enum editorAction action, int *cursor_or_edit_out,
                                         void (*project_replace_from_search)(void)) {
	int cursor_or_edit = 0;
	if (!editorDrawerIsCollapsed() && editorFileSearchIsActive()) {
		actionsWorkspaceHandleFileSearchAction(action, &cursor_or_edit);
	} else if (!editorDrawerIsCollapsed() && editorProjectSearchIsActive()) {
		actionsWorkspaceHandleProjectSearchAction(action, project_replace_from_search,
		                                          &cursor_or_edit);
	} else {
		return 0;
	}
	if (cursor_or_edit_out != NULL) {
		*cursor_or_edit_out = cursor_or_edit;
	}
	return 1;
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
		(void)editorDrawerSetCollapsed(0);
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

static int actionsWorkspaceToggleDrawerHeaderMode(enum editorDrawerMode mode) {
	if (actionsWorkspaceActiveDrawerHeaderMode() == mode) {
		editorHistoryBreakGroup();
		if (editorDrawerToggleCollapsed()) {
			editorSetDrawerCollapseStatus(editorDrawerIsCollapsed());
			if (!editorDrawerIsCollapsed()) {
				E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
			}
		}
		return 0;
	}
	return editorSwitchDrawerHeaderMode(mode);
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

static void actionsWorkspaceSplit(enum editorSplitOrientation orientation) {
	editorHistoryBreakGroup();
	struct editorPaneNode *sibling = editorLayoutSplitFocused(orientation, 0.5);
	if (sibling == NULL) {
		return;
	}
	/* Terminal tabs are single-hosted, so their new sibling needs an editor tab. */
	if (!sibling->is_split && sibling->as.leaf.view.active_tab_idx < 0) {
		int empty_idx = editorTabAppendEmptyForPane(sibling);
		if (empty_idx >= 0) {
			(void)editorTabSwitchToIndex(empty_idx);
		}
	}
	editorPaneAnnounceFocus();
}

static void actionsWorkspaceFocusDirection(enum editorFocusDirection dir) {
	editorHistoryBreakGroup();
	/* Pane navigation does not clear drawer focus; right returns to the last pane. */
	if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER && dir == EDITOR_FOCUS_RIGHT) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		struct editorPaneNode *target = E.focused_leaf;
		if (target == NULL || target->is_split) {
			target = editorPaneNodeFirstLeaf(E.layout_root);
		}
		if (target != NULL) {
			(void)editorLayoutSetFocusedLeaf(target);
		}
		editorPaneAnnounceFocus();
		return;
	}
	if (editorLayoutFocusDirection(dir)) {
		editorPaneAnnounceFocus();
		return;
	}
	if (dir == EDITOR_FOCUS_LEFT && !editorDrawerIsCollapsed() &&
	    editorDrawerRootPath() != NULL) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	}
}

static struct editorPaneNode *actionsWorkspaceLastLeaf(struct editorPaneNode *node) {
	if (node == NULL) {
		return NULL;
	}
	if (!node->is_split) {
		return node;
	}
	struct editorPaneNode *last = actionsWorkspaceLastLeaf(node->as.split.second);
	if (last != NULL) {
		return last;
	}
	return actionsWorkspaceLastLeaf(node->as.split.first);
}

static void actionsWorkspaceFocusNext(int reverse) {
	editorHistoryBreakGroup();
	int drawer_focusable = !editorDrawerIsCollapsed() && editorDrawerRootPath() != NULL;
	if (drawer_focusable && E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
		struct editorPaneNode *target = reverse ? actionsWorkspaceLastLeaf(E.layout_root)
		                                        : editorPaneNodeFirstLeaf(E.layout_root);
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		if (target != NULL && editorLayoutSetFocusedLeaf(target)) {
			editorPaneAnnounceFocus();
		}
		return;
	}
	if (drawer_focusable) {
		int index = -1;
		int count = 0;
		if (editorLayoutFocusedLeafIndex(&index, &count) &&
		    ((!reverse && index == count - 1) || (reverse && index == 0))) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
			return;
		}
	}
	if (editorLayoutFocusNext(reverse)) {
		editorPaneAnnounceFocus();
	}
}

static void actionsWorkspaceMoveTabDirection(enum editorFocusDirection dir) {
	editorHistoryBreakGroup();
	(void)editorActionMoveActiveTabToNeighborPane(dir);
}

static void actionsWorkspaceResizePane(int grow) {
	editorHistoryBreakGroup();
	if (editorLayoutResizeFocused(grow)) {
		editorTerminalPaneResizeAllToLayout(E.layout_root);
	}
}

static void actionsWorkspaceResizeDrawer(int delta) {
	editorHistoryBreakGroup();
	if (editorDrawerIsCollapsed()) {
		(void)editorDrawerSetCollapsed(0);
	}
	(void)editorDrawerResizeByDeltaForCols(delta, E.window_cols);
}

static void actionsWorkspaceDrawerDapActivate(editorJumpToPathLocationFn jump_fn,
                                              int cursor_or_edit_effect_bit, int *effects) {
	int launch_idx = -1;
	int default_idx = -1;
	if (editorDrawerSelectedDapLaunch(&launch_idx)) {
		E.dap_selected_launch = launch_idx;
		if (editorDapStartLaunch(launch_idx)) {
			*effects |= cursor_or_edit_effect_bit;
		}
		return;
	}
	if (editorDrawerSelectedDapDefault(&default_idx)) {
		if (editorDapCreateProjectLaunchFromDefault(default_idx, E.drawer_root_path)) {
			*effects |= cursor_or_edit_effect_bit;
		}
		return;
	}
	if (editorJumpToSelectedDapDrawerLocation(0, jump_fn)) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		*effects |= cursor_or_edit_effect_bit;
	}
}

static void
actionsWorkspaceDrawerFileOrMenuActivate(editorWorkspaceProcessMappedActionFn process_mapped_action,
                                         int *effects) {
	enum editorAction menu_action = EDITOR_ACTION_COUNT;
	if (editorDrawerSelectedMenuAction(&menu_action)) {
		int mapped_effects = 0;
		if (process_mapped_action != NULL) {
			(void)process_mapped_action(menu_action, &mapped_effects);
		}
		*effects |= mapped_effects;
		return;
	}
	if (editorDrawerOpenSelectedFileInTab()) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
}

static void
actionsWorkspaceDrawerNewlineActivate(editorWorkspaceProcessMappedActionFn process_mapped_action,
                                      editorJumpToPathLocationFn jump_fn,
                                      int cursor_or_edit_effect_bit, int *effects) {
	editorHistoryBreakGroup();
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	if (editorDrawerSelectedIsDirectory()) {
		(void)editorDrawerToggleSelectionExpanded(E.window_rows);
		return;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		enum editorAction git_action = EDITOR_ACTION_COUNT;
		if (editorDrawerGitSelectedAction(&git_action)) {
			(void)process_mapped_action(git_action, effects);
			return;
		}
		if (editorOpenSelectedGitDiff()) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		}
		return;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		if (editorJumpToSelectedLspDrawerLocation(0, jump_fn)) {
			E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
			*effects |= cursor_or_edit_effect_bit;
		}
		return;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		actionsWorkspaceDrawerDapActivate(jump_fn, cursor_or_edit_effect_bit, effects);
		return;
	}
	actionsWorkspaceDrawerFileOrMenuActivate(process_mapped_action, effects);
}

/* Returns 1 if the action was handled. */
static int actionsWorkspaceHandleGlobalAction(enum editorAction action) {
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
		case EDITOR_ACTION_EXPLORER_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_TREE);
			return 1;
		case EDITOR_ACTION_MAIN_MENU:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_MAIN_MENU);
			return 1;
		case EDITOR_ACTION_GIT_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_GIT);
			return 1;
		case EDITOR_ACTION_LSP_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_LSP);
			return 1;
		case EDITOR_ACTION_DAP_DRAWER:
			(void)actionsWorkspaceToggleDrawerHeaderMode(EDITOR_DRAWER_MODE_DAP);
			return 1;
		case EDITOR_ACTION_SPLIT_HORIZONTAL:
			actionsWorkspaceSplit(EDITOR_SPLIT_HORIZONTAL);
			return 1;
		case EDITOR_ACTION_SPLIT_VERTICAL:
			actionsWorkspaceSplit(EDITOR_SPLIT_VERTICAL);
			return 1;
		case EDITOR_ACTION_CLOSE_PANE:
			editorHistoryBreakGroup();
			if (editorLayoutCloseFocused() != NULL) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_CLOSE_OTHER_PANES:
			editorHistoryBreakGroup();
			if (editorLayoutCloseOthers()) {
				editorPaneAnnounceFocus();
			}
			return 1;
		case EDITOR_ACTION_FOCUS_NEXT_PANE:
			actionsWorkspaceFocusNext(0);
			return 1;
		case EDITOR_ACTION_FOCUS_PREV_PANE:
			actionsWorkspaceFocusNext(1);
			return 1;
		case EDITOR_ACTION_FOCUS_LEFT_PANE:
			actionsWorkspaceFocusDirection(EDITOR_FOCUS_LEFT);
			return 1;
		case EDITOR_ACTION_FOCUS_RIGHT_PANE:
			actionsWorkspaceFocusDirection(EDITOR_FOCUS_RIGHT);
			return 1;
		case EDITOR_ACTION_FOCUS_UP_PANE:
			actionsWorkspaceFocusDirection(EDITOR_FOCUS_UP);
			return 1;
		case EDITOR_ACTION_FOCUS_DOWN_PANE:
			actionsWorkspaceFocusDirection(EDITOR_FOCUS_DOWN);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_LEFT_PANE:
			actionsWorkspaceMoveTabDirection(EDITOR_FOCUS_LEFT);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_RIGHT_PANE:
			actionsWorkspaceMoveTabDirection(EDITOR_FOCUS_RIGHT);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_UP_PANE:
			actionsWorkspaceMoveTabDirection(EDITOR_FOCUS_UP);
			return 1;
		case EDITOR_ACTION_MOVE_TAB_DOWN_PANE:
			actionsWorkspaceMoveTabDirection(EDITOR_FOCUS_DOWN);
			return 1;
		case EDITOR_ACTION_PANE_GROW:
			actionsWorkspaceResizePane(1);
			return 1;
		case EDITOR_ACTION_PANE_SHRINK:
			actionsWorkspaceResizePane(0);
			return 1;
		case EDITOR_ACTION_RESIZE_DRAWER_NARROW:
			actionsWorkspaceResizeDrawer(-1);
			return 1;
		case EDITOR_ACTION_RESIZE_DRAWER_WIDEN:
			actionsWorkspaceResizeDrawer(1);
			return 1;
		case EDITOR_ACTION_FIND_FILE:
			editorOpenFileSearchDrawer();
			return 1;
		case EDITOR_ACTION_PROJECT_SEARCH:
			editorOpenProjectSearchDrawer();
			return 1;
		default:
			return 0;
	}
}

/* Returns 1 if the action was handled. */
static int
actionsWorkspaceHandleDrawerFocusAction(enum editorAction action, int cursor_or_edit_effect_bit,
                                        editorWorkspaceProcessMappedActionFn process_mapped_action,
                                        editorJumpToPathLocationFn jump_fn, int *effects) {
	switch (action) {
		case EDITOR_ACTION_COLUMN_SELECT_LEFT:
			actionsWorkspaceResizeDrawer(-1);
			return 1;
		case EDITOR_ACTION_COLUMN_SELECT_RIGHT:
			actionsWorkspaceResizeDrawer(1);
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
		case EDITOR_ACTION_NEWLINE:
			actionsWorkspaceDrawerNewlineActivate(process_mapped_action, jump_fn,
			                                      cursor_or_edit_effect_bit, effects);
			return 1;
		default:
			return 0;
	}
}

int editorHandleWorkspaceMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
                                      editorWorkspaceProcessMappedActionFn process_mapped_action,
                                      editorJumpToPathLocationFn jump_fn, int *effects_io) {
	if (actionsWorkspaceHandleGlobalAction(action)) {
		return 1;
	}
	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
		return 0;
	}
	int effects = effects_io != NULL ? *effects_io : 0;
	if (!actionsWorkspaceHandleDrawerFocusAction(action, cursor_or_edit_effect_bit,
	                                             process_mapped_action, jump_fn, &effects)) {
		return 0;
	}
	if (effects_io != NULL) {
		*effects_io = effects;
	}
	return 1;
}
