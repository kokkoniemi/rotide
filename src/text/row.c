#include "text/row.h"

#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/utf8.h"

#include <stdlib.h>
#include <string.h>

int editorRowClampCxToCharBoundary(const struct erow *row, int cx) {
	if (cx < 0) {
		return 0;
	}
	if (cx > row->size) {
		cx = row->size;
	}
	while (cx > 0 && cx < row->size &&
			editorIsUtf8ContinuationByte((unsigned char)row->chars[cx])) {
		cx--;
	}
	return cx;
}

int editorRowPrevCharIdx(const struct erow *row, int idx) {
	if (idx <= 0) {
		return 0;
	}
	idx = editorRowClampCxToCharBoundary(row, idx);
	idx--;
	while (idx > 0 && editorIsUtf8ContinuationByte((unsigned char)row->chars[idx])) {
		idx--;
	}
	return idx;
}

int editorRowNextCharIdx(const struct erow *row, int idx) {
	if (idx >= row->size) {
		return row->size;
	}
	idx = editorRowClampCxToCharBoundary(row, idx);
	unsigned int cp = 0;
	int step = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &cp);
	if (step <= 0) {
		step = 1;
	}
	if (idx + step > row->size) {
		return row->size;
	}
	return idx + step;
}

int editorRowNextClusterIdx(const struct erow *row, int idx) {
	idx = editorRowClampCxToCharBoundary(row, idx);
	if (idx >= row->size) {
		return row->size;
	}

	unsigned int cp = 0;
	int cp_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &cp);
	if (cp_len <= 0) {
		cp_len = 1;
	}
	idx += cp_len;

	// Pair regional indicators into one cluster so flag emojis step as one unit.
	if (editorIsRegionalIndicatorCodepoint(cp)) {
		if (idx < row->size) {
			unsigned int next_cp = 0;
			int next_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &next_cp);
			if (next_len <= 0) {
				next_len = 1;
			}
			if (editorIsRegionalIndicatorCodepoint(next_cp)) {
				idx += next_len;
			}
		}
		return idx;
	}

	while (idx < row->size) {
		unsigned int next_cp = 0;
		int next_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &next_cp);
		if (next_len <= 0) {
			next_len = 1;
		}

		if (editorIsGraphemeExtendCodepoint(next_cp)) {
			idx += next_len;
			continue;
		}

		// Keep ZWJ-linked emoji sequences in a single grapheme cluster.
		if (next_cp == 0x200D) {
			int after_zwj = idx + next_len;
			idx = after_zwj;
			if (idx >= row->size) {
				return row->size;
			}

			int linked_len = editorUtf8DecodeCodepoint(
					&row->chars[idx], row->size - idx, &next_cp);
			if (linked_len <= 0) {
				linked_len = 1;
			}
			idx += linked_len;
			continue;
		}

		break;
	}

	return idx;
}

int editorRowPrevClusterIdx(const struct erow *row, int idx) {
	idx = editorRowClampCxToCharBoundary(row, idx);
	if (idx <= 0) {
		return 0;
	}

	int prev = 0;
	int scan = 0;
	while (scan < idx) {
		prev = scan;
		scan = editorRowNextClusterIdx(row, scan);
		if (scan <= prev) {
			return prev;
		}
	}

	return prev;
}

int editorRowClampCxToClusterBoundary(const struct erow *row, int cx) {
	cx = editorRowClampCxToCharBoundary(row, cx);
	if (cx <= 0) {
		return 0;
	}

	int boundary = 0;
	while (boundary < cx) {
		int next_boundary = editorRowNextClusterIdx(row, boundary);
		if (next_boundary > cx || next_boundary <= boundary) {
			break;
		}
		boundary = next_boundary;
	}

	return boundary;
}

static char editorHexUpperDigit(unsigned int value) {
	return value < 10 ? (char)('0' + value) : (char)('A' + (value - 10));
}

// Convert the next source token into render-space metadata.
// This is the single source of truth for:
// 1) bytes consumed from row->chars,
// 2) bytes produced in row->render,
// 3) display columns occupied on screen.
// Keeping these together ensures render-building, cursor math, and highlight
// mapping stay consistent when controls are escaped.
static int editorBuildRenderToken(const char *s, int len, int rx, int expand_tabs,
		char *render_out, int *src_len_out, int *render_len_out, int *width_out) {
	if (s == NULL || len <= 0) {
		return 0;
	}

	unsigned int cp = 0;
	int src_len = editorUtf8DecodeCodepoint(s, len, &cp);
	if (src_len <= 0) {
		// Invalid leading byte: consume one byte so callers always make progress.
		src_len = 1;
	}
	if (src_len > len) {
		src_len = len;
	}

	if (cp == '\t' && expand_tabs) {
		int spaces = ROTIDE_TAB_WIDTH - (rx % ROTIDE_TAB_WIDTH);
		if (spaces <= 0) {
			spaces = ROTIDE_TAB_WIDTH;
		}
		if (render_out != NULL) {
			for (int i = 0; i < spaces; i++) {
				render_out[i] = ' ';
			}
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = spaces;
		}
		if (width_out != NULL) {
			*width_out = spaces;
		}
		return 1;
	}

	if (cp <= 0x1F) {
		if (render_out != NULL) {
			render_out[0] = '^';
			render_out[1] = (char)('@' + (int)cp);
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = 2;
		}
		if (width_out != NULL) {
			*width_out = 2;
		}
		return 1;
	}

	if (cp == 0x7F) {
		if (render_out != NULL) {
			render_out[0] = '^';
			render_out[1] = '?';
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = 2;
		}
		if (width_out != NULL) {
			*width_out = 2;
		}
		return 1;
	}

	if (cp >= 0x80 && cp <= 0x9F) {
		// Render C1 controls as ASCII text so raw bytes never reach the terminal.
		if (render_out != NULL) {
			render_out[0] = '\\';
			render_out[1] = 'x';
			render_out[2] = editorHexUpperDigit((cp >> 4) & 0x0F);
			render_out[3] = editorHexUpperDigit(cp & 0x0F);
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = 4;
		}
		if (width_out != NULL) {
			*width_out = 4;
		}
		return 1;
	}

	if (render_out != NULL) {
		memcpy(render_out, s, (size_t)src_len);
	}
	if (src_len_out != NULL) {
		*src_len_out = src_len;
	}
	if (render_len_out != NULL) {
		*render_len_out = src_len;
	}
	if (width_out != NULL) {
		*width_out = editorCharDisplayWidth(s, len);
	}
	return 1;
}

int editorRowCxToRx(const struct erow *row, int cx) {
	int rx = 0;
	cx = editorRowClampCxToClusterBoundary(row, cx);
	for (int idx = 0; idx < cx && idx < row->size;) {
		int src_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&row->chars[idx], row->size - idx, rx, 1, NULL,
				&src_len, NULL, &token_width)) {
			break;
		}
		if (src_len <= 0) {
			break;
		}
		rx += token_width;
		idx += src_len;
	}
	return rx;
}

int editorRowRxToCx(const struct erow *row, int rx) {
	if (rx <= 0) {
		return 0;
	}

	int cx = 0;
	int cur_rx = 0;
	while (cx < row->size) {
		int next_cx = editorRowNextClusterIdx(row, cx);
		if (next_cx <= cx) {
			break;
		}

		int cluster_width = 0;
		// Compute width for the whole grapheme cluster so cursor positions never
		// land inside a cluster even when escaped controls widen render output.
		for (int idx = cx; idx < next_cx;) {
			int src_len = 0;
			int token_width = 0;
			if (!editorBuildRenderToken(&row->chars[idx], row->size - idx, cur_rx + cluster_width,
					1, NULL, &src_len, NULL, &token_width)) {
				break;
			}
			if (src_len <= 0) {
				break;
			}
			cluster_width += token_width;
			idx += src_len;
		}

		if (cur_rx + cluster_width > rx) {
			return cx;
		}

		cur_rx += cluster_width;
		cx = next_cx;
	}

	return row->size;
}

int editorRowCxToRenderIdx(const struct erow *row, int cx) {
	int clamped_cx = editorRowClampCxToClusterBoundary(row, cx);
	int render_idx = 0;
	int rx = 0;
	// Map logical char-space boundaries to byte offsets in row->render using
	// the exact same tokenization as render construction and rx/cx conversion.
	for (int idx = 0; idx < clamped_cx && idx < row->size;) {
		int src_len = 0;
		int render_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&row->chars[idx], row->size - idx, rx, 1, NULL,
				&src_len, &render_len, &token_width)) {
			break;
		}
		if (src_len <= 0) {
			break;
		}
		render_idx += render_len;
		rx += token_width;
		idx += src_len;
	}
	if (render_idx > row->rsize) {
		render_idx = row->rsize;
	}
	return render_idx;
}

int editorRowBuildRender(const char *chars, int size, char **render_out, int *rsize_out,
		int *display_cols_out) {
	size_t render_cap = 1;
	int rx = 0;
	// Capacity prepass mirrors the write pass token-for-token.
	for (int idx = 0; idx < size;) {
		int src_len = 0;
		int render_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&chars[idx], size - idx, rx, 1, NULL, &src_len,
				&render_len, &token_width)) {
			return 0;
		}
		if (src_len <= 0) {
			return 0;
		}
		size_t token_len = 0;
		if (!editorIntToSize(render_len, &token_len)) {
			return 0;
		}
		if (!editorSizeAdd(render_cap, token_len, &render_cap)) {
			return 0;
		}
		if (render_cap - 1 > ROTIDE_MAX_TEXT_BYTES) {
			return 0;
		}
		rx += token_width;
		idx += src_len;
	}

	char *render = editorMalloc(render_cap);
	if (render == NULL) {
		return 0;
	}

	size_t out_idx = 0;
	rx = 0;
	for (int idx = 0; idx < size;) {
		// Largest escaped token written here is a tab expansion (TAB_WIDTH spaces).
		char token[ROTIDE_TAB_WIDTH];
		int src_len = 0;
		int render_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&chars[idx], size - idx, rx, 1, token, &src_len,
				&render_len, &token_width)) {
			free(render);
			return 0;
		}
		size_t token_len = 0;
		if (!editorIntToSize(render_len, &token_len)) {
			free(render);
			return 0;
		}
		memcpy(&render[out_idx], token, token_len);
		out_idx += token_len;
		rx += token_width;
		idx += src_len;
	}
	render[out_idx] = '\0';

	int out_idx_int = 0;
	if (!editorSizeToInt(out_idx, &out_idx_int)) {
		free(render);
		return 0;
	}
	*render_out = render;
	*rsize_out = out_idx_int;
	if (display_cols_out != NULL) {
		*display_cols_out = rx;
	}
	return 1;
}
