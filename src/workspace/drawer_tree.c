#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "workspace/drawer_internal.h"

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
	node->scan_state_known = 0;
	node->scan_dev = 0;
	node->scan_ino = 0;
	node->scan_mtime = (struct timespec){0};
	node->scan_ctime = (struct timespec){0};
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

static int drawerTreeChildIsDir(const char *child_path) {
	struct stat st;
	if (lstat(child_path, &st) != 0) {
		return 0;
	}
	return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
}

static void drawerTreeStoreDirState(struct editorDrawerNode *node, const struct stat *st) {
	node->scan_state_known = 1;
	node->scan_dev = st->st_dev;
	node->scan_ino = st->st_ino;
	node->scan_mtime = st->st_mtim;
	node->scan_ctime = st->st_ctim;
}

/* Records the directory's own identity/mtime so reconciliation can detect when
 * its entry list is unchanged and skip a re-readdir. */
static void drawerTreeCaptureDirState(struct editorDrawerNode *node) {
	struct stat st;
	if (node == NULL) {
		return;
	}
	if (stat(node->path, &st) == 0) {
		drawerTreeStoreDirState(node, &st);
	} else {
		node->scan_state_known = 0;
	}
}

static int drawerTreeTimespecEqual(struct timespec a, struct timespec b) {
	return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
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
	drawerTreeCaptureDirState(node);
	return 1;
}

static int drawerTreeChildrenAppend(struct editorDrawerNode ***children, int *count, int *capacity,
                                    struct editorDrawerNode *child) {
	const size_t child_entry_size = sizeof(struct editorDrawerNode *);
	if (*count >= *capacity) {
		int new_capacity = *capacity > 0 ? *capacity * 2 : 8;
		size_t cap_size = 0;
		size_t bytes = 0;
		if (!editorIntToSize(new_capacity, &cap_size) ||
		    !editorSizeMul(child_entry_size, cap_size, &bytes)) {
			return 0;
		}
		struct editorDrawerNode **grown = editorRealloc(*children, bytes);
		if (grown == NULL) {
			return 0;
		}
		*children = grown;
		*capacity = new_capacity;
	}
	(*children)[(*count)++] = child;
	return 1;
}

/* Re-reads a scanned directory and merges the on-disk entries into its cached
 * children, keeping existing child nodes (and their expansion/subtrees) by name.
 * Consumed slots in the old array are NULLed; survivors are freed as deletions.
 * Returns 1 when the child set changed. */
static int drawerTreeReconcileDir(struct editorDrawerNode *node) {
	DIR *dir = opendir(node->path);
	if (dir == NULL) {
		/* Directory vanished or became unreadable: drop cached children and
		 * force a fresh scan next time it is opened. The parent's own pass
		 * removes this node when the directory itself is gone. */
		if (node->child_count == 0 && node->children == NULL) {
			node->scanned = 0;
			node->scan_state_known = 0;
			return 0;
		}
		for (int i = 0; i < node->child_count; i++) {
			editorDrawerNodeFree(node->children[i]);
		}
		free(node->children);
		node->children = NULL;
		node->child_count = 0;
		node->scanned = 0;
		node->scan_state_known = 0;
		return 1;
	}

	struct editorDrawerNode **old = node->children;
	int old_count = node->child_count;
	struct editorDrawerNode **children = NULL;
	int child_count = 0;
	int child_capacity = 0;
	int changed = 0;
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
		int is_dir = drawerTreeChildIsDir(child_path);

		struct editorDrawerNode *child = NULL;
		for (int i = 0; i < old_count; i++) {
			if (old[i] != NULL && strcmp(old[i]->name, entry->d_name) == 0) {
				if (old[i]->is_dir == is_dir) {
					child = old[i];
				} else {
					/* Path changed kind (file<->dir/symlink): replace. */
					editorDrawerNodeFree(old[i]);
					changed = 1;
				}
				old[i] = NULL;
				break;
			}
		}
		if (child == NULL) {
			child = editorDrawerNodeNew(entry->d_name, child_path, is_dir, node);
			if (child == NULL) {
				free(child_path);
				editorSetAllocFailureStatus();
				break;
			}
			changed = 1;
		}
		free(child_path);

		if (!drawerTreeChildrenAppend(&children, &child_count, &child_capacity, child)) {
			/* Keep the node consistent by not orphaning `child` if it was a
			 * freshly created one; a reused node still lives in `old`-derived
			 * ownership now transferred here, so free it to avoid a leak. */
			editorDrawerNodeFree(child);
			editorSetAllocFailureStatus();
			break;
		}
	}

	(void)closedir(dir);

	/* Anything left in `old` was not seen on disk: it was deleted. */
	for (int i = 0; i < old_count; i++) {
		if (old[i] != NULL) {
			editorDrawerNodeFree(old[i]);
			changed = 1;
		}
	}
	free(old);

	if (child_count > 1) {
		qsort(children, (size_t)child_count, sizeof(struct editorDrawerNode *),
		      drawerTreeNodeCmp);
	}
	node->children = children;
	node->child_count = child_count;
	drawerTreeCaptureDirState(node);
	return changed;
}

int editorDrawerReconcile(struct editorDrawerNode *node) {
	if (node == NULL || !node->is_dir || !node->scanned) {
		return 0;
	}

	int need_merge = 1;
	struct stat st;
	if (node->scan_state_known && stat(node->path, &st) == 0 && st.st_dev == node->scan_dev &&
	    st.st_ino == node->scan_ino && drawerTreeTimespecEqual(st.st_mtim, node->scan_mtime) &&
	    drawerTreeTimespecEqual(st.st_ctim, node->scan_ctime)) {
		need_merge = 0;
	}

	int changed = 0;
	if (need_merge) {
		changed |= drawerTreeReconcileDir(node);
	}

	/* Recurse into scanned subdirectories regardless: a nested directory's
	 * mtime changes independently of its parent's. */
	for (int i = 0; i < node->child_count; i++) {
		struct editorDrawerNode *child = node->children[i];
		if (child->is_dir && child->scanned) {
			changed |= editorDrawerReconcile(child);
		}
	}
	return changed;
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

/* Resolves an absolute path to its node without scanning: only walks already
 * scanned directories, returning NULL if any component is missing. Used to keep
 * a selection anchored across a passive reconcile. */
struct editorDrawerNode *editorDrawerFindNodeByPath(const char *abs_path) {
	if (abs_path == NULL || E.drawer_root == NULL || E.drawer_root_path == NULL) {
		return NULL;
	}
	if (strcmp(abs_path, E.drawer_root_path) == 0) {
		return E.drawer_root;
	}
	size_t root_len = strlen(E.drawer_root_path);
	if (root_len == 0 || strncmp(abs_path, E.drawer_root_path, root_len) != 0 ||
	    abs_path[root_len] != '/') {
		return NULL;
	}
	struct editorDrawerNode *node = E.drawer_root;
	const char *component = abs_path + root_len + 1;
	while (component[0] != '\0') {
		if (!node->is_dir || !node->scanned) {
			return NULL;
		}
		const char *slash = strchr(component, '/');
		size_t comp_len = slash != NULL ? (size_t)(slash - component) : strlen(component);
		if (comp_len == 0) {
			return NULL;
		}
		struct editorDrawerNode *child =
		        editorDrawerFindChildByName(node, component, comp_len);
		if (child == NULL) {
			return NULL;
		}
		node = child;
		if (slash == NULL) {
			break;
		}
		component = slash + 1;
	}
	return node;
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
