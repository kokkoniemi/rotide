#include "workspace/drawer.h"

#include "editing/buffer_core.h"
#include "support/file_io.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

#include <stdlib.h>
#include <string.h>

static char *drawerResolveRootPathForStartup(int argc, char *argv[], int restored_session) {
	char *cwd = editorPathGetCwd();
	if (cwd == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	if (restored_session || argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
		return cwd;
	}

	char *absolute = NULL;
	if (argv[1][0] == '/') {
		absolute = strdup(argv[1]);
	} else {
		absolute = editorPathJoin(cwd, argv[1]);
	}
	free(cwd);
	if (absolute == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	char *dir = editorPathDirnameDup(absolute);
	free(absolute);
	if (dir == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	char *resolved = realpath(dir, NULL);
	if (resolved != NULL) {
		free(dir);
		return resolved;
	}

	return dir;
}

void editorDrawerClampViewport(int viewport_rows) {
	if (editorFileSearchIsActive()) {
		editorFileSearchClampViewport(viewport_rows);
		return;
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchClampViewport(viewport_rows);
		return;
	}
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		E.drawer_selected_index = 0;
		E.drawer_rowoff = 0;
		return;
	}

	if (E.drawer_selected_index < -1) {
		E.drawer_selected_index = -1;
	}
	if (E.drawer_selected_index >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}
	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	if (E.drawer_rowoff > max_rowoff) {
		E.drawer_rowoff = max_rowoff;
	}
	if (E.drawer_rowoff < 0) {
		E.drawer_rowoff = 0;
	}
}

void editorDrawerClampSelectionAndScroll(int viewport_rows) {
	editorDrawerClampViewport(viewport_rows);

	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		return;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}
	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	if (E.drawer_selected_index < 0) {
		return;
	}

	if (E.drawer_selected_index < E.drawer_rowoff) {
		E.drawer_rowoff = E.drawer_selected_index;
	}
	if (E.drawer_selected_index >= E.drawer_rowoff + viewport_rows) {
		E.drawer_rowoff = E.drawer_selected_index - viewport_rows + 1;
	}

	if (E.drawer_rowoff > max_rowoff) {
		E.drawer_rowoff = max_rowoff;
	}
	if (E.drawer_rowoff < 0) {
		E.drawer_rowoff = 0;
	}
}

int editorDrawerIsCollapsed(void) {
	return E.drawer_collapsed != 0;
}

int editorDrawerSetCollapsed(int collapsed) {
	int new_collapsed = collapsed != 0;
	if (E.drawer_collapsed == new_collapsed) {
		return 0;
	}

	E.drawer_collapsed = new_collapsed;
	E.drawer_resize_active = 0;
	if (new_collapsed && E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
	return 1;
}

int editorDrawerToggleCollapsed(void) {
	return editorDrawerSetCollapsed(!editorDrawerIsCollapsed());
}

int editorDrawerMoveSelectionBy(int delta, int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchMoveSelectionBy(delta, viewport_rows);
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchMoveSelectionBy(delta, viewport_rows);
	}
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		return 0;
	}

	if (E.drawer_selected_index < 0) {
		E.drawer_selected_index = delta < 0 ? visible_count - 1 : 0;
	} else if (delta < 0 && E.drawer_selected_index + delta < 0) {
		E.drawer_selected_index = 0;
	} else if (delta > 0 && E.drawer_selected_index + delta >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	} else {
		E.drawer_selected_index += delta;
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerScrollBy(int delta, int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		E.drawer_rowoff = 0;
		return 0;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}

	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	int old_rowoff = E.drawer_rowoff;
	int new_rowoff = E.drawer_rowoff + delta;
	if (new_rowoff < 0) {
		new_rowoff = 0;
	}
	if (new_rowoff > max_rowoff) {
		new_rowoff = max_rowoff;
	}
	E.drawer_rowoff = new_rowoff;
	return E.drawer_rowoff != old_rowoff;
}

int editorDrawerSelectVisibleIndex(int visible_idx, int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchSelectVisibleIndex(visible_idx, viewport_rows);
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchSelectVisibleIndex(visible_idx, viewport_rows);
	}
	int visible_count = editorDrawerVisibleCount();
	if (visible_idx < 0 || visible_idx >= visible_count) {
		return 0;
	}

	E.drawer_selected_index = visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectedIsRoot(void) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode != EDITOR_DRAWER_MODE_TREE) {
		return 0;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node == E.drawer_root;
}

const char *editorDrawerSelectedPath(void) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return NULL;
	}
	if (E.drawer_mode != EDITOR_DRAWER_MODE_TREE) {
		return NULL;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return NULL;
	}
	return lookup.node->path;
}

struct editorDrawerNode *editorDrawerSelectedTreeNode(void) {
	if (E.drawer_mode != EDITOR_DRAWER_MODE_TREE) {
		return NULL;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return NULL;
	}
	return lookup.node;
}

const char *editorDrawerRootPath(void) {
	return E.drawer_root_path;
}

void editorDrawerShutdown(void) {
	editorFileSearchFree();
	editorProjectSearchFree();
	editorDrawerNodeFree(E.drawer_root);
	E.drawer_root = NULL;
	free(E.drawer_root_path);
	E.drawer_root_path = NULL;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	E.drawer_resize_active = 0;
	E.drawer_collapsed = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
}

int editorDrawerInitForStartup(int argc, char *argv[], int restored_session) {
	editorDrawerShutdown();

	char *root_path = drawerResolveRootPathForStartup(argc, argv, restored_session);
	if (root_path == NULL) {
		return 0;
	}

	char *root_name = editorPathBasenameDup(root_path);
	if (root_name == NULL) {
		free(root_path);
		editorSetAllocFailureStatus();
		return 0;
	}

	struct editorDrawerNode *root = editorDrawerNodeNew(root_name, root_path, 1, NULL);
	free(root_name);
	if (root == NULL) {
		free(root_path);
		editorSetAllocFailureStatus();
		return 0;
	}
	root->is_expanded = 1;

	E.drawer_root_path = root_path;
	E.drawer_root = root;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	if (E.drawer_width_cols <= 0) {
		E.drawer_width_cols = ROTIDE_DRAWER_DEFAULT_WIDTH;
		E.drawer_width_user_set = 0;
	}
	E.drawer_collapsed = 0;
	E.drawer_resize_active = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	editorDrawerClampSelectionAndScroll(E.window_rows);
	return 1;
}
