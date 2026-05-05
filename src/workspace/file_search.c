#include "workspace/file_search.h"

#include "editing/buffer_core.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *editorFileSearchRoot(void) {
	const char *root = editorDrawerRootPath();
	return root != NULL && root[0] != '\0' ? root : ".";
}

static const char *editorFileSearchDisplayPath(const char *path) {
	const char *root = editorFileSearchRoot();
	size_t root_len = strlen(root);
	if (path != NULL && root_len > 0 && strncmp(path, root, root_len) == 0 &&
			path[root_len] == '/') {
		return path + root_len + 1;
	}
	return path != NULL ? path : "";
}

static void editorFileSearchFreePathList(void) {
	for (int i = 0; i < E.drawer_search_path_count; i++) {
		free(E.drawer_search_paths[i]);
	}
	free(E.drawer_search_paths);
	E.drawer_search_paths = NULL;
	E.drawer_search_path_count = 0;
	E.drawer_search_path_capacity = 0;

	free(E.drawer_search_filtered_indices);
	E.drawer_search_filtered_indices = NULL;
	E.drawer_search_filtered_count = 0;
	E.drawer_search_filtered_capacity = 0;
}

void editorFileSearchFree(void) {
	editorFileSearchFreePathList();
	free(E.drawer_search_query);
	E.drawer_search_query = NULL;
	E.drawer_search_query_len = 0;
	free(E.drawer_search_previewed_path);
	E.drawer_search_previewed_path = NULL;
	E.drawer_search_active_tab_before = -1;
	if (E.drawer_mode == EDITOR_DRAWER_MODE_FILE_SEARCH) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	}
}

int editorFileSearchIsActive(void) {
	return E.drawer_mode == EDITOR_DRAWER_MODE_FILE_SEARCH;
}

const char *editorFileSearchQuery(void) {
	return E.drawer_search_query != NULL ? E.drawer_search_query : "";
}

static int editorFileSearchEnsurePathCapacity(int needed) {
	if (needed <= E.drawer_search_path_capacity) {
		return 1;
	}
	int new_capacity = E.drawer_search_path_capacity > 0 ? E.drawer_search_path_capacity * 2 : 64;
	while (new_capacity < needed) {
		if (new_capacity > INT_MAX / 2) {
			return 0;
		}
		new_capacity *= 2;
	}

	size_t cap_size = 0;
	size_t bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(*E.drawer_search_paths), cap_size, &bytes)) {
		return 0;
	}
	char **paths = editorRealloc(E.drawer_search_paths, bytes);
	if (paths == NULL) {
		return 0;
	}
	E.drawer_search_paths = paths;
	E.drawer_search_path_capacity = new_capacity;
	return 1;
}

static int editorFileSearchAppendPath(const char *path) {
	if (!editorFileSearchEnsurePathCapacity(E.drawer_search_path_count + 1)) {
		return 0;
	}
	char *copy = strdup(path);
	if (copy == NULL) {
		return 0;
	}
	E.drawer_search_paths[E.drawer_search_path_count] = copy;
	E.drawer_search_path_count++;
	return 1;
}

static int editorFileSearchShouldSkipDir(const char *name) {
	return strcmp(name, ".git") == 0 || strcmp(name, "node_modules") == 0;
}

static int editorFileSearchEnumerateDir(const char *dir_path) {
	DIR *dir = opendir(dir_path);
	if (dir == NULL) {
		return 1;
	}

	int ok = 1;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		char *child_path = editorPathJoin(dir_path, entry->d_name);
		if (child_path == NULL) {
			ok = 0;
			break;
		}

		struct stat st;
		if (lstat(child_path, &st) == -1) {
			free(child_path);
			continue;
		}

		if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
			if (!editorFileSearchShouldSkipDir(entry->d_name) &&
					!editorFileSearchEnumerateDir(child_path)) {
				ok = 0;
				free(child_path);
				break;
			}
		} else if (S_ISREG(st.st_mode)) {
			if (!editorFileSearchAppendPath(child_path)) {
				ok = 0;
				free(child_path);
				break;
			}
		}
		free(child_path);
	}

	closedir(dir);
	return ok;
}

static int editorFileSearchPathCmp(const void *a, const void *b) {
	const char *left = editorFileSearchDisplayPath(*(char * const *)a);
	const char *right = editorFileSearchDisplayPath(*(char * const *)b);
	int ci_cmp = strcasecmp(left, right);
	if (ci_cmp != 0) {
		return ci_cmp;
	}
	return strcmp(left, right);
}

static int editorFileSearchFilteredIndexCmp(const void *a, const void *b) {
	int left_idx = *(const int *)a;
	int right_idx = *(const int *)b;
	const char *left = editorFileSearchDisplayPath(E.drawer_search_paths[left_idx]);
	const char *right = editorFileSearchDisplayPath(E.drawer_search_paths[right_idx]);
	size_t left_len = strlen(left);
	size_t right_len = strlen(right);
	if (left_len != right_len) {
		return left_len < right_len ? -1 : 1;
	}
	int ci_cmp = strcasecmp(left, right);
	if (ci_cmp != 0) {
		return ci_cmp;
	}
	return strcmp(left, right);
}

static int editorFileSearchEnsureFilteredCapacity(int needed) {
	if (needed <= E.drawer_search_filtered_capacity) {
		return 1;
	}
	int new_capacity =
			E.drawer_search_filtered_capacity > 0 ? E.drawer_search_filtered_capacity * 2 : 64;
	while (new_capacity < needed) {
		if (new_capacity > INT_MAX / 2) {
			return 0;
		}
		new_capacity *= 2;
	}

	size_t cap_size = 0;
	size_t bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(*E.drawer_search_filtered_indices), cap_size, &bytes)) {
		return 0;
	}
	int *indices = editorRealloc(E.drawer_search_filtered_indices, bytes);
	if (indices == NULL) {
		return 0;
	}
	E.drawer_search_filtered_indices = indices;
	E.drawer_search_filtered_capacity = new_capacity;
	return 1;
}

static int editorAsciiCaseContains(const char *haystack, const char *needle) {
	if (needle == NULL || needle[0] == '\0') {
		return 1;
	}
	if (haystack == NULL) {
		return 0;
	}

	size_t needle_len = strlen(needle);
	for (size_t i = 0; haystack[i] != '\0'; i++) {
		size_t j = 0;
		while (j < needle_len && haystack[i + j] != '\0' &&
				tolower((unsigned char)haystack[i + j]) ==
						tolower((unsigned char)needle[j])) {
			j++;
		}
		if (j == needle_len) {
			return 1;
		}
	}
	return 0;
}

static int editorFileSearchRefreshFilter(void) {
	E.drawer_search_filtered_count = 0;
	for (int i = 0; i < E.drawer_search_path_count; i++) {
		const char *display_path = editorFileSearchDisplayPath(E.drawer_search_paths[i]);
		if (!editorAsciiCaseContains(display_path, editorFileSearchQuery())) {
			continue;
		}
		if (!editorFileSearchEnsureFilteredCapacity(E.drawer_search_filtered_count + 1)) {
			return 0;
		}
		E.drawer_search_filtered_indices[E.drawer_search_filtered_count] = i;
		E.drawer_search_filtered_count++;
	}

	if (E.drawer_search_filtered_count > 1) {
		qsort(E.drawer_search_filtered_indices, (size_t)E.drawer_search_filtered_count,
				sizeof(*E.drawer_search_filtered_indices), editorFileSearchFilteredIndexCmp);
	}
	if (E.drawer_selected_index < 1 ||
			E.drawer_selected_index >= editorFileSearchVisibleCount()) {
		E.drawer_selected_index = 1;
	}
	E.drawer_rowoff = 0;
	return 1;
}

static const char *editorFileSearchSelectedPath(void) {
	int result_idx = E.drawer_selected_index - 1;
	if (result_idx < 0 || result_idx >= E.drawer_search_filtered_count) {
		return NULL;
	}
	int path_idx = E.drawer_search_filtered_indices[result_idx];
	if (path_idx < 0 || path_idx >= E.drawer_search_path_count) {
		return NULL;
	}
	return E.drawer_search_paths[path_idx];
}

int editorFileSearchEnter(void) {
	editorFileSearchFree();
	E.drawer_mode = EDITOR_DRAWER_MODE_FILE_SEARCH;
	E.drawer_search_active_tab_before = editorTabActiveIndex();
	E.drawer_search_query = strdup("");
	if (E.drawer_search_query == NULL) {
		editorFileSearchExit(0);
		editorSetAllocFailureStatus();
		return 0;
	}
	E.drawer_search_query_len = 0;

	if (!editorFileSearchEnumerateDir(editorFileSearchRoot())) {
		editorFileSearchExit(0);
		editorSetAllocFailureStatus();
		return 0;
	}
	if (E.drawer_search_path_count > 1) {
		qsort(E.drawer_search_paths, (size_t)E.drawer_search_path_count,
				sizeof(*E.drawer_search_paths), editorFileSearchPathCmp);
	}
	if (!editorFileSearchRefreshFilter()) {
		editorFileSearchExit(0);
		editorSetAllocFailureStatus();
		return 0;
	}

	E.drawer_selected_index = 1;
	E.drawer_rowoff = 0;
	return 1;
}

void editorFileSearchExit(int restore_previous_tab) {
	int previous_tab = E.drawer_search_active_tab_before;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	editorFileSearchFree();
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	if (restore_previous_tab && previous_tab >= 0 && previous_tab < editorTabCount()) {
		(void)editorTabSwitchToIndex(previous_tab);
	}
}

int editorFileSearchAppendByte(int c) {
	if (c < CHAR_MIN || c > CHAR_MAX) {
		return 0;
	}
	unsigned char byte = (unsigned char)c;
	if (byte < 0x80 && iscntrl(byte)) {
		return 0;
	}
	size_t new_len = E.drawer_search_query_len + 1;
	char *query = editorRealloc(E.drawer_search_query, new_len + 1);
	if (query == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	E.drawer_search_query = query;
	E.drawer_search_query[E.drawer_search_query_len] = (char)byte;
	E.drawer_search_query_len = new_len;
	E.drawer_search_query[E.drawer_search_query_len] = '\0';
	if (!editorFileSearchRefreshFilter()) {
		editorSetAllocFailureStatus();
		return 0;
	}
	return 1;
}

int editorFileSearchBackspace(void) {
	if (E.drawer_search_query_len == 0) {
		return 0;
	}

	size_t delete_idx = E.drawer_search_query_len - 1;
	while (delete_idx > 0 &&
			(((unsigned char)E.drawer_search_query[delete_idx] & 0xC0) == 0x80)) {
		delete_idx--;
	}
	E.drawer_search_query[delete_idx] = '\0';
	E.drawer_search_query_len = delete_idx;
	if (!editorFileSearchRefreshFilter()) {
		editorSetAllocFailureStatus();
		return 0;
	}
	return 1;
}

int editorFileSearchVisibleCount(void) {
	return 1 + (E.drawer_search_filtered_count > 0 ? E.drawer_search_filtered_count : 1);
}

int editorFileSearchGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL || visible_idx < 0 || visible_idx >= editorFileSearchVisibleCount()) {
		return 0;
	}

	memset(view_out, 0, sizeof(*view_out));
	view_out->depth = 0;
	view_out->parent_visible_idx = -1;
	view_out->is_last_sibling = 1;
	if (visible_idx == 0) {
		view_out->name = editorFileSearchQuery();
		view_out->is_search_header = 1;
		return 1;
	}

	if (E.drawer_search_filtered_count <= 0) {
		view_out->name = "No matches";
		view_out->is_placeholder = 1;
		return 1;
	}

	int path_idx = E.drawer_search_filtered_indices[visible_idx - 1];
	const char *path = E.drawer_search_paths[path_idx];
	view_out->name = editorFileSearchDisplayPath(path);
	view_out->path = path;
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->is_last_sibling = visible_idx == E.drawer_search_filtered_count;
	view_out->is_active_file = E.filename != NULL && editorPathsReferToSameFile(path, E.filename);
	return 1;
}

void editorFileSearchClampViewport(int viewport_rows) {
	int visible_count = editorFileSearchVisibleCount();
	if (visible_count <= 1) {
		E.drawer_selected_index = 1;
		E.drawer_rowoff = 0;
		return;
	}
	if (E.drawer_selected_index < 1) {
		E.drawer_selected_index = 1;
	}
	if (E.drawer_selected_index >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	}
	if (viewport_rows < 1) {
		viewport_rows = 1;
	}
	if (E.drawer_selected_index < E.drawer_rowoff + 1) {
		E.drawer_rowoff = E.drawer_selected_index - 1;
	} else if (E.drawer_selected_index >= E.drawer_rowoff + viewport_rows) {
		E.drawer_rowoff = E.drawer_selected_index - viewport_rows + 1;
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

int editorFileSearchMoveSelectionBy(int delta, int viewport_rows) {
	int visible_count = editorFileSearchVisibleCount();
	int old_selection = E.drawer_selected_index;
	if (visible_count <= 1) {
		return 0;
	}
	E.drawer_selected_index += delta;
	if (E.drawer_selected_index < 1) {
		E.drawer_selected_index = 1;
	}
	if (E.drawer_selected_index >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	}
	editorFileSearchClampViewport(viewport_rows);
	return E.drawer_selected_index != old_selection;
}

int editorFileSearchSelectVisibleIndex(int visible_idx, int viewport_rows) {
	if (visible_idx <= 0 || visible_idx >= editorFileSearchVisibleCount()) {
		return 0;
	}
	E.drawer_selected_index = visible_idx;
	editorFileSearchClampViewport(viewport_rows);
	return 1;
}

int editorFileSearchSelectedIsDirectory(void) {
	return 0;
}

static int editorFileSearchQueryDisplayCols(void) {
	const char *query = editorFileSearchQuery();
	int query_len = (int)strlen(query);
	int cols = 0;
	for (int idx = 0; idx < query_len;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&query[idx], query_len - idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > query_len - idx) {
			src_len = query_len - idx;
		}
		cols += editorCharDisplayWidth(&query[idx], query_len - idx);
		idx += src_len;
	}
	return cols;
}

int editorFileSearchOpenSelectedFileInTab(void) {
	const char *path = editorFileSearchSelectedPath();
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	char *path_copy = strdup(path);
	if (path_copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorTabOpenOrSwitchToFile(path)) {
		free(path_copy);
		return 0;
	}
	editorFileSearchExit(0);
	(void)editorDrawerRevealPath(path_copy, E.window_rows);
	free(path_copy);
	return 1;
}

int editorFileSearchOpenSelectedFileInPreviewTab(void) {
	const char *path = editorFileSearchSelectedPath();
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToPreviewFile(path);
}

int editorFileSearchPreviewSelection(void) {
	const char *path = editorFileSearchSelectedPath();
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	if (E.drawer_search_previewed_path != NULL &&
			editorPathsReferToSameFile(E.drawer_search_previewed_path, path)) {
		return 1;
	}
	if (!editorTabOpenOrSwitchToPreviewFile(path)) {
		return 0;
	}
	char *copy = strdup(path);
	if (copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	free(E.drawer_search_previewed_path);
	E.drawer_search_previewed_path = copy;
	return 1;
}

int editorFileSearchHeaderCursorCol(int drawer_cols) {
	int col = 6 + editorFileSearchQueryDisplayCols() + 1;
	if (col < 1) {
		col = 1;
	}
	if (col > drawer_cols) {
		col = drawer_cols;
	}
	return col;
}
