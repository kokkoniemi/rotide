#include "rotide.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "config/common.h"
#include "config/keymap.h"
#include "config/lsp_config.h"
#include "config/runtime_config.h"
#include "config/theme_config.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "language/syntax_worker.h"
#include "render/screen.h"
#include "support/terminal.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/workspace_state.h"

struct editorConfig E;

void initEditor(void) {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.wrapoff = 0;
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = 0;
	E.tab_title = NULL;
	E.cursor_offset = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.dirty = 0;
	E.filename = NULL;
	memset(&E.disk_state, 0, sizeof(E.disk_state));
	E.disk_conflict = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.syntax_state = NULL;
	E.syntax_parse_failures = 0;
	E.syntax_revision = 0;
	E.syntax_generation = 0;
	E.syntax_background_pending = 0;
	E.syntax_pending_revision = 0;
	E.syntax_pending_first_row = 0;
	E.syntax_pending_row_count = 0;
	editorLspConfigInitDefaults(&E.lsp_gopls_enabled, &E.lsp_clangd_enabled,
			&E.lsp_html_enabled, &E.lsp_css_enabled, &E.lsp_json_enabled,
			&E.lsp_javascript_enabled,
			&E.lsp_eslint_enabled, E.lsp_gopls_command, sizeof(E.lsp_gopls_command),
			E.lsp_gopls_install_command, sizeof(E.lsp_gopls_install_command),
			E.lsp_clangd_command, sizeof(E.lsp_clangd_command), E.lsp_html_command,
			sizeof(E.lsp_html_command), E.lsp_css_command, sizeof(E.lsp_css_command),
			E.lsp_json_command, sizeof(E.lsp_json_command), E.lsp_javascript_command,
			sizeof(E.lsp_javascript_command), E.lsp_javascript_install_command,
			sizeof(E.lsp_javascript_install_command),
			E.lsp_eslint_command, sizeof(E.lsp_eslint_command),
			E.lsp_vscode_langservers_install_command,
			sizeof(E.lsp_vscode_langservers_install_command),
			&E.lsp_autocomplete_enabled, &E.lsp_autocomplete_max_items);
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	E.lsp_eslint_doc_open = 0;
	E.lsp_eslint_doc_version = 0;
	E.lsp_diagnostics = NULL;
	E.lsp_diagnostic_count = 0;
	E.lsp_diagnostic_error_count = 0;
	E.lsp_diagnostic_warning_count = 0;
	E.lsp_symbols = NULL;
	E.lsp_symbol_count = 0;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.search_query = NULL;
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
	E.hover_link_active = 0;
	E.hover_link_row = -1;
	E.hover_link_cx_start = 0;
	E.hover_link_cx_end = 0;
	E.clipboard_text = NULL;
	E.clipboard_textlen = 0;
	E.clipboard_external_sink = NULL;
	E.undo_history.start = 0;
	E.undo_history.len = 0;
	E.redo_history.start = 0;
	E.redo_history.len = 0;
	memset(&E.edit_pending_entry, 0, sizeof(E.edit_pending_entry));
	E.edit_pending_entry_valid = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
	E.tabs = NULL;
	E.tab_count = 0;
	E.tab_capacity = 0;
	E.active_tab = 0;
	E.tab_view_start = 0;
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
	E.pane_focus = EDITOR_PANE_TEXT;
	E.layout_root = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_EDITOR);
	if (E.layout_root == NULL) {
		errno = ENOMEM;
		panic("editorPaneNodeNewLeaf");
	}
	E.focused_leaf = E.layout_root;
	editorKeymapInitDefaults(&E.keymap);
	editorClipboardSetExternalSink(editorClipboardSyncAll);
	if (!editorTabsInit()) {
		errno = ENOMEM;
		panic("editorTabsInit");
	}
	/* Seed the initial pane's tab-membership list with the bootstrap
	 * tab so subsequent open/close/cycle operations stay consistent. */
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split &&
			E.tab_count > 0) {
		(void)editorPaneViewAddTab(&E.focused_leaf->as.leaf.view,
				E.active_tab);
		E.focused_leaf->as.leaf.view.active_tab_idx = E.active_tab;
	}

	if (!editorRefreshWindowSize()) {
		panic("readWindowSize");
	}
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");
	setRawMode();
	initEditor();
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
	(void)editorWorkspaceStateLoadAndApply(E.window_cols);
	if (!restored_session && argc < 2) {
		(void)editorWorkspaceStateRestoreTabs();
	}
	(void)editorGitInit();

	if (E.statusmsg[0] == '\0') {
		char help_msg[160];
		editorKeymapBuildHelpStatus(&E.keymap, help_msg, sizeof(help_msg));
		editorSetStatusMsg("%s", help_msg);
	}

	while (1) {
		editorSyntaxBackgroundPoll();
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}
