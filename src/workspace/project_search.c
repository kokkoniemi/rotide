#include "workspace/project_search.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define EDITOR_PROJECT_SEARCH_MAX_RESULTS 500
#define EDITOR_PROJECT_SEARCH_OUTPUT_MAX (2U * 1024U * 1024U)

struct editorProjectSearchCommandOutput {
	char *data;
	size_t len;
	int exit_code;
	int truncated;
};

static const char *editorProjectSearchRoot(void) {
	const char *root = editorDrawerRootPath();
	return root != NULL && root[0] != '\0' ? root : ".";
}

static const char *editorProjectSearchDisplayPath(const char *path) {
	const char *root = editorProjectSearchRoot();
	size_t root_len = strlen(root);
	if (path != NULL && root_len > 0 && strncmp(path, root, root_len) == 0 &&
			path[root_len] == '/') {
		return path + root_len + 1;
	}
	return path != NULL ? path : "";
}

static void editorProjectSearchFreeResult(struct editorProjectSearchResult *result) {
	if (result == NULL) {
		return;
	}
	free(result->path);
	free(result->line_text);
	free(result->display);
	memset(result, 0, sizeof(*result));
}

static void editorProjectSearchClearResults(void) {
	for (int i = 0; i < E.drawer_project_search_result_count; i++) {
		editorProjectSearchFreeResult(&E.drawer_project_search_results[i]);
	}
	E.drawer_project_search_result_count = 0;
	free(E.drawer_project_search_previewed_path);
	E.drawer_project_search_previewed_path = NULL;
	E.drawer_project_search_previewed_line = 0;
	E.drawer_project_search_previewed_col = 0;
}

void editorProjectSearchFree(void) {
	editorProjectSearchClearResults();
	free(E.drawer_project_search_results);
	E.drawer_project_search_results = NULL;
	E.drawer_project_search_result_capacity = 0;
	free(E.drawer_project_search_query);
	E.drawer_project_search_query = NULL;
	E.drawer_project_search_query_len = 0;
	E.drawer_project_search_active_tab_before = -1;
	if (E.drawer_mode == EDITOR_DRAWER_MODE_PROJECT_SEARCH) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	}
}

int editorProjectSearchIsActive(void) {
	return E.drawer_mode == EDITOR_DRAWER_MODE_PROJECT_SEARCH;
}

const char *editorProjectSearchQuery(void) {
	return E.drawer_project_search_query != NULL ? E.drawer_project_search_query : "";
}

static int editorProjectSearchEnsureResultCapacity(int needed) {
	if (needed <= E.drawer_project_search_result_capacity) {
		return 1;
	}
	int new_capacity = E.drawer_project_search_result_capacity > 0 ?
			E.drawer_project_search_result_capacity * 2 : 32;
	while (new_capacity < needed) {
		if (new_capacity > INT_MAX / 2) {
			return 0;
		}
		new_capacity *= 2;
	}

	size_t cap_size = 0;
	size_t bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(*E.drawer_project_search_results), cap_size, &bytes)) {
		return 0;
	}
	struct editorProjectSearchResult *results =
			editorRealloc(E.drawer_project_search_results, bytes);
	if (results == NULL) {
		return 0;
	}
	for (int i = E.drawer_project_search_result_capacity; i < new_capacity; i++) {
		memset(&results[i], 0, sizeof(results[i]));
	}
	E.drawer_project_search_results = results;
	E.drawer_project_search_result_capacity = new_capacity;
	return 1;
}

static int editorProjectSearchBuildDisplay(const char *path, int line, int col,
		const char *line_text, char **display_out) {
	const char *display_path = editorProjectSearchDisplayPath(path);
	int len = snprintf(NULL, 0, "%s:%d:%d: %s", display_path, line, col,
			line_text != NULL ? line_text : "");
	if (len < 0) {
		return 0;
	}
	size_t bytes = (size_t)len + 1;
	char *display = editorMalloc(bytes);
	if (display == NULL) {
		return 0;
	}
	snprintf(display, bytes, "%s:%d:%d: %s", display_path, line, col,
			line_text != NULL ? line_text : "");
	*display_out = display;
	return 1;
}

static int editorProjectSearchAppendResult(const char *path, int line, int col,
		const char *line_text) {
	if (E.drawer_project_search_result_count >= EDITOR_PROJECT_SEARCH_MAX_RESULTS) {
		return 1;
	}
	if (path == NULL || path[0] == '\0' || line < 1 || col < 1) {
		return 1;
	}
	if (!editorProjectSearchEnsureResultCapacity(E.drawer_project_search_result_count + 1)) {
		return 0;
	}

	struct editorProjectSearchResult result;
	memset(&result, 0, sizeof(result));
	result.path = strdup(path);
	result.line = line;
	result.col = col;
	result.line_text = strdup(line_text != NULL ? line_text : "");
	if (result.path == NULL || result.line_text == NULL ||
			!editorProjectSearchBuildDisplay(result.path, result.line, result.col,
					result.line_text, &result.display)) {
		editorProjectSearchFreeResult(&result);
		return 0;
	}

	E.drawer_project_search_results[E.drawer_project_search_result_count] = result;
	E.drawer_project_search_result_count++;
	return 1;
}

static int editorProjectSearchOutputAppend(struct editorProjectSearchCommandOutput *output,
		const char *buf, size_t len) {
	if (len == 0 || output->truncated) {
		return 1;
	}
	size_t new_len = 0;
	if (!editorSizeAdd(output->len, len, &new_len) ||
			new_len > EDITOR_PROJECT_SEARCH_OUTPUT_MAX) {
		output->truncated = 1;
		return 1;
	}
	char *grown = editorRealloc(output->data, new_len + 1);
	if (grown == NULL) {
		return 0;
	}
	memcpy(grown + output->len, buf, len);
	grown[new_len] = '\0';
	output->data = grown;
	output->len = new_len;
	return 1;
}

static int editorProjectSearchRunCommand(char *const argv[],
		struct editorProjectSearchCommandOutput *output) {
	int pipefd[2] = {-1, -1};
	if (output == NULL || pipe(pipefd) == -1) {
		return 0;
	}
	memset(output, 0, sizeof(*output));
	output->exit_code = 126;

	pid_t pid = fork();
	if (pid == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return 0;
	}
	if (pid == 0) {
		(void)signal(SIGPIPE, SIG_DFL);
		close(pipefd[0]);
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			_exit(126);
		}
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull != -1) {
			(void)dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		close(pipefd[1]);
		execvp(argv[0], argv);
		_exit(errno == ENOENT ? 127 : 126);
	}

	close(pipefd[1]);
	char buf[4096];
	while (1) {
		ssize_t nread = read(pipefd[0], buf, sizeof(buf));
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			close(pipefd[0]);
			(void)waitpid(pid, NULL, 0);
			free(output->data);
			memset(output, 0, sizeof(*output));
			return 0;
		}
		if (nread == 0) {
			break;
		}
		if (!editorProjectSearchOutputAppend(output, buf, (size_t)nread)) {
			close(pipefd[0]);
			(void)waitpid(pid, NULL, 0);
			free(output->data);
			memset(output, 0, sizeof(*output));
			return 0;
		}
	}
	close(pipefd[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			free(output->data);
			memset(output, 0, sizeof(*output));
			return 0;
		}
	}
	if (WIFEXITED(status)) {
		output->exit_code = WEXITSTATUS(status);
	} else {
		output->exit_code = 126;
	}
	if (output->data == NULL) {
		output->data = strdup("");
		if (output->data == NULL) {
			return 0;
		}
	}
	return 1;
}

static int editorProjectSearchParsePositiveInt(const char *start, const char *end,
		int *value_out) {
	if (start == NULL || end == NULL || start >= end || value_out == NULL) {
		return 0;
	}
	long value = 0;
	for (const char *p = start; p < end; p++) {
		if (!isdigit((unsigned char)*p)) {
			return 0;
		}
		int digit = *p - '0';
		if (value > (LONG_MAX - digit) / 10) {
			return 0;
		}
		value = value * 10 + digit;
		if (value > INT_MAX) {
			return 0;
		}
	}
	if (value < 1) {
		return 0;
	}
	*value_out = (int)value;
	return 1;
}

static int editorProjectSearchParseRgLine(const char *line) {
	for (const char *first = strchr(line, ':'); first != NULL; first = strchr(first + 1, ':')) {
		const char *second = strchr(first + 1, ':');
		if (second == NULL) {
			return 1;
		}
		const char *third = strchr(second + 1, ':');
		if (third == NULL) {
			return 1;
		}
		int line_no = 0;
		int col = 0;
		if (!editorProjectSearchParsePositiveInt(first + 1, second, &line_no) ||
				!editorProjectSearchParsePositiveInt(second + 1, third, &col)) {
			continue;
		}
		size_t path_len = (size_t)(first - line);
		char *path = editorMalloc(path_len + 1);
		if (path == NULL) {
			return 0;
		}
		memcpy(path, line, path_len);
		path[path_len] = '\0';
		int ok = editorProjectSearchAppendResult(path, line_no, col, third + 1);
		free(path);
		return ok;
	}
	return 1;
}

static const char *editorProjectSearchCaseSensitiveStrstr(const char *haystack,
		const char *needle) {
	if (needle == NULL || needle[0] == '\0') {
		return haystack;
	}
	return strstr(haystack, needle);
}

static int editorProjectSearchParseGrepLine(const char *line, const char *query) {
	for (const char *first = strchr(line, ':'); first != NULL; first = strchr(first + 1, ':')) {
		const char *second = strchr(first + 1, ':');
		if (second == NULL) {
			return 1;
		}
		int line_no = 0;
		if (!editorProjectSearchParsePositiveInt(first + 1, second, &line_no)) {
			continue;
		}
		const char *line_text = second + 1;
		const char *match = editorProjectSearchCaseSensitiveStrstr(line_text, query);
		int col = 1;
		if (match != NULL) {
			size_t col_size = (size_t)(match - line_text) + 1;
			if (!editorSizeToInt(col_size, &col)) {
				col = 1;
			}
		}
		size_t path_len = (size_t)(first - line);
		char *path = editorMalloc(path_len + 1);
		if (path == NULL) {
			return 0;
		}
		memcpy(path, line, path_len);
		path[path_len] = '\0';
		int ok = editorProjectSearchAppendResult(path, line_no, col, line_text);
		free(path);
		return ok;
	}
	return 1;
}

static int editorProjectSearchParseOutput(char *data, const char *query, int rg_format) {
	if (data == NULL) {
		return 1;
	}
	char *line = data;
	while (line[0] != '\0') {
		char *next = strchr(line, '\n');
		if (next != NULL) {
			*next = '\0';
		}
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\r') {
			line[len - 1] = '\0';
		}
		if (line[0] != '\0') {
			int ok = rg_format ? editorProjectSearchParseRgLine(line) :
					editorProjectSearchParseGrepLine(line, query);
			if (!ok) {
				return 0;
			}
		}
		if (next == NULL ||
				E.drawer_project_search_result_count >= EDITOR_PROJECT_SEARCH_MAX_RESULTS) {
			break;
		}
		line = next + 1;
	}
	return 1;
}

static int editorProjectSearchRefreshResults(void) {
	editorProjectSearchClearResults();
	const char *query = editorProjectSearchQuery();
	if (query[0] == '\0') {
		E.drawer_selected_index = 1;
		E.drawer_rowoff = 0;
		return 1;
	}

	const char *root = editorProjectSearchRoot();
	struct editorProjectSearchCommandOutput output;
	char *rg_argv[] = {"rg", "--fixed-strings", "--line-number", "--column", "--no-heading",
			"--color", "never", "--no-messages", "-e", (char *)query, (char *)root, NULL};
	if (!editorProjectSearchRunCommand(rg_argv, &output)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	int rg_format = 1;
	if (output.exit_code == 127) {
		free(output.data);
		char *grep_argv[] = {"grep", "-RInF", "--exclude-dir=.git",
				"--exclude-dir=node_modules", "--binary-files=without-match", "-e",
				(char *)query, (char *)root, NULL};
		if (!editorProjectSearchRunCommand(grep_argv, &output)) {
			editorSetAllocFailureStatus();
			return 0;
		}
		rg_format = 0;
	}

	if (output.exit_code == 127) {
		editorSetStatusMsg("Project search requires rg or grep");
		free(output.data);
		return 1;
	}
	if (output.exit_code != 0 && output.exit_code != 1) {
		editorSetStatusMsg("Project search failed");
		free(output.data);
		return 1;
	}
	if (!editorProjectSearchParseOutput(output.data, query, rg_format)) {
		free(output.data);
		editorSetAllocFailureStatus();
		return 0;
	}
	if (output.truncated ||
			E.drawer_project_search_result_count >= EDITOR_PROJECT_SEARCH_MAX_RESULTS) {
		editorSetStatusMsg("Project search results truncated");
	}
	free(output.data);

	if (E.drawer_selected_index < 1 ||
			E.drawer_selected_index >= editorProjectSearchVisibleCount()) {
		E.drawer_selected_index = 1;
	}
	E.drawer_rowoff = 0;
	return 1;
}

static const struct editorProjectSearchResult *editorProjectSearchSelectedResult(void) {
	int result_idx = E.drawer_selected_index - 1;
	if (result_idx < 0 || result_idx >= E.drawer_project_search_result_count) {
		return NULL;
	}
	return &E.drawer_project_search_results[result_idx];
}

int editorProjectSearchEnter(void) {
	editorProjectSearchFree();
	E.drawer_mode = EDITOR_DRAWER_MODE_PROJECT_SEARCH;
	E.drawer_project_search_active_tab_before = editorTabActiveIndex();
	E.drawer_project_search_query = strdup("");
	if (E.drawer_project_search_query == NULL) {
		editorProjectSearchExit(0);
		editorSetAllocFailureStatus();
		return 0;
	}
	E.drawer_project_search_query_len = 0;
	if (!editorProjectSearchRefreshResults()) {
		editorProjectSearchExit(0);
		return 0;
	}
	E.drawer_selected_index = 1;
	E.drawer_rowoff = 0;
	return 1;
}

void editorProjectSearchExit(int restore_previous_tab) {
	int previous_tab = E.drawer_project_search_active_tab_before;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	editorProjectSearchFree();
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	if (restore_previous_tab && previous_tab >= 0 && previous_tab < editorTabCount()) {
		(void)editorTabSwitchToIndex(previous_tab);
	}
}

int editorProjectSearchAppendByte(int c) {
	if (c < CHAR_MIN || c > CHAR_MAX) {
		return 0;
	}
	unsigned char byte = (unsigned char)c;
	if (byte < 0x80 && iscntrl(byte)) {
		return 0;
	}
	size_t new_len = E.drawer_project_search_query_len + 1;
	char *query = editorRealloc(E.drawer_project_search_query, new_len + 1);
	if (query == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	E.drawer_project_search_query = query;
	E.drawer_project_search_query[E.drawer_project_search_query_len] = (char)byte;
	E.drawer_project_search_query_len = new_len;
	E.drawer_project_search_query[E.drawer_project_search_query_len] = '\0';
	return editorProjectSearchRefreshResults();
}

int editorProjectSearchBackspace(void) {
	if (E.drawer_project_search_query_len == 0) {
		return 0;
	}

	size_t delete_idx = E.drawer_project_search_query_len - 1;
	while (delete_idx > 0 &&
			(((unsigned char)E.drawer_project_search_query[delete_idx] & 0xC0) == 0x80)) {
		delete_idx--;
	}
	E.drawer_project_search_query[delete_idx] = '\0';
	E.drawer_project_search_query_len = delete_idx;
	return editorProjectSearchRefreshResults();
}

int editorProjectSearchVisibleCount(void) {
	return 1 + (E.drawer_project_search_result_count > 0 ? E.drawer_project_search_result_count : 1);
}

int editorProjectSearchGetVisibleEntry(int visible_idx,
		struct editorDrawerEntryView *view_out) {
	if (view_out == NULL || visible_idx < 0 || visible_idx >= editorProjectSearchVisibleCount()) {
		return 0;
	}

	memset(view_out, 0, sizeof(*view_out));
	view_out->depth = 0;
	view_out->parent_visible_idx = -1;
	view_out->is_last_sibling = 1;
	if (visible_idx == 0) {
		view_out->name = editorProjectSearchQuery();
		view_out->is_search_header = 1;
		return 1;
	}

	if (E.drawer_project_search_result_count <= 0) {
		view_out->name = editorProjectSearchQuery()[0] == '\0' ? "Type to search" : "No matches";
		view_out->is_placeholder = 1;
		return 1;
	}

	const struct editorProjectSearchResult *result =
			&E.drawer_project_search_results[visible_idx - 1];
	view_out->name = result->display;
	view_out->path = result->path;
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->is_last_sibling = visible_idx == E.drawer_project_search_result_count;
	view_out->is_active_file = E.filename != NULL &&
			editorPathsReferToSameFile(result->path, E.filename);
	return 1;
}

void editorProjectSearchClampViewport(int viewport_rows) {
	int visible_count = editorProjectSearchVisibleCount();
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

int editorProjectSearchMoveSelectionBy(int delta, int viewport_rows) {
	int visible_count = editorProjectSearchVisibleCount();
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
	editorProjectSearchClampViewport(viewport_rows);
	return E.drawer_selected_index != old_selection;
}

int editorProjectSearchSelectVisibleIndex(int visible_idx, int viewport_rows) {
	if (visible_idx <= 0 || visible_idx >= editorProjectSearchVisibleCount()) {
		return 0;
	}
	E.drawer_selected_index = visible_idx;
	editorProjectSearchClampViewport(viewport_rows);
	return 1;
}

int editorProjectSearchSelectedIsDirectory(void) {
	return 0;
}

static int editorProjectSearchApplySelectedLocation(const struct editorProjectSearchResult *result) {
	if (result == NULL) {
		return 0;
	}
	int row = result->line - 1;
	int col = result->col - 1;
	if (row < 0) {
		row = 0;
	}
	if (row >= E.numrows) {
		row = E.numrows > 0 ? E.numrows - 1 : 0;
	}
	if (col < 0) {
		col = 0;
	}
	if (row < E.numrows && col > E.rows[row].size) {
		col = E.rows[row].size;
	}
	size_t offset = 0;
	if (!editorBufferPosToOffset(row, col, &offset) ||
			!editorSyncCursorFromOffsetByteBoundary(offset)) {
		return 0;
	}

	free(E.search_query);
	E.search_query = strdup(editorProjectSearchQuery());
	if (E.search_query != NULL) {
		E.search_match_offset = E.cursor_offset;
		size_t query_len = strlen(E.search_query);
		E.search_match_len = query_len <= (size_t)INT_MAX ? (int)query_len : 0;
	} else {
		E.search_match_offset = 0;
		E.search_match_len = 0;
		editorSetAllocFailureStatus();
	}
	editorViewportEnsureCursorVisible();
	return 1;
}

int editorProjectSearchOpenSelectedFileInTab(void) {
	const struct editorProjectSearchResult *result = editorProjectSearchSelectedResult();
	if (result == NULL || result->path == NULL || result->path[0] == '\0') {
		return 0;
	}
	char *path_copy = strdup(result->path);
	if (path_copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	int line = result->line;
	int col = result->col;
	if (!editorTabOpenOrSwitchToFile(result->path)) {
		free(path_copy);
		return 0;
	}
	struct editorProjectSearchResult selected;
	memset(&selected, 0, sizeof(selected));
	selected.path = path_copy;
	selected.line = line;
	selected.col = col;
	(void)editorProjectSearchApplySelectedLocation(&selected);
	editorProjectSearchExit(0);
	(void)editorDrawerRevealPath(path_copy, E.window_rows + 1);
	free(path_copy);
	return 1;
}

int editorProjectSearchOpenSelectedFileInPreviewTab(void) {
	const struct editorProjectSearchResult *result = editorProjectSearchSelectedResult();
	if (result == NULL || result->path == NULL || result->path[0] == '\0') {
		return 0;
	}
	if (!editorTabOpenOrSwitchToPreviewFile(result->path)) {
		return 0;
	}
	return editorProjectSearchApplySelectedLocation(result);
}

int editorProjectSearchPreviewSelection(void) {
	const struct editorProjectSearchResult *result = editorProjectSearchSelectedResult();
	if (result == NULL || result->path == NULL || result->path[0] == '\0') {
		return 0;
	}
	if (E.drawer_project_search_previewed_path != NULL &&
			editorPathsReferToSameFile(E.drawer_project_search_previewed_path, result->path) &&
			E.drawer_project_search_previewed_line == result->line &&
			E.drawer_project_search_previewed_col == result->col) {
		return editorProjectSearchApplySelectedLocation(result);
	}
	if (!editorProjectSearchOpenSelectedFileInPreviewTab()) {
		return 0;
	}
	char *copy = strdup(result->path);
	if (copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	free(E.drawer_project_search_previewed_path);
	E.drawer_project_search_previewed_path = copy;
	E.drawer_project_search_previewed_line = result->line;
	E.drawer_project_search_previewed_col = result->col;
	return 1;
}

static int editorProjectSearchQueryDisplayCols(void) {
	const char *query = editorProjectSearchQuery();
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

int editorProjectSearchHeaderCursorCol(int drawer_cols) {
	int col = 3 + 1 + 6 + editorProjectSearchQueryDisplayCols();
	if (col < 1) {
		col = 1;
	}
	if (col > drawer_cols) {
		col = drawer_cols;
	}
	return col;
}
