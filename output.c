#include "output.h"

#include "alloc.h"
#include "buffer.h"
#include "size_utils.h"
#include <errno.h>
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
#define VT100_INVERTED_COLORS_4 "\x1b[7m"
#define VT100_NORMAL_COLORS_3 "\x1b[m"
#define DRAWER_SPLITTER_UTF8 "\xE2\x94\x82"

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

/*** Output ***/

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

static int editorDrawGreeting(struct writeBuf *wb, int cols) {
	char greet[80];
	int greetlen = snprintf(greet, sizeof(greet),
				"RotIDE editor - version %s", ROTIDE_VERSION);
	if (greetlen > cols) {
		greetlen = cols;
	}
	int pad = (cols - greetlen) / 2;
	if (pad) {
		if (!wbAppend(wb, "~", 1)) {
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
	} else if (E.search_match_row == row_idx && E.search_match_len > 0) {
		highlight_start_chars = E.search_match_start;
		highlight_len_chars = E.search_match_len;
	}

	if (highlight_len_chars <= 0) {
		return wbAppend(wb, &row->render[start], end - start);
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
		return wbAppend(wb, &row->render[start], end - start);
	}

	int highlight_start = start > match_render_start ? start : match_render_start;
	int highlight_end = end < match_render_end ? end : match_render_end;
	if (highlight_end <= highlight_start) {
		return wbAppend(wb, &row->render[start], end - start);
	}

	if (highlight_start > start &&
			!wbAppend(wb, &row->render[start], highlight_start - start)) {
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
			!wbAppend(wb, &row->render[highlight_end], end - highlight_end)) {
		return 0;
	}

	return 1;
}

static int editorDrawFileRow(struct writeBuf *wb, size_t i, int text_cols) {
	return editorDrawRenderSlice(wb, &E.rows[i], (int)i, E.coloff, text_cols);
}

static const char *editorTabLabelFromFilename(const char *filename) {
	if (filename == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(filename, '/');
	if (slash != NULL && slash[1] != '\0') {
		return slash + 1;
	}
	return filename;
}

static int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols);
static int editorDrawDrawerSeparatorCell(struct writeBuf *wb, int separator_cols);

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
			const char *label = editorTabLabelFromFilename(editorTabFilenameAt(tab_idx));
			int label_cols = content_width - slot_cols;
			int written = 0;
			if (!editorAppendSanitizedMiddleTruncated(wb, label, label_cols, &written)) {
				return 0;
			}
			slot_cols += written;
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

static int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols) {
	if (drawer_cols <= 0) {
		return 1;
	}

	struct editorDrawerEntryView entry;
	int visible_idx = E.drawer_rowoff + row_idx;
	int written_cols = 0;
	if (editorDrawerGetVisibleEntry(visible_idx, &entry)) {
		char selected_marker = entry.is_selected ? '>' : ' ';
		if (!wbAppend(wb, &selected_marker, 1)) {
			return 0;
		}
		written_cols++;

		char dir_marker = ' ';
		if (entry.is_dir) {
			dir_marker = entry.is_expanded ? 'v' : '>';
		}
		if (entry.has_scan_error) {
			dir_marker = '!';
		}
		if (written_cols < drawer_cols && !wbAppend(wb, &dir_marker, 1)) {
			return 0;
		}
		if (written_cols < drawer_cols) {
			written_cols++;
		}

		if (written_cols < drawer_cols && !wbAppend(wb, " ", 1)) {
			return 0;
		}
		if (written_cols < drawer_cols) {
			written_cols++;
		}

		int indent = entry.depth * 2;
		while (indent > 0 && written_cols < drawer_cols) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			written_cols++;
			indent--;
		}

		if (written_cols < drawer_cols) {
			int remaining = drawer_cols - written_cols;
			int wrote = 0;
			if (!editorAppendSanitizedText(wb, entry.name, remaining, &wrote)) {
				return 0;
			}
			written_cols += wrote;
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

static int editorDrawRows(struct writeBuf *wb) {
	(void)editorDrawerMoveSelectionBy(0, E.window_rows + 1);

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	for (int y = 0; y < E.window_rows; y++) {
		int y_offset = y + E.rowoff;

		if (!editorDrawDrawerRow(wb, y + 1, drawer_cols)) {
			return 0;
		}
		if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
			return 0;
		}

		if (y_offset < E.numrows) {
			if (!editorDrawFileRow(wb, y_offset, text_cols)) {
				return 0;
			}
		} else if (E.numrows == 0 && y == E.window_rows / 3) {
			if (!editorDrawGreeting(wb, text_cols)) {
				return 0;
			}
		} else {
			if (!wbAppend(wb, "~", 1)) {
				return 0;
			}
		}

		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			return 0;
		}
		if (!wbAppend(wb, "\r\n", 2)) {
			return 0;
		}
	}

	return 1;
}

static int editorDrawStatusBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
		return 0;
	}
	char rightbuf[80];
	const char *filename = E.filename;
	if (filename == NULL) {
		filename = "[No Name]";
	}
	const char *dirtyflag = "";
	if (E.dirty) {
		dirtyflag = "[+]";
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

	int left_cols = 0;
	if (!editorAppendSanitizedStatusPath(wb, filename, path_budget, &left_cols)) {
		return 0;
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
}

static void editorUpdateRenderXFromCursor(void) {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}
}

static void editorFollowCursorViewport(void) {
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	if (text_cols < 1) {
		text_cols = 1;
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
	editorScroll();

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

	if (!editorDrawTabBar(&wb) || !editorDrawRows(&wb) || !editorDrawStatusBar(&wb) ||
			!editorDrawMessageBar(&wb)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}

	int cursor_row = (E.cy - E.rowoff) + 2;
	int cursor_col = editorDrawerTextStartColForCols(E.window_cols) + (E.rx - E.coloff) + 1;
	int cursor_visible = 1;
	if (E.pane_focus == EDITOR_PANE_DRAWER && editorDrawerWidthForCols(E.window_cols) > 0) {
		int drawer_row = E.drawer_selected_index - E.drawer_rowoff;
		if (drawer_row < 0) {
			drawer_row = 0;
		}
		if (drawer_row > E.window_rows) {
			drawer_row = E.window_rows;
		}
		cursor_row = drawer_row + 1;
		cursor_col = 1;
	} else {
		int text_row_min = 2;
		int text_row_max = E.window_rows + 1;
		if (text_row_max < text_row_min) {
			text_row_max = text_row_min;
		}

		int text_col_min = editorDrawerTextStartColForCols(E.window_cols) + 1;
		int text_col_max = text_col_min + editorDrawerTextViewportCols(E.window_cols) - 1;
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
