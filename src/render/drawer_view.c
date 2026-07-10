#include "render/drawer_view.h"

#include "config/theme_config.h"
#include "debug/dap.h"
#include "language/syntax.h"
#include "render/ansi_style.h"
#include "render/display_text.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define VT100_BOLD_ON_4 "\x1b[1m"
#define VT100_BOLD_OFF_5 "\x1b[22m"
#define DRAWER_SPLITTER_UTF8 "\xE2\x94\x82"
#define DRAWER_CARET_EXPANDED_UTF8 "\xE2\x96\xBE"
#define DRAWER_CARET_COLLAPSED_UTF8 "\xE2\x96\xB8"
#define DRAWER_TREE_BRANCH_MID_UTF8 "\xE2\x94\x9C"
#define DRAWER_TREE_BRANCH_LAST_UTF8 "\xE2\x94\x94"
#define DRAWER_TREE_HORIZONTAL_UTF8 "\xE2\x94\x80"
#define DRAWER_COLLAPSE_INDICATOR "\xE2\x80\xB9"
#define DRAWER_EXPAND_INDICATOR "\xE2\x80\xBA"
#define DRAWER_HEADER_EXPLORER_SYMBOL_UTF8 "E"
#define DRAWER_HEADER_FILE_SEARCH_SYMBOL_UTF8 "F"
#define DRAWER_HEADER_PROJECT_SEARCH_SYMBOL_UTF8 "/"
#define DRAWER_HEADER_LSP_SYMBOL_UTF8 "L"
#define DRAWER_HEADER_DAP_SYMBOL_UTF8 "D"
#define DRAWER_HEADER_GIT_SYMBOL_UTF8 "\xE2\x91\x82"
#define DRAWER_HEADER_MAIN_MENU_SYMBOL_UTF8 "\xE2\x89\xA1"
#define DRAWER_NERD_FOLDER_UTF8 "\xEF\x81\xBB"
#define DRAWER_NERD_FILE_UTF8 "\xEF\x85\x9B"
#define DRAWER_NERD_FILE_TEXT_UTF8 "\xEF\x85\x9C"
#define DRAWER_NERD_FILE_CODE_UTF8 "\xEF\x87\x89"
#define DRAWER_NERD_FILE_IMAGE_UTF8 "\xEF\x87\x85"
#define DRAWER_NERD_FILE_ARCHIVE_UTF8 "\xEF\x87\x86"
#define DRAWER_NERD_FILE_PDF_UTF8 "\xEF\x87\x81"
#define DRAWER_NERD_FILE_AUDIO_UTF8 "\xEF\x87\x87"
#define DRAWER_NERD_FILE_VIDEO_UTF8 "\xEF\x87\x88"
#define DRAWER_NERD_GEAR_UTF8 "\xEF\x80\x93"
#define DRAWER_NERD_SEARCH_UTF8 "\xEF\x80\x82"
#define DRAWER_NERD_TREE_UTF8 "\xEF\x83\xA8"
#define DRAWER_NERD_TERMINAL_UTF8 "\xEF\x84\xA0"
#define DRAWER_NERD_BUG_UTF8 "\xEF\x86\x88"
#define DRAWER_NERD_BRANCH_UTF8 "\xEF\x84\xA6"
#define DRAWER_NERD_BARS_UTF8 "\xEF\x83\x89"
#define DRAWER_NERD_SAVE_UTF8 "\xEF\x83\x87"
#define DRAWER_NERD_PLUS_UTF8 "\xEF\x81\xA7"
#define DRAWER_NERD_CLOSE_UTF8 "\xEF\x80\x8D"
#define DRAWER_NERD_EDIT_UTF8 "\xEF\x81\x84"
#define DRAWER_NERD_TRASH_UTF8 "\xEF\x87\xB8"
#define DRAWER_NERD_COPY_UTF8 "\xEF\x83\x85"
#define DRAWER_NERD_CUT_UTF8 "\xEF\x83\x84"
#define DRAWER_NERD_PASTE_UTF8 "\xEF\x83\xAA"
#define DRAWER_NERD_UNDO_UTF8 "\xEF\x83\xA2"
#define DRAWER_NERD_REDO_UTF8 "\xEF\x80\x9E"
#define DRAWER_NERD_ARROW_RIGHT_UTF8 "\xEF\x81\xA1"
#define DRAWER_NERD_ARROW_LEFT_UTF8 "\xEF\x81\xA0"
#define DRAWER_NERD_EYE_UTF8 "\xEF\x81\xAE"
#define DRAWER_NERD_LINE_CHART_UTF8 "\xEF\x88\x81"
#define DRAWER_NERD_PLAY_UTF8 "\xEF\x81\x8B"
#define DRAWER_NERD_CHECK_UTF8 "\xEF\x80\x8C"          /* U+F00C check */
#define DRAWER_NERD_HISTORY_UTF8 "\xEF\x87\x9A"        /* U+F1DA history */
#define DRAWER_NERD_ARCHIVE_BOX_UTF8 "\xEF\x86\x87"    /* U+F187 archive */
#define DRAWER_NERD_UPLOAD_UTF8 "\xEF\x82\x93"         /* U+F093 upload */
#define DRAWER_NERD_DOWNLOAD_UTF8 "\xEF\x80\x99"       /* U+F019 download */
#define DRAWER_NERD_CLOUD_DOWNLOAD_UTF8 "\xEF\x83\xAD" /* U+F0ED cloud-download */
#define DRAWER_NERD_REFRESH_UTF8 "\xEF\x80\xA1"        /* U+F021 refresh */
#define DRAWER_DAP_BREAKPOINT_UTF8 "\xE2\x97\x8F"
#define DRAWER_HEADER_MODE_BUTTON_COLS 3
#define DRAWER_HEADER_MODE_BUTTON_COUNT 7
#define DRAWER_HEADER_MODE_BUTTONS_MIN_COLS                                                        \
	(ROTIDE_DRAWER_COLLAPSED_WIDTH +                                                           \
	 DRAWER_HEADER_MODE_BUTTON_COLS * DRAWER_HEADER_MODE_BUTTON_COUNT)

static int drawerViewDrawHeaderCell(struct writeBuf *wb, const char *label, int active,
                                    int *written_cols, int drawer_cols) {
	if (label == NULL || written_cols == NULL || *written_cols >= drawer_cols) {
		return 1;
	}

	char text[16];
	int len = snprintf(text, sizeof(text), " %s ", label);
	if (len <= 0 || len >= (int)sizeof(text)) {
		return 0;
	}

	if (active) {
		if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_DRAWER_HEADER_ACTIVE)) {
			return 0;
		}
	} else if (!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_DRAWER_HEADER_BG)) {
		return 0;
	}

	int wrote = 0;
	if (!editorAppendSanitizedText(wb, text, drawer_cols - *written_cols, &wrote) ||
	    !editorAppendThemeReset(wb)) {
		return 0;
	}
	*written_cols += wrote;
	return 1;
}

static int drawerViewDrawCollapsedRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
	int written_cols = 0;
	if (row_idx == 0 &&
	    !drawerViewDrawHeaderCell(wb, DRAWER_EXPAND_INDICATOR, 0, &written_cols, drawer_cols)) {
		return 0;
	}

	while (written_cols < drawer_cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		written_cols++;
	}

	return 1;
}

static enum editorDrawerMode drawerViewActiveHeaderMode(void) {
	if (editorFileSearchIsActive()) {
		return EDITOR_DRAWER_MODE_FILE_SEARCH;
	}
	if (editorProjectSearchIsActive()) {
		return EDITOR_DRAWER_MODE_PROJECT_SEARCH;
	}
	return E.drawer_mode;
}

static const char *drawerViewHeaderSymbol(enum editorDrawerMode mode) {
	if (!E.nerd_fonts_enabled) {
		switch (mode) {
			case EDITOR_DRAWER_MODE_TREE:
				return DRAWER_HEADER_EXPLORER_SYMBOL_UTF8;
			case EDITOR_DRAWER_MODE_FILE_SEARCH:
				return DRAWER_HEADER_FILE_SEARCH_SYMBOL_UTF8;
			case EDITOR_DRAWER_MODE_PROJECT_SEARCH:
				return DRAWER_HEADER_PROJECT_SEARCH_SYMBOL_UTF8;
			case EDITOR_DRAWER_MODE_LSP:
				return DRAWER_HEADER_LSP_SYMBOL_UTF8;
			case EDITOR_DRAWER_MODE_DAP:
				return DRAWER_HEADER_DAP_SYMBOL_UTF8;
			case EDITOR_DRAWER_MODE_GIT:
				return DRAWER_HEADER_GIT_SYMBOL_UTF8;
			case EDITOR_DRAWER_MODE_MAIN_MENU:
				return DRAWER_HEADER_MAIN_MENU_SYMBOL_UTF8;
			default:
				return "";
		}
	}

	switch (mode) {
		case EDITOR_DRAWER_MODE_TREE:
			return DRAWER_NERD_FOLDER_UTF8;
		case EDITOR_DRAWER_MODE_FILE_SEARCH:
			return DRAWER_NERD_FILE_TEXT_UTF8;
		case EDITOR_DRAWER_MODE_PROJECT_SEARCH:
			return DRAWER_NERD_SEARCH_UTF8;
		case EDITOR_DRAWER_MODE_LSP:
			return DRAWER_NERD_TERMINAL_UTF8;
		case EDITOR_DRAWER_MODE_DAP:
			return DRAWER_NERD_BUG_UTF8;
		case EDITOR_DRAWER_MODE_GIT:
			return DRAWER_NERD_BRANCH_UTF8;
		case EDITOR_DRAWER_MODE_MAIN_MENU:
			return DRAWER_NERD_BARS_UTF8;
		default:
			return "";
	}
}

static int drawerViewDrawHeaderModeButton(struct writeBuf *wb, const char *label,
                                          enum editorDrawerMode mode,
                                          enum editorDrawerMode active_mode, int *written_cols,
                                          int drawer_cols) {
	return drawerViewDrawHeaderCell(wb, label, mode == active_mode, written_cols, drawer_cols);
}

static int drawerViewDrawExpandedHeaderRow(struct writeBuf *wb, int drawer_cols) {
	int written_cols = 0;
	if (!drawerViewDrawHeaderCell(wb, DRAWER_COLLAPSE_INDICATOR, 0, &written_cols,
	                              drawer_cols)) {
		return 0;
	}

	if (drawer_cols >= DRAWER_HEADER_MODE_BUTTONS_MIN_COLS) {
		enum editorDrawerMode active_mode = drawerViewActiveHeaderMode();
		if (!drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_TREE),
		            EDITOR_DRAWER_MODE_TREE, active_mode, &written_cols, drawer_cols) ||
		    !drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_FILE_SEARCH),
		            EDITOR_DRAWER_MODE_FILE_SEARCH, active_mode, &written_cols,
		            drawer_cols) ||
		    !drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_PROJECT_SEARCH),
		            EDITOR_DRAWER_MODE_PROJECT_SEARCH, active_mode, &written_cols,
		            drawer_cols) ||
		    !drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_LSP),
		            EDITOR_DRAWER_MODE_LSP, active_mode, &written_cols, drawer_cols) ||
		    !drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_DAP),
		            EDITOR_DRAWER_MODE_DAP, active_mode, &written_cols, drawer_cols) ||
		    !drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_GIT),
		            EDITOR_DRAWER_MODE_GIT, active_mode, &written_cols, drawer_cols) ||
		    !drawerViewDrawHeaderModeButton(
		            wb, drawerViewHeaderSymbol(EDITOR_DRAWER_MODE_MAIN_MENU),
		            EDITOR_DRAWER_MODE_MAIN_MENU, active_mode, &written_cols,
		            drawer_cols)) {
			return 0;
		}
	}

	if (written_cols < drawer_cols) {
		if (!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_DRAWER_HEADER_BG)) {
			return 0;
		}
		while (written_cols < drawer_cols) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			written_cols++;
		}
		if (!editorAppendThemeReset(wb)) {
			return 0;
		}
	}

	return 1;
}

static int drawerViewHasSuffixCaseInsensitive(const char *text, const char *suffix) {
	if (text == NULL || suffix == NULL) {
		return 0;
	}
	size_t text_len = strlen(text);
	size_t suffix_len = strlen(suffix);
	if (suffix_len > text_len) {
		return 0;
	}
	return strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

static const char *drawerViewNameForFileIcon(const struct editorDrawerEntryView *entry,
                                             const char *entry_name) {
	if (entry != NULL && entry->path != NULL && entry->path[0] != '\0') {
		return entry->path;
	}
	if (entry_name == NULL) {
		return "";
	}
	if (entry_name[0] != '\0' && entry_name[1] == ' ') {
		return entry_name + 2;
	}
	return entry_name;
}

static const char *drawerViewNerdIconForMenuLabel(const char *label) {
	if (label == NULL) {
		return NULL;
	}
	if (strcmp(label, "Main Menu") == 0 || strcmp(label, "Find") == 0 ||
	    strcmp(label, "File") == 0 || strcmp(label, "Tabs") == 0 ||
	    strcmp(label, "Edit") == 0 || strcmp(label, "View") == 0) {
		return NULL;
	}
	if (strcmp(label, "Find File") == 0 || strcmp(label, "Find in Buffer") == 0) {
		return DRAWER_NERD_SEARCH_UTF8;
	}
	if (strcmp(label, "Next Tab") == 0) {
		return DRAWER_NERD_ARROW_RIGHT_UTF8;
	}
	if (strcmp(label, "Previous Tab") == 0) {
		return DRAWER_NERD_ARROW_LEFT_UTF8;
	}
	if (strcmp(label, "Rename...") == 0 || strcmp(label, "Find & replace") == 0 ||
	    strcmp(label, "Toggle Comment") == 0) {
		return DRAWER_NERD_EDIT_UTF8;
	}
	if (strncmp(label, "Toggle ", 7) == 0) {
		return DRAWER_NERD_EYE_UTF8;
	}
	if (strcmp(label, "Save") == 0) {
		return DRAWER_NERD_SAVE_UTF8;
	}
	if (strcmp(label, "New Tab") == 0 || strcmp(label, "New File...") == 0 ||
	    strcmp(label, "New Folder...") == 0) {
		return DRAWER_NERD_PLUS_UTF8;
	}
	if (strcmp(label, "Close Tab") == 0 || strcmp(label, "Quit") == 0) {
		return DRAWER_NERD_CLOSE_UTF8;
	}
	if (strcmp(label, "Delete...") == 0 || strcmp(label, "Delete Selection") == 0) {
		return DRAWER_NERD_TRASH_UTF8;
	}
	if (strcmp(label, "Settings") == 0) {
		return DRAWER_NERD_GEAR_UTF8;
	}
	if (strcmp(label, "Project Files") == 0 || strcmp(label, "Collapse Drawer") == 0) {
		return DRAWER_NERD_FOLDER_UTF8;
	}
	if (strcmp(label, "Search Project Text") == 0) {
		return DRAWER_NERD_TREE_UTF8;
	}
	if (strncmp(label, "Go to ", 6) == 0) {
		return DRAWER_NERD_ARROW_RIGHT_UTF8;
	}
	if (strcmp(label, "Git Changes") == 0) {
		return DRAWER_NERD_BRANCH_UTF8;
	}
	if (strcmp(label, "LSP") == 0) {
		return DRAWER_NERD_TERMINAL_UTF8;
	}
	if (strcmp(label, "Debugger") == 0) {
		return DRAWER_NERD_BUG_UTF8;
	}
	if (strcmp(label, "Undo") == 0) {
		return DRAWER_NERD_UNDO_UTF8;
	}
	if (strcmp(label, "Redo") == 0) {
		return DRAWER_NERD_REDO_UTF8;
	}
	if (strcmp(label, "Copy Selection") == 0) {
		return DRAWER_NERD_COPY_UTF8;
	}
	if (strcmp(label, "Cut Selection") == 0) {
		return DRAWER_NERD_CUT_UTF8;
	}
	if (strcmp(label, "Paste") == 0) {
		return DRAWER_NERD_PASTE_UTF8;
	}
	if (strcmp(label, "Toggle Selection") == 0) {
		return DRAWER_NERD_LINE_CHART_UTF8;
	}
	return DRAWER_NERD_FILE_TEXT_UTF8;
}

static const char *drawerViewNerdIconForFileName(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return DRAWER_NERD_FILE_UTF8;
	}
	const char *slash = strrchr(name, '/');
	const char *base = slash != NULL ? slash + 1 : name;
	if (strcmp(base, "Makefile") == 0 || strcmp(base, "makefile") == 0 ||
	    drawerViewHasSuffixCaseInsensitive(base, ".c") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".h") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".cc") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".cpp") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".cxx") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".hpp") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".go") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".rs") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".js") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".jsx") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".ts") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".tsx") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".py") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".php") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".java") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".rb") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".cs") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".hs") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".ml") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".jl") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".scala") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".sh") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".bash") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".zsh")) {
		return DRAWER_NERD_FILE_CODE_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".toml") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".json") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".yaml") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".yml") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".xml") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".ini") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".conf") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".cfg") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".env")) {
		return DRAWER_NERD_GEAR_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".md") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".markdown") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".txt") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".log")) {
		return DRAWER_NERD_FILE_TEXT_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".png") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".jpg") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".jpeg") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".gif") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".svg") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".webp")) {
		return DRAWER_NERD_FILE_IMAGE_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".zip") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".tar") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".gz") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".bz2") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".xz")) {
		return DRAWER_NERD_FILE_ARCHIVE_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".pdf")) {
		return DRAWER_NERD_FILE_PDF_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".mp3") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".wav") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".flac")) {
		return DRAWER_NERD_FILE_AUDIO_UTF8;
	}
	if (drawerViewHasSuffixCaseInsensitive(base, ".mp4") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".mov") ||
	    drawerViewHasSuffixCaseInsensitive(base, ".webm")) {
		return DRAWER_NERD_FILE_VIDEO_UTF8;
	}
	return DRAWER_NERD_FILE_UTF8;
}

static const char *drawerViewNerdIconForGitActionLabel(const char *label) {
	static const struct {
		const char *label;
		const char *icon;
	} k_action_icons[] = {
	        {"Commit staged…", DRAWER_NERD_CHECK_UTF8},
	        {"Amend last commit…", DRAWER_NERD_EDIT_UTF8},
	        {"Branches", DRAWER_NERD_BRANCH_UTF8},
	        {"Commit log", DRAWER_NERD_HISTORY_UTF8},
	        {"Stashes", DRAWER_NERD_ARCHIVE_BOX_UTF8},
	        {"Push", DRAWER_NERD_UPLOAD_UTF8},
	        {"Pull", DRAWER_NERD_DOWNLOAD_UTF8},
	        {"Fetch", DRAWER_NERD_CLOUD_DOWNLOAD_UTF8},
	        {"Refresh", DRAWER_NERD_REFRESH_UTF8},
	};
	if (label == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(k_action_icons) / sizeof(k_action_icons[0]); i++) {
		if (strcmp(label, k_action_icons[i].label) == 0) {
			return k_action_icons[i].icon;
		}
	}
	return NULL;
}

static const char *drawerViewNerdIconForEntry(const struct editorDrawerEntryView *entry,
                                              const char *entry_name) {
	if (entry == NULL || entry->is_search_header || entry->is_placeholder ||
	    entry->icon_kind == EDITOR_DRAWER_ENTRY_ICON_NONE) {
		return NULL;
	}
	if (!E.nerd_fonts_enabled) {
		return NULL;
	}
	switch (entry->icon_kind) {
		case EDITOR_DRAWER_ENTRY_ICON_DAP_START:
			return DRAWER_NERD_PLAY_UTF8;
		case EDITOR_DRAWER_ENTRY_ICON_DAP_BREAKPOINT:
			return DRAWER_DAP_BREAKPOINT_UTF8;
		default:
			break;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return NULL;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return drawerViewNerdIconForMenuLabel(entry_name);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		if (entry->is_root) {
			return NULL;
		}
		const char *action_icon = drawerViewNerdIconForGitActionLabel(entry_name);
		if (action_icon != NULL) {
			return action_icon;
		}
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP && entry->is_root) {
		return NULL;
	}
	if (entry->is_dir) {
		return NULL;
	}
	return drawerViewNerdIconForFileName(drawerViewNameForFileIcon(entry, entry_name));
}

static int drawerViewIconColorForEntry(const struct editorDrawerEntryView *entry,
                                       struct editorThemeColor *color_out) {
	if (entry == NULL || color_out == NULL) {
		return 0;
	}
	switch (entry->icon_color) {
		case EDITOR_DRAWER_ENTRY_ICON_COLOR_DAP_START:
			*color_out = editorThemeResolveAnsi(EDITOR_THEME_ANSI_GREEN, 1);
			return 1;
		case EDITOR_DRAWER_ENTRY_ICON_COLOR_DAP_BREAKPOINT:
			switch (entry->dap_breakpoint_kind) {
				case EDITOR_DAP_BREAKPOINT_CONDITIONAL:
					*color_out =
					        editorThemeResolveAnsi(EDITOR_THEME_ANSI_YELLOW, 1);
					return 1;
				case EDITOR_DAP_BREAKPOINT_LOGPOINT:
					*color_out =
					        editorThemeResolveAnsi(EDITOR_THEME_ANSI_CYAN, 1);
					return 1;
				case EDITOR_DAP_BREAKPOINT_FUNCTION:
					*color_out = editorThemeResolveAnsi(
					        EDITOR_THEME_ANSI_MAGENTA, 1);
					return 1;
				case EDITOR_DAP_BREAKPOINT_DATA:
					*color_out =
					        editorThemeResolveAnsi(EDITOR_THEME_ANSI_BLUE, 1);
					return 1;
				default:
					*color_out = E.theme.ui[EDITOR_THEME_UI_BREAKPOINT];
					return 1;
			}
		default:
			return 0;
	}
}

static int drawerViewAppendNerdIcon(struct writeBuf *wb, const char *icon, int row_inverted,
                                    const struct editorThemeColor *color, int *written_cols,
                                    int drawer_cols, int *appended_out) {
	if (appended_out != NULL) {
		*appended_out = 0;
	}
	if (icon == NULL || written_cols == NULL || *written_cols >= drawer_cols) {
		return 1;
	}
	int icon_cols = editorDisplayTextCols(icon);
	if (icon_cols <= 0) {
		icon_cols = 1;
	}
	if (*written_cols + icon_cols > drawer_cols) {
		return 1;
	}
	if (!row_inverted) {
		if (color != NULL) {
			if (!editorAppendThemeForeground(wb, *color)) {
				return 0;
			}
		} else if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_DRAWER_ICON)) {
			return 0;
		}
	}
	if (!wbAppend(wb, icon, strlen(icon))) {
		return 0;
	}
	if (!row_inverted && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	*written_cols += icon_cols;
	if (appended_out != NULL) {
		*appended_out = 1;
	}
	return 1;
}

static int drawerViewAppendCell(struct writeBuf *wb, const char *text, size_t len,
                                int *written_cols, int drawer_cols) {
	if (written_cols == NULL || *written_cols >= drawer_cols) {
		return 1;
	}
	if (!wbAppend(wb, text, len)) {
		return 0;
	}
	(*written_cols)++;
	return 1;
}

static int drawerViewAppendSanitizedSpan(struct writeBuf *wb, const char *text, int row_inverted,
                                         const struct editorThemeColor *color, int *written_cols,
                                         int drawer_cols) {
	if (text == NULL || text[0] == '\0' || written_cols == NULL ||
	    *written_cols >= drawer_cols) {
		return 1;
	}
	if (!row_inverted && color != NULL && !editorAppendThemeForeground(wb, *color)) {
		return 0;
	}
	int wrote = 0;
	if (!editorAppendSanitizedText(wb, text, drawer_cols - *written_cols, &wrote)) {
		return 0;
	}
	if (!row_inverted && color != NULL && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	*written_cols += wrote;
	return 1;
}

static int drawerViewAppendGrayCell(struct writeBuf *wb, const char *text, size_t len,
                                    int *written_cols, int drawer_cols) {
	if (written_cols == NULL || *written_cols >= drawer_cols) {
		return 1;
	}
	if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_DRAWER_CONNECTOR) ||
	    !wbAppend(wb, text, len) || !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	(*written_cols)++;
	return 1;
}

static int drawerViewAppendConnectorCell(struct writeBuf *wb, const char *text, size_t len,
                                         int *written_cols, int drawer_cols, int use_gray) {
	if (use_gray) {
		return drawerViewAppendGrayCell(wb, text, len, written_cols, drawer_cols);
	}
	return drawerViewAppendCell(wb, text, len, written_cols, drawer_cols);
}

static int drawerViewDrawAncestorGuides(struct writeBuf *wb, int parent_visible_idx,
                                        int *written_cols, int drawer_cols, int gray_connectors) {
	if (parent_visible_idx < 0) {
		return 1;
	}

	struct editorDrawerEntryView parent_entry;
	if (!editorDrawerVisibleEntryView(parent_visible_idx, &parent_entry)) {
		return 1;
	}

	if (parent_entry.depth >= 2) {
		if (!drawerViewDrawAncestorGuides(wb, parent_entry.parent_visible_idx, written_cols,
		                                  drawer_cols, gray_connectors)) {
			return 0;
		}
		if (parent_entry.is_last_sibling) {
			if (!drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
				return 0;
			}
		} else {
			if (!drawerViewAppendConnectorCell(
			            wb, DRAWER_SPLITTER_UTF8, sizeof(DRAWER_SPLITTER_UTF8) - 1,
			            written_cols, drawer_cols, gray_connectors)) {
				return 0;
			}
		}
		if (!drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
			return 0;
		}
		if (!drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
			return 0;
		}
	}

	return 1;
}

static int drawerViewBuildAncestorGuidesPlain(struct writeBuf *wb, int parent_visible_idx) {
	if (parent_visible_idx < 0) {
		return 1;
	}

	struct editorDrawerEntryView parent_entry;
	if (!editorDrawerVisibleEntryView(parent_visible_idx, &parent_entry)) {
		return 1;
	}

	if (parent_entry.depth >= 2) {
		if (!drawerViewBuildAncestorGuidesPlain(wb, parent_entry.parent_visible_idx)) {
			return 0;
		}
		if (parent_entry.is_last_sibling) {
			if (!wbAppend(wb, "   ", 3)) {
				return 0;
			}
		} else if (!wbAppend(wb, DRAWER_SPLITTER_UTF8 "  ",
		                     sizeof(DRAWER_SPLITTER_UTF8) + 2 - 1)) {
			return 0;
		}
	}

	return 1;
}

static int drawerViewEntryHasPrefix(const struct editorDrawerEntryView *entry) {
	return entry != NULL && entry->prefix != NULL && entry->prefix[0] != '\0';
}

static int drawerViewEntryHasVariableDetails(const struct editorDrawerEntryView *entry) {
	return entry != NULL &&
	       ((entry->detail_type != NULL && entry->detail_type[0] != '\0') ||
	        (entry->detail_value != NULL && entry->detail_value[0] != '\0') ||
	        (entry->detail_reference != NULL && entry->detail_reference[0] != '\0') ||
	        (entry->detail_address != NULL && entry->detail_address[0] != '\0') ||
	        (entry->detail_preview != NULL && entry->detail_preview[0] != '\0'));
}

static int drawerViewLooksLikePointerType(const char *type) {
	return type != NULL && strchr(type, '*') != NULL;
}

static int drawerViewLooksLikeHexAddress(const char *value) {
	return value != NULL && value[0] == '0' && (value[1] == 'x' || value[1] == 'X') &&
	       value[2] != '\0';
}

static void drawerViewResolveVariableValueAddress(const struct editorDrawerEntryView *entry,
                                                  const char **shown_value,
                                                  const char **shown_address) {
	const char *value = entry->detail_value;
	const char *address = entry->detail_address;
	int has_preview = entry->detail_preview != NULL && entry->detail_preview[0] != '\0';
	int pointer_address = drawerViewLooksLikePointerType(entry->detail_type) &&
	                      drawerViewLooksLikeHexAddress(value);
	*shown_value = pointer_address ? "->" : value;
	*shown_address = pointer_address ? value : address;
	if (has_preview && value != NULL && strcmp(entry->detail_preview, value) == 0) {
		*shown_value = NULL;
	}
	if (has_preview && !pointer_address && drawerViewLooksLikeHexAddress(value)) {
		*shown_value = NULL;
		if (address == NULL || address[0] == '\0') {
			*shown_address = value;
		}
	}
}

static int drawerViewAppendPlainPrefix(struct writeBuf *wb, const char *prefix) {
	if (prefix == NULL || prefix[0] == '\0') {
		return 1;
	}
	return wbAppend(wb, prefix, strlen(prefix)) && wbAppend(wb, " ", 1);
}

static int drawerViewAppendPlainVariableDetails(struct writeBuf *wb,
                                                const struct editorDrawerEntryView *entry) {
	if (!drawerViewEntryHasVariableDetails(entry)) {
		return 1;
	}
	const char *shown_value = NULL;
	const char *shown_address = NULL;
	drawerViewResolveVariableValueAddress(entry, &shown_value, &shown_address);
	if ((entry->detail_type != NULL && entry->detail_type[0] != '\0' &&
	     (!wbAppend(wb, "  ", 2) ||
	      !wbAppend(wb, entry->detail_type, strlen(entry->detail_type)))) ||
	    (entry->detail_preview != NULL && entry->detail_preview[0] != '\0' &&
	     (!wbAppend(wb, "  ", 2) ||
	      !wbAppend(wb, entry->detail_preview, strlen(entry->detail_preview)))) ||
	    (shown_value != NULL && shown_value[0] != '\0' &&
	     (!wbAppend(wb, "  ", 2) || !wbAppend(wb, shown_value, strlen(shown_value)))) ||
	    (entry->detail_reference != NULL && entry->detail_reference[0] != '\0' &&
	     (!wbAppend(wb, "  ", 2) ||
	      !wbAppend(wb, entry->detail_reference, strlen(entry->detail_reference)))) ||
	    (shown_address != NULL && shown_address[0] != '\0' &&
	     (!wbAppend(wb, "  ", 2) || !wbAppend(wb, shown_address, strlen(shown_address))))) {
		return 0;
	}
	return 1;
}

static int drawerViewBuildRowPlain(struct writeBuf *wb, int visible_idx) {
	struct editorDrawerEntryView entry;
	if (!editorDrawerVisibleEntryView(visible_idx, &entry)) {
		return 1;
	}

	char entry_name_buf[PATH_MAX + 512];
	(void)snprintf(entry_name_buf, sizeof(entry_name_buf), "%s",
	               entry.name != NULL ? entry.name : "");
	char prefix_buf[64];
	(void)snprintf(prefix_buf, sizeof(prefix_buf), "%s",
	               drawerViewEntryHasPrefix(&entry) ? entry.prefix : "");

	if (!entry.is_root && !wbAppend(wb, " ", 1)) {
		return 0;
	}

	if (entry.depth > 1) {
		const char *branch = entry.is_last_sibling ? DRAWER_TREE_BRANCH_LAST_UTF8
		                                           : DRAWER_TREE_BRANCH_MID_UTF8;
		size_t branch_len = entry.is_last_sibling ? sizeof(DRAWER_TREE_BRANCH_LAST_UTF8) - 1
		                                          : sizeof(DRAWER_TREE_BRANCH_MID_UTF8) - 1;
		if (!drawerViewBuildAncestorGuidesPlain(wb, entry.parent_visible_idx) ||
		    !wbAppend(wb, branch, branch_len) ||
		    !wbAppend(wb, DRAWER_TREE_HORIZONTAL_UTF8 " ",
		              sizeof(DRAWER_TREE_HORIZONTAL_UTF8 " ") - 1)) {
			return 0;
		}
	}

	if (entry.is_dir && !entry.is_root) {
		if (entry.has_scan_error) {
			if (!wbAppend(wb, "! ", 2)) {
				return 0;
			}
		} else {
			const char *caret = entry.is_expanded ? DRAWER_CARET_EXPANDED_UTF8
			                                      : DRAWER_CARET_COLLAPSED_UTF8;
			size_t caret_len = entry.is_expanded
			                           ? sizeof(DRAWER_CARET_EXPANDED_UTF8) - 1
			                           : sizeof(DRAWER_CARET_COLLAPSED_UTF8) - 1;
			if (!wbAppend(wb, caret, caret_len) || !wbAppend(wb, " ", 1)) {
				return 0;
			}
		}
	}

	const char *icon = drawerViewNerdIconForEntry(&entry, entry_name_buf);
	if (icon != NULL && (!wbAppend(wb, icon, strlen(icon)) || !wbAppend(wb, " ", 1))) {
		return 0;
	}

	if (!drawerViewAppendPlainPrefix(wb, prefix_buf) ||
	    !editorAppendSanitizedText(wb, entry_name_buf, -1, NULL) ||
	    !drawerViewAppendPlainVariableDetails(wb, &entry)) {
		return 0;
	}
	return 1;
}

int editorDrawDrawerSelectionOverflow(struct writeBuf *wb, int row_idx, int drawer_cols,
                                      int separator_cols, int text_cols, int terminal_row,
                                      int *overlay_drawn_out) {
	if (overlay_drawn_out != NULL) {
		*overlay_drawn_out = 0;
	}
	if (separator_cols + text_cols <= 0 || E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
		return 1;
	}
	if (row_idx <= 0) {
		return 1;
	}

	int visible_idx = E.drawer_rowoff + row_idx - 1;
	struct editorDrawerEntryView entry;
	if (!editorDrawerVisibleEntryView(visible_idx, &entry) || !entry.is_selected) {
		return 1;
	}

	struct writeBuf plain = WRITEBUF_INIT;
	if (!drawerViewBuildRowPlain(&plain, visible_idx)) {
		wbFree(&plain);
		return 0;
	}
	if (!wbAppend(&plain, "\0", 1)) {
		wbFree(&plain);
		return 0;
	}

	int total_cols = editorDisplayTextCols(plain.b != NULL ? plain.b : "");
	if (total_cols <= drawer_cols) {
		wbFree(&plain);
		return 1;
	}

	int overlay_budget = separator_cols + text_cols;
	int overlay_written = 0;
	char move_buf[32];
	int move_len =
	        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", terminal_row, drawer_cols + 1);
	if (move_len <= 0 || move_len >= (int)sizeof(move_buf)) {
		wbFree(&plain);
		return 0;
	}
	if (!wbAppend(wb, move_buf, (size_t)move_len) ||
	    !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION) ||
	    !editorAppendDisplaySlice(wb, plain.b != NULL ? plain.b : "", drawer_cols,
	                              overlay_budget, &overlay_written) ||
	    !editorAppendThemeReset(wb)) {
		wbFree(&plain);
		return 0;
	}

	wbFree(&plain);
	if (overlay_drawn_out != NULL) {
		*overlay_drawn_out = 1;
	}
	return 1;
}

int editorDrawDrawerSeparatorCell(struct writeBuf *wb, int separator_cols) {
	if (separator_cols != 1) {
		return 1;
	}
	return wbAppend(wb, DRAWER_SPLITTER_UTF8, sizeof(DRAWER_SPLITTER_UTF8) - 1);
}

static int drawerViewRenderSearchHeaderRow(struct writeBuf *wb, const char *entry_name,
                                           int drawer_cols, int *written_cols) {
	if (*written_cols < drawer_cols) {
		int wrote = 0;
		const char *label = editorProjectSearchIsActive() ? editorProjectSearchHeaderLabel()
		                                                  : editorFileSearchHeaderLabel();
		if (!editorAppendSanitizedText(wb, label, drawer_cols - *written_cols, &wrote)) {
			return 0;
		}
		*written_cols += wrote;
	}
	if (*written_cols < drawer_cols) {
		int wrote = 0;
		if (!editorAppendSanitizedText(wb, entry_name, drawer_cols - *written_cols,
		                               &wrote)) {
			return 0;
		}
		*written_cols += wrote;
	}
	return 1;
}

static int drawerViewRenderTreeGuides(struct writeBuf *wb,
                                      const struct editorDrawerEntryView *entry,
                                      int gray_connectors, int drawer_cols, int *written_cols) {
	if (!drawerViewDrawAncestorGuides(wb, entry->parent_visible_idx, written_cols, drawer_cols,
	                                  gray_connectors)) {
		return 0;
	}
	const char *branch =
	        entry->is_last_sibling ? DRAWER_TREE_BRANCH_LAST_UTF8 : DRAWER_TREE_BRANCH_MID_UTF8;
	size_t branch_len = entry->is_last_sibling ? sizeof(DRAWER_TREE_BRANCH_LAST_UTF8) - 1
	                                           : sizeof(DRAWER_TREE_BRANCH_MID_UTF8) - 1;
	if (!drawerViewAppendConnectorCell(wb, branch, branch_len, written_cols, drawer_cols,
	                                   gray_connectors)) {
		return 0;
	}
	if (!drawerViewAppendConnectorCell(wb, DRAWER_TREE_HORIZONTAL_UTF8,
	                                   sizeof(DRAWER_TREE_HORIZONTAL_UTF8) - 1, written_cols,
	                                   drawer_cols, gray_connectors)) {
		return 0;
	}
	return drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols);
}

static int drawerViewRenderDirIndicator(struct writeBuf *wb,
                                        const struct editorDrawerEntryView *entry, int drawer_cols,
                                        int *written_cols) {
	if (entry->has_scan_error) {
		if (!drawerViewAppendCell(wb, "!", 1, written_cols, drawer_cols)) {
			return 0;
		}
	} else {
		const char *caret = entry->is_expanded ? DRAWER_CARET_EXPANDED_UTF8
		                                       : DRAWER_CARET_COLLAPSED_UTF8;
		size_t caret_len = entry->is_expanded ? sizeof(DRAWER_CARET_EXPANDED_UTF8) - 1
		                                      : sizeof(DRAWER_CARET_COLLAPSED_UTF8) - 1;
		if (!drawerViewAppendCell(wb, caret, caret_len, written_cols, drawer_cols)) {
			return 0;
		}
	}
	return drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols);
}

static int drawerViewRenderEntryIcon(struct writeBuf *wb, const struct editorDrawerEntryView *entry,
                                     const char *entry_name, int row_inverted, int drawer_cols,
                                     int *written_cols) {
	const char *icon = drawerViewNerdIconForEntry(entry, entry_name);
	struct editorThemeColor icon_color;
	struct editorThemeColor *icon_color_ptr =
	        drawerViewIconColorForEntry(entry, &icon_color) ? &icon_color : NULL;
	int icon_appended = 0;
	if (!drawerViewAppendNerdIcon(wb, icon, row_inverted, icon_color_ptr, written_cols,
	                              drawer_cols, &icon_appended)) {
		return 0;
	}
	if (icon_appended && !drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
		return 0;
	}
	return 1;
}

static int drawerViewRenderEntryPrefix(struct writeBuf *wb,
                                       const struct editorDrawerEntryView *entry,
                                       const char *prefix, int row_inverted, int drawer_cols,
                                       int *written_cols) {
	if (prefix == NULL || prefix[0] == '\0') {
		return 1;
	}
	struct editorThemeColor color = E.theme.ui[EDITOR_THEME_UI_DRAWER_CONNECTOR];
	const struct editorThemeColor *color_ptr =
	        !row_inverted && entry->prefix_muted ? &color : NULL;
	if (!drawerViewAppendSanitizedSpan(wb, prefix, row_inverted, color_ptr, written_cols,
	                                   drawer_cols)) {
		return 0;
	}
	return drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols);
}

static int drawerViewRenderVariableDetailToken(struct writeBuf *wb, const char *text,
                                               int row_inverted, struct editorThemeColor color,
                                               int drawer_cols, int *written_cols) {
	if (text == NULL || text[0] == '\0' || *written_cols >= drawer_cols) {
		return 1;
	}
	if (!drawerViewAppendSanitizedSpan(wb, "  ", row_inverted, NULL, written_cols,
	                                   drawer_cols)) {
		return 0;
	}
	return drawerViewAppendSanitizedSpan(wb, text, row_inverted, row_inverted ? NULL : &color,
	                                     written_cols, drawer_cols);
}

static int drawerViewRenderVariableDetails(struct writeBuf *wb,
                                           const struct editorDrawerEntryView *entry,
                                           int row_inverted, int drawer_cols, int *written_cols) {
	if (!drawerViewEntryHasVariableDetails(entry)) {
		return 1;
	}
	const char *shown_value = NULL;
	const char *shown_address = NULL;
	drawerViewResolveVariableValueAddress(entry, &shown_value, &shown_address);
	struct editorThemeColor type_color = E.theme.syntax[EDITOR_SYNTAX_HL_TYPE];
	struct editorThemeColor preview_color = E.theme.syntax[EDITOR_SYNTAX_HL_CONSTANT];
	struct editorThemeColor value_color = E.theme.syntax[EDITOR_SYNTAX_HL_NUMBER];
	struct editorThemeColor reference_color = E.theme.ui[EDITOR_THEME_UI_DRAWER_CONNECTOR];
	if (!drawerViewRenderVariableDetailToken(wb, entry->detail_type, row_inverted, type_color,
	                                         drawer_cols, written_cols) ||
	    !drawerViewRenderVariableDetailToken(wb, entry->detail_preview, row_inverted,
	                                         preview_color, drawer_cols, written_cols) ||
	    !drawerViewRenderVariableDetailToken(wb, shown_value, row_inverted, value_color,
	                                         drawer_cols, written_cols) ||
	    !drawerViewRenderVariableDetailToken(wb, entry->detail_reference, row_inverted,
	                                         reference_color, drawer_cols, written_cols) ||
	    !drawerViewRenderVariableDetailToken(wb, shown_address, row_inverted, reference_color,
	                                         drawer_cols, written_cols)) {
		return 0;
	}
	return 1;
}

static int drawerViewApplyGitColor(struct writeBuf *wb, const struct editorDrawerEntryView *entry,
                                   int *git_color_out) {
	*git_color_out = 0;
	if (E.git_repo_root == NULL) {
		return 1;
	}
	enum editorThemeUiRole role;
	switch (entry->git_status) {
		case EDITOR_GIT_STATUS_MODIFIED:
			role = EDITOR_THEME_UI_GIT_MODIFIED;
			break;
		case EDITOR_GIT_STATUS_ADDED:
			role = EDITOR_THEME_UI_GIT_ADDED;
			break;
		case EDITOR_GIT_STATUS_DELETED:
			role = EDITOR_THEME_UI_GIT_DELETED;
			break;
		case EDITOR_GIT_STATUS_UNTRACKED:
			role = EDITOR_THEME_UI_GIT_UNTRACKED;
			break;
		case EDITOR_GIT_STATUS_CONFLICT:
			role = EDITOR_THEME_UI_GIT_CONFLICT;
			break;
		default:
			return 1;
	}
	*git_color_out = 1;
	return editorAppendThemeForegroundRole(wb, role);
}

static int drawerViewRenderEntryNameLsp(struct writeBuf *wb,
                                        const struct editorDrawerEntryView *entry,
                                        const char *entry_name, int remaining, int *wrote_out) {
	struct editorThemeColor problem_color = {
	        .kind = EDITOR_THEME_COLOR_ANSI,
	        .value = entry->lsp_problem_severity == 1 ? EDITOR_THEME_ANSI_RED
	                                                  : EDITOR_THEME_ANSI_YELLOW,
	};
	int prefix_budget = entry->lsp_problem_kind_len;
	if (prefix_budget > remaining) {
		prefix_budget = remaining;
	}
	int prefix_wrote = 0;
	if (!editorAppendThemeForeground(wb, problem_color) ||
	    !editorAppendSanitizedText(wb, entry_name, prefix_budget, &prefix_wrote) ||
	    !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	*wrote_out = prefix_wrote;
	if (*wrote_out < remaining && prefix_wrote >= entry->lsp_problem_kind_len) {
		int rest_wrote = 0;
		if (!editorAppendSanitizedText(wb, entry_name + entry->lsp_problem_kind_len,
		                               remaining - *wrote_out, &rest_wrote)) {
			return 0;
		}
		*wrote_out += rest_wrote;
	}
	return 1;
}

static int drawerViewRenderEntryNameStyled(struct writeBuf *wb,
                                           const struct editorDrawerEntryView *entry,
                                           const char *entry_name, int row_inverted, int remaining,
                                           int *wrote_out) {
	int root_bold = entry->is_root || (entry->is_dir && !row_inverted);
	int root_color = entry->is_root;
	int dir_color = entry->is_dir && !entry->is_root && !row_inverted;
	int placeholder_color = entry->is_placeholder;
	int lsp_problem_kind_color =
	        !row_inverted && entry->lsp_problem_kind_len > 0 &&
	        (entry->lsp_problem_severity == 1 || entry->lsp_problem_severity == 2);
	int git_color = 0;
	if (!row_inverted && !drawerViewApplyGitColor(wb, entry, &git_color)) {
		return 0;
	}
	if (root_bold && !wbAppend(wb, VT100_BOLD_ON_4, 4)) {
		return 0;
	}
	if (root_color && !editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_ROOT)) {
		return 0;
	}
	if (dir_color && !editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_DIRECTORY)) {
		return 0;
	}
	if (placeholder_color &&
	    !editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_PLACEHOLDER)) {
		return 0;
	}
	int wrote = 0;
	if (lsp_problem_kind_color) {
		if (!drawerViewRenderEntryNameLsp(wb, entry, entry_name, remaining, &wrote)) {
			return 0;
		}
	} else if (!editorAppendSanitizedText(wb, entry_name, remaining, &wrote)) {
		return 0;
	}
	if (placeholder_color && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	if (dir_color && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	if (root_color && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	if (root_bold && !wbAppend(wb, VT100_BOLD_OFF_5, 5)) {
		return 0;
	}
	if (git_color && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	*wrote_out = wrote;
	return 1;
}

static int drawerViewRenderEntryRow(struct writeBuf *wb, int visible_idx, int drawer_cols,
                                    int *written_cols, int *row_inverted_out) {
	*row_inverted_out = 0;
	struct editorDrawerEntryView entry;
	if (!editorDrawerVisibleEntryView(visible_idx, &entry)) {
		return 1;
	}

	char entry_name_buf[PATH_MAX + 512];
	(void)snprintf(entry_name_buf, sizeof(entry_name_buf), "%s",
	               entry.name != NULL ? entry.name : "");
	const char *entry_name = entry_name_buf;
	char prefix_buf[64];
	(void)snprintf(prefix_buf, sizeof(prefix_buf), "%s",
	               drawerViewEntryHasPrefix(&entry) ? entry.prefix : "");

	int selected_with_focus =
	        entry.is_selected && E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER;
	int row_inverted = selected_with_focus || (entry.is_active_file && !entry.is_dir);
	*row_inverted_out = row_inverted;
	int gray_connectors = !row_inverted;
	if (row_inverted && !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
		return 0;
	}

	if (entry.is_search_header) {
		return drawerViewRenderSearchHeaderRow(wb, entry_name, drawer_cols, written_cols);
	}

	if (!entry.is_root && !drawerViewAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
		return 0;
	}
	if (entry.depth > 1 &&
	    !drawerViewRenderTreeGuides(wb, &entry, gray_connectors, drawer_cols, written_cols)) {
		return 0;
	}
	if (entry.is_dir && !entry.is_root &&
	    !drawerViewRenderDirIndicator(wb, &entry, drawer_cols, written_cols)) {
		return 0;
	}
	if (!drawerViewRenderEntryIcon(wb, &entry, entry_name, row_inverted, drawer_cols,
	                               written_cols)) {
		return 0;
	}
	if (!drawerViewRenderEntryPrefix(wb, &entry, prefix_buf, row_inverted, drawer_cols,
	                                 written_cols)) {
		return 0;
	}
	if (*written_cols < drawer_cols) {
		int remaining = drawer_cols - *written_cols;
		int wrote = 0;
		if (!drawerViewRenderEntryNameStyled(wb, &entry, entry_name, row_inverted,
		                                     remaining, &wrote)) {
			return 0;
		}
		*written_cols += wrote;
	}
	if (*written_cols < drawer_cols &&
	    !drawerViewRenderVariableDetails(wb, &entry, row_inverted, drawer_cols, written_cols)) {
		return 0;
	}
	return 1;
}

int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
	if (drawer_cols <= 0) {
		return 1;
	}
	if (editorDrawerIsCollapsed()) {
		return drawerViewDrawCollapsedRow(wb, row_idx, drawer_cols);
	}
	if (row_idx == 0) {
		return drawerViewDrawExpandedHeaderRow(wb, drawer_cols);
	}

	int visible_idx = E.drawer_rowoff + row_idx - 1;
	int written_cols = 0;
	int row_inverted = 0;
	if (!drawerViewRenderEntryRow(wb, visible_idx, drawer_cols, &written_cols, &row_inverted)) {
		return 0;
	}
	while (written_cols < drawer_cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		written_cols++;
	}
	if (row_inverted && !editorAppendThemeReset(wb)) {
		return 0;
	}
	return 1;
}
