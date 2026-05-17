#ifndef TEXT_TREE_H
#define TEXT_TREE_H

#include "text/text_buffer.h"
#include "text/text_summary.h"

#include <stddef.h>
#include <stdint.h>

struct editorTextSource;

#define EDITOR_TEXT_TREE_FANOUT 16
/* Slack so a mid-piece insert (which can add 2 pieces) fits before the split. */
#define EDITOR_TEXT_TREE_NODE_SLACK 2
#define EDITOR_TEXT_TREE_NODE_CAPACITY \
	(EDITOR_TEXT_TREE_FANOUT + EDITOR_TEXT_TREE_NODE_SLACK)

/* Leaf entry: a slice into a refcounted immutable buffer. */
struct editorTextChunk {
	struct editorTextBuffer *buf;
	size_t offset;
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

/* The tree holds the root and an "add" buffer that grows as inserts run.
 * Original-file bytes live in their own buffer(s), retained by pieces.
 */
struct editorTextTree {
	struct editorTextNode *root;
	struct editorTextBuffer *add_buf;
};

/* Initialises an empty tree. Returns 1 on success, 0 on OOM (in which case
 * the tree is zeroed and safe to pass to editorTextTreeFree, which is a
 * no-op).
 */
int editorTextTreeInit(struct editorTextTree *tree);
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

/* Pre-grow the add buffer so the next `additional_bytes` of insert traffic
 * can be appended without realloc. Used by edit pipelines that need a revert
 * path to be allocation-free. Returns 1 on success, 0 on OOM.
 */
int editorTextTreeReserveAddBufCapacity(struct editorTextTree *tree, size_t additional_bytes);

/* Returns the byte offset where `line_idx` begins. Line 0 starts at byte 0;
 * line k (k >= 1) starts at the byte immediately after the k-th newline.
 * Valid range: 0 <= line_idx <= summary.newlines.
 */
int editorTextTreeLocateLine(const struct editorTextTree *tree, int line_idx,
		size_t *start_byte_out);

/* Returns the line index containing `byte`. `byte` must be in [0, length). */
int editorTextTreeLineForByte(const struct editorTextTree *tree, size_t byte,
		int *line_idx_out);

/* Diagnostic snapshot — used by tests/benchmarks to assert that piece counts
 * stay bounded under heavy editing.
 *
 * max_depth counts internal-node edges from the root: a single-leaf tree
 * reports 0, a root-with-leaf-children tree reports 1, and so on.
 */
struct editorTextTreeStats {
	int leaf_count;
	int internal_node_count;
	int piece_count;
	int max_depth;
	size_t total_bytes;
};

void editorTextTreeCollectStats(const struct editorTextTree *tree,
		struct editorTextTreeStats *out);

#endif
