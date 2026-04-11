#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "rope.h"
#include <stddef.h>
#include <stdint.h>

struct editorDocument {
	struct editorRope rope;
	size_t *line_starts;
	int line_count;
	int line_capacity;
};

void editorDocumentInit(struct editorDocument *document);
void editorDocumentFree(struct editorDocument *document);
int editorDocumentResetFromString(struct editorDocument *document, const char *text, size_t len);
size_t editorDocumentLength(const struct editorDocument *document);
const char *editorDocumentRead(const struct editorDocument *document, size_t byte_index,
		uint32_t *bytes_read);
int editorDocumentCopyRange(const struct editorDocument *document, size_t start_byte,
		size_t end_byte, char *dst);
char *editorDocumentDupRange(const struct editorDocument *document, size_t start_byte,
		size_t end_byte, size_t *len_out);
int editorDocumentReplaceRange(struct editorDocument *document, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len);
int editorDocumentLineCount(const struct editorDocument *document);
int editorDocumentLineStartByte(const struct editorDocument *document, int line_idx,
		size_t *start_byte_out);
int editorDocumentLineIndexForByteOffset(const struct editorDocument *document, size_t byte_offset,
		int *line_idx_out);

#endif
