#include "workspace/workspace_state.h"

#include "config/dap_config.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "language/lsp.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "terminal/terminal_pane.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ROTIDE_WORKSPACE_RECENT_FILE_LIMIT 64
#define ROTIDE_WORKSPACE_PENDING_TAB_LIMIT 256
#define ROTIDE_WORKSPACE_PENDING_PANE_TAB_LIMIT 512

/* Bumped on incompatible file-format changes so an older binary reading a
 * newer state file can refuse rather than silently misinterpret unknown
 * keys. Unversioned files (pre-versioning) are treated as version 1. */
#define ROTIDE_WORKSPACE_STATE_VERSION 2

struct workspaceStatePendingTab {
	char *path;
	int cx;
	int cy;
};

struct workspaceStatePendingPaneTab {
	int pane_idx;
	int is_active;
	char *path;
};

static struct workspaceStatePendingTab *g_pending_tabs;
static int g_pending_tab_count;
static int g_pending_tab_capacity;
static struct workspaceStatePendingPaneTab *g_pending_pane_tabs;
static int g_pending_pane_tab_count;
static int g_pending_pane_tab_capacity;
static int g_pending_focused_pane = -1;

static void workspaceStateFreeRecentFiles(void);
static void workspaceStateFreePendingTabs(void);
static void workspaceStateFreePendingPaneTabs(void);

static void *workspaceStateEnsureArrayCapacity(void *items, int needed, int *capacity,
                                               size_t elem_size, int limit, int initial_capacity) {
	if (needed <= *capacity) {
		return items;
	}
	if (needed > limit || elem_size == 0 || initial_capacity <= 0) {
		return NULL;
	}

	int new_capacity = initial_capacity;
	if (*capacity > 0) {
		if (*capacity > INT32_MAX / 2) {
			return NULL;
		}
		new_capacity = *capacity * 2;
	}
	while (new_capacity < needed && new_capacity < limit) {
		if (new_capacity > INT32_MAX / 2) {
			return NULL;
		}
		new_capacity *= 2;
	}
	if (new_capacity > limit) {
		new_capacity = limit;
	}
	if (needed > new_capacity || (size_t)new_capacity > SIZE_MAX / elem_size) {
		return NULL;
	}

	void *grown = editorRealloc(items, elem_size * (size_t)new_capacity);
	if (grown == NULL) {
		return NULL;
	}
	*capacity = new_capacity;
	return grown;
}

static uint64_t workspaceStateHashPath(const char *path) {
	uint64_t hash = UINT64_C(1469598103934665603);
	const unsigned char *p = (const unsigned char *)path;
	while (*p != '\0') {
		hash ^= (uint64_t)*p;
		hash *= UINT64_C(1099511628211);
		p++;
	}
	return hash;
}

static int workspaceStateEnsureDir(const char *path) {
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

static char *workspaceStateBuildName(uint64_t hash) {
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

static char *workspaceStateResolvePath(void) {
	char *cwd = editorPathCwdDup();
	if (cwd == NULL) {
		return NULL;
	}
	uint64_t hash = workspaceStateHashPath(cwd);
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
	if (workspaceStateEnsureDir(dot_rotide) && workspaceStateEnsureDir(state_dir)) {
		char *name = workspaceStateBuildName(hash);
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
	workspaceStateFreeRecentFiles();
	workspaceStateFreePendingTabs();
	workspaceStateFreePendingPaneTabs();
	E.workspace_state_path = workspaceStateResolvePath();
	return E.workspace_state_path != NULL;
}

void editorWorkspaceStateShutdown(void) {
	free(E.workspace_state_path);
	E.workspace_state_path = NULL;
	workspaceStateFreeRecentFiles();
	workspaceStateFreePendingTabs();
	workspaceStateFreePendingPaneTabs();
}

const char *editorWorkspaceStatePath(void) {
	return E.workspace_state_path;
}

static void workspaceStateFreeRecentFiles(void) {
	for (int i = 0; i < E.recent_file_count; i++) {
		free(E.recent_file_paths[i]);
	}
	free(E.recent_file_paths);
	E.recent_file_paths = NULL;
	E.recent_file_count = 0;
	E.recent_file_capacity = 0;
}

static void workspaceStateFreePendingTabs(void) {
	for (int i = 0; i < g_pending_tab_count; i++) {
		free(g_pending_tabs[i].path);
	}
	free(g_pending_tabs);
	g_pending_tabs = NULL;
	g_pending_tab_count = 0;
	g_pending_tab_capacity = 0;
}

static void workspaceStateFreePendingPaneTabs(void) {
	for (int i = 0; i < g_pending_pane_tab_count; i++) {
		free(g_pending_pane_tabs[i].path);
	}
	free(g_pending_pane_tabs);
	g_pending_pane_tabs = NULL;
	g_pending_pane_tab_count = 0;
	g_pending_pane_tab_capacity = 0;
	g_pending_focused_pane = -1;
}

static int workspaceStateAppendPendingPaneTab(int pane_idx, int is_active, const char *path) {
	if (pane_idx < 0 || path == NULL || path[0] == '\0') {
		return 0;
	}
	if (g_pending_pane_tab_count >= ROTIDE_WORKSPACE_PENDING_PANE_TAB_LIMIT) {
		return 0;
	}
	struct workspaceStatePendingPaneTab *grown = workspaceStateEnsureArrayCapacity(
	        g_pending_pane_tabs, g_pending_pane_tab_count + 1, &g_pending_pane_tab_capacity,
	        sizeof(*g_pending_pane_tabs), ROTIDE_WORKSPACE_PENDING_PANE_TAB_LIMIT, 16);
	if (grown == NULL) {
		return 0;
	}
	g_pending_pane_tabs = grown;
	char *copy = strdup(path);
	if (copy == NULL) {
		return 0;
	}
	g_pending_pane_tabs[g_pending_pane_tab_count].pane_idx = pane_idx;
	g_pending_pane_tabs[g_pending_pane_tab_count].is_active = is_active ? 1 : 0;
	g_pending_pane_tabs[g_pending_pane_tab_count].path = copy;
	g_pending_pane_tab_count++;
	return 1;
}

static int workspaceStateAppendPendingTab(int cx, int cy, const char *path) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	if (g_pending_tab_count >= ROTIDE_WORKSPACE_PENDING_TAB_LIMIT) {
		return 0;
	}
	struct workspaceStatePendingTab *grown = workspaceStateEnsureArrayCapacity(
	        g_pending_tabs, g_pending_tab_count + 1, &g_pending_tab_capacity,
	        sizeof(*g_pending_tabs), ROTIDE_WORKSPACE_PENDING_TAB_LIMIT, 8);
	if (grown == NULL) {
		return 0;
	}
	g_pending_tabs = grown;
	char *copy = strdup(path);
	if (copy == NULL) {
		return 0;
	}
	g_pending_tabs[g_pending_tab_count].path = copy;
	g_pending_tabs[g_pending_tab_count].cx = cx < 0 ? 0 : cx;
	g_pending_tabs[g_pending_tab_count].cy = cy < 0 ? 0 : cy;
	g_pending_tab_count++;
	return 1;
}

static int workspaceStateEnsureRecentFileCapacity(int needed) {
	char **paths = workspaceStateEnsureArrayCapacity(
	        E.recent_file_paths, needed, &E.recent_file_capacity, sizeof(*E.recent_file_paths),
	        ROTIDE_WORKSPACE_RECENT_FILE_LIMIT, 16);
	if (paths == NULL) {
		return 0;
	}
	E.recent_file_paths = paths;
	return 1;
}

static int workspaceStatePathCanWriteLine(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	for (const char *p = path; *p != '\0'; p++) {
		if (*p == '\n' || *p == '\r') {
			return 0;
		}
	}
	return 1;
}

static int workspaceStateRecentFileIndex(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	for (int i = 0; i < E.recent_file_count; i++) {
		if (strcmp(E.recent_file_paths[i], path) == 0) {
			return i;
		}
	}
	return -1;
}

int editorWorkspaceStateRecentFileRank(const char *path) {
	return workspaceStateRecentFileIndex(path);
}

static int workspaceStateAppendRecentFile(const char *path) {
	if (!workspaceStatePathCanWriteLine(path)) {
		return 1;
	}
	if (workspaceStateRecentFileIndex(path) >= 0) {
		return 1;
	}
	if (E.recent_file_count >= ROTIDE_WORKSPACE_RECENT_FILE_LIMIT) {
		return 1;
	}
	if (!workspaceStateEnsureRecentFileCapacity(E.recent_file_count + 1)) {
		return 0;
	}
	char *copy = strdup(path);
	if (copy == NULL) {
		return 0;
	}
	E.recent_file_paths[E.recent_file_count] = copy;
	E.recent_file_count++;
	return 1;
}

int editorWorkspaceStateRememberRecentFile(const char *path) {
	if (!workspaceStatePathCanWriteLine(path)) {
		return 0;
	}
	char *absolute = editorPathAbsoluteDup(path);
	if (absolute == NULL) {
		return 0;
	}

	int existing_idx = workspaceStateRecentFileIndex(absolute);
	if (existing_idx == 0) {
		free(absolute);
		return 1;
	}
	if (existing_idx > 0) {
		free(E.recent_file_paths[existing_idx]);
		memmove(&E.recent_file_paths[existing_idx], &E.recent_file_paths[existing_idx + 1],
		        sizeof(*E.recent_file_paths) *
		                (size_t)(E.recent_file_count - existing_idx - 1));
		E.recent_file_count--;
	}
	if (E.recent_file_count >= ROTIDE_WORKSPACE_RECENT_FILE_LIMIT) {
		free(E.recent_file_paths[E.recent_file_count - 1]);
		E.recent_file_count--;
	}
	if (!workspaceStateEnsureRecentFileCapacity(E.recent_file_count + 1)) {
		free(absolute);
		return 0;
	}
	memmove(&E.recent_file_paths[1], &E.recent_file_paths[0],
	        sizeof(*E.recent_file_paths) * (size_t)E.recent_file_count);
	E.recent_file_paths[0] = absolute;
	E.recent_file_count++;
	return 1;
}

static enum editorDrawerMode workspaceStateModeFromString(const char *value) {
	if (strcmp(value, "main_menu") == 0) {
		return EDITOR_DRAWER_MODE_MAIN_MENU;
	}
	if (strcmp(value, "git") == 0) {
		return EDITOR_DRAWER_MODE_GIT;
	}
	if (strcmp(value, "lsp") == 0) {
		return EDITOR_DRAWER_MODE_LSP;
	}
	if (strcmp(value, "dap") == 0) {
		return EDITOR_DRAWER_MODE_DAP;
	}
	return EDITOR_DRAWER_MODE_TREE;
}

static const char *workspaceStateModeToString(enum editorDrawerMode mode) {
	switch (mode) {
		case EDITOR_DRAWER_MODE_MAIN_MENU:
			return "main_menu";
		case EDITOR_DRAWER_MODE_GIT:
			return "git";
		case EDITOR_DRAWER_MODE_LSP:
			return "lsp";
		case EDITOR_DRAWER_MODE_DAP:
			return "dap";
		default:
			return "tree";
	}
}

static int workspaceStateParseInt(const char *value, int *out) {
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

static int workspaceStateParsePaneTabLine(const char *value, int *pane_idx_out, int *is_active_out,
                                          const char **path_out) {
	if (value == NULL || pane_idx_out == NULL || is_active_out == NULL || path_out == NULL) {
		return 0;
	}
	const char *first = strchr(value, '|');
	if (first == NULL) {
		return 0;
	}
	const char *second = strchr(first + 1, '|');
	if (second == NULL) {
		return 0;
	}
	char pane_buf[32];
	char active_buf[32];
	size_t pane_len = (size_t)(first - value);
	size_t active_len = (size_t)(second - first - 1);
	if (pane_len >= sizeof(pane_buf) || active_len >= sizeof(active_buf)) {
		return 0;
	}
	memcpy(pane_buf, value, pane_len);
	pane_buf[pane_len] = '\0';
	memcpy(active_buf, first + 1, active_len);
	active_buf[active_len] = '\0';
	int pane_idx = 0;
	int is_active = 0;
	if (!workspaceStateParseInt(pane_buf, &pane_idx) ||
	    !workspaceStateParseInt(active_buf, &is_active)) {
		return 0;
	}
	if (pane_idx < 0) {
		return 0;
	}
	*pane_idx_out = pane_idx;
	*is_active_out = is_active ? 1 : 0;
	*path_out = second + 1;
	return 1;
}

static int workspaceStateParseTabLine(const char *value, int *cx_out, int *cy_out,
                                      const char **path_out) {
	if (value == NULL || cx_out == NULL || cy_out == NULL || path_out == NULL) {
		return 0;
	}
	const char *first = strchr(value, '|');
	if (first == NULL) {
		return 0;
	}
	const char *second = strchr(first + 1, '|');
	if (second == NULL) {
		return 0;
	}
	char cx_buf[32];
	char cy_buf[32];
	size_t cx_len = (size_t)(first - value);
	size_t cy_len = (size_t)(second - first - 1);
	if (cx_len >= sizeof(cx_buf) || cy_len >= sizeof(cy_buf)) {
		return 0;
	}
	memcpy(cx_buf, value, cx_len);
	cx_buf[cx_len] = '\0';
	memcpy(cy_buf, first + 1, cy_len);
	cy_buf[cy_len] = '\0';
	int cx = 0;
	int cy = 0;
	if (!workspaceStateParseInt(cx_buf, &cx) || !workspaceStateParseInt(cy_buf, &cy)) {
		return 0;
	}
	*cx_out = cx;
	*cy_out = cy;
	*path_out = second + 1;
	return 1;
}

struct workspaceStateParsedSettings {
	int width;
	int width_user_set;
	int collapsed;
	enum editorDrawerMode mode;
	int saw_mode;
	int menu_expanded;
	int git_expanded;
	int lsp_expanded;
	int dap_expanded;
};

static void workspaceStateInitParsedSettings(struct workspaceStateParsedSettings *s) {
	s->width = -1;
	s->width_user_set = -1;
	s->collapsed = -1;
	s->mode = EDITOR_DRAWER_MODE_TREE;
	s->saw_mode = 0;
	s->menu_expanded = -1;
	s->git_expanded = -1;
	s->lsp_expanded = -1;
	s->dap_expanded = -1;
}

static void workspaceStateLoadLayoutLine(const char *value) {
	struct editorPaneNode *restored = editorLayoutDeserialize(value);
	if (restored == NULL) {
		return;
	}
	editorPaneNodeFree(E.layout_root);
	E.layout_root = restored;
	/* Prefer the first editor leaf so the pre-hydrate file-open loop doesn't park
	 * tabs in a terminal placeholder, and we don't capture editor cursor state
	 * into a terminal leaf's view. Fall back to any leaf for a terminal-only
	 * workspace. */
	E.focused_leaf = editorPaneNodeFirstLeafOfKind(E.layout_root, EDITOR_PANE_KIND_EDITOR);
	if (E.focused_leaf == NULL) {
		E.focused_leaf = editorPaneNodeFirstLeaf(E.layout_root);
	}
	if (E.focused_leaf != NULL && E.focused_leaf->as.leaf.kind == EDITOR_PANE_KIND_EDITOR) {
		editorPaneViewCaptureFromState(&E.focused_leaf->as.leaf.view);
	}
}

static void workspaceStateLoadTabLine(const char *value) {
	int tab_cx = 0;
	int tab_cy = 0;
	const char *tab_path = NULL;
	if (workspaceStateParseTabLine(value, &tab_cx, &tab_cy, &tab_path)) {
		(void)workspaceStateAppendPendingTab(tab_cx, tab_cy, tab_path);
	}
}

static void workspaceStateLoadPaneTabLine(const char *value) {
	int pane_idx = -1;
	int is_active = 0;
	const char *pane_path = NULL;
	if (workspaceStateParsePaneTabLine(value, &pane_idx, &is_active, &pane_path)) {
		(void)workspaceStateAppendPendingPaneTab(pane_idx, is_active, pane_path);
	}
}

static void workspaceStateLoadFocusedPaneLine(const char *value) {
	int focused = -1;
	if (workspaceStateParseInt(value, &focused) && focused >= 0) {
		g_pending_focused_pane = focused;
	}
}

/* Returns 1 normally, 0 to abort the load (newer-format file). */
static int workspaceStateProcessLine(const char *key, const char *value, int reset_panes,
                                     struct workspaceStateParsedSettings *s) {
	if (strcmp(key, "version") == 0) {
		int parsed_version = 0;
		if (workspaceStateParseInt(value, &parsed_version) &&
		    parsed_version > ROTIDE_WORKSPACE_STATE_VERSION) {
			return 0;
		}
		return 1;
	}
	if (strcmp(key, "drawer_width_cols") == 0) {
		(void)workspaceStateParseInt(value, &s->width);
	} else if (strcmp(key, "drawer_width_user_set") == 0) {
		(void)workspaceStateParseInt(value, &s->width_user_set);
	} else if (strcmp(key, "drawer_collapsed") == 0) {
		(void)workspaceStateParseInt(value, &s->collapsed);
	} else if (strcmp(key, "drawer_mode") == 0) {
		s->mode = workspaceStateModeFromString(value);
		s->saw_mode = 1;
	} else if (strcmp(key, "drawer_menu_expanded") == 0) {
		(void)workspaceStateParseInt(value, &s->menu_expanded);
	} else if (strcmp(key, "drawer_git_expanded") == 0) {
		(void)workspaceStateParseInt(value, &s->git_expanded);
	} else if (strcmp(key, "drawer_lsp_expanded") == 0) {
		(void)workspaceStateParseInt(value, &s->lsp_expanded);
	} else if (strcmp(key, "drawer_dap_expanded") == 0) {
		(void)workspaceStateParseInt(value, &s->dap_expanded);
	} else if (strcmp(key, "recent_file") == 0) {
		(void)workspaceStateAppendRecentFile(value);
	} else if (!reset_panes) {
		if (strcmp(key, "tab") == 0) {
			workspaceStateLoadTabLine(value);
		} else if (strcmp(key, "pane_tab") == 0) {
			workspaceStateLoadPaneTabLine(value);
		} else if (strcmp(key, "focused_pane") == 0) {
			workspaceStateLoadFocusedPaneLine(value);
		} else if (strcmp(key, "layout") == 0) {
			workspaceStateLoadLayoutLine(value);
		}
	}
	return 1;
}

/* Splits a single line (with trailing newline stripped) into key/value at '='.
 * Returns 1 on success; 0 if the line is blank, a comment, or has no '='. */
static int workspaceStateSplitKeyValue(char *line, const char **key_out, const char **value_out) {
	size_t line_len = strlen(line);
	while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
		line[--line_len] = '\0';
	}
	if (line_len == 0 || line[0] == '#') {
		return 0;
	}
	char *eq = strchr(line, '=');
	if (eq == NULL) {
		return 0;
	}
	*eq = '\0';
	*key_out = line;
	*value_out = eq + 1;
	return 1;
}

static void workspaceStateApplyParsedSettings(const struct workspaceStateParsedSettings *s,
                                              int total_cols) {
	if (s->width > 0 && total_cols > 0) {
		(void)editorDrawerSetWidthForCols(s->width, total_cols);
		if (s->width_user_set == 0) {
			E.drawer_width_user_set = 0;
		}
	}
	if (s->collapsed >= 0) {
		(void)editorDrawerSetCollapsed(s->collapsed != 0);
	}
	if (s->saw_mode) {
		E.drawer_mode = s->mode;
	}
	if (s->menu_expanded >= 0) {
		E.drawer_menu_expanded = (unsigned int)s->menu_expanded;
	}
	if (s->git_expanded >= 0) {
		E.drawer_git_expanded = (unsigned int)s->git_expanded;
	}
	if (s->lsp_expanded >= 0) {
		E.drawer_lsp_expanded = (unsigned int)s->lsp_expanded;
	}
	if (s->dap_expanded >= 0) {
		E.drawer_dap_expanded = (unsigned int)s->dap_expanded;
	}
}

int editorWorkspaceStateLoadAndApply(int total_cols, int reset_panes) {
	if (E.workspace_state_path == NULL) {
		return 0;
	}
	FILE *fp = fopen(E.workspace_state_path, "r");
	if (fp == NULL) {
		return 0;
	}

	struct workspaceStateParsedSettings settings;
	workspaceStateInitParsedSettings(&settings);
	workspaceStateFreePendingTabs();
	workspaceStateFreePendingPaneTabs();

	char line[4096];
	while (fgets(line, sizeof(line), fp) != NULL) {
		const char *key = NULL;
		const char *value = NULL;
		if (!workspaceStateSplitKeyValue(line, &key, &value)) {
			continue;
		}
		if (!workspaceStateProcessLine(key, value, reset_panes, &settings)) {
			(void)fclose(fp);
			workspaceStateFreePendingTabs();
			workspaceStateFreePendingPaneTabs();
			return 0;
		}
	}
	(void)fclose(fp);

	workspaceStateApplyParsedSettings(&settings, total_cols);
	return 1;
}

#define ROTIDE_WORKSPACE_MAX_LEAVES 128

static int workspaceStateCollectLeavesRecursive(struct editorPaneNode *node,
                                                struct editorPaneNode **out, int *count,
                                                int max_count) {
	if (node == NULL) {
		return 1;
	}
	if (node->is_split) {
		if (!workspaceStateCollectLeavesRecursive(node->as.split.first, out, count,
		                                          max_count)) {
			return 0;
		}
		return workspaceStateCollectLeavesRecursive(node->as.split.second, out, count,
		                                            max_count);
	}
	if (*count >= max_count) {
		return 0;
	}
	out[*count] = node;
	(*count)++;
	return 1;
}

static int workspaceStateCollectLeaves(struct editorPaneNode *root, struct editorPaneNode **out,
                                       int max_count) {
	int count = 0;
	if (!workspaceStateCollectLeavesRecursive(root, out, &count, max_count)) {
		return -1;
	}
	return count;
}

static int workspaceStateFindTabIndexByPath(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	for (int i = 0; i < E.tab_count; i++) {
		const char *name = editorTabFilenameAt(i);
		if (name != NULL && strcmp(name, path) == 0) {
			return i;
		}
	}
	return -1;
}

static void workspaceStateApplyPendingPaneAssignment(void) {
	if (g_pending_pane_tab_count <= 0 || E.layout_root == NULL) {
		return;
	}
	struct editorPaneNode *leaves[ROTIDE_WORKSPACE_MAX_LEAVES];
	int leaf_count =
	        workspaceStateCollectLeaves(E.layout_root, leaves, ROTIDE_WORKSPACE_MAX_LEAVES);
	if (leaf_count <= 0) {
		return;
	}

	/* Wipe whatever the open loop dropped into the first leaf so each pane
	 * starts from a clean slate and we can repopulate per the saved data. */
	for (int i = 0; i < leaf_count; i++) {
		if (leaves[i]->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
			continue;
		}
		editorPaneViewClearTabs(&leaves[i]->as.leaf.view);
	}

	for (int j = 0; j < g_pending_pane_tab_count; j++) {
		const struct workspaceStatePendingPaneTab *p = &g_pending_pane_tabs[j];
		if (p->pane_idx >= leaf_count) {
			continue;
		}
		struct editorPaneNode *leaf = leaves[p->pane_idx];
		if (leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
			continue;
		}
		int tab_idx = workspaceStateFindTabIndexByPath(p->path);
		if (tab_idx < 0) {
			continue;
		}
		if (!editorPaneViewAddTab(&leaf->as.leaf.view, tab_idx)) {
			continue;
		}
		if (p->is_active) {
			(void)editorPaneViewActivateTab(&leaf->as.leaf.view, tab_idx);
		}
	}

	for (int i = 0; i < leaf_count; i++) {
		struct editorPaneView *v = &leaves[i]->as.leaf.view;
		if (v->active_tab_idx < 0 && v->pane_tab_count > 0) {
			(void)editorPaneViewActivateTab(v, v->pane_tabs[0]);
		}
	}

	/* Visit each pane to copy its active tab's cursor/scroll into the view, so
	 * later focus changes that call editorPaneViewLoadIntoState don't reset the
	 * cursor to (0, 0). Terminal leaves carry no editor-tab state, so leave
	 * them alone — driving the global tab switch from a terminal leaf would
	 * desync E.active_tab. */
	for (int i = 0; i < leaf_count; i++) {
		if (leaves[i]->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
			continue;
		}
		struct editorPaneView *v = &leaves[i]->as.leaf.view;
		if (v->active_tab_idx < 0) {
			continue;
		}
		E.focused_leaf = leaves[i];
		(void)editorTabSwitchToIndex(v->active_tab_idx);
		editorPaneViewCaptureFromState(v);
	}

	int focused = g_pending_focused_pane;
	if (focused < 0 || focused >= leaf_count) {
		focused = 0;
	}
	struct editorPaneNode *target = leaves[focused];
	E.focused_leaf = target;
	if (target->as.leaf.kind == EDITOR_PANE_KIND_EDITOR &&
	    target->as.leaf.view.active_tab_idx >= 0) {
		(void)editorTabSwitchToIndex(target->as.leaf.view.active_tab_idx);
	}
}

static void workspaceStateHydrateTerminalsIfAny(void) {
	if (E.layout_root == NULL) {
		return;
	}
	const char *shell = getenv("SHELL");
	if (shell == NULL || shell[0] == '\0') {
		shell = "/bin/sh";
	}
	/* HydratePlaceholders walks the tree for `term` placeholder leaves and is a
	 * no-op when there are none, so no separate gate is needed. */
	int failures = editorTerminalPaneHydratePlaceholders(E.layout_root, shell);
	/* Resize here is safe to run before pane-tab assignment because
	 * assignment doesn't change leaf geometry — only pane-membership
	 * and active_tab_idx. If that ever changes, this needs to move
	 * after the assignment pass. */
	editorTerminalPaneResizeAllToLayout(E.layout_root);
	if (failures > 0) {
		editorSetStatusMsg("Failed to restore %d terminal pane(s)", failures);
	}
}

int editorWorkspaceStateRestoreTabs(void) {
	if (g_pending_tab_count <= 0) {
		/* Symmetric cleanup so pending pane-tab state doesn't linger to the
		 * next session shutdown when no tabs were queued for restore. */
		workspaceStateHydrateTerminalsIfAny();
		workspaceStateFreePendingPaneTabs();
		return 0;
	}
	int opened_any = 0;
	/*
	 * Restoring a workspace with several tabs would otherwise pay the cost of a synchronous
	 * LSP initialize round-trip per language. Skip LSP for every tab during this loop;
	 * the active tab below and any later tab switch will trigger didOpen lazily.
	 */
	editorOpenSetDeferLsp(1);
	for (int i = 0; i < g_pending_tab_count; i++) {
		const struct workspaceStatePendingTab *pending = &g_pending_tabs[i];
		if (!editorTabOpenOrSwitchToFile(pending->path)) {
			continue;
		}
		opened_any = 1;
		if (E.numrows <= 0) {
			continue;
		}
		int target_cy = pending->cy;
		if (target_cy < 0) {
			target_cy = 0;
		}
		if (target_cy >= E.numrows) {
			target_cy = E.numrows - 1;
		}
		int target_cx = pending->cx;
		if (target_cx < 0) {
			target_cx = 0;
		}
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, target_cy, &line)) {
			if (target_cx > line.size) {
				target_cx = line.size;
			}
			target_cx = editorBytesClampCxToClusterBoundary(line.data, line.size,
			                                                target_cx);
			if (target_cx < 0) {
				target_cx = 0;
			}
			if (target_cx > line.size) {
				target_cx = line.size;
			}
			editorLineViewRelease(&line);
		}
		size_t target_offset = 0;
		if (editorBufferPosToOffset(target_cy, target_cx, &target_offset)) {
			(void)editorSyncCursorFromOffset(target_offset);
		}
		editorViewportCenterCursor();
	}
	editorOpenSetDeferLsp(0);
	if (opened_any && g_pending_pane_tab_count > 0) {
		/* Redistribute the opened tabs across the saved panes; this also
		 * sets E.focused_leaf and switches to the focused pane's active tab. */
		workspaceStateApplyPendingPaneAssignment();
	}
	/* Hydrate AFTER pane assignment: assignment wipes and repopulates each
	 * editor leaf's membership, so a terminal placeholder must still be a
	 * TERMINAL leaf during assignment (it skips non-editor leaves). Hydration
	 * then demotes the placeholder to an editor leaf hosting a TERMINAL tab. */
	workspaceStateHydrateTerminalsIfAny();
	/*
	 * When the active path is the last tab we opened, editorTabSwitchToIndex returns early
	 * without calling editorLoadActiveTab, so the LSP didOpen never runs. Force it here.
	 */
	if (opened_any) {
		editorLspEnsureActiveDocumentTracked();
	}
	/* Layout deserialize restores tree structure but not pane membership;
	 * the loop above only populated the first leaf. Backfill the rest. */
	editorTabsEnsurePaneOccupancy();
	/*
	 * Document symbols are only refreshed on explicit LSP-drawer activation. If the drawer
	 * was left in LSP mode across restarts, populate them now.
	 */
	if (opened_any && E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		editorLspRefreshActiveDocumentSymbols();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		(void)editorDapConfigReloadProject(E.drawer_root_path);
	}
	workspaceStateFreePendingTabs();
	workspaceStateFreePendingPaneTabs();
	return opened_any;
}

static int workspaceStateWriteAll(int fd, const char *buf, size_t len) {
	const char *p = buf;
	size_t remaining = len;
	while (remaining > 0) {
		ssize_t n = write(fd, p, remaining);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		p += n;
		remaining -= (size_t)n;
	}
	return 1;
}

static enum editorDrawerMode workspaceStateNormalizedSaveMode(void) {
	enum editorDrawerMode mode = E.drawer_mode;
	if (mode != EDITOR_DRAWER_MODE_TREE && mode != EDITOR_DRAWER_MODE_MAIN_MENU &&
	    mode != EDITOR_DRAWER_MODE_GIT && mode != EDITOR_DRAWER_MODE_LSP &&
	    mode != EDITOR_DRAWER_MODE_DAP) {
		mode = EDITOR_DRAWER_MODE_TREE;
	}
	return mode;
}

static int workspaceStateWriteHeader(int fd) {
	enum editorDrawerMode mode = workspaceStateNormalizedSaveMode();
	char buf[256];
	int len = snprintf(buf, sizeof(buf),
	                   "version=%d\n"
	                   "drawer_width_cols=%d\n"
	                   "drawer_width_user_set=%d\n"
	                   "drawer_collapsed=%d\n"
	                   "drawer_mode=%s\n"
	                   "drawer_menu_expanded=%u\n"
	                   "drawer_git_expanded=%u\n"
	                   "drawer_lsp_expanded=%u\n"
	                   "drawer_dap_expanded=%u\n",
	                   ROTIDE_WORKSPACE_STATE_VERSION, E.drawer_width_cols,
	                   E.drawer_width_user_set ? 1 : 0, E.drawer_collapsed ? 1 : 0,
	                   workspaceStateModeToString(mode), E.drawer_menu_expanded,
	                   E.drawer_git_expanded, E.drawer_lsp_expanded, E.drawer_dap_expanded);
	if (len <= 0 || (size_t)len >= sizeof(buf)) {
		return 0;
	}
	return workspaceStateWriteAll(fd, buf, (size_t)len);
}

static int workspaceStateWriteRecentFiles(int fd) {
	for (int i = 0; i < E.recent_file_count; i++) {
		const char *path = E.recent_file_paths[i];
		if (!workspaceStatePathCanWriteLine(path)) {
			continue;
		}
		if (!workspaceStateWriteAll(fd, "recent_file=", strlen("recent_file=")) ||
		    !workspaceStateWriteAll(fd, path, strlen(path)) ||
		    !workspaceStateWriteAll(fd, "\n", 1)) {
			return 0;
		}
	}
	return 1;
}

static int workspaceStateWriteTabs(int fd) {
	for (int i = 0; i < E.tab_count; i++) {
		const struct editorBuffer *tab = editorTabBufferHandleAt(i);
		if (tab == NULL || tab->tab_kind != EDITOR_TAB_FILE || tab->is_preview ||
		    !workspaceStatePathCanWriteLine(tab->filename)) {
			continue;
		}
		char prefix[64];
		int prefix_len = snprintf(prefix, sizeof(prefix), "tab=%d|%d|", tab->cx, tab->cy);
		if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix)) {
			return 0;
		}
		if (!workspaceStateWriteAll(fd, prefix, (size_t)prefix_len) ||
		    !workspaceStateWriteAll(fd, tab->filename, strlen(tab->filename)) ||
		    !workspaceStateWriteAll(fd, "\n", 1)) {
			return 0;
		}
	}
	return 1;
}

static int workspaceStateWriteLayoutSerialization(int fd) {
	if (E.layout_root == NULL ||
	    (!E.layout_root->is_split && E.layout_root->as.leaf.kind == EDITOR_PANE_KIND_EDITOR)) {
		return 1;
	}
	char layout_buf[2048];
	if (editorLayoutSerialize(E.layout_root, layout_buf, sizeof(layout_buf)) <= 0) {
		return 1;
	}
	if (!workspaceStateWriteAll(fd, "layout=", strlen("layout=")) ||
	    !workspaceStateWriteAll(fd, layout_buf, strlen(layout_buf)) ||
	    !workspaceStateWriteAll(fd, "\n", 1)) {
		return 0;
	}
	return 1;
}

static int workspaceStateWritePaneTabLine(int fd, const struct editorBuffer *tab, int leaf_idx,
                                          int is_active) {
	char prefix[64];
	int prefix_len = snprintf(prefix, sizeof(prefix), "pane_tab=%d|%d|", leaf_idx, is_active);
	if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix)) {
		return 0;
	}
	if (!workspaceStateWriteAll(fd, prefix, (size_t)prefix_len) ||
	    !workspaceStateWriteAll(fd, tab->filename, strlen(tab->filename)) ||
	    !workspaceStateWriteAll(fd, "\n", 1)) {
		return 0;
	}
	return 1;
}

static int workspaceStateWritePaneTabsForLeaf(int fd, struct editorPaneNode *leaf, int leaf_idx) {
	if (leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return 1;
	}
	const struct editorPaneView *view = &leaf->as.leaf.view;
	for (int slot = 0; slot < view->pane_tab_count; slot++) {
		int tab_idx = view->pane_tabs[slot];
		if (tab_idx < 0 || tab_idx >= E.tab_count) {
			continue;
		}
		const struct editorBuffer *tab = editorTabBufferHandleAt(tab_idx);
		if (tab == NULL || tab->tab_kind != EDITOR_TAB_FILE || tab->is_preview ||
		    !workspaceStatePathCanWriteLine(tab->filename)) {
			continue;
		}
		int is_active = tab_idx == view->active_tab_idx ? 1 : 0;
		if (!workspaceStateWritePaneTabLine(fd, tab, leaf_idx, is_active)) {
			return 0;
		}
	}
	return 1;
}

static int workspaceStateWriteFocusedPane(int fd, int focused_idx) {
	if (focused_idx < 0) {
		return 1;
	}
	char focus_buf[64];
	int n = snprintf(focus_buf, sizeof(focus_buf), "focused_pane=%d\n", focused_idx);
	if (n <= 0 || (size_t)n >= sizeof(focus_buf)) {
		return 1;
	}
	return workspaceStateWriteAll(fd, focus_buf, (size_t)n);
}

static int workspaceStateWritePaneTabsAndFocus(int fd) {
	if (E.layout_root == NULL) {
		return 1;
	}
	struct editorPaneNode *leaves[ROTIDE_WORKSPACE_MAX_LEAVES];
	int leaf_count =
	        workspaceStateCollectLeaves(E.layout_root, leaves, ROTIDE_WORKSPACE_MAX_LEAVES);
	int focused_idx = -1;
	for (int i = 0; i < leaf_count; i++) {
		if (leaves[i] == E.focused_leaf) {
			focused_idx = i;
		}
		if (!workspaceStateWritePaneTabsForLeaf(fd, leaves[i], i)) {
			return 0;
		}
	}
	return workspaceStateWriteFocusedPane(fd, focused_idx);
}

int editorWorkspaceStateSave(void) {
	if (E.workspace_state_path == NULL) {
		return 0;
	}

	char *tmp_path = NULL;
	int fd = editorAtomicOpenTemp(E.workspace_state_path, &tmp_path, 0600);
	if (fd == -1) {
		return 0;
	}

	if (!workspaceStateWriteHeader(fd) || !workspaceStateWriteRecentFiles(fd) ||
	    !workspaceStateWriteTabs(fd) || !workspaceStateWriteLayoutSerialization(fd) ||
	    !workspaceStateWritePaneTabsAndFocus(fd)) {
		editorAtomicAbortTemp(fd, tmp_path);
		return 0;
	}
	return editorAtomicCommitTemp(fd, tmp_path, E.workspace_state_path);
}
