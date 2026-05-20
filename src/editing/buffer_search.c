#include "editing/buffer_search.h"

#include "editing/document_position.h"
#include "editing/text_source.h"
#include "language/syntax.h"
#include "support/size_utils.h"
#include "text/document.h"

#include <stdlib.h>
#include <string.h>

static int editorTextSourceFindForwardInRange(const struct editorTextSource *source,
                                              size_t start_byte, size_t end_byte, const char *query,
                                              int from_idx, int *out_idx) {
	if (source == NULL || query == NULL || out_idx == NULL || from_idx < 0 ||
	    end_byte < start_byte) {
		return 0;
	}

	size_t line_len = end_byte - start_byte;
	size_t from = 0;
	if (!editorIntToSize(from_idx, &from) || from > line_len) {
		return 0;
	}

	size_t text_len = 0;
	char *text = editorTextSourceDupRange(source, start_byte, end_byte, &text_len);
	if (text == NULL) {
		return 0;
	}

	const char *match = strstr(text + from, query);
	if (match == NULL) {
		free(text);
		return 0;
	}

	*out_idx = (int)(match - text);
	free(text);
	return 1;
}

static int editorTextSourceFindBackwardInRange(const struct editorTextSource *source,
                                               size_t start_byte, size_t end_byte,
                                               const char *query, int before_idx, int *out_idx) {
	if (source == NULL || query == NULL || out_idx == NULL || end_byte < start_byte) {
		return 0;
	}

	size_t line_len = end_byte - start_byte;
	size_t before = 0;
	if (before_idx < 0) {
		before = 0;
	} else if (!editorIntToSize(before_idx, &before)) {
		return 0;
	}
	if (before > line_len) {
		before = line_len;
	}

	size_t text_len = 0;
	char *text = editorTextSourceDupRange(source, start_byte, end_byte, &text_len);
	if (text == NULL) {
		return 0;
	}

	int last = -1;
	const char *scan = text;
	while (1) {
		const char *match = strstr(scan, query);
		if (match == NULL) {
			break;
		}

		int idx = (int)(match - text);
		if (idx >= (int)before) {
			break;
		}
		last = idx;
		scan = match + 1;
	}

	free(text);
	if (last == -1) {
		return 0;
	}
	*out_idx = last;
	return 1;
}

int editorBufferFindForward(const char *query, int start_row, int start_col, int *out_row,
                            int *out_col) {
	if (query == NULL || out_row == NULL || out_col == NULL || E.numrows == 0) {
		return 0;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	if (start_row < 0 || start_row >= E.numrows) {
		start_row = 0;
		start_col = -1;
	}

	size_t line_start = 0;
	size_t line_end = 0;
	int col = 0;
	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
	    editorTextSourceFindForwardInRange(&source, line_start, line_end, query, start_col + 1,
	                                       &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	for (int offset = 1; offset < E.numrows; offset++) {
		int row = (start_row + offset) % E.numrows;
		if (editorBufferLineByteRange(row, &line_start, &line_end) &&
		    editorTextSourceFindForwardInRange(&source, line_start, line_end, query, 0,
		                                       &col)) {
			*out_row = row;
			*out_col = col;
			return 1;
		}
	}

	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
	    editorTextSourceFindForwardInRange(&source, line_start, line_end, query, 0, &col) &&
	    col <= start_col) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	return 0;
}

int editorBufferFindBackward(const char *query, int start_row, int start_col, int *out_row,
                             int *out_col) {
	if (query == NULL || out_row == NULL || out_col == NULL || E.numrows == 0) {
		return 0;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	if (start_row < 0 || start_row >= E.numrows) {
		start_row = E.numrows - 1;
		start_col = (int)editorDocumentLineLength(E.document, start_row);
	}

	size_t line_start = 0;
	size_t line_end = 0;
	int col = 0;
	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
	    editorTextSourceFindBackwardInRange(&source, line_start, line_end, query, start_col,
	                                        &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	for (int offset = 1; offset < E.numrows; offset++) {
		int row = (start_row - offset + E.numrows) % E.numrows;
		if (editorBufferLineByteRange(row, &line_start, &line_end) &&
		    editorTextSourceFindBackwardInRange(
		            &source, line_start, line_end, query,
		            (int)editorDocumentLineLength(E.document, row) + 1, &col)) {
			*out_row = row;
			*out_col = col;
			return 1;
		}
	}

	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
	    editorTextSourceFindBackwardInRange(&source, line_start, line_end, query,
	                                        (int)(line_end - line_start + 1), &col) &&
	    col > start_col) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	return 0;
}
