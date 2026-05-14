#include "config/runtime_config.h"

#include "config/editor_config.h"
#include "config/keymap.h"
#include "config/dap_config.h"
#include "config/lsp_config.h"
#include "config/theme_config.h"
#include "editing/edit.h"
#include "language/lsp.h"

struct editorConfiguredSettingsStatus {
	enum editorConfigBootstrapStatus bootstrap_status;
	enum editorKeymapLoadStatus keymap_status;
	enum editorCursorStyleLoadStatus cursor_style_status;
	enum editorCursorBlinkLoadStatus cursor_blink_status;
	enum editorLineWrapLoadStatus line_wrap_status;
	enum editorLineNumbersLoadStatus line_numbers_status;
	enum editorCurrentLineHighlightLoadStatus current_line_highlight_status;
	enum editorNerdFontsLoadStatus nerd_fonts_status;
	enum editorIndentConfigLoadStatus indent_config_status;
	enum editorColumnSelectDragModifierLoadStatus column_select_drag_modifier_status;
	enum editorThemeLoadStatus theme_status;
	enum editorLspConfigLoadStatus lsp_config_status;
	enum editorDapConfigLoadStatus dap_config_status;
};

static void editorConfigLoadConfiguredSettings(
		struct editorConfiguredSettingsStatus *status) {
	status->keymap_status = editorKeymapLoadConfigured(&E.keymap);
	status->cursor_style_status = editorCursorStyleLoadConfigured(&E.cursor_style);
	status->cursor_blink_status = editorCursorBlinkLoadConfigured(&E.cursor_blink_enabled);
	status->line_wrap_status = editorLineWrapLoadConfigured(&E.line_wrap_enabled);
	status->line_numbers_status = editorLineNumbersLoadConfigured(&E.line_numbers_enabled);
	status->current_line_highlight_status = editorCurrentLineHighlightLoadConfigured(
			&E.current_line_highlight_enabled);
	status->nerd_fonts_status = editorNerdFontsLoadConfigured(&E.nerd_fonts_enabled);
	status->indent_config_status = editorIndentConfigLoadConfigured(&E.auto_indent_enabled,
			&E.indent_use_tabs, &E.indent_width);
	status->column_select_drag_modifier_status =
			editorColumnSelectDragModifierLoadConfigured(&E.column_select_drag_modifier);
	status->theme_status = editorThemeLoadConfigured(&E.theme);
	status->lsp_config_status = editorLspConfigLoadConfigured(&E.lsp_gopls_enabled,
			&E.lsp_clangd_enabled, &E.lsp_html_enabled, &E.lsp_css_enabled,
			&E.lsp_json_enabled, &E.lsp_javascript_enabled, &E.lsp_eslint_enabled,
			E.lsp_gopls_command, sizeof(E.lsp_gopls_command),
			E.lsp_gopls_install_command, sizeof(E.lsp_gopls_install_command),
			E.lsp_clangd_command, sizeof(E.lsp_clangd_command),
			E.lsp_html_command, sizeof(E.lsp_html_command),
			E.lsp_css_command, sizeof(E.lsp_css_command),
			E.lsp_json_command, sizeof(E.lsp_json_command),
			E.lsp_javascript_command, sizeof(E.lsp_javascript_command),
			E.lsp_javascript_install_command, sizeof(E.lsp_javascript_install_command),
			E.lsp_eslint_command, sizeof(E.lsp_eslint_command),
			E.lsp_vscode_langservers_install_command,
			sizeof(E.lsp_vscode_langservers_install_command),
			&E.lsp_autocomplete_enabled, &E.lsp_autocomplete_max_items);
	status->dap_config_status = editorDapConfigLoadConfiguredGlobal();
}

static int editorConfigSetConfiguredSettingsStatus(
		const struct editorConfiguredSettingsStatus *status,
		const char *success_status) {
	if (status->keymap_status == EDITOR_KEYMAP_LOAD_INVALID_PROJECT) {
		editorSetStatusMsg("Invalid keymap config, using defaults");
		return 1;
	}
	if (status->keymap_status == EDITOR_KEYMAP_LOAD_INVALID_GLOBAL) {
		editorSetStatusMsg("Invalid global keymap config, ignoring ~/.rotide/config.toml");
		return 1;
	}
	if (status->keymap_status == EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY ||
			(status->cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->cursor_blink_status & EDITOR_CURSOR_BLINK_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->line_wrap_status & EDITOR_LINE_WRAP_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->line_numbers_status & EDITOR_LINE_NUMBERS_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->current_line_highlight_status &
					EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->nerd_fonts_status & EDITOR_NERD_FONTS_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->indent_config_status & EDITOR_INDENT_CONFIG_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->column_select_drag_modifier_status &
					EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->theme_status & EDITOR_THEME_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->lsp_config_status & EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY) != 0 ||
			(status->dap_config_status & EDITOR_DAP_CONFIG_LOAD_OUT_OF_MEMORY) != 0) {
		editorSetStatusMsg("Out of memory");
		return 1;
	}
	if ((status->dap_config_status & EDITOR_DAP_CONFIG_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid [dap] in ~/.rotide/config.toml, ignoring DAP config");
		return 1;
	}
	if ((status->lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL) != 0 &&
			(status->lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [lsp] in global/project config, using defaults");
		return 1;
	}
	if ((status->lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [lsp] in ./.rotide.toml, using defaults");
		return 1;
	}
	if ((status->lsp_config_status & EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid [lsp] in ~/.rotide/config.toml, using defaults");
		return 1;
	}
	if ((status->cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL) != 0 &&
			(status->cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid cursor_style in global/project config, using bar");
		return 1;
	}
	if ((status->cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid cursor_style in ./.rotide.toml, using bar");
		return 1;
	}
	if ((status->cursor_style_status & EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid cursor_style in ~/.rotide/config.toml, using bar");
		return 1;
	}
	if ((status->cursor_blink_status & EDITOR_CURSOR_BLINK_LOAD_INVALID_GLOBAL) != 0 &&
			(status->cursor_blink_status & EDITOR_CURSOR_BLINK_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid cursor_blink in global/project config, using true");
		return 1;
	}
	if ((status->cursor_blink_status & EDITOR_CURSOR_BLINK_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid cursor_blink in ./.rotide.toml, using true");
		return 1;
	}
	if ((status->cursor_blink_status & EDITOR_CURSOR_BLINK_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid cursor_blink in ~/.rotide/config.toml, using true");
		return 1;
	}
	if ((status->line_wrap_status & EDITOR_LINE_WRAP_LOAD_INVALID_GLOBAL) != 0 &&
			(status->line_wrap_status & EDITOR_LINE_WRAP_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid line_wrap in global/project config, using false");
		return 1;
	}
	if ((status->line_wrap_status & EDITOR_LINE_WRAP_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid line_wrap in ./.rotide.toml, using false");
		return 1;
	}
	if ((status->line_wrap_status & EDITOR_LINE_WRAP_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid line_wrap in ~/.rotide/config.toml, using false");
		return 1;
	}
	if ((status->line_numbers_status & EDITOR_LINE_NUMBERS_LOAD_INVALID_GLOBAL) != 0 &&
			(status->line_numbers_status & EDITOR_LINE_NUMBERS_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid line_numbers in global/project config, using true");
		return 1;
	}
	if ((status->line_numbers_status & EDITOR_LINE_NUMBERS_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid line_numbers in ./.rotide.toml, using true");
		return 1;
	}
	if ((status->line_numbers_status & EDITOR_LINE_NUMBERS_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid line_numbers in ~/.rotide/config.toml, using true");
		return 1;
	}
	if ((status->current_line_highlight_status &
					EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_GLOBAL) != 0 &&
			(status->current_line_highlight_status &
					EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg(
				"Invalid current_line_highlight in global/project config, using true");
		return 1;
	}
	if ((status->current_line_highlight_status &
					EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid current_line_highlight in ./.rotide.toml, using true");
		return 1;
	}
	if ((status->current_line_highlight_status &
					EDITOR_CURRENT_LINE_HIGHLIGHT_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg(
				"Invalid current_line_highlight in ~/.rotide/config.toml, using true");
		return 1;
	}
	if ((status->nerd_fonts_status & EDITOR_NERD_FONTS_LOAD_INVALID_GLOBAL) != 0 &&
			(status->nerd_fonts_status & EDITOR_NERD_FONTS_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid nerd_fonts in global/project config, using false");
		return 1;
	}
	if ((status->nerd_fonts_status & EDITOR_NERD_FONTS_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid nerd_fonts in ./.rotide.toml, using false");
		return 1;
	}
	if ((status->nerd_fonts_status & EDITOR_NERD_FONTS_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid nerd_fonts in ~/.rotide/config.toml, using false");
		return 1;
	}
	if ((status->indent_config_status & EDITOR_INDENT_CONFIG_LOAD_INVALID_GLOBAL) != 0 &&
			(status->indent_config_status & EDITOR_INDENT_CONFIG_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid indent config in global/project config, using defaults");
		return 1;
	}
	if ((status->indent_config_status & EDITOR_INDENT_CONFIG_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid indent config in ./.rotide.toml, using defaults");
		return 1;
	}
	if ((status->indent_config_status & EDITOR_INDENT_CONFIG_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid indent config in ~/.rotide/config.toml, using defaults");
		return 1;
	}
	if ((status->column_select_drag_modifier_status &
					EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_GLOBAL) != 0 &&
			(status->column_select_drag_modifier_status &
					EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg(
				"Invalid column_select_drag_modifier in global/project config, using alt");
		return 1;
	}
	if ((status->column_select_drag_modifier_status &
					EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg(
				"Invalid column_select_drag_modifier in ./.rotide.toml, using alt");
		return 1;
	}
	if ((status->column_select_drag_modifier_status &
					EDITOR_COLUMN_SELECT_DRAG_MODIFIER_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg(
				"Invalid column_select_drag_modifier in ~/.rotide/config.toml, using alt");
		return 1;
	}
	if ((status->theme_status & EDITOR_THEME_LOAD_INVALID_THEME) != 0) {
		editorSetStatusMsg("Invalid theme, using terminal");
		return 1;
	}
	if ((status->theme_status & EDITOR_THEME_LOAD_INVALID_GLOBAL) != 0 &&
			(status->theme_status & EDITOR_THEME_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [theme] in global/project config, using terminal");
		return 1;
	}
	if ((status->theme_status & EDITOR_THEME_LOAD_INVALID_PROJECT) != 0) {
		editorSetStatusMsg("Invalid [theme] in ./.rotide.toml, using terminal");
		return 1;
	}
	if ((status->theme_status & EDITOR_THEME_LOAD_INVALID_GLOBAL) != 0) {
		editorSetStatusMsg("Invalid [theme] in ~/.rotide/config.toml, using terminal");
		return 1;
	}
	if (status->bootstrap_status == EDITOR_CONFIG_BOOTSTRAP_CREATED) {
		editorSetStatusMsg("Created ~/.rotide/config.toml with default values");
		return 1;
	}
	if (status->bootstrap_status == EDITOR_CONFIG_BOOTSTRAP_FAILED) {
		editorSetStatusMsg("Could not create ~/.rotide/config.toml, using defaults");
		return 1;
	}
	if (success_status != NULL) {
		editorSetStatusMsg("%s", success_status);
		return 1;
	}
	return 0;
}

void editorConfigApplyConfiguredSettings(
		enum editorConfigBootstrapStatus bootstrap_status, const char *success_status) {
	struct editorConfiguredSettingsStatus status = {
		.bootstrap_status = bootstrap_status,
	};
	editorConfigLoadConfiguredSettings(&status);
	(void)editorConfigSetConfiguredSettingsStatus(&status, success_status);
}

void editorConfigReloadConfiguredSettings(void) {
	editorLspShutdown();
	editorLspResetTrackedDocuments();
	editorConfigApplyConfiguredSettings(EDITOR_CONFIG_BOOTSTRAP_OK, "Settings reloaded");
	editorLspEnsureActiveDocumentTracked();
}
