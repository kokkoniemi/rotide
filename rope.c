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
	size_t old_total = rope->length;
	size_t kept_tail = old_total - end_byte;
	size_t next_len = 0;
	size_t next_cap = 0;
	if (!editorSizeAdd(start_byte, new_len, &next_len) ||
			!editorSizeAdd(next_len, kept_tail, &next_len) ||
			!editorSizeAdd(next_len, 1, &next_cap)) {
		return 0;
	}

	char *flat = editorMalloc(next_cap);
	if (flat == NULL) {
		return 0;
	}

	if (start_byte > 0 && !editorRopeCopyRange(rope, 0, start_byte, flat)) {
		free(flat);
		return 0;
	}
	if (new_len > 0) {
		memcpy(flat + start_byte, new_text, new_len);
	}
	if (kept_tail > 0 &&
			!editorRopeCopyRange(rope, end_byte, old_total, flat + start_byte + new_len)) {
		free(flat);
		return 0;
	}
	flat[next_len] = '\0';

	int ok = editorRopeResetFromString(rope, flat, next_len);
	free(flat);
	return ok;
}
