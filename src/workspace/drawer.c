#include "workspace/drawer.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/project_search.h"
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

struct editorDrawerMenuItem {
	const char *name;
	enum editorAction action;
};

struct editorDrawerMenuGroup {
	const char *name;
	const struct editorDrawerMenuItem *items;
	int item_count;
};

enum editorDrawerMenuEntryKind {
	EDITOR_DRAWER_MENU_ENTRY_ROOT = 0,
	EDITOR_DRAWER_MENU_ENTRY_GROUP,
	EDITOR_DRAWER_MENU_ENTRY_ITEM
};

struct editorDrawerMenuLookup {
	enum editorDrawerMenuEntryKind kind;
	int group_idx;
	int item_idx;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
};

static const struct editorDrawerMenuItem editor_drawer_menu_search_items[] = {
	{"Find File", EDITOR_ACTION_FIND_FILE},
	{"Find in Buffer", EDITOR_ACTION_FIND},
	{"Search Project Text", EDITOR_ACTION_PROJECT_SEARCH},
	{"Find & replace", EDITOR_ACTION_FIND_REPLACE},
	{"Go to Line", EDITOR_ACTION_GOTO_LINE},
	{"Go to Definition", EDITOR_ACTION_GOTO_DEFINITION},
};

static const struct editorDrawerMenuItem editor_drawer_menu_file_items[] = {
	{"Save", EDITOR_ACTION_SAVE},
	{"New Tab", EDITOR_ACTION_NEW_TAB},
	{"Close Tab", EDITOR_ACTION_CLOSE_TAB},
	{"Quit", EDITOR_ACTION_QUIT},
};

static const struct editorDrawerMenuItem editor_drawer_menu_tabs_items[] = {
	{"Next Tab", EDITOR_ACTION_NEXT_TAB},
	{"Previous Tab", EDITOR_ACTION_PREV_TAB},
};

static const struct editorDrawerMenuItem editor_drawer_menu_edit_items[] = {
	{"Undo", EDITOR_ACTION_UNDO},
	{"Redo", EDITOR_ACTION_REDO},
	{"Toggle Selection", EDITOR_ACTION_TOGGLE_SELECTION},
	{"Copy Selection", EDITOR_ACTION_COPY_SELECTION},
	{"Cut Selection", EDITOR_ACTION_CUT_SELECTION},
	{"Paste", EDITOR_ACTION_PASTE},
	{"Delete Selection", EDITOR_ACTION_DELETE_SELECTION},
	{"Toggle Comment", EDITOR_ACTION_TOGGLE_COMMENT},
};

static const struct editorDrawerMenuItem editor_drawer_menu_view_items[] = {
	{"Project Files", EDITOR_ACTION_MAIN_MENU},
	{"Collapse Drawer", EDITOR_ACTION_TOGGLE_DRAWER},
	{"Toggle Line Wrap", EDITOR_ACTION_TOGGLE_LINE_WRAP},
	{"Toggle Line Numbers", EDITOR_ACTION_TOGGLE_LINE_NUMBERS},
	{"Toggle Current Line", EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT},
};

static const struct editorDrawerMenuGroup editor_drawer_menu_groups[] = {
	{"Find", editor_drawer_menu_search_items,
			(int)(sizeof(editor_drawer_menu_search_items) /
					sizeof(editor_drawer_menu_search_items[0]))},
	{"File", editor_drawer_menu_file_items,
			(int)(sizeof(editor_drawer_menu_file_items) /
					sizeof(editor_drawer_menu_file_items[0]))},
	{"Tabs", editor_drawer_menu_tabs_items,
			(int)(sizeof(editor_drawer_menu_tabs_items) /
					sizeof(editor_drawer_menu_tabs_items[0]))},
	{"Edit", editor_drawer_menu_edit_items,
			(int)(sizeof(editor_drawer_menu_edit_items) /
					sizeof(editor_drawer_menu_edit_items[0]))},
	{"View", editor_drawer_menu_view_items,
			(int)(sizeof(editor_drawer_menu_view_items) /
					sizeof(editor_drawer_menu_view_items[0]))},
};

static const int editor_drawer_menu_group_count =
		(int)(sizeof(editor_drawer_menu_groups) / sizeof(editor_drawer_menu_groups[0]));

static int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out);
static int editorDrawerMenuVisibleCount(void);
static int editorDrawerMenuLookupByVisibleIndex(int visible_idx,
		struct editorDrawerMenuLookup *lookup_out);
static void editorDrawerMenuEnsureDefaultExpanded(void);
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

static unsigned int editorDrawerMenuAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < editor_drawer_menu_group_count; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static int editorDrawerMenuGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= editor_drawer_menu_group_count) {
		return 0;
	}
	return (E.drawer_menu_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static void editorDrawerMenuEnsureDefaultExpanded(void) {
	E.drawer_menu_expanded = editorDrawerMenuAllGroupsMask();
}

static int editorDrawerMenuVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < editor_drawer_menu_group_count; group_idx++) {
		count++;
		if (editorDrawerMenuGroupExpanded(group_idx)) {
			count += editor_drawer_menu_groups[group_idx].item_count;
		}
	}
	return count;
}

static int editorDrawerMenuLookupByVisibleIndex(int visible_idx,
		struct editorDrawerMenuLookup *lookup_out) {
	if (lookup_out == NULL || visible_idx < 0) {
		return 0;
	}

	memset(lookup_out, 0, sizeof(*lookup_out));
	lookup_out->visible_idx = visible_idx;
	lookup_out->group_idx = -1;
	lookup_out->item_idx = -1;
	lookup_out->parent_visible_idx = -1;
	lookup_out->group_visible_idx = -1;

	if (visible_idx == 0) {
		lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_ROOT;
		return 1;
	}

	int cursor = 1;
	for (int group_idx = 0; group_idx < editor_drawer_menu_group_count; group_idx++) {
		int group_visible_idx = cursor;
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			return 1;
		}
		cursor++;

		if (!editorDrawerMenuGroupExpanded(group_idx)) {
			continue;
		}

		const struct editorDrawerMenuGroup *group = &editor_drawer_menu_groups[group_idx];
		for (int item_idx = 0; item_idx < group->item_count; item_idx++) {
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_ITEM;
				lookup_out->group_idx = group_idx;
				lookup_out->item_idx = item_idx;
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->group_visible_idx = group_visible_idx;
				return 1;
			}
			cursor++;
		}
	}

	return 0;
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

static struct editorDrawerNode *editorDrawerFindChildByName(struct editorDrawerNode *node,
		const char *name, size_t name_len) {
	if (node == NULL || !node->is_dir || name == NULL) {
		return NULL;
	}
	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		if (strlen(node->children[i]->name) == name_len &&
				strncmp(node->children[i]->name, name, name_len) == 0) {
			return node->children[i];
		}
	}
	return NULL;
}

static int editorDrawerFindVisibleIndexForNodeRecursive(struct editorDrawerNode *node,
		struct editorDrawerNode *target, int *cursor, int *visible_idx_out) {
	if (node == NULL || target == NULL || cursor == NULL || visible_idx_out == NULL) {
		return 0;
	}

	int current = *cursor;
	if (node == target) {
		*visible_idx_out = current;
		return 1;
	}
	(*cursor)++;
	if (!node->is_dir || !node->is_expanded) {
		return 0;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		if (editorDrawerFindVisibleIndexForNodeRecursive(node->children[i], target, cursor,
					visible_idx_out)) {
			return 1;
		}
	}
	return 0;
}

static int editorDrawerFindVisibleIndexForNode(struct editorDrawerNode *target,
		int *visible_idx_out) {
	int cursor = 0;
	return editorDrawerFindVisibleIndexForNodeRecursive(E.drawer_root, target, &cursor,
			visible_idx_out);
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

int editorDrawerMainMenuToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 1;
	}

	if (editorFileSearchIsActive()) {
		editorFileSearchExit(1);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(1);
	}
	editorDrawerMenuEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_MAIN_MENU;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.pane_focus = EDITOR_PANE_DRAWER;
	return 1;
}

int editorDrawerCollapsedToggleWidthForCols(int total_cols) {
	if (total_cols <= 0) {
		return 0;
	}
	if (total_cols < ROTIDE_DRAWER_COLLAPSED_WIDTH) {
		return total_cols;
	}
	return ROTIDE_DRAWER_COLLAPSED_WIDTH;
}

int editorDrawerWidthForCols(int total_cols) {
	if (editorDrawerIsCollapsed()) {
		return 0;
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

static int editorLineNumberDigitCols(void) {
	int rows = E.numrows > 0 ? E.numrows : 1;
	int digits = 1;
	while (rows >= 10) {
		rows /= 10;
		digits++;
	}
	return digits;
}

int editorLineNumberGutterColsForCols(int total_cols) {
	if (!E.line_numbers_enabled) {
		return 0;
	}

	int text_cols = editorDrawerTextViewportCols(total_cols);
	if (text_cols <= 1) {
		return 0;
	}

	int gutter_cols = editorLineNumberDigitCols() + 1;
	if (gutter_cols >= text_cols) {
		gutter_cols = text_cols - 1;
	}
	return gutter_cols;
}

int editorTextBodyStartColForCols(int total_cols) {
	int text_start = editorDrawerTextStartColForCols(total_cols);
	int text_cols = editorDrawerTextViewportCols(total_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(total_cols);
	text_start += gutter_cols;
	text_cols -= gutter_cols;
	if (text_cols >= 3) {
		return text_start + 1;
	}
	return text_start;
}

int editorTextBodyViewportCols(int total_cols) {
	int text_cols = editorDrawerTextViewportCols(total_cols);
	text_cols -= editorLineNumberGutterColsForCols(total_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}
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
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchVisibleCount();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return editorDrawerMenuVisibleCount();
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
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchGetVisibleEntry(visible_idx, view_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(visible_idx, &lookup)) {
			return 0;
		}

		memset(view_out, 0, sizeof(*view_out));
		view_out->is_selected = visible_idx == E.drawer_selected_index;
		view_out->parent_visible_idx = lookup.parent_visible_idx;
		switch (lookup.kind) {
		case EDITOR_DRAWER_MENU_ENTRY_ROOT:
			view_out->name = "Main Menu";
			view_out->depth = 0;
			view_out->is_dir = 1;
			view_out->is_expanded = 1;
			view_out->is_root = 1;
			view_out->is_last_sibling = 1;
			return 1;
		case EDITOR_DRAWER_MENU_ENTRY_GROUP:
			view_out->name = editor_drawer_menu_groups[lookup.group_idx].name;
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = editorDrawerMenuGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling = lookup.group_idx == editor_drawer_menu_group_count - 1;
			return 1;
		case EDITOR_DRAWER_MENU_ENTRY_ITEM:
			view_out->name =
					editor_drawer_menu_groups[lookup.group_idx].items[lookup.item_idx].name;
			view_out->depth = 2;
			view_out->is_last_sibling =
					lookup.item_idx ==
					editor_drawer_menu_groups[lookup.group_idx].item_count - 1;
			return 1;
		default:
			return 0;
		}
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
	if (E.git_repo_root != NULL) {
		if (lookup.node->is_dir) {
			view_out->git_status = editorGitDirStatus(lookup.node->path);
		} else {
			view_out->git_status = editorGitFileStatus(lookup.node->path);
		}
	}
	return 1;
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

int editorDrawerExpandSelection(int viewport_rows) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_MENU_ENTRY_GROUP ||
				editorDrawerMenuGroupExpanded(lookup.group_idx)) {
			return 0;
		}
		E.drawer_menu_expanded |= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
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
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_GROUP) {
			if (!editorDrawerMenuGroupExpanded(lookup.group_idx)) {
				return 0;
			}
			E.drawer_menu_expanded &= ~(1u << (unsigned int)lookup.group_idx);
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ITEM) {
			E.drawer_selected_index = lookup.group_visible_idx;
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
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
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_MENU_ENTRY_GROUP) {
			return 0;
		}
		E.drawer_menu_expanded ^= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
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

int editorDrawerSelectedIsDirectory(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchSelectedIsDirectory();
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchSelectedIsDirectory();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		return lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT ||
				lookup.kind == EDITOR_DRAWER_MENU_ENTRY_GROUP;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node->is_dir;
}

int editorDrawerSelectedMenuAction(enum editorAction *action_out) {
	if (action_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_MAIN_MENU) {
		return 0;
	}
	struct editorDrawerMenuLookup lookup;
	if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
			lookup.kind != EDITOR_DRAWER_MENU_ENTRY_ITEM) {
		return 0;
	}
	*action_out = editor_drawer_menu_groups[lookup.group_idx].items[lookup.item_idx].action;
	return 1;
}

int editorDrawerOpenSelectedFileInTab(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchOpenSelectedFileInTab();
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchOpenSelectedFileInTab();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return 0;
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
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchOpenSelectedFileInPreviewTab();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return 0;
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

int editorDrawerRevealPath(const char *path, int viewport_rows) {
	if (path == NULL || path[0] == '\0' || E.drawer_root == NULL || E.drawer_root_path == NULL) {
		return 0;
	}

	char *absolute = editorPathAbsoluteDup(path);
	if (absolute == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	size_t root_len = strlen(E.drawer_root_path);
	if (strcmp(absolute, E.drawer_root_path) == 0) {
		E.drawer_root->is_expanded = 1;
		E.drawer_selected_index = 0;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		free(absolute);
		return 1;
	}
	if (root_len == 0 || strncmp(absolute, E.drawer_root_path, root_len) != 0 ||
			absolute[root_len] != '/') {
		free(absolute);
		return 0;
	}

	struct editorDrawerNode *node = E.drawer_root;
	node->is_expanded = 1;
	const char *component = absolute + root_len + 1;
	while (component[0] != '\0') {
		const char *slash = strchr(component, '/');
		size_t component_len =
				slash != NULL ? (size_t)(slash - component) : strlen(component);
		if (component_len == 0) {
			free(absolute);
			return 0;
		}

		struct editorDrawerNode *child =
				editorDrawerFindChildByName(node, component, component_len);
		if (child == NULL) {
			free(absolute);
			return 0;
		}

		node = child;
		if (slash == NULL) {
			break;
		}
		if (!node->is_dir) {
			free(absolute);
			return 0;
		}
		node->is_expanded = 1;
		component = slash + 1;
	}

	int visible_idx = 0;
	if (!editorDrawerFindVisibleIndexForNode(node, &visible_idx)) {
		free(absolute);
		return 0;
	}
	E.drawer_selected_index = visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	free(absolute);
	return 1;
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
	editorDrawerClampSelectionAndScroll(E.window_rows);
	return 1;
}
