#include "buffer.h"

#include "alloc.h"
#include "input.h"
#include "output.h"
#include "save_syscalls.h"
#include "size_utils.h"
#include "terminal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

/*** File io ***/

#define NEWLINE_CHAR_WIDTH 1

static char *editorPathJoin(const char *left, const char *right);

static void editorSetAllocFailureStatus(void) {
	editorSetStatusMsg("Out of memory");
}

static void editorSetOperationTooLargeStatus(void) {
	editorSetStatusMsg("Operation too large");
}

static void editorSetFileTooLargeStatus(void) {
	editorSetStatusMsg("File too large");
}

char *editorRowsToStr(size_t *buflen) {
	if (buflen == NULL) {
		errno = EINVAL;
		return NULL;
	}

	size_t total = 0;
	for (int i = 0; i < E.numrows; i++) {
		size_t row_size = 0;
		size_t row_total = 0;
		if (!editorIntToSize(E.rows[i].size, &row_size) ||
				!editorSizeAdd(row_size, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(total, row_total, &total) ||
				total > ROTIDE_MAX_TEXT_BYTES) {
			errno = EOVERFLOW;
			*buflen = 0;
			return NULL;
		}
	}

	// Save format is a plain newline-terminated concatenation of rows.
	char *buf = NULL;
	if (total > 0) {
		buf = editorMalloc(total);
		if (buf == NULL) {
			errno = ENOMEM;
			*buflen = total;
			return NULL;
		}
	}
	char *p = buf;
	for (int i = 0; i < E.numrows; i++) {
		size_t row_size = (size_t)E.rows[i].size;
		memcpy(p, E.rows[i].chars, row_size);
		p += row_size;
		*p = '\n';
		p++;
	}

	errno = 0;
	*buflen = total;
	return buf;
}

int editorIsUtf8ContinuationByte(unsigned char c) {
	return (c & 0xC0) == 0x80;
}

int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp) {
	if (len <= 0) {
		*cp = 0;
		return 0;
	}

	unsigned char b0 = (unsigned char)s[0];
	if (b0 < 0x80) {
		*cp = b0;
		return 1;
	}

	int expected_len = 0;
	unsigned int codepoint = 0;
	unsigned int min_codepoint = 0;

	// Determine UTF-8 sequence length and initial payload bits from leading byte.
	if ((b0 & 0xE0) == 0xC0) {
		expected_len = 2;
		codepoint = b0 & 0x1F;
		min_codepoint = 0x80;
	} else if ((b0 & 0xF0) == 0xE0) {
		expected_len = 3;
		codepoint = b0 & 0x0F;
		min_codepoint = 0x800;
	} else if ((b0 & 0xF8) == 0xF0) {
		expected_len = 4;
		codepoint = b0 & 0x07;
		min_codepoint = 0x10000;
	} else {
		*cp = b0;
		return 1;
	}

	// If sequence is truncated or malformed, treat first byte as standalone.
	if (len < expected_len) {
		*cp = b0;
		return 1;
	}

	for (int i = 1; i < expected_len; i++) {
		unsigned char bx = (unsigned char)s[i];
		if (!editorIsUtf8ContinuationByte(bx)) {
			*cp = b0;
			return 1;
		}
			codepoint = (codepoint << 6) | (unsigned int)(bx & 0x3F);
	}

	// Reject overlong forms, surrogate range, and out-of-range codepoints.
	if (codepoint < min_codepoint || codepoint > 0x10FFFF ||
			(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
		*cp = b0;
		return 1;
	}

	*cp = codepoint;
	return expected_len;
}

int editorIsRegionalIndicatorCodepoint(unsigned int cp) {
	return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

int editorIsGraphemeExtendCodepoint(unsigned int cp) {
	// Unicode combining mark blocks that should stay in the same grapheme.
	if ((cp >= 0x0300 && cp <= 0x036F) ||
			(cp >= 0x1AB0 && cp <= 0x1AFF) ||
			(cp >= 0x1DC0 && cp <= 0x1DFF) ||
			(cp >= 0x20D0 && cp <= 0x20FF) ||
			(cp >= 0xFE20 && cp <= 0xFE2F)) {
		return 1;
	}
	// Variation selectors modify the previous glyph and should not split clusters.
	if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
			(cp >= 0xE0100 && cp <= 0xE01EF)) {
		return 1;
	}
	// Emoji skin-tone modifiers are attached to the previous emoji.
	if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
		return 1;
	}
	// Keep ZWNJ with the current cluster for cursor stepping consistency.
	if (cp == 0x200C) {
		return 1;
	}
	if (cp > (unsigned int)WCHAR_MAX) {
		return 0;
	}
	// Fallback: many libc locales report combining marks with width 0.
	wchar_t wc = (wchar_t)cp;
	int width = wcwidth(wc);
	return width == 0 && cp != 0x200D;
}

int editorCharDisplayWidth(const char *s, int len) {
	unsigned char c = s[0];
	if (c < 0x80) {
		return 1;
	}
	// Continuation bytes are part of a previous codepoint and should not
	// advance visual columns when scanning byte-by-byte.
	if (editorIsUtf8ContinuationByte(c)) {
		return 0;
	}

	mbstate_t ps = {0};
	wchar_t wc;
	size_t read = mbrtowc(&wc, s, len, &ps);
	if (read == (size_t)-1 || read == (size_t)-2) {
		return 1;
	}
	int width = wcwidth(wc);
	if (width < 0) {
		return 1;
	}
	return width;
}

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

static int editorBuildRender(const char *chars, int size, char **render_out, int *rsize_out) {
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
	return 1;
}

static void editorFreeRowArray(struct erow *rows, int numrows);
static int editorSnapshotBuildRows(const struct editorSnapshot *snapshot, struct erow **rows_out,
		int *numrows_out);
static void editorSnapshotClampCursor(const struct editorSnapshot *snapshot);

/*** Selection and clipboard ***/

static void editorClearSelectionState(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_cx = 0;
	E.selection_anchor_cy = 0;
}

static int editorPosComesBefore(int left_cy, int left_cx, int right_cy, int right_cx) {
	if (left_cy != right_cy) {
		return left_cy < right_cy;
	}
	return left_cx < right_cx;
}

static void editorClampPositionToBuffer(int *cy, int *cx) {
	if (*cy < 0) {
		*cy = 0;
	}
	if (*cy > E.numrows) {
		*cy = E.numrows;
	}

	if (*cy == E.numrows) {
		*cx = 0;
		return;
	}

	struct erow *row = &E.rows[*cy];
	if (*cx < 0) {
		*cx = 0;
	}
	if (*cx > row->size) {
		*cx = row->size;
	}
	*cx = editorRowClampCxToClusterBoundary(row, *cx);
	if (*cx > row->size) {
		*cx = row->size;
	}
}

static int editorNormalizeRange(const struct editorSelectionRange *range,
		struct editorSelectionRange *out) {
	if (range == NULL || out == NULL) {
		return 0;
	}

	int start_cy = range->start_cy;
	int start_cx = range->start_cx;
	int end_cy = range->end_cy;
	int end_cx = range->end_cx;
	editorClampPositionToBuffer(&start_cy, &start_cx);
	editorClampPositionToBuffer(&end_cy, &end_cx);

	if (editorPosComesBefore(end_cy, end_cx, start_cy, start_cx)) {
		int tmp_cy = start_cy;
		int tmp_cx = start_cx;
		start_cy = end_cy;
		start_cx = end_cx;
		end_cy = tmp_cy;
		end_cx = tmp_cx;
	}

	if (start_cy == end_cy && start_cx == end_cx) {
		return 0;
	}

	out->start_cy = start_cy;
	out->start_cx = start_cx;
	out->end_cy = end_cy;
	out->end_cx = end_cx;
	return 1;
}

static int editorPosToLinearOffset(int cy, int cx, size_t *offset_out) {
	size_t offset = 0;
	for (int row = 0; row < cy; row++) {
		size_t row_size = 0;
		size_t row_total = 0;
		if (!editorIntToSize(E.rows[row].size, &row_size) ||
				!editorSizeAdd(row_size, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(offset, row_total, &offset) ||
				offset > ROTIDE_MAX_TEXT_BYTES) {
			return 0;
		}
	}

	size_t cx_size = 0;
	if (!editorIntToSize(cx, &cx_size) ||
			!editorSizeAdd(offset, cx_size, &offset) ||
			offset > ROTIDE_MAX_TEXT_BYTES) {
		return 0;
	}

	*offset_out = offset;
	return 1;
}

int editorGetSelectionRange(struct editorSelectionRange *range_out) {
	if (range_out == NULL || !E.selection_mode_active) {
		return 0;
	}

	struct editorSelectionRange range = {
		.start_cy = E.selection_anchor_cy,
		.start_cx = E.selection_anchor_cx,
		.end_cy = E.cy,
		.end_cx = E.cx
	};
	if (!editorNormalizeRange(&range, range_out)) {
		return 0;
	}

	return 1;
}

int editorExtractRangeText(const struct editorSelectionRange *range, char **text_out,
		size_t *len_out) {
	if (text_out == NULL || len_out == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	struct editorSelectionRange normalized;
	if (!editorNormalizeRange(range, &normalized)) {
		return 0;
	}

	size_t full_len = 0;
	errno = 0;
	char *full_text = editorRowsToStr(&full_len);
	if (full_text == NULL && (full_len > 0 || errno != 0)) {
		if (errno == EOVERFLOW) {
			editorSetOperationTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return -1;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	if (!editorPosToLinearOffset(normalized.start_cy, normalized.start_cx, &start_offset) ||
			!editorPosToLinearOffset(normalized.end_cy, normalized.end_cx, &end_offset) ||
			end_offset < start_offset || end_offset > full_len) {
		free(full_text);
		editorSetOperationTooLargeStatus();
		return -1;
	}
	size_t selected_len = end_offset - start_offset;
	if (selected_len == 0) {
		free(full_text);
		return 0;
	}

	size_t selected_cap = 0;
	if (!editorSizeAdd(selected_len, 1, &selected_cap)) {
		free(full_text);
		editorSetOperationTooLargeStatus();
		return -1;
	}

	char *selected = editorMalloc(selected_cap);
	if (selected == NULL) {
		free(full_text);
		editorSetAllocFailureStatus();
		return -1;
	}
	memcpy(selected, &full_text[start_offset], selected_len);
	selected[selected_len] = '\0';
	free(full_text);

	*text_out = selected;
	*len_out = selected_len;
	return 1;
}

int editorDeleteRange(const struct editorSelectionRange *range) {
	struct editorSelectionRange normalized;
	if (!editorNormalizeRange(range, &normalized)) {
		return 0;
	}

	size_t full_len = 0;
	errno = 0;
	char *full_text = editorRowsToStr(&full_len);
	if (full_text == NULL && (full_len > 0 || errno != 0)) {
		if (errno == EOVERFLOW) {
			editorSetOperationTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return -1;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	if (!editorPosToLinearOffset(normalized.start_cy, normalized.start_cx, &start_offset) ||
			!editorPosToLinearOffset(normalized.end_cy, normalized.end_cx, &end_offset) ||
			end_offset < start_offset || end_offset > full_len) {
		free(full_text);
		editorSetOperationTooLargeStatus();
		return -1;
	}
	size_t removed_len = end_offset - start_offset;
	if (removed_len == 0) {
		free(full_text);
		return 0;
	}

	size_t new_len = full_len - removed_len;
	char *new_text = NULL;
	if (new_len > 0) {
		size_t new_cap = 0;
		if (!editorSizeAdd(new_len, 1, &new_cap)) {
			free(full_text);
			editorSetOperationTooLargeStatus();
			return -1;
		}
		new_text = editorMalloc(new_cap);
		if (new_text == NULL) {
			free(full_text);
			editorSetAllocFailureStatus();
			return -1;
		}

		memcpy(new_text, full_text, start_offset);
		memcpy(&new_text[start_offset], &full_text[end_offset], full_len - end_offset);
		new_text[new_len] = '\0';
	}
	free(full_text);

	struct editorSnapshot replacement = {
		.text = new_text,
		.textlen = new_len,
		.cx = normalized.start_cx,
		.cy = normalized.start_cy,
		.dirty = E.dirty + 1
	};
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	if (!editorSnapshotBuildRows(&replacement, &new_rows, &new_numrows)) {
		free(new_text);
		editorSetAllocFailureStatus();
		return -1;
	}

	struct erow *old_rows = E.rows;
	int old_numrows = E.numrows;

	E.rows = new_rows;
	E.numrows = new_numrows;
	editorSnapshotClampCursor(&replacement);
	E.dirty = replacement.dirty;

	editorFreeRowArray(old_rows, old_numrows);
	free(new_text);
	return 1;
}

int editorClipboardSet(const char *text, size_t len) {
	if (len > ROTIDE_MAX_TEXT_BYTES) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	char *new_text = NULL;
	if (len > 0) {
		if (text == NULL) {
			return 0;
		}
		size_t cap = 0;
		if (!editorSizeAdd(len, 1, &cap)) {
			editorSetOperationTooLargeStatus();
			return 0;
		}
		new_text = editorMalloc(cap);
		if (new_text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
		memcpy(new_text, text, len);
		new_text[len] = '\0';
	}

	free(E.clipboard_text);
	E.clipboard_text = new_text;
	E.clipboard_textlen = len;

	if (E.clipboard_external_sink != NULL) {
		const char *sink_text = E.clipboard_text;
		if (sink_text == NULL) {
			sink_text = "";
		}
		E.clipboard_external_sink(sink_text, E.clipboard_textlen);
	}

	return 1;
}

const char *editorClipboardGet(size_t *len_out) {
	if (len_out != NULL) {
		*len_out = E.clipboard_textlen;
	}
	if (E.clipboard_text != NULL) {
		return E.clipboard_text;
	}
	return "";
}

void editorClipboardClear(void) {
	free(E.clipboard_text);
	E.clipboard_text = NULL;
	E.clipboard_textlen = 0;
}

void editorClipboardSetExternalSink(editorClipboardExternalSink sink) {
	E.clipboard_external_sink = sink;
}

/*** History ***/

static void editorSnapshotFree(struct editorSnapshot *snapshot) {
	free(snapshot->text);
	snapshot->text = NULL;
	snapshot->textlen = 0;
	snapshot->cx = 0;
	snapshot->cy = 0;
	snapshot->dirty = 0;
}

static void editorHistoryClear(struct editorHistory *history) {
	for (int i = 0; i < history->len; i++) {
		int idx = (history->start + i) % ROTIDE_UNDO_HISTORY_LIMIT;
		editorSnapshotFree(&history->entries[idx]);
	}
	history->start = 0;
	history->len = 0;
}

static void editorHistoryPushNewest(struct editorHistory *history, struct editorSnapshot *snapshot) {
	int slot = 0;
	if (history->len < ROTIDE_UNDO_HISTORY_LIMIT) {
		slot = (history->start + history->len) % ROTIDE_UNDO_HISTORY_LIMIT;
		history->len++;
	} else {
		slot = history->start;
		editorSnapshotFree(&history->entries[slot]);
		history->start = (history->start + 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	}

	history->entries[slot] = *snapshot;
	snapshot->text = NULL;
	snapshot->textlen = 0;
	snapshot->cx = 0;
	snapshot->cy = 0;
	snapshot->dirty = 0;
}

static int editorHistoryPopNewest(struct editorHistory *history, struct editorSnapshot *snapshot) {
	if (history->len == 0) {
		return 0;
	}

	int idx = (history->start + history->len - 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	*snapshot = history->entries[idx];
	history->entries[idx].text = NULL;
	history->entries[idx].textlen = 0;
	history->entries[idx].cx = 0;
	history->entries[idx].cy = 0;
	history->entries[idx].dirty = 0;
	history->len--;
	if (history->len == 0) {
		history->start = 0;
	}
	return 1;
}

static int editorSnapshotCaptureCurrent(struct editorSnapshot *snapshot) {
	memset(snapshot, 0, sizeof(*snapshot));

	size_t textlen = 0;
	errno = 0;
	char *text = editorRowsToStr(&textlen);
	if (text == NULL && (textlen > 0 || errno != 0)) {
		if (errno == EOVERFLOW) {
			editorSetOperationTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return 0;
	}

	snapshot->text = text;
	snapshot->textlen = textlen;
	snapshot->cx = E.cx;
	snapshot->cy = E.cy;
	snapshot->dirty = E.dirty;
	return 1;
}

static void editorFreeRowArray(struct erow *rows, int numrows) {
	for (int i = 0; i < numrows; i++) {
		free(rows[i].chars);
		free(rows[i].render);
	}
	free(rows);
}

static int editorAppendRestoredRow(struct erow **rows, int *numrows, const char *s, size_t len) {
	int row_size = 0;
	size_t row_cap = 0;
	size_t numrows_size = 0;
	size_t new_numrows = 0;
	size_t row_bytes = 0;

	if (!editorSizeToInt(len, &row_size) ||
			!editorSizeAdd(len, 1, &row_cap) ||
			!editorIntToSize(*numrows, &numrows_size) ||
			!editorSizeAdd(numrows_size, 1, &new_numrows) ||
			!editorSizeMul(sizeof(struct erow), new_numrows, &row_bytes)) {
		return 0;
	}

	char *row_chars = editorMalloc(row_cap);
	if (row_chars == NULL) {
		return 0;
	}
	memcpy(row_chars, s, len);
	row_chars[len] = '\0';

	char *row_render = NULL;
	int row_rsize = 0;
	if (!editorBuildRender(row_chars, row_size, &row_render, &row_rsize)) {
		free(row_chars);
		return 0;
	}

	struct erow *new_rows = editorRealloc(*rows, row_bytes);
	if (new_rows == NULL) {
		free(row_render);
		free(row_chars);
		return 0;
	}

	*rows = new_rows;
	(*rows)[*numrows].size = row_size;
	(*rows)[*numrows].rsize = row_rsize;
	(*rows)[*numrows].chars = row_chars;
	(*rows)[*numrows].render = row_render;
	(*numrows)++;
	return 1;
}

static int editorSnapshotBuildRows(const struct editorSnapshot *snapshot, struct erow **rows_out,
		int *numrows_out) {
	struct erow *rows = NULL;
	int numrows = 0;
	size_t line_start = 0;

	for (size_t i = 0; i < snapshot->textlen; i++) {
		if (snapshot->text[i] != '\n') {
			continue;
		}

		size_t line_len = i - line_start;
		if (!editorAppendRestoredRow(&rows, &numrows, &snapshot->text[line_start], line_len)) {
			editorFreeRowArray(rows, numrows);
			return 0;
		}
		line_start = i + 1;
	}

	if (line_start < snapshot->textlen) {
		size_t line_len = snapshot->textlen - line_start;
		if (!editorAppendRestoredRow(&rows, &numrows, &snapshot->text[line_start], line_len)) {
			editorFreeRowArray(rows, numrows);
			return 0;
		}
	}

	*rows_out = rows;
	*numrows_out = numrows;
	return 1;
}

static void editorSnapshotClampCursor(const struct editorSnapshot *snapshot) {
	if (snapshot->cy < 0) {
		E.cy = 0;
	} else if (snapshot->cy > E.numrows) {
		E.cy = E.numrows;
	} else {
		E.cy = snapshot->cy;
	}

	if (E.cy < E.numrows) {
		struct erow *row = &E.rows[E.cy];
		int cx = snapshot->cx;
		if (cx < 0) {
			cx = 0;
		}
		if (cx > row->size) {
			cx = row->size;
		}
		E.cx = editorRowClampCxToClusterBoundary(row, cx);
		if (E.cx > row->size) {
			E.cx = row->size;
		}
		if (E.cx < 0) {
			E.cx = 0;
		}
	} else {
		E.cx = 0;
	}
}

static int editorSnapshotRestore(const struct editorSnapshot *snapshot) {
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	if (!editorSnapshotBuildRows(snapshot, &new_rows, &new_numrows)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct erow *old_rows = E.rows;
	int old_numrows = E.numrows;

	E.rows = new_rows;
	E.numrows = new_numrows;
	editorSnapshotClampCursor(snapshot);
	E.dirty = snapshot->dirty;
	editorClearSelectionState();

	editorFreeRowArray(old_rows, old_numrows);
	return 1;
}

static void editorTabStateInitEmpty(struct editorTabState *tab) {
	memset(tab, 0, sizeof(*tab));
	tab->search_match_row = -1;
	tab->search_direction = 1;
}

static void editorResetActiveBufferFields(void) {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.search_query = NULL;
	E.search_match_row = -1;
	E.search_match_start = 0;
	E.search_match_len = 0;
	E.search_direction = 1;
	E.search_saved_cx = 0;
	E.search_saved_cy = 0;
	E.selection_mode_active = 0;
	E.selection_anchor_cx = 0;
	E.selection_anchor_cy = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_anchor_cx = 0;
	E.mouse_drag_anchor_cy = 0;
	E.mouse_drag_started = 0;
	E.undo_history.start = 0;
	E.undo_history.len = 0;
	E.redo_history.start = 0;
	E.redo_history.len = 0;
	E.edit_pending_snapshot.text = NULL;
	E.edit_pending_snapshot.textlen = 0;
	E.edit_pending_snapshot.cx = 0;
	E.edit_pending_snapshot.cy = 0;
	E.edit_pending_snapshot.dirty = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

static void editorFreeTabRows(struct editorTabState *tab) {
	for (int i = 0; i < tab->numrows; i++) {
		free(tab->rows[i].chars);
		free(tab->rows[i].render);
	}
	free(tab->rows);
	tab->rows = NULL;
	tab->numrows = 0;
}

static void editorTabStateFree(struct editorTabState *tab) {
	editorFreeTabRows(tab);
	free(tab->filename);
	tab->filename = NULL;
	free(tab->search_query);
	tab->search_query = NULL;
	editorHistoryClear(&tab->undo_history);
	editorHistoryClear(&tab->redo_history);
	editorSnapshotFree(&tab->edit_pending_snapshot);
	editorTabStateInitEmpty(tab);
}

static void editorFreeActiveBufferState(void) {
	for (int i = 0; i < E.numrows; i++) {
		free(E.rows[i].chars);
		free(E.rows[i].render);
	}
	free(E.rows);
	E.rows = NULL;
	E.numrows = 0;

	free(E.filename);
	E.filename = NULL;
	free(E.search_query);
	E.search_query = NULL;
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorSnapshotFree(&E.edit_pending_snapshot);
	editorResetActiveBufferFields();
}

static void editorTabStateCaptureActive(struct editorTabState *tab) {
	editorTabStateFree(tab);

	tab->cx = E.cx;
	tab->cy = E.cy;
	tab->rx = E.rx;
	tab->rowoff = E.rowoff;
	tab->coloff = E.coloff;
	tab->numrows = E.numrows;
	tab->rows = E.rows;
	tab->dirty = E.dirty;
	tab->filename = E.filename;
	tab->search_query = E.search_query;
	tab->search_match_row = E.search_match_row;
	tab->search_match_start = E.search_match_start;
	tab->search_match_len = E.search_match_len;
	tab->search_direction = E.search_direction;
	tab->search_saved_cx = E.search_saved_cx;
	tab->search_saved_cy = E.search_saved_cy;
	tab->selection_mode_active = E.selection_mode_active;
	tab->selection_anchor_cx = E.selection_anchor_cx;
	tab->selection_anchor_cy = E.selection_anchor_cy;
	tab->mouse_left_button_down = E.mouse_left_button_down;
	tab->mouse_drag_anchor_cx = E.mouse_drag_anchor_cx;
	tab->mouse_drag_anchor_cy = E.mouse_drag_anchor_cy;
	tab->mouse_drag_started = E.mouse_drag_started;
	tab->undo_history = E.undo_history;
	tab->redo_history = E.redo_history;
	tab->edit_pending_snapshot = E.edit_pending_snapshot;
	tab->edit_group_kind = E.edit_group_kind;
	tab->edit_pending_kind = E.edit_pending_kind;
	tab->edit_pending_mode = E.edit_pending_mode;

	editorResetActiveBufferFields();
}

static void editorTabStateLoadActive(struct editorTabState *tab) {
	E.cx = tab->cx;
	E.cy = tab->cy;
	E.rx = tab->rx;
	E.rowoff = tab->rowoff;
	E.coloff = tab->coloff;
	E.numrows = tab->numrows;
	E.rows = tab->rows;
	E.dirty = tab->dirty;
	E.filename = tab->filename;
	E.search_query = tab->search_query;
	E.search_match_row = tab->search_match_row;
	E.search_match_start = tab->search_match_start;
	E.search_match_len = tab->search_match_len;
	E.search_direction = tab->search_direction;
	E.search_saved_cx = tab->search_saved_cx;
	E.search_saved_cy = tab->search_saved_cy;
	E.selection_mode_active = tab->selection_mode_active;
	E.selection_anchor_cx = tab->selection_anchor_cx;
	E.selection_anchor_cy = tab->selection_anchor_cy;
	E.mouse_left_button_down = tab->mouse_left_button_down;
	E.mouse_drag_anchor_cx = tab->mouse_drag_anchor_cx;
	E.mouse_drag_anchor_cy = tab->mouse_drag_anchor_cy;
	E.mouse_drag_started = tab->mouse_drag_started;
	E.undo_history = tab->undo_history;
	E.redo_history = tab->redo_history;
	E.edit_pending_snapshot = tab->edit_pending_snapshot;
	E.edit_group_kind = tab->edit_group_kind;
	E.edit_pending_kind = tab->edit_pending_kind;
	E.edit_pending_mode = tab->edit_pending_mode;

	editorTabStateInitEmpty(tab);
}

static int editorEnsureTabCapacity(int needed) {
	if (needed <= E.tab_capacity) {
		return 1;
	}

	int new_capacity = E.tab_capacity > 0 ? E.tab_capacity : 4;
	while (new_capacity < needed) {
		if (new_capacity >= ROTIDE_MAX_TABS) {
			new_capacity = ROTIDE_MAX_TABS;
			break;
		}
		new_capacity *= 2;
		if (new_capacity > ROTIDE_MAX_TABS) {
			new_capacity = ROTIDE_MAX_TABS;
		}
	}
	if (new_capacity < needed) {
		return 0;
	}

	size_t cap_size = 0;
	size_t tabs_bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(struct editorTabState), cap_size, &tabs_bytes)) {
		return 0;
	}

	struct editorTabState *new_tabs = editorRealloc(E.tabs, tabs_bytes);
	if (new_tabs == NULL) {
		return 0;
	}

	for (int i = E.tab_capacity; i < new_capacity; i++) {
		editorTabStateInitEmpty(&new_tabs[i]);
	}

	E.tabs = new_tabs;
	E.tab_capacity = new_capacity;
	return 1;
}

static void editorStoreActiveTab(void) {
	if (E.tabs == NULL || E.tab_count <= 0 ||
			E.active_tab < 0 || E.active_tab >= E.tab_count) {
		return;
	}
	editorTabStateCaptureActive(&E.tabs[E.active_tab]);
}

static void editorLoadActiveTab(int tab_idx) {
	if (E.tabs == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		editorResetActiveBufferFields();
		return;
	}
	editorTabStateLoadActive(&E.tabs[tab_idx]);
}

int editorTabsInit(void) {
	editorTabsFreeAll();
	if (!editorEnsureTabCapacity(1)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	E.tab_count = 1;
	E.active_tab = 0;
	E.tab_view_start = 0;
	editorTabStateInitEmpty(&E.tabs[0]);
	editorLoadActiveTab(0);
	return 1;
}

void editorTabsFreeAll(void) {
	editorFreeActiveBufferState();

	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			editorTabStateFree(&E.tabs[i]);
		}
	}
	free(E.tabs);
	E.tabs = NULL;
	E.tab_count = 0;
	E.tab_capacity = 0;
	E.active_tab = 0;
	E.tab_view_start = 0;
}

int editorTabNewEmpty(void) {
	if (E.tab_count >= ROTIDE_MAX_TABS) {
		editorSetStatusMsg("Tab limit reached (%d)", ROTIDE_MAX_TABS);
		return 0;
	}
	if (E.tab_count == 0) {
		return editorTabsInit();
	}

	editorStoreActiveTab();
	int new_idx = E.tab_count;
	if (!editorEnsureTabCapacity(E.tab_count + 1)) {
		editorLoadActiveTab(E.active_tab);
		editorSetAllocFailureStatus();
		return 0;
	}

	editorTabStateInitEmpty(&E.tabs[new_idx]);
	E.tab_count++;
	E.active_tab = new_idx;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabOpenFileAsNew(const char *filename) {
	if (!editorTabNewEmpty()) {
		return 0;
	}
	editorOpen(filename);
	return 1;
}

static const char *editorTabPathAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

static int editorPathsReferToSameFile(const char *left, const char *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}

	struct stat left_st;
	struct stat right_st;
	if (stat(left, &left_st) == 0 && stat(right, &right_st) == 0) {
		return left_st.st_dev == right_st.st_dev && left_st.st_ino == right_st.st_ino;
	}

	return strcmp(left, right) == 0;
}

static int editorTabFindOpenFileIndex(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}

	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		const char *tab_path = editorTabPathAt(tab_idx);
		if (tab_path == NULL || tab_path[0] == '\0') {
			continue;
		}
		if (editorPathsReferToSameFile(path, tab_path)) {
			return tab_idx;
		}
	}

	return -1;
}

int editorTabSwitchToIndex(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return 1;
	}

	editorStoreActiveTab();
	E.active_tab = idx;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabSwitchByDelta(int delta) {
	if (E.tab_count <= 0) {
		return 0;
	}
	if (delta == 0 || E.tab_count == 1) {
		return 1;
	}

	int target = (E.active_tab + delta) % E.tab_count;
	if (target < 0) {
		target += E.tab_count;
	}
	return editorTabSwitchToIndex(target);
}

int editorTabCloseActive(void) {
	if (E.tab_count <= 0 || E.tabs == NULL) {
		return 0;
	}

	editorStoreActiveTab();
	int closing = E.active_tab;
	editorTabStateFree(&E.tabs[closing]);

	if (E.tab_count == 1) {
		editorTabStateInitEmpty(&E.tabs[0]);
		E.active_tab = 0;
		E.tab_count = 1;
		E.tab_view_start = 0;
		editorLoadActiveTab(0);
		return 1;
	}

	memmove(&E.tabs[closing], &E.tabs[closing + 1],
			sizeof(struct editorTabState) * (size_t)(E.tab_count - closing - 1));
	E.tab_count--;
	if (closing >= E.tab_count) {
		closing = E.tab_count - 1;
	}
	E.active_tab = closing;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabCount(void) {
	return E.tab_count;
}

int editorTabActiveIndex(void) {
	return E.active_tab;
}

int editorTabAnyDirty(void) {
	if (E.tab_count <= 0) {
		return E.dirty != 0;
	}
	if (E.dirty) {
		return 1;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (i == E.active_tab) {
			continue;
		}
		if (E.tabs[i].dirty) {
			return 1;
		}
	}
	return 0;
}

const char *editorTabFilenameAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

int editorTabDirtyAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return E.dirty != 0;
	}
	return E.tabs[idx].dirty != 0;
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

static int editorSanitizedTokenDisplayCols(const char *text, int text_len, int *src_len_out) {
	unsigned int cp = 0;
	int src_len = editorUtf8DecodeCodepoint(text, text_len, &cp);
	if (src_len <= 0) {
		src_len = 1;
	}
	if (src_len > text_len) {
		src_len = text_len;
	}
	if (src_len_out != NULL) {
		*src_len_out = src_len;
	}

	if (cp == '\t' || cp <= 0x1F || cp == 0x7F) {
		return 2;
	}
	if (cp >= 0x80 && cp <= 0x9F) {
		return 4;
	}
	return editorCharDisplayWidth(text, text_len);
}

static int editorSanitizedTextDisplayCols(const char *text, int max_cols) {
	if (text == NULL) {
		return 0;
	}

	int text_len = (int)strlen(text);
	int total_cols = 0;
	for (int idx = 0; idx < text_len;) {
		int src_len = 0;
		int token_cols = editorSanitizedTokenDisplayCols(&text[idx], text_len - idx, &src_len);
		if (max_cols >= 0 && total_cols + token_cols > max_cols) {
			break;
		}
		total_cols += token_cols;
		idx += src_len;
	}

	return total_cols;
}

static int editorTabLabelColsAt(int tab_idx) {
	const char *label = editorTabLabelFromFilename(editorTabFilenameAt(tab_idx));
	int cols = editorSanitizedTextDisplayCols(label, ROTIDE_TAB_TITLE_MAX_COLS);
	if (cols < 1) {
		cols = 1;
	}
	return cols;
}

static int editorTabWidthColsAt(int tab_idx) {
	return 3 + editorTabLabelColsAt(tab_idx);
}

static void editorTabVisibleRangeFromStart(int start_idx, int cols, int *last_idx_out) {
	int last_idx = start_idx - 1;
	if (E.tab_count <= 0 || cols <= 0 || start_idx < 0 || start_idx >= E.tab_count) {
		*last_idx_out = last_idx;
		return;
	}

	int used_cols = 0;
	for (int tab_idx = start_idx; tab_idx < E.tab_count; tab_idx++) {
		int width_cols = editorTabWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (tab_idx == start_idx && width_cols > cols) {
			width_cols = cols;
		}
		if (tab_idx > start_idx && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		used_cols += width_cols;
		last_idx = tab_idx;
		if (used_cols >= cols) {
			break;
		}
	}

	if (last_idx < start_idx && cols > 0) {
		last_idx = start_idx;
	}
	*last_idx_out = last_idx;
}

static void editorTabsAlignViewToActiveForWidth(int cols) {
	if (E.tab_count <= 0) {
		E.tab_view_start = 0;
		return;
	}
	if (E.active_tab < 0) {
		E.active_tab = 0;
	}
	if (E.active_tab >= E.tab_count) {
		E.active_tab = E.tab_count - 1;
	}

	if (E.tab_view_start < 0) {
		E.tab_view_start = 0;
	}
	if (E.tab_view_start >= E.tab_count) {
		E.tab_view_start = E.tab_count - 1;
	}

	if (cols <= 0) {
		if (E.active_tab < E.tab_view_start) {
			E.tab_view_start = E.active_tab;
		}
		return;
	}

	if (E.active_tab < E.tab_view_start) {
		E.tab_view_start = E.active_tab;
	}

	int last_visible = E.tab_view_start;
	editorTabVisibleRangeFromStart(E.tab_view_start, cols, &last_visible);
	while (E.active_tab > last_visible && E.tab_view_start < E.active_tab) {
		E.tab_view_start++;
		editorTabVisibleRangeFromStart(E.tab_view_start, cols, &last_visible);
	}
}

int editorTabBuildLayoutForWidth(int cols, struct editorTabLayoutEntry *entries, int max_entries,
		int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (E.tab_count <= 0 || cols <= 0 || max_entries == 0) {
		if (E.tab_count <= 0) {
			E.tab_view_start = 0;
		}
		return 1;
	}
	if (entries == NULL || max_entries < 0) {
		return 0;
	}

	editorTabsAlignViewToActiveForWidth(cols);
	int start_idx = E.tab_view_start;
	if (start_idx < 0) {
		start_idx = 0;
	}
	if (start_idx >= E.tab_count) {
		start_idx = E.tab_count - 1;
	}

	int used_cols = 0;
	int count = 0;
	for (int tab_idx = start_idx; tab_idx < E.tab_count && used_cols < cols; tab_idx++) {
		if (count >= max_entries) {
			break;
		}

		int width_cols = editorTabWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (count == 0 && width_cols > cols) {
			width_cols = cols;
		}
		if (count > 0 && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		struct editorTabLayoutEntry *entry = &entries[count];
		entry->tab_idx = tab_idx;
		entry->start_col = used_cols;
		entry->width_cols = width_cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;

		used_cols += width_cols;
		count++;
	}

	if (count == 0) {
		struct editorTabLayoutEntry *entry = &entries[0];
		entry->tab_idx = start_idx;
		entry->start_col = 0;
		entry->width_cols = cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;
		count = 1;
	}

	if (count > 0) {
		entries[0].show_left_overflow = entries[0].tab_idx > 0;
		entries[count - 1].show_right_overflow =
				entries[count - 1].tab_idx < E.tab_count - 1;
	}

	if (count_out != NULL) {
		*count_out = count;
	}
	return 1;
}

int editorTabHitTestColumn(int col, int cols) {
	if (col < 0 || col >= cols || E.tab_count <= 0 || cols <= 0) {
		return -1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForWidth(cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
		return -1;
	}
	for (int i = 0; i < layout_count; i++) {
		int start_col = layout[i].start_col;
		int end_col = start_col + layout[i].width_cols;
		if (col >= start_col && col < end_col) {
			return layout[i].tab_idx;
		}
	}
	return -1;
}

/*** Drawer ***/

struct editorDrawerNode {
	char *name;
	char *path;
	int is_dir;
	int is_expanded;
	int scanned;
	int scan_error;
	struct editorDrawerNode *parent;
	struct editorDrawerNode **children;
	int child_count;
};

struct editorDrawerLookup {
	struct editorDrawerNode *node;
	int depth;
	int visible_idx;
	int parent_visible_idx;
};

static char *editorDrawerPathJoin(const char *left, const char *right) {
	size_t left_len = strlen(left);
	while (left_len > 1 && left[left_len - 1] == '/') {
		left_len--;
	}

	while (right[0] == '/' && right[1] != '\0') {
		right++;
	}
	size_t right_len = strlen(right);

	int need_slash = 1;
	if (left_len == 0 || (left_len == 1 && left[0] == '/')) {
		need_slash = 0;
	}

	size_t total = 0;
	if (!editorSizeAdd(left_len, right_len, &total) ||
			(need_slash && !editorSizeAdd(total, 1, &total)) ||
			!editorSizeAdd(total, 1, &total)) {
		return NULL;
	}

	char *path = editorMalloc(total);
	if (path == NULL) {
		return NULL;
	}

	size_t write_idx = 0;
	if (left_len > 0) {
		memcpy(path, left, left_len);
		write_idx += left_len;
	}
	if (need_slash) {
		path[write_idx++] = '/';
	}
	if (right_len > 0) {
		memcpy(path + write_idx, right, right_len);
		write_idx += right_len;
	}
	path[write_idx] = '\0';
	return path;
}

static char *editorDrawerBasenameDup(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return strdup(".");
	}

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}
	if (len == 1 && path[0] == '/') {
		return strdup("/");
	}

	size_t start = len;
	while (start > 0 && path[start - 1] != '/') {
		start--;
	}

	size_t name_len = len - start;
	char *name = editorMalloc(name_len + 1);
	if (name == NULL) {
		return NULL;
	}
	memcpy(name, path + start, name_len);
	name[name_len] = '\0';
	return name;
}

static char *editorDrawerDirnameDup(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return strdup(".");
	}

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}

	size_t slash = len;
	while (slash > 0 && path[slash - 1] != '/') {
		slash--;
	}
	if (slash == 0) {
		return strdup(".");
	}
	if (slash == 1) {
		return strdup("/");
	}

	size_t dir_len = slash - 1;
	char *dir = editorMalloc(dir_len + 1);
	if (dir == NULL) {
		return NULL;
	}
	memcpy(dir, path, dir_len);
	dir[dir_len] = '\0';
	return dir;
}

static char *editorDrawerGetCwd(void) {
	char *cwd = getcwd(NULL, 0);
	if (cwd != NULL) {
		return cwd;
	}

	cwd = strdup(".");
	if (cwd == NULL) {
		editorSetAllocFailureStatus();
	}
	return cwd;
}

static char *editorDrawerResolveRootPathForStartup(int argc, char *argv[], int restored_session) {
	char *cwd = editorDrawerGetCwd();
	if (cwd == NULL) {
		return NULL;
	}

	if (restored_session || argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
		return cwd;
	}

	char *absolute = NULL;
	if (argv[1][0] == '/') {
		absolute = strdup(argv[1]);
	} else {
		absolute = editorDrawerPathJoin(cwd, argv[1]);
	}
	free(cwd);
	if (absolute == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	char *dir = editorDrawerDirnameDup(absolute);
	free(absolute);
	if (dir == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	char *resolved = realpath(dir, NULL);
	if (resolved != NULL) {
		free(dir);
		return resolved;
	}

	return dir;
}

static struct editorDrawerNode *editorDrawerNodeNew(const char *name, const char *path, int is_dir,
		struct editorDrawerNode *parent) {
	struct editorDrawerNode *node = editorMalloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}

	node->name = strdup(name);
	node->path = strdup(path);
	if (node->name == NULL || node->path == NULL) {
		free(node->name);
		free(node->path);
		free(node);
		return NULL;
	}

	node->is_dir = is_dir;
	node->is_expanded = 0;
	node->scanned = 0;
	node->scan_error = 0;
	node->parent = parent;
	node->children = NULL;
	node->child_count = 0;
	return node;
}

static void editorDrawerNodeFree(struct editorDrawerNode *node) {
	if (node == NULL) {
		return;
	}

	for (int i = 0; i < node->child_count; i++) {
		editorDrawerNodeFree(node->children[i]);
	}
	free(node->children);
	free(node->name);
	free(node->path);
	free(node);
}

static int editorDrawerNodeCmp(const void *a, const void *b) {
	const struct editorDrawerNode *left = *(const struct editorDrawerNode * const *)a;
	const struct editorDrawerNode *right = *(const struct editorDrawerNode * const *)b;

	if (left->is_dir != right->is_dir) {
		return right->is_dir - left->is_dir;
	}

	int ci_cmp = strcasecmp(left->name, right->name);
	if (ci_cmp != 0) {
		return ci_cmp;
	}
	return strcmp(left->name, right->name);
}

static int editorDrawerEnsureScanned(struct editorDrawerNode *node) {
	if (node == NULL || !node->is_dir || node->scanned) {
		return 1;
	}

	DIR *dir = opendir(node->path);
	if (dir == NULL) {
		node->scanned = 1;
		node->scan_error = 1;
		editorSetStatusMsg("Drawer scan failed: %s", strerror(errno));
		return 0;
	}

	struct editorDrawerNode **children = NULL;
	int child_count = 0;
	int child_capacity = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
			continue;
		}

		char *child_path = editorDrawerPathJoin(node->path, entry->d_name);
		if (child_path == NULL) {
			editorSetAllocFailureStatus();
			break;
		}

		struct stat st;
		int is_dir = 0;
		if (lstat(child_path, &st) == 0) {
			is_dir = S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
		}

		struct editorDrawerNode *child = editorDrawerNodeNew(entry->d_name, child_path, is_dir, node);
		free(child_path);
		if (child == NULL) {
			editorSetAllocFailureStatus();
			break;
		}

		if (child_count >= child_capacity) {
			int new_capacity = child_capacity > 0 ? child_capacity * 2 : 8;
			size_t cap_size = 0;
			size_t bytes = 0;
			if (!editorIntToSize(new_capacity, &cap_size) ||
					!editorSizeMul(sizeof(*children), cap_size, &bytes)) {
				editorDrawerNodeFree(child);
				editorSetAllocFailureStatus();
				break;
			}

			struct editorDrawerNode **grown = editorRealloc(children, bytes);
			if (grown == NULL) {
				editorDrawerNodeFree(child);
				editorSetAllocFailureStatus();
				break;
			}
			children = grown;
			child_capacity = new_capacity;
		}

		children[child_count++] = child;
	}

	(void)closedir(dir);

	if (child_count > 1) {
		qsort(children, (size_t)child_count, sizeof(*children), editorDrawerNodeCmp);
	}

	node->children = children;
	node->child_count = child_count;
	node->scanned = 1;
	return 1;
}

static int editorDrawerCountVisibleFromNode(struct editorDrawerNode *node) {
	if (node == NULL) {
		return 0;
	}

	int count = 1;
	if (!node->is_dir || !node->is_expanded) {
		return count;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		count += editorDrawerCountVisibleFromNode(node->children[i]);
	}
	return count;
}

static int editorDrawerLookupByVisibleIndexRecursive(struct editorDrawerNode *node, int depth,
		int parent_visible_idx, int *cursor, int target_visible_idx,
		struct editorDrawerLookup *lookup_out) {
	if (node == NULL || cursor == NULL || lookup_out == NULL) {
		return 0;
	}

	int current = *cursor;
	if (current == target_visible_idx) {
		lookup_out->node = node;
		lookup_out->depth = depth;
		lookup_out->visible_idx = current;
		lookup_out->parent_visible_idx = parent_visible_idx;
		return 1;
	}

	(*cursor)++;
	if (!node->is_dir || !node->is_expanded) {
		return 0;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		if (editorDrawerLookupByVisibleIndexRecursive(node->children[i], depth + 1, current, cursor,
					target_visible_idx, lookup_out)) {
			return 1;
		}
	}
	return 0;
}

static int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out) {
	if (lookup_out == NULL || E.drawer_root == NULL || visible_idx < 0) {
		return 0;
	}

	int cursor = 0;
	return editorDrawerLookupByVisibleIndexRecursive(E.drawer_root, 0, -1, &cursor, visible_idx,
			lookup_out);
}

static void editorDrawerClampSelectionAndScroll(int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		E.drawer_selected_index = 0;
		E.drawer_rowoff = 0;
		return;
	}

	if (E.drawer_selected_index < 0) {
		E.drawer_selected_index = 0;
	}
	if (E.drawer_selected_index >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}
	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	if (E.drawer_rowoff > max_rowoff) {
		E.drawer_rowoff = max_rowoff;
	}
	if (E.drawer_rowoff < 0) {
		E.drawer_rowoff = 0;
	}

	if (E.drawer_selected_index < E.drawer_rowoff) {
		E.drawer_rowoff = E.drawer_selected_index;
	}
	if (E.drawer_selected_index >= E.drawer_rowoff + viewport_rows) {
		E.drawer_rowoff = E.drawer_selected_index - viewport_rows + 1;
	}

	if (E.drawer_rowoff > max_rowoff) {
		E.drawer_rowoff = max_rowoff;
	}
	if (E.drawer_rowoff < 0) {
		E.drawer_rowoff = 0;
	}
}

static int editorDrawerClampWidthForCols(int desired_width, int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	if (desired_width < 1) {
		desired_width = 1;
	}

	int max_drawer = total_cols - 2;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	if (desired_width > max_drawer) {
		desired_width = max_drawer;
	}

	return desired_width;
}

static int editorDrawerDefaultMaxWidthForCols(int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	int min_text_cols = total_cols / 2;
	if (min_text_cols < 1) {
		min_text_cols = 1;
	}
	int max_drawer = total_cols - 1 - min_text_cols;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	return max_drawer;
}

int editorDrawerWidthForCols(int total_cols) {
	int desired_width = E.drawer_width_cols;
	if (desired_width <= 0) {
		desired_width = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}

	int width = editorDrawerClampWidthForCols(desired_width, total_cols);
	if (!E.drawer_width_user_set) {
		int default_max = editorDrawerDefaultMaxWidthForCols(total_cols);
		if (width > default_max) {
			width = default_max;
		}
	}
	return width;
}

int editorDrawerSeparatorWidthForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	if (drawer_cols <= 0) {
		return 0;
	}
	return total_cols - drawer_cols >= 2 ? 1 : 0;
}

int editorDrawerTextStartColForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(total_cols);
	return drawer_cols + separator_cols;
}

int editorDrawerTextViewportCols(int total_cols) {
	if (total_cols <= 1) {
		return 1;
	}
	int text_cols = total_cols - editorDrawerTextStartColForCols(total_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}
	return text_cols;
}

int editorDrawerSetWidthForCols(int width, int total_cols) {
	int clamped = editorDrawerClampWidthForCols(width, total_cols);
	E.drawer_width_user_set = 1;
	if (E.drawer_width_cols == clamped) {
		return 0;
	}
	E.drawer_width_cols = clamped;
	return 1;
}

int editorDrawerResizeByDeltaForCols(int delta, int total_cols) {
	int current = editorDrawerWidthForCols(total_cols);
	return editorDrawerSetWidthForCols(current + delta, total_cols);
}

int editorDrawerVisibleCount(void) {
	return editorDrawerCountVisibleFromNode(E.drawer_root);
}

int editorDrawerGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}

	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(visible_idx, &lookup)) {
		return 0;
	}

	view_out->name = lookup.node->name;
	view_out->depth = lookup.depth;
	view_out->is_dir = lookup.node->is_dir;
	view_out->is_expanded = lookup.node->is_expanded;
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->has_scan_error = lookup.node->scan_error;
	view_out->is_root = lookup.node == E.drawer_root;
	return 1;
}

int editorDrawerMoveSelectionBy(int delta, int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		return 0;
	}

	if (delta < 0 && E.drawer_selected_index + delta < 0) {
		E.drawer_selected_index = 0;
	} else if (delta > 0 && E.drawer_selected_index + delta >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	} else {
		E.drawer_selected_index += delta;
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerExpandSelection(int viewport_rows) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}

	lookup.node->is_expanded = 1;
	(void)editorDrawerEnsureScanned(lookup.node);
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerCollapseSelection(int viewport_rows) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}

	if (lookup.node->is_dir && lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	if (lookup.parent_visible_idx >= 0) {
		E.drawer_selected_index = lookup.parent_visible_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	return 0;
}

int editorDrawerToggleSelectionExpanded(int viewport_rows) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}

	if (lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
	} else {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectVisibleIndex(int visible_idx, int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_idx < 0 || visible_idx >= visible_count) {
		return 0;
	}

	E.drawer_selected_index = visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectedIsDirectory(void) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node->is_dir;
}

int editorDrawerOpenSelectedFileInTab(void) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.node->is_dir || lookup.node->path == NULL || lookup.node->path[0] == '\0') {
		return 0;
	}

	int existing_tab = editorTabFindOpenFileIndex(lookup.node->path);
	if (existing_tab >= 0) {
		return editorTabSwitchToIndex(existing_tab);
	}

	return editorTabOpenFileAsNew(lookup.node->path);
}

const char *editorDrawerRootPath(void) {
	return E.drawer_root_path;
}

void editorDrawerShutdown(void) {
	editorDrawerNodeFree(E.drawer_root);
	E.drawer_root = NULL;
	free(E.drawer_root_path);
	E.drawer_root_path = NULL;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	E.drawer_resize_active = 0;
	E.pane_focus = EDITOR_PANE_TEXT;
}

int editorDrawerInitForStartup(int argc, char *argv[], int restored_session) {
	editorDrawerShutdown();

	char *root_path = editorDrawerResolveRootPathForStartup(argc, argv, restored_session);
	if (root_path == NULL) {
		return 0;
	}

	char *root_name = editorDrawerBasenameDup(root_path);
	if (root_name == NULL) {
		free(root_path);
		editorSetAllocFailureStatus();
		return 0;
	}

	struct editorDrawerNode *root = editorDrawerNodeNew(root_name, root_path, 1, NULL);
	free(root_name);
	if (root == NULL) {
		free(root_path);
		editorSetAllocFailureStatus();
		return 0;
	}
	root->is_expanded = 1;

	E.drawer_root_path = root_path;
	E.drawer_root = root;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	if (E.drawer_width_cols <= 0) {
		E.drawer_width_cols = ROTIDE_DRAWER_DEFAULT_WIDTH;
		E.drawer_width_user_set = 0;
	}
	E.drawer_resize_active = 0;
	E.pane_focus = EDITOR_PANE_TEXT;
	editorDrawerClampSelectionAndScroll(E.window_rows + 1);
	return 1;
}

void editorHistoryReset(void) {
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorSnapshotFree(&E.edit_pending_snapshot);
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
	editorClearSelectionState();
}

void editorHistoryBreakGroup(void) {
	E.edit_group_kind = EDITOR_EDIT_NONE;
}

void editorHistoryDiscardEdit(void);

void editorHistoryBeginEdit(enum editorEditKind kind) {
	editorHistoryDiscardEdit();
	E.edit_pending_kind = kind;

	if (kind != EDITOR_EDIT_INSERT_TEXT) {
		E.edit_group_kind = EDITOR_EDIT_NONE;
	}

	if (kind == EDITOR_EDIT_INSERT_TEXT && E.edit_group_kind == EDITOR_EDIT_INSERT_TEXT) {
		E.edit_pending_mode = EDITOR_EDIT_PENDING_GROUPED;
		return;
	}

	if (!editorSnapshotCaptureCurrent(&E.edit_pending_snapshot)) {
		E.edit_pending_mode = EDITOR_EDIT_PENDING_SKIPPED;
		E.edit_group_kind = EDITOR_EDIT_NONE;
		return;
	}

	E.edit_pending_mode = EDITOR_EDIT_PENDING_CAPTURED;
}

void editorHistoryCommitEdit(enum editorEditKind kind, int changed) {
	(void)kind;

	enum editorEditPendingMode mode = E.edit_pending_mode;
	if (!changed) {
		editorHistoryDiscardEdit();
		E.edit_group_kind = EDITOR_EDIT_NONE;
		return;
	}

	editorHistoryClear(&E.redo_history);

	if (mode == EDITOR_EDIT_PENDING_CAPTURED) {
		editorHistoryPushNewest(&E.undo_history, &E.edit_pending_snapshot);
	}

	if (E.edit_pending_kind == EDITOR_EDIT_INSERT_TEXT &&
			mode != EDITOR_EDIT_PENDING_SKIPPED &&
			mode != EDITOR_EDIT_PENDING_NONE) {
		E.edit_group_kind = EDITOR_EDIT_INSERT_TEXT;
	} else {
		E.edit_group_kind = EDITOR_EDIT_NONE;
	}

	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

void editorHistoryDiscardEdit(void) {
	editorSnapshotFree(&E.edit_pending_snapshot);
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

int editorUndo(void) {
	editorHistoryBreakGroup();
	editorHistoryDiscardEdit();

	struct editorSnapshot target = {0};
	if (!editorHistoryPopNewest(&E.undo_history, &target)) {
		editorSetStatusMsg("Nothing to undo");
		return 0;
	}

	struct editorSnapshot current = {0};
	if (!editorSnapshotCaptureCurrent(&current)) {
		editorHistoryPushNewest(&E.undo_history, &target);
		return -1;
	}

	if (!editorSnapshotRestore(&target)) {
		editorSnapshotFree(&current);
		editorHistoryPushNewest(&E.undo_history, &target);
		return -1;
	}

	editorHistoryPushNewest(&E.redo_history, &current);
	editorSnapshotFree(&target);
	return 1;
}

int editorRedo(void) {
	editorHistoryBreakGroup();
	editorHistoryDiscardEdit();

	struct editorSnapshot target = {0};
	if (!editorHistoryPopNewest(&E.redo_history, &target)) {
		editorSetStatusMsg("Nothing to redo");
		return 0;
	}

	struct editorSnapshot current = {0};
	if (!editorSnapshotCaptureCurrent(&current)) {
		editorHistoryPushNewest(&E.redo_history, &target);
		return -1;
	}

	if (!editorSnapshotRestore(&target)) {
		editorSnapshotFree(&current);
		editorHistoryPushNewest(&E.redo_history, &target);
		return -1;
	}

	editorHistoryPushNewest(&E.undo_history, &current);
	editorSnapshotFree(&target);
	return 1;
}

static int editorRebuildRowRender(struct erow *row) {
	char *new_render = NULL;
	int new_rsize = 0;
	if (!editorBuildRender(row->chars, row->size, &new_render, &new_rsize)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	free(row->render);
	row->render = new_render;
	row->rsize = new_rsize;
	return 1;
}

void editorUpdateRow(struct erow *row) {
	(void)editorRebuildRowRender(row);
}

void editorInsertRow(int idx, const char *s, size_t len) {
	if (idx < 0 || idx > E.numrows) {
		return;
	}
	if (len > ROTIDE_MAX_TEXT_BYTES || !editorSizeWithinInt(len)) {
		editorSetOperationTooLargeStatus();
		return;
	}

	size_t row_cap = 0;
	size_t numrows_size = 0;
	size_t new_numrows = 0;
	size_t rows_bytes = 0;
	if (!editorSizeAdd(len, 1, &row_cap) ||
			!editorIntToSize(E.numrows, &numrows_size) ||
			!editorSizeAdd(numrows_size, 1, &new_numrows) ||
			!editorSizeMul(sizeof(struct erow), new_numrows, &rows_bytes)) {
		editorSetOperationTooLargeStatus();
		return;
	}

	char *row_chars = editorMalloc(row_cap);
	if (row_chars == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	memcpy(row_chars, s, len);
	row_chars[len] = '\0';

	struct erow *new_rows = editorRealloc(E.rows, rows_bytes);
	if (new_rows == NULL) {
		free(row_chars);
		editorSetAllocFailureStatus();
		return;
	}

	E.rows = new_rows;
	memmove(&E.rows[idx + 1], &E.rows[idx], sizeof(struct erow) * (E.numrows - idx));

	E.rows[idx].size = (int)len;
	E.rows[idx].chars = row_chars;
	E.rows[idx].rsize = 0;
	E.rows[idx].render = NULL;
	if (!editorRebuildRowRender(&E.rows[idx])) {
		free(E.rows[idx].chars);
		memmove(&E.rows[idx], &E.rows[idx + 1], sizeof(struct erow) * (E.numrows - idx));
		if (E.numrows > 0) {
			struct erow *shrunk = editorRealloc(E.rows, sizeof(struct erow) * E.numrows);
			if (shrunk != NULL) {
				E.rows = shrunk;
			}
		}
		return;
	}

	E.numrows++;
	E.dirty++;
}

void editorDeleteRow(int idx) {
	if (idx < 0 || idx >= E.numrows) {
		return;
	}
	struct erow *row = &E.rows[idx];
	free(row->render);
	free(row->chars);

	memmove(row, &E.rows[idx + 1], sizeof(struct erow) * (E.numrows - idx - 1));
	E.numrows--;
	E.dirty++;
}

void editorInsertCharAt(struct erow *row, int idx, int c) {
	if (idx < 0 || row->size < idx) {
		idx = row->size;
	}

	size_t row_size = 0;
	size_t next_size = 0;
	size_t next_cap = 0;
	if (!editorIntToSize(row->size, &row_size) ||
			!editorSizeAdd(row_size, 1, &next_size) ||
			next_size > ROTIDE_MAX_TEXT_BYTES ||
			!editorSizeAdd(next_size, 1, &next_cap)) {
		editorSetOperationTooLargeStatus();
		return;
	}

	char *new_chars = editorRealloc(row->chars, next_cap);
	if (new_chars == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	row->chars = new_chars;
	memmove(&row->chars[idx + 1], &row->chars[idx], row->size - idx + 1);
	row->size++;
	row->chars[idx] = (char)c;
	if (!editorRebuildRowRender(row)) {
		memmove(&row->chars[idx], &row->chars[idx + 1], row->size - idx);
		row->size--;
		row->chars[row->size] = '\0';
		return;
	}
	E.dirty++;
}

void editorRowAppendString(struct erow *row, const char *s, size_t len) {
	int old_size = row->size;
	size_t row_size = 0;
	size_t new_size = 0;
	size_t new_cap = 0;
	if (!editorIntToSize(row->size, &row_size) ||
			!editorSizeAdd(row_size, len, &new_size) ||
			new_size > ROTIDE_MAX_TEXT_BYTES ||
			!editorSizeAdd(new_size, 1, &new_cap) ||
			!editorSizeWithinInt(new_size)) {
		editorSetOperationTooLargeStatus();
		return;
	}

	char *new_chars = editorRealloc(row->chars, new_cap);
	if (new_chars == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	row->chars = new_chars;
	memcpy(&row->chars[row->size], s, len);
	row->size = (int)new_size;
	row->chars[row->size] = '\0';
	if (!editorRebuildRowRender(row)) {
		row->size = old_size;
		row->chars[row->size] = '\0';
		return;
	}
	E.dirty++;
}

void editorDelCharAt(struct erow *row, int idx) {
	if (idx < 0 || row->size <= idx) {
		return;
	}

	char deleted = row->chars[idx];
	memmove(&row->chars[idx], &row->chars[idx + 1], row->size - idx);
	row->size--;
	if (!editorRebuildRowRender(row)) {
		memmove(&row->chars[idx + 1], &row->chars[idx], row->size - idx + 1);
		row->chars[idx] = deleted;
		row->size++;
		return;
	}
	E.dirty++;
}

void editorDelCharsAt(struct erow *row, int idx, int len) {
	if (idx < 0 || len <= 0 || idx > row->size || len > row->size - idx) {
		return;
	}

	char *removed = editorMalloc((size_t)len);
	if (removed == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	memcpy(removed, &row->chars[idx], (size_t)len);

	memmove(&row->chars[idx], &row->chars[idx + len], row->size - idx - len + 1);
	row->size -= len;
	if (!editorRebuildRowRender(row)) {
		memmove(&row->chars[idx + len], &row->chars[idx], row->size - idx + 1);
		memcpy(&row->chars[idx], removed, (size_t)len);
		row->size += len;
		free(removed);
		return;
	}
	free(removed);
	E.dirty++;
}

void editorInsertChar(int c) {
	if (E.cy == E.numrows) {
		int prev_numrows = E.numrows;
		editorInsertRow(E.numrows, "", 0);
		if (E.numrows == prev_numrows) {
			return;
		}
	}

	int dirty_before = E.dirty;
	editorInsertCharAt(&E.rows[E.cy], E.cx, c);
	if (E.dirty != dirty_before) {
		E.cx++;
	}
}

void editorInsertNewline(void) {
	if (E.cx == 0) {
		int prev_numrows = E.numrows;
		editorInsertRow(E.cy, "", 0);
		if (E.numrows != prev_numrows) {
			E.cy++;
		}
		return;
	}

	struct erow *row = &E.rows[E.cy];
	int split_idx = editorRowClampCxToClusterBoundary(row, E.cx);
	char *prefix_render = NULL;
	int prefix_rsize = 0;
	if (!editorBuildRender(row->chars, split_idx, &prefix_render, &prefix_rsize)) {
		editorSetAllocFailureStatus();
		return;
	}

	int prev_numrows = E.numrows;
	editorInsertRow(E.cy + 1, &row->chars[split_idx], (size_t)(row->size - split_idx));
	if (E.numrows == prev_numrows) {
		free(prefix_render);
		return;
	}

	row = &E.rows[E.cy];
	row->size = split_idx;
	row->chars[row->size] = '\0';
	free(row->render);
	row->render = prefix_render;
	row->rsize = prefix_rsize;
	E.cy++;
	E.cx = 0;
}

void editorDelChar(void) {
	if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
		return;
	}
	struct erow *row = &E.rows[E.cy];

	if (E.cx > 0) {
		int prev_cx = editorRowPrevClusterIdx(row, E.cx);
		int dirty_before = E.dirty;
		editorDelCharsAt(row, prev_cx, E.cx - prev_cx);
		if (E.dirty != dirty_before) {
			E.cx = prev_cx;
		}
		return;
	}

	int old_cx = E.cx;
	E.cx = E.rows[E.cy - 1].size;
	int dirty_before = E.dirty;
	editorRowAppendString(&E.rows[E.cy - 1], row->chars, row->size);
	if (E.dirty == dirty_before) {
		E.cx = old_cx;
		return;
	}
	editorDeleteRow(E.cy);
	E.cy--;
}

void editorOpen(const char *filename) {
	editorHistoryReset();
	editorClearSelectionState();
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		panic("fopen");
	}

	char *l = NULL;
	size_t lcap = 0;
	ssize_t llen;
	size_t total_bytes = 0;
	while ((llen = getline(&l, &lcap, fp)) != -1) {
		size_t line_len = 0;
		if (!editorSsizeToSize(llen, &line_len)) {
			editorSetFileTooLargeStatus();
			break;
		}

		while (line_len > 0 && (l[line_len - 1] == '\n' || l[line_len - 1] == '\r')) {
			line_len--;
		}

		size_t row_total = 0;
		size_t next_total = 0;
		if (!editorSizeAdd(line_len, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(total_bytes, row_total, &next_total) ||
				next_total > ROTIDE_MAX_TEXT_BYTES) {
			editorSetFileTooLargeStatus();
			break;
		}

		int prev_numrows = E.numrows;
		editorInsertRow(E.numrows, l, line_len);
		if (E.numrows == prev_numrows) {
			break;
		}
		total_bytes = next_total;
	}

	free(l);
	fclose(fp);
	E.dirty = 0;
}

void editorSetStatusMsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static int editorWriteAll(int fd, const char *buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (written == 0) {
			errno = EIO;
			return -1;
		}
		total += (size_t)written;
	}
	return 0;
}

static char *editorTempPathForTarget(const char *target) {
	const char *slash = strrchr(target, '/');
	const char *basename = target;
	size_t dir_len = 0;
	static const char suffix[] = ".rotide-tmp-XXXXXX";

	if (slash != NULL) {
		basename = slash + 1;
		dir_len = (size_t)(slash - target + 1);
	}

	size_t base_len = strlen(basename);
	size_t total_len = 0;
	if (!editorSizeAdd(dir_len, base_len, &total_len) ||
			!editorSizeAdd(total_len, sizeof(suffix), &total_len)) {
		return NULL;
	}
	char *tmp_path = editorMalloc(total_len);
	if (tmp_path == NULL) {
		return NULL;
	}

	if (dir_len > 0) {
		memcpy(tmp_path, target, dir_len);
	}
	memcpy(tmp_path + dir_len, basename, base_len);
	memcpy(tmp_path + dir_len + base_len, suffix, sizeof(suffix));

	return tmp_path;
}

static int editorOpenParentDirForTarget(const char *target) {
	const char *slash = strrchr(target, '/');
	if (slash == NULL) {
		return editorSaveOpenDir(".");
	}
	if (slash == target) {
		return editorSaveOpenDir("/");
	}

	size_t dir_len = (size_t)(slash - target);
	size_t dir_cap = 0;
	if (!editorSizeAdd(dir_len, 1, &dir_cap)) {
		errno = ENOMEM;
		return -1;
	}
	char *dir_path = editorMalloc(dir_cap);
	if (dir_path == NULL) {
		errno = ENOMEM;
		return -1;
	}

	memcpy(dir_path, target, dir_len);
	dir_path[dir_len] = '\0';
	int dir_fd = editorSaveOpenDir(dir_path);
	free(dir_path);
	return dir_fd;
}

static int editorSaveCleanupOnError(int *fd, int *dir_fd, const char *tmp_path,
		int tmp_created, int tmp_renamed, int *cleanup_errno) {
	int first_cleanup_errno = 0;

	if (*fd != -1) {
		if (editorSaveClose(*fd) == -1 && first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
		*fd = -1;
	}
	if (*dir_fd != -1) {
		if (editorSaveClose(*dir_fd) == -1 && first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
		*dir_fd = -1;
	}

	if (tmp_path != NULL && tmp_created && !tmp_renamed) {
		if (editorSaveUnlink(tmp_path) == -1 && errno != ENOENT &&
				first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
	}

	if (cleanup_errno != NULL) {
		*cleanup_errno = first_cleanup_errno;
	}
	return first_cleanup_errno == 0 ? 0 : -1;
}

static const char *editorSaveFailureClass(int errnum) {
	switch (errnum) {
		case EACCES:
		case EPERM:
			return "permission denied";
		case ENOENT:
		case ENOTDIR:
			return "missing path";
		case EROFS:
			return "read-only filesystem";
		case ENOSPC:
#ifdef EDQUOT
		case EDQUOT:
#endif
			return "no space left";
		default:
			return "system error";
	}
}

static void editorSetSaveFailureStatus(int saved_errno, int cleanup_errno) {
	const char *error_class = editorSaveFailureClass(saved_errno);
	const char *error_text = strerror(saved_errno);

	if (cleanup_errno != 0) {
		editorSetStatusMsg("Save failed: %s (%s); cleanup failed (%s)", error_class,
				error_text, strerror(cleanup_errno));
		return;
	}

	editorSetStatusMsg("Save failed: %s (%s)", error_class, error_text);
}

static mode_t editorDefaultCreateMode(void) {
	mode_t mask = umask(0);
	umask(mask);
	return 0644 & ~mask;
}

void editorSave(void) {
	if (E.filename == NULL) {
		if ((E.filename = editorPrompt("Save as: %s")) == NULL) {
			if (E.statusmsg[0] == '\0') {
				editorSetStatusMsg("Save aborted");
			}
			return;
		}
	}

	size_t len = 0;
	errno = 0;
	char *buf = editorRowsToStr(&len);
	char *tmp_path = editorTempPathForTarget(E.filename);
	int fd = -1;
	int dir_fd = -1;
	int tmp_created = 0;
	int tmp_renamed = 0;
	mode_t mode = editorDefaultCreateMode();
	struct stat st;

	if (buf == NULL && (len > 0 || errno != 0)) {
		free(tmp_path);
		if (errno == EOVERFLOW) {
			editorSetFileTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return;
	}

	if (stat(E.filename, &st) == 0) {
		mode = st.st_mode & 0777;
	}

	if (tmp_path == NULL) {
		free(buf);
		editorSetAllocFailureStatus();
		return;
	}

	// Write to a sibling temp file and atomically replace the target on success.
	// This avoids leaving a partially written file if the save path fails midway.
	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, mode) == -1) {
		goto err;
	}
	if (editorWriteAll(fd, buf, len) == -1) {
		goto err;
	}
	if (editorSaveFsync(fd) == -1) {
		goto err;
	}
	if (editorSaveClose(fd) == -1) {
		fd = -1;
		goto err;
	}
	fd = -1;
	if (editorSaveRename(tmp_path, E.filename) == -1) {
		goto err;
	}
	tmp_renamed = 1;

	dir_fd = editorOpenParentDirForTarget(E.filename);
	if (dir_fd == -1) {
		goto err;
	}
	if (editorSaveFsync(dir_fd) == -1) {
		goto err;
	}
	if (editorSaveClose(dir_fd) == -1) {
		dir_fd = -1;
		goto err;
	}
	dir_fd = -1;

	E.dirty = 0;
	free(tmp_path);
	free(buf);
	editorSetStatusMsg("%zu bytes written to disk", len);
	return;

err: {
	int saved_errno = errno;
	int cleanup_errno = 0;
	(void)editorSaveCleanupOnError(&fd, &dir_fd, tmp_path, tmp_created, tmp_renamed,
			&cleanup_errno);
	free(tmp_path);
	free(buf);
	editorSetSaveFailureStatus(saved_errno, cleanup_errno);
}
}

/*** Recovery ***/

#define ROTIDE_RECOVERY_MAGIC "RTRECOV1"
#define ROTIDE_RECOVERY_MAGIC_LEN 8
#define ROTIDE_RECOVERY_VERSION 1U
#define ROTIDE_RECOVERY_AUTOSAVE_DEBOUNCE_SECONDS 5
#define ROTIDE_RECOVERY_MAX_FILENAME_BYTES 4096

struct editorRecoveryRow {
	char *chars;
	size_t len;
};

struct editorRecoveryTab {
	int cx;
	int cy;
	int rowoff;
	int coloff;
	char *filename;
	int row_count;
	struct editorRecoveryRow *rows;
};

struct editorRecoverySession {
	int tab_count;
	int active_tab;
	struct editorRecoveryTab *tabs;
};

struct editorRecoveryTabView {
	int cx;
	int cy;
	int rowoff;
	int coloff;
	int numrows;
	struct erow *rows;
	const char *filename;
};

enum editorRecoveryLoadStatus {
	EDITOR_RECOVERY_LOAD_OK = 0,
	EDITOR_RECOVERY_LOAD_NOT_FOUND,
	EDITOR_RECOVERY_LOAD_INVALID,
	EDITOR_RECOVERY_LOAD_OOM,
	EDITOR_RECOVERY_LOAD_IO
};

enum editorReadExactStatus {
	EDITOR_READ_EXACT_OK = 1,
	EDITOR_READ_EXACT_EOF = 0,
	EDITOR_READ_EXACT_ERR = -1
};

static void editorRecoverySessionFree(struct editorRecoverySession *session) {
	if (session == NULL || session->tabs == NULL) {
		return;
	}

	for (int tab_idx = 0; tab_idx < session->tab_count; tab_idx++) {
		struct editorRecoveryTab *tab = &session->tabs[tab_idx];
		for (int row_idx = 0; row_idx < tab->row_count; row_idx++) {
			free(tab->rows[row_idx].chars);
			tab->rows[row_idx].chars = NULL;
			tab->rows[row_idx].len = 0;
		}
		free(tab->rows);
		tab->rows = NULL;
		tab->row_count = 0;
		free(tab->filename);
		tab->filename = NULL;
	}

	free(session->tabs);
	session->tabs = NULL;
	session->tab_count = 0;
	session->active_tab = 0;
}

static uint64_t editorRecoveryHashPath(const char *path) {
	uint64_t hash = UINT64_C(1469598103934665603);
	const unsigned char *p = (const unsigned char *)path;
	while (*p != '\0') {
		hash ^= (uint64_t)*p;
		hash *= UINT64_C(1099511628211);
		p++;
	}
	return hash;
}

static char *editorPathJoin(const char *left, const char *right) {
	size_t left_len = strlen(left);
	size_t right_len = strlen(right);
	size_t total = 0;
	if (!editorSizeAdd(left_len, 1, &total) ||
			!editorSizeAdd(total, right_len, &total) ||
			!editorSizeAdd(total, 1, &total)) {
		return NULL;
	}

	char *path = editorMalloc(total);
	if (path == NULL) {
		return NULL;
	}

	memcpy(path, left, left_len);
	path[left_len] = '/';
	memcpy(path + left_len + 1, right, right_len);
	path[left_len + 1 + right_len] = '\0';
	return path;
}

static int editorEnsureDirectoryExists(const char *path, mode_t mode) {
	if (mkdir(path, mode) == 0) {
		return 1;
	}
	if (errno != EEXIST) {
		return 0;
	}

	struct stat st;
	if (stat(path, &st) == -1) {
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static char *editorRecoveryBuildPathForBase(const char *base, uint64_t hash) {
	char name[128];
	int written = snprintf(name, sizeof(name), "rotide-recovery-u%lu-%016llx.swap",
			(unsigned long)getuid(), (unsigned long long)hash);
	if (written <= 0 || (size_t)written >= sizeof(name)) {
		return NULL;
	}
	return editorPathJoin(base, name);
}

static char *editorResolveRecoveryPath(void) {
	char *cwd = getcwd(NULL, 0);
	if (cwd == NULL) {
		cwd = strdup(".");
		if (cwd == NULL) {
			return NULL;
		}
	}

	uint64_t cwd_hash = editorRecoveryHashPath(cwd);
	free(cwd);

	char *recovery_path = NULL;
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *dot_rotide = editorPathJoin(home, ".rotide");
		char *recovery_dir = NULL;
		if (dot_rotide != NULL) {
			recovery_dir = editorPathJoin(dot_rotide, "recovery");
		}

		if (dot_rotide != NULL && recovery_dir != NULL &&
				editorEnsureDirectoryExists(dot_rotide, 0700) &&
				editorEnsureDirectoryExists(recovery_dir, 0700)) {
			recovery_path = editorRecoveryBuildPathForBase(recovery_dir, cwd_hash);
		}

		free(dot_rotide);
		free(recovery_dir);
	}

	if (recovery_path == NULL) {
		recovery_path = editorRecoveryBuildPathForBase("/tmp", cwd_hash);
	}

	return recovery_path;
}

int editorRecoveryInitForCurrentDir(void) {
	free(E.recovery_path);
	E.recovery_path = editorResolveRecoveryPath();
	E.recovery_last_autosave_time = 0;
	return E.recovery_path != NULL;
}

void editorRecoveryShutdown(void) {
	free(E.recovery_path);
	E.recovery_path = NULL;
	E.recovery_last_autosave_time = 0;
}

const char *editorRecoveryPath(void) {
	return E.recovery_path;
}

int editorRecoveryHasSnapshot(void) {
	if (E.recovery_path == NULL) {
		return 0;
	}
	struct stat st;
	if (stat(E.recovery_path, &st) == -1) {
		return 0;
	}
	return S_ISREG(st.st_mode);
}

void editorRecoveryCleanupOnCleanExit(void) {
	if (E.recovery_path == NULL) {
		return;
	}
	(void)unlink(E.recovery_path);
	E.recovery_last_autosave_time = 0;
}

static int editorRecoveryReadExact(int fd, void *buf, size_t len) {
	char *dst = (char *)buf;
	size_t total = 0;
	while (total < len) {
		ssize_t nread = read(fd, dst + total, len - total);
		if (nread == 0) {
			return EDITOR_READ_EXACT_EOF;
		}
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return EDITOR_READ_EXACT_ERR;
		}
		total += (size_t)nread;
	}
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryWriteU32(int fd, uint32_t value) {
	unsigned char bytes[4];
	bytes[0] = (unsigned char)(value & 0xFFU);
	bytes[1] = (unsigned char)((value >> 8) & 0xFFU);
	bytes[2] = (unsigned char)((value >> 16) & 0xFFU);
	bytes[3] = (unsigned char)((value >> 24) & 0xFFU);
	return editorWriteAll(fd, (const char *)bytes, sizeof(bytes)) == 0;
}

static int editorRecoveryWriteI32(int fd, int32_t value) {
	return editorRecoveryWriteU32(fd, (uint32_t)value);
}

static int editorRecoveryReadU32(int fd, uint32_t *value_out) {
	unsigned char bytes[4];
	int read_status = editorRecoveryReadExact(fd, bytes, sizeof(bytes));
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	*value_out = (uint32_t)bytes[0] |
			((uint32_t)bytes[1] << 8) |
			((uint32_t)bytes[2] << 16) |
			((uint32_t)bytes[3] << 24);
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryReadI32(int fd, int32_t *value_out) {
	uint32_t raw = 0;
	int read_status = editorRecoveryReadU32(fd, &raw);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	*value_out = (int32_t)raw;
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryGetTabView(int idx, struct editorRecoveryTabView *view_out) {
	if (view_out == NULL) {
		errno = EINVAL;
		return 0;
	}

	int tab_count = E.tab_count > 0 ? E.tab_count : 1;
	int active_tab = E.active_tab;
	if (tab_count == 1) {
		active_tab = 0;
	}
	if (idx < 0 || idx >= tab_count) {
		errno = EINVAL;
		return 0;
	}

	if (idx == active_tab) {
		view_out->cx = E.cx;
		view_out->cy = E.cy;
		view_out->rowoff = E.rowoff;
		view_out->coloff = E.coloff;
		view_out->numrows = E.numrows;
		view_out->rows = E.rows;
		view_out->filename = E.filename;
		return 1;
	}

	struct editorTabState *tab = &E.tabs[idx];
	view_out->cx = tab->cx;
	view_out->cy = tab->cy;
	view_out->rowoff = tab->rowoff;
	view_out->coloff = tab->coloff;
	view_out->numrows = tab->numrows;
	view_out->rows = tab->rows;
	view_out->filename = tab->filename;
	return 1;
}

static int editorRecoveryWriteSessionToFd(int fd) {
	int tab_count = E.tab_count > 0 ? E.tab_count : 1;
	int active_tab = E.active_tab;
	if (tab_count == 1) {
		active_tab = 0;
	}
	if (tab_count < 1 || tab_count > ROTIDE_MAX_TABS ||
			active_tab < 0 || active_tab >= tab_count) {
		errno = EINVAL;
		return 0;
	}

	if (editorWriteAll(fd, ROTIDE_RECOVERY_MAGIC, ROTIDE_RECOVERY_MAGIC_LEN) == -1 ||
			!editorRecoveryWriteU32(fd, ROTIDE_RECOVERY_VERSION) ||
			!editorRecoveryWriteU32(fd, (uint32_t)tab_count) ||
			!editorRecoveryWriteU32(fd, (uint32_t)active_tab)) {
		if (errno == 0) {
			errno = EIO;
		}
		return 0;
	}

	for (int tab_idx = 0; tab_idx < tab_count; tab_idx++) {
		struct editorRecoveryTabView view;
		if (!editorRecoveryGetTabView(tab_idx, &view)) {
			return 0;
		}
		if (view.numrows < 0 || (view.rows == NULL && view.numrows > 0)) {
			errno = EINVAL;
			return 0;
		}

		size_t filename_len = 0;
		if (view.filename != NULL) {
			filename_len = strlen(view.filename);
			if (filename_len > ROTIDE_RECOVERY_MAX_FILENAME_BYTES) {
				errno = EOVERFLOW;
				return 0;
			}
		}

		if (!editorRecoveryWriteI32(fd, (int32_t)view.cx) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.cy) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.rowoff) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.coloff) ||
				!editorRecoveryWriteU32(fd, (uint32_t)filename_len)) {
			if (errno == 0) {
				errno = EIO;
			}
			return 0;
		}
		if (filename_len > 0 && editorWriteAll(fd, view.filename, filename_len) == -1) {
			return 0;
		}

		if (!editorRecoveryWriteU32(fd, (uint32_t)view.numrows)) {
			if (errno == 0) {
				errno = EIO;
			}
			return 0;
		}

		size_t total_bytes = 0;
		for (int row_idx = 0; row_idx < view.numrows; row_idx++) {
			if (view.rows[row_idx].size < 0) {
				errno = EINVAL;
				return 0;
			}
			size_t row_len = (size_t)view.rows[row_idx].size;
			size_t row_total = 0;
			if (!editorSizeAdd(row_len, NEWLINE_CHAR_WIDTH, &row_total) ||
					!editorSizeAdd(total_bytes, row_total, &total_bytes) ||
					total_bytes > ROTIDE_MAX_TEXT_BYTES) {
				errno = EOVERFLOW;
				return 0;
			}

			if (!editorRecoveryWriteU32(fd, (uint32_t)row_len)) {
				if (errno == 0) {
					errno = EIO;
				}
				return 0;
			}
			if (row_len > 0 &&
					editorWriteAll(fd, view.rows[row_idx].chars, row_len) == -1) {
				return 0;
			}
		}
	}

	return 1;
}

static int editorRecoveryWriteSnapshotAtomic(void) {
	if (E.recovery_path == NULL) {
		errno = EINVAL;
		return 0;
	}

	char *tmp_path = editorTempPathForTarget(E.recovery_path);
	if (tmp_path == NULL) {
		errno = ENOMEM;
		return 0;
	}

	int fd = -1;
	int tmp_created = 0;
	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, 0600) == -1) {
		goto err;
	}
	if (!editorRecoveryWriteSessionToFd(fd)) {
		if (errno == 0) {
			errno = EIO;
		}
		goto err;
	}
	if (fsync(fd) == -1) {
		goto err;
	}
	if (close(fd) == -1) {
		fd = -1;
		goto err;
	}
	fd = -1;

	if (rename(tmp_path, E.recovery_path) == -1) {
		goto err;
	}

	free(tmp_path);
	return 1;

err: {
	int saved_errno = errno;
	if (fd != -1) {
		(void)close(fd);
	}
	if (tmp_created) {
		(void)unlink(tmp_path);
	}
	free(tmp_path);
	errno = saved_errno;
	return 0;
}
}

static int editorRecoveryTabReadRows(int fd, struct editorRecoveryTab *tab) {
	uint32_t row_count_u32 = 0;
	int read_status = editorRecoveryReadU32(fd, &row_count_u32);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	if (row_count_u32 > (uint32_t)INT_MAX) {
		return EDITOR_READ_EXACT_EOF;
	}

	tab->row_count = (int)row_count_u32;
	if (tab->row_count == 0) {
		tab->rows = NULL;
		return EDITOR_READ_EXACT_OK;
	}

	size_t rows_bytes = 0;
	if (!editorSizeMul(sizeof(struct editorRecoveryRow), (size_t)tab->row_count, &rows_bytes)) {
		return EDITOR_READ_EXACT_EOF;
	}
	tab->rows = editorMalloc(rows_bytes);
	if (tab->rows == NULL) {
		errno = ENOMEM;
		return EDITOR_READ_EXACT_ERR;
	}
	memset(tab->rows, 0, rows_bytes);

	size_t total_bytes = 0;
	for (int row_idx = 0; row_idx < tab->row_count; row_idx++) {
		uint32_t row_len_u32 = 0;
		read_status = editorRecoveryReadU32(fd, &row_len_u32);
		if (read_status != EDITOR_READ_EXACT_OK) {
			return read_status;
		}
		if (row_len_u32 > (uint32_t)INT_MAX) {
			return EDITOR_READ_EXACT_EOF;
		}

		size_t row_len = (size_t)row_len_u32;
		size_t row_total = 0;
		if (!editorSizeAdd(row_len, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(total_bytes, row_total, &total_bytes) ||
				total_bytes > ROTIDE_MAX_TEXT_BYTES) {
			return EDITOR_READ_EXACT_EOF;
		}

		size_t row_alloc = 0;
		if (!editorSizeAdd(row_len, 1, &row_alloc)) {
			return EDITOR_READ_EXACT_EOF;
		}
		tab->rows[row_idx].chars = editorMalloc(row_alloc);
		if (tab->rows[row_idx].chars == NULL) {
			errno = ENOMEM;
			return EDITOR_READ_EXACT_ERR;
		}
		if (row_len > 0) {
			read_status = editorRecoveryReadExact(fd, tab->rows[row_idx].chars, row_len);
			if (read_status != EDITOR_READ_EXACT_OK) {
				return read_status;
			}
		}
		tab->rows[row_idx].chars[row_len] = '\0';
		tab->rows[row_idx].len = row_len;
	}

	return EDITOR_READ_EXACT_OK;
}

static enum editorRecoveryLoadStatus editorRecoveryLoadSessionFromPath(const char *path,
		struct editorRecoverySession *session_out) {
	memset(session_out, 0, sizeof(*session_out));

	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT) {
			return EDITOR_RECOVERY_LOAD_NOT_FOUND;
		}
		return EDITOR_RECOVERY_LOAD_IO;
	}

	enum editorRecoveryLoadStatus status = EDITOR_RECOVERY_LOAD_INVALID;
	char magic[ROTIDE_RECOVERY_MAGIC_LEN];
	int read_status = editorRecoveryReadExact(fd, magic, sizeof(magic));
	if (read_status != EDITOR_READ_EXACT_OK) {
		status = read_status == EDITOR_READ_EXACT_ERR ? EDITOR_RECOVERY_LOAD_IO :
				EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (memcmp(magic, ROTIDE_RECOVERY_MAGIC, ROTIDE_RECOVERY_MAGIC_LEN) != 0) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}

	uint32_t version = 0;
	uint32_t tab_count_u32 = 0;
	uint32_t active_tab_u32 = 0;
	if (editorRecoveryReadU32(fd, &version) != EDITOR_READ_EXACT_OK ||
			editorRecoveryReadU32(fd, &tab_count_u32) != EDITOR_READ_EXACT_OK ||
			editorRecoveryReadU32(fd, &active_tab_u32) != EDITOR_READ_EXACT_OK) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (version != ROTIDE_RECOVERY_VERSION ||
			tab_count_u32 < 1 ||
			tab_count_u32 > ROTIDE_MAX_TABS ||
			active_tab_u32 >= tab_count_u32) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}

	session_out->tab_count = (int)tab_count_u32;
	session_out->active_tab = (int)active_tab_u32;
	size_t tabs_bytes = 0;
	if (!editorSizeMul(sizeof(struct editorRecoveryTab), (size_t)session_out->tab_count,
				&tabs_bytes)) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	session_out->tabs = editorMalloc(tabs_bytes);
	if (session_out->tabs == NULL) {
		status = EDITOR_RECOVERY_LOAD_OOM;
		goto out;
	}
	memset(session_out->tabs, 0, tabs_bytes);

	for (int tab_idx = 0; tab_idx < session_out->tab_count; tab_idx++) {
		struct editorRecoveryTab *tab = &session_out->tabs[tab_idx];
		int32_t cx = 0;
		int32_t cy = 0;
		int32_t rowoff = 0;
		int32_t coloff = 0;
		if (editorRecoveryReadI32(fd, &cx) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &cy) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &rowoff) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &coloff) != EDITOR_READ_EXACT_OK) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		tab->cx = (int)cx;
		tab->cy = (int)cy;
		tab->rowoff = (int)rowoff;
		tab->coloff = (int)coloff;

		uint32_t filename_len_u32 = 0;
		if (editorRecoveryReadU32(fd, &filename_len_u32) != EDITOR_READ_EXACT_OK) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		if (filename_len_u32 > ROTIDE_RECOVERY_MAX_FILENAME_BYTES) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		if (filename_len_u32 > 0) {
			size_t filename_len = (size_t)filename_len_u32;
			size_t filename_alloc = 0;
			if (!editorSizeAdd(filename_len, 1, &filename_alloc)) {
				status = EDITOR_RECOVERY_LOAD_INVALID;
				goto out;
			}
			tab->filename = editorMalloc(filename_alloc);
			if (tab->filename == NULL) {
				status = EDITOR_RECOVERY_LOAD_OOM;
				goto out;
			}
			read_status = editorRecoveryReadExact(fd, tab->filename, filename_len);
			if (read_status != EDITOR_READ_EXACT_OK) {
				status = read_status == EDITOR_READ_EXACT_ERR ? EDITOR_RECOVERY_LOAD_IO :
						EDITOR_RECOVERY_LOAD_INVALID;
				goto out;
			}
			tab->filename[filename_len] = '\0';
		}

		read_status = editorRecoveryTabReadRows(fd, tab);
		if (read_status != EDITOR_READ_EXACT_OK) {
			if (read_status == EDITOR_READ_EXACT_ERR) {
				status = errno == ENOMEM ? EDITOR_RECOVERY_LOAD_OOM :
						EDITOR_RECOVERY_LOAD_IO;
			} else {
				status = EDITOR_RECOVERY_LOAD_INVALID;
			}
			goto out;
		}
	}

	char trailing = '\0';
	ssize_t trailing_read;
	do {
		trailing_read = read(fd, &trailing, 1);
	} while (trailing_read == -1 && errno == EINTR);
	if (trailing_read == 1) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (trailing_read == -1) {
		status = EDITOR_RECOVERY_LOAD_IO;
		goto out;
	}

	status = EDITOR_RECOVERY_LOAD_OK;

out:
	if (close(fd) == -1 && status == EDITOR_RECOVERY_LOAD_OK) {
		status = EDITOR_RECOVERY_LOAD_IO;
	}
	if (status != EDITOR_RECOVERY_LOAD_OK) {
		editorRecoverySessionFree(session_out);
	}
	return status;
}

static void editorRecoveryClampActiveCursorAndScroll(const struct editorRecoveryTab *tab) {
	if (tab->cy < 0) {
		E.cy = 0;
	} else {
		E.cy = tab->cy;
	}
	if (E.numrows == 0) {
		E.cy = 0;
		E.cx = 0;
		E.rowoff = 0;
		E.coloff = tab->coloff < 0 ? 0 : tab->coloff;
		return;
	}
	if (E.cy >= E.numrows) {
		E.cy = E.numrows - 1;
	}

	struct erow *row = &E.rows[E.cy];
	int target_cx = tab->cx;
	if (target_cx < 0) {
		target_cx = 0;
	}
	if (target_cx > row->size) {
		target_cx = row->size;
	}
	E.cx = editorRowClampCxToClusterBoundary(row, target_cx);

	if (tab->rowoff < 0) {
		E.rowoff = 0;
	} else {
		E.rowoff = tab->rowoff;
	}
	if (E.rowoff >= E.numrows) {
		E.rowoff = E.numrows - 1;
	}
	E.coloff = tab->coloff < 0 ? 0 : tab->coloff;
}

static int editorRecoveryPopulateActiveFromTab(const struct editorRecoveryTab *tab) {
	free(E.filename);
	E.filename = NULL;
	if (tab->filename != NULL) {
		E.filename = strdup(tab->filename);
		if (E.filename == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
	}

	for (int row_idx = 0; row_idx < tab->row_count; row_idx++) {
		int prev_numrows = E.numrows;
		editorInsertRow(E.numrows, tab->rows[row_idx].chars, tab->rows[row_idx].len);
		if (E.numrows == prev_numrows) {
			return 0;
		}
	}

	E.dirty = 1;
	editorHistoryReset();
	editorRecoveryClampActiveCursorAndScroll(tab);
	return 1;
}

static int editorRecoveryApplySession(const struct editorRecoverySession *session) {
	if (session == NULL || session->tab_count < 1) {
		return 0;
	}

	if (!editorTabsInit()) {
		return 0;
	}

	for (int tab_idx = 0; tab_idx < session->tab_count; tab_idx++) {
		if (tab_idx > 0 && !editorTabNewEmpty()) {
			(void)editorTabsInit();
			return 0;
		}

		if (!editorRecoveryPopulateActiveFromTab(&session->tabs[tab_idx])) {
			(void)editorTabsInit();
			return 0;
		}
	}

	if (!editorTabSwitchToIndex(session->active_tab)) {
		(void)editorTabsInit();
		return 0;
	}

	return 1;
}

int editorRecoveryRestoreSnapshot(void) {
	if (E.recovery_path == NULL) {
		return 0;
	}

	struct editorRecoverySession session;
	enum editorRecoveryLoadStatus load_status =
			editorRecoveryLoadSessionFromPath(E.recovery_path, &session);
	if (load_status == EDITOR_RECOVERY_LOAD_NOT_FOUND) {
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_OOM) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_IO) {
		editorSetStatusMsg("Recovery load failed (%s)", strerror(errno));
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_INVALID) {
		(void)unlink(E.recovery_path);
		editorSetStatusMsg("Recovery data was invalid and was discarded");
		return 0;
	}

	int applied = editorRecoveryApplySession(&session);
	editorRecoverySessionFree(&session);
	if (!applied) {
		return 0;
	}

	editorSetStatusMsg("Recovered previous session");
	return 1;
}

static int editorRecoveryPromptRestoreChoice(void) {
	while (1) {
		editorSetStatusMsg("Recovery data found. Restore previous session? (y/N)");
		editorRefreshScreen();

		int key = editorReadKey();
		if (key == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (key == MOUSE_EVENT) {
			continue;
		}
		if (key == INPUT_EOF_EVENT) {
			return 0;
		}
		if (key == 'y' || key == 'Y') {
			return 1;
		}
		if (key == 'n' || key == 'N' || key == '\x1b' || key == '\r') {
			return 0;
		}
	}
}

int editorRecoveryPromptAndMaybeRestore(void) {
	if (!editorRecoveryHasSnapshot()) {
		return 0;
	}

	if (editorRecoveryPromptRestoreChoice()) {
		return editorRecoveryRestoreSnapshot();
	}

	editorRecoveryCleanupOnCleanExit();
	editorSetStatusMsg("Discarded recovery data");
	return 0;
}

void editorRecoveryMaybeAutosaveOnActivity(void) {
	if (E.recovery_path == NULL) {
		return;
	}

	if (!editorTabAnyDirty()) {
		editorRecoveryCleanupOnCleanExit();
		return;
	}

	time_t now = time(NULL);
	if (now != (time_t)-1 &&
			E.recovery_last_autosave_time != 0 &&
			now - E.recovery_last_autosave_time < ROTIDE_RECOVERY_AUTOSAVE_DEBOUNCE_SECONDS) {
		return;
	}

	if (!editorRecoveryWriteSnapshotAtomic()) {
		int saved_errno = errno;
		if (saved_errno != 0) {
			editorSetStatusMsg("Recovery autosave failed (%s)", strerror(saved_errno));
		} else {
			editorSetStatusMsg("Recovery autosave failed");
		}
		if (now != (time_t)-1) {
			E.recovery_last_autosave_time = now;
		}
		return;
	}

	if (now != (time_t)-1) {
		E.recovery_last_autosave_time = now;
	}
}

int editorStartupLoadRecoveryOrOpenArgs(int argc, char *argv[]) {
	int restored = editorRecoveryPromptAndMaybeRestore();
	if (!restored && argc >= 2) {
		editorOpen(argv[1]);
		for (int i = 2; i < argc; i++) {
			if (!editorTabOpenFileAsNew(argv[i])) {
				break;
			}
		}
		(void)editorTabSwitchToIndex(0);
	}
	return restored;
}
