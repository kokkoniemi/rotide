#include "editing/document_position.h"

#include "editing/document_bridge.h"
#include "support/size_utils.h"
#include "text/document.h"
#include "text/row.h"

static int editorActiveDocumentCurrent(const struct editorDocument **document_out) {
	if (document_out == NULL || !editorTabKindSupportsDocument(E.tab_kind) ||
	    !editorDocumentEnsureActiveCurrent() || E.document == NULL) {
		return 0;
	}
	*document_out = E.document;
	return 1;
}

int editorBufferPosToOffset(int cy, int cx, size_t *offset_out) {
	if (cy < 0 || cy > E.numrows || cx < 0 || offset_out == NULL) {
		return 0;
	}
	const struct editorDocument *document = NULL;
	if (!editorActiveDocumentCurrent(&document)) {
		return 0;
	}
	size_t offset = 0;
	size_t column = 0;
	if (!editorIntToSize(cx, &column) ||
	    !editorDocumentPositionToByteOffset(document, cy, column, &offset) ||
	    offset > ROTIDE_MAX_TEXT_BYTES) {
		return 0;
	}
	*offset_out = offset;
	return 1;
}

int editorBufferOffsetToPos(size_t offset, int *cy_out, int *cx_out) {
	if (cy_out == NULL || cx_out == NULL) {
		return 0;
	}

	const struct editorDocument *document = NULL;
	if (!editorActiveDocumentCurrent(&document)) {
		return 0;
	}
	int line_idx = 0;
	size_t column = 0;
	if (!editorDocumentByteOffsetToPosition(document, offset, &line_idx, &column)) {
		return 0;
	}
	int cx = 0;
	if (!editorSizeToInt(column, &cx)) {
		return 0;
	}
	*cy_out = line_idx;
	*cx_out = cx;
	return 1;
}

int editorBufferLineByteRange(int row_idx, size_t *start_byte_out, size_t *end_byte_out) {
	if (start_byte_out == NULL || end_byte_out == NULL || row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	const struct editorDocument *document = NULL;
	if (!editorActiveDocumentCurrent(&document)) {
		return 0;
	}
	return editorDocumentLineStartByte(document, row_idx, start_byte_out) &&
	       editorDocumentLineEndByte(document, row_idx, end_byte_out);
}

static int editorCursorPositionForOffset(const struct editorDocument *document, int numrows,
                                         size_t offset, int *cy_out, int *cx_out,
                                         size_t *normalized_offset_out) {
	size_t document_len = 0;
	int cy = 0;
	size_t column = 0;
	int cx = 0;

	if (document == NULL || cy_out == NULL || cx_out == NULL || normalized_offset_out == NULL ||
	    numrows < 0) {
		return 0;
	}

	document_len = editorDocumentLength(document);
	if (offset > document_len) {
		offset = document_len;
	}
	if (!editorDocumentByteOffsetToPosition(document, offset, &cy, &column)) {
		return 0;
	}
	if (!editorSizeToInt(column, &cx)) {
		return 0;
	}

	if (cy < 0) {
		cy = 0;
	}
	if (cy > numrows) {
		cy = numrows;
	}

	if (cy < numrows) {
		size_t line_start = 0;
		size_t cx_size = 0;
		if (cx < 0) {
			cx = 0;
		}
		struct editorLineView line = {0};
		if (editorDocumentLineView(document, cy, &line)) {
			if (cx > line.size) {
				cx = line.size;
			}
			cx = editorBytesClampCxToClusterBoundary(line.data, line.size, cx);
			if (cx < 0) {
				cx = 0;
			}
			if (cx > line.size) {
				cx = line.size;
			}
			editorLineViewRelease(&line);
		}
		if (!editorDocumentLineStartByte(document, cy, &line_start) ||
		    !editorIntToSize(cx, &cx_size) ||
		    !editorSizeAdd(line_start, cx_size, &offset)) {
			return 0;
		}
	} else {
		cx = 0;
		offset = document_len;
	}

	*cy_out = cy;
	*cx_out = cx;
	*normalized_offset_out = offset;
	return 1;
}

int editorSyncCursorFromOffset(size_t target_offset) {
	size_t normalized_offset = 0;
	int new_cy = 0;
	int new_cx = 0;

	if (E.document == NULL ||
	    !editorCursorPositionForOffset(E.document, E.numrows, target_offset, &new_cy, &new_cx,
	                                   &normalized_offset)) {
		return 0;
	}

	E.cursor_offset = normalized_offset;
	E.cy = new_cy;
	E.cx = new_cx;
	return 1;
}

int editorSyncCursorFromOffsetByteBoundary(size_t target_offset) {
	size_t document_len = 0;
	size_t normalized_offset = 0;
	int new_cy = 0;
	size_t column = 0;
	int new_cx = 0;

	if (E.document == NULL) {
		return 0;
	}

	document_len = editorDocumentLength(E.document);
	if (target_offset > document_len) {
		target_offset = document_len;
	}
	if (!editorDocumentByteOffsetToPosition(E.document, target_offset, &new_cy, &column) ||
	    !editorSizeToInt(column, &new_cx)) {
		return 0;
	}

	if (new_cy < 0) {
		new_cy = 0;
	}
	if (new_cy > E.numrows) {
		new_cy = E.numrows;
	}

	if (new_cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, new_cy, &line)) {
			return 0;
		}
		if (new_cx < 0) {
			new_cx = 0;
		}
		if (new_cx > line.size) {
			new_cx = line.size;
		}
		new_cx = editorBytesClampCxToCharBoundary(line.data, line.size, new_cx);
		if (new_cx < 0) {
			new_cx = 0;
		}
		if (new_cx > line.size) {
			new_cx = line.size;
		}
		editorLineViewRelease(&line);

		size_t line_start = 0;
		size_t cx_size = 0;
		if (!editorDocumentLineStartByte(E.document, new_cy, &line_start) ||
		    !editorIntToSize(new_cx, &cx_size) ||
		    !editorSizeAdd(line_start, cx_size, &normalized_offset)) {
			return 0;
		}
	} else {
		new_cx = 0;
		normalized_offset = document_len;
	}

	E.cursor_offset = normalized_offset;
	E.cy = new_cy;
	E.cx = new_cx;
	return 1;
}
