#include "editing/selection.h"

#include "editing/buffer_core.h"
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
