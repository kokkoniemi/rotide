#include "workspace/workspace_state.h"

#include "rotide.h"
#include "config/dap_config.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/lsp.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "text/row.h"
#include "workspace/layout.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROTIDE_WORKSPACE_RECENT_FILE_LIMIT 64
#define ROTIDE_WORKSPACE_PENDING_TAB_LIMIT 256

struct editorWorkspacePendingTab {
	char *path;
	int cx;
	int cy;
};

static struct editorWorkspacePendingTab *g_pending_tabs;
static int g_pending_tab_count;
static int g_pending_tab_capacity;
static int g_pending_active_idx;
static char *g_pending_active_path;

static void editorWorkspaceStateFreeRecentFiles(void);
static void editorWorkspaceStateFreePendingTabs(void);

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
	editorWorkspaceStateFreeRecentFiles();
	editorWorkspaceStateFreePendingTabs();
	E.workspace_state_path = editorWorkspaceStateResolvePath();
	return E.workspace_state_path != NULL;
}

void editorWorkspaceStateShutdown(void) {
	free(E.workspace_state_path);
	E.workspace_state_path = NULL;
	editorWorkspaceStateFreeRecentFiles();
	editorWorkspaceStateFreePendingTabs();
}

const char *editorWorkspaceStatePath(void) {
	return E.workspace_state_path;
}

static void editorWorkspaceStateFreeRecentFiles(void) {
	for (int i = 0; i < E.recent_file_count; i++) {
		free(E.recent_file_paths[i]);
	}
	free(E.recent_file_paths);
	E.recent_file_paths = NULL;
	E.recent_file_count = 0;
	E.recent_file_capacity = 0;
}

static void editorWorkspaceStateFreePendingTabs(void) {
	for (int i = 0; i < g_pending_tab_count; i++) {
		free(g_pending_tabs[i].path);
	}
	free(g_pending_tabs);
	g_pending_tabs = NULL;
	g_pending_tab_count = 0;
	g_pending_tab_capacity = 0;
	g_pending_active_idx = -1;
	free(g_pending_active_path);
	g_pending_active_path = NULL;
}

static int editorWorkspaceStateAppendPendingTab(int cx, int cy, const char *path) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}
	if (g_pending_tab_count >= ROTIDE_WORKSPACE_PENDING_TAB_LIMIT) {
		return 0;
	}
	if (g_pending_tab_count >= g_pending_tab_capacity) {
		int new_cap = g_pending_tab_capacity > 0 ? g_pending_tab_capacity * 2 : 8;
		if (new_cap > ROTIDE_WORKSPACE_PENDING_TAB_LIMIT) {
			new_cap = ROTIDE_WORKSPACE_PENDING_TAB_LIMIT;
		}
		struct editorWorkspacePendingTab *grown =
				realloc(g_pending_tabs, sizeof(*g_pending_tabs) * (size_t)new_cap);
		if (grown == NULL) {
			return 0;
		}
		g_pending_tabs = grown;
		g_pending_tab_capacity = new_cap;
	}
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

static int editorWorkspaceStateEnsureRecentFileCapacity(int needed) {
	if (needed <= E.recent_file_capacity) {
		return 1;
	}
	int new_capacity = E.recent_file_capacity > 0 ? E.recent_file_capacity * 2 : 16;
	while (new_capacity < needed && new_capacity < ROTIDE_WORKSPACE_RECENT_FILE_LIMIT) {
		if (new_capacity > INT32_MAX / 2) {
			return 0;
		}
		new_capacity *= 2;
	}
	if (new_capacity > ROTIDE_WORKSPACE_RECENT_FILE_LIMIT) {
		new_capacity = ROTIDE_WORKSPACE_RECENT_FILE_LIMIT;
	}
	if (needed > new_capacity) {
		return 0;
	}

	char **paths =
			editorRealloc(E.recent_file_paths, sizeof(*E.recent_file_paths) *
					(size_t)new_capacity);
	if (paths == NULL) {
		return 0;
	}
	E.recent_file_paths = paths;
	E.recent_file_capacity = new_capacity;
	return 1;
}

static int editorWorkspaceStatePathCanWriteLine(const char *path) {
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

static int editorWorkspaceStateRecentFileIndex(const char *path) {
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
	return editorWorkspaceStateRecentFileIndex(path);
}

static int editorWorkspaceStateAppendRecentFile(const char *path) {
	if (!editorWorkspaceStatePathCanWriteLine(path)) {
		return 1;
	}
	if (editorWorkspaceStateRecentFileIndex(path) >= 0) {
		return 1;
	}
	if (E.recent_file_count >= ROTIDE_WORKSPACE_RECENT_FILE_LIMIT) {
		return 1;
	}
	if (!editorWorkspaceStateEnsureRecentFileCapacity(E.recent_file_count + 1)) {
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
	if (!editorWorkspaceStatePathCanWriteLine(path)) {
		return 0;
	}
	char *absolute = editorPathAbsoluteDup(path);
	if (absolute == NULL) {
		return 0;
	}

	int existing_idx = editorWorkspaceStateRecentFileIndex(absolute);
	if (existing_idx == 0) {
		free(absolute);
		return 1;
	}
	if (existing_idx > 0) {
		free(E.recent_file_paths[existing_idx]);
		memmove(&E.recent_file_paths[existing_idx],
				&E.recent_file_paths[existing_idx + 1],
				sizeof(*E.recent_file_paths) *
						(size_t)(E.recent_file_count - existing_idx - 1));
		E.recent_file_count--;
	}
	if (E.recent_file_count >= ROTIDE_WORKSPACE_RECENT_FILE_LIMIT) {
		free(E.recent_file_paths[E.recent_file_count - 1]);
		E.recent_file_count--;
	}
	if (!editorWorkspaceStateEnsureRecentFileCapacity(E.recent_file_count + 1)) {
		free(absolute);
		return 0;
	}
	memmove(&E.recent_file_paths[1], &E.recent_file_paths[0],
			sizeof(*E.recent_file_paths) * (size_t)E.recent_file_count);
	E.recent_file_paths[0] = absolute;
	E.recent_file_count++;
	return 1;
}

static enum editorDrawerMode editorWorkspaceStateModeFromString(const char *value) {
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

static const char *editorWorkspaceStateModeToString(enum editorDrawerMode mode) {
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

static int editorWorkspaceStateParseTabLine(const char *value, int *cx_out, int *cy_out,
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
	if (!editorWorkspaceStateParseInt(cx_buf, &cx) ||
			!editorWorkspaceStateParseInt(cy_buf, &cy)) {
		return 0;
	}
	*cx_out = cx;
	*cy_out = cy;
	*path_out = second + 1;
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
	int menu_expanded = -1;
	int git_expanded = -1;
	int lsp_expanded = -1;
	int dap_expanded = -1;
	editorWorkspaceStateFreePendingTabs();
	g_pending_active_idx = -1;

	char line[4096];
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
		} else if (strcmp(key, "drawer_menu_expanded") == 0) {
			(void)editorWorkspaceStateParseInt(value, &menu_expanded);
		} else if (strcmp(key, "drawer_git_expanded") == 0) {
			(void)editorWorkspaceStateParseInt(value, &git_expanded);
		} else if (strcmp(key, "drawer_lsp_expanded") == 0) {
			(void)editorWorkspaceStateParseInt(value, &lsp_expanded);
		} else if (strcmp(key, "drawer_dap_expanded") == 0) {
			(void)editorWorkspaceStateParseInt(value, &dap_expanded);
		} else if (strcmp(key, "recent_file") == 0) {
			(void)editorWorkspaceStateAppendRecentFile(value);
		} else if (strcmp(key, "tab") == 0) {
			int tab_cx = 0;
			int tab_cy = 0;
			const char *tab_path = NULL;
			if (editorWorkspaceStateParseTabLine(value, &tab_cx, &tab_cy, &tab_path)) {
				(void)editorWorkspaceStateAppendPendingTab(tab_cx, tab_cy, tab_path);
			}
		} else if (strcmp(key, "active_tab") == 0) {
			free(g_pending_active_path);
			g_pending_active_path = strdup(value);
		} else if (strcmp(key, "layout") == 0) {
			struct editorPaneNode *restored = editorLayoutDeserialize(value);
			if (restored != NULL) {
				editorPaneNodeFree(E.layout_root);
				E.layout_root = restored;
				E.focused_leaf = editorPaneNodeFirstLeaf(E.layout_root);
				if (E.focused_leaf != NULL) {
					editorPaneViewCaptureFromState(&E.focused_leaf->as.leaf.view);
				}
			}
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
	if (menu_expanded >= 0) {
		E.drawer_menu_expanded = (unsigned int)menu_expanded;
	}
	if (git_expanded >= 0) {
		E.drawer_git_expanded = (unsigned int)git_expanded;
	}
	if (lsp_expanded >= 0) {
		E.drawer_lsp_expanded = (unsigned int)lsp_expanded;
	}
	if (dap_expanded >= 0) {
		E.drawer_dap_expanded = (unsigned int)dap_expanded;
	}
	return 1;
}

int editorWorkspaceStateRestoreTabs(void) {
	if (g_pending_tab_count <= 0) {
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
		const struct editorWorkspacePendingTab *pending = &g_pending_tabs[i];
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
		if (target_cx > E.rows[target_cy].size) {
			target_cx = E.rows[target_cy].size;
		}
		target_cx = editorRowClampCxToClusterBoundary(&E.rows[target_cy], target_cx);
		if (target_cx < 0) {
			target_cx = 0;
		}
		if (target_cx > E.rows[target_cy].size) {
			target_cx = E.rows[target_cy].size;
		}
		size_t target_offset = 0;
		if (editorBufferPosToOffset(target_cy, target_cx, &target_offset)) {
			(void)editorSyncCursorFromOffset(target_offset);
		}
		editorViewportCenterCursor();
	}
	editorOpenSetDeferLsp(0);
	if (opened_any && g_pending_active_path != NULL) {
		(void)editorTabOpenOrSwitchToFile(g_pending_active_path);
	}
	/*
	 * When the active path is the last tab we opened, editorTabSwitchToIndex returns early
	 * without calling editorLoadActiveTab, so the LSP didOpen never runs. Force it here.
	 */
	if (opened_any) {
		editorLspEnsureActiveDocumentTracked();
	}
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
	editorWorkspaceStateFreePendingTabs();
	return opened_any;
}

static int editorWorkspaceStateWriteAll(int fd, const char *buf, size_t len) {
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
			mode != EDITOR_DRAWER_MODE_GIT && mode != EDITOR_DRAWER_MODE_LSP &&
			mode != EDITOR_DRAWER_MODE_DAP) {
		mode = EDITOR_DRAWER_MODE_TREE;
	}

	char buf[256];
	int len = snprintf(buf, sizeof(buf),
			"drawer_width_cols=%d\n"
			"drawer_width_user_set=%d\n"
			"drawer_collapsed=%d\n"
			"drawer_mode=%s\n"
			"drawer_menu_expanded=%u\n"
			"drawer_git_expanded=%u\n"
			"drawer_lsp_expanded=%u\n"
			"drawer_dap_expanded=%u\n",
			E.drawer_width_cols,
			E.drawer_width_user_set ? 1 : 0,
			E.drawer_collapsed ? 1 : 0,
			editorWorkspaceStateModeToString(mode),
			E.drawer_menu_expanded,
			E.drawer_git_expanded,
			E.drawer_lsp_expanded,
			E.drawer_dap_expanded);
	if (len <= 0 || (size_t)len >= sizeof(buf)) {
		(void)close(fd);
		return 0;
	}

	if (!editorWorkspaceStateWriteAll(fd, buf, (size_t)len)) {
		(void)close(fd);
		return 0;
	}
	for (int i = 0; i < E.recent_file_count; i++) {
		const char *path = E.recent_file_paths[i];
		if (!editorWorkspaceStatePathCanWriteLine(path)) {
			continue;
		}
		if (!editorWorkspaceStateWriteAll(fd, "recent_file=", strlen("recent_file=")) ||
				!editorWorkspaceStateWriteAll(fd, path, strlen(path)) ||
				!editorWorkspaceStateWriteAll(fd, "\n", 1)) {
			(void)close(fd);
			return 0;
		}
	}

	const char *active_path = NULL;
	for (int i = 0; i < E.tab_count; i++) {
		const struct editorBuffer *tab = editorTabBufferHandleAt(i);
		if (tab == NULL) {
			continue;
		}
		if (tab->tab_kind != EDITOR_TAB_FILE || tab->is_preview ||
				!editorWorkspaceStatePathCanWriteLine(tab->filename)) {
			continue;
		}
		char prefix[64];
		int prefix_len = snprintf(prefix, sizeof(prefix), "tab=%d|%d|", tab->cx, tab->cy);
		if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix)) {
			(void)close(fd);
			return 0;
		}
		if (!editorWorkspaceStateWriteAll(fd, prefix, (size_t)prefix_len) ||
				!editorWorkspaceStateWriteAll(fd, tab->filename, strlen(tab->filename)) ||
				!editorWorkspaceStateWriteAll(fd, "\n", 1)) {
			(void)close(fd);
			return 0;
		}
		if (i == E.active_tab) {
			active_path = tab->filename;
		}
	}
	if (active_path != NULL) {
		if (!editorWorkspaceStateWriteAll(fd, "active_tab=", strlen("active_tab=")) ||
				!editorWorkspaceStateWriteAll(fd, active_path, strlen(active_path)) ||
				!editorWorkspaceStateWriteAll(fd, "\n", 1)) {
			(void)close(fd);
			return 0;
		}
	}
	if (E.layout_root != NULL && E.layout_root->is_split) {
		char layout_buf[2048];
		if (editorLayoutSerialize(E.layout_root, layout_buf,
					sizeof(layout_buf)) > 0) {
			if (!editorWorkspaceStateWriteAll(fd, "layout=", strlen("layout=")) ||
					!editorWorkspaceStateWriteAll(fd, layout_buf,
							strlen(layout_buf)) ||
					!editorWorkspaceStateWriteAll(fd, "\n", 1)) {
				(void)close(fd);
				return 0;
			}
		}
	}
	if (close(fd) != 0) {
		return 0;
	}
	return 1;
}
