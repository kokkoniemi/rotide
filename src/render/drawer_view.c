#include "render/drawer_view.h"

#include "render/ansi_style.h"
#include "render/display_text.h"
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
#define DRAWER_HEADER_MODE_BUTTON_COLS 3
#define DRAWER_HEADER_MODE_BUTTON_COUNT 7
#define DRAWER_HEADER_MODE_BUTTONS_MIN_COLS \
	(ROTIDE_DRAWER_COLLAPSED_WIDTH + \
			DRAWER_HEADER_MODE_BUTTON_COLS * DRAWER_HEADER_MODE_BUTTON_COUNT)

static int editorDrawDrawerHeaderCell(struct writeBuf *wb, const char *label, int active,
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

static int editorDrawCollapsedDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
	int written_cols = 0;
	if (row_idx == 0 && !editorDrawDrawerHeaderCell(wb, DRAWER_EXPAND_INDICATOR, 0,
				&written_cols, drawer_cols)) {
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

static enum editorDrawerMode editorActiveDrawerHeaderMode(void) {
	if (editorFileSearchIsActive()) {
		return EDITOR_DRAWER_MODE_FILE_SEARCH;
	}
	if (editorProjectSearchIsActive()) {
		return EDITOR_DRAWER_MODE_PROJECT_SEARCH;
	}
	return E.drawer_mode;
}

static const char *editorDrawerHeaderSymbol(enum editorDrawerMode mode) {
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

static int editorDrawDrawerHeaderModeButton(struct writeBuf *wb, const char *label,
		enum editorDrawerMode mode, enum editorDrawerMode active_mode, int *written_cols,
		int drawer_cols) {
	return editorDrawDrawerHeaderCell(wb, label, mode == active_mode, written_cols, drawer_cols);
}

static int editorDrawExpandedDrawerHeaderRow(struct writeBuf *wb, int drawer_cols) {
	int written_cols = 0;
	if (!editorDrawDrawerHeaderCell(wb, DRAWER_COLLAPSE_INDICATOR, 0, &written_cols,
				drawer_cols)) {
		return 0;
	}

	if (drawer_cols >= DRAWER_HEADER_MODE_BUTTONS_MIN_COLS) {
		enum editorDrawerMode active_mode = editorActiveDrawerHeaderMode();
		if (!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_TREE),
					EDITOR_DRAWER_MODE_TREE, active_mode, &written_cols, drawer_cols) ||
				!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_FILE_SEARCH),
					EDITOR_DRAWER_MODE_FILE_SEARCH, active_mode, &written_cols,
					drawer_cols) ||
				!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_PROJECT_SEARCH),
					EDITOR_DRAWER_MODE_PROJECT_SEARCH, active_mode, &written_cols,
					drawer_cols) ||
				!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_LSP),
					EDITOR_DRAWER_MODE_LSP, active_mode, &written_cols, drawer_cols) ||
				!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_DAP),
					EDITOR_DRAWER_MODE_DAP, active_mode, &written_cols, drawer_cols) ||
				!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_GIT),
					EDITOR_DRAWER_MODE_GIT, active_mode, &written_cols,
					drawer_cols) ||
				!editorDrawDrawerHeaderModeButton(wb,
					editorDrawerHeaderSymbol(EDITOR_DRAWER_MODE_MAIN_MENU),
					EDITOR_DRAWER_MODE_MAIN_MENU, active_mode, &written_cols,
					drawer_cols)) {
			return 0;
		}
	}

	while (written_cols < drawer_cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		written_cols++;
	}

	return 1;
}

static int editorDrawerHasSuffixCaseInsensitive(const char *text, const char *suffix) {
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

static const char *editorDrawerNameForFileIcon(const struct editorDrawerEntryView *entry,
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

static const char *editorDrawerNerdIconForMenuLabel(const char *label) {
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

static const char *editorDrawerNerdIconForFileName(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return DRAWER_NERD_FILE_UTF8;
	}
	const char *slash = strrchr(name, '/');
	const char *base = slash != NULL ? slash + 1 : name;
	if (strcmp(base, "Makefile") == 0 || strcmp(base, "makefile") == 0 ||
			editorDrawerHasSuffixCaseInsensitive(base, ".c") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".h") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".cc") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".cpp") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".cxx") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".hpp") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".go") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".rs") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".js") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".jsx") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".ts") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".tsx") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".py") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".php") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".java") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".rb") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".cs") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".hs") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".ml") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".jl") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".scala") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".sh") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".bash") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".zsh")) {
		return DRAWER_NERD_FILE_CODE_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".toml") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".json") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".yaml") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".yml") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".xml") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".ini") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".conf") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".cfg") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".env")) {
		return DRAWER_NERD_GEAR_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".md") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".markdown") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".txt") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".log")) {
		return DRAWER_NERD_FILE_TEXT_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".png") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".jpg") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".jpeg") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".gif") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".svg") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".webp")) {
		return DRAWER_NERD_FILE_IMAGE_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".zip") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".tar") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".gz") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".bz2") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".xz")) {
		return DRAWER_NERD_FILE_ARCHIVE_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".pdf")) {
		return DRAWER_NERD_FILE_PDF_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".mp3") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".wav") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".flac")) {
		return DRAWER_NERD_FILE_AUDIO_UTF8;
	}
	if (editorDrawerHasSuffixCaseInsensitive(base, ".mp4") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".mov") ||
			editorDrawerHasSuffixCaseInsensitive(base, ".webm")) {
		return DRAWER_NERD_FILE_VIDEO_UTF8;
	}
	return DRAWER_NERD_FILE_UTF8;
}

static const char *editorDrawerNerdIconForEntry(const struct editorDrawerEntryView *entry,
		const char *entry_name) {
	if (!E.nerd_fonts_enabled || entry == NULL || entry->is_search_header ||
			entry->is_placeholder) {
		return NULL;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return editorDrawerNerdIconForMenuLabel(entry_name);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT && entry->is_root) {
		return NULL;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP && entry->is_root) {
		return NULL;
	}
	if (entry->is_dir) {
		return NULL;
	}
	return editorDrawerNerdIconForFileName(editorDrawerNameForFileIcon(entry, entry_name));
}

static int editorDrawerAppendNerdIcon(struct writeBuf *wb, const char *icon, int row_inverted,
		int *written_cols, int drawer_cols, int *appended_out) {
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
	if (!row_inverted && !editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_DRAWER_ICON)) {
		return 0;
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

static int editorDrawerAppendCell(struct writeBuf *wb, const char *text, size_t len,
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

static int editorDrawerAppendGrayCell(struct writeBuf *wb, const char *text, size_t len,
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

static int editorDrawerAppendConnectorCell(struct writeBuf *wb, const char *text, size_t len,
		int *written_cols, int drawer_cols, int use_gray) {
	if (use_gray) {
		return editorDrawerAppendGrayCell(wb, text, len, written_cols, drawer_cols);
	}
	return editorDrawerAppendCell(wb, text, len, written_cols, drawer_cols);
}

static int editorDrawDrawerAncestorGuides(struct writeBuf *wb, int parent_visible_idx,
		int *written_cols, int drawer_cols, int gray_connectors) {
	if (parent_visible_idx < 0) {
		return 1;
	}

	struct editorDrawerEntryView parent_entry;
	if (!editorDrawerGetVisibleEntry(parent_visible_idx, &parent_entry)) {
		return 1;
	}

	if (parent_entry.depth >= 2) {
		if (!editorDrawDrawerAncestorGuides(wb, parent_entry.parent_visible_idx, written_cols,
					drawer_cols, gray_connectors)) {
			return 0;
		}
		if (parent_entry.is_last_sibling) {
			if (!editorDrawerAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
				return 0;
			}
		} else {
			if (!editorDrawerAppendConnectorCell(wb, DRAWER_SPLITTER_UTF8,
						sizeof(DRAWER_SPLITTER_UTF8) - 1,
						written_cols, drawer_cols, gray_connectors)) {
				return 0;
			}
		}
		if (!editorDrawerAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
			return 0;
		}
		if (!editorDrawerAppendCell(wb, " ", 1, written_cols, drawer_cols)) {
			return 0;
		}
	}

	return 1;
}

static int editorBuildDrawerAncestorGuidesPlain(struct writeBuf *wb, int parent_visible_idx) {
	if (parent_visible_idx < 0) {
		return 1;
	}

	struct editorDrawerEntryView parent_entry;
	if (!editorDrawerGetVisibleEntry(parent_visible_idx, &parent_entry)) {
		return 1;
	}

	if (parent_entry.depth >= 2) {
		if (!editorBuildDrawerAncestorGuidesPlain(wb, parent_entry.parent_visible_idx)) {
			return 0;
		}
		if (parent_entry.is_last_sibling) {
			if (!wbAppend(wb, "   ", 3)) {
				return 0;
			}
		} else if (!wbAppend(wb, DRAWER_SPLITTER_UTF8 "  ", sizeof(DRAWER_SPLITTER_UTF8) + 2 - 1)) {
			return 0;
		}
	}

	return 1;
}

static int editorBuildDrawerRowPlain(struct writeBuf *wb, int visible_idx) {
	struct editorDrawerEntryView entry;
	if (!editorDrawerGetVisibleEntry(visible_idx, &entry)) {
		return 1;
	}

	char entry_name_buf[PATH_MAX + 512];
	snprintf(entry_name_buf, sizeof(entry_name_buf), "%s",
			entry.name != NULL ? entry.name : "");

	if (!entry.is_root && !wbAppend(wb, " ", 1)) {
		return 0;
	}

	if (entry.depth > 1) {
		const char *branch = entry.is_last_sibling ? DRAWER_TREE_BRANCH_LAST_UTF8 :
				DRAWER_TREE_BRANCH_MID_UTF8;
		size_t branch_len = entry.is_last_sibling ? sizeof(DRAWER_TREE_BRANCH_LAST_UTF8) - 1 :
				sizeof(DRAWER_TREE_BRANCH_MID_UTF8) - 1;
		if (!editorBuildDrawerAncestorGuidesPlain(wb, entry.parent_visible_idx) ||
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
			const char *caret = entry.is_expanded ? DRAWER_CARET_EXPANDED_UTF8 :
					DRAWER_CARET_COLLAPSED_UTF8;
			size_t caret_len = entry.is_expanded ? sizeof(DRAWER_CARET_EXPANDED_UTF8) - 1 :
					sizeof(DRAWER_CARET_COLLAPSED_UTF8) - 1;
			if (!wbAppend(wb, caret, caret_len) || !wbAppend(wb, " ", 1)) {
				return 0;
			}
		}
	}

	const char *icon = editorDrawerNerdIconForEntry(&entry, entry_name_buf);
	if (icon != NULL && (!wbAppend(wb, icon, strlen(icon)) || !wbAppend(wb, " ", 1))) {
		return 0;
	}

	return editorAppendSanitizedText(wb, entry_name_buf, -1, NULL);
}

int editorDrawDrawerSelectionOverflow(struct writeBuf *wb, int row_idx, int drawer_cols,
		int separator_cols, int text_cols, int terminal_row, int *overlay_drawn_out) {
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
	if (!editorDrawerGetVisibleEntry(visible_idx, &entry) || !entry.is_selected) {
		return 1;
	}

	struct writeBuf plain = WRITEBUF_INIT;
	if (!editorBuildDrawerRowPlain(&plain, visible_idx)) {
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
	int move_len = snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", terminal_row, drawer_cols + 1);
	if (move_len <= 0 || move_len >= (int)sizeof(move_buf)) {
		wbFree(&plain);
		return 0;
	}
	if (!wbAppend(wb, move_buf, (size_t)move_len) ||
			!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION) ||
			!editorAppendDisplaySlice(wb, plain.b != NULL ? plain.b : "", drawer_cols, overlay_budget,
					&overlay_written) ||
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

int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
	if (drawer_cols <= 0) {
		return 1;
	}
	if (editorDrawerIsCollapsed()) {
		return editorDrawCollapsedDrawerRow(wb, row_idx, drawer_cols);
	}
	if (row_idx == 0) {
		return editorDrawExpandedDrawerHeaderRow(wb, drawer_cols);
	}

	struct editorDrawerEntryView entry;
	int visible_idx = E.drawer_rowoff + row_idx - 1;
	int written_cols = 0;
	int row_inverted = 0;
	if (editorDrawerGetVisibleEntry(visible_idx, &entry)) {
		char entry_name_buf[PATH_MAX + 512];
		const char *entry_name = entry.name != NULL ? entry.name : "";
		snprintf(entry_name_buf, sizeof(entry_name_buf), "%s", entry_name);
		entry_name = entry_name_buf;
		int selected_with_focus = entry.is_selected && E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER;
		row_inverted = selected_with_focus || (entry.is_active_file && !entry.is_dir);
		int gray_connectors = !row_inverted;
		if (row_inverted && !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
			return 0;
		}

		if (entry.is_search_header) {
			if (written_cols < drawer_cols) {
				int wrote = 0;
				const char *label = editorProjectSearchIsActive() ?
						editorProjectSearchHeaderLabel() : editorFileSearchHeaderLabel();
				if (!editorAppendSanitizedText(wb, label, drawer_cols - written_cols, &wrote)) {
					return 0;
				}
				written_cols += wrote;
			}
			if (written_cols < drawer_cols) {
				int wrote = 0;
				if (!editorAppendSanitizedText(wb, entry_name, drawer_cols - written_cols,
							&wrote)) {
					return 0;
				}
				written_cols += wrote;
			}
			goto pad_drawer_row;
		}

		if (!entry.is_root && !editorDrawerAppendCell(wb, " ", 1, &written_cols, drawer_cols)) {
			return 0;
		}

		if (entry.depth > 1) {
			if (!editorDrawDrawerAncestorGuides(wb, entry.parent_visible_idx, &written_cols,
						drawer_cols, gray_connectors)) {
				return 0;
			}
			const char *branch = entry.is_last_sibling ?
					DRAWER_TREE_BRANCH_LAST_UTF8 : DRAWER_TREE_BRANCH_MID_UTF8;
			size_t branch_len = entry.is_last_sibling ?
					sizeof(DRAWER_TREE_BRANCH_LAST_UTF8) - 1 :
					sizeof(DRAWER_TREE_BRANCH_MID_UTF8) - 1;
			if (!editorDrawerAppendConnectorCell(wb, branch, branch_len, &written_cols, drawer_cols,
						gray_connectors)) {
				return 0;
			}
			if (!editorDrawerAppendConnectorCell(wb, DRAWER_TREE_HORIZONTAL_UTF8,
						sizeof(DRAWER_TREE_HORIZONTAL_UTF8) - 1, &written_cols, drawer_cols,
						gray_connectors)) {
				return 0;
			}
			if (!editorDrawerAppendCell(wb, " ", 1, &written_cols, drawer_cols)) {
				return 0;
			}
		}

		if (entry.is_dir && !entry.is_root) {
			if (entry.has_scan_error) {
				if (!editorDrawerAppendCell(wb, "!", 1, &written_cols, drawer_cols)) {
					return 0;
				}
			} else if (entry.is_expanded) {
				if (!editorDrawerAppendCell(wb, DRAWER_CARET_EXPANDED_UTF8,
							sizeof(DRAWER_CARET_EXPANDED_UTF8) - 1, &written_cols, drawer_cols)) {
					return 0;
				}
			} else if (!editorDrawerAppendCell(wb, DRAWER_CARET_COLLAPSED_UTF8,
							sizeof(DRAWER_CARET_COLLAPSED_UTF8) - 1, &written_cols, drawer_cols)) {
				return 0;
			}
			if (!editorDrawerAppendCell(wb, " ", 1, &written_cols, drawer_cols)) {
				return 0;
			}
		}

		const char *icon = editorDrawerNerdIconForEntry(&entry, entry_name);
		int icon_appended = 0;
		if (!editorDrawerAppendNerdIcon(wb, icon, row_inverted, &written_cols, drawer_cols,
					&icon_appended)) {
			return 0;
		}
		if (icon_appended &&
				!editorDrawerAppendCell(wb, " ", 1, &written_cols, drawer_cols)) {
			return 0;
		}

		if (written_cols < drawer_cols) {
			int remaining = drawer_cols - written_cols;
			int wrote = 0;
			int root_bold = entry.is_root || (entry.is_dir && !row_inverted);
			int root_color = entry.is_root;
			int dir_color = entry.is_dir && !entry.is_root && !row_inverted;
			int placeholder_color = entry.is_placeholder;
			int git_color = 0;
			int lsp_problem_kind_color =
					!row_inverted && entry.lsp_problem_kind_len > 0 &&
					(entry.lsp_problem_severity == 1 || entry.lsp_problem_severity == 2);
			if (!row_inverted && E.git_repo_root != NULL) {
				switch (entry.git_status) {
				case EDITOR_GIT_STATUS_MODIFIED:
					git_color = 1;
					if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_GIT_MODIFIED)) {
						return 0;
					}
					break;
				case EDITOR_GIT_STATUS_UNTRACKED:
					git_color = 1;
					if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_GIT_UNTRACKED)) {
						return 0;
					}
					break;
				case EDITOR_GIT_STATUS_CONFLICT:
					git_color = 1;
					if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_GIT_CONFLICT)) {
						return 0;
					}
					break;
				default:
					break;
				}
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
			if (lsp_problem_kind_color) {
				struct editorThemeColor problem_color = {
					.kind = EDITOR_THEME_COLOR_ANSI,
					.value = entry.lsp_problem_severity == 1 ?
							EDITOR_THEME_ANSI_RED : EDITOR_THEME_ANSI_YELLOW,
				};
				int prefix_budget = entry.lsp_problem_kind_len;
				if (prefix_budget > remaining) {
					prefix_budget = remaining;
				}
				int prefix_wrote = 0;
				if (!editorAppendThemeForeground(wb, problem_color) ||
						!editorAppendSanitizedText(wb, entry_name, prefix_budget,
								&prefix_wrote) ||
						!editorAppendThemeBaseForeground(wb)) {
					return 0;
				}
				wrote += prefix_wrote;
				if (wrote < remaining && prefix_wrote >= entry.lsp_problem_kind_len) {
					int rest_wrote = 0;
					if (!editorAppendSanitizedText(wb,
								entry_name + entry.lsp_problem_kind_len,
								remaining - wrote, &rest_wrote)) {
						return 0;
					}
					wrote += rest_wrote;
				}
			} else {
				if (!editorAppendSanitizedText(wb, entry_name, remaining, &wrote)) {
					return 0;
				}
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
			written_cols += wrote;
		}
	}

pad_drawer_row:
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
