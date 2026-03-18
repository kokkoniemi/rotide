#include "buffer.h"

#include "alloc.h"
#include "input.h"
#include "save_syscalls.h"
#include "size_utils.h"
#include "terminal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

/*** File io ***/

#define NEWLINE_CHAR_WIDTH 1

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

int editorTabVisibleSlotsForWidth(int cols) {
	int slots = cols / ROTIDE_TAB_SLOT_WIDTH;
	if (slots < 1) {
		slots = 1;
	}
	return slots;
}

void editorTabsAlignViewToActive(int cols) {
	if (E.tab_count <= 0) {
		E.tab_view_start = 0;
		return;
	}

	int visible = editorTabVisibleSlotsForWidth(cols);
	int max_start = E.tab_count - visible;
	if (max_start < 0) {
		max_start = 0;
	}

	if (E.tab_view_start > max_start) {
		E.tab_view_start = max_start;
	}
	if (E.tab_view_start < 0) {
		E.tab_view_start = 0;
	}
	if (E.active_tab < E.tab_view_start) {
		E.tab_view_start = E.active_tab;
	}
	if (E.active_tab >= E.tab_view_start + visible) {
		E.tab_view_start = E.active_tab - visible + 1;
	}
	if (E.tab_view_start > max_start) {
		E.tab_view_start = max_start;
	}
	if (E.tab_view_start < 0) {
		E.tab_view_start = 0;
	}
}

int editorTabHitTestColumn(int col, int cols) {
	if (col < 0 || col >= cols || E.tab_count <= 0) {
		return -1;
	}

	editorTabsAlignViewToActive(cols);
	int visible = editorTabVisibleSlotsForWidth(cols);
	int slot = col / ROTIDE_TAB_SLOT_WIDTH;
	if (slot < 0 || slot >= visible) {
		return -1;
	}

	int tab_idx = E.tab_view_start + slot;
	if (tab_idx < 0 || tab_idx >= E.tab_count) {
		return -1;
	}
	return tab_idx;
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
