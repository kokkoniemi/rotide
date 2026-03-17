#include "buffer.h"

#include "alloc.h"
#include "input.h"
#include "save_syscalls.h"
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

char *editorRowsToStr(int *buflen) {
	int total = 0;
	for (int i = 0; i < E.numrows; i++) {
		total += E.rows[i].size + NEWLINE_CHAR_WIDTH;
	}

	// Save format is a plain newline-terminated concatenation of rows.
	char *buf = NULL;
	if (total > 0) {
		buf = editorMalloc((size_t)total);
		if (buf == NULL) {
			*buflen = total;
			return NULL;
		}
	}
	char *p = buf;
	for (int i = 0; i < E.numrows; i++) {
		memcpy(p, E.rows[i].chars, E.rows[i].size);
		p += E.rows[i].size;
		*p = '\n';
		p++;
	}

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

int editorRowCxToRx(const struct erow *row, int cx) {
	int rx = 0;
	cx = editorRowClampCxToClusterBoundary(row, cx);
	for (int j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') {
			rx += (ROTIDE_TAB_WIDTH - 1) - (rx % ROTIDE_TAB_WIDTH);
			rx++;
			continue;
		}
		rx += editorCharDisplayWidth(&row->chars[j], row->size - j);
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
		for (int j = cx; j < next_cx; j++) {
			if (row->chars[j] == '\t') {
				cluster_width += (ROTIDE_TAB_WIDTH - 1) -
						((cur_rx + cluster_width) % ROTIDE_TAB_WIDTH);
				cluster_width++;
				continue;
			}
			cluster_width += editorCharDisplayWidth(&row->chars[j], row->size - j);
		}

		if (cur_rx + cluster_width > rx) {
			return cx;
		}

		cur_rx += cluster_width;
		cx = next_cx;
	}

	return row->size;
}

static int editorBuildRender(const char *chars, int size, char **render_out, int *rsize_out) {
	int tabs = 0;
	for (int i = 0; i < size; i++) {
		if (chars[i] == '\t') {
			tabs++;
		}
	}

	size_t render_cap = (size_t)size + (size_t)tabs * (ROTIDE_TAB_WIDTH - 1) + 1;
	char *render = editorMalloc(render_cap);
	if (render == NULL) {
		return 0;
	}

	int idx = 0;
	int rx = 0;
	for (int i = 0; i < size; i++) {
		if (chars[i] == '\t') {
			// Expand tabs to the next visual tab stop, not a fixed byte count.
			do {
				render[idx++] = ' ';
				rx++;
			} while (rx % ROTIDE_TAB_WIDTH != 0);
			continue;
		}
		render[idx++] = chars[i];
		rx += editorCharDisplayWidth(&chars[i], size - i);
	}
	render[idx] = '\0';

	*render_out = render;
	*rsize_out = idx;
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

static int editorPosToLinearOffset(int cy, int cx) {
	int offset = 0;
	for (int row = 0; row < cy; row++) {
		offset += E.rows[row].size + 1;
	}
	return offset + cx;
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
		int *len_out) {
	if (text_out == NULL || len_out == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	struct editorSelectionRange normalized;
	if (!editorNormalizeRange(range, &normalized)) {
		return 0;
	}

	int full_len = 0;
	char *full_text = editorRowsToStr(&full_len);
	if (full_text == NULL && full_len > 0) {
		editorSetAllocFailureStatus();
		return -1;
	}

	int start_offset = editorPosToLinearOffset(normalized.start_cy, normalized.start_cx);
	int end_offset = editorPosToLinearOffset(normalized.end_cy, normalized.end_cx);
	int selected_len = end_offset - start_offset;
	if (selected_len <= 0) {
		free(full_text);
		return 0;
	}

	char *selected = editorMalloc((size_t)selected_len + 1);
	if (selected == NULL) {
		free(full_text);
		editorSetAllocFailureStatus();
		return -1;
	}
	memcpy(selected, &full_text[start_offset], (size_t)selected_len);
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

	int full_len = 0;
	char *full_text = editorRowsToStr(&full_len);
	if (full_text == NULL && full_len > 0) {
		editorSetAllocFailureStatus();
		return -1;
	}

	int start_offset = editorPosToLinearOffset(normalized.start_cy, normalized.start_cx);
	int end_offset = editorPosToLinearOffset(normalized.end_cy, normalized.end_cx);
	int removed_len = end_offset - start_offset;
	if (removed_len <= 0) {
		free(full_text);
		return 0;
	}

	int new_len = full_len - removed_len;
	char *new_text = NULL;
	if (new_len > 0) {
		new_text = editorMalloc((size_t)new_len + 1);
		if (new_text == NULL) {
			free(full_text);
			editorSetAllocFailureStatus();
			return -1;
		}

		memcpy(new_text, full_text, (size_t)start_offset);
		memcpy(&new_text[start_offset], &full_text[end_offset], (size_t)(full_len - end_offset));
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

int editorClipboardSet(const char *text, int len) {
	if (len < 0) {
		len = 0;
	}

	char *new_text = NULL;
	if (len > 0) {
		if (text == NULL) {
			return 0;
		}
		new_text = editorMalloc((size_t)len + 1);
		if (new_text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
		memcpy(new_text, text, (size_t)len);
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

const char *editorClipboardGet(int *len_out) {
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

	int textlen = 0;
	char *text = editorRowsToStr(&textlen);
	if (text == NULL && textlen > 0) {
		editorSetAllocFailureStatus();
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

static int editorAppendRestoredRow(struct erow **rows, int *numrows, const char *s, int len) {
	char *row_chars = editorMalloc((size_t)len + 1);
	if (row_chars == NULL) {
		return 0;
	}
	memcpy(row_chars, s, (size_t)len);
	row_chars[len] = '\0';

	char *row_render = NULL;
	int row_rsize = 0;
	if (!editorBuildRender(row_chars, len, &row_render, &row_rsize)) {
		free(row_chars);
		return 0;
	}

	struct erow *new_rows = editorRealloc(*rows, sizeof(struct erow) * (size_t)(*numrows + 1));
	if (new_rows == NULL) {
		free(row_render);
		free(row_chars);
		return 0;
	}

	*rows = new_rows;
	(*rows)[*numrows].size = len;
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
	int line_start = 0;

	for (int i = 0; i < snapshot->textlen; i++) {
		if (snapshot->text[i] != '\n') {
			continue;
		}

		int line_len = i - line_start;
		if (!editorAppendRestoredRow(&rows, &numrows, &snapshot->text[line_start], line_len)) {
			editorFreeRowArray(rows, numrows);
			return 0;
		}
		line_start = i + 1;
	}

	if (line_start < snapshot->textlen) {
		int line_len = snapshot->textlen - line_start;
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

	char *row_chars = editorMalloc(len + 1);
	if (row_chars == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	memcpy(row_chars, s, len);
	row_chars[len] = '\0';

	struct erow *new_rows = editorRealloc(E.rows, sizeof(struct erow) * (E.numrows + 1));
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

	char *new_chars = editorRealloc(row->chars, (size_t)row->size + 2);
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
	char *new_chars = editorRealloc(row->chars, (size_t)row->size + len + 1);
	if (new_chars == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	row->chars = new_chars;
	memcpy(&row->chars[row->size], s, len);
	row->size += (int)len;
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
	if (idx < 0 || len <= 0 || idx + len > row->size) {
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
	while ((llen = getline(&l, &lcap, fp)) != -1) {
		while (llen > 0 && (l[llen - 1] == '\n' || l[llen - 1] == '\r')) {
			llen--;
		}

		editorInsertRow(E.numrows, l, llen);
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

static int editorWriteAll(int fd, const char *buf, int len) {
	int total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, (size_t)(len - total));
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		total += (int)written;
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
	size_t total_len = dir_len + base_len + sizeof(suffix);
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
	char *dir_path = editorMalloc(dir_len + 1);
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

	int len;
	char *buf = editorRowsToStr(&len);
	char *tmp_path = editorTempPathForTarget(E.filename);
	int fd = -1;
	int dir_fd = -1;
	int tmp_created = 0;
	int tmp_renamed = 0;
	mode_t mode = editorDefaultCreateMode();
	struct stat st;

	if (buf == NULL && len > 0) {
		free(tmp_path);
		editorSetAllocFailureStatus();
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
	editorSetStatusMsg("%d bytes written to disk", len);
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
