#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "workspace/drawer_internal.h"
#include "rotide.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

struct editorDrawerNode *editorDrawerNodeNew(const char *name, const char *path, int is_dir,
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

void editorDrawerNodeFree(struct editorDrawerNode *node) {
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

static int drawerTreeNodeCmp(const void *a, const void *b) {
	const struct editorDrawerNode *left = *(const struct editorDrawerNode *const *)a;
	const struct editorDrawerNode *right = *(const struct editorDrawerNode *const *)b;

	if (left->is_dir != right->is_dir) {
		return right->is_dir - left->is_dir;
	}

	int ci_cmp = strcasecmp(left->name, right->name);
	if (ci_cmp != 0) {
		return ci_cmp;
	}
	return strcmp(left->name, right->name);
}

int editorDrawerEnsureScanned(struct editorDrawerNode *node) {
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
	const size_t child_entry_size = sizeof(children[0]); // NOLINT(bugprone-sizeof-expression)
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

		struct editorDrawerNode *child =
		        editorDrawerNodeNew(entry->d_name, child_path, is_dir, node);
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
			    !editorSizeMul(child_entry_size, cap_size, &bytes)) {
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
		qsort(children, (size_t)child_count, child_entry_size, drawerTreeNodeCmp);
	}

	node->children = children;
	node->child_count = child_count;
	node->scanned = 1;
	return 1;
}

int editorDrawerCountVisibleFromNode(struct editorDrawerNode *node) {
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

static int drawerTreeLookupByVisibleIndexRecursive(struct editorDrawerNode *node, int depth,
                                                   int parent_visible_idx, int *cursor,
                                                   int target_visible_idx,
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
		if (drawerTreeLookupByVisibleIndexRecursive(node->children[i], depth + 1, current,
		                                            cursor, target_visible_idx,
		                                            lookup_out)) {
			return 1;
		}
	}
	return 0;
}

int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out) {
	if (visible_idx < 0 || lookup_out == NULL || E.drawer_root == NULL) {
		return 0;
	}

	int cursor = 0;
	return drawerTreeLookupByVisibleIndexRecursive(E.drawer_root, 0, -1, &cursor, visible_idx,
	                                               lookup_out);
}

struct editorDrawerNode *editorDrawerFindChildByName(struct editorDrawerNode *node,
                                                     const char *name, size_t name_len) {
	if (node == NULL || name == NULL || name_len == 0) {
		return NULL;
	}
	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		struct editorDrawerNode *child = node->children[i];
		if (strlen(child->name) != name_len) {
			continue;
		}
		if (strncmp(child->name, name, name_len) == 0) {
			return child;
		}
	}
	return NULL;
}

static int drawerTreeFindVisibleIndexForNodeRecursive(struct editorDrawerNode *node,
                                                      struct editorDrawerNode *target, int *cursor,
                                                      int *visible_idx_out) {
	if (node == NULL || target == NULL || cursor == NULL || visible_idx_out == NULL) {
		return 0;
	}

	if (node == target) {
		*visible_idx_out = *cursor;
		return 1;
	}

	(*cursor)++;
	if (!node->is_dir || !node->is_expanded) {
		return 0;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		if (drawerTreeFindVisibleIndexForNodeRecursive(node->children[i], target, cursor,
		                                               visible_idx_out)) {
			return 1;
		}
	}
	return 0;
}

int editorDrawerFindVisibleIndexForNode(struct editorDrawerNode *target, int *visible_idx_out) {
	int cursor = 0;
	return drawerTreeFindVisibleIndexForNodeRecursive(E.drawer_root, target, &cursor,
	                                                  visible_idx_out);
}
