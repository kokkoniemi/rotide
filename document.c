#include "document.h"

#include "alloc.h"
#include "size_utils.h"
#include <stdlib.h>

static int editorDocumentEnsureLineCapacity(struct editorDocument *document, int needed) {
	if (document == NULL || needed < 0) {
		return 0;
	}
	if (needed <= document->line_capacity) {
		return 1;
	}

	int new_capacity = document->line_capacity > 0 ? document->line_capacity : 4;
	while (new_capacity < needed) {
		new_capacity *= 2;
	}

	size_t cap_size = 0;
	size_t bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(*document->line_starts), cap_size, &bytes)) {
		return 0;
	}

	size_t *grown = editorRealloc(document->line_starts, bytes);
	if (grown == NULL) {
		return 0;
	}
	document->line_starts = grown;
	document->line_capacity = new_capacity;
	return 1;
}

static int editorDocumentRebuildLineIndex(struct editorDocument *document) {
	if (document == NULL) {
		return 0;
	}

	document->line_count = 0;
	size_t len = editorDocumentLength(document);
	if (len == 0) {
		return 1;
	}
	if (!editorDocumentEnsureLineCapacity(document, 1)) {
		return 0;
	}

	document->line_starts[document->line_count++] = 0;

	size_t offset = 0;
	while (offset < len) {
		uint32_t chunk_len = 0;
		const char *chunk = editorDocumentRead(document, offset, &chunk_len);
		if (chunk == NULL || chunk_len == 0) {
			break;
		}
		for (uint32_t i = 0; i < chunk_len; i++) {
			if (chunk[i] != '\n') {
				continue;
			}
			size_t next_start = offset + i + 1;
			if (next_start >= len) {
				continue;
			}
			if (!editorDocumentEnsureLineCapacity(document, document->line_count + 1)) {
				return 0;
			}
			document->line_starts[document->line_count++] = next_start;
		}
		offset += chunk_len;
	}

	return 1;
}

static int editorDocumentReadByte(const struct editorDocument *document, size_t byte_index,
		char *byte_out) {
	if (document == NULL || byte_out == NULL) {
		return 0;
	}
	uint32_t chunk_len = 0;
	const char *chunk = editorDocumentRead(document, byte_index, &chunk_len);
	if (chunk == NULL || chunk_len == 0) {
		return 0;
	}
	*byte_out = chunk[0];
	return 1;
}

static const char *editorDocumentTextSourceRead(const struct editorTextSource *source,
		size_t byte_index, uint32_t *bytes_read) {
	const struct editorDocument *document = source != NULL ? source->context : NULL;
	return editorDocumentRead(document, byte_index, bytes_read);
}

void editorDocumentInit(struct editorDocument *document) {
	if (document == NULL) {
		return;
	}
	editorRopeInit(&document->rope);
	document->line_starts = NULL;
	document->line_count = 0;
	document->line_capacity = 0;
}

void editorDocumentFree(struct editorDocument *document) {
	if (document == NULL) {
		return;
	}
	editorRopeFree(&document->rope);
	free(document->line_starts);
	document->line_starts = NULL;
	document->line_count = 0;
	document->line_capacity = 0;
}

int editorDocumentResetFromString(struct editorDocument *document, const char *text, size_t len) {
	if (document == NULL) {
		return 0;
	}
	if (!editorRopeResetFromString(&document->rope, text, len)) {
		return 0;
	}
	return editorDocumentRebuildLineIndex(document);
}

int editorDocumentResetFromDocument(struct editorDocument *document,
		const struct editorDocument *source) {
	if (document == NULL || source == NULL) {
		return 0;
	}

	struct editorTextSource text_source = {
		.read = editorDocumentTextSourceRead,
		.context = source,
		.length = editorDocumentLength(source)
	};
	return editorDocumentResetFromTextSource(document, &text_source);
}

int editorDocumentResetFromTextSource(struct editorDocument *document,
		const struct editorTextSource *source) {
	if (document == NULL || source == NULL || source->read == NULL) {
		return 0;
	}

	struct editorDocument rebuilt;
	editorDocumentInit(&rebuilt);

	size_t offset = 0;
	while (offset < source->length) {
		uint32_t chunk_len = 0;
		const char *chunk = source->read(source, offset, &chunk_len);
		if (chunk == NULL || chunk_len == 0) {
			editorDocumentFree(&rebuilt);
			return 0;
		}

		size_t remaining = source->length - offset;
		if ((size_t)chunk_len > remaining ||
				!editorRopeAppend(&rebuilt.rope, chunk, (size_t)chunk_len)) {
			editorDocumentFree(&rebuilt);
			return 0;
		}
		offset += (size_t)chunk_len;
	}

	if (!editorDocumentRebuildLineIndex(&rebuilt)) {
		editorDocumentFree(&rebuilt);
		return 0;
	}

	editorDocumentFree(document);
	*document = rebuilt;
	return 1;
}

size_t editorDocumentLength(const struct editorDocument *document) {
	return document != NULL ? editorRopeLength(&document->rope) : 0;
}

const char *editorDocumentRead(const struct editorDocument *document, size_t byte_index,
		uint32_t *bytes_read) {
	if (document == NULL) {
		if (bytes_read != NULL) {
			*bytes_read = 0;
		}
		return NULL;
	}
	return editorRopeRead(&document->rope, byte_index, bytes_read);
}

int editorDocumentCopyRange(const struct editorDocument *document, size_t start_byte,
		size_t end_byte, char *dst) {
	if (document == NULL) {
		return 0;
	}
	return editorRopeCopyRange(&document->rope, start_byte, end_byte, dst);
}

char *editorDocumentDupRange(const struct editorDocument *document, size_t start_byte,
		size_t end_byte, size_t *len_out) {
	if (document == NULL) {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	return editorRopeDupRange(&document->rope, start_byte, end_byte, len_out);
}

int editorDocumentReplaceRange(struct editorDocument *document, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len) {
	if (document == NULL) {
		return 0;
	}
	if (!editorRopeReplaceRange(&document->rope, start_byte, old_len, new_text, new_len)) {
		return 0;
	}
	return editorDocumentRebuildLineIndex(document);
}

int editorDocumentLineCount(const struct editorDocument *document) {
	return document != NULL ? document->line_count : 0;
}

int editorDocumentLineStartByte(const struct editorDocument *document, int line_idx,
		size_t *start_byte_out) {
	if (document == NULL || start_byte_out == NULL ||
			line_idx < 0 || line_idx >= document->line_count) {
		return 0;
	}
	*start_byte_out = document->line_starts[line_idx];
	return 1;
}

int editorDocumentLineEndByte(const struct editorDocument *document, int line_idx,
		size_t *end_byte_out) {
	if (document == NULL || end_byte_out == NULL ||
			line_idx < 0 || line_idx >= document->line_count) {
		return 0;
	}

	size_t len = editorDocumentLength(document);
	size_t start = document->line_starts[line_idx];
	size_t next = line_idx + 1 < document->line_count ? document->line_starts[line_idx + 1] : len;
	size_t end = next;
	if (next > start) {
		char previous = '\0';
		if (editorDocumentReadByte(document, next - 1, &previous) && previous == '\n') {
			end = next - 1;
		}
	}

	if (end < start || end > len) {
		return 0;
	}
	*end_byte_out = end;
	return 1;
}

int editorDocumentLineIndexForByteOffset(const struct editorDocument *document, size_t byte_offset,
		int *line_idx_out) {
	if (document == NULL || line_idx_out == NULL) {
		return 0;
	}
	if (document->line_count == 0) {
		return 0;
	}

	size_t len = editorDocumentLength(document);
	if (byte_offset > len) {
		return 0;
	}
	if (byte_offset == len && len > 0) {
		byte_offset--;
	}

	int low = 0;
	int high = document->line_count - 1;
	while (low <= high) {
		int mid = low + (high - low) / 2;
		size_t start = document->line_starts[mid];
		size_t next = mid + 1 < document->line_count ? document->line_starts[mid + 1] : len;
		if (byte_offset < start) {
			high = mid - 1;
		} else if (byte_offset >= next) {
			low = mid + 1;
		} else {
			*line_idx_out = mid;
			return 1;
		}
	}

	return 0;
}

int editorDocumentPositionToByteOffset(const struct editorDocument *document, int line_idx,
		size_t column, size_t *byte_offset_out) {
	if (document == NULL || byte_offset_out == NULL || line_idx < 0) {
		return 0;
	}

	size_t len = editorDocumentLength(document);
	if (document->line_count == 0) {
		if (line_idx == 0 && column == 0) {
			*byte_offset_out = 0;
			return 1;
		}
		return 0;
	}

	if (line_idx == document->line_count) {
		if (column != 0) {
			return 0;
		}
		*byte_offset_out = len;
		return 1;
	}
	if (line_idx > document->line_count) {
		return 0;
	}

	size_t start = 0;
	size_t end = 0;
	if (!editorDocumentLineStartByte(document, line_idx, &start) ||
			!editorDocumentLineEndByte(document, line_idx, &end) ||
			column > end - start) {
		return 0;
	}
	*byte_offset_out = start + column;
	return 1;
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
	if (document->line_count == 0) {
		if (byte_offset == 0) {
			*line_idx_out = 0;
			*column_out = 0;
			return 1;
		}
		return 0;
	}
	if (byte_offset == len) {
		*line_idx_out = document->line_count;
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
