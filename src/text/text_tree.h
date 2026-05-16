#ifndef TEXT_TREE_H
#define TEXT_TREE_H

#include "text/text_summary.h"

#include <stddef.h>
#include <stdint.h>

struct editorTextSource;

struct editorTextChunk {
	char *bytes;
	size_t len;
};

struct editorTextNode {
	struct editorTextChunk *chunks;
	int chunk_count;
	int chunk_capacity;
	struct editorTextSummary summary;
};

struct editorTextTree {
	struct editorTextNode root;
};

void editorTextTreeInit(struct editorTextTree *tree);
void editorTextTreeFree(struct editorTextTree *tree);

size_t editorTextTreeLength(const struct editorTextTree *tree);
const struct editorTextSummary *editorTextTreeSummary(const struct editorTextTree *tree);

const char *editorTextTreeRead(const struct editorTextTree *tree, size_t byte_index,
		uint32_t *bytes_read);
int editorTextTreeCopyRange(const struct editorTextTree *tree, size_t start_byte,
		size_t end_byte, char *dst);
char *editorTextTreeDupRange(const struct editorTextTree *tree, size_t start_byte,
		size_t end_byte, size_t *len_out);

int editorTextTreeAppend(struct editorTextTree *tree, const char *text, size_t len);
int editorTextTreeResetFromString(struct editorTextTree *tree, const char *text, size_t len);
int editorTextTreeResetFromTextSource(struct editorTextTree *tree,
		const struct editorTextSource *source);
int editorTextTreeReplaceRange(struct editorTextTree *tree, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len);

#endif
