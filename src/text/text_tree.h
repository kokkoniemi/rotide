#ifndef TEXT_TREE_H
#define TEXT_TREE_H

#include "text/text_summary.h"

#include <stddef.h>
#include <stdint.h>

struct editorTextSource;

#define EDITOR_TEXT_TREE_FANOUT 16
/* Slack so a mid-piece insert (which can add 2 pieces) fits before the split. */
#define EDITOR_TEXT_TREE_NODE_SLACK 2
#define EDITOR_TEXT_TREE_NODE_CAPACITY \
	(EDITOR_TEXT_TREE_FANOUT + EDITOR_TEXT_TREE_NODE_SLACK)

struct editorTextChunk {
	char *bytes;
	size_t len;
	struct editorTextSummary summary;
};

struct editorTextNode {
	unsigned char is_leaf;
	unsigned char count;
	struct editorTextSummary summary;
	union {
		struct editorTextNode *children[EDITOR_TEXT_TREE_NODE_CAPACITY];
		struct editorTextChunk pieces[EDITOR_TEXT_TREE_NODE_CAPACITY];
	} u;
};

struct editorTextTree {
	struct editorTextNode *root;
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

/* Returns the byte offset where `line_idx` begins. Line 0 starts at byte 0;
 * line k (k >= 1) starts at the byte immediately after the k-th newline.
 * Valid range: 0 <= line_idx <= summary.newlines.
 */
int editorTextTreeLocateLine(const struct editorTextTree *tree, int line_idx,
		size_t *start_byte_out);

/* Returns the line index containing `byte`. `byte` must be in [0, length). */
int editorTextTreeLineForByte(const struct editorTextTree *tree, size_t byte,
		int *line_idx_out);

#endif
