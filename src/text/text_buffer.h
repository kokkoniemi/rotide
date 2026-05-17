#ifndef TEXT_BUFFER_H
#define TEXT_BUFFER_H

#include <stddef.h>

/* Immutable, refcounted byte buffer that tree pieces slice into.
 *
 * Two kinds of buffer exist in practice: "original" buffers built once when a
 * document is loaded (held by pieces for the lifetime of the document) and
 * per-tree "add" buffers that grow as inserts append bytes. Both share the
 * same lifecycle: pieces retain on copy, release on drop, and the buffer is
 * freed when the last ref disappears.
 *
 * `bytes` may be reallocated as the buffer grows, but the struct address is
 * stable, so it is safe for callers to hold `editorTextBuffer *` and look up
 * `buf->bytes + offset` at read time.
 */
struct editorTextBuffer {
	char *bytes;
	size_t len;
	size_t capacity;
	int ref_count;
};

struct editorTextBuffer *editorTextBufferAlloc(size_t capacity);
struct editorTextBuffer *editorTextBufferRetain(struct editorTextBuffer *buf);
void editorTextBufferRelease(struct editorTextBuffer *buf);
/* Appends `len` bytes. Returns 1 on success and writes the start offset to
 * *offset_out. Returns 0 on OOM (buffer is left unchanged).
 */
int editorTextBufferAppend(struct editorTextBuffer *buf, const char *bytes, size_t len,
		size_t *offset_out);

#endif
