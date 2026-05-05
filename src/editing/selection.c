#include "editing/selection.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/syntax.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/row.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void editorClearSelectionState(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	editorColumnSelectionClear();
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

int editorGetSelectionRange(struct editorSelectionRange *range_out) {
	if (range_out == NULL || !E.selection_mode_active) {
		return 0;
	}

	int anchor_cy = 0;
	int anchor_cx = 0;
	if (!editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_cy, &anchor_cx)) {
		return 0;
	}

	struct editorSelectionRange range = {
		.start_cy = anchor_cy,
		.start_cx = anchor_cx,
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

	errno = 0;
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		if (errno == EOVERFLOW) {
			editorSetOperationTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return -1;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	if (!editorBufferPosToOffset(normalized.start_cy, normalized.start_cx, &start_offset) ||
			!editorBufferPosToOffset(normalized.end_cy, normalized.end_cx, &end_offset) ||
			end_offset < start_offset || end_offset > source.length) {
		editorSetOperationTooLargeStatus();
		return -1;
	}
	size_t selected_len = end_offset - start_offset;
	if (selected_len == 0) {
		return 0;
	}

	char *selected = editorTextSourceDupRange(&source, start_offset, end_offset, len_out);
	if (selected == NULL) {
		editorSetAllocFailureStatus();
		return -1;
	}

	*text_out = selected;
	return 1;
}

int editorDeleteRange(const struct editorSelectionRange *range) {
	struct editorSelectionRange normalized;
	if (!editorNormalizeRange(range, &normalized)) {
		return 0;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	int dirty_delta = 1;
	if (!editorBufferPosToOffset(normalized.start_cy, normalized.start_cx, &start_offset) ||
			!editorBufferPosToOffset(normalized.end_cy, normalized.end_cx, &end_offset) ||
			end_offset < start_offset) {
		editorSetOperationTooLargeStatus();
		return -1;
	}
	size_t removed_len = end_offset - start_offset;
	if (removed_len == 0) {
		return 0;
	}
	size_t before_cursor_offset = start_offset;
	if (!editorBufferPosToOffset(E.cy, E.cx, &before_cursor_offset)) {
		before_cursor_offset = start_offset;
	}
	if (normalized.start_cy != normalized.end_cy) {
		dirty_delta = 2;
	}
	if (E.dirty > INT_MAX - dirty_delta) {
		editorSetOperationTooLargeStatus();
		return -1;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_DELETE_TEXT,
		.start_offset = start_offset,
		.old_len = removed_len,
		.new_text = "",
		.new_len = 0,
		.before_cursor_offset = before_cursor_offset,
		.after_cursor_offset = start_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	if (!editorApplyDocumentEdit(&edit)) {
		return -1;
	}
	return 1;
}

void editorColumnSelectionClear(void) {
	E.column_select_active = 0;
	E.column_select_anchor_cy = 0;
	E.column_select_anchor_rx = 0;
	E.column_select_cursor_rx = 0;
}

int editorColumnSelectionGetRect(struct editorColumnSelectionRect *rect_out) {
	if (rect_out == NULL || !E.column_select_active) {
		return 0;
	}
	int top = E.column_select_anchor_cy < E.cy ? E.column_select_anchor_cy : E.cy;
	int bottom = E.column_select_anchor_cy > E.cy ? E.column_select_anchor_cy : E.cy;
	int left = E.column_select_anchor_rx < E.column_select_cursor_rx
			? E.column_select_anchor_rx
			: E.column_select_cursor_rx;
	int right = E.column_select_anchor_rx > E.column_select_cursor_rx
			? E.column_select_anchor_rx
			: E.column_select_cursor_rx;
	if (top < 0) {
		top = 0;
	}
	if (bottom > E.numrows) {
		bottom = E.numrows;
	}
	if (left < 0) {
		left = 0;
	}
	if (right < left) {
		right = left;
	}
	rect_out->top_cy = top;
	rect_out->bottom_cy = bottom;
	rect_out->left_rx = left;
	rect_out->right_rx = right;
	return 1;
}

int editorColumnSelectionRowSpan(int row_idx, int left_rx, int right_rx, int *cx_start_out,
		int *cx_end_out) {
	if (cx_start_out == NULL || cx_end_out == NULL) {
		return 0;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}
	struct erow *row = &E.rows[row_idx];
	int cx_start = editorRowRxToCx(row, left_rx);
	int cx_end = editorRowRxToCx(row, right_rx);
	if (cx_start < 0) {
		cx_start = 0;
	}
	if (cx_end > row->size) {
		cx_end = row->size;
	}
	if (cx_start > row->size) {
		cx_start = row->size;
	}
	if (cx_end < cx_start) {
		cx_end = cx_start;
	}
	cx_start = editorRowClampCxToClusterBoundary(row, cx_start);
	cx_end = editorRowClampCxToClusterBoundary(row, cx_end);
	*cx_start_out = cx_start;
	*cx_end_out = cx_end;
	return 1;
}

int editorColumnSelectionExtractText(char **text_out, size_t *len_out) {
	if (text_out == NULL || len_out == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	struct editorColumnSelectionRect rect;
	if (!editorColumnSelectionGetRect(&rect)) {
		return 0;
	}

	size_t total = 0;
	for (int r = rect.top_cy; r <= rect.bottom_cy && r < E.numrows; r++) {
		int cx_start = 0;
		int cx_end = 0;
		(void)editorColumnSelectionRowSpan(r, rect.left_rx, rect.right_rx, &cx_start, &cx_end);
		size_t row_len = (size_t)(cx_end - cx_start);
		size_t add = (r > rect.top_cy) ? row_len + 1 : row_len;
		if (total > ROTIDE_MAX_TEXT_BYTES - add) {
			editorSetOperationTooLargeStatus();
			return -1;
		}
		total += add;
	}

	if (total == 0) {
		return 0;
	}

	char *text = editorMalloc(total + 1);
	if (text == NULL) {
		editorSetAllocFailureStatus();
		return -1;
	}

	size_t pos = 0;
	for (int r = rect.top_cy; r <= rect.bottom_cy && r < E.numrows; r++) {
		int cx_start = 0;
		int cx_end = 0;
		(void)editorColumnSelectionRowSpan(r, rect.left_rx, rect.right_rx, &cx_start, &cx_end);
		if (r > rect.top_cy) {
			text[pos++] = '\n';
		}
		size_t row_len = (size_t)(cx_end - cx_start);
		if (row_len > 0) {
			memcpy(text + pos, E.rows[r].chars + cx_start, row_len);
			pos += row_len;
		}
	}
	text[pos] = '\0';
	*text_out = text;
	*len_out = pos;
	return 1;
}

struct editorColumnInsertLine {
	const char *text;
	size_t len;
};

static int editorColumnSelectionRowCount(const struct editorColumnSelectionRect *rect) {
	if (rect == NULL || rect->top_cy < 0 || rect->bottom_cy < rect->top_cy ||
			rect->top_cy >= E.numrows) {
		return 0;
	}
	int bottom = rect->bottom_cy;
	if (bottom >= E.numrows) {
		bottom = E.numrows - 1;
	}
	return bottom - rect->top_cy + 1;
}

static void editorColumnSelectionMoveCursorToRx(int cy, int rx) {
	if (cy < 0) {
		cy = 0;
	}
	if (cy > E.numrows) {
		cy = E.numrows;
	}

	int cx = 0;
	if (cy < E.numrows) {
		cx = editorRowRxToCx(&E.rows[cy], rx);
		if (cx > E.rows[cy].size) {
			cx = E.rows[cy].size;
		}
		cx = editorRowClampCxToClusterBoundary(&E.rows[cy], cx);
	}

	size_t offset = 0;
	if (editorBufferPosToOffset(cy, cx, &offset)) {
		(void)editorSyncCursorFromOffsetByteBoundary(offset);
	}
}

// Apply one document edit that rebuilds every affected row with its own column
// slice replaced. Width-zero rect with empty inserted text is a no-op.
static int editorColumnSelectionApplyMultiRowEdit(const struct editorColumnSelectionRect *rect,
		const struct editorColumnInsertLine *insert_lines, int insert_line_count,
		int repeat_insert, enum editorEditKind kind, int cursor_cy, size_t *cursor_insert_len_out) {
	if (rect == NULL || rect->top_cy < 0 || rect->top_cy > E.numrows ||
			rect->bottom_cy < rect->top_cy || insert_lines == NULL || insert_line_count <= 0) {
		return 0;
	}

	int top = rect->top_cy;
	int bottom = rect->bottom_cy;
	if (bottom >= E.numrows) {
		bottom = E.numrows - 1;
	}
	if (top > bottom) {
		return 0;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	size_t ignored = 0;
	if (!editorBufferLineByteRange(top, &start_offset, &ignored) ||
			!editorBufferLineByteRange(bottom, &ignored, &end_offset) ||
			end_offset < start_offset) {
		return 0;
	}

	size_t old_len = end_offset - start_offset;

	size_t new_len = 0;
	int changed = 0;
	size_t cursor_insert_len = 0;
	for (int r = top; r <= bottom; r++) {
		int cs = 0;
		int ce = 0;
		(void)editorColumnSelectionRowSpan(r, rect->left_rx, rect->right_rx, &cs, &ce);
		size_t row_size = (size_t)E.rows[r].size;
		int insert_idx = repeat_insert ? 0 : r - top;
		if (insert_idx < 0 || insert_idx >= insert_line_count) {
			return 0;
		}
		size_t insert_len = insert_lines[insert_idx].len;
		size_t add = (size_t)cs + insert_len + (row_size - (size_t)ce);
		if (r > top) {
			add += 1;
		}
		if (new_len > ROTIDE_MAX_TEXT_BYTES - add) {
			editorSetOperationTooLargeStatus();
			return -1;
		}
		new_len += add;
		if (ce > cs || insert_len > 0) {
			changed = 1;
		}
		if (r == cursor_cy) {
			cursor_insert_len = insert_len;
		}
	}
	if (!changed) {
		return 0;
	}

	char *new_text = NULL;
	if (new_len > 0) {
		new_text = editorMalloc(new_len);
		if (new_text == NULL) {
			editorSetAllocFailureStatus();
			return -1;
		}
	}

	size_t pos = 0;
	for (int r = top; r <= bottom; r++) {
		int cs = 0;
		int ce = 0;
		(void)editorColumnSelectionRowSpan(r, rect->left_rx, rect->right_rx, &cs, &ce);
		size_t row_size = (size_t)E.rows[r].size;
		int insert_idx = repeat_insert ? 0 : r - top;
		const char *insert_text = insert_lines[insert_idx].text;
		size_t insert_len = insert_lines[insert_idx].len;
		if (r > top) {
			new_text[pos++] = '\n';
		}
		memcpy(new_text + pos, E.rows[r].chars, (size_t)cs);
		pos += (size_t)cs;
		if (insert_len > 0) {
			memcpy(new_text + pos, insert_text, insert_len);
			pos += insert_len;
		}
		size_t tail = row_size - (size_t)ce;
		if (tail > 0) {
			memcpy(new_text + pos, E.rows[r].chars + ce, tail);
			pos += tail;
		}
	}

	int dirty_delta = (top != bottom) ? (bottom - top + 1) : 1;
	if (dirty_delta < 1) {
		dirty_delta = 1;
	}
	if (E.dirty > INT_MAX - dirty_delta) {
		free(new_text);
		editorSetOperationTooLargeStatus();
		return -1;
	}

	size_t before_cursor_offset = start_offset;
	(void)editorBufferPosToOffset(E.cy, E.cx, &before_cursor_offset);

	size_t after_cursor_offset = start_offset;
	if (cursor_cy < top || cursor_cy > bottom) {
		cursor_cy = top;
	}
	size_t row_offset = 0;
	for (int r = top; r <= bottom; r++) {
		int cs = 0;
		int ce = 0;
		(void)editorColumnSelectionRowSpan(r, rect->left_rx, rect->right_rx, &cs, &ce);
		int insert_idx = repeat_insert ? 0 : r - top;
		size_t insert_len = insert_lines[insert_idx].len;
		if (r == cursor_cy) {
			after_cursor_offset = start_offset + row_offset + (size_t)cs + insert_len;
			break;
		}
		row_offset += (size_t)E.rows[r].size - (size_t)(ce - cs) + insert_len + 1;
	}

	struct editorDocumentEdit edit = {
		.kind = kind,
		.start_offset = start_offset,
		.old_len = old_len,
		.new_text = new_text != NULL ? new_text : "",
		.new_len = new_len,
		.before_cursor_offset = before_cursor_offset,
		.after_cursor_offset = after_cursor_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};

	int rc = editorApplyDocumentEdit(&edit);
	free(new_text);
	if (!rc) {
		return -1;
	}
	if (cursor_insert_len_out != NULL) {
		*cursor_insert_len_out = cursor_insert_len;
	}
	return 1;
}

static int editorColumnSelectionApplyRepeatedText(const struct editorColumnSelectionRect *rect,
		const char *insert_text, size_t insert_len, enum editorEditKind kind, int cursor_cy,
		size_t *cursor_insert_len_out) {
	struct editorColumnInsertLine insert_line = {
		.text = insert_text != NULL ? insert_text : "",
		.len = insert_len
	};
	return editorColumnSelectionApplyMultiRowEdit(rect, &insert_line, 1, 1, kind, cursor_cy,
			cursor_insert_len_out);
}

int editorColumnSelectionDelete(void) {
	struct editorColumnSelectionRect rect;
	if (!editorColumnSelectionGetRect(&rect)) {
		return 0;
	}
	int rc = editorColumnSelectionApplyRepeatedText(&rect, NULL, 0, EDITOR_EDIT_DELETE_TEXT, E.cy,
			NULL);
	if (rc <= 0) {
		if (rc == 0) {
			editorColumnSelectionClear();
		}
		return rc;
	}
	editorColumnSelectionClear();
	return 1;
}

int editorColumnSelectionDeleteForward(void) {
	if (!E.column_select_active) {
		return 0;
	}
	struct editorColumnSelectionRect rect;
	if (!editorColumnSelectionGetRect(&rect)) {
		return 0;
	}
	if (rect.left_rx == rect.right_rx) {
		rect.right_rx += 1;
	}
	int saved_cy = E.cy;
	int top = rect.top_cy;
	int left_rx = rect.left_rx;
	int rc = editorColumnSelectionApplyRepeatedText(&rect, NULL, 0, EDITOR_EDIT_DELETE_TEXT,
			saved_cy, NULL);
	if (rc <= 0) {
		if (rc == 0) {
			editorColumnSelectionClear();
		}
		return rc;
	}

	E.column_select_active = 1;
	E.column_select_anchor_cy = top;
	E.column_select_anchor_rx = left_rx;
	E.column_select_cursor_rx = left_rx;
	editorColumnSelectionMoveCursorToRx(saved_cy, left_rx);
	return 1;
}

int editorColumnSelectionInsertChar(int c) {
	if (!E.column_select_active) {
		return 0;
	}
	struct editorColumnSelectionRect rect;
	if (!editorColumnSelectionGetRect(&rect)) {
		return 0;
	}
	if (c < 0x20 || c >= 0x7f) {
		// Only ASCII printable for now.
		return 0;
	}
	char ch = (char)c;
	int saved_anchor_cy = E.column_select_anchor_cy;
	int saved_cursor_cy = E.cy;
	int left_rx = rect.left_rx;
	size_t cursor_insert_len = 0;

	int rc = editorColumnSelectionApplyRepeatedText(&rect, &ch, 1, EDITOR_EDIT_INSERT_TEXT,
			saved_cursor_cy, &cursor_insert_len);
	if (rc <= 0) {
		return rc;
	}

	// Preserve the row span; collapse to width-zero (multi-cursor) at left_rx + 1.
	E.column_select_active = 1;
	E.column_select_anchor_cy = saved_anchor_cy;
	E.column_select_anchor_rx = left_rx + (int)cursor_insert_len;
	E.column_select_cursor_rx = left_rx + (int)cursor_insert_len;
	editorColumnSelectionMoveCursorToRx(saved_cursor_cy, left_rx + (int)cursor_insert_len);
	return 1;
}

static int editorColumnSelectionSplitLines(const char *text, size_t len,
		struct editorColumnInsertLine **lines_out, int *line_count_out) {
	if (text == NULL || lines_out == NULL || line_count_out == NULL) {
		return 0;
	}
	*lines_out = NULL;
	*line_count_out = 0;

	int line_count = 1;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			if (line_count == INT_MAX) {
				editorSetOperationTooLargeStatus();
				return -1;
			}
			line_count++;
		}
	}

	size_t alloc_size = 0;
	if (!editorSizeMul(sizeof(struct editorColumnInsertLine), (size_t)line_count, &alloc_size)) {
		editorSetOperationTooLargeStatus();
		return -1;
	}
	struct editorColumnInsertLine *lines = editorMalloc(alloc_size);
	if (lines == NULL) {
		editorSetAllocFailureStatus();
		return -1;
	}

	int line_idx = 0;
	size_t line_start = 0;
	for (size_t i = 0; i <= len; i++) {
		if (i == len || text[i] == '\n') {
			lines[line_idx].text = text + line_start;
			lines[line_idx].len = i - line_start;
			line_idx++;
			line_start = i + 1;
		}
	}

	*lines_out = lines;
	*line_count_out = line_count;
	return 1;
}

int editorColumnSelectionPasteText(const char *text, size_t len) {
	if (!E.column_select_active) {
		return 0;
	}
	if (text == NULL || len == 0) {
		return 0;
	}

	struct editorColumnSelectionRect rect;
	if (!editorColumnSelectionGetRect(&rect)) {
		return 0;
	}

	int row_count = editorColumnSelectionRowCount(&rect);
	if (row_count <= 0) {
		return 0;
	}

	struct editorColumnInsertLine *lines = NULL;
	int line_count = 0;
	int split = editorColumnSelectionSplitLines(text, len, &lines, &line_count);
	if (split <= 0) {
		return split;
	}

	int repeat_insert = line_count == 1;
	if (!repeat_insert && line_count != row_count) {
		free(lines);
		editorSetStatusMsg("Clipboard lines do not match cursors");
		return 0;
	}

	int saved_anchor_cy = E.column_select_anchor_cy;
	int saved_cursor_cy = E.cy;
	int left_rx = rect.left_rx;
	size_t cursor_insert_len = 0;
	int rc = editorColumnSelectionApplyMultiRowEdit(&rect, lines, line_count, repeat_insert,
			EDITOR_EDIT_INSERT_TEXT, saved_cursor_cy, &cursor_insert_len);
	free(lines);
	if (rc <= 0) {
		return rc;
	}

	E.column_select_active = 1;
	E.column_select_anchor_cy = saved_anchor_cy;
	E.column_select_anchor_rx = left_rx + (int)cursor_insert_len;
	E.column_select_cursor_rx = left_rx + (int)cursor_insert_len;
	editorColumnSelectionMoveCursorToRx(saved_cursor_cy, left_rx + (int)cursor_insert_len);
	return 1;
}

int editorColumnSelectionBackspace(void) {
	if (!E.column_select_active) {
		return 0;
	}
	struct editorColumnSelectionRect rect;
	if (!editorColumnSelectionGetRect(&rect)) {
		return 0;
	}
	if (rect.left_rx == rect.right_rx) {
		// Zero-width rect: shrink left by one column to make a 1-col-wide delete.
		if (rect.left_rx == 0) {
			editorColumnSelectionClear();
			return 0;
		}
		rect.left_rx -= 1;
	}
	int saved_cy = E.cy;
	int top = rect.top_cy;
	int left_rx = rect.left_rx;
	int rc = editorColumnSelectionApplyRepeatedText(&rect, NULL, 0, EDITOR_EDIT_DELETE_TEXT,
			saved_cy, NULL);
	if (rc <= 0) {
		if (rc == 0) {
			editorColumnSelectionClear();
		}
		return rc;
	}

	// Collapse to width-zero at left_rx.
	E.column_select_active = 1;
	E.column_select_anchor_cy = top;
	E.column_select_anchor_rx = left_rx;
	E.column_select_cursor_rx = left_rx;
	editorColumnSelectionMoveCursorToRx(saved_cy, left_rx);
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
