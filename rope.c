#include "rope.h"

#include "alloc.h"
#include "size_utils.h"
#include <stdlib.h>
#include <string.h>

#define EDITOR_ROPE_CHUNK_BYTES 1024

static void editorRopeChunkFree(struct editorRopeChunk *chunk) {
	if (chunk == NULL) {
		return;
	}
	free(chunk->bytes);
	chunk->bytes = NULL;
	chunk->len = 0;
}

static void editorRopeChunkArrayFree(struct editorRopeChunk *chunks, int chunk_count) {
	if (chunks == NULL) {
		return;
	}
	for (int i = 0; i < chunk_count; i++) {
		editorRopeChunkFree(&chunks[i]);
	}
	free(chunks);
}

static int editorRopeEnsureCapacity(struct editorRope *rope, int needed) {
	if (rope == NULL || needed < 0) {
		return 0;
	}
	if (needed <= rope->chunk_capacity) {
		return 1;
	}

	int new_capacity = rope->chunk_capacity > 0 ? rope->chunk_capacity : 4;
	while (new_capacity < needed) {
		new_capacity *= 2;
	}

	size_t cap_size = 0;
	size_t bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(*rope->chunks), cap_size, &bytes)) {
		return 0;
	}

	struct editorRopeChunk *grown = editorRealloc(rope->chunks, bytes);
	if (grown == NULL) {
		return 0;
	}
	for (int i = rope->chunk_capacity; i < new_capacity; i++) {
		grown[i].bytes = NULL;
		grown[i].len = 0;
	}
	rope->chunks = grown;
	rope->chunk_capacity = new_capacity;
	return 1;
}

static int editorRopeSplitChunkAt(struct editorRope *rope, int chunk_idx, size_t split_off) {
	if (rope == NULL || chunk_idx < 0 || chunk_idx >= rope->chunk_count) {
		return 0;
	}
	struct editorRopeChunk *chunk = &rope->chunks[chunk_idx];
	if (split_off == 0 || split_off >= chunk->len) {
		return 1;
	}
	if (!editorRopeEnsureCapacity(rope, rope->chunk_count + 1)) {
		return 0;
	}
	chunk = &rope->chunks[chunk_idx];

	size_t left_len = split_off;
	size_t right_len = chunk->len - split_off;
	char *left = editorMalloc(left_len);
	char *right = editorMalloc(right_len);
	if (left == NULL || right == NULL) {
		free(left);
		free(right);
		return 0;
	}
	memcpy(left, chunk->bytes, left_len);
	memcpy(right, chunk->bytes + split_off, right_len);
	free(chunk->bytes);

	memmove(&rope->chunks[chunk_idx + 2], &rope->chunks[chunk_idx + 1],
			sizeof(*rope->chunks) * (size_t)(rope->chunk_count - chunk_idx - 1));
	rope->chunk_count++;

	rope->chunks[chunk_idx].bytes = left;
	rope->chunks[chunk_idx].len = left_len;
	rope->chunks[chunk_idx + 1].bytes = right;
	rope->chunks[chunk_idx + 1].len = right_len;
	return 1;
}

static int editorRopeLocateBoundary(const struct editorRope *rope, size_t byte_offset, int *idx_out) {
	if (rope == NULL || idx_out == NULL || byte_offset > rope->length) {
		return 0;
	}
	if (byte_offset == rope->length) {
		*idx_out = rope->chunk_count;
		return 1;
	}

	size_t offset = 0;
	for (int i = 0; i < rope->chunk_count; i++) {
		size_t next = offset + rope->chunks[i].len;
		if (byte_offset < next) {
			if (byte_offset == offset) {
				*idx_out = i;
				return 1;
			}
			if (byte_offset == next) {
				*idx_out = i + 1;
				return 1;
			}
			return editorRopeSplitChunkAt((struct editorRope *)rope, i, byte_offset - offset) &&
					((*idx_out = i + 1), 1);
		}
		offset = next;
	}
	*idx_out = rope->chunk_count;
	return 1;
}

static int editorRopeBuildChunkArrayFromText(const char *text, size_t len,
		struct editorRopeChunk **chunks_out, int *chunk_count_out) {
	if (chunks_out == NULL || chunk_count_out == NULL || (len > 0 && text == NULL)) {
		return 0;
	}
	*chunks_out = NULL;
	*chunk_count_out = 0;
	if (len == 0) {
		return 1;
	}

	size_t needed_size = 0;
	size_t chunk_count_size = (len + EDITOR_ROPE_CHUNK_BYTES - 1) / EDITOR_ROPE_CHUNK_BYTES;
	if (!editorSizeMul(sizeof(struct editorRopeChunk), chunk_count_size, &needed_size) ||
			!editorSizeWithinInt(chunk_count_size)) {
		return 0;
	}
	struct editorRopeChunk *chunks = editorMalloc(needed_size);
	if (chunks == NULL) {
		return 0;
	}
	memset(chunks, 0, needed_size);

	size_t offset = 0;
	int chunk_count = 0;
	while (offset < len) {
		size_t chunk_len = len - offset;
		if (chunk_len > EDITOR_ROPE_CHUNK_BYTES) {
			chunk_len = EDITOR_ROPE_CHUNK_BYTES;
		}
		char *copy = editorMalloc(chunk_len);
		if (copy == NULL) {
			editorRopeChunkArrayFree(chunks, chunk_count);
			return 0;
		}
		memcpy(copy, text + offset, chunk_len);
		chunks[chunk_count].bytes = copy;
		chunks[chunk_count].len = chunk_len;
		chunk_count++;
		offset += chunk_len;
	}

	*chunks_out = chunks;
	*chunk_count_out = chunk_count;
	return 1;
}

static void editorRopeRemoveChunkRange(struct editorRope *rope, int start_idx, int remove_count) {
	if (rope == NULL || remove_count <= 0 || start_idx < 0 || start_idx >= rope->chunk_count ||
			remove_count > rope->chunk_count - start_idx) {
		return;
	}
	for (int i = 0; i < remove_count; i++) {
		editorRopeChunkFree(&rope->chunks[start_idx + i]);
	}
	memmove(&rope->chunks[start_idx], &rope->chunks[start_idx + remove_count],
			sizeof(*rope->chunks) * (size_t)(rope->chunk_count - start_idx - remove_count));
	rope->chunk_count -= remove_count;
}

static int editorRopeInsertChunkArray(struct editorRope *rope, int insert_idx,
		struct editorRopeChunk *chunks, int chunk_count) {
	if (rope == NULL || insert_idx < 0 || insert_idx > rope->chunk_count ||
			chunk_count < 0 || (chunk_count > 0 && chunks == NULL)) {
		return 0;
	}
	if (chunk_count == 0) {
		return 1;
	}
	if (!editorRopeEnsureCapacity(rope, rope->chunk_count + chunk_count)) {
		return 0;
	}
	memmove(&rope->chunks[insert_idx + chunk_count], &rope->chunks[insert_idx],
			sizeof(*rope->chunks) * (size_t)(rope->chunk_count - insert_idx));
	for (int i = 0; i < chunk_count; i++) {
		rope->chunks[insert_idx + i] = chunks[i];
	}
	rope->chunk_count += chunk_count;
	return 1;
}

static int editorRopeAppendChunkCopy(struct editorRope *rope, const char *text, size_t len) {
	if (len == 0) {
		return 1;
	}
	if (!editorRopeEnsureCapacity(rope, rope->chunk_count + 1)) {
		return 0;
	}

	char *copy = editorMalloc(len);
	if (copy == NULL) {
		return 0;
	}
	memcpy(copy, text, len);

	rope->chunks[rope->chunk_count].bytes = copy;
	rope->chunks[rope->chunk_count].len = len;
	rope->chunk_count++;
	rope->length += len;
	return 1;
}

int editorRopeAppend(struct editorRope *rope, const char *text, size_t len) {
	if (rope == NULL || (len > 0 && text == NULL)) {
		return 0;
	}

	size_t offset = 0;
	while (offset < len) {
		size_t chunk_len = len - offset;
		if (chunk_len > EDITOR_ROPE_CHUNK_BYTES) {
			chunk_len = EDITOR_ROPE_CHUNK_BYTES;
		}
		if (!editorRopeAppendChunkCopy(rope, text + offset, chunk_len)) {
			return 0;
		}
		offset += chunk_len;
	}

	return 1;
}

void editorRopeInit(struct editorRope *rope) {
	if (rope == NULL) {
		return;
	}
	rope->chunks = NULL;
	rope->chunk_count = 0;
	rope->chunk_capacity = 0;
	rope->length = 0;
}

void editorRopeFree(struct editorRope *rope) {
	if (rope == NULL) {
		return;
	}
	for (int i = 0; i < rope->chunk_count; i++) {
		editorRopeChunkFree(&rope->chunks[i]);
	}
	free(rope->chunks);
	editorRopeInit(rope);
}

int editorRopeResetFromString(struct editorRope *rope, const char *text, size_t len) {
	struct editorRope rebuilt;
	editorRopeInit(&rebuilt);

	if (len > 0 && text == NULL) {
		return 0;
	}

	if (!editorRopeAppend(&rebuilt, text, len)) {
		editorRopeFree(&rebuilt);
		return 0;
	}

	editorRopeFree(rope);
	*rope = rebuilt;
	return 1;
}

size_t editorRopeLength(const struct editorRope *rope) {
	return rope != NULL ? rope->length : 0;
}

const char *editorRopeRead(const struct editorRope *rope, size_t byte_index, uint32_t *bytes_read) {
	if (bytes_read == NULL) {
		return NULL;
	}
	*bytes_read = 0;
	if (rope == NULL || byte_index >= rope->length) {
		return NULL;
	}

	size_t offset = 0;
	for (int i = 0; i < rope->chunk_count; i++) {
		size_t next = offset + rope->chunks[i].len;
		if (byte_index < next) {
			size_t local = byte_index - offset;
			size_t remaining = rope->chunks[i].len - local;
			if (remaining > UINT32_MAX) {
				remaining = UINT32_MAX;
			}
			*bytes_read = (uint32_t)remaining;
			return rope->chunks[i].bytes + local;
		}
		offset = next;
	}

	return NULL;
}

int editorRopeCopyRange(const struct editorRope *rope, size_t start_byte, size_t end_byte, char *dst) {
	if (rope == NULL || dst == NULL || start_byte > end_byte || end_byte > rope->length) {
		return 0;
	}

	size_t copied = 0;
	size_t offset = 0;
	for (int i = 0; i < rope->chunk_count && copied < end_byte - start_byte; i++) {
		size_t next = offset + rope->chunks[i].len;
		if (start_byte < next && end_byte > offset) {
			size_t local_start = start_byte > offset ? start_byte - offset : 0;
			size_t local_end = end_byte < next ? end_byte - offset : rope->chunks[i].len;
			size_t local_len = local_end - local_start;
			memcpy(dst + copied, rope->chunks[i].bytes + local_start, local_len);
			copied += local_len;
		}
		offset = next;
	}

	return copied == end_byte - start_byte;
}

char *editorRopeDupRange(const struct editorRope *rope, size_t start_byte, size_t end_byte,
		size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (rope == NULL || start_byte > end_byte || end_byte > rope->length) {
		return NULL;
	}

	size_t len = end_byte - start_byte;
	size_t cap = 0;
	if (!editorSizeAdd(len, 1, &cap)) {
		return NULL;
	}

	char *dup = editorMalloc(cap);
	if (dup == NULL) {
		return NULL;
	}
	if (len > 0 && !editorRopeCopyRange(rope, start_byte, end_byte, dup)) {
		free(dup);
		return NULL;
	}
	dup[len] = '\0';
	if (len_out != NULL) {
		*len_out = len;
	}
	return dup;
}

int editorRopeReplaceRange(struct editorRope *rope, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len) {
	if (rope == NULL || start_byte > rope->length || old_len > rope->length - start_byte) {
		return 0;
	}
	if (new_len > 0 && new_text == NULL) {
		return 0;
	}

	size_t end_byte = start_byte + old_len;
	size_t next_len = 0;
	if (!editorSizeAdd(rope->length - old_len, new_len, &next_len)) {
		return 0;
	}
	int start_idx = 0;
	int end_idx = 0;
	struct editorRopeChunk *insert_chunks = NULL;
	int insert_chunk_count = 0;
	if (!editorRopeLocateBoundary(rope, start_byte, &start_idx) ||
			!editorRopeLocateBoundary(rope, end_byte, &end_idx)) {
		return 0;
	}
	if (!editorRopeBuildChunkArrayFromText(new_text, new_len, &insert_chunks, &insert_chunk_count)) {
		return 0;
	}

	int remove_count = end_idx - start_idx;
	editorRopeRemoveChunkRange(rope, start_idx, remove_count);
	if (!editorRopeInsertChunkArray(rope, start_idx, insert_chunks, insert_chunk_count)) {
		editorRopeChunkArrayFree(insert_chunks, insert_chunk_count);
		return 0;
	}
	free(insert_chunks);
	rope->length = next_len;
	return 1;
}
