#include "render/display_text.h"

#include "render/write_buf.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/utf8.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static char displayTextHexUpperDigit(unsigned int value) {
	return value < 10 ? (char)('0' + value) : (char)('A' + (value - 10));
}

static void displayTextSanitizedToken(const char *text, int text_len, int idx,
                                      const char **token_out, int *token_len_out,
                                      int *token_cols_out, int *src_len_out, char escaped[4]) {
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
		escaped[2] = displayTextHexUpperDigit((cp >> 4) & 0x0F);
		escaped[3] = displayTextHexUpperDigit(cp & 0x0F);
		token = escaped;
		token_len = 4;
		token_cols = 4;
	}

	*token_out = token;
	*token_len_out = token_len;
	*token_cols_out = token_cols;
	*src_len_out = src_len;
}

int editorDisplayTextCols(const char *text) {
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

void editorDisplayWrapNextLine(const char *text, int text_len, int start_idx, int max_cols,
                               int *end_idx_out, int *cols_out) {
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

int editorDisplayWrapLineCount(const char *text, int max_cols) {
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

int editorAppendDisplayPrefix(struct writeBuf *wb, const char *text, int max_cols,
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

int editorAppendDisplaySuffix(struct writeBuf *wb, const char *text, int max_cols,
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
		int src_len =
		        editorUtf8DecodeCodepoint(&text[start_idx], text_len - start_idx, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > text_len - start_idx) {
			src_len = text_len - start_idx;
		}
		remaining_cols -= editorCharDisplayWidth(&text[start_idx], text_len - start_idx);
		start_idx += src_len;
	}

	if (start_idx < text_len &&
	    !wbAppend(wb, &text[start_idx], (size_t)(text_len - start_idx))) {
		return 0;
	}

	if (written_cols_out != NULL) {
		*written_cols_out = remaining_cols;
	}
	return 1;
}

int editorAppendDisplaySlice(struct writeBuf *wb, const char *text, int start_col, int max_cols,
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

char *editorSanitizeTextRangeDup(const char *text, int text_len, int *cols_out) {
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
	size_t out_cap = 1;
	int total_cols = 0;
	for (int idx = 0; idx < text_len;) {
		char escaped[4];
		const char *token = NULL;
		int token_len = 0;
		int token_cols = 0;
		int src_len = 0;
		displayTextSanitizedToken(text, text_len, idx, &token, &token_len, &token_cols,
		                          &src_len, escaped);

		size_t token_len_sz = 0;
		size_t new_len = 0;
		size_t alloc_len = 0;
		if (!editorIntToSize(token_len, &token_len_sz) ||
		    !editorSizeAdd(out_len, token_len_sz, &new_len) ||
		    new_len > ROTIDE_MAX_TEXT_BYTES || !editorSizeAdd(new_len, 1, &alloc_len)) {
			free(out);
			return NULL;
		}

		/* Grow geometrically: an exact realloc per token is O(tokens^2) under
		 * allocators that always copy (Fil-C's GC heap), and this builds tab
		 * labels and status segments every frame. */
		if (alloc_len > out_cap) {
			size_t new_cap = out_cap;
			while (new_cap < alloc_len) {
				if (new_cap > ROTIDE_MAX_TEXT_BYTES / 2) {
					new_cap = alloc_len;
					break;
				}
				new_cap *= 2;
			}
			char *grown = editorRealloc(out, new_cap);
			if (grown == NULL) {
				free(out);
				return NULL;
			}
			out = grown;
			out_cap = new_cap;
		}
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

char *editorSanitizeTextDup(const char *text, int *cols_out) {
	if (text == NULL) {
		return editorSanitizeTextRangeDup("", 0, cols_out);
	}
	int text_len = (int)strlen(text);
	return editorSanitizeTextRangeDup(text, text_len, cols_out);
}

static int displayTextDiagnosticMessageIsInlineSpace(unsigned int cp) {
	return cp <= 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

char *editorSanitizeDiagnosticMessageDup(const char *text, int *cols_out) {
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

		if (displayTextDiagnosticMessageIsInlineSpace(cp)) {
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

int editorAppendSanitizedText(struct writeBuf *wb, const char *text, int max_cols,
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
		displayTextSanitizedToken(text, text_len, idx, &token, &token_len, &token_cols,
		                          &src_len, escaped);

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

int editorAppendSanitizedMiddleTruncated(struct writeBuf *wb, const char *text, int max_cols,
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
			if (!editorAppendDisplayPrefix(wb, sanitized, prefix_cols,
			                               &prefix_written)) {
				free(sanitized);
				return 0;
			}
			if (!wbAppend(wb, marker, strlen(marker))) {
				free(sanitized);
				return 0;
			}
			if (!editorAppendDisplaySuffix(wb, sanitized, suffix_cols,
			                               &suffix_written)) {
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

static int displayTextAppendStatusBasename(struct writeBuf *wb, const char *sanitized_basename,
                                           int max_cols, const char *marker, int marker_cols,
                                           int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}

	if (max_cols <= marker_cols) {
		return editorAppendDisplayPrefix(wb, marker, max_cols, written_cols_out);
	}

	int suffix_written = 0;
	if (!wbAppend(wb, marker, strlen(marker)) ||
	    !editorAppendDisplaySuffix(wb, sanitized_basename, max_cols - marker_cols,
	                               &suffix_written)) {
		return 0;
	}
	if (written_cols_out != NULL) {
		*written_cols_out = marker_cols + suffix_written;
	}
	return 1;
}

static int displayTextAppendStatusDirPrefix(struct writeBuf *wb, const char *path, int dir_len,
                                            int prefix_budget, const char *marker, int marker_cols,
                                            int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (prefix_budget <= 0 || dir_len <= 0) {
		return 1;
	}

	int dir_cols = 0;
	char *sanitized_dir = editorSanitizeTextRangeDup(path, dir_len, &dir_cols);
	if (sanitized_dir == NULL) {
		return 0;
	}

	int prefix_written = 0;
	int ok = 1;
	if (dir_cols <= prefix_budget) {
		ok = editorAppendDisplayPrefix(wb, sanitized_dir, prefix_budget, &prefix_written);
	} else if (prefix_budget <= marker_cols) {
		ok = editorAppendDisplaySuffix(wb, sanitized_dir, prefix_budget, &prefix_written);
	} else {
		int suffix_written = 0;
		ok = wbAppend(wb, marker, strlen(marker)) &&
		     editorAppendDisplaySuffix(wb, sanitized_dir, prefix_budget - marker_cols,
		                               &suffix_written);
		prefix_written = marker_cols + suffix_written;
	}

	free(sanitized_dir);
	if (!ok) {
		return 0;
	}
	if (written_cols_out != NULL) {
		*written_cols_out = prefix_written;
	}
	return 1;
}

int editorAppendSanitizedStatusPath(struct writeBuf *wb, const char *path, int max_cols,
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
		if (!displayTextAppendStatusBasename(wb, sanitized_basename, max_cols, marker,
		                                     marker_cols, &written_cols)) {
			free(sanitized_basename);
			free(sanitized_full);
			return 0;
		}
	} else {
		int prefix_budget = max_cols - basename_cols;
		int prefix_written = 0;
		if (!displayTextAppendStatusDirPrefix(wb, path, dir_len, prefix_budget, marker,
		                                      marker_cols, &prefix_written)) {
			free(sanitized_basename);
			free(sanitized_full);
			return 0;
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
