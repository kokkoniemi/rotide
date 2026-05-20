#include "text/document.h"

#include "editing/document_bridge.h"
#include "support/alloc.h"

#include <stdlib.h>
#include <string.h>

static const char *editorDocumentTextSourceRead(const struct editorTextSource *source,
                                                size_t byte_index, uint32_t *bytes_read) {
	const struct editorDocument *document = source != NULL ? source->context : NULL;
	return editorDocumentRead(document, byte_index, bytes_read);
}

void editorDocumentInit(struct editorDocument *document) {
	if (document == NULL) {
		return;
	}
	/* Best-effort init. On OOM the tree's root/add_buf are left NULL and the
	 * first ReplaceRange / Append call retries the allocation via the
	 * Ensure* helpers inside text_tree.c. Callers that need eager failure
	 * detection should follow up with a length query or use the explicit
	 * ResetFrom* paths which do propagate OOM.
	 */
	(void)editorTextTreeInit(&document->tree);
}

void editorDocumentFree(struct editorDocument *document) {
	if (document == NULL) {
		return;
	}
	editorTextTreeFree(&document->tree);
}

int editorDocumentResetFromString(struct editorDocument *document, const char *text, size_t len) {
	if (document == NULL) {
		return 0;
	}
	if (!editorTextTreeResetFromString(&document->tree, text, len)) {
		return 0;
	}
	editorTextTreeStatsRecordFullRebuild();
	return 1;
}

int editorDocumentResetFromDocument(struct editorDocument *document,
                                    const struct editorDocument *source) {
	if (document == NULL || source == NULL) {
		return 0;
	}

	struct editorTextSource text_source = {.read = editorDocumentTextSourceRead,
	                                       .context = source,
	                                       .length = editorDocumentLength(source)};
	return editorDocumentResetFromTextSource(document, &text_source);
}

int editorDocumentResetFromTextSource(struct editorDocument *document,
                                      const struct editorTextSource *source) {
	if (document == NULL || source == NULL || source->read == NULL) {
		return 0;
	}

	if (!editorTextTreeResetFromTextSource(&document->tree, source)) {
		return 0;
	}
	editorTextTreeStatsRecordFullRebuild();
	return 1;
}

size_t editorDocumentLength(const struct editorDocument *document) {
	return document != NULL ? editorTextTreeLength(&document->tree) : 0;
}

const char *editorDocumentRead(const struct editorDocument *document, size_t byte_index,
                               uint32_t *bytes_read) {
	if (document == NULL) {
		if (bytes_read != NULL) {
			*bytes_read = 0;
		}
		return NULL;
	}
	return editorTextTreeRead(&document->tree, byte_index, bytes_read);
}

int editorDocumentCopyRange(const struct editorDocument *document, size_t start_byte,
                            size_t end_byte, char *dst) {
	if (document == NULL) {
		return 0;
	}
	return editorTextTreeCopyRange(&document->tree, start_byte, end_byte, dst);
}

char *editorDocumentDupRange(const struct editorDocument *document, size_t start_byte,
                             size_t end_byte, size_t *len_out) {
	if (document == NULL) {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	return editorTextTreeDupRange(&document->tree, start_byte, end_byte, len_out);
}

int editorDocumentReplaceRange(struct editorDocument *document, size_t start_byte, size_t old_len,
                               const char *new_text, size_t new_len) {
	if (document == NULL) {
		return 0;
	}
	if (!editorTextTreeReplaceRange(&document->tree, start_byte, old_len, new_text, new_len)) {
		return 0;
	}
	editorTextTreeStatsRecordIncrementalUpdate();
	return 1;
}

int editorDocumentReserveInsertCapacity(struct editorDocument *document, size_t additional_bytes) {
	if (document == NULL) {
		return 0;
	}
	return editorTextTreeReserveAddBufCapacity(&document->tree, additional_bytes);
}

int editorDocumentLineCount(const struct editorDocument *document) {
	if (document == NULL) {
		return 0;
	}
	const struct editorTextSummary *summary = editorTextTreeSummary(&document->tree);
	if (summary == NULL || summary->bytes == 0) {
		return 0;
	}
	/* A document ending in '\n' has zero bytes in its last "line"; that
	 * trailing empty line is not counted, matching the prior behaviour.
	 */
	return summary->newlines + (summary->last_line_bytes > 0 ? 1 : 0);
}

int editorDocumentLineStartByte(const struct editorDocument *document, int line_idx,
                                size_t *start_byte_out) {
	if (document == NULL || start_byte_out == NULL || line_idx < 0) {
		return 0;
	}
	if (line_idx >= editorDocumentLineCount(document)) {
		return 0;
	}
	return editorTextTreeLocateLine(&document->tree, line_idx, start_byte_out);
}

int editorDocumentLineEndByte(const struct editorDocument *document, int line_idx,
                              size_t *end_byte_out) {
	if (document == NULL || end_byte_out == NULL || line_idx < 0) {
		return 0;
	}
	const struct editorTextSummary *summary = editorTextTreeSummary(&document->tree);
	if (summary == NULL || line_idx >= editorDocumentLineCount(document)) {
		return 0;
	}
	if (line_idx + 1 <= summary->newlines) {
		size_t next_start = 0;
		if (!editorTextTreeLocateLine(&document->tree, line_idx + 1, &next_start) ||
		    next_start == 0) {
			return 0;
		}
		*end_byte_out = next_start - 1;
		return 1;
	}
	*end_byte_out = summary->bytes;
	return 1;
}

int editorDocumentLineIndexForByteOffset(const struct editorDocument *document, size_t byte_offset,
                                         int *line_idx_out) {
	if (document == NULL || line_idx_out == NULL) {
		return 0;
	}
	size_t len = editorDocumentLength(document);
	if (len == 0 || byte_offset > len) {
		return 0;
	}
	if (byte_offset == len) {
		byte_offset--;
	}
	return editorTextTreeLineForByte(&document->tree, byte_offset, line_idx_out);
}

int editorDocumentPositionToByteOffset(const struct editorDocument *document, int line_idx,
                                       size_t column, size_t *byte_offset_out) {
	if (document == NULL || byte_offset_out == NULL || line_idx < 0) {
		return 0;
	}

	int line_count = editorDocumentLineCount(document);
	size_t len = editorDocumentLength(document);
	if (line_count == 0) {
		if (line_idx == 0 && column == 0) {
			*byte_offset_out = 0;
			return 1;
		}
		return 0;
	}

	if (line_idx == line_count) {
		if (column != 0) {
			return 0;
		}
		*byte_offset_out = len;
		return 1;
	}
	if (line_idx > line_count) {
		return 0;
	}

	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) ||
	    !editorDocumentLineEndByte(document, line_idx, &end) || column > end - start) {
		return 0;
	}
	*byte_offset_out = start + column;
	return 1;
}

size_t editorDocumentMaxLineBytes(const struct editorDocument *document) {
	if (document == NULL) {
		return 0;
	}
	const struct editorTextSummary *summary = editorTextTreeSummary(&document->tree);
	if (summary == NULL || summary->bytes == 0) {
		return 0;
	}
	size_t max_bytes = summary->max_line_bytes;
	if (summary->first_line_bytes > max_bytes) {
		max_bytes = summary->first_line_bytes;
	}
	if (summary->last_line_bytes > max_bytes) {
		max_bytes = summary->last_line_bytes;
	}
	return max_bytes;
}

size_t editorDocumentLineLength(const struct editorDocument *document, int line_idx) {
	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) ||
	    !editorDocumentLineEndByte(document, line_idx, &end)) {
		return 0;
	}
	return end - start;
}

const char *editorDocumentLineBytes(const struct editorDocument *document, int line_idx,
                                    size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) ||
	    !editorDocumentLineEndByte(document, line_idx, &end)) {
		return NULL;
	}
	size_t len = end - start;
	if (len_out != NULL) {
		*len_out = len;
	}
	if (len == 0) {
		return "";
	}
	uint32_t avail = 0;
	const char *ptr = editorTextTreeRead(&document->tree, start, &avail);
	if (ptr == NULL || (size_t)avail < len) {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	return ptr;
}

char *editorDocumentLineDup(const struct editorDocument *document, int line_idx, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) ||
	    !editorDocumentLineEndByte(document, line_idx, &end)) {
		return NULL;
	}
	return editorDocumentDupRange(document, start, end, len_out);
}

int editorDocumentLineView(const struct editorDocument *document, int line_idx,
                           struct editorLineView *view_out) {
	if (view_out == NULL) {
		return 0;
	}
	view_out->data = NULL;
	view_out->size = 0;
	view_out->owned = NULL;

	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) ||
	    !editorDocumentLineEndByte(document, line_idx, &end)) {
		return 0;
	}
	size_t len = end - start;
	if (len == 0) {
		view_out->data = "";
		view_out->size = 0;
		return 1;
	}
	uint32_t avail = 0;
	const char *ptr = editorTextTreeRead(&document->tree, start, &avail);
	if (ptr != NULL && (size_t)avail >= len) {
		view_out->data = ptr;
		view_out->size = (int)len;
		return 1;
	}
	size_t dup_len = 0;
	char *dup = editorDocumentDupRange(document, start, end, &dup_len);
	if (dup == NULL) {
		return 0;
	}
	view_out->data = dup;
	view_out->owned = dup;
	view_out->size = (int)dup_len;
	return 1;
}

void editorLineViewRelease(struct editorLineView *view) {
	if (view == NULL) {
		return;
	}
	free(view->owned);
	view->data = NULL;
	view->owned = NULL;
	view->size = 0;
}

int editorDocumentByteOffsetToPosition(const struct editorDocument *document, size_t byte_offset,
                                       int *line_idx_out, size_t *column_out) {
	if (document == NULL || line_idx_out == NULL || column_out == NULL) {
		return 0;
	}

	size_t len = editorDocumentLength(document);
	if (byte_offset > len) {
		return 0;
	}
	int line_count = editorDocumentLineCount(document);
	if (line_count == 0) {
		if (byte_offset == 0) {
			*line_idx_out = 0;
			*column_out = 0;
			return 1;
		}
		return 0;
	}
	if (byte_offset == len) {
		*line_idx_out = line_count;
		*column_out = 0;
		return 1;
	}

	int line_idx = -1;
	if (!editorDocumentLineIndexForByteOffset(document, byte_offset, &line_idx)) {
		return 0;
	}
	size_t start = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) || byte_offset < start) {
		return 0;
	}
	*line_idx_out = line_idx;
	*column_out = byte_offset - start;
	return 1;
}
