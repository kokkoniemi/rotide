#include "test_helpers.h"

#include "alloc_test_hooks.h"
#include "config/dap_config.h"
#include "config/keymap.h"
#include "config/lsp_config.h"
#include "config/theme_config.h"
#include "debug/dap.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "editor_test_api.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/prompt.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "render/viewport.h"
#include "rotide.h"
#include "save_syscalls_test_hooks.h"
#include "support/terminal.h"
#include "text/document.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/watch.h"
#include "workspace/workspace_state.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

static char *g_test_repo_root = NULL;

static void ensure_test_stdout_open(void) {
	if (fcntl(STDOUT_FILENO, F_GETFD) != -1 || errno != EBADF) {
		return;
	}

	int reopened = dup(STDERR_FILENO);
	if (reopened == -1) {
		return;
	}
	if (reopened != STDOUT_FILENO) {
		(void)dup2(reopened, STDOUT_FILENO);
		(void)close(reopened);
	}
}

void clear_editor_state(void) {
	editorDapShutdown();
	editorDrawerShutdown();
	editorRecoveryShutdown();
	editorWorkspaceStateShutdown();
	editorTabsFreeAll();
	editorWatchTestReset();
	editorOutputTestResetFrameCache();
	editorClipboardClear();
	editorVimRegistersClear();
	editorVimKeymapResetDefaults();
	editorPaneNodeFree(E.layout_root);
	E.layout_root = NULL;
	E.focused_leaf = NULL;
}

void reset_editor_state(void) {
	ensure_test_stdout_open();
	editorTestAllocReset();
	editorTestSaveSyscallsReset();
	editorLspTestResetMock();
	editorDapShutdown();
	editorDocumentTestResetStats();
	editorActiveTextSourceDupTestResetCount();
	clear_editor_state();
	memset(&E, 0, sizeof(E));
	E.window_rows = 8;
	E.window_cols = 40;
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = 0;
	E.tab_title = NULL;
	E.cursor_offset = 0;
	E.search_match_offset = 0;
	E.search_match_len = 0;
	E.search_direction = 1;
	E.search_saved_offset = 0;
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.column_select_active = 0;
	E.column_select_anchor_cy = 0;
	E.column_select_anchor_rx = 0;
	E.column_select_cursor_rx = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_anchor_offset = 0;
	E.mouse_drag_started = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.syntax_state = NULL;
	E.syntax_parse_failures = 0;
	E.syntax_revision = 0;
	E.syntax_generation = 0;
	E.syntax_background_pending = 0;
	E.syntax_pending_revision = 0;
	E.syntax_pending_first_row = 0;
	E.syntax_pending_row_count = 0;
	editorLspConfigInitDefaults(&E.lsp_config);
	E.lsp_config.gopls_enabled = 0;
	E.lsp_config.clangd_enabled = 0;
	E.lsp_config.html_enabled = 0;
	E.lsp_config.css_enabled = 0;
	E.lsp_config.json_enabled = 0;
	E.lsp_config.javascript_enabled = 0;
	E.lsp_config.eslint_enabled = 0;
	E.lsp_config.autocomplete_enabled = 0;
	E.lsp_config.autocomplete_max_items = 50;
	editorDapConfigInitDefaults();
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	E.lsp_eslint_doc_open = 0;
	E.lsp_eslint_doc_version = 0;
	E.task_pid = 0;
	E.task_output_fd = -1;
	E.task_running = 0;
	E.task_tab_idx = -1;
	E.task_output_truncated = 0;
	E.task_output_bytes = 0;
	E.task_exit_code = 0;
	E.task_success_status[0] = '\0';
	E.task_failure_status[0] = '\0';
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	E.drawer_menu_expanded = 0;
	E.drawer_git_expanded = 0;
	E.drawer_lsp_expanded = 0;
	E.drawer_dap_expanded = 0;
	E.drawer_width_cols = ROTIDE_DRAWER_DEFAULT_WIDTH;
	E.drawer_width_user_set = 0;
	E.drawer_collapsed = 0;
	E.drawer_resize_active = 0;
	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	E.drawer_search_active_tab_before = -1;
	E.drawer_search_restore_collapsed = 0;
	E.drawer_project_search_active_tab_before = -1;
	E.drawer_project_search_restore_collapsed = 0;
	E.cursor_style = EDITOR_CURSOR_STYLE_BAR;
	E.cursor_blink_enabled = 1;
	E.line_wrap_enabled = 0;
	E.line_numbers_enabled = 1;
	E.current_line_highlight_enabled = 1;
	E.nerd_fonts_enabled = 0;
	E.auto_indent_enabled = 0;
	E.indent_use_tabs = 0;
	E.indent_width = ROTIDE_INDENT_WIDTH_DEFAULT;
	E.column_select_drag_modifier = EDITOR_MOUSE_MOD_ALT | EDITOR_MOUSE_MOD_SHIFT;
	E.wrapoff = 0;
	editorThemeInitDefault(&E.theme);
	editorSyntaxTestResetBudgetOverrides();
	editorSyntaxTestResetParseFailureCountdowns();
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	editorKeymapInitDefaults(&E.keymap);
	(void)editorInputSystemActivate("cua");
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	E.focused_leaf = E.layout_root;
}

void add_row(const char *s) {
	add_row_bytes(s, strlen(s));
}

void add_row_bytes(const char *s, size_t len) {
	if (s == NULL) {
		return;
	}
	int saved_cy = E.cy;
	int saved_cx = E.cx;
	size_t saved_offset = E.cursor_offset;
	size_t with_newline = len + 1;
	char *line = malloc(with_newline);
	if (line == NULL) {
		return;
	}
	memcpy(line, s, len);
	line[len] = '\n';
	E.cy = E.numrows;
	E.cx = 0;
	(void)editorInsertText(line, with_newline);
	free(line);

	if (saved_cy < 0) {
		saved_cy = 0;
	}
	if (saved_cy > E.numrows) {
		saved_cy = E.numrows;
	}
	if (saved_cy < E.numrows) {
		if (saved_cx < 0) {
			saved_cx = 0;
		}
		int line_len = (int)editorDocumentLineLength(E.document, saved_cy);
		if (saved_cx > line_len) {
			saved_cx = line_len;
		}
	} else {
		saved_cx = 0;
	}
	E.cy = saved_cy;
	E.cx = saved_cx;
	if (!editorBufferPosToOffset(E.cy, E.cx, &E.cursor_offset)) {
		E.cursor_offset = saved_offset;
	}
}

int write_all(int fd, const char *buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written <= 0) {
			return -1;
		}
		total += (size_t)written;
	}
	return 0;
}

int setup_stdin_bytes(const char *data, size_t len, int *saved_stdin) {
	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return -1;
	}
	if (write_all(pipefd[1], data, len) == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (close(pipefd[1]) == -1) {
		close(pipefd[0]);
		return -1;
	}

	*saved_stdin = dup(STDIN_FILENO);
	if (*saved_stdin == -1) {
		close(pipefd[0]);
		return -1;
	}
	if (dup2(pipefd[0], STDIN_FILENO) == -1) {
		close(pipefd[0]);
		close(*saved_stdin);
		return -1;
	}
	if (close(pipefd[0]) == -1) {
		close(*saved_stdin);
		return -1;
	}

	return 0;
}

int restore_stdin(int saved_stdin) {
	int ret = dup2(saved_stdin, STDIN_FILENO);
	int close_ret = close(saved_stdin);
	if (ret == -1 || close_ret == -1) {
		return -1;
	}
	return 0;
}

int redirect_stdout_to_devnull(int *saved_stdout) {
	int devnull_fd = open("/dev/null", O_WRONLY);
	if (devnull_fd == -1) {
		return -1;
	}

	*saved_stdout = dup(STDOUT_FILENO);
	if (*saved_stdout == -1) {
		close(devnull_fd);
		return -1;
	}
	if (dup2(devnull_fd, STDOUT_FILENO) == -1) {
		close(devnull_fd);
		close(*saved_stdout);
		return -1;
	}
	if (close(devnull_fd) == -1) {
		close(*saved_stdout);
		return -1;
	}

	return 0;
}

int restore_stdout(int saved_stdout) {
	int ret = dup2(saved_stdout, STDOUT_FILENO);
	int close_ret = close(saved_stdout);
	if (ret == -1 || close_ret == -1) {
		return -1;
	}
	return 0;
}

int redirect_stderr_to_devnull(int *saved_stderr) {
	int devnull_fd = open("/dev/null", O_WRONLY);
	if (devnull_fd == -1) {
		return -1;
	}

	(void)fflush(stderr);
	*saved_stderr = dup(STDERR_FILENO);
	if (*saved_stderr == -1) {
		close(devnull_fd);
		return -1;
	}
	if (dup2(devnull_fd, STDERR_FILENO) == -1) {
		close(devnull_fd);
		close(*saved_stderr);
		return -1;
	}
	if (close(devnull_fd) == -1) {
		close(*saved_stderr);
		return -1;
	}

	return 0;
}

int restore_stderr(int saved_stderr) {
	(void)fflush(stderr);
	int ret = dup2(saved_stderr, STDERR_FILENO);
	int close_ret = close(saved_stderr);
	if (ret == -1 || close_ret == -1) {
		return -1;
	}
	return 0;
}

int start_stdout_capture(struct stdoutCapture *capture) {
	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return -1;
	}

	capture->saved_stdout = dup(STDOUT_FILENO);
	if (capture->saved_stdout == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		close(capture->saved_stdout);
		return -1;
	}
	if (close(pipefd[1]) == -1) {
		close(pipefd[0]);
		close(capture->saved_stdout);
		return -1;
	}

	capture->read_fd = pipefd[0];
	return 0;
}

char *read_all_fd(int fd, size_t *len_out) {
	size_t cap = 256;
	size_t len = 0;
	char *buf = malloc(cap + 1);
	if (buf == NULL) {
		return NULL;
	}

	for (;;) {
		if (len == cap) {
			size_t new_cap = cap * 2;
			char *new_buf = realloc(buf, new_cap + 1);
			if (new_buf == NULL) {
				free(buf);
				return NULL;
			}
			buf = new_buf;
			cap = new_cap;
		}

		ssize_t nread = read(fd, buf + len, cap - len);
		if (nread == 0) {
			break;
		}
		if (nread < 0) {
			if (errno == EINTR) {
				continue;
			}
			free(buf);
			return NULL;
		}
		len += (size_t)nread;
	}

	buf[len] = '\0';
	*len_out = len;
	return buf;
}

char *stop_stdout_capture(struct stdoutCapture *capture, size_t *len_out) {
	if (dup2(capture->saved_stdout, STDOUT_FILENO) == -1) {
		close(capture->saved_stdout);
		close(capture->read_fd);
		return NULL;
	}
	if (close(capture->saved_stdout) == -1) {
		close(capture->read_fd);
		return NULL;
	}

	char *output = read_all_fd(capture->read_fd, len_out);
	close(capture->read_fd);
	return output;
}

char *read_file_contents(const char *path, size_t *len_out) {
	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		return NULL;
	}
	char *contents = read_all_fd(fd, len_out);
	close(fd);
	return contents;
}

void testHelpersInitPaths(const char *startup_cwd) {
	free(g_test_repo_root);
	g_test_repo_root = NULL;

	if (startup_cwd == NULL) {
		return;
	}

	g_test_repo_root = strdup(startup_cwd);
}

char *testResolveRepoPath(const char *relative_path) {
	if (g_test_repo_root == NULL || relative_path == NULL || relative_path[0] == '\0') {
		return NULL;
	}

	size_t root_len = strlen(g_test_repo_root);
	size_t rel_len = strlen(relative_path);
	int needs_slash = root_len > 0 && g_test_repo_root[root_len - 1] != '/';
	size_t total_len = root_len + (size_t)needs_slash + rel_len;
	char *path = malloc(total_len + 1);
	if (path == NULL) {
		return NULL;
	}

	memcpy(path, g_test_repo_root, root_len);
	if (needs_slash) {
		path[root_len] = '/';
	}
	memcpy(path + root_len + (size_t)needs_slash, relative_path, rel_len);
	path[total_len] = '\0';
	return path;
}

int copyTestFixtureToPath(const char *fixture_relative_path, const char *target_path) {
	if (target_path == NULL) {
		return 0;
	}

	char *fixture_path = testResolveRepoPath(fixture_relative_path);
	if (fixture_path == NULL) {
		return 0;
	}

	size_t content_len = 0;
	char *contents = read_file_contents(fixture_path, &content_len);
	free(fixture_path);
	if (contents == NULL) {
		return 0;
	}

	int fd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		free(contents);
		return 0;
	}

	int write_ok = write_all(fd, contents, content_len) == 0;
	free(contents);
	int close_ok = close(fd) == 0;
	return write_ok && close_ok;
}

int editor_read_key_with_input(const char *input, size_t len, int *key_out) {
	int saved_stdin;
	if (setup_stdin_bytes(input, len, &saved_stdin) == -1) {
		return -1;
	}

	*key_out = editorReadKey();

	if (restore_stdin(saved_stdin) == -1) {
		return -1;
	}
	return 0;
}

int editor_process_keypress_with_input(const char *input, size_t len) {
	int saved_stdin;
	if (setup_stdin_bytes(input, len, &saved_stdin) == -1) {
		return -1;
	}

	/* Always dispatch at least once: empty-input callers (the EOF tests)
	 * rely on a single editorProcessKeypress to read EOF and trigger
	 * editorExitOnInputShutdown. After that, editorReadKey can return
	 * *_EVENT without consuming the queued byte if task/syntax polling
	 * fires first; loop while bytes remain so every queued byte gets a
	 * real keypress dispatch. FIONREAD distinguishes "bytes still
	 * queued" from "EOF only" — stopping on the latter avoids retriggering
	 * the EOF-exit path. */
	do {
		editorProcessKeypress();
		int avail = 0;
		if (ioctl(STDIN_FILENO, FIONREAD, &avail) == -1 || avail <= 0) {
			break;
		}
	} while (1);

	if (restore_stdin(saved_stdin) == -1) {
		return -1;
	}
	return 0;
}

char *editor_prompt_with_input(const char *input, size_t len, const char *prompt) {
	int saved_stdin;
	int saved_stdout;
	if (setup_stdin_bytes(input, len, &saved_stdin) == -1) {
		return NULL;
	}
	if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
		restore_stdin(saved_stdin);
		return NULL;
	}

	char *result = editorPrompt(prompt);

	restore_stdout(saved_stdout);
	restore_stdin(saved_stdin);
	return result;
}

char *refresh_screen_and_capture(size_t *len_out) {
	struct stdoutCapture capture;
	if (start_stdout_capture(&capture) == -1) {
		return NULL;
	}

	editorLspPumpNotifications();
	editorDapPumpNotifications();
	editorViewportUpdateForFrame();
	editorRefreshScreen();

	return stop_stdout_capture(&capture, len_out);
}

static char *editor_test_dup_line(const struct editorDocument *document, int cy) {
	/* Uses libc malloc directly so the alloc-failure probe in OOM tests does
	 * not affect post-failure row reads.
	 */
	size_t len = 0;
	const char *bytes = editorDocumentLineBytes(document, cy, &len);
	if (bytes != NULL) {
		char *out = (char *)malloc(len + 1);
		if (out == NULL) {
			return NULL;
		}
		memcpy(out, bytes, len);
		out[len] = '\0';
		return out;
	}
	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, cy, &start) ||
	    !editorDocumentLineEndByte(document, cy, &end)) {
		return NULL;
	}
	size_t span = end - start;
	char *out = (char *)malloc(span + 1);
	if (out == NULL) {
		return NULL;
	}
	if (span > 0 && !editorDocumentCopyRange(document, start, end, out)) {
		free(out);
		return NULL;
	}
	out[span] = '\0';
	return out;
}

char *editor_test_row_text(int cy) {
	return editor_test_dup_line(E.document, cy);
}

int editor_test_row_size(int cy) {
	return (int)editorDocumentLineLength(E.document, cy);
}

char *editor_test_tab_row_text(const struct editorTabState *tab, int cy) {
	if (tab == NULL) {
		return NULL;
	}
	return editor_test_dup_line(tab->document, cy);
}

static long long g_property_ops_total = 0;
static double g_property_ops_elapsed_seconds = 0.0;

void test_property_ops_reset(void) {
	g_property_ops_total = 0;
	g_property_ops_elapsed_seconds = 0.0;
}

void test_property_ops_record(long long ops, double elapsed_seconds) {
	if (ops < 0 || elapsed_seconds < 0.0) {
		return;
	}
	g_property_ops_total += ops;
	g_property_ops_elapsed_seconds += elapsed_seconds;
}

long long test_property_ops_total(void) {
	return g_property_ops_total;
}

double test_property_ops_elapsed_seconds(void) {
	return g_property_ops_elapsed_seconds;
}
