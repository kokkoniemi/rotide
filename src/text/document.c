#include "text/document.h"

#include "support/alloc.h"
#include "support/size_utils.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

static int editorDocumentFindContainingLineFromIndex(const size_t *line_starts, int line_count,
		size_t doc_len, size_t byte_offset, int *line_idx_out) {
	if (line_starts == NULL || line_count <= 0 || line_idx_out == NULL || byte_offset >= doc_len) {
		return 0;
	}

	int low = 0;
	int high = line_count - 1;
	while (low <= high) {
		int mid = low + (high - low) / 2;
		size_t start = line_starts[mid];
		size_t next = mid + 1 < line_count ? line_starts[mid + 1] : doc_len;
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

static int editorDocumentApplySignedDelta(size_t value, size_t old_total, size_t new_total,
		size_t *out) {
	if (out == NULL) {
		return 0;
	}
	if (new_total >= old_total) {
		size_t delta = new_total - old_total;
		return editorSizeAdd(value, delta, out);
	}
	size_t delta = old_total - new_total;
	if (value < delta) {
		return 0;
	}
	*out = value - delta;
	return 1;
}

static int editorDocumentCollectLineStartsInRange(const struct editorDocument *document,
		size_t range_start, size_t range_end, size_t **starts_out, int *count_out) {
	if (starts_out == NULL || count_out == NULL || document == NULL ||
			range_start > range_end || range_end > editorDocumentLength(document)) {
		return 0;
	}
	*starts_out = NULL;
	*count_out = 0;

	size_t total_len = editorDocumentLength(document);
	if (total_len == 0 || range_start == total_len) {
		return 1;
	}

	int capacity = 8;
	size_t capacity_size = 0;
	size_t capacity_bytes = 0;
	if (!editorIntToSize(capacity, &capacity_size) ||
			!editorSizeMul(sizeof(**starts_out), capacity_size, &capacity_bytes)) {
		return 0;
	}
	size_t *starts = editorMalloc(capacity_bytes);
	if (starts == NULL) {
		return 0;
	}
	starts[0] = range_start;
	int count = 1;

	size_t offset = range_start;
	while (offset < range_end) {
		uint32_t chunk_len = 0;
		const char *chunk = editorDocumentRead(document, offset, &chunk_len);
		if (chunk == NULL || chunk_len == 0) {
			free(starts);
			return 0;
		}
		size_t max_chunk = range_end - offset;
		if ((size_t)chunk_len > max_chunk) {
			chunk_len = (uint32_t)max_chunk;
		}
		for (uint32_t i = 0; i < chunk_len; i++) {
			if (chunk[i] != '\n') {
				continue;
			}
			size_t next_start = offset + i + 1;
			if (next_start >= total_len || next_start >= range_end) {
				continue;
			}
			if (count >= capacity) {
				int next_capacity = capacity * 2;
				size_t next_cap_size = 0;
				size_t next_bytes = 0;
				if (next_capacity <= capacity ||
						!editorIntToSize(next_capacity, &next_cap_size) ||
						!editorSizeMul(sizeof(*starts), next_cap_size, &next_bytes)) {
					free(starts);
					return 0;
				}
				size_t *grown = editorRealloc(starts, next_bytes);
				if (grown == NULL) {
					free(starts);
					return 0;
				}
				starts = grown;
				capacity = next_capacity;
			}
			starts[count++] = next_start;
		}
		offset += chunk_len;
	}

	*starts_out = starts;
	*count_out = count;
	return 1;
}

static int editorDocumentPrepareReplaceLineRegion(const struct editorDocument *document,
		size_t start_byte, size_t old_len, size_t old_total, int *prefix_idx_out,
		int *suffix_idx_out, size_t *prefix_start_out, size_t *suffix_start_out) {
	if (document == NULL || prefix_idx_out == NULL || suffix_idx_out == NULL ||
			prefix_start_out == NULL || suffix_start_out == NULL) {
		return 0;
	}
	if (document->line_count == 0 || old_total == 0) {
		*prefix_idx_out = 0;
		*suffix_idx_out = 0;
		*prefix_start_out = 0;
		*suffix_start_out = 0;
		return 1;
	}

	size_t first_lookup = start_byte;
	if (first_lookup == old_total) {
		first_lookup = old_total - 1;
	}
	int first_line_idx = 0;
	if (!editorDocumentFindContainingLineFromIndex(document->line_starts, document->line_count,
				old_total, first_lookup, &first_line_idx)) {
		return 0;
	}

	size_t last_lookup = first_lookup;
	if (old_len > 0) {
		last_lookup = start_byte + old_len - 1;
	}
	int last_line_idx = 0;
	if (!editorDocumentFindContainingLineFromIndex(document->line_starts, document->line_count,
				old_total, last_lookup, &last_line_idx)) {
		return 0;
	}

	*prefix_idx_out = first_line_idx;
	*suffix_idx_out = last_line_idx + 1;
	*prefix_start_out = document->line_starts[first_line_idx];
	*suffix_start_out = *suffix_idx_out < document->line_count ?
			document->line_starts[*suffix_idx_out] : old_total;
	return 1;
}

static int editorDocumentApplyReplaceLineRegion(struct editorDocument *document,
		int prefix_idx, int suffix_idx, size_t prefix_start, size_t suffix_start_old,
		size_t old_total) {
	if (document == NULL || prefix_idx < 0 || suffix_idx < prefix_idx ||
			suffix_idx > document->line_count) {
		return 0;
	}

	size_t new_total = editorDocumentLength(document);
	size_t new_suffix_start = 0;
	if (!editorDocumentApplySignedDelta(suffix_start_old, old_total, new_total, &new_suffix_start) ||
			new_suffix_start > new_total || prefix_start > new_suffix_start) {
		return 0;
	}

	size_t *middle_starts = NULL;
	int middle_count = 0;
	if (!editorDocumentCollectLineStartsInRange(document, prefix_start, new_suffix_start,
				&middle_starts, &middle_count)) {
		return 0;
	}
	int tail_count = document->line_count - suffix_idx;
	if (middle_count < 0 || tail_count < 0) {
		free(middle_starts);
		return 0;
	}

	int new_line_count = 0;
	if (prefix_idx > INT_MAX - middle_count || prefix_idx + middle_count > INT_MAX - tail_count) {
		free(middle_starts);
		return 0;
	}
	new_line_count = prefix_idx + middle_count + tail_count;
	size_t *new_starts = NULL;
	if (new_line_count > 0) {
		size_t line_count_size = 0;
		size_t line_bytes = 0;
		if (!editorIntToSize(new_line_count, &line_count_size) ||
				!editorSizeMul(sizeof(*new_starts), line_count_size, &line_bytes)) {
			free(middle_starts);
			return 0;
		}
		new_starts = editorMalloc(line_bytes);
		if (new_starts == NULL) {
			free(middle_starts);
			return 0;
		}
	}

	size_t new_total_for_index = new_total;
	int out_idx = 0;
	for (int i = 0; i < prefix_idx; i++) {
		size_t start = document->line_starts[i];
		if (start >= new_total_for_index) {
			continue;
		}
		if (out_idx > 0 && start <= new_starts[out_idx - 1]) {
			continue;
		}
		new_starts[out_idx++] = start;
	}
	for (int i = 0; i < middle_count; i++) {
		size_t start = middle_starts[i];
		if (start >= new_total_for_index) {
			continue;
		}
		if (out_idx > 0 && start <= new_starts[out_idx - 1]) {
			continue;
		}
		new_starts[out_idx++] = start;
	}
	for (int i = suffix_idx; i < document->line_count; i++) {
		size_t shifted = 0;
		char previous = '\0';
		if (!editorDocumentApplySignedDelta(document->line_starts[i], old_total, new_total,
					&shifted)) {
			free(new_starts);
			free(middle_starts);
			return 0;
		}
		if (shifted >= new_total_for_index) {
			continue;
		}
		if (shifted > 0) {
			if (!editorDocumentReadByte(document, shifted - 1, &previous) || previous != '\n') {
				continue;
			}
		}
		if (out_idx > 0 && shifted <= new_starts[out_idx - 1]) {
			continue;
		}
		new_starts[out_idx++] = shifted;
	}
	free(middle_starts);

	if (new_total_for_index > 0) {
		if (out_idx <= 0 || new_starts == NULL) {
			free(new_starts);
			return 0;
		}
		if (new_starts[0] != 0) {
			free(new_starts);
			return 0;
		}
	}

	free(document->line_starts);
	document->line_starts = new_starts;
	document->line_count = out_idx;
	document->line_capacity = out_idx;
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
	size_t old_total = editorDocumentLength(document);
	int prefix_idx = 0;
	int suffix_idx = 0;
	size_t prefix_start = 0;
	size_t suffix_start_old = 0;
	if (!editorDocumentPrepareReplaceLineRegion(document, start_byte, old_len, old_total,
				&prefix_idx, &suffix_idx, &prefix_start, &suffix_start_old)) {
		return 0;
	}
	if (!editorRopeReplaceRange(&document->rope, start_byte, old_len, new_text, new_len)) {
		return 0;
	}
	if (editorDocumentApplyReplaceLineRegion(document, prefix_idx, suffix_idx, prefix_start,
				suffix_start_old, old_total)) {
		return 1;
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
