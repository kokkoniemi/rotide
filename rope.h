#ifndef ROPE_H
#define ROPE_H

#include <stddef.h>
#include <stdint.h>

struct editorRopeChunk {
	char *bytes;
	size_t len;
};

struct editorRope {
	struct editorRopeChunk *chunks;
	int chunk_count;
	int chunk_capacity;
	size_t length;
};

void editorRopeInit(struct editorRope *rope);
void editorRopeFree(struct editorRope *rope);
int editorRopeAppend(struct editorRope *rope, const char *text, size_t len);
int editorRopeResetFromString(struct editorRope *rope, const char *text, size_t len);
size_t editorRopeLength(const struct editorRope *rope);
const char *editorRopeRead(const struct editorRope *rope, size_t byte_index, uint32_t *bytes_read);
int editorRopeCopyRange(const struct editorRope *rope, size_t start_byte, size_t end_byte, char *dst);
char *editorRopeDupRange(const struct editorRope *rope, size_t start_byte, size_t end_byte,
		size_t *len_out);
int editorRopeReplaceRange(struct editorRope *rope, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len);

#endif
