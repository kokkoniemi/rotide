#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "rotide.h"
#include "text/text_tree.h"

/* Canonical writable text for a tab. Storage and line metadata are unified in
 * the tree: line and byte position queries descend through the per-node
 * summaries instead of consulting an external index.
 */
struct editorDocument {
	struct editorTextTree tree;
};

void editorDocumentInit(struct editorDocument *document);
void editorDocumentFree(struct editorDocument *document);
int editorDocumentResetFromString(struct editorDocument *document, const char *text, size_t len);
int editorDocumentResetFromDocument(struct editorDocument *document,
                                    const struct editorDocument *source);
int editorDocumentResetFromTextSource(struct editorDocument *document,
                                      const struct editorTextSource *source);
size_t editorDocumentLength(const struct editorDocument *document);
const char *editorDocumentRead(const struct editorDocument *document, size_t byte_index,
                               uint32_t *bytes_read);
int editorDocumentCopyRange(const struct editorDocument *document, size_t start_byte,
                            size_t end_byte, char *dst);
char *editorDocumentDupRange(const struct editorDocument *document, size_t start_byte,
                             size_t end_byte, size_t *len_out);
int editorDocumentReplaceRange(struct editorDocument *document, size_t start_byte, size_t old_len,
                               const char *new_text, size_t new_len);

/* Reserve add-buffer capacity for `additional_bytes` so the next inserts of
 * that total size happen without realloc — used by edit pipelines that need
 * an alloc-free revert path. Returns 1 on success, 0 on OOM.
 */
int editorDocumentReserveInsertCapacity(struct editorDocument *document, size_t additional_bytes);

/* Byte/line mapping helpers are the boundary between document storage and
 * editor cursor/search/selection state.
 */
int editorDocumentLineCount(const struct editorDocument *document);
int editorDocumentLineStartByte(const struct editorDocument *document, int line_idx,
                                size_t *start_byte_out);
int editorDocumentLineEndByte(const struct editorDocument *document, int line_idx,
                              size_t *end_byte_out);
int editorDocumentLineIndexForByteOffset(const struct editorDocument *document, size_t byte_offset,
                                         int *line_idx_out);
int editorDocumentPositionToByteOffset(const struct editorDocument *document, int line_idx,
                                       size_t column, size_t *byte_offset_out);
int editorDocumentByteOffsetToPosition(const struct editorDocument *document, size_t byte_offset,
                                       int *line_idx_out, size_t *column_out);

size_t editorDocumentMaxLineBytes(const struct editorDocument *document);

size_t editorDocumentLineLength(const struct editorDocument *document, int line_idx);

/* Borrowed pointer to line bytes; valid only until the next document mutation.
 * Returns NULL when the line straddles tree pieces — callers fall back to
 * editorDocumentLineDup.
 */
const char *editorDocumentLineBytes(const struct editorDocument *document, int line_idx,
                                    size_t *len_out);

/* NUL-terminated copy of the line; caller frees. */
char *editorDocumentLineDup(const struct editorDocument *document, int line_idx, size_t *len_out);

/* Line bytes accessor that hides the zero-copy / fallback split. `owned` is
 * non-NULL only when the bytes were copied; in either case release via
 * editorLineViewRelease. When `owned` is NULL, `data` borrows from the tree
 * and must not outlive the next document mutation.
 */
struct editorLineView {
	const char *data;
	int size;
	char *owned;
};

int editorDocumentLineView(const struct editorDocument *document, int line_idx,
                           struct editorLineView *view_out);
void editorLineViewRelease(struct editorLineView *view);

#endif
