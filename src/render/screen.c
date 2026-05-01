#include "render/screen.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "language/lsp.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define VT100_CURSOR_STEADY_BLOCK_5 "\x1b[2 q"
#define VT100_CURSOR_STEADY_UNDERLINE_5 "\x1b[4 q"
#define VT100_CURSOR_STEADY_BAR_5 "\x1b[6 q"
#define VT100_ITALIC_ON_4 "\x1b[3m"
#define VT100_ITALIC_OFF_5 "\x1b[23m"
#define VT100_BOLD_ON_4 "\x1b[1m"
#define VT100_BOLD_OFF_5 "\x1b[22m"
#define VT100_INVERTED_COLORS_4 "\x1b[7m"
#define VT100_NORMAL_COLORS_3 "\x1b[m"
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
#define DRAWER_COLLAPSE_INDICATOR "[<]"
#define DRAWER_EXPAND_INDICATOR "[>]"
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

static int editorAppendGrayBytes(struct writeBuf *wb, const char *text, size_t len) {
	return wbAppend(wb, VT100_FG_GRAY_5, 5) && wbAppend(wb, text, len) &&
			wbAppend(wb, VT100_FG_DEFAULT_5, 5);
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

static const char *editorThemeColorSequence(enum editorThemeColor color, size_t *len_out) {
	const char *sequence = VT100_FG_DEFAULT_5;
	switch (color) {
		case EDITOR_THEME_COLOR_BLACK:
			sequence = VT100_FG_BLACK_5;
			break;
		case EDITOR_THEME_COLOR_RED:
			sequence = VT100_FG_RED_5;
			break;
		case EDITOR_THEME_COLOR_GREEN:
			sequence = VT100_FG_GREEN_5;
			break;
		case EDITOR_THEME_COLOR_YELLOW:
			sequence = VT100_FG_YELLOW_5;
			break;
		case EDITOR_THEME_COLOR_BLUE:
			sequence = VT100_FG_BLUE_5;
			break;
		case EDITOR_THEME_COLOR_MAGENTA:
			sequence = VT100_FG_MAGENTA_5;
			break;
		case EDITOR_THEME_COLOR_CYAN:
			sequence = VT100_FG_CYAN_5;
			break;
		case EDITOR_THEME_COLOR_WHITE:
			sequence = VT100_FG_WHITE_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_BLACK:
			sequence = VT100_FG_GRAY_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_RED:
			sequence = VT100_FG_BRIGHT_RED_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_GREEN:
			sequence = VT100_FG_BRIGHT_GREEN_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_YELLOW:
			sequence = VT100_FG_BRIGHT_YELLOW_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_BLUE:
			sequence = VT100_FG_BRIGHT_BLUE_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_MAGENTA:
			sequence = VT100_FG_BRIGHT_MAGENTA_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_CYAN:
			sequence = VT100_FG_BRIGHT_CYAN_5;
			break;
		case EDITOR_THEME_COLOR_BRIGHT_WHITE:
			sequence = VT100_FG_BRIGHT_WHITE_5;
			break;
		case EDITOR_THEME_COLOR_DEFAULT:
		default:
			sequence = VT100_FG_DEFAULT_5;
			break;
	}

	if (len_out != NULL) {
		*len_out = strlen(sequence);
	}
	return sequence;
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

static int editorDrawRenderSliceWithSyntax(struct writeBuf *wb, const struct erow *row,
		int segment_start, int segment_end, const struct editorRowSyntaxSpan *spans, int span_count) {
	if (segment_end <= segment_start) {
		return 1;
	}
	if (spans == NULL || span_count <= 0 || E.syntax_state == NULL ||
			E.syntax_language == EDITOR_SYNTAX_NONE) {
		return wbAppend(wb, &row->render[segment_start], (size_t)(segment_end - segment_start));
	}

	enum editorThemeColor active_color = EDITOR_THEME_COLOR_DEFAULT;
	int pos = segment_start;
	while (pos < segment_end) {
		enum editorSyntaxHighlightClass highlight_class =
				editorSyntaxClassAtRenderIdx(spans, span_count, pos);
		enum editorThemeColor next_color = EDITOR_THEME_COLOR_DEFAULT;
		if (highlight_class > EDITOR_SYNTAX_HL_NONE &&
				highlight_class < EDITOR_SYNTAX_HL_CLASS_COUNT) {
			next_color = E.syntax_theme[highlight_class];
		}

		if (next_color != active_color) {
			size_t seq_len = 0;
			const char *seq = editorThemeColorSequence(next_color, &seq_len);
			if (!wbAppend(wb, seq, seq_len)) {
				return 0;
			}
			active_color = next_color;
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

	if (active_color != EDITOR_THEME_COLOR_DEFAULT &&
			!wbAppend(wb, VT100_FG_DEFAULT_5, 5)) {
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

	if (highlight_len_chars <= 0) {
		return editorDrawRenderSliceWithSyntax(wb, row, start, end, syntax_spans, syntax_span_count);
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
		return editorDrawRenderSliceWithSyntax(wb, row, start, end, syntax_spans, syntax_span_count);
	}

	int highlight_start = start > match_render_start ? start : match_render_start;
	int highlight_end = end < match_render_end ? end : match_render_end;
	if (highlight_end <= highlight_start) {
		return editorDrawRenderSliceWithSyntax(wb, row, start, end, syntax_spans, syntax_span_count);
	}

	if (highlight_start > start &&
			!editorDrawRenderSliceWithSyntax(wb, row, start, highlight_start, syntax_spans,
					syntax_span_count)) {
		return 0;
	}
	if (!wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
		return 0;
	}
	if (!wbAppend(wb, &row->render[highlight_start], highlight_end - highlight_start)) {
		return 0;
	}
	if (!wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
		return 0;
	}
	if (highlight_end < end &&
			!editorDrawRenderSliceWithSyntax(wb, row, highlight_end, end, syntax_spans,
					syntax_span_count)) {
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

static int editorRenderDisplayCols(const struct erow *row) {
	if (row == NULL || row->rsize <= 0) {
		return 0;
	}

	int cols = 0;
	for (int i = 0; i < row->rsize;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], row->rsize - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > row->rsize - i) {
			src_len = row->rsize - i;
		}
		cols += editorCharDisplayWidth(&row->render[i], row->rsize - i);
		i += src_len;
	}
	return cols;
}

static int editorWrapBodyCols(void) {
	int body_cols = editorTextBodyViewportCols(E.window_cols);
	return body_cols < 1 ? 1 : body_cols;
}

static int editorWrapSegmentCountForRowIndex(int row_idx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 1;
	}
	int cols = editorRenderDisplayCols(&E.rows[row_idx]);
	if (cols <= 0) {
		return 1;
	}
	return (cols + body_cols - 1) / body_cols;
}

static int editorWrapCursorSegmentForRx(int rx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (rx <= 0) {
		return 0;
	}
	return (rx - 1) / body_cols;
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
	if (row_idx_out == NULL || segment_coloff_out == NULL || screen_row < 0) {
		return 0;
	}
	if (!E.line_wrap_enabled) {
		*row_idx_out = E.rowoff + screen_row;
		*segment_coloff_out = E.coloff;
		return 1;
	}

	int row = E.rowoff;
	int segment = E.wrapoff;
	int body_cols = editorWrapBodyCols();
	for (int y = 0; y < screen_row; y++) {
		editorWrappedAdvancePosition(&row, &segment, body_cols);
	}
	*row_idx_out = row;
	*segment_coloff_out = segment * body_cols;
	return 1;
}

static int editorAppendGrayGlyph(struct writeBuf *wb, const char *glyph, size_t glyph_len) {
	return editorAppendGrayBytes(wb, glyph, glyph_len);
}

static int editorDrawFileRowWrapped(struct writeBuf *wb, size_t i, int text_cols,
		int segment_coloff) {
	struct erow *row = &E.rows[i];
	if (text_cols >= 3) {
		int body_cols = editorTextBodyViewportCols(E.window_cols);
		int rendered_cols = editorRenderSliceDisplayCols(row, segment_coloff, body_cols, NULL);

		if (segment_coloff > 0) {
			if (!editorAppendGrayGlyph(wb, TEXT_WRAP_CONTINUATION_UTF8,
						sizeof(TEXT_WRAP_CONTINUATION_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}

		if (!editorDrawRenderSlice(wb, row, (int)i, segment_coloff, body_cols)) {
			return 0;
		}

		for (int pad = rendered_cols; pad < body_cols; pad++) {
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
		if (is_active && !wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
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

		if (is_active && !wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
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

static int editorDrawCollapsedDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
	int written_cols = 0;
	if (row_idx == 0) {
		int indicator_cols = 0;
		if (!editorAppendSanitizedText(wb, DRAWER_EXPAND_INDICATOR, drawer_cols, &indicator_cols)) {
			return 0;
		}
		written_cols += indicator_cols;
	}

	while (written_cols < drawer_cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		written_cols++;
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
	if (!wbAppend(wb, VT100_FG_GRAY_5, 5) || !wbAppend(wb, text, len) ||
			!wbAppend(wb, VT100_FG_DEFAULT_5, 5)) {
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
			if (!wbAppend(wb, "  ", 2)) {
				return 0;
			}
		} else if (!wbAppend(wb, DRAWER_SPLITTER_UTF8 " ", sizeof(DRAWER_SPLITTER_UTF8) + 1 - 1)) {
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

	return editorAppendSanitizedText(wb, entry.name, -1, NULL);
}

static int editorDrawerSearchHeaderActive(void) {
	return editorFileSearchIsActive() || editorProjectSearchIsActive();
}

static int editorDrawDrawerSelectionOverflow(struct writeBuf *wb, int row_idx, int drawer_cols,
		int separator_cols, int text_cols, int terminal_row, int *overlay_drawn_out) {
	if (overlay_drawn_out != NULL) {
		*overlay_drawn_out = 0;
	}
	if (separator_cols + text_cols <= 0 || E.pane_focus != EDITOR_PANE_DRAWER) {
		return 1;
	}

	int visible_idx =
			(editorDrawerSearchHeaderActive() && row_idx == 0) ? 0 : E.drawer_rowoff + row_idx;
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
			!wbAppend(wb, VT100_INVERTED_COLORS_4, 4) ||
			!editorAppendDisplaySlice(wb, plain.b != NULL ? plain.b : "", drawer_cols, overlay_budget,
					&overlay_written) ||
			!wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
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

	struct editorDrawerEntryView entry;
	int visible_idx =
			(editorDrawerSearchHeaderActive() && row_idx == 0) ? 0 : E.drawer_rowoff + row_idx;
	int written_cols = 0;
	int selected_with_focus = 0;
	int row_inverted = 0;
	if (editorDrawerGetVisibleEntry(visible_idx, &entry)) {
		selected_with_focus = entry.is_selected && E.pane_focus == EDITOR_PANE_DRAWER;
		row_inverted = selected_with_focus || (entry.is_active_file && !entry.is_dir);
		int gray_connectors = !row_inverted;
		if (row_inverted && !wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
			return 0;
		}

		if (row_idx == 0) {
			int indicator_written = 0;
			if (!editorAppendSanitizedText(wb, DRAWER_COLLAPSE_INDICATOR, drawer_cols,
						&indicator_written)) {
				return 0;
			}
			written_cols += indicator_written;
			if (written_cols < drawer_cols &&
					!editorDrawerAppendCell(wb, " ", 1, &written_cols, drawer_cols)) {
				return 0;
			}
		}

		if (entry.is_search_header) {
			if (written_cols < drawer_cols) {
				int wrote = 0;
				const char *label = editorProjectSearchIsActive() ? "Text: " : "Find: ";
				if (!editorAppendSanitizedText(wb, label, drawer_cols - written_cols, &wrote)) {
					return 0;
				}
				written_cols += wrote;
			}
			if (written_cols < drawer_cols) {
				int wrote = 0;
				if (!editorAppendSanitizedText(wb, entry.name, drawer_cols - written_cols,
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

		if (written_cols < drawer_cols) {
			int remaining = drawer_cols - written_cols;
			int wrote = 0;
			int root_bold = entry.is_root;
			int root_white = entry.is_root;
			int placeholder_gray = entry.is_placeholder;
			if (root_bold && !wbAppend(wb, VT100_BOLD_ON_4, 4)) {
				return 0;
			}
			if (root_white && !wbAppend(wb, VT100_FG_WHITE_5, 5)) {
				return 0;
			}
			if (placeholder_gray && !wbAppend(wb, VT100_FG_GRAY_5, 5)) {
				return 0;
			}
			if (!editorAppendSanitizedText(wb, entry.name, remaining, &wrote)) {
				return 0;
			}
			if (placeholder_gray && !wbAppend(wb, VT100_FG_DEFAULT_5, 5)) {
				return 0;
			}
			if (root_white && !wbAppend(wb, VT100_FG_DEFAULT_5, 5)) {
				return 0;
			}
			if (root_bold && !wbAppend(wb, VT100_BOLD_OFF_5, 5)) {
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

	if (row_inverted && !wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
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

	if (y_offset < E.numrows) {
		if (E.line_wrap_enabled) {
			if (!editorDrawFileRowWrapped(wb, (size_t)y_offset, text_cols, segment_coloff)) {
				return 0;
			}
		} else if (!editorDrawFileRow(wb, (size_t)y_offset, text_cols)) {
			return 0;
		}
	} else if (E.numrows == 0 && y == E.window_rows / 3) {
		if (!editorDrawGreeting(wb, text_cols)) {
			return 0;
		}
	} else if (!editorAppendGrayBytes(wb, "~", 1)) {
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

static int editorDrawRows(struct writeBuf *wb) {
	editorDrawerClampViewport(E.window_rows + 1);
	(void)editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows);

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	int file_row_draw_count = 0;
	int force_full = 0;
	if (!g_file_row_frame_cache.valid || g_file_row_frame_cache.window_rows != E.window_rows ||
			g_file_row_frame_cache.window_cols != E.window_cols) {
		force_full = 1;
	}

	if (!editorFileRowFrameCacheEnsureCapacity(E.window_rows)) {
		return 0;
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

static int editorDrawStatusBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
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

	int progress = 0;
	if (E.numrows == 1) {
		progress = 100;
	} else if (E.numrows > 1) {
		progress = (int)((float)E.cy / (E.numrows - 1) * 100);
	}
	if (progress < 0) {
		progress = 0;
	}
	if (progress > 100) {
		progress = 100;
	}
	int cursor_col = E.rx + 1;
	if (cursor_col < 1) {
		cursor_col = 1;
	}
	int rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%",
			E.cy + 1, cursor_col, progress);
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
	if (!wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
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
		int cursor_segment = editorWrapCursorSegmentForRx(E.rx, body_cols);
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

static const char *editorCursorStyleSequence(enum editorCursorStyle style, size_t *len_out) {
	const char *sequence = VT100_CURSOR_STEADY_BAR_5;
	switch (style) {
		case EDITOR_CURSOR_STYLE_BLOCK:
			sequence = VT100_CURSOR_STEADY_BLOCK_5;
			break;
		case EDITOR_CURSOR_STYLE_UNDERLINE:
			sequence = VT100_CURSOR_STEADY_UNDERLINE_5;
			break;
		case EDITOR_CURSOR_STYLE_BAR:
		default:
			sequence = VT100_CURSOR_STEADY_BAR_5;
			break;
	}
	if (len_out != NULL) {
		*len_out = strlen(sequence);
	}
	return sequence;
}

void editorRefreshScreen(void) {
	editorLspPumpNotifications();
	editorScroll();
	g_editor_output_last_refresh_file_row_draw_count = 0;

	struct writeBuf wb = WRITEBUF_INIT;
	size_t cursor_style_len = 0;
	const char *cursor_style_sequence =
			editorCursorStyleSequence(E.cursor_style, &cursor_style_len);

	// Build a full frame in memory and write once to reduce terminal flicker.
	if (!wbAppend(&wb, VT100_HIDE_CURSOR_6, 6) ||
			!wbAppend(&wb, cursor_style_sequence, cursor_style_len) ||
			!wbAppend(&wb, VT100_RESET_CURSOR_POS_3, 3)) {
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

	int cursor_row = (E.cy - E.rowoff) + 2;
	int cursor_col = editorTextBodyStartColForCols(E.window_cols) + (E.rx - E.coloff) + 1;
	int cursor_visible = 1;
	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = editorWrapCursorSegmentForRx(E.rx, body_cols);
		int cursor_segment_col = E.rx - (cursor_segment * body_cols);
		if (cursor_segment_col >= body_cols) {
			cursor_segment_col = body_cols - 1;
		}
		if (cursor_segment_col < 0) {
			cursor_segment_col = 0;
		}
		int cursor_distance = 0;
		if (editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
					E.window_rows > 0 ? E.window_rows - 1 : 0, body_cols, &cursor_distance)) {
			cursor_row = cursor_distance + 2;
		} else {
			cursor_visible = 0;
		}
		cursor_col = editorTextBodyStartColForCols(E.window_cols) + cursor_segment_col + 1;
	}
	if (E.pane_focus == EDITOR_PANE_DRAWER && editorDrawerWidthForCols(E.window_cols) > 0) {
		if (editorFileSearchIsActive()) {
			cursor_row = 1;
			cursor_col = editorFileSearchHeaderCursorCol(editorDrawerWidthForCols(E.window_cols));
		} else if (editorProjectSearchIsActive()) {
			cursor_row = 1;
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
