#include "editor/file_io.h"

#include "alloc.h"
#include "save_syscalls.h"
#include "size_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *editorPathJoin(const char *left, const char *right) {
	size_t left_len = strlen(left);
	while (left_len > 1 && left[left_len - 1] == '/') {
		left_len--;
	}

	while (right[0] == '/' && right[1] != '\0') {
		right++;
	}
	size_t right_len = strlen(right);

	int need_slash = 1;
	if (left_len == 0 || (left_len == 1 && left[0] == '/')) {
		need_slash = 0;
	}

	size_t total = 0;
	if (!editorSizeAdd(left_len, right_len, &total) ||
			(need_slash && !editorSizeAdd(total, 1, &total)) ||
			!editorSizeAdd(total, 1, &total)) {
		return NULL;
	}

	char *path = editorMalloc(total);
	if (path == NULL) {
		return NULL;
	}

	size_t write_idx = 0;
	if (left_len > 0) {
		memcpy(path, left, left_len);
		write_idx += left_len;
	}
	if (need_slash) {
		path[write_idx++] = '/';
	}
	if (right_len > 0) {
		memcpy(path + write_idx, right, right_len);
		write_idx += right_len;
	}
	path[write_idx] = '\0';
	return path;
}

char *editorPathBasenameDup(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return strdup(".");
	}

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}
	if (len == 1 && path[0] == '/') {
		return strdup("/");
	}

	size_t start = len;
	while (start > 0 && path[start - 1] != '/') {
		start--;
	}

	size_t name_len = len - start;
	char *name = editorMalloc(name_len + 1);
	if (name == NULL) {
		return NULL;
	}
	memcpy(name, path + start, name_len);
	name[name_len] = '\0';
	return name;
}

char *editorPathDirnameDup(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return strdup(".");
	}

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}

	size_t slash = len;
	while (slash > 0 && path[slash - 1] != '/') {
		slash--;
	}
	if (slash == 0) {
		return strdup(".");
	}
	if (slash == 1) {
		return strdup("/");
	}

	size_t dir_len = slash - 1;
	char *dir = editorMalloc(dir_len + 1);
	if (dir == NULL) {
		return NULL;
	}
	memcpy(dir, path, dir_len);
	dir[dir_len] = '\0';
	return dir;
}

char *editorPathGetCwd(void) {
	char *cwd = getcwd(NULL, 0);
	if (cwd != NULL) {
		return cwd;
	}

	return strdup(".");
}

int editorPathsReferToSameFile(const char *left, const char *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}

	struct stat left_st;
	struct stat right_st;
	if (stat(left, &left_st) == 0 && stat(right, &right_st) == 0) {
		return left_st.st_dev == right_st.st_dev && left_st.st_ino == right_st.st_ino;
	}

	return strcmp(left, right) == 0;
}

char *editorTempPathForTarget(const char *target) {
	const char *slash = strrchr(target, '/');
	const char *basename = target;
	size_t dir_len = 0;
	static const char suffix[] = ".rotide-tmp-XXXXXX";

	if (slash != NULL) {
		basename = slash + 1;
		dir_len = (size_t)(slash - target + 1);
	}

	size_t base_len = strlen(basename);
	size_t total_len = 0;
	if (!editorSizeAdd(dir_len, base_len, &total_len) ||
			!editorSizeAdd(total_len, sizeof(suffix), &total_len)) {
		return NULL;
	}
	char *tmp_path = editorMalloc(total_len);
	if (tmp_path == NULL) {
		return NULL;
	}

	if (dir_len > 0) {
		memcpy(tmp_path, target, dir_len);
	}
	memcpy(tmp_path + dir_len, basename, base_len);
	memcpy(tmp_path + dir_len + base_len, suffix, sizeof(suffix));

	return tmp_path;
}

int editorOpenParentDirForTarget(const char *target) {
	const char *slash = strrchr(target, '/');
	if (slash == NULL) {
		return editorSaveOpenDir(".");
	}
	if (slash == target) {
		return editorSaveOpenDir("/");
	}

	size_t dir_len = (size_t)(slash - target);
	size_t dir_cap = 0;
	if (!editorSizeAdd(dir_len, 1, &dir_cap)) {
		errno = ENOMEM;
		return -1;
	}
	char *dir_path = editorMalloc(dir_cap);
	if (dir_path == NULL) {
		errno = ENOMEM;
		return -1;
	}

	memcpy(dir_path, target, dir_len);
	dir_path[dir_len] = '\0';
	int dir_fd = editorSaveOpenDir(dir_path);
	free(dir_path);
	return dir_fd;
}
