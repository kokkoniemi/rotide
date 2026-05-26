#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/file_io.h"
#include "workspace/drawer.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"
#include "rotide.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int drawerFileOpsNameIsValid(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return 0;
	}
	if (strchr(name, '/') != NULL) {
		return 0;
	}
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return 0;
	}
	return 1;
}

static struct editorDrawerNode *drawerFileOpsCreationTargetDir(struct editorDrawerNode *selected) {
	if (selected == NULL) {
		return E.drawer_root;
	}
	if (selected->is_dir) {
		return selected;
	}
	if (selected->parent != NULL) {
		return selected->parent;
	}
	return E.drawer_root;
}

static void drawerFileOpsInvalidateScan(struct editorDrawerNode *node) {
	if (node == NULL) {
		return;
	}
	for (int i = 0; i < node->child_count; i++) {
		editorDrawerNodeFree(node->children[i]);
	}
	free(node->children);
	node->children = NULL;
	node->child_count = 0;
	node->scanned = 0;
	node->scan_error = 0;
}

static int drawerFileOpsSelectChildByName(struct editorDrawerNode *parent, const char *name,
                                          int viewport_rows) {
	if (parent == NULL || name == NULL) {
		return 0;
	}
	struct editorDrawerNode *child = editorDrawerFindChildByName(parent, name, strlen(name));
	if (child == NULL) {
		return 0;
	}
	int visible_idx = -1;
	if (!editorDrawerFindVisibleIndexForNode(child, &visible_idx)) {
		return 0;
	}
	E.drawer_selected_index = visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

static int drawerFileOpsRemovePathRecursive(const char *path) {
	if (path == NULL || path[0] == '\0') {
		errno = EINVAL;
		return 0;
	}

	struct stat st;
	if (lstat(path, &st) != 0) {
		return 0;
	}

	if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
		return unlink(path) == 0;
	}

	DIR *dir = opendir(path);
	if (dir == NULL) {
		return 0;
	}

	int ok = 1;
	struct dirent *entry;
	while (ok && (entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		char *child_path = editorPathJoin(path, entry->d_name);
		if (child_path == NULL) {
			ok = 0;
			errno = ENOMEM;
			break;
		}
		if (!drawerFileOpsRemovePathRecursive(child_path)) {
			ok = 0;
		}
		free(child_path);
	}
	(void)closedir(dir);

	if (ok && rmdir(path) != 0) {
		ok = 0;
	}
	return ok;
}

int editorDrawerCreateFileAtSelection(const char *name, int viewport_rows) {
	if (E.drawer_root == NULL) {
		editorSetStatusMsg("No drawer open");
		return 0;
	}
	if (!drawerFileOpsNameIsValid(name)) {
		editorSetStatusMsg("Invalid file name");
		return 0;
	}

	struct editorDrawerNode *selected = editorDrawerSelectedTreeNode();
	struct editorDrawerNode *target_dir = drawerFileOpsCreationTargetDir(selected);
	if (target_dir == NULL) {
		editorSetStatusMsg("No target directory");
		return 0;
	}

	char *new_path = editorPathJoin(target_dir->path, name);
	if (new_path == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct stat st;
	if (lstat(new_path, &st) == 0) {
		editorSetStatusMsg("'%s' already exists", name);
		free(new_path);
		return 0;
	}

	int fd = open(new_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		editorSetStatusMsg("Create failed: %s", strerror(errno));
		free(new_path);
		return 0;
	}
	(void)close(fd);

	if (target_dir->is_dir) {
		target_dir->is_expanded = 1;
	}
	drawerFileOpsInvalidateScan(target_dir);
	(void)editorDrawerEnsureScanned(target_dir);
	(void)drawerFileOpsSelectChildByName(target_dir, name, viewport_rows);
	editorSetStatusMsg("Created %s", new_path);
	free(new_path);
	return 1;
}

int editorDrawerCreateFolderAtSelection(const char *name, int viewport_rows) {
	if (E.drawer_root == NULL) {
		editorSetStatusMsg("No drawer open");
		return 0;
	}
	if (!drawerFileOpsNameIsValid(name)) {
		editorSetStatusMsg("Invalid folder name");
		return 0;
	}

	struct editorDrawerNode *selected = editorDrawerSelectedTreeNode();
	struct editorDrawerNode *target_dir = drawerFileOpsCreationTargetDir(selected);
	if (target_dir == NULL) {
		editorSetStatusMsg("No target directory");
		return 0;
	}

	char *new_path = editorPathJoin(target_dir->path, name);
	if (new_path == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct stat st;
	if (lstat(new_path, &st) == 0) {
		editorSetStatusMsg("'%s' already exists", name);
		free(new_path);
		return 0;
	}

	if (mkdir(new_path, 0755) != 0) {
		editorSetStatusMsg("Create folder failed: %s", strerror(errno));
		free(new_path);
		return 0;
	}

	if (target_dir->is_dir) {
		target_dir->is_expanded = 1;
	}
	drawerFileOpsInvalidateScan(target_dir);
	(void)editorDrawerEnsureScanned(target_dir);
	(void)drawerFileOpsSelectChildByName(target_dir, name, viewport_rows);
	editorSetStatusMsg("Created %s", new_path);
	free(new_path);
	return 1;
}

int editorDrawerRenameSelection(const char *new_name, int viewport_rows) {
	if (E.drawer_root == NULL) {
		editorSetStatusMsg("No drawer open");
		return 0;
	}
	if (!drawerFileOpsNameIsValid(new_name)) {
		editorSetStatusMsg("Invalid name");
		return 0;
	}

	struct editorDrawerNode *selected = editorDrawerSelectedTreeNode();
	if (selected == NULL || selected == E.drawer_root) {
		editorSetStatusMsg("Select an entry to rename");
		return 0;
	}
	struct editorDrawerNode *parent = selected->parent;
	if (parent == NULL) {
		editorSetStatusMsg("Cannot rename root");
		return 0;
	}

	if (strcmp(selected->name, new_name) == 0) {
		return 0;
	}

	char *new_path = editorPathJoin(parent->path, new_name);
	if (new_path == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct stat st;
	if (lstat(new_path, &st) == 0) {
		editorSetStatusMsg("'%s' already exists", new_name);
		free(new_path);
		return 0;
	}

	if (rename(selected->path, new_path) != 0) {
		editorSetStatusMsg("Rename failed: %s", strerror(errno));
		free(new_path);
		return 0;
	}

	free(new_path);
	drawerFileOpsInvalidateScan(parent);
	(void)editorDrawerEnsureScanned(parent);
	if (!drawerFileOpsSelectChildByName(parent, new_name, viewport_rows)) {
		E.drawer_selected_index = -1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
	}
	editorSetStatusMsg("Renamed to %s", new_name);
	return 1;
}

int editorDrawerDeleteSelection(int viewport_rows) {
	if (E.drawer_root == NULL) {
		editorSetStatusMsg("No drawer open");
		return 0;
	}

	struct editorDrawerNode *selected = editorDrawerSelectedTreeNode();
	if (selected == NULL || selected == E.drawer_root) {
		editorSetStatusMsg("Select an entry to delete");
		return 0;
	}
	struct editorDrawerNode *parent = selected->parent;
	if (parent == NULL) {
		editorSetStatusMsg("Cannot delete root");
		return 0;
	}

	char *path_copy = strdup(selected->path);
	char *name_copy = strdup(selected->name);
	if (path_copy == NULL || name_copy == NULL) {
		free(path_copy);
		free(name_copy);
		editorSetAllocFailureStatus();
		return 0;
	}

	if (!drawerFileOpsRemovePathRecursive(path_copy)) {
		editorSetStatusMsg("Delete failed: %s", strerror(errno));
		free(path_copy);
		free(name_copy);
		return 0;
	}

	drawerFileOpsInvalidateScan(parent);
	(void)editorDrawerEnsureScanned(parent);
	int parent_visible_idx = -1;
	if (parent == E.drawer_root) {
		parent_visible_idx = 0;
	} else {
		(void)editorDrawerFindVisibleIndexForNode(parent, &parent_visible_idx);
	}
	if (parent_visible_idx >= 0) {
		E.drawer_selected_index = parent_visible_idx;
	} else {
		E.drawer_selected_index = 0;
	}
	editorDrawerClampSelectionAndScroll(viewport_rows);
	editorSetStatusMsg("Deleted %s", name_copy);
	free(path_copy);
	free(name_copy);
	return 1;
}

int editorDrawerOpenSelectedFileInTab(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchOpenSelectedFileInTab();
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchOpenSelectedFileInTab();
	}

	struct editorDrawerNode *selected = editorDrawerSelectedTreeNode();
	if (selected == NULL || selected->is_dir || selected->path == NULL ||
	    selected->path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToFile(selected->path);
}

int editorDrawerOpenSelectedFileInPreviewTab(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchOpenSelectedFileInPreviewTab();
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchOpenSelectedFileInPreviewTab();
	}

	struct editorDrawerNode *selected = editorDrawerSelectedTreeNode();
	if (selected == NULL || selected->is_dir || selected->path == NULL ||
	    selected->path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToPreviewFile(selected->path);
}

int editorDrawerRevealPath(const char *path, int viewport_rows) {
	if (path == NULL || path[0] == '\0' || E.drawer_root == NULL ||
	    E.drawer_root_path == NULL) {
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
