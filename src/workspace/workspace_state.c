#include "workspace/workspace_state.h"

#include "rotide.h"
#include "support/file_io.h"
#include "workspace/drawer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t editorWorkspaceStateHashPath(const char *path) {
	uint64_t hash = UINT64_C(1469598103934665603);
	const unsigned char *p = (const unsigned char *)path;
	while (*p != '\0') {
		hash ^= (uint64_t)*p;
		hash *= UINT64_C(1099511628211);
		p++;
	}
	return hash;
}

static int editorWorkspaceStateEnsureDir(const char *path) {
	if (mkdir(path, 0700) == 0) {
		return 1;
	}
	if (errno != EEXIST) {
		return 0;
	}
	struct stat st;
	if (stat(path, &st) == -1) {
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static char *editorWorkspaceStateBuildName(uint64_t hash) {
	char name[128];
	int written = snprintf(name, sizeof(name), "rotide-workspace-u%lu-%016llx.toml",
			(unsigned long)getuid(), (unsigned long long)hash);
	if (written <= 0 || (size_t)written >= sizeof(name)) {
		return NULL;
	}
	char *dup = malloc((size_t)written + 1);
	if (dup == NULL) {
		return NULL;
	}
	memcpy(dup, name, (size_t)written + 1);
	return dup;
}

static char *editorWorkspaceStateResolvePath(void) {
	char *cwd = editorPathGetCwd();
	if (cwd == NULL) {
		return NULL;
	}
	uint64_t hash = editorWorkspaceStateHashPath(cwd);
	free(cwd);

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return NULL;
	}

	char *dot_rotide = editorPathJoin(home, ".rotide");
	if (dot_rotide == NULL) {
		return NULL;
	}
	char *state_dir = editorPathJoin(dot_rotide, "state");
	if (state_dir == NULL) {
		free(dot_rotide);
		return NULL;
	}

	char *path = NULL;
	if (editorWorkspaceStateEnsureDir(dot_rotide) &&
			editorWorkspaceStateEnsureDir(state_dir)) {
		char *name = editorWorkspaceStateBuildName(hash);
		if (name != NULL) {
			path = editorPathJoin(state_dir, name);
			free(name);
		}
	}

	free(dot_rotide);
	free(state_dir);
	return path;
}

int editorWorkspaceStateInitForCurrentDir(void) {
	free(E.workspace_state_path);
	E.workspace_state_path = editorWorkspaceStateResolvePath();
	return E.workspace_state_path != NULL;
}

void editorWorkspaceStateShutdown(void) {
	free(E.workspace_state_path);
	E.workspace_state_path = NULL;
}

const char *editorWorkspaceStatePath(void) {
	return E.workspace_state_path;
}

static enum editorDrawerMode editorWorkspaceStateModeFromString(const char *value) {
	if (strcmp(value, "main_menu") == 0) {
		return EDITOR_DRAWER_MODE_MAIN_MENU;
	}
	if (strcmp(value, "git") == 0) {
		return EDITOR_DRAWER_MODE_GIT;
	}
	return EDITOR_DRAWER_MODE_TREE;
}

static const char *editorWorkspaceStateModeToString(enum editorDrawerMode mode) {
	switch (mode) {
		case EDITOR_DRAWER_MODE_MAIN_MENU:
			return "main_menu";
		case EDITOR_DRAWER_MODE_GIT:
			return "git";
		default:
			return "tree";
	}
}

static int editorWorkspaceStateParseInt(const char *value, int *out) {
	if (value == NULL || value[0] == '\0') {
		return 0;
	}
	char *end = NULL;
	long parsed = strtol(value, &end, 10);
	if (end == value || (end != NULL && *end != '\0')) {
		return 0;
	}
	if (parsed < INT32_MIN || parsed > INT32_MAX) {
		return 0;
	}
	*out = (int)parsed;
	return 1;
}

int editorWorkspaceStateLoadAndApply(int total_cols) {
	if (E.workspace_state_path == NULL) {
		return 0;
	}
	FILE *fp = fopen(E.workspace_state_path, "r");
	if (fp == NULL) {
		return 0;
	}

	int width = -1;
	int width_user_set = -1;
	int collapsed = -1;
	enum editorDrawerMode mode = EDITOR_DRAWER_MODE_TREE;
	int saw_mode = 0;

	char line[256];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
			line[--line_len] = '\0';
		}
		if (line_len == 0 || line[0] == '#') {
			continue;
		}
		char *eq = strchr(line, '=');
		if (eq == NULL) {
			continue;
		}
		*eq = '\0';
		const char *key = line;
		const char *value = eq + 1;
		int parsed = 0;
		if (strcmp(key, "drawer_width_cols") == 0) {
			(void)editorWorkspaceStateParseInt(value, &width);
		} else if (strcmp(key, "drawer_width_user_set") == 0) {
			(void)editorWorkspaceStateParseInt(value, &width_user_set);
		} else if (strcmp(key, "drawer_collapsed") == 0) {
			(void)editorWorkspaceStateParseInt(value, &collapsed);
		} else if (strcmp(key, "drawer_mode") == 0) {
			mode = editorWorkspaceStateModeFromString(value);
			saw_mode = 1;
		}
		(void)parsed;
	}
	fclose(fp);

	if (width > 0 && total_cols > 0) {
		(void)editorDrawerSetWidthForCols(width, total_cols);
		if (width_user_set == 0) {
			E.drawer_width_user_set = 0;
		}
	}
	if (collapsed >= 0) {
		(void)editorDrawerSetCollapsed(collapsed != 0);
	}
	if (saw_mode) {
		E.drawer_mode = mode;
	}
	return 1;
}

int editorWorkspaceStateSave(void) {
	if (E.workspace_state_path == NULL) {
		return 0;
	}

	int fd = open(E.workspace_state_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		return 0;
	}

	enum editorDrawerMode mode = E.drawer_mode;
	if (mode != EDITOR_DRAWER_MODE_TREE && mode != EDITOR_DRAWER_MODE_MAIN_MENU &&
			mode != EDITOR_DRAWER_MODE_GIT) {
		mode = EDITOR_DRAWER_MODE_TREE;
	}

	char buf[256];
	int len = snprintf(buf, sizeof(buf),
			"drawer_width_cols=%d\n"
			"drawer_width_user_set=%d\n"
			"drawer_collapsed=%d\n"
			"drawer_mode=%s\n",
			E.drawer_width_cols,
			E.drawer_width_user_set ? 1 : 0,
			E.drawer_collapsed ? 1 : 0,
			editorWorkspaceStateModeToString(mode));
	if (len <= 0 || (size_t)len >= sizeof(buf)) {
		(void)close(fd);
		return 0;
	}

	const char *p = buf;
	size_t remaining = (size_t)len;
	while (remaining > 0) {
		ssize_t n = write(fd, p, remaining);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(fd);
			return 0;
		}
		p += n;
		remaining -= (size_t)n;
	}
	if (close(fd) != 0) {
		return 0;
	}
	return 1;
}
