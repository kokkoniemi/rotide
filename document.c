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
