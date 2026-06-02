#include "rotide.h"

#include "config/common.h"
#include "config/keymap.h"
#include "config/lsp_config.h"
#include "config/runtime_config.h"
#include "config/theme_config.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "language/lsp.h"
#include "language/syntax_worker.h"
#include "render/screen.h"
#include "render/viewport.h"
#include "support/perf_trace.h"
#include "support/terminal.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/workspace_state.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

struct editorConfig E;

/* --render-once: non-interactive single-frame render mode. Skips raw
 * mode and the TTY window-size probe, uses fixed 80x24 dimensions,
 * runs one editorRefreshScreen, and exits. Useful for any caller that
 * wants a deterministic one-shot render without a controlling
 * terminal (headless capture, docs screenshots, cold-open timing). */
static int g_render_once = 0;
#define RENDER_ONCE_DEFAULT_COLS 80
#define RENDER_ONCE_DEFAULT_ROWS 24

void editorInit(void) {
	editorResetActiveBufferFields();
	editorLspConfigInitDefaults(&E.lsp_config);
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.hover_link_active = 0;
	E.hover_link_row = -1;
	E.hover_link_cx_start = 0;
	E.hover_link_cx_end = 0;
	E.clipboard_text = NULL;
	E.clipboard_textlen = 0;
	E.clipboard_external_sink = NULL;
	E.tabs = NULL;
	E.tab_count = 0;
	E.tab_capacity = 0;
	E.active_tab = 0;
	E.close_confirmed = 0;
	E.task_pid = 0;
	E.task_output_fd = -1;
	E.task_running = 0;
	E.task_tab_idx = -1;
	E.task_output_truncated = 0;
	E.task_output_bytes = 0;
	E.task_exit_code = 0;
	E.task_success_status[0] = '\0';
	E.task_failure_status[0] = '\0';
	E.recovery_path = NULL;
	E.workspace_state_path = NULL;
	E.recovery_last_autosave_time = 0;
	E.drawer_root_path = NULL;
	E.drawer_root = NULL;
	E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
	E.drawer_menu_expanded = 0;
	E.drawer_git_expanded = 0;
	E.drawer_lsp_expanded = 0;
	E.drawer_dap_expanded = 0;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	E.text_last_click_cy = -1;
	E.text_last_click_cx = -1;
	E.text_last_click_ms = 0;
	E.text_click_count = 0;
	E.tab_last_click_idx = -1;
	E.tab_last_click_ms = 0;
	E.drawer_width_cols = ROTIDE_DRAWER_DEFAULT_WIDTH;
	E.drawer_width_user_set = 0;
	E.drawer_collapsed = 0;
	E.drawer_resize_active = 0;
	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	E.drawer_search_query = NULL;
	E.drawer_search_query_len = 0;
	E.drawer_search_paths = NULL;
	E.drawer_search_path_count = 0;
	E.drawer_search_path_capacity = 0;
	E.drawer_search_filtered_indices = NULL;
	E.drawer_search_filtered_count = 0;
	E.drawer_search_filtered_capacity = 0;
	E.drawer_search_previewed_path = NULL;
	E.drawer_search_active_tab_before = -1;
	E.drawer_search_restore_collapsed = 0;
	E.recent_file_paths = NULL;
	E.recent_file_count = 0;
	E.recent_file_capacity = 0;
	E.drawer_project_search_query = NULL;
	E.drawer_project_search_query_len = 0;
	E.drawer_project_search_results = NULL;
	E.drawer_project_search_result_count = 0;
	E.drawer_project_search_result_capacity = 0;
	E.drawer_project_search_previewed_path = NULL;
	E.drawer_project_search_previewed_line = 0;
	E.drawer_project_search_previewed_col = 0;
	E.drawer_project_search_active_tab_before = -1;
	E.drawer_project_search_restore_collapsed = 0;
	E.git_repo_root = NULL;
	E.git_branch = NULL;
	E.git_entries = NULL;
	E.git_entry_count = 0;
	E.git_entry_capacity = 0;
	E.cursor_style = EDITOR_CURSOR_STYLE_BAR;
	E.cursor_blink_enabled = 1;
	E.line_wrap_enabled = 0;
	E.line_numbers_enabled = 1;
	E.current_line_highlight_enabled = 1;
	E.nerd_fonts_enabled = 0;
	E.auto_indent_enabled = 0;
	E.indent_use_tabs = 0;
	E.indent_width = ROTIDE_INDENT_WIDTH_DEFAULT;
	E.column_select_drag_modifier = EDITOR_MOUSE_MOD_ALT;
	editorThemeInitDefault(&E.theme);
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (E.layout_root == NULL) {
		errno = ENOMEM;
		editorPanic("editorPaneNodeNewLeaf");
	}
	E.focused_leaf = E.layout_root;
	editorKeymapInitDefaults(&E.keymap);
	editorClipboardSetExternalSink(editorClipboardSyncAll);
	if (!editorTabsInit()) {
		errno = ENOMEM;
		editorPanic("editorTabsInit");
	}
	/* Seed the initial pane's tab-membership list with the bootstrap
	 * tab so subsequent open/close/cycle operations stay consistent. */
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split && E.tab_count > 0) {
		(void)editorPaneViewAddTab(&E.focused_leaf->as.leaf.view, E.active_tab);
		E.focused_leaf->as.leaf.view.active_tab_idx = E.active_tab;
	}

	if (g_render_once) {
		/* Skip the TIOCGWINSZ / cursor-position-query path entirely so
		 * stdin/stdout don't need to be a TTY. Match the text-rows math
		 * in editorRefreshWindowSize (subtract 3 for chrome). */
		E.window_cols = RENDER_ONCE_DEFAULT_COLS;
		E.window_rows = RENDER_ONCE_DEFAULT_ROWS - 3;
		if (E.window_rows < 1) {
			E.window_rows = 1;
		}
	} else if (!editorRefreshWindowSize()) {
		editorPanic("editorReadWindowSize");
	}
}

/* Strip the first occurrence of `flag` from argv in place. Returns 1
 * if the flag was present, 0 otherwise. Lets us extract a leading
 * mode flag without forcing the rest of the argv parsing through a
 * general option parser. */
static int rotideStripFlag(int *argc, char *argv[], const char *flag) {
	for (int i = 1; i < *argc; i++) {
		if (strcmp(argv[i], flag) == 0) {
			for (int j = i; j < *argc - 1; j++) {
				argv[j] = argv[j + 1];
			}
			(*argc)--;
			argv[*argc] = NULL;
			return 1;
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	(void)setlocale(LC_CTYPE, "");

	g_render_once = rotideStripFlag(&argc, argv, "--render-once");

	editorPerfInit();
	if (!g_render_once) {
		editorSetRawMode();
	}
	editorInit();
	if (!editorSyntaxBackgroundStart()) {
		editorSetStatusMsg("Tree-sitter background worker disabled");
	}

	enum editorConfigBootstrapStatus bootstrap_status = editorConfigEnsureGlobalConfig();

	if (!editorRecoveryInitForCurrentDir()) {
		editorSetStatusMsg("Recovery disabled (path setup failed)");
	}
	(void)editorWorkspaceStateInitForCurrentDir();
	editorConfigApplyConfiguredSettings(bootstrap_status, NULL);

	int restored_session = editorStartupLoadRecoveryOrOpenArgs(argc, argv);
	if (!editorDrawerInitForStartup(argc, argv, restored_session)) {
		editorSetStatusMsg("Drawer disabled (init failed)");
	}
	/* When a file argument is given on startup we keep the fresh single-pane
	 * layout the open path built — restoring the persisted pane tree would
	 * resurrect terminal placeholders without hydrating them, leaving an
	 * empty terminal pane that steals nothing but renders dead. */
	int reset_panes = !restored_session && argc >= 2;
	(void)editorWorkspaceStateLoadAndApply(E.window_cols, reset_panes);
	if (!restored_session && argc < 2) {
		(void)editorWorkspaceStateRestoreTabs();
	}
	(void)editorGitInit();

	if (E.statusmsg[0] == '\0') {
		char help_msg[160];
		editorKeymapBuildHelpStatus(&E.keymap, help_msg, sizeof(help_msg));
		editorSetStatusMsg("%s", help_msg);
	}

	if (g_render_once) {
		/* Single-frame render path: viewport update + one refresh, then
		 * exit. Skip the background-worker pumps because the contract
		 * is "render what the editor would draw on the first frame",
		 * not "render after async workers have caught up". */
		editorViewportUpdateForFrame();
		editorRefreshScreen();
		return EXIT_SUCCESS;
	}

	while (1) {
		editorPerfBeginFrame();
		editorSyntaxBackgroundPoll();
		editorLspPumpNotifications();
		editorDapPumpNotifications();
		editorViewportUpdateForFrame();
		long refresh_t0 = editorPerfEnabled() ? editorPerfMonotonicUs() : 0;
		editorRefreshScreen();
		if (editorPerfEnabled()) {
			editorPerfRecordRefreshUs(editorPerfMonotonicUs() - refresh_t0);
		}
		editorMarkFrameRendered();
		editorPerfEndFrame();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}
