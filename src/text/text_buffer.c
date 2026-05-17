#include "text/text_buffer.h"

#include "support/alloc.h"
#include "support/size_utils.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct editorTextBuffer *editorTextBufferAlloc(size_t capacity) {
	struct editorTextBuffer *buf = editorMalloc(sizeof(*buf));
	if (buf == NULL) {
		return NULL;
	}
	buf->bytes = NULL;
	buf->len = 0;
	buf->capacity = 0;
	buf->ref_count = 1;
	if (capacity > 0) {
		buf->bytes = editorMalloc(capacity);
		if (buf->bytes == NULL) {
			free(buf);
			return NULL;
		}
		buf->capacity = capacity;
	}
	return buf;
}

struct editorTextBuffer *editorTextBufferRetain(struct editorTextBuffer *buf) {
	if (buf == NULL) {
		return NULL;
	}
	buf->ref_count++;
	return buf;
}

void editorTextBufferRelease(struct editorTextBuffer *buf) {
	if (buf == NULL) {
		return;
	}
	assert(buf->ref_count > 0);
	buf->ref_count--;
	if (buf->ref_count == 0) {
		free(buf->bytes);
		free(buf);
	}
}

int editorTextBufferReserve(struct editorTextBuffer *buf, size_t min_capacity) {
	if (buf == NULL) {
		return 0;
	}
	if (min_capacity <= buf->capacity) {
		return 1;
	}
	size_t new_cap = buf->capacity > 0 ? buf->capacity : 64;
	while (new_cap < min_capacity) {
		if (new_cap > ((size_t)-1) / 2) {
			new_cap = min_capacity;
			break;
		}
		new_cap *= 2;
	}
	char *grown = editorRealloc(buf->bytes, new_cap);
	if (grown == NULL) {
		return 0;
	}
	buf->bytes = grown;
	buf->capacity = new_cap;
	return 1;
}

int editorTextBufferAppend(struct editorTextBuffer *buf, const char *bytes, size_t len,
		size_t *offset_out) {
	if (buf == NULL || (len > 0 && bytes == NULL)) {
		return 0;
	}
	size_t start = buf->len;
	size_t needed = 0;
	if (!editorSizeAdd(buf->len, len, &needed)) {
		return 0;
	}
	if (!editorTextBufferReserve(buf, needed)) {
		return 0;
	}
	if (len > 0) {
		memcpy(buf->bytes + start, bytes, len);
	}
	buf->len = needed;
	if (offset_out != NULL) {
		*offset_out = start;
	}
	return 1;
}
