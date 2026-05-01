#include "workspace/drawer.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "workspace/file_search.h"
#include "workspace/tabs.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct editorDrawerNode {
	char *name;
	char *path;
	int is_dir;
	int is_expanded;
	int scanned;
	int scan_error;
	struct editorDrawerNode *parent;
	struct editorDrawerNode **children;
	int child_count;
};

struct editorDrawerLookup {
	struct editorDrawerNode *node;
	int depth;
	int visible_idx;
	int parent_visible_idx;
};

static int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out);
static void editorDrawerClampSelectionAndScroll(int viewport_rows);

static char *editorDrawerResolveRootPathForStartup(int argc, char *argv[], int restored_session) {
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

static struct editorDrawerNode *editorDrawerNodeNew(const char *name, const char *path, int is_dir,
		struct editorDrawerNode *parent) {
	struct editorDrawerNode *node = editorMalloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}

	node->name = strdup(name);
	node->path = strdup(path);
	if (node->name == NULL || node->path == NULL) {
		free(node->name);
		free(node->path);
		free(node);
		return NULL;
	}

	node->is_dir = is_dir;
	node->is_expanded = 0;
	node->scanned = 0;
	node->scan_error = 0;
	node->parent = parent;
	node->children = NULL;
	node->child_count = 0;
	return node;
}

static void editorDrawerNodeFree(struct editorDrawerNode *node) {
	if (node == NULL) {
		return;
	}

	for (int i = 0; i < node->child_count; i++) {
		editorDrawerNodeFree(node->children[i]);
	}
	free(node->children);
	free(node->name);
	free(node->path);
	free(node);
}

static int editorDrawerNodeCmp(const void *a, const void *b) {
	const struct editorDrawerNode *left = *(const struct editorDrawerNode * const *)a;
	const struct editorDrawerNode *right = *(const struct editorDrawerNode * const *)b;

	if (left->is_dir != right->is_dir) {
		return right->is_dir - left->is_dir;
	}

	int ci_cmp = strcasecmp(left->name, right->name);
	if (ci_cmp != 0) {
		return ci_cmp;
	}
	return strcmp(left->name, right->name);
}

static int editorDrawerEnsureScanned(struct editorDrawerNode *node) {
	if (node == NULL || !node->is_dir || node->scanned) {
		return 1;
	}

	DIR *dir = opendir(node->path);
	if (dir == NULL) {
		node->scanned = 1;
		node->scan_error = 1;
		editorSetStatusMsg("Drawer scan failed: %s", strerror(errno));
		return 0;
	}

	struct editorDrawerNode **children = NULL;
	int child_count = 0;
	int child_capacity = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
			continue;
		}

		char *child_path = editorPathJoin(node->path, entry->d_name);
		if (child_path == NULL) {
			editorSetAllocFailureStatus();
			break;
		}

		struct stat st;
		int is_dir = 0;
		if (lstat(child_path, &st) == 0) {
			is_dir = S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
		}

		struct editorDrawerNode *child = editorDrawerNodeNew(entry->d_name, child_path, is_dir, node);
		free(child_path);
		if (child == NULL) {
			editorSetAllocFailureStatus();
			break;
		}

		if (child_count >= child_capacity) {
			int new_capacity = child_capacity > 0 ? child_capacity * 2 : 8;
			size_t cap_size = 0;
			size_t bytes = 0;
			if (!editorIntToSize(new_capacity, &cap_size) ||
					!editorSizeMul(sizeof(*children), cap_size, &bytes)) {
				editorDrawerNodeFree(child);
				editorSetAllocFailureStatus();
				break;
			}

			struct editorDrawerNode **grown = editorRealloc(children, bytes);
			if (grown == NULL) {
				editorDrawerNodeFree(child);
				editorSetAllocFailureStatus();
				break;
			}
			children = grown;
			child_capacity = new_capacity;
		}

		children[child_count++] = child;
	}

	(void)closedir(dir);

	if (child_count > 1) {
		qsort(children, (size_t)child_count, sizeof(*children), editorDrawerNodeCmp);
	}

	node->children = children;
	node->child_count = child_count;
	node->scanned = 1;
	return 1;
}

static int editorDrawerCountVisibleFromNode(struct editorDrawerNode *node) {
	if (node == NULL) {
		return 0;
	}

	int count = 1;
	if (!node->is_dir || !node->is_expanded) {
		return count;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		count += editorDrawerCountVisibleFromNode(node->children[i]);
	}
	return count;
}

static int editorDrawerLookupByVisibleIndexRecursive(struct editorDrawerNode *node, int depth,
		int parent_visible_idx, int *cursor, int target_visible_idx,
		struct editorDrawerLookup *lookup_out) {
	if (node == NULL || cursor == NULL || lookup_out == NULL) {
		return 0;
	}

	int current = *cursor;
	if (current == target_visible_idx) {
		lookup_out->node = node;
		lookup_out->depth = depth;
		lookup_out->visible_idx = current;
		lookup_out->parent_visible_idx = parent_visible_idx;
		return 1;
	}

	(*cursor)++;
	if (!node->is_dir || !node->is_expanded) {
		return 0;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		if (editorDrawerLookupByVisibleIndexRecursive(node->children[i], depth + 1, current, cursor,
					target_visible_idx, lookup_out)) {
			return 1;
		}
	}
	return 0;
}

static int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out) {
	if (lookup_out == NULL || E.drawer_root == NULL || visible_idx < 0) {
		return 0;
	}

	int cursor = 0;
	return editorDrawerLookupByVisibleIndexRecursive(E.drawer_root, 0, -1, &cursor, visible_idx,
			lookup_out);
}

void editorDrawerClampViewport(int viewport_rows) {
	if (editorFileSearchIsActive()) {
		editorFileSearchClampViewport(viewport_rows);
		return;
	}
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		E.drawer_selected_index = 0;
		E.drawer_rowoff = 0;
		return;
	}

	if (E.drawer_selected_index < 0) {
		E.drawer_selected_index = 0;
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

static void editorDrawerClampSelectionAndScroll(int viewport_rows) {
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

static int editorDrawerClampWidthForCols(int desired_width, int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	if (desired_width < 1) {
		desired_width = 1;
	}

	int max_drawer = total_cols - 2;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	if (desired_width > max_drawer) {
		desired_width = max_drawer;
	}

	return desired_width;
}

static int editorDrawerDefaultMaxWidthForCols(int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	int min_text_cols = total_cols / 2;
	if (min_text_cols < 1) {
		min_text_cols = 1;
	}
	int max_drawer = total_cols - 1 - min_text_cols;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	return max_drawer;
}

int editorDrawerIsCollapsed(void) {
	return E.drawer_collapsed != 0;
}

int editorDrawerSetCollapsed(int collapsed) {
	int new_collapsed = collapsed != 0;
	if (E.drawer_collapsed == new_collapsed) {
		return 0;
	}

	if (new_collapsed && editorFileSearchIsActive()) {
		editorFileSearchExit(1);
	}
	E.drawer_collapsed = new_collapsed;
	E.drawer_resize_active = 0;
	if (new_collapsed && E.pane_focus == EDITOR_PANE_DRAWER) {
		E.pane_focus = EDITOR_PANE_TEXT;
	}
	return 1;
}

int editorDrawerToggleCollapsed(void) {
	return editorDrawerSetCollapsed(!editorDrawerIsCollapsed());
}

int editorDrawerWidthForCols(int total_cols) {
	if (editorDrawerIsCollapsed()) {
		return editorDrawerClampWidthForCols(ROTIDE_DRAWER_COLLAPSED_WIDTH, total_cols);
	}

	int desired_width = E.drawer_width_cols;
	if (desired_width <= 0) {
		desired_width = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}

	int width = editorDrawerClampWidthForCols(desired_width, total_cols);
	if (!E.drawer_width_user_set) {
		int default_max = editorDrawerDefaultMaxWidthForCols(total_cols);
		if (width > default_max) {
			width = default_max;
		}
	}
	return width;
}

int editorDrawerSeparatorWidthForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	if (drawer_cols <= 0) {
		return 0;
	}
	return total_cols - drawer_cols >= 2 ? 1 : 0;
}

int editorDrawerTextStartColForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(total_cols);
	return drawer_cols + separator_cols;
}

int editorDrawerTextViewportCols(int total_cols) {
	if (total_cols <= 1) {
		return 1;
	}
	int text_cols = total_cols - editorDrawerTextStartColForCols(total_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}
	return text_cols;
}

int editorTextBodyStartColForCols(int total_cols) {
	int text_start = editorDrawerTextStartColForCols(total_cols);
	int text_cols = editorDrawerTextViewportCols(total_cols);
	if (text_cols >= 3) {
		return text_start + 1;
	}
	return text_start;
}

int editorTextBodyViewportCols(int total_cols) {
	int text_cols = editorDrawerTextViewportCols(total_cols);
	if (text_cols >= 3) {
		return text_cols - 2;
	}
	return text_cols;
}

int editorDrawerSetWidthForCols(int width, int total_cols) {
	int clamped = editorDrawerClampWidthForCols(width, total_cols);
	E.drawer_width_user_set = 1;
	if (E.drawer_width_cols == clamped) {
		return 0;
	}
	E.drawer_width_cols = clamped;
	return 1;
}

int editorDrawerResizeByDeltaForCols(int delta, int total_cols) {
	int current = editorDrawerIsCollapsed() ? E.drawer_width_cols :
			editorDrawerWidthForCols(total_cols);
	if (current <= 0) {
		current = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}
	return editorDrawerSetWidthForCols(current + delta, total_cols);
}

int editorDrawerVisibleCount(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchVisibleCount();
	}
	return editorDrawerCountVisibleFromNode(E.drawer_root);
}

int editorDrawerGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}
	if (editorFileSearchIsActive()) {
		return editorFileSearchGetVisibleEntry(visible_idx, view_out);
	}

	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(visible_idx, &lookup)) {
		return 0;
	}

	memset(view_out, 0, sizeof(*view_out));
	view_out->name = lookup.node->name;
	view_out->path = lookup.node->path;
	view_out->depth = lookup.depth;
	view_out->is_dir = lookup.node->is_dir;
	view_out->is_expanded = lookup.node->is_expanded;
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->has_scan_error = lookup.node->scan_error;
	view_out->is_root = lookup.node == E.drawer_root;
	view_out->parent_visible_idx = lookup.parent_visible_idx;
	if (lookup.node->parent != NULL && lookup.node->parent->child_count > 0 &&
			lookup.node->parent->children[lookup.node->parent->child_count - 1] == lookup.node) {
		view_out->is_last_sibling = 1;
	} else {
		view_out->is_last_sibling = lookup.node->parent == NULL;
	}
	view_out->is_active_file = !lookup.node->is_dir && E.filename != NULL &&
			editorPathsReferToSameFile(lookup.node->path, E.filename);
	return 1;
}

int editorDrawerMoveSelectionBy(int delta, int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchMoveSelectionBy(delta, viewport_rows);
	}
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		return 0;
	}

	if (delta < 0 && E.drawer_selected_index + delta < 0) {
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

int editorDrawerExpandSelection(int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return 0;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}

	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	lookup.node->is_expanded = 1;
	(void)editorDrawerEnsureScanned(lookup.node);
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerCollapseSelection(int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return 0;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}

	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}

	if (lookup.node->is_dir && lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	if (lookup.parent_visible_idx >= 0) {
		E.drawer_selected_index = lookup.parent_visible_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	return 0;
}

int editorDrawerToggleSelectionExpanded(int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return 0;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}
	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}

	if (lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
	} else {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectVisibleIndex(int visible_idx, int viewport_rows) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchSelectVisibleIndex(visible_idx, viewport_rows);
	}
	int visible_count = editorDrawerVisibleCount();
	if (visible_idx < 0 || visible_idx >= visible_count) {
		return 0;
	}

	E.drawer_selected_index = visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectedIsDirectory(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchSelectedIsDirectory();
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node->is_dir;
}

int editorDrawerOpenSelectedFileInTab(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchOpenSelectedFileInTab();
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.node->is_dir || lookup.node->path == NULL || lookup.node->path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToFile(lookup.node->path);
}

int editorDrawerOpenSelectedFileInPreviewTab(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchOpenSelectedFileInPreviewTab();
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.node->is_dir || lookup.node->path == NULL || lookup.node->path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToPreviewFile(lookup.node->path);
}

const char *editorDrawerRootPath(void) {
	return E.drawer_root_path;
}

void editorDrawerShutdown(void) {
	editorFileSearchFree();
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
	E.pane_focus = EDITOR_PANE_TEXT;
}

int editorDrawerInitForStartup(int argc, char *argv[], int restored_session) {
	editorDrawerShutdown();

	char *root_path = editorDrawerResolveRootPathForStartup(argc, argv, restored_session);
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
	E.pane_focus = EDITOR_PANE_TEXT;
	editorDrawerClampSelectionAndScroll(E.window_rows + 1);
	return 1;
}
