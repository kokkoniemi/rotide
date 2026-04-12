#include "rotide.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "config/editor_config.h"
#include "config/keymap.h"
#include "config/lsp_config.h"
#include "config/theme_config.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "render/screen.h"
#include "support/terminal.h"
#include "workspace/drawer.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"

struct editorConfig E;

void initEditor(void) {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = 0;
	E.tab_title = NULL;
	E.cursor_offset = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.syntax_state = NULL;
	editorLspConfigInitDefaults(&E.lsp_enabled, E.lsp_gopls_command,
			sizeof(E.lsp_gopls_command), E.lsp_gopls_install_command,
			sizeof(E.lsp_gopls_install_command));
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.search_query = NULL;
	E.search_match_offset = 0;
	E.search_match_len = 0;
	E.search_direction = 1;
	E.search_saved_offset = 0;
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_anchor_offset = 0;
	E.mouse_drag_started = 0;
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
	E.recovery_last_autosave_time = 0;
	E.drawer_root_path = NULL;
	E.drawer_root = NULL;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	E.drawer_width_cols = ROTIDE_DRAWER_DEFAULT_WIDTH;
	E.drawer_width_user_set = 0;
	E.drawer_collapsed = 0;
	E.drawer_resize_active = 0;
	E.cursor_style = EDITOR_CURSOR_STYLE_BAR;
	editorSyntaxThemeInitDefaults(E.syntax_theme);
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	E.pane_focus = EDITOR_PANE_TEXT;
	editorKeymapInitDefaults(&E.keymap);
	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	if (!editorTabsInit()) {
		errno = ENOMEM;
		panic("editorTabsInit");
	}

	if (!editorRefreshWindowSize()) {
		panic("readWindowSize");
	}
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");
	setRawMode();
	initEditor();

	enum editorKeymapLoadStatus keymap_status = editorKeymapLoadConfigured(&E.keymap);
	enum editorCursorStyleLoadStatus cursor_style_status =
			editorCursorStyleLoadConfigured(&E.cursor_style);
	enum editorSyntaxThemeLoadStatus syntax_theme_status =
			editorSyntaxThemeLoadConfigured(E.syntax_theme);
	enum editorLspConfigLoadStatus lsp_config_status =
			editorLspConfigLoadConfigured(&E.lsp_enabled, E.lsp_gopls_command,
					sizeof(E.lsp_gopls_command), E.lsp_gopls_install_command,
					sizeof(E.lsp_gopls_install_command));
	if (!editorRecoveryInitForCurrentDir()) {
		editorSetStatusMsg("Recovery disabled (path setup failed)");
	}
	if (keymap_status == EDITOR_KEYMAP_LOAD_INVALID_PROJECT) {
		editorSetStatusMsg("Invalid keymap config, using defaults");
	} else if (keymap_status == EDITOR_KEYMAP_LOAD_INVALID_GLOBAL) {
		editorSetStatusMsg("Invalid global keymap config, ignoring ~/.rotide/config.toml");
	} else if (keymap_status == EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY ||
			(cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY) != 0 ||
			(syntax_theme_status & EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY) != 0 ||
			(lsp_config_status & EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY) != 0) {
		editorSetStatusMsg("Out of memory");
	} else if ((lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL) != 0 &&
			(lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [lsp] in global/project config, using defaults");
	} else if ((lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [lsp] in ./.rotide.toml, using defaults");
	} else if ((lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid [lsp] in ~/.rotide/config.toml, using defaults");
	} else if ((cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL) != 0 &&
			(cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid cursor_style in global/project config, using bar");
	} else if ((cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid cursor_style in ./.rotide.toml, using bar");
	} else if ((cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid cursor_style in ~/.rotide/config.toml, using bar");
	} else if ((syntax_theme_status & EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL) != 0 &&
			(syntax_theme_status & EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [theme.syntax] in global/project config, using defaults");
	} else if ((syntax_theme_status & EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [theme.syntax] in ./.rotide.toml, using defaults");
	} else if ((syntax_theme_status & EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid [theme.syntax] in ~/.rotide/config.toml, using defaults");
	}

	int restored_session = editorStartupLoadRecoveryOrOpenArgs(argc, argv);
	if (!editorDrawerInitForStartup(argc, argv, restored_session)) {
		editorSetStatusMsg("Drawer disabled (init failed)");
	}

	if (keymap_status == EDITOR_KEYMAP_LOAD_OK && E.statusmsg[0] == '\0') {
		char help_msg[160];
		editorKeymapBuildHelpStatus(&E.keymap, help_msg, sizeof(help_msg));
		editorSetStatusMsg("%s", help_msg);
	}

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}
