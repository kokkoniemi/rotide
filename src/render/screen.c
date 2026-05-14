#include "render/screen.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "language/dap.h"
#include "language/lsp.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "text/row.h"
#include "text/utf8.h"
#include "render/popup.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/*** Write buffer ***/

struct writeBuf {
	char *b;
	size_t len;
};

#define WRITEBUF_INIT {NULL, 0}

#define VT100_CLEAR_ROW_3 "\x1b[K"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"
#define VT100_HIDE_CURSOR_6 "\x1b[?25l"
#define VT100_SHOW_CURSOR_6 "\x1b[?25h"
#define VT100_CURSOR_BLINKING_BLOCK_5 "\x1b[1 q"
#define VT100_CURSOR_STEADY_BLOCK_5 "\x1b[2 q"
#define VT100_CURSOR_BLINKING_UNDERLINE_5 "\x1b[3 q"
#define VT100_CURSOR_STEADY_UNDERLINE_5 "\x1b[4 q"
#define VT100_CURSOR_BLINKING_BAR_5 "\x1b[5 q"
#define VT100_CURSOR_STEADY_BAR_5 "\x1b[6 q"
#define VT100_CURSOR_COLOR_WHITE "\x1b]12;white\a"
#define VT100_ITALIC_ON_4 "\x1b[3m"
#define VT100_ITALIC_OFF_5 "\x1b[23m"
#define VT100_BOLD_ON_4 "\x1b[1m"
#define VT100_BOLD_OFF_5 "\x1b[22m"
#define VT100_UNDERLINE_ON_4 "\x1b[4m"
#define VT100_UNDERLINE_OFF_5 "\x1b[24m"
#define VT100_UNDERLINE_COLOR_RED_9 "\x1b[58;5;1m"
#define VT100_UNDERLINE_COLOR_DEFAULT_5 "\x1b[59m"
#define VT100_INVERTED_COLORS_4 "\x1b[7m"
#define VT100_NORMAL_COLORS_3 "\x1b[m"
#define VT100_BG_CURRENT_LINE "\x1b[48;5;236m"
#define VT100_FG_BLACK_5 "\x1b[30m"
#define VT100_FG_RED_5 "\x1b[31m"
#define VT100_FG_GREEN_5 "\x1b[32m"
#define VT100_FG_YELLOW_5 "\x1b[33m"
#define VT100_FG_BLUE_5 "\x1b[34m"
#define VT100_FG_MAGENTA_5 "\x1b[35m"
#define VT100_FG_CYAN_5 "\x1b[36m"
#define VT100_FG_WHITE_5 "\x1b[37m"
#define VT100_FG_GRAY_5 "\x1b[90m"
#define VT100_FG_BRIGHT_RED_5 "\x1b[91m"
#define VT100_FG_BRIGHT_GREEN_5 "\x1b[92m"
#define VT100_FG_BRIGHT_YELLOW_5 "\x1b[93m"
#define VT100_FG_BRIGHT_BLUE_5 "\x1b[94m"
#define VT100_FG_BRIGHT_MAGENTA_5 "\x1b[95m"
#define VT100_FG_BRIGHT_CYAN_5 "\x1b[96m"
#define VT100_FG_BRIGHT_WHITE_5 "\x1b[97m"
#define VT100_FG_DEFAULT_5 "\x1b[39m"
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
#define DRAWER_NERD_FOLDER_OPEN_UTF8 "\xEF\x81\xBC"
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
#define DRAWER_NERD_CODE_UTF8 "\xEF\x84\xA1"
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
#define EDITOR_DIAGNOSTIC_MAX_RENDER_SPANS_PER_ROW 64
#define TEXT_OVERFLOW_LEFT_UTF8 "\xE2\x86\x90"
#define TEXT_OVERFLOW_RIGHT_UTF8 "\xE2\x86\x92"
#define TEXT_WRAP_CONTINUATION_UTF8 "\xE2\x86\xB3"

static int wbAppend(struct writeBuf *wb, const char *s, size_t len) {
	if (len == 0) {
		return 1;
	}

	size_t new_len = 0;
	if (!editorSizeAdd(wb->len, len, &new_len) || new_len > ROTIDE_MAX_TEXT_BYTES) {
		return 0;
	}

	char *new = editorRealloc(wb->b, new_len);

	if (new == NULL) {
		return 0;
	}

	memcpy(&new[wb->len], s, len);
	wb->b = new;
	wb->len += len;

	return 1;
}

static void wbFree(struct writeBuf *wb) {
	free(wb->b);
}

static int editorThemeColorEquals(struct editorThemeColor a, struct editorThemeColor b) {
	return a.kind == b.kind && a.value == b.value && a.r == b.r && a.g == b.g && a.b == b.b;
}

static int editorThemeColorIsDefault(struct editorThemeColor color) {
	return color.kind == EDITOR_THEME_COLOR_DEFAULT;
}

static int editorAppendThemeColor(struct writeBuf *wb, struct editorThemeColor color, int bg) {
	if (color.kind == EDITOR_THEME_COLOR_DEFAULT) {
		return wbAppend(wb, bg ? "\x1b[49m" : "\x1b[39m", 5);
	}
	if (color.kind == EDITOR_THEME_COLOR_ANSI) {
		static const char *fg_sequences[EDITOR_THEME_ANSI_COUNT] = {
			VT100_FG_BLACK_5,
			VT100_FG_RED_5,
			VT100_FG_GREEN_5,
			VT100_FG_YELLOW_5,
			VT100_FG_BLUE_5,
			VT100_FG_MAGENTA_5,
			VT100_FG_CYAN_5,
			VT100_FG_WHITE_5,
			VT100_FG_GRAY_5,
			VT100_FG_BRIGHT_RED_5,
			VT100_FG_BRIGHT_GREEN_5,
			VT100_FG_BRIGHT_YELLOW_5,
			VT100_FG_BRIGHT_BLUE_5,
			VT100_FG_BRIGHT_MAGENTA_5,
			VT100_FG_BRIGHT_CYAN_5,
			VT100_FG_BRIGHT_WHITE_5,
		};
		static const char *bg_sequences[EDITOR_THEME_ANSI_COUNT] = {
			"\x1b[40m", "\x1b[41m", "\x1b[42m", "\x1b[43m",
			"\x1b[44m", "\x1b[45m", "\x1b[46m", "\x1b[47m",
			"\x1b[100m", "\x1b[101m", "\x1b[102m", "\x1b[103m",
			"\x1b[104m", "\x1b[105m", "\x1b[106m", "\x1b[107m",
		};
		if (color.value >= EDITOR_THEME_ANSI_COUNT) {
			return wbAppend(wb, bg ? "\x1b[49m" : "\x1b[39m", 5);
		}
		const char *sequence = bg ? bg_sequences[color.value] : fg_sequences[color.value];
		return wbAppend(wb, sequence, strlen(sequence));
	}

	char sequence[32];
	int len = 0;
	if (color.kind == EDITOR_THEME_COLOR_256) {
		len = snprintf(sequence, sizeof(sequence), "\x1b[%d;5;%um", bg ? 48 : 38,
				(unsigned int)color.value);
	} else {
		len = snprintf(sequence, sizeof(sequence), "\x1b[%d;2;%u;%u;%um", bg ? 48 : 38,
				(unsigned int)color.r, (unsigned int)color.g, (unsigned int)color.b);
	}
	if (len <= 0 || len >= (int)sizeof(sequence)) {
		return 0;
	}
	return wbAppend(wb, sequence, (size_t)len);
}

static int editorAppendThemeForeground(struct writeBuf *wb, struct editorThemeColor color) {
	return editorAppendThemeColor(wb, color, 0);
}

static int editorAppendThemeBackground(struct writeBuf *wb, struct editorThemeColor color) {
	return editorAppendThemeColor(wb, color, 1);
}

static int editorAppendThemeBaseForeground(struct writeBuf *wb) {
	return editorAppendThemeForeground(wb, E.theme.ui[EDITOR_THEME_UI_FOREGROUND]);
}

static int editorAppendThemeBaseStyle(struct writeBuf *wb) {
	if (!editorThemeColorIsDefault(E.theme.ui[EDITOR_THEME_UI_FOREGROUND]) &&
			!editorAppendThemeForeground(wb, E.theme.ui[EDITOR_THEME_UI_FOREGROUND])) {
		return 0;
	}
	if (!editorThemeColorIsDefault(E.theme.ui[EDITOR_THEME_UI_BACKGROUND]) &&
			!editorAppendThemeBackground(wb, E.theme.ui[EDITOR_THEME_UI_BACKGROUND])) {
		return 0;
	}
	return 1;
}

static int editorAppendThemeReset(struct writeBuf *wb) {
	return wbAppend(wb, VT100_NORMAL_COLORS_3, 3) && editorAppendThemeBaseStyle(wb);
}

static int editorAppendThemeStyle(struct writeBuf *wb, enum editorThemeStyleRole role) {
	if (role < 0 || role >= EDITOR_THEME_STYLE_ROLE_COUNT) {
		return 1;
	}
	struct editorThemeStyle style = E.theme.styles[role];
	if (style.reverse) {
		return wbAppend(wb, VT100_INVERTED_COLORS_4, 4);
	}
	if (!editorAppendThemeForeground(wb, style.fg)) {
		return 0;
	}
	return editorAppendThemeBackground(wb, style.bg);
}

static int editorAppendThemeForegroundRole(struct writeBuf *wb, enum editorThemeUiRole role) {
	if (role < 0 || role >= EDITOR_THEME_UI_ROLE_COUNT) {
		return 1;
	}
	return editorAppendThemeForeground(wb, E.theme.ui[role]);
}

static int editorAppendThemeBackgroundRole(struct writeBuf *wb, enum editorThemeUiRole role) {
	if (role < 0 || role >= EDITOR_THEME_UI_ROLE_COUNT) {
		return 1;
	}
	return editorAppendThemeBackground(wb, E.theme.ui[role]);
}

static int editorAppendThemeCursorColor(struct writeBuf *wb) {
	struct editorThemeColor color = E.theme.ui[EDITOR_THEME_UI_CURSOR];
	if (color.kind == EDITOR_THEME_COLOR_DEFAULT) {
		return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
	}
	if (color.kind == EDITOR_THEME_COLOR_ANSI) {
		static const char *names[EDITOR_THEME_ANSI_COUNT] = {
			"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
			"brightblack", "brightred", "brightgreen", "brightyellow", "brightblue",
			"brightmagenta", "brightcyan", "brightwhite",
		};
		if (color.value == EDITOR_THEME_ANSI_WHITE) {
			return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
		}
		if (color.value >= EDITOR_THEME_ANSI_COUNT) {
			return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
		}
		char sequence[40];
		int len = snprintf(sequence, sizeof(sequence), "\x1b]12;%s\a", names[color.value]);
		if (len <= 0 || len >= (int)sizeof(sequence)) {
			return 0;
		}
		return wbAppend(wb, sequence, (size_t)len);
	}
	if (color.kind == EDITOR_THEME_COLOR_256) {
		return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
	}
	char sequence[40];
	int len = snprintf(sequence, sizeof(sequence), "\x1b]12;rgb:%02x/%02x/%02x\a",
			(unsigned int)color.r, (unsigned int)color.g, (unsigned int)color.b);
	if (len <= 0 || len >= (int)sizeof(sequence)) {
		return 0;
	}
	return wbAppend(wb, sequence, (size_t)len);
}

static int editorAppendGrayBytes(struct writeBuf *wb, const char *text, size_t len) {
	return editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_PLACEHOLDER) &&
			wbAppend(wb, text, len) && editorAppendThemeBaseForeground(wb);
}

/*** Output ***/

struct editorFileRowFrameCache {
	int valid;
	int row_capacity;
	int row_count;
	int window_rows;
	int window_cols;
	char **rows;
	size_t *row_lens;
};

static struct editorFileRowFrameCache g_file_row_frame_cache = {0};
static int g_editor_output_last_refresh_file_row_draw_count = 0;
static int g_editor_drawing_current_line_highlight = 0;
static int g_popup_prev_screen_top = 0;
static int g_popup_prev_row_count = 0;

static int editorWriteAllToStdout(const char *buf, size_t len) {
	if (len == 0) {
		return 1;
	}

	errno = 0;
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(STDOUT_FILENO, buf + total, len - total);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (written == 0) {
			errno = 0;
			return 0;
		}
		total += (size_t)written;
	}
	return 1;
}

static void editorFileRowFrameCacheClearRowsFrom(int start_row) {
	if (start_row < 0) {
		start_row = 0;
	}
	if (start_row > g_file_row_frame_cache.row_capacity) {
		start_row = g_file_row_frame_cache.row_capacity;
	}
	for (int i = start_row; i < g_file_row_frame_cache.row_capacity; i++) {
		free(g_file_row_frame_cache.rows[i]);
		g_file_row_frame_cache.rows[i] = NULL;
		g_file_row_frame_cache.row_lens[i] = 0;
	}
	if (g_file_row_frame_cache.row_count < start_row) {
		g_file_row_frame_cache.row_count = start_row;
	}
}

static void editorFileRowFrameCacheReset(void) {
	editorFileRowFrameCacheClearRowsFrom(0);
	g_file_row_frame_cache.valid = 0;
	g_file_row_frame_cache.row_count = 0;
	g_file_row_frame_cache.window_rows = 0;
	g_file_row_frame_cache.window_cols = 0;
	g_popup_prev_screen_top = 0;
	g_popup_prev_row_count = 0;
}

static int editorAppendTextRowReset(struct writeBuf *wb) {
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	if (g_editor_drawing_current_line_highlight &&
			!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
		return 0;
	}
	return 1;
}

static int editorFileRowFrameCacheEnsureCapacity(int needed_rows) {
	if (needed_rows <= g_file_row_frame_cache.row_capacity) {
		return 1;
	}
	if (needed_rows <= 0) {
		return 1;
	}

	size_t cap_size = 0;
	size_t rows_bytes = 0;
	if (!editorIntToSize(needed_rows, &cap_size) ||
			!editorSizeMul(sizeof(*g_file_row_frame_cache.rows), cap_size, &rows_bytes)) {
		return 0;
	}

	int old_capacity = g_file_row_frame_cache.row_capacity;
	char **new_rows = editorRealloc(g_file_row_frame_cache.rows, rows_bytes);
	if (new_rows == NULL) {
		return 0;
	}
	g_file_row_frame_cache.rows = new_rows;

	size_t lens_bytes = 0;
	if (!editorSizeMul(sizeof(*g_file_row_frame_cache.row_lens), cap_size, &lens_bytes)) {
		return 0;
	}
	size_t *new_lens = editorRealloc(g_file_row_frame_cache.row_lens, lens_bytes);
	if (new_lens == NULL) {
		return 0;
	}
	g_file_row_frame_cache.row_lens = new_lens;

	for (int i = old_capacity; i < needed_rows; i++) {
		g_file_row_frame_cache.rows[i] = NULL;
		g_file_row_frame_cache.row_lens[i] = 0;
	}

	g_file_row_frame_cache.row_capacity = needed_rows;
	return 1;
}

static int editorFileRowFrameCacheStoreRow(int row_idx, const char *row_data, size_t row_len) {
	if (row_idx < 0 || row_data == NULL || row_idx >= g_file_row_frame_cache.row_capacity) {
		return 0;
	}

	char *copy = NULL;
	if (row_len > 0) {
		copy = editorMalloc(row_len);
		if (copy == NULL) {
			return 0;
		}
		memcpy(copy, row_data, row_len);
	}

	free(g_file_row_frame_cache.rows[row_idx]);
	g_file_row_frame_cache.rows[row_idx] = copy;
	g_file_row_frame_cache.row_lens[row_idx] = row_len;
	return 1;
}

static int editorAppendCursorMove(struct writeBuf *wb, int row, int col) {
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
	if (len <= 0 || len >= (int)sizeof(buf)) {
		return 0;
	}
	return wbAppend(wb, buf, (size_t)len);
}

static int editorDrawGreeting(struct writeBuf *wb, int cols) {
	char greet[80];
	int greetlen = snprintf(greet, sizeof(greet),
				"RotIDE editor - version %s", ROTIDE_VERSION);
	if (greetlen > cols) {
		greetlen = cols;
	}
	int pad = (cols - greetlen) / 2;
	if (pad) {
		if (!editorAppendGrayBytes(wb, "~", 1)) {
			return 0;
		}
		pad--;
	}
	while (pad--) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return wbAppend(wb, greet, greetlen);
}

static void editorRenderSliceBounds(const struct erow *row, int coloff, int cols, int *start_out,
		int *end_out) {
	int rx = 0;
	int start = -1;
	int end = row->rsize;

	// Map display columns back to byte offsets in row->render.
	// This keeps horizontal scrolling aligned even when wide/zero-width
	// codepoints appear in the rendered data.
	for (int i = 0; i < row->rsize; i++) {
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);

		if (start == -1 && rx + width > coloff) {
			start = i;
		}
		if (start != -1 && width > 0 && rx + width > coloff + cols) {
			end = i;
			break;
		}

		rx += width;
	}

	*start_out = start;
	*end_out = end;
}

static char editorHexUpperDigit(unsigned int value) {
	return value < 10 ? (char)('0' + value) : (char)('A' + (value - 10));
}

static void editorGetSanitizedToken(const char *text, int text_len, int idx, const char **token_out,
		int *token_len_out, int *token_cols_out, int *src_len_out, char escaped[4]) {
	unsigned int cp = 0;
	int src_len = editorUtf8DecodeCodepoint(&text[idx], text_len - idx, &cp);
	if (src_len <= 0) {
		src_len = 1;
	}
	if (src_len > text_len - idx) {
		src_len = text_len - idx;
	}

	const char *token = &text[idx];
	int token_len = src_len;
	int token_cols = editorCharDisplayWidth(&text[idx], text_len - idx);
	if (cp == '\t') {
		escaped[0] = '^';
		escaped[1] = 'I';
		token = escaped;
		token_len = 2;
		token_cols = 2;
	} else if (cp <= 0x1F) {
		escaped[0] = '^';
		escaped[1] = (char)('@' + (int)cp);
		token = escaped;
		token_len = 2;
		token_cols = 2;
	} else if (cp == 0x7F) {
		escaped[0] = '^';
		escaped[1] = '?';
		token = escaped;
		token_len = 2;
		token_cols = 2;
	} else if (cp >= 0x80 && cp <= 0x9F) {
		escaped[0] = '\\';
		escaped[1] = 'x';
		escaped[2] = editorHexUpperDigit((cp >> 4) & 0x0F);
		escaped[3] = editorHexUpperDigit(cp & 0x0F);
		token = escaped;
		token_len = 4;
		token_cols = 4;
	}

	*token_out = token;
	*token_len_out = token_len;
	*token_cols_out = token_cols;
	*src_len_out = src_len;
}

static int editorDisplayTextCols(const char *text) {
	if (text == NULL) {
		return 0;
	}

	int cols = 0;
	int text_len = (int)strlen(text);
	for (int idx = 0; idx < text_len;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&text[idx], text_len - idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - idx) {
			src_len = text_len - idx;
		}
		cols += editorCharDisplayWidth(&text[idx], text_len - idx);
		idx += src_len;
	}

	return cols;
}

static void editorDisplayWrapNextLine(const char *text, int text_len, int start_idx,
		int max_cols, int *end_idx_out, int *cols_out) {
	if (end_idx_out != NULL) {
		*end_idx_out = start_idx;
	}
	if (cols_out != NULL) {
		*cols_out = 0;
	}
	if (text == NULL || text_len <= 0 || start_idx < 0 || start_idx >= text_len ||
			max_cols <= 0) {
		return;
	}

	int idx = start_idx;
	int cols = 0;
	int last_space_end = -1;
	int last_space_cols = 0;
	while (idx < text_len) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&text[idx], text_len - idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - idx) {
			src_len = text_len - idx;
		}
		int token_cols = editorCharDisplayWidth(&text[idx], text_len - idx);
		if (token_cols < 0) {
			token_cols = 0;
		}
		if (cols + token_cols > max_cols) {
			if (last_space_end > start_idx) {
				idx = last_space_end;
				cols = last_space_cols;
			} else if (idx == start_idx) {
				idx += src_len;
				cols += token_cols;
			}
			break;
		}
		cols += token_cols;
		idx += src_len;
		if (cp == ' ') {
			last_space_end = idx;
			last_space_cols = cols;
		}
		if (cols >= max_cols) {
			if (idx < text_len && cp != ' ' && last_space_end > start_idx) {
				idx = last_space_end;
				cols = last_space_cols;
			}
			break;
		}
	}

	if (idx <= start_idx && start_idx < text_len) {
		idx = start_idx + 1;
		cols = 1;
	}
	if (end_idx_out != NULL) {
		*end_idx_out = idx;
	}
	if (cols_out != NULL) {
		*cols_out = cols;
	}
}

static int editorDisplayWrapLineCount(const char *text, int max_cols) {
	if (text == NULL || text[0] == '\0' || max_cols <= 0) {
		return 0;
	}
	int text_len = (int)strlen(text);
	int row_count = 0;
	for (int idx = 0; idx < text_len;) {
		int next_idx = idx;
		editorDisplayWrapNextLine(text, text_len, idx, max_cols, &next_idx, NULL);
		if (next_idx <= idx) {
			break;
		}
		row_count++;
		idx = next_idx;
	}
	return row_count;
}

static int editorAppendDisplayPrefix(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (text == NULL || max_cols <= 0) {
		return 1;
	}

	int text_len = (int)strlen(text);
	int written_cols = 0;
	for (int idx = 0; idx < text_len;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&text[idx], text_len - idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - idx) {
			src_len = text_len - idx;
		}
		int token_cols = editorCharDisplayWidth(&text[idx], text_len - idx);
		if (written_cols + token_cols > max_cols) {
			break;
		}
		if (!wbAppend(wb, &text[idx], (size_t)src_len)) {
			return 0;
		}
		written_cols += token_cols;
		idx += src_len;
	}

	if (written_cols_out != NULL) {
		*written_cols_out = written_cols;
	}
	return 1;
}

static int editorAppendDisplaySuffix(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (text == NULL || max_cols <= 0) {
		return 1;
	}

	int total_cols = editorDisplayTextCols(text);
	if (total_cols <= max_cols) {
		int text_len = (int)strlen(text);
		if (text_len > 0 && !wbAppend(wb, text, (size_t)text_len)) {
			return 0;
		}
		if (written_cols_out != NULL) {
			*written_cols_out = total_cols;
		}
		return 1;
	}

	int text_len = (int)strlen(text);
	int remaining_cols = total_cols;
	int start_idx = 0;
	while (start_idx < text_len && remaining_cols > max_cols) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&text[start_idx], text_len - start_idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - start_idx) {
			src_len = text_len - start_idx;
		}
		remaining_cols -= editorCharDisplayWidth(&text[start_idx], text_len - start_idx);
		start_idx += src_len;
	}

	if (start_idx < text_len && !wbAppend(wb, &text[start_idx], (size_t)(text_len - start_idx))) {
		return 0;
	}

	if (written_cols_out != NULL) {
		*written_cols_out = remaining_cols;
	}
	return 1;
}

static int editorAppendDisplaySlice(struct writeBuf *wb, const char *text, int start_col, int max_cols,
		int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (text == NULL || max_cols <= 0) {
		return 1;
	}
	if (start_col < 0) {
		start_col = 0;
	}

	int text_len = (int)strlen(text);
	int cursor_col = 0;
	int written_cols = 0;
	for (int idx = 0; idx < text_len;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&text[idx], text_len - idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - idx) {
			src_len = text_len - idx;
		}

		int token_cols = editorCharDisplayWidth(&text[idx], text_len - idx);
		if (token_cols < 0) {
			token_cols = 0;
		}

		int token_end = cursor_col + token_cols;
		if (token_end > start_col && written_cols + token_cols <= max_cols) {
			if (!wbAppend(wb, &text[idx], (size_t)src_len)) {
				return 0;
			}
			written_cols += token_cols;
		}
		if (written_cols >= max_cols) {
			break;
		}

		cursor_col += token_cols;
		idx += src_len;
	}

	if (written_cols_out != NULL) {
		*written_cols_out = written_cols;
	}
	return 1;
}

static char *editorSanitizeTextRangeDup(const char *text, int text_len, int *cols_out) {
	if (cols_out != NULL) {
		*cols_out = 0;
	}

	char *out = editorMalloc(1);
	if (out == NULL) {
		return NULL;
	}
	out[0] = '\0';

	if (text == NULL || text_len <= 0) {
		return out;
	}

	size_t out_len = 0;
	int total_cols = 0;
	for (int idx = 0; idx < text_len;) {
		char escaped[4];
		const char *token = NULL;
		int token_len = 0;
		int token_cols = 0;
		int src_len = 0;
		editorGetSanitizedToken(text, text_len, idx, &token, &token_len, &token_cols, &src_len,
				escaped);

		size_t token_len_sz = 0;
		size_t new_len = 0;
		size_t alloc_len = 0;
		if (!editorIntToSize(token_len, &token_len_sz) ||
				!editorSizeAdd(out_len, token_len_sz, &new_len) ||
				new_len > ROTIDE_MAX_TEXT_BYTES ||
				!editorSizeAdd(new_len, 1, &alloc_len)) {
			free(out);
			return NULL;
		}

		char *grown = editorRealloc(out, alloc_len);
		if (grown == NULL) {
			free(out);
			return NULL;
		}
		out = grown;
		memcpy(&out[out_len], token, token_len_sz);
		out_len = new_len;
		out[out_len] = '\0';

		total_cols += token_cols;
		idx += src_len;
	}

	if (cols_out != NULL) {
		*cols_out = total_cols;
	}
	return out;
}

static char *editorSanitizeTextDup(const char *text, int *cols_out) {
	if (text == NULL) {
		return editorSanitizeTextRangeDup("", 0, cols_out);
	}
	int text_len = (int)strlen(text);
	return editorSanitizeTextRangeDup(text, text_len, cols_out);
}

static int editorDiagnosticMessageIsInlineSpace(unsigned int cp) {
	return cp <= 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

static char *editorSanitizeDiagnosticMessageDup(const char *text, int *cols_out) {
	if (cols_out != NULL) {
		*cols_out = 0;
	}

	struct writeBuf out = WRITEBUF_INIT;
	if (text == NULL || text[0] == '\0') {
		if (!wbAppend(&out, "\0", 1)) {
			return NULL;
		}
		return out.b;
	}

	int text_len = (int)strlen(text);
	int total_cols = 0;
	int pending_space = 0;
	for (int idx = 0; idx < text_len;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&text[idx], text_len - idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - idx) {
			src_len = text_len - idx;
		}

		if (editorDiagnosticMessageIsInlineSpace(cp)) {
			pending_space = out.len > 0;
			idx += src_len;
			continue;
		}

		if (pending_space) {
			if (!wbAppend(&out, " ", 1)) {
				wbFree(&out);
				return NULL;
			}
			total_cols++;
			pending_space = 0;
		}
		if (!wbAppend(&out, &text[idx], (size_t)src_len)) {
			wbFree(&out);
			return NULL;
		}
		total_cols += editorCharDisplayWidth(&text[idx], text_len - idx);
		idx += src_len;
	}

	if (!wbAppend(&out, "\0", 1)) {
		wbFree(&out);
		return NULL;
	}
	if (cols_out != NULL) {
		*cols_out = total_cols;
	}
	return out.b;
}

// Sanitize untrusted UI text (filename/status/message) using the same control
// escaping policy as file rows. Tabs intentionally become "^I" here instead of
// visual tab expansion so these bars keep deterministic layout.
static int editorAppendSanitizedText(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (text == NULL) {
		return 1;
	}

	int text_len = (int)strlen(text);
	int written_cols = 0;
	for (int idx = 0; idx < text_len;) {
		char escaped[4];
		const char *token = NULL;
		int token_len = 0;
		int token_cols = 0;
		int src_len = 0;
		editorGetSanitizedToken(text, text_len, idx, &token, &token_len, &token_cols, &src_len,
				escaped);

		if (max_cols >= 0 && written_cols + token_cols > max_cols) {
			break;
		}
		if (!wbAppend(wb, token, (size_t)token_len)) {
			return 0;
		}

		written_cols += token_cols;
		idx += src_len;
	}

	if (written_cols_out != NULL) {
		*written_cols_out = written_cols;
	}
	return 1;
}

static int editorAppendSanitizedMiddleTruncated(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (max_cols <= 0) {
		return 1;
	}

	int sanitized_cols = 0;
	char *sanitized = editorSanitizeTextDup(text, &sanitized_cols);
	if (sanitized == NULL) {
		return 0;
	}

	int written_cols = 0;
	if (sanitized_cols <= max_cols) {
		size_t len = strlen(sanitized);
		if (len > 0 && !wbAppend(wb, sanitized, len)) {
			free(sanitized);
			return 0;
		}
		written_cols = sanitized_cols;
	} else {
		const char *marker = ROTIDE_TAB_TRUNC_MARKER;
		int marker_cols = editorDisplayTextCols(marker);
		if (max_cols <= marker_cols) {
			if (!editorAppendDisplayPrefix(wb, marker, max_cols, &written_cols)) {
				free(sanitized);
				return 0;
			}
		} else {
			int prefix_cols = (max_cols - marker_cols + 1) / 2;
			int suffix_cols = max_cols - marker_cols - prefix_cols;

			int prefix_written = 0;
			int suffix_written = 0;
			if (!editorAppendDisplayPrefix(wb, sanitized, prefix_cols, &prefix_written)) {
				free(sanitized);
				return 0;
			}
			if (!wbAppend(wb, marker, strlen(marker))) {
				free(sanitized);
				return 0;
			}
			if (!editorAppendDisplaySuffix(wb, sanitized, suffix_cols, &suffix_written)) {
				free(sanitized);
				return 0;
			}
			written_cols = prefix_written + marker_cols + suffix_written;
		}
	}

	free(sanitized);
	if (written_cols_out != NULL) {
		*written_cols_out = written_cols;
	}
	return 1;
}

static int editorAppendSanitizedStatusPath(struct writeBuf *wb, const char *path, int max_cols,
		int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (path == NULL || max_cols <= 0) {
		return 1;
	}

	int full_cols = 0;
	char *sanitized_full = editorSanitizeTextDup(path, &full_cols);
	if (sanitized_full == NULL) {
		return 0;
	}
	if (full_cols <= max_cols) {
		size_t full_len = strlen(sanitized_full);
		int ok = full_len == 0 || wbAppend(wb, sanitized_full, full_len);
		free(sanitized_full);
		if (!ok) {
			return 0;
		}
		if (written_cols_out != NULL) {
			*written_cols_out = full_cols;
		}
		return 1;
	}

	const char *basename = path;
	const char *slash = strrchr(path, '/');
	if (slash != NULL && slash[1] != '\0') {
		basename = slash + 1;
	}
	size_t dir_len_sz = (size_t)(basename - path);
	if (dir_len_sz > (size_t)INT_MAX) {
		free(sanitized_full);
		return 0;
	}
	int dir_len = (int)dir_len_sz;

	int basename_cols = 0;
	char *sanitized_basename = editorSanitizeTextDup(basename, &basename_cols);
	if (sanitized_basename == NULL) {
		free(sanitized_full);
		return 0;
	}

	const char *marker = ROTIDE_TAB_TRUNC_MARKER;
	int marker_cols = editorDisplayTextCols(marker);
	int written_cols = 0;
	if (basename_cols >= max_cols) {
		if (max_cols <= marker_cols) {
			if (!editorAppendDisplayPrefix(wb, marker, max_cols, &written_cols)) {
				free(sanitized_basename);
				free(sanitized_full);
				return 0;
			}
		} else {
			int suffix_written = 0;
			if (!wbAppend(wb, marker, strlen(marker)) ||
					!editorAppendDisplaySuffix(wb, sanitized_basename, max_cols - marker_cols,
							&suffix_written)) {
				free(sanitized_basename);
				free(sanitized_full);
				return 0;
			}
			written_cols = marker_cols + suffix_written;
		}
	} else {
		int prefix_budget = max_cols - basename_cols;
		int prefix_written = 0;
		if (prefix_budget > 0 && dir_len > 0) {
			int dir_cols = 0;
			char *sanitized_dir = editorSanitizeTextRangeDup(path, dir_len, &dir_cols);
			if (sanitized_dir == NULL) {
				free(sanitized_basename);
				free(sanitized_full);
				return 0;
			}

			if (dir_cols <= prefix_budget) {
				if (!editorAppendDisplayPrefix(wb, sanitized_dir, prefix_budget, &prefix_written)) {
					free(sanitized_dir);
					free(sanitized_basename);
					free(sanitized_full);
					return 0;
				}
			} else if (prefix_budget <= marker_cols) {
				if (!editorAppendDisplaySuffix(wb, sanitized_dir, prefix_budget, &prefix_written)) {
					free(sanitized_dir);
					free(sanitized_basename);
					free(sanitized_full);
					return 0;
				}
			} else {
				int suffix_written = 0;
				if (!wbAppend(wb, marker, strlen(marker)) ||
						!editorAppendDisplaySuffix(wb, sanitized_dir, prefix_budget - marker_cols,
								&suffix_written)) {
					free(sanitized_dir);
					free(sanitized_basename);
					free(sanitized_full);
					return 0;
				}
				prefix_written = marker_cols + suffix_written;
			}

			free(sanitized_dir);
		}

		int basename_written = 0;
		if (!editorAppendDisplayPrefix(wb, sanitized_basename, max_cols - prefix_written,
					&basename_written)) {
			free(sanitized_basename);
			free(sanitized_full);
			return 0;
		}
		written_cols = prefix_written + basename_written;
	}

	free(sanitized_basename);
	free(sanitized_full);
	if (written_cols_out != NULL) {
		*written_cols_out = written_cols;
	}
	return 1;
}

static int editorSelectionSpanForRow(int row_idx, int *start_out, int *end_out) {
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	if (E.column_select_active) {
		struct editorColumnSelectionRect rect;
		if (!editorColumnSelectionGetRect(&rect)) {
			return 0;
		}
		if (row_idx < rect.top_cy || row_idx > rect.bottom_cy) {
			return 0;
		}
		int cx_start = 0;
		int cx_end = 0;
		if (!editorColumnSelectionRowSpan(row_idx, rect.left_rx, rect.right_rx, &cx_start, &cx_end)) {
			return 0;
		}
if (cx_end <= cx_start) {
			return 0;
		}
		*start_out = cx_start;
		*end_out = cx_end;
		return 1;
	}

	struct editorSelectionRange selection;
	if (!editorGetSelectionRange(&selection)) {
		return 0;
	}
	if (row_idx < selection.start_cy || row_idx > selection.end_cy) {
		return 0;
	}

	int start = 0;
	int end = E.rows[row_idx].size;
	if (selection.start_cy == selection.end_cy) {
		start = selection.start_cx;
		end = selection.end_cx;
	} else {
		if (row_idx == selection.start_cy) {
			start = selection.start_cx;
		}
		if (row_idx == selection.end_cy && selection.end_cy < E.numrows) {
			end = selection.end_cx;
		}
	}

	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static int editorSearchSpanForRow(int row_idx, int *start_out, int *end_out) {
	if (row_idx < 0 || row_idx >= E.numrows || E.search_match_len <= 0) {
		return 0;
	}

	int start_row = 0;
	int start_col = 0;
	if (!editorBufferOffsetToPos(E.search_match_offset, &start_row, &start_col)) {
		return 0;
	}

	size_t end_offset = E.search_match_offset + (size_t)E.search_match_len;
	int end_row = 0;
	int end_col = 0;
	if (!editorBufferOffsetToPos(end_offset, &end_row, &end_col)) {
		return 0;
	}

	if (row_idx < start_row || row_idx > end_row) {
		return 0;
	}

	int start = 0;
	int end = E.rows[row_idx].size;
	if (start_row == end_row) {
		start = start_col;
		end = end_col;
	} else {
		if (row_idx == start_row) {
			start = start_col;
		}
		if (row_idx == end_row && end_row < E.numrows) {
			end = end_col;
		}
	}

	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static enum editorSyntaxHighlightClass editorSyntaxClassAtRenderIdx(
		const struct editorRowSyntaxSpan *spans, int span_count, int render_idx) {
	enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
	for (int i = 0; i < span_count; i++) {
		if (spans[i].end_render_idx <= spans[i].start_render_idx) {
			continue;
		}
		if (render_idx >= spans[i].start_render_idx && render_idx < spans[i].end_render_idx) {
			highlight_class = spans[i].highlight_class;
		}
	}
	return highlight_class;
}

static int editorDiagnosticSeverityIsError(int severity) {
	return severity == 1;
}

static int editorDiagnosticRangeForRow(const struct editorLspDiagnostic *diagnostic,
		int row_idx, int row_size, int *start_out, int *end_out) {
	if (diagnostic == NULL || start_out == NULL || end_out == NULL ||
			row_idx < diagnostic->start_line || row_idx > diagnostic->end_line ||
			diagnostic->end_line < diagnostic->start_line) {
		return 0;
	}

	int start = row_idx == diagnostic->start_line ? diagnostic->start_character : 0;
	int end = row_idx == diagnostic->end_line ? diagnostic->end_character : row_size;
	if (start < 0) {
		start = 0;
	}
	if (end < 0) {
		end = 0;
	}
	if (start > row_size) {
		start = row_size;
	}
	if (end > row_size) {
		end = row_size;
	}
	if (end <= start && start < row_size) {
		end = start + 1;
	}
	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static int editorDiagnosticAtRenderIdx(
		const struct editorRowSyntaxSpan *spans, int span_count, int render_idx) {
	for (int i = 0; i < span_count; i++) {
		if (spans[i].end_render_idx <= spans[i].start_render_idx) {
			continue;
		}
		if (render_idx >= spans[i].start_render_idx && render_idx < spans[i].end_render_idx) {
			return 1;
		}
	}
	return 0;
}

static int editorBuildDiagnosticRenderSpansForRow(int row_idx, const struct erow *row,
		struct editorRowSyntaxSpan *spans, int max_spans, int *span_count_out) {
	if (span_count_out != NULL) {
		*span_count_out = 0;
	}
	if (row == NULL || spans == NULL || span_count_out == NULL || max_spans <= 0 ||
			E.lsp_diagnostics == NULL || E.lsp_diagnostic_count <= 0) {
		return 1;
	}

	int count = 0;
	for (int i = 0; i < E.lsp_diagnostic_count && count < max_spans; i++) {
		if (!editorDiagnosticSeverityIsError(E.lsp_diagnostics[i].severity)) {
			continue;
		}
		int start = 0;
		int end = 0;
		if (!editorDiagnosticRangeForRow(&E.lsp_diagnostics[i], row_idx, row->size, &start,
					&end)) {
			continue;
		}
		int render_start = editorRowCxToRenderIdx(row, start);
		int render_end = editorRowCxToRenderIdx(row, end);
		if (render_end <= render_start) {
			continue;
		}
		spans[count].start_render_idx = render_start;
		spans[count].end_render_idx = render_end;
		spans[count].highlight_class = EDITOR_SYNTAX_HL_NONE;
		count++;
	}
	*span_count_out = count;
	return 1;
}

static int editorAppendUnderlineToggle(struct writeBuf *wb, int on) {
	if (on) {
		return wbAppend(wb, VT100_UNDERLINE_ON_4, 4);
	}
	return wbAppend(wb, VT100_UNDERLINE_OFF_5, 5);
}

static int editorAppendUnderlineColor(struct writeBuf *wb, int red) {
	if (red) {
		return wbAppend(wb, VT100_UNDERLINE_COLOR_RED_9, 9);
	}
	return wbAppend(wb, VT100_UNDERLINE_COLOR_DEFAULT_5, 5);
}

static int editorDrawRenderSliceWithSyntax(struct writeBuf *wb, const struct erow *row,
		int segment_start, int segment_end, const struct editorRowSyntaxSpan *spans,
		int span_count, const struct editorRowSyntaxSpan *diagnostic_spans,
		int diagnostic_span_count, int hover_render_start, int hover_render_end) {
	if (segment_end <= segment_start) {
		return 1;
	}
	if ((spans == NULL || span_count <= 0 || E.syntax_state == NULL ||
				E.syntax_language == EDITOR_SYNTAX_NONE) &&
			(diagnostic_spans == NULL || diagnostic_span_count <= 0)) {
		return wbAppend(wb, &row->render[segment_start], (size_t)(segment_end - segment_start));
	}

	struct editorThemeColor base_color = E.theme.ui[EDITOR_THEME_UI_FOREGROUND];
	struct editorThemeColor active_color = base_color;
	int active_color_emitted = 0;
	int underline_on = 0;
	int underline_red = 0;
	int has_hover = hover_render_end > hover_render_start;
	int pos = segment_start;
	while (pos < segment_end) {
		enum editorSyntaxHighlightClass highlight_class =
				editorSyntaxClassAtRenderIdx(spans, span_count, pos);
		struct editorThemeColor next_color = base_color;
		if (highlight_class > EDITOR_SYNTAX_HL_NONE &&
				highlight_class < EDITOR_SYNTAX_HL_CLASS_COUNT) {
			next_color = E.theme.syntax[highlight_class];
		}

		if (!editorThemeColorEquals(next_color, active_color)) {
			if (!editorAppendThemeForeground(wb, next_color)) {
				return 0;
			}
			active_color = next_color;
			active_color_emitted = 1;
		}

		int diag_here = editorDiagnosticAtRenderIdx(diagnostic_spans,
				diagnostic_span_count, pos);
		int hover_here = has_hover && pos >= hover_render_start && pos < hover_render_end;
		int next_on = diag_here || hover_here;
		int next_red = diag_here;
		if (next_on != underline_on) {
			if (!editorAppendUnderlineToggle(wb, next_on)) {
				return 0;
			}
			underline_on = next_on;
		}
		if (next_red != underline_red) {
			if (!editorAppendUnderlineColor(wb, next_red)) {
				return 0;
			}
			underline_red = next_red;
		}

		int next = segment_end;
		for (int i = 0; i < span_count; i++) {
			int span_start = spans[i].start_render_idx;
			int span_end = spans[i].end_render_idx;
			if (span_end <= span_start) {
				continue;
			}
			if (span_start > pos && span_start < next) {
				next = span_start;
			}
			if (span_end > pos && span_end < next) {
				next = span_end;
			}
		}
		for (int i = 0; i < diagnostic_span_count; i++) {
			int span_start = diagnostic_spans[i].start_render_idx;
			int span_end = diagnostic_spans[i].end_render_idx;
			if (span_end <= span_start) {
				continue;
			}
			if (span_start > pos && span_start < next) {
				next = span_start;
			}
			if (span_end > pos && span_end < next) {
				next = span_end;
			}
		}
		if (has_hover) {
			if (hover_render_start > pos && hover_render_start < next) {
				next = hover_render_start;
			}
			if (hover_render_end > pos && hover_render_end < next) {
				next = hover_render_end;
			}
		}
		if (next <= pos) {
			unsigned int cp = 0;
			int step = editorUtf8DecodeCodepoint(&row->render[pos], segment_end - pos, &cp);
			(void)cp;
			if (step <= 0) {
				step = 1;
			}
			if (step > segment_end - pos) {
				step = segment_end - pos;
			}
			next = pos + step;
		}

		if (!wbAppend(wb, &row->render[pos], (size_t)(next - pos))) {
			return 0;
		}
		pos = next;
	}

	if (underline_on && !editorAppendUnderlineToggle(wb, 0)) {
		return 0;
	}
	if (underline_red && !editorAppendUnderlineColor(wb, 0)) {
		return 0;
	}
	if (active_color_emitted && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	return 1;
}

static int editorDrawRenderSlice(struct writeBuf *wb, struct erow *row, int row_idx, int coloff,
		int cols) {
	if (cols <= 0 || coloff < 0 || row->rsize <= 0) {
		return 1;
	}

	int start = -1;
	int end = row->rsize;
	editorRenderSliceBounds(row, coloff, cols, &start, &end);
	if (start == -1 || end <= start) {
		return 1;
	}

	int highlight_start_chars = -1;
	int highlight_len_chars = 0;

	int selection_start = 0;
	int selection_end = 0;
	if (editorSelectionSpanForRow(row_idx, &selection_start, &selection_end)) {
		highlight_start_chars = selection_start;
		highlight_len_chars = selection_end - selection_start;
	} else if (editorSearchSpanForRow(row_idx, &selection_start, &selection_end)) {
		highlight_start_chars = selection_start;
		highlight_len_chars = selection_end - selection_start;
	}

	struct editorRowSyntaxSpan syntax_spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int syntax_span_count = 0;
	if (!editorSyntaxRowRenderSpans(row_idx, syntax_spans, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW,
				&syntax_span_count)) {
		syntax_span_count = 0;
	}
	struct editorRowSyntaxSpan diagnostic_spans[EDITOR_DIAGNOSTIC_MAX_RENDER_SPANS_PER_ROW];
	int diagnostic_span_count = 0;
	if (!editorBuildDiagnosticRenderSpansForRow(row_idx, row, diagnostic_spans,
				EDITOR_DIAGNOSTIC_MAX_RENDER_SPANS_PER_ROW, &diagnostic_span_count)) {
		diagnostic_span_count = 0;
	}

	int hover_render_start = -1;
	int hover_render_end = -1;
	if (E.hover_link_active && E.hover_link_row == row_idx &&
			E.hover_link_cx_end > E.hover_link_cx_start) {
		int hov_start_clamped = E.hover_link_cx_start;
		int hov_end_clamped = E.hover_link_cx_end;
		if (hov_start_clamped < 0) {
			hov_start_clamped = 0;
		}
		if (hov_end_clamped > row->size) {
			hov_end_clamped = row->size;
		}
		if (hov_end_clamped > hov_start_clamped) {
			hover_render_start = editorRowCxToRenderIdx(row, hov_start_clamped);
			hover_render_end = editorRowCxToRenderIdx(row, hov_end_clamped);
		}
	}

	if (highlight_len_chars <= 0) {
		return editorDrawRenderSliceWithSyntax(wb, row, start, end, syntax_spans,
				syntax_span_count, diagnostic_spans, diagnostic_span_count,
				hover_render_start, hover_render_end);
	}

	int match_start_chars = highlight_start_chars;
	if (match_start_chars < 0) {
		match_start_chars = 0;
	}
	if (match_start_chars > row->size) {
		match_start_chars = row->size;
	}
	long long match_end_ll = (long long)match_start_chars + (long long)highlight_len_chars;
	if (match_end_ll < match_start_chars) {
		match_end_ll = match_start_chars;
	}
	if (match_end_ll > row->size) {
		match_end_ll = row->size;
	}
	int match_end_chars = (int)match_end_ll;

	// Convert char-space selection/search boundaries into render byte indices
	// with the same mapper used by row rendering and cursor calculations.
	int match_render_start = editorRowCxToRenderIdx(row, match_start_chars);
	int match_render_end = editorRowCxToRenderIdx(row, match_end_chars);
	if (match_render_end <= match_render_start) {
		return editorDrawRenderSliceWithSyntax(wb, row, start, end, syntax_spans,
				syntax_span_count, diagnostic_spans, diagnostic_span_count,
				hover_render_start, hover_render_end);
	}

	int highlight_start = start > match_render_start ? start : match_render_start;
	int highlight_end = end < match_render_end ? end : match_render_end;
	if (highlight_end <= highlight_start) {
		return editorDrawRenderSliceWithSyntax(wb, row, start, end, syntax_spans,
				syntax_span_count, diagnostic_spans, diagnostic_span_count,
				hover_render_start, hover_render_end);
	}

	if (highlight_start > start &&
			!editorDrawRenderSliceWithSyntax(wb, row, start, highlight_start, syntax_spans,
					syntax_span_count, diagnostic_spans, diagnostic_span_count,
					hover_render_start, hover_render_end)) {
		return 0;
	}
	if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
		return 0;
	}
	if (!wbAppend(wb, &row->render[highlight_start], highlight_end - highlight_start)) {
		return 0;
	}
	if (!editorAppendTextRowReset(wb)) {
		return 0;
	}
	if (highlight_end < end &&
			!editorDrawRenderSliceWithSyntax(wb, row, highlight_end, end, syntax_spans,
					syntax_span_count, diagnostic_spans, diagnostic_span_count,
					hover_render_start, hover_render_end)) {
		return 0;
	}

	return 1;
}

static int editorRenderSliceDisplayCols(const struct erow *row, int coloff, int cols,
		int *has_right_overflow_out) {
	if (has_right_overflow_out != NULL) {
		*has_right_overflow_out = 0;
	}
	if (row == NULL || cols <= 0 || coloff < 0 || row->rsize <= 0) {
		return 0;
	}

	int start = -1;
	int end = row->rsize;
	editorRenderSliceBounds(row, coloff, cols, &start, &end);
	if (start == -1 || end <= start) {
		return 0;
	}

	int drawn_cols = 0;
	for (int i = start; i < end;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], end - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > end - i) {
			src_len = end - i;
		}
		drawn_cols += editorCharDisplayWidth(&row->render[i], end - i);
		i += src_len;
	}

	if (has_right_overflow_out != NULL) {
		*has_right_overflow_out = end < row->rsize;
	}
	return drawn_cols;
}

static int editorWrapBodyCols(void) {
	int body_cols = editorTextBodyViewportCols(E.window_cols);
	return body_cols < 1 ? 1 : body_cols;
}

static int editorWrapContinuationIndentCols(const struct erow *row, int body_cols) {
	if (row == NULL || body_cols <= 1 || row->rsize <= 0) {
		return 0;
	}

	int max_indent = body_cols - 1;
	int cols = 0;
	for (int i = 0; i < row->rsize && cols < max_indent;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], row->rsize - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > row->rsize - i) {
			src_len = row->rsize - i;
		}
		if (cp != ' ' && cp != '\t') {
			break;
		}
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);
		if (cols + width > max_indent) {
			break;
		}
		cols += width;
		i += src_len;
	}
	return cols;
}

static int editorWrapBreaksAfterCodepoint(unsigned int cp) {
	switch (cp) {
		case ' ':
		case '\t':
		case '.':
		case ',':
		case ';':
		case ':':
		case '/':
		case '\\':
		case '-':
		case '_':
		case '(':
		case '[':
		case '{':
		case ')':
		case ']':
		case '}':
		case '=':
		case '+':
		case '&':
		case '|':
			return 1;
		default:
			return 0;
	}
}

static int editorWrapNextStartCol(const struct erow *row, int start_col, int available_cols,
		int total_cols) {
	if (row == NULL || available_cols <= 0 || start_col < 0 || start_col >= total_cols) {
		return total_cols;
	}
	if (start_col + available_cols >= total_cols) {
		return total_cols;
	}

	int target_col = start_col + available_cols;
	int best_break_col = -1;
	int best_fit_col = -1;
	int first_advance_col = -1;
	int rx = 0;
	for (int i = 0; i < row->rsize;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], row->rsize - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > row->rsize - i) {
			src_len = row->rsize - i;
		}
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);
		int next_rx = rx + width;
		if (next_rx > start_col && first_advance_col == -1) {
			first_advance_col = next_rx;
		}
		if (next_rx > start_col && next_rx <= target_col) {
			best_fit_col = next_rx;
			if (editorWrapBreaksAfterCodepoint(cp)) {
				best_break_col = next_rx;
			}
		}
		if (next_rx > target_col) {
			break;
		}
		rx = next_rx;
		i += src_len;
	}

	if (best_break_col > start_col) {
		return best_break_col;
	}
	if (best_fit_col > start_col) {
		return best_fit_col;
	}
	if (first_advance_col > start_col) {
		return first_advance_col;
	}
	return total_cols;
}

static int editorWrapCacheReserve(struct erow *row, int needed_capacity) {
	if (needed_capacity <= row->wrap_cache_capacity) {
		return 1;
	}
	int new_cap = row->wrap_cache_capacity > 0 ? row->wrap_cache_capacity : 4;
	while (new_cap < needed_capacity) {
		new_cap *= 2;
	}
	int *grown = realloc(row->wrap_cache_segments, (size_t)new_cap * sizeof(int));
	if (grown == NULL) {
		return 0;
	}
	row->wrap_cache_segments = grown;
	row->wrap_cache_capacity = new_cap;
	return 1;
}

static void editorWrapEnsureCache(struct erow *row, int body_cols) {
	if (row == NULL) {
		return;
	}
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (row->wrap_cache_body_cols == body_cols && row->wrap_cache_segment_count > 0) {
		return;
	}

	row->wrap_cache_body_cols = body_cols;
	row->wrap_cache_indent_cols = 0;
	row->wrap_cache_segment_count = 1;
	if (!editorWrapCacheReserve(row, 1)) {
		row->wrap_cache_body_cols = 0;
		row->wrap_cache_segment_count = 0;
		return;
	}
	row->wrap_cache_segments[0] = 0;

	int total_cols = row->render_display_cols;
	if (total_cols <= 0 || total_cols <= body_cols) {
		return;
	}

	int indent_cols = editorWrapContinuationIndentCols(row, body_cols);
	row->wrap_cache_indent_cols = indent_cols;

	int start_col = 0;
	int count = 1;
	while (start_col < total_cols) {
		int current_indent = count == 1 ? 0 : indent_cols;
		int available_cols = body_cols - current_indent;
		if (available_cols < 1) {
			available_cols = 1;
		}
		int next_start = editorWrapNextStartCol(row, start_col, available_cols, total_cols);
		if (next_start >= total_cols || next_start <= start_col) {
			break;
		}
		if (!editorWrapCacheReserve(row, count + 1)) {
			break;
		}
		row->wrap_cache_segments[count] = next_start;
		count++;
		start_col = next_start;
	}
	row->wrap_cache_segment_count = count;
}

static void editorWrapSegmentInfo(struct erow *row, int segment_idx, int body_cols,
		int *start_col_out, int *available_cols_out, int *indent_cols_out) {
	if (start_col_out != NULL) {
		*start_col_out = 0;
	}
	if (available_cols_out != NULL) {
		*available_cols_out = body_cols < 1 ? 1 : body_cols;
	}
	if (indent_cols_out != NULL) {
		*indent_cols_out = 0;
	}
	if (row == NULL) {
		return;
	}
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (segment_idx < 0) {
		segment_idx = 0;
	}

	editorWrapEnsureCache(row, body_cols);
	if (row->wrap_cache_segment_count <= 0) {
		return;
	}
	if (segment_idx >= row->wrap_cache_segment_count) {
		segment_idx = row->wrap_cache_segment_count - 1;
	}

	int start_col = row->wrap_cache_segments[segment_idx];
	int current_indent = segment_idx == 0 ? 0 : row->wrap_cache_indent_cols;
	int available_cols = body_cols - current_indent;
	if (available_cols < 1) {
		available_cols = 1;
		current_indent = body_cols > 1 ? body_cols - 1 : 0;
	}

	if (start_col_out != NULL) {
		*start_col_out = start_col;
	}
	if (available_cols_out != NULL) {
		*available_cols_out = available_cols;
	}
	if (indent_cols_out != NULL) {
		*indent_cols_out = current_indent;
	}
}

static int editorWrapSegmentCountForRowIndex(int row_idx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 1;
	}
	editorWrapEnsureCache(&E.rows[row_idx], body_cols);
	int count = E.rows[row_idx].wrap_cache_segment_count;
	return count > 0 ? count : 1;
}

static int editorWrapCursorSegmentForRx(struct erow *row, int rx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (rx <= 0 || row == NULL) {
		return 0;
	}
	editorWrapEnsureCache(row, body_cols);
	int count = row->wrap_cache_segment_count;
	if (count <= 1) {
		return 0;
	}
	int lo = 0;
	int hi = count - 1;
	while (lo < hi) {
		int mid = lo + (hi - lo + 1) / 2;
		if (row->wrap_cache_segments[mid] < rx) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return lo;
}

static void editorWrappedClampViewportOffsets(void) {
	if (!E.line_wrap_enabled) {
		E.wrapoff = 0;
		return;
	}
	E.coloff = 0;
	if (E.rowoff < 0) {
		E.rowoff = 0;
	}
	int max_rowoff = E.numrows > 0 ? E.numrows - 1 : 0;
	if (E.rowoff > max_rowoff) {
		E.rowoff = max_rowoff;
	}
	int body_cols = editorWrapBodyCols();
	int max_wrapoff = editorWrapSegmentCountForRowIndex(E.rowoff, body_cols) - 1;
	if (E.wrapoff < 0) {
		E.wrapoff = 0;
	}
	if (E.wrapoff > max_wrapoff) {
		E.wrapoff = max_wrapoff;
	}
}

static void editorWrappedAdvancePosition(int *row_idx, int *segment_idx, int body_cols) {
	if (row_idx == NULL || segment_idx == NULL) {
		return;
	}
	if (*row_idx >= E.numrows) {
		return;
	}
	int segment_count = editorWrapSegmentCountForRowIndex(*row_idx, body_cols);
	if (*segment_idx + 1 < segment_count) {
		(*segment_idx)++;
		return;
	}
	(*row_idx)++;
	*segment_idx = 0;
}

static void editorWrappedMoveBackPosition(int *row_idx, int *segment_idx, int body_cols) {
	if (row_idx == NULL || segment_idx == NULL || (*row_idx <= 0 && *segment_idx <= 0)) {
		return;
	}
	if (*segment_idx > 0) {
		(*segment_idx)--;
		return;
	}
	(*row_idx)--;
	*segment_idx = editorWrapSegmentCountForRowIndex(*row_idx, body_cols) - 1;
}

static int editorWrappedPositionBefore(int row_a, int segment_a, int row_b, int segment_b) {
	return row_a < row_b || (row_a == row_b && segment_a < segment_b);
}

static int editorWrappedDistanceForward(int from_row, int from_segment, int to_row, int to_segment,
		int max_distance, int body_cols, int *distance_out) {
	int row = from_row;
	int segment = from_segment;
	for (int distance = 0; distance <= max_distance; distance++) {
		if (row == to_row && segment == to_segment) {
			if (distance_out != NULL) {
				*distance_out = distance;
			}
			return 1;
		}
		editorWrappedAdvancePosition(&row, &segment, body_cols);
		if (row >= E.numrows) {
			break;
		}
	}
	return 0;
}

int editorViewportTextScreenRowToBufferRow(int screen_row, int *row_idx_out,
		int *segment_coloff_out) {
	return editorViewportTextScreenRowToBufferPosition(screen_row, row_idx_out, segment_coloff_out,
			NULL);
}

int editorViewportTextScreenRowToBufferPosition(int screen_row, int *row_idx_out,
		int *segment_coloff_out, int *segment_indent_cols_out) {
	if (row_idx_out == NULL || segment_coloff_out == NULL || screen_row < 0) {
		return 0;
	}
	if (!E.line_wrap_enabled) {
		*row_idx_out = E.rowoff + screen_row;
		*segment_coloff_out = E.coloff;
		if (segment_indent_cols_out != NULL) {
			*segment_indent_cols_out = 0;
		}
		return 1;
	}

	int row = E.rowoff;
	int segment = E.wrapoff;
	int body_cols = editorWrapBodyCols();
	for (int y = 0; y < screen_row; y++) {
		editorWrappedAdvancePosition(&row, &segment, body_cols);
	}
	*row_idx_out = row;
	int available_cols = 0;
	int indent_cols = 0;
	if (row >= 0 && row < E.numrows) {
		editorWrapSegmentInfo(&E.rows[row], segment, body_cols, segment_coloff_out,
				&available_cols, &indent_cols);
	} else {
		*segment_coloff_out = 0;
	}
	if (segment_indent_cols_out != NULL) {
		*segment_indent_cols_out = indent_cols;
	}
	return 1;
}

static int editorAppendGrayGlyph(struct writeBuf *wb, const char *glyph, size_t glyph_len) {
	return editorAppendGrayBytes(wb, glyph, glyph_len);
}

static int editorCurrentLineHighlightApplies(int row_idx, int segment_coloff) {
	if (!E.current_line_highlight_enabled || row_idx != E.cy) {
		return 0;
	}
	if (E.pane_focus != EDITOR_PANE_TEXT && !E.is_preview) {
		const char *lsp_path = NULL;
		int lsp_line = -1;
		int lsp_character = -1;
		if (E.pane_focus != EDITOR_PANE_DRAWER ||
				E.drawer_mode != EDITOR_DRAWER_MODE_LSP ||
				!editorDrawerSelectedLspLocation(&lsp_path, &lsp_line, &lsp_character) ||
				lsp_path == NULL || E.filename == NULL ||
				strcmp(lsp_path, E.filename) != 0 || lsp_line != E.cy) {
			return 0;
		}
	}
	if (!E.line_wrap_enabled) {
		return 1;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	int body_cols = editorWrapBodyCols();
	int cursor_segment = editorWrapCursorSegmentForRx(&E.rows[row_idx], E.rx, body_cols);
	int cursor_segment_coloff = 0;
	editorWrapSegmentInfo(&E.rows[row_idx], cursor_segment, body_cols, &cursor_segment_coloff,
			NULL, NULL);
	return segment_coloff == cursor_segment_coloff;
}

static int editorDrawLineNumberGutter(struct writeBuf *wb, int row_idx, int segment_coloff,
		int gutter_cols) {
	if (gutter_cols <= 0) {
		return 1;
	}

	if (row_idx >= 0 && row_idx < E.numrows && segment_coloff == 0) {
		char number[32];
		int len = snprintf(number, sizeof(number), "%d", row_idx + 1);
		if (len < 0) {
			return 0;
		}
		if (len >= (int)sizeof(number)) {
			len = (int)sizeof(number) - 1;
		}

		int number_cols = gutter_cols > 1 ? gutter_cols - 1 : gutter_cols;
		int pad_cols = number_cols - len;
		if (pad_cols < 0) {
			pad_cols = 0;
		}

		if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_LINE_NUMBER)) {
			return 0;
		}
		for (int pad = 0; pad < pad_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}
		const char *visible_number = number;
		if (len > number_cols) {
			visible_number = number + (len - number_cols);
			len = number_cols;
		}
		if (len > 0 && !wbAppend(wb, visible_number, (size_t)len)) {
			return 0;
		}
		if (gutter_cols > 1 && !wbAppend(wb, " ", 1)) {
			return 0;
		}
		return editorAppendThemeBaseForeground(wb);
	}

	for (int col = 0; col < gutter_cols; col++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return 1;
}

static int editorDrawFileRowWrapped(struct writeBuf *wb, size_t i, int text_cols,
		int segment_coloff) {
	struct erow *row = &E.rows[i];
	if (text_cols >= 3) {
		int body_cols = editorTextBodyViewportCols(E.window_cols);
		int indent_cols = segment_coloff > 0 ? editorWrapContinuationIndentCols(row, body_cols) : 0;
		int available_cols = body_cols - indent_cols;
		if (available_cols < 1) {
			available_cols = 1;
			indent_cols = body_cols > 1 ? body_cols - 1 : 0;
		}
		int total_cols = row->render_display_cols;
		int draw_cols = available_cols;
		if (segment_coloff < total_cols) {
			int next_start = editorWrapNextStartCol(row, segment_coloff, available_cols, total_cols);
			if (next_start > segment_coloff && next_start - segment_coloff < draw_cols) {
				draw_cols = next_start - segment_coloff;
			}
		}
		int rendered_cols =
				editorRenderSliceDisplayCols(row, segment_coloff, draw_cols, NULL);

		if (segment_coloff > 0) {
			if (!editorAppendGrayGlyph(wb, TEXT_WRAP_CONTINUATION_UTF8,
						sizeof(TEXT_WRAP_CONTINUATION_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}

		for (int pad = 0; pad < indent_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}

		if (!editorDrawRenderSlice(wb, row, (int)i, segment_coloff, draw_cols)) {
			return 0;
		}

		for (int pad = indent_cols + rendered_cols; pad < body_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}

		return wbAppend(wb, " ", 1);
	}

	return editorDrawRenderSlice(wb, row, (int)i, segment_coloff, text_cols);
}

static int editorDrawFileRow(struct writeBuf *wb, size_t i, int text_cols) {
	struct erow *row = &E.rows[i];
	if (E.line_wrap_enabled) {
		return editorDrawFileRowWrapped(wb, i, text_cols, 0);
	}
	if (text_cols >= 3) {
		int body_cols = editorTextBodyViewportCols(E.window_cols);
		int has_right_overflow = 0;
		int rendered_cols = editorRenderSliceDisplayCols(row, E.coloff, body_cols, &has_right_overflow);
		int has_left_overflow = E.coloff > 0 && row->rsize > 0;

		if (has_left_overflow) {
			if (!editorAppendGrayGlyph(wb, TEXT_OVERFLOW_LEFT_UTF8, sizeof(TEXT_OVERFLOW_LEFT_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}

		if (!editorDrawRenderSlice(wb, row, (int)i, E.coloff, body_cols)) {
			return 0;
		}

		for (int pad = rendered_cols; pad < body_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}

		if (has_right_overflow) {
			if (!editorAppendGrayGlyph(wb, TEXT_OVERFLOW_RIGHT_UTF8,
						sizeof(TEXT_OVERFLOW_RIGHT_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		return 1;
	}

	return editorDrawRenderSlice(wb, row, (int)i, E.coloff, text_cols);
}

static const char *editorTabLabelFromDisplayName(const char *display_name) {
	if (display_name == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(display_name, '/');
	if (slash != NULL && slash[1] != '\0') {
		return slash + 1;
	}
	return display_name;
}

static int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols);
static int editorDrawDrawerSeparatorCell(struct writeBuf *wb, int separator_cols);
static int editorDrawCollapsedDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols);
static int editorDrawDrawerSelectionOverflow(struct writeBuf *wb, int row_idx, int drawer_cols,
		int separator_cols, int text_cols, int terminal_row, int *overlay_drawn_out);

static int editorDrawTabSlots(struct writeBuf *wb, int cols) {
	if (cols <= 0) {
		return 1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForWidth(cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
		return 0;
	}

	int active = editorTabActiveIndex();
	int drawn_cols = 0;
	for (int i = 0; i < layout_count; i++) {
		const struct editorTabLayoutEntry *entry = &layout[i];
		int tab_idx = entry->tab_idx;
		int slot_width = entry->width_cols;
		if (slot_width <= 0) {
			continue;
		}
		int is_active = tab_idx == active;
		if (is_active && !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_TAB_ACTIVE)) {
			return 0;
		}

		int content_width = slot_width;
		if (entry->show_right_overflow && content_width > 0) {
			content_width--;
		}

		int slot_cols = 0;
		char marker = entry->show_left_overflow ? '<' : ' ';
		if (slot_cols < content_width && !wbAppend(wb, &marker, 1)) {
			return 0;
		}
		if (slot_cols < content_width) {
			slot_cols++;
		}

		char dirty = ' ';
		if (editorTabDirtyAt(tab_idx)) {
			dirty = '*';
		}
		if (slot_cols < content_width && !wbAppend(wb, &dirty, 1)) {
			return 0;
		}
		if (slot_cols < content_width) {
			slot_cols++;
		}

		if (slot_cols < content_width && !wbAppend(wb, " ", 1)) {
			return 0;
		}
		if (slot_cols < content_width) {
			slot_cols++;
		}

		if (slot_cols < content_width) {
			const char *label = editorTabLabelFromDisplayName(editorTabDisplayNameAt(tab_idx));
			int is_preview = editorTabIsPreviewAt(tab_idx);
			int right_pad_cols = 3;
			int label_cols = content_width - slot_cols - right_pad_cols;
			if (label_cols < 0) {
				label_cols = 0;
			}
			int written = 0;
			if (is_preview && !wbAppend(wb, VT100_ITALIC_ON_4, 4)) {
				return 0;
			}
			if (!editorAppendSanitizedMiddleTruncated(wb, label, label_cols, &written)) {
				return 0;
			}
			if (is_preview && !wbAppend(wb, VT100_ITALIC_OFF_5, 5)) {
				return 0;
			}
			slot_cols += written;

			while (right_pad_cols > 0 && slot_cols < content_width) {
				if (!wbAppend(wb, " ", 1)) {
					return 0;
				}
				slot_cols++;
				right_pad_cols--;
			}
		}

		while (slot_cols < content_width) {
			char pad = ' ';
			if (!wbAppend(wb, &pad, 1)) {
				return 0;
			}
			slot_cols++;
		}
		if (entry->show_right_overflow) {
			char overflow = '>';
			if (!wbAppend(wb, &overflow, 1)) {
				return 0;
			}
			slot_cols++;
		}

		if (is_active && !editorAppendThemeReset(wb)) {
			return 0;
		}

		drawn_cols += slot_width;
	}

	while (drawn_cols < cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		drawn_cols++;
	}

	return 1;
}

static int editorDrawTabBar(struct writeBuf *wb) {
	if (E.window_cols <= 0) {
		return wbAppend(wb, "\r\n", 2);
	}

	if (editorDrawerIsCollapsed()) {
		int toggle_cols = editorDrawerCollapsedToggleWidthForCols(E.window_cols);
		if (!editorDrawCollapsedDrawerRow(wb, 0, toggle_cols)) {
			return 0;
		}
		if (!editorDrawTabSlots(wb, E.window_cols - toggle_cols)) {
			return 0;
		}
		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			return 0;
		}
		return wbAppend(wb, "\r\n", 2);
	}

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	if (!editorDrawDrawerRow(wb, 0, drawer_cols)) {
		return 0;
	}
	if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
		return 0;
	}
	if (!editorDrawTabSlots(wb, text_cols)) {
		return 0;
	}

	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	return wbAppend(wb, "\r\n", 2);
}

static int editorDrawDrawerSeparatorCell(struct writeBuf *wb, int separator_cols) {
	if (separator_cols != 1) {
		return 1;
	}
	return wbAppend(wb, DRAWER_SPLITTER_UTF8, sizeof(DRAWER_SPLITTER_UTF8) - 1);
}

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

static int editorDrawerAppendCell(struct writeBuf *wb, const char *text, size_t len, int *written_cols,
		int drawer_cols) {
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

static int editorDrawDrawerAncestorGuides(struct writeBuf *wb, int parent_visible_idx, int *written_cols,
		int drawer_cols, int gray_connectors) {
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

static int editorDrawDrawerSelectionOverflow(struct writeBuf *wb, int row_idx, int drawer_cols,
		int separator_cols, int text_cols, int terminal_row, int *overlay_drawn_out) {
	if (overlay_drawn_out != NULL) {
		*overlay_drawn_out = 0;
	}
	if (separator_cols + text_cols <= 0 || E.pane_focus != EDITOR_PANE_DRAWER) {
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

static int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
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
	int selected_with_focus = 0;
	int row_inverted = 0;
	if (editorDrawerGetVisibleEntry(visible_idx, &entry)) {
		char entry_name_buf[PATH_MAX + 512];
		const char *entry_name = entry.name != NULL ? entry.name : "";
		snprintf(entry_name_buf, sizeof(entry_name_buf), "%s", entry_name);
		entry_name = entry_name_buf;
		selected_with_focus = entry.is_selected && E.pane_focus == EDITOR_PANE_DRAWER;
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

static int editorBuildFileRowLine(struct writeBuf *wb, int y, int drawer_cols, int separator_cols,
		int text_cols) {
	int y_offset = y + E.rowoff;
	int segment_coloff = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferRow(y, &y_offset, &segment_coloff)) {
			y_offset = E.numrows;
			segment_coloff = 0;
		}
	}

	if (!editorDrawDrawerRow(wb, y + 1, drawer_cols)) {
		return 0;
	}

	if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
		return 0;
	}

	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	int file_cols = text_cols - gutter_cols;
	if (file_cols < 1) {
		file_cols = 1;
	}
	int highlight_row = y_offset < E.numrows &&
			editorCurrentLineHighlightApplies(y_offset, segment_coloff);
	if (highlight_row &&
			!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
		return 0;
	}
	g_editor_drawing_current_line_highlight = highlight_row;
	if (!editorDrawLineNumberGutter(wb, y_offset, segment_coloff, gutter_cols)) {
		g_editor_drawing_current_line_highlight = 0;
		return 0;
	}
	if (y_offset < E.numrows) {
		if (E.line_wrap_enabled) {
			if (!editorDrawFileRowWrapped(wb, (size_t)y_offset, file_cols, segment_coloff)) {
				g_editor_drawing_current_line_highlight = 0;
				return 0;
			}
		} else if (!editorDrawFileRow(wb, (size_t)y_offset, file_cols)) {
			g_editor_drawing_current_line_highlight = 0;
			return 0;
		}
	} else if (E.numrows == 0 && y == E.window_rows / 3) {
		if (!editorDrawGreeting(wb, file_cols)) {
			g_editor_drawing_current_line_highlight = 0;
			return 0;
		}
	} else if (!editorAppendGrayBytes(wb, "~", 1)) {
		g_editor_drawing_current_line_highlight = 0;
		return 0;
	}
	g_editor_drawing_current_line_highlight = 0;
	if (highlight_row && !editorAppendThemeReset(wb)) {
		return 0;
	}

	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	if (!editorDrawDrawerSelectionOverflow(wb, y + 1, drawer_cols, separator_cols, text_cols, y + 2,
				NULL)) {
		return 0;
	}

	return 1;
}

/* "│" U+2502 BOX DRAWINGS LIGHT VERTICAL (UTF-8: e2 94 82) */
#define EDITOR_PANE_VBORDER "\xe2\x94\x82"
/* "─" U+2500 BOX DRAWINGS LIGHT HORIZONTAL (UTF-8: e2 94 80) */
#define EDITOR_PANE_HBORDER "\xe2\x94\x80"

static int editorPaneLeafAt(const struct editorLeafLayout *layout, int x,
		int y, struct editorRect *out_rect) {
	for (int i = 0; i < layout->count; i++) {
		struct editorRect r = layout->rects[i].rect;
		if (r.x <= x && x < r.x + r.w && r.y <= y && y < r.y + r.h) {
			if (out_rect != NULL) {
				*out_rect = r;
			}
			return i;
		}
	}
	return -1;
}

static int editorPaneRowIsHorizontalBorder(int screen_y,
		const struct editorLeafLayout *layout) {
	int has_above = 0;
	int has_below = 0;
	for (int i = 0; i < layout->count; i++) {
		struct editorRect r = layout->rects[i].rect;
		if (r.y <= screen_y && screen_y < r.y + r.h) {
			return 0;
		}
		if (r.y + r.h <= screen_y) {
			has_above = 1;
		}
		if (r.y > screen_y) {
			has_below = 1;
		}
	}
	return has_above && has_below;
}

static int editorPaneColIsVerticalBorder(int x, int screen_y,
		const struct editorLeafLayout *layout) {
	int has_left = 0;
	int has_right = 0;
	for (int i = 0; i < layout->count; i++) {
		struct editorRect r = layout->rects[i].rect;
		if (r.y > screen_y || r.y + r.h <= screen_y) {
			continue;
		}
		if (r.x + r.w <= x) {
			has_left = 1;
		}
		if (r.x > x) {
			has_right = 1;
		}
	}
	return has_left && has_right;
}

static int editorDrawFocusedPaneSlice(struct writeBuf *wb, int body_row_in_pane,
		int slice_cols) {
	int y_offset = body_row_in_pane + E.rowoff;
	int segment_coloff = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferRow(body_row_in_pane, &y_offset,
				&segment_coloff)) {
			y_offset = E.numrows;
			segment_coloff = 0;
		}
	}

	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > slice_cols) {
		gutter_cols = slice_cols;
	}
	int file_cols = slice_cols - gutter_cols;
	if (file_cols < 0) {
		file_cols = 0;
	}

	int highlight_row = y_offset < E.numrows &&
			editorCurrentLineHighlightApplies(y_offset, segment_coloff);
	if (highlight_row &&
			!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
		return 0;
	}
	g_editor_drawing_current_line_highlight = highlight_row;
	if (!editorDrawLineNumberGutter(wb, y_offset, segment_coloff, gutter_cols)) {
		g_editor_drawing_current_line_highlight = 0;
		return 0;
	}
	if (file_cols > 0) {
		if (y_offset < E.numrows) {
			if (E.line_wrap_enabled) {
				if (!editorDrawFileRowWrapped(wb, (size_t)y_offset, file_cols,
						segment_coloff)) {
					g_editor_drawing_current_line_highlight = 0;
					return 0;
				}
			} else if (!editorDrawFileRow(wb, (size_t)y_offset, file_cols)) {
				g_editor_drawing_current_line_highlight = 0;
				return 0;
			}
		} else if (!editorAppendGrayBytes(wb, "~", 1)) {
			g_editor_drawing_current_line_highlight = 0;
			return 0;
		}
	}
	g_editor_drawing_current_line_highlight = 0;
	if (highlight_row && !editorAppendThemeReset(wb)) {
		return 0;
	}
	return 1;
}

static int editorDrawBlankCells(struct writeBuf *wb, int cells) {
	for (int i = 0; i < cells; i++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return 1;
}

static int editorDrawMultiPaneRows(struct writeBuf *wb,
		const struct editorLeafLayout *layout, struct editorRect focused_rect) {
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_start_col = editorDrawerTextStartColForCols(E.window_cols);

	for (int y_body = 0; y_body < E.window_rows; y_body++) {
		int screen_y = y_body + 1;
		int terminal_row = y_body + 2;
		if (!editorAppendCursorMove(wb, terminal_row, 1)) {
			return 0;
		}
		if (!editorDrawDrawerRow(wb, y_body + 1, drawer_cols)) {
			return 0;
		}
		if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
			return 0;
		}

		if (editorPaneRowIsHorizontalBorder(screen_y, layout)) {
			int body_cols = E.window_cols - text_start_col;
			for (int i = 0; i < body_cols; i++) {
				if (!wbAppend(wb, EDITOR_PANE_HBORDER,
						sizeof(EDITOR_PANE_HBORDER) - 1)) {
					return 0;
				}
			}
			if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
				return 0;
			}
			continue;
		}

		int focused_intersects =
				screen_y >= focused_rect.y &&
				screen_y < focused_rect.y + focused_rect.h;
		int x = text_start_col;
		while (x < E.window_cols) {
			struct editorRect leaf_rect = {0};
			int leaf_idx = editorPaneLeafAt(layout, x, screen_y, &leaf_rect);
			if (leaf_idx < 0) {
				if (editorPaneColIsVerticalBorder(x, screen_y, layout)) {
					if (!wbAppend(wb, EDITOR_PANE_VBORDER,
							sizeof(EDITOR_PANE_VBORDER) - 1)) {
						return 0;
					}
				} else if (!wbAppend(wb, " ", 1)) {
					return 0;
				}
				x++;
				continue;
			}
			int slice_cols = leaf_rect.x + leaf_rect.w - x;
			if (slice_cols <= 0) {
				slice_cols = 1;
			}
			int is_focused_slice = focused_intersects &&
					layout->rects[leaf_idx].node == E.focused_leaf;
			if (is_focused_slice) {
				int body_row_in_pane = screen_y - focused_rect.y;
				if (!editorDrawFocusedPaneSlice(wb, body_row_in_pane, slice_cols)) {
					return 0;
				}
			} else if (!editorDrawBlankCells(wb, slice_cols)) {
				return 0;
			}
			x += slice_cols;
		}
		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			return 0;
		}
	}

	editorFileRowFrameCacheClearRowsFrom(0);
	g_file_row_frame_cache.valid = 0;
	g_editor_output_last_refresh_file_row_draw_count = E.window_rows;
	return 1;
}

static int editorDrawRows(struct writeBuf *wb) {
	editorDrawerClampViewport(E.window_rows);
	(void)editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows);

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	struct editorRect leaf_rect = {0};
	int text_cols;
	int has_focused_rect = editorLayoutFocusedLeafRect(&leaf_rect);
	if (has_focused_rect) {
		text_cols = leaf_rect.w;
	} else {
		text_cols = editorDrawerTextViewportCols(E.window_cols);
	}

	int leaf_count = editorPaneTreeLeafCount(E.layout_root);
	if (leaf_count > 1 && has_focused_rect) {
		struct editorRect viewport;
		if (!editorLayoutEditorViewport(&viewport)) {
			return 0;
		}
		struct editorLeafLayout layout = {0};
		if (!editorLayoutComputeBorderedInto(E.layout_root, viewport,
				ROTIDE_PANE_BORDER_SIZE, &layout)) {
			editorLeafLayoutFree(&layout);
			return 0;
		}
		int ok = editorDrawMultiPaneRows(wb, &layout, leaf_rect);
		editorLeafLayoutFree(&layout);
		return ok;
	}
	int file_row_draw_count = 0;
	int force_full = 0;
	if (!g_file_row_frame_cache.valid || g_file_row_frame_cache.window_rows != E.window_rows ||
			g_file_row_frame_cache.window_cols != E.window_cols) {
		force_full = 1;
	}

	if (!editorFileRowFrameCacheEnsureCapacity(E.window_rows)) {
		return 0;
	}

	/*
	 * Cells that were covered by the popup overlay during the previous frame are not tracked
	 * in the row cache. Drop those entries so the rows beneath them get repainted now that
	 * the popup may have moved or closed.
	 */
	if (g_popup_prev_row_count > 0) {
		int top_idx = g_popup_prev_screen_top - 2;
		int end_idx = top_idx + g_popup_prev_row_count;
		if (top_idx < 0) {
			top_idx = 0;
		}
		if (end_idx > g_file_row_frame_cache.row_capacity) {
			end_idx = g_file_row_frame_cache.row_capacity;
		}
		for (int i = top_idx; i < end_idx; i++) {
			free(g_file_row_frame_cache.rows[i]);
			g_file_row_frame_cache.rows[i] = NULL;
			g_file_row_frame_cache.row_lens[i] = 0;
		}
	}

	for (int y = 0; y < E.window_rows; y++) {
		struct writeBuf row_buf = WRITEBUF_INIT;
		if (!editorBuildFileRowLine(&row_buf, y, drawer_cols, separator_cols, text_cols)) {
			wbFree(&row_buf);
			return 0;
		}

		int changed = force_full;
		if (!changed && y < g_file_row_frame_cache.row_count &&
				g_file_row_frame_cache.rows[y] != NULL &&
				g_file_row_frame_cache.row_lens[y] == row_buf.len &&
				(row_buf.len == 0 ||
						memcmp(g_file_row_frame_cache.rows[y], row_buf.b, row_buf.len) == 0)) {
			changed = 0;
		} else {
			changed = 1;
		}

		if (changed) {
			if (!editorAppendCursorMove(wb, y + 2, 1) || !wbAppend(wb, row_buf.b, row_buf.len)) {
				wbFree(&row_buf);
				return 0;
			}
			if (!editorFileRowFrameCacheStoreRow(y, row_buf.b, row_buf.len)) {
				wbFree(&row_buf);
				return 0;
			}
			file_row_draw_count++;
		}

		wbFree(&row_buf);
	}

	if (g_file_row_frame_cache.row_count > E.window_rows) {
		editorFileRowFrameCacheClearRowsFrom(E.window_rows);
	}
	g_file_row_frame_cache.row_count = E.window_rows;
	g_file_row_frame_cache.window_rows = E.window_rows;
	g_file_row_frame_cache.window_cols = E.window_cols;
	g_file_row_frame_cache.valid = 1;
	g_editor_output_last_refresh_file_row_draw_count = file_row_draw_count;

	return 1;
}

static int editorScrollProgressPercent(void) {
	int visible_rows = E.window_rows > 0 ? E.window_rows : 1;
	long long top = 0;
	long long total = 0;

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		for (int i = 0; i < E.numrows; i++) {
			int segment_count = editorWrapSegmentCountForRowIndex(i, body_cols);
			if (i < E.rowoff) {
				top += segment_count;
			}
			total += segment_count;
		}
		if (E.rowoff >= 0 && E.rowoff < E.numrows) {
			int top_segment_count = editorWrapSegmentCountForRowIndex(E.rowoff, body_cols);
			int top_segment = E.wrapoff;
			if (top_segment < 0) {
				top_segment = 0;
			}
			if (top_segment >= top_segment_count) {
				top_segment = top_segment_count - 1;
			}
			top += top_segment;
		}
	} else {
		top = E.rowoff;
		total = E.numrows;
	}

	if (total <= 0) {
		return 0;
	}
	if (total <= visible_rows || top + visible_rows >= total) {
		return 100;
	}

	long long max_top = total - visible_rows;
	if (max_top <= 0) {
		return 100;
	}
	int progress = (int)((top * 100) / max_top);
	if (progress < 0) {
		return 0;
	}
	if (progress > 100) {
		return 100;
	}
	return progress;
}

static int editorDrawStatusBar(struct writeBuf *wb) {
	if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS)) {
		return 0;
	}
	char rightbuf[80];
	char diagbuf[48];
	const char *filename = editorActiveBufferDisplayName();
	const char *dirtyflag = "";
	diagbuf[0] = '\0';
	if (E.dirty) {
		dirtyflag = "[+]";
	}
	if (E.lsp_diagnostic_count > 0) {
		(void)snprintf(diagbuf, sizeof(diagbuf), " [E:%d W:%d]",
				E.lsp_diagnostic_error_count, E.lsp_diagnostic_warning_count);
	}

	int progress = editorScrollProgressPercent();
	int cursor_col = E.rx + 1;
	if (cursor_col < 1) {
		cursor_col = 1;
	}
	const char *git_branch = editorGitBranch();
	int rlen;
	if (git_branch != NULL) {
		char branch_trunc[25];
		(void)snprintf(branch_trunc, sizeof(branch_trunc), "%s", git_branch);
		const char *dirty_marker = E.git_entry_count > 0 ? "+" : "";
		rlen = snprintf(rightbuf, sizeof(rightbuf), " %s%s  %d,%d    %d%%",
				branch_trunc, dirty_marker, E.cy + 1, cursor_col, progress);
	} else {
		rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%",
				E.cy + 1, cursor_col, progress);
	}
	if (rlen < 0) {
		rlen = 0;
	}

	int right_start_col = E.window_cols - rlen;
	if (right_start_col < 0) {
		right_start_col = 0;
	}

	int dirty_cols = (int)strlen(dirtyflag);
	int diag_cols = (int)strlen(diagbuf);
	int left_budget = right_start_col;
	int reserved_for_dirty = 0;
	int include_dirty_sep = 0;
	if (dirty_cols > 0) {
		if (left_budget >= dirty_cols + 1) {
			reserved_for_dirty = dirty_cols + 1;
			include_dirty_sep = 1;
		} else if (left_budget >= dirty_cols) {
			reserved_for_dirty = dirty_cols;
		}
	}

	int path_budget = left_budget - reserved_for_dirty;
	if (path_budget < 0) {
		path_budget = 0;
	}
	if (diag_cols > 0 && path_budget >= diag_cols) {
		path_budget -= diag_cols;
	}

	int left_cols = 0;
	if (!editorAppendSanitizedStatusPath(wb, filename, path_budget, &left_cols)) {
		return 0;
	}
	if (diagbuf[0] != '\0' && left_cols < right_start_col) {
		int appended = 0;
		if (!editorAppendSanitizedText(wb, diagbuf, right_start_col - left_cols, &appended)) {
			return 0;
		}
		left_cols += appended;
	}

	if (reserved_for_dirty > 0) {
		if (include_dirty_sep && left_cols < right_start_col) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			left_cols++;
		}

		for (int i = 0; dirtyflag[i] != '\0' && left_cols < right_start_col; i++) {
			if (!wbAppend(wb, &dirtyflag[i], 1)) {
				return 0;
			}
			left_cols++;
		}
	}

	for (; left_cols < right_start_col; left_cols++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}

	if (rlen > 0 && !wbAppend(wb, rightbuf, (size_t)rlen)) {
		return 0;
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	return wbAppend(wb, "\r\n", 2);
}

static void editorClampViewportOffsets(void) {
	if (E.line_wrap_enabled) {
		editorWrappedClampViewportOffsets();
		return;
	}
	if (E.rowoff < 0) {
		E.rowoff = 0;
	}
	int max_rowoff = E.numrows > 0 ? E.numrows - 1 : 0;
	if (E.rowoff > max_rowoff) {
		E.rowoff = max_rowoff;
	}
	if (E.coloff < 0) {
		E.coloff = 0;
	}
	if (E.coloff > 0) {
		int max_coloff = editorBufferMaxRenderCols();
		if (max_coloff > 0) {
			max_coloff--;
		}
		if (E.coloff > max_coloff) {
			E.coloff = max_coloff;
		}
	}
	E.wrapoff = 0;
}

static void editorUpdateRenderXFromCursor(void) {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}
}

static void editorFollowCursorViewport(void) {
	int text_cols = editorTextBodyViewportCols(E.window_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = E.cy < E.numrows ?
				editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols) : 0;
		E.coloff = 0;

		if (editorWrappedPositionBefore(E.cy, cursor_segment, E.rowoff, E.wrapoff)) {
			E.rowoff = E.cy;
			E.wrapoff = cursor_segment;
			return;
		}

		int distance = 0;
		if (!editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
					E.window_rows > 0 ? E.window_rows - 1 : 0, body_cols, &distance)) {
			int top_row = E.cy;
			int top_segment = cursor_segment;
			int back_count = E.window_rows > 0 ? E.window_rows - 1 : 0;
			for (int i = 0; i < back_count; i++) {
				editorWrappedMoveBackPosition(&top_row, &top_segment, body_cols);
			}
			E.rowoff = top_row;
			E.wrapoff = top_segment;
		}
		return;
	}

	// Keep the cursor visible vertically and horizontally by moving
	// the window origin just enough to include the current position.
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	} else if (E.cy >= E.rowoff + E.window_rows) {
		E.rowoff = E.cy - E.window_rows + 1;
	}

	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	} else if (E.rx >= E.coloff + text_cols) {
		E.coloff = E.rx - text_cols + 1;
	}
}

void editorViewportSetMode(enum editorViewportMode mode) {
	if (mode == EDITOR_VIEWPORT_FREE_SCROLL) {
		E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
	} else {
		E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	}
}

void editorViewportScrollByRows(int delta_rows) {
	if (delta_rows == 0) {
		return;
	}

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		editorWrappedClampViewportOffsets();
		if (delta_rows > 0) {
			for (int i = 0; i < delta_rows; i++) {
				int old_row = E.rowoff;
				int old_segment = E.wrapoff;
				editorWrappedAdvancePosition(&E.rowoff, &E.wrapoff, body_cols);
				if (E.rowoff >= E.numrows) {
					E.rowoff = old_row;
					E.wrapoff = old_segment;
					break;
				}
			}
		} else {
			for (int i = 0; i > delta_rows; i--) {
				int old_row = E.rowoff;
				int old_segment = E.wrapoff;
				editorWrappedMoveBackPosition(&E.rowoff, &E.wrapoff, body_cols);
				if (E.rowoff == old_row && E.wrapoff == old_segment) {
					break;
				}
			}
		}
		E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
		editorWrappedClampViewportOffsets();
		return;
	}

	long long target = (long long)E.rowoff + (long long)delta_rows;
	if (target < 0) {
		target = 0;
	}
	long long max_rowoff = E.numrows > 0 ? (long long)E.numrows - 1 : 0;
	if (target > max_rowoff) {
		target = max_rowoff;
	}
	E.rowoff = (int)target;
	E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
	editorClampViewportOffsets();
}

void editorViewportScrollByCols(int delta_cols) {
	if (delta_cols == 0) {
		return;
	}

	if (E.line_wrap_enabled) {
		E.coloff = 0;
		editorWrappedClampViewportOffsets();
		return;
	}

	long long target = (long long)E.coloff + (long long)delta_cols;
	if (target < 0) {
		target = 0;
	}
	if (target > INT_MAX) {
		target = INT_MAX;
	}
	E.coloff = (int)target;
	E.viewport_mode = EDITOR_VIEWPORT_FREE_SCROLL;
	editorClampViewportOffsets();
}

void editorViewportEnsureCursorVisible(void) {
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	editorUpdateRenderXFromCursor();
	editorFollowCursorViewport();
	editorClampViewportOffsets();
}

static int editorViewportCenteredScreenRowAvoidingDrawerSelection(int desired_screen_row) {
	if (desired_screen_row < 0 || E.pane_focus != EDITOR_PANE_DRAWER ||
			E.drawer_selected_index < 0 || E.window_rows <= 1) {
		return desired_screen_row;
	}
	int drawer_screen_row = E.drawer_selected_index - E.drawer_rowoff;
	if (drawer_screen_row != desired_screen_row) {
		return desired_screen_row;
	}
	if (desired_screen_row + 1 < E.window_rows) {
		return desired_screen_row + 1;
	}
	if (desired_screen_row > 0) {
		return desired_screen_row - 1;
	}
	return desired_screen_row;
}

void editorViewportCenterCursor(void) {
	E.viewport_mode = EDITOR_VIEWPORT_FOLLOW_CURSOR;
	editorUpdateRenderXFromCursor();

	int target_screen_row = E.window_rows > 0 ? E.window_rows / 2 : 0;
	target_screen_row = editorViewportCenteredScreenRowAvoidingDrawerSelection(target_screen_row);

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = E.cy < E.numrows ?
				editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols) : 0;
		E.coloff = 0;
		int top_row = E.cy;
		int top_segment = cursor_segment;
		for (int i = 0; i < target_screen_row; i++) {
			editorWrappedMoveBackPosition(&top_row, &top_segment, body_cols);
		}
		E.rowoff = top_row;
		E.wrapoff = top_segment;
	} else {
		int target_rowoff = E.cy - target_screen_row;
		if (target_rowoff < 0) {
			target_rowoff = 0;
		}
		E.rowoff = target_rowoff;

		int text_cols = editorTextBodyViewportCols(E.window_cols);
		if (text_cols < 1) {
			text_cols = 1;
		}
		if (E.rx < E.coloff) {
			E.coloff = E.rx;
		} else if (E.rx >= E.coloff + text_cols) {
			E.coloff = E.rx - text_cols + 1;
		}
	}
	editorClampViewportOffsets();
}

void editorScroll(void) {
	editorUpdateRenderXFromCursor();
	if (E.viewport_mode == EDITOR_VIEWPORT_FOLLOW_CURSOR) {
		editorFollowCursorViewport();
	}
	editorClampViewportOffsets();
}

static int editorDrawMessageBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	if (E.statusmsg[0] != '\0' && time(NULL) - E.statusmsg_time < 5) {
		// Truncate by display columns after escaping, not by raw byte count.
		if (!editorAppendSanitizedText(wb, E.statusmsg, E.window_cols, NULL)) {
			return 0;
		}
	}

	return 1;
}

static const char *editorCursorStyleSequence(enum editorCursorStyle style, int blink_enabled,
		size_t *len_out) {
	const char *sequence = blink_enabled ? VT100_CURSOR_BLINKING_BAR_5 : VT100_CURSOR_STEADY_BAR_5;
	switch (style) {
		case EDITOR_CURSOR_STYLE_BLOCK:
			sequence = blink_enabled ? VT100_CURSOR_BLINKING_BLOCK_5 : VT100_CURSOR_STEADY_BLOCK_5;
			break;
		case EDITOR_CURSOR_STYLE_UNDERLINE:
			sequence = blink_enabled ? VT100_CURSOR_BLINKING_UNDERLINE_5 :
					VT100_CURSOR_STEADY_UNDERLINE_5;
			break;
		case EDITOR_CURSOR_STYLE_BAR:
		default:
			sequence = blink_enabled ? VT100_CURSOR_BLINKING_BAR_5 : VT100_CURSOR_STEADY_BAR_5;
			break;
	}
	if (len_out != NULL) {
		*len_out = strlen(sequence);
	}
	return sequence;
}

static const struct editorLspDiagnostic *editorDiagnosticAtCursor(void) {
	if (E.pane_focus != EDITOR_PANE_TEXT || E.cy < 0 || E.cy >= E.numrows ||
			E.lsp_diagnostics == NULL || E.lsp_diagnostic_count <= 0) {
		return NULL;
	}
	int row_size = E.rows[E.cy].size;
	for (int i = 0; i < E.lsp_diagnostic_count; i++) {
		int start = 0;
		int end = 0;
		if (!editorDiagnosticRangeForRow(&E.lsp_diagnostics[i], E.cy, row_size, &start, &end)) {
			continue;
		}
		if (E.cx >= start && E.cx < end) {
			return &E.lsp_diagnostics[i];
		}
	}
	return NULL;
}

static int editorCursorScreenPosition(int *screen_row_out, int *screen_col_out) {
	if (screen_row_out == NULL || screen_col_out == NULL || E.cy < 0 || E.cy >= E.numrows) {
		return 0;
	}
	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols);
		int cursor_distance = 0;
		if (!editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
					E.window_rows > 0 ? E.window_rows - 1 : 0, body_cols, &cursor_distance)) {
			return 0;
		}
		int segment_start_col = 0;
		int segment_indent_cols = 0;
		editorWrapSegmentInfo(&E.rows[E.cy], cursor_segment, body_cols, &segment_start_col,
				NULL, &segment_indent_cols);
		int cursor_segment_col = segment_indent_cols + E.rx - segment_start_col;
		if (cursor_segment_col < 0) {
			cursor_segment_col = 0;
		}
		if (cursor_segment_col >= body_cols) {
			cursor_segment_col = body_cols - 1;
		}
		*screen_row_out = cursor_distance;
		*screen_col_out = cursor_segment_col;
		return 1;
	}

	int screen_row = E.cy - E.rowoff;
	int screen_col = E.rx - E.coloff;
	if (screen_row < 0 || screen_row >= E.window_rows || screen_col < 0) {
		return 0;
	}
	*screen_row_out = screen_row;
	*screen_col_out = screen_col;
	return 1;
}

static int editorDrawDiagnosticPopdown(struct writeBuf *wb) {
	if (editorPopupIsVisible()) {
		return 1;
	}
	const struct editorLspDiagnostic *diagnostic = editorDiagnosticAtCursor();
	const char *message = diagnostic != NULL && diagnostic->message != NULL ?
			diagnostic->message : NULL;
	if (message == NULL || message[0] == '\0') {
		return 1;
	}

	int screen_row = 0;
	int screen_col = 0;
	if (!editorCursorScreenPosition(&screen_row, &screen_col)) {
		return 1;
	}

	int text_start_col = editorTextBodyStartColForCols(E.window_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	int terminal_col_zero = text_start_col + gutter_cols + screen_col;
	int message_cols = 0;
	char *sanitized = editorSanitizeDiagnosticMessageDup(message, &message_cols);
	if (sanitized == NULL) {
		return 0;
	}
	int available_cols = E.window_cols - text_start_col;
	int cols = message_cols + 2;
	if (cols > available_cols) {
		cols = available_cols;
	}
	if (cols < 4) {
		free(sanitized);
		return 1;
	}
	int content_cols = cols - 2;
	int row_count = editorDisplayWrapLineCount(sanitized, content_cols);
	if (row_count <= 0) {
		free(sanitized);
		return 1;
	}

	int rows_below = E.window_rows - (screen_row + 1);
	int rows_above = screen_row;
	int popdown_screen_row = -1;
	int visible_rows = row_count;
	if (row_count <= rows_below) {
		popdown_screen_row = screen_row + 1;
	} else if (row_count <= rows_above) {
		popdown_screen_row = screen_row - row_count;
	} else if (rows_below >= rows_above && rows_below > 0) {
		popdown_screen_row = screen_row + 1;
		visible_rows = rows_below;
	} else if (rows_above > 0) {
		popdown_screen_row = screen_row - rows_above;
		visible_rows = rows_above;
	} else {
		free(sanitized);
		return 1;
	}

	if (terminal_col_zero + cols > E.window_cols) {
		terminal_col_zero = E.window_cols - cols;
	}
	if (terminal_col_zero < text_start_col) {
		terminal_col_zero = text_start_col;
	}

	int terminal_col = terminal_col_zero + 1;
	int text_len = (int)strlen(sanitized);
	int start_idx = 0;
	int rows_drawn = 0;
	for (; rows_drawn < visible_rows && start_idx < text_len; rows_drawn++) {
		int terminal_row = popdown_screen_row + rows_drawn + 2;
		int end_idx = start_idx;
		int wrote = 0;
		editorDisplayWrapNextLine(sanitized, text_len, start_idx, content_cols, &end_idx,
				&wrote);
		if (end_idx <= start_idx) {
			break;
		}
		if (!editorAppendCursorMove(wb, terminal_row, terminal_col) ||
				!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS) ||
				!wbAppend(wb, " ", 1) ||
				!wbAppend(wb, &sanitized[start_idx], (size_t)(end_idx - start_idx))) {
			free(sanitized);
			return 0;
		}
		int padding = cols - 1 - wrote;
		while (padding > 0) {
			if (!wbAppend(wb, " ", 1)) {
				free(sanitized);
				return 0;
			}
			padding--;
		}
		if (!editorAppendThemeReset(wb)) {
			free(sanitized);
			return 0;
		}
		start_idx = end_idx;
	}

	free(sanitized);
	g_popup_prev_screen_top = popdown_screen_row + 2;
	g_popup_prev_row_count = rows_drawn;
	return 1;
}

static int editorDrawPopupOverlay(struct writeBuf *wb) {
	if (!editorPopupIsVisible()) {
		g_popup_prev_screen_top = 0;
		g_popup_prev_row_count = 0;
		return 1;
	}
	int terminal_row = 0;
	int terminal_col = 0;
	int visible_rows = 0;
	int cols = 0;
	int place_above = 0;
	editorPopupComputePlacement(&terminal_row, &terminal_col, &visible_rows, &cols, &place_above);
	if (visible_rows <= 0 || cols <= 0) {
		g_popup_prev_screen_top = 0;
		g_popup_prev_row_count = 0;
		return 1;
	}
	g_popup_prev_screen_top = terminal_row;
	g_popup_prev_row_count = visible_rows;

	int item_count = editorPopupItemCount();
	int row_offset = E.popup.row_offset;
	int selected_idx = editorPopupSelectedIndex();

	for (int row = 0; row < visible_rows; row++) {
		int item_idx = row_offset + row;
		if (item_idx >= item_count) {
			break;
		}
		char move_buf[32];
		int move_len = snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH",
				terminal_row + row, terminal_col);
		if (move_len <= 0 || move_len >= (int)sizeof(move_buf)) {
			return 0;
		}
		if (!wbAppend(wb, move_buf, (size_t)move_len)) {
			return 0;
		}
		int is_selected = item_idx == selected_idx;
		if (is_selected) {
			if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
				return 0;
			}
		} else {
			if (!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
				return 0;
			}
		}
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		const char *label = E.popup.items[item_idx].label != NULL ?
				E.popup.items[item_idx].label : "";
		int wrote = 0;
		if (!editorAppendSanitizedText(wb, label, cols - 2, &wrote)) {
			return 0;
		}
		int padding = cols - 1 - wrote;
		while (padding > 0) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			padding--;
		}
		if (!editorAppendThemeReset(wb)) {
			return 0;
		}
	}
	return 1;
}

void editorRefreshScreen(void) {
	editorLspPumpNotifications();
	editorDapPumpNotifications();
	editorScroll();
	g_editor_output_last_refresh_file_row_draw_count = 0;

	struct writeBuf wb = WRITEBUF_INIT;
	size_t cursor_style_len = 0;
	const char *cursor_style_sequence =
			editorCursorStyleSequence(E.cursor_style, E.cursor_blink_enabled, &cursor_style_len);

	// Build a full frame in memory and write once to reduce terminal flicker.
	if (!wbAppend(&wb, VT100_HIDE_CURSOR_6, 6) ||
			!editorAppendThemeCursorColor(&wb) ||
			!wbAppend(&wb, cursor_style_sequence, cursor_style_len) ||
			!wbAppend(&wb, VT100_RESET_CURSOR_POS_3, 3) ||
			!editorAppendThemeBaseStyle(&wb)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}

	int status_row = E.window_rows + 2;
	int message_row = E.window_rows + 3;
	if (!editorDrawTabBar(&wb) || !editorDrawRows(&wb) ||
			!editorAppendCursorMove(&wb, status_row, 1) || !editorDrawStatusBar(&wb) ||
			!editorAppendCursorMove(&wb, message_row, 1) || !editorDrawMessageBar(&wb)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}
	if (!editorDrawPopupOverlay(&wb)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}
	if (!editorDrawDiagnosticPopdown(&wb)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}

	struct editorRect cursor_focused_rect = {0};
	int has_focus_rect = editorLayoutFocusedLeafRect(&cursor_focused_rect);
	int cursor_pane_y = has_focus_rect ? cursor_focused_rect.y : 1;
	int cursor_pane_text_start_col;
	if (has_focus_rect) {
		int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
		int text_body_cols = cursor_focused_rect.w - gutter_cols;
		cursor_pane_text_start_col = cursor_focused_rect.x + gutter_cols;
		if (text_body_cols >= 3) {
			cursor_pane_text_start_col += 1;
		}
	} else {
		cursor_pane_text_start_col = editorTextBodyStartColForCols(E.window_cols);
	}

	int cursor_row = cursor_pane_y + (E.cy - E.rowoff) + 1;
	int cursor_col = cursor_pane_text_start_col + (E.rx - E.coloff) + 1;
	int cursor_visible = 1;
	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = E.cy < E.numrows ?
				editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols) : 0;
		int segment_start_col = 0;
		int segment_indent_cols = 0;
		if (E.cy < E.numrows) {
			editorWrapSegmentInfo(&E.rows[E.cy], cursor_segment, body_cols, &segment_start_col,
					NULL, &segment_indent_cols);
		}
		int cursor_segment_col = segment_indent_cols + E.rx - segment_start_col;
		if (cursor_segment_col >= body_cols) {
			cursor_segment_col = body_cols - 1;
		}
		if (cursor_segment_col < 0) {
			cursor_segment_col = segment_indent_cols;
			if (cursor_segment_col >= body_cols) {
				cursor_segment_col = body_cols - 1;
			}
		}
		int cursor_distance = 0;
		if (editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
					E.window_rows > 0 ? E.window_rows - 1 : 0, body_cols, &cursor_distance)) {
			cursor_row = cursor_pane_y + cursor_distance + 1;
		} else {
			cursor_visible = 0;
		}
		cursor_col = cursor_pane_text_start_col + cursor_segment_col + 1;
	}
	if (E.pane_focus == EDITOR_PANE_DRAWER && editorDrawerWidthForCols(E.window_cols) > 0) {
		if (editorFileSearchIsActive()) {
			cursor_row = 2;
			cursor_col = editorFileSearchHeaderCursorCol(editorDrawerWidthForCols(E.window_cols));
		} else if (editorProjectSearchIsActive()) {
			cursor_row = 2;
			cursor_col = editorProjectSearchHeaderCursorCol(editorDrawerWidthForCols(E.window_cols));
		} else {
			cursor_visible = 0;
		}
	} else {
		int text_row_min = 2;
		int text_row_max = E.window_rows + 1;
		if (text_row_max < text_row_min) {
			text_row_max = text_row_min;
		}

		int text_col_min = editorTextBodyStartColForCols(E.window_cols) + 1;
		int text_col_max = text_col_min + editorTextBodyViewportCols(E.window_cols) - 1;
		if (text_col_max < text_col_min) {
			text_col_max = text_col_min;
		}
		if (E.viewport_mode == EDITOR_VIEWPORT_FREE_SCROLL &&
				(cursor_row < text_row_min || cursor_row > text_row_max || cursor_col < text_col_min ||
						cursor_col > text_col_max)) {
			cursor_visible = 0;
		} else {
			if (cursor_row < text_row_min) {
				cursor_row = text_row_min;
			}
			if (cursor_row > text_row_max) {
				cursor_row = text_row_max;
			}
			if (cursor_col < text_col_min) {
				cursor_col = text_col_min;
			}
			if (cursor_col > text_col_max) {
				cursor_col = text_col_max;
			}
		}
	}
	if (cursor_visible) {
		if (cursor_row < 1) {
			cursor_row = 1;
		}
		if (cursor_col < 1) {
			cursor_col = 1;
		}

		char buf[32];
		int buflen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_row, cursor_col);
		if (buflen > 0 && buflen < (int)sizeof(buf)) {
			if (!wbAppend(&wb, buf, buflen)) {
				wbFree(&wb);
				editorSetStatusMsg("Out of memory");
				return;
			}
		}

		if (!wbAppend(&wb, VT100_SHOW_CURSOR_6, 6)) {
			wbFree(&wb);
			editorSetStatusMsg("Out of memory");
			return;
		}
	}

	if (!editorWriteAllToStdout(wb.b, wb.len)) {
		int saved_errno = errno;
		wbFree(&wb);
		if (saved_errno != 0) {
			editorSetStatusMsg("Output write failed: %s", strerror(saved_errno));
		} else {
			editorSetStatusMsg("Output write failed");
		}
		return;
	}

	wbFree(&wb);
}

void editorOutputTestResetFrameCache(void) {
	editorFileRowFrameCacheReset();
	g_editor_output_last_refresh_file_row_draw_count = 0;
}

int editorOutputTestLastRefreshFileRowDrawCount(void) {
	return g_editor_output_last_refresh_file_row_draw_count;
}
