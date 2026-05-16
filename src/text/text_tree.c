#include "text/text_tree.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/text_summary.h"

#include <stdlib.h>
#include <string.h>

#define EDITOR_TEXT_TREE_CHUNK_BYTES 1024

static void editorTextChunkFree(struct editorTextChunk *chunk) {
	if (chunk == NULL) {
		return;
	}
	free(chunk->bytes);
	chunk->bytes = NULL;
	chunk->len = 0;
}

static void editorTextChunkArrayFree(struct editorTextChunk *chunks, int chunk_count) {
	if (chunks == NULL) {
		return;
	}
	for (int i = 0; i < chunk_count; i++) {
		editorTextChunkFree(&chunks[i]);
	}
	free(chunks);
}

static void editorTextNodeRecomputeSummary(struct editorTextNode *node) {
	struct editorTextSummary acc;
	editorTextSummaryZero(&acc);
	if (node == NULL) {
		return;
	}
	for (int i = 0; i < node->chunk_count; i++) {
		struct editorTextSummary piece;
		editorTextSummaryFromBytes(node->chunks[i].bytes, node->chunks[i].len, &piece);
		struct editorTextSummary merged;
		editorTextSummaryMerge(&acc, &piece, &merged);
		acc = merged;
	}
	node->summary = acc;
}

static int editorTextNodeEnsureCapacity(struct editorTextNode *node, int needed) {
	if (node == NULL || needed < 0) {
		return 0;
	}
	if (needed <= node->chunk_capacity) {
		return 1;
	}

	int new_capacity = node->chunk_capacity > 0 ? node->chunk_capacity : 4;
	while (new_capacity < needed) {
		new_capacity *= 2;
	}

	size_t cap_size = 0;
	size_t bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(*node->chunks), cap_size, &bytes)) {
		return 0;
	}

	struct editorTextChunk *grown = editorRealloc(node->chunks, bytes);
	if (grown == NULL) {
		return 0;
	}
	for (int i = node->chunk_capacity; i < new_capacity; i++) {
		grown[i].bytes = NULL;
		grown[i].len = 0;
	}
	node->chunks = grown;
	node->chunk_capacity = new_capacity;
	return 1;
}

static int editorTextNodeSplitChunkAt(struct editorTextNode *node, int chunk_idx,
		size_t split_off) {
	if (node == NULL || chunk_idx < 0 || chunk_idx >= node->chunk_count) {
		return 0;
	}
	struct editorTextChunk *chunk = &node->chunks[chunk_idx];
	if (split_off == 0 || split_off >= chunk->len) {
		return 1;
	}
	if (!editorTextNodeEnsureCapacity(node, node->chunk_count + 1)) {
		return 0;
	}
	chunk = &node->chunks[chunk_idx];

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

	memmove(&node->chunks[chunk_idx + 2], &node->chunks[chunk_idx + 1],
			sizeof(*node->chunks) * (size_t)(node->chunk_count - chunk_idx - 1));
	node->chunk_count++;

	node->chunks[chunk_idx].bytes = left;
	node->chunks[chunk_idx].len = left_len;
	node->chunks[chunk_idx + 1].bytes = right;
	node->chunks[chunk_idx + 1].len = right_len;
	return 1;
}

static int editorTextNodeLocateBoundary(const struct editorTextNode *node, size_t byte_offset,
		int *idx_out) {
	if (node == NULL || idx_out == NULL || byte_offset > node->summary.bytes) {
		return 0;
	}
	if (byte_offset == node->summary.bytes) {
		*idx_out = node->chunk_count;
		return 1;
	}

	size_t offset = 0;
	for (int i = 0; i < node->chunk_count; i++) {
		size_t next = offset + node->chunks[i].len;
		if (byte_offset < next) {
			if (byte_offset == offset) {
				*idx_out = i;
				return 1;
			}
			if (byte_offset == next) {
				*idx_out = i + 1;
				return 1;
			}
			return editorTextNodeSplitChunkAt((struct editorTextNode *)node, i,
					byte_offset - offset) && ((*idx_out = i + 1), 1);
		}
		offset = next;
	}
	*idx_out = node->chunk_count;
	return 1;
}

static int editorTextNodeBuildChunkArrayFromText(const char *text, size_t len,
		struct editorTextChunk **chunks_out, int *chunk_count_out) {
	if (chunks_out == NULL || chunk_count_out == NULL || (len > 0 && text == NULL)) {
		return 0;
	}
	*chunks_out = NULL;
	*chunk_count_out = 0;
	if (len == 0) {
		return 1;
	}

	size_t needed_size = 0;
	size_t chunk_count_size = (len + EDITOR_TEXT_TREE_CHUNK_BYTES - 1) / EDITOR_TEXT_TREE_CHUNK_BYTES;
	if (!editorSizeMul(sizeof(struct editorTextChunk), chunk_count_size, &needed_size) ||
			!editorSizeWithinInt(chunk_count_size)) {
		return 0;
	}
	struct editorTextChunk *chunks = editorMalloc(needed_size);
	if (chunks == NULL) {
		return 0;
	}
	memset(chunks, 0, needed_size);

	size_t offset = 0;
	int chunk_count = 0;
	while (offset < len) {
		size_t chunk_len = len - offset;
		if (chunk_len > EDITOR_TEXT_TREE_CHUNK_BYTES) {
			chunk_len = EDITOR_TEXT_TREE_CHUNK_BYTES;
		}
		char *copy = editorMalloc(chunk_len);
		if (copy == NULL) {
			editorTextChunkArrayFree(chunks, chunk_count);
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

static void editorTextNodeRemoveChunkRange(struct editorTextNode *node, int start_idx,
		int remove_count) {
	if (node == NULL || remove_count <= 0 || start_idx < 0 || start_idx >= node->chunk_count ||
			remove_count > node->chunk_count - start_idx) {
		return;
	}
	for (int i = 0; i < remove_count; i++) {
		editorTextChunkFree(&node->chunks[start_idx + i]);
	}
	memmove(&node->chunks[start_idx], &node->chunks[start_idx + remove_count],
			sizeof(*node->chunks) * (size_t)(node->chunk_count - start_idx - remove_count));
	node->chunk_count -= remove_count;
}

static int editorTextNodeInsertChunkArray(struct editorTextNode *node, int insert_idx,
		struct editorTextChunk *chunks, int chunk_count) {
	if (node == NULL || insert_idx < 0 || insert_idx > node->chunk_count ||
			chunk_count < 0 || (chunk_count > 0 && chunks == NULL)) {
		return 0;
	}
	if (chunk_count == 0) {
		return 1;
	}
	if (!editorTextNodeEnsureCapacity(node, node->chunk_count + chunk_count)) {
		return 0;
	}
	memmove(&node->chunks[insert_idx + chunk_count], &node->chunks[insert_idx],
			sizeof(*node->chunks) * (size_t)(node->chunk_count - insert_idx));
	for (int i = 0; i < chunk_count; i++) {
		node->chunks[insert_idx + i] = chunks[i];
	}
	node->chunk_count += chunk_count;
	return 1;
}

static int editorTextNodeAppendChunkCopy(struct editorTextNode *node, const char *text,
		size_t len) {
	if (len == 0) {
		return 1;
	}
	if (!editorTextNodeEnsureCapacity(node, node->chunk_count + 1)) {
		return 0;
	}

	char *copy = editorMalloc(len);
	if (copy == NULL) {
		return 0;
	}
	memcpy(copy, text, len);

	node->chunks[node->chunk_count].bytes = copy;
	node->chunks[node->chunk_count].len = len;
	node->chunk_count++;
	return 1;
}

void editorTextTreeInit(struct editorTextTree *tree) {
	if (tree == NULL) {
		return;
	}
	tree->root.chunks = NULL;
	tree->root.chunk_count = 0;
	tree->root.chunk_capacity = 0;
	editorTextSummaryZero(&tree->root.summary);
}

void editorTextTreeFree(struct editorTextTree *tree) {
	if (tree == NULL) {
		return;
	}
	for (int i = 0; i < tree->root.chunk_count; i++) {
		editorTextChunkFree(&tree->root.chunks[i]);
	}
	free(tree->root.chunks);
	editorTextTreeInit(tree);
}

size_t editorTextTreeLength(const struct editorTextTree *tree) {
	return tree != NULL ? tree->root.summary.bytes : 0;
}

const struct editorTextSummary *editorTextTreeSummary(const struct editorTextTree *tree) {
	return tree != NULL ? &tree->root.summary : NULL;
}

int editorTextTreeAppend(struct editorTextTree *tree, const char *text, size_t len) {
	if (tree == NULL || (len > 0 && text == NULL)) {
		return 0;
	}

	size_t offset = 0;
	while (offset < len) {
		size_t chunk_len = len - offset;
		if (chunk_len > EDITOR_TEXT_TREE_CHUNK_BYTES) {
			chunk_len = EDITOR_TEXT_TREE_CHUNK_BYTES;
		}
		if (!editorTextNodeAppendChunkCopy(&tree->root, text + offset, chunk_len)) {
			return 0;
		}
		offset += chunk_len;
	}

	editorTextNodeRecomputeSummary(&tree->root);
	return 1;
}

int editorTextTreeResetFromString(struct editorTextTree *tree, const char *text, size_t len) {
	struct editorTextTree rebuilt;
	editorTextTreeInit(&rebuilt);

	if (len > 0 && text == NULL) {
		return 0;
	}

	if (!editorTextTreeAppend(&rebuilt, text, len)) {
		editorTextTreeFree(&rebuilt);
		return 0;
	}

	editorTextTreeFree(tree);
	*tree = rebuilt;
	return 1;
}

int editorTextTreeResetFromTextSource(struct editorTextTree *tree,
		const struct editorTextSource *source) {
	if (tree == NULL || source == NULL || source->read == NULL) {
		return 0;
	}

	struct editorTextTree rebuilt;
	editorTextTreeInit(&rebuilt);

	size_t offset = 0;
	while (offset < source->length) {
		uint32_t chunk_len = 0;
		const char *chunk = source->read(source, offset, &chunk_len);
		if (chunk == NULL || chunk_len == 0) {
			editorTextTreeFree(&rebuilt);
			return 0;
		}

		size_t remaining = source->length - offset;
		if ((size_t)chunk_len > remaining ||
				!editorTextTreeAppend(&rebuilt, chunk, (size_t)chunk_len)) {
			editorTextTreeFree(&rebuilt);
			return 0;
		}
		offset += (size_t)chunk_len;
	}

	editorTextTreeFree(tree);
	*tree = rebuilt;
	return 1;
}

const char *editorTextTreeRead(const struct editorTextTree *tree, size_t byte_index,
		uint32_t *bytes_read) {
	if (bytes_read == NULL) {
		return NULL;
	}
	*bytes_read = 0;
	if (tree == NULL || byte_index >= tree->root.summary.bytes) {
		return NULL;
	}

	size_t offset = 0;
	for (int i = 0; i < tree->root.chunk_count; i++) {
		size_t next = offset + tree->root.chunks[i].len;
		if (byte_index < next) {
			size_t local = byte_index - offset;
			size_t remaining = tree->root.chunks[i].len - local;
			if (remaining > UINT32_MAX) {
				remaining = UINT32_MAX;
			}
			*bytes_read = (uint32_t)remaining;
			return tree->root.chunks[i].bytes + local;
		}
		offset = next;
	}

	return NULL;
}

int editorTextTreeCopyRange(const struct editorTextTree *tree, size_t start_byte,
		size_t end_byte, char *dst) {
	if (tree == NULL || dst == NULL || start_byte > end_byte ||
			end_byte > tree->root.summary.bytes) {
		return 0;
	}

	size_t copied = 0;
	size_t offset = 0;
	for (int i = 0; i < tree->root.chunk_count && copied < end_byte - start_byte; i++) {
		size_t next = offset + tree->root.chunks[i].len;
		if (start_byte < next && end_byte > offset) {
			size_t local_start = start_byte > offset ? start_byte - offset : 0;
			size_t local_end = end_byte < next ? end_byte - offset : tree->root.chunks[i].len;
			size_t local_len = local_end - local_start;
			memcpy(dst + copied, tree->root.chunks[i].bytes + local_start, local_len);
			copied += local_len;
		}
		offset = next;
	}

	return copied == end_byte - start_byte;
}

char *editorTextTreeDupRange(const struct editorTextTree *tree, size_t start_byte,
		size_t end_byte, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (tree == NULL || start_byte > end_byte || end_byte > tree->root.summary.bytes) {
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
	if (len > 0 && !editorTextTreeCopyRange(tree, start_byte, end_byte, dup)) {
		free(dup);
		return NULL;
	}
	dup[len] = '\0';
	if (len_out != NULL) {
		*len_out = len;
	}
	return dup;
}

int editorTextTreeReplaceRange(struct editorTextTree *tree, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len) {
	if (tree == NULL || start_byte > tree->root.summary.bytes ||
			old_len > tree->root.summary.bytes - start_byte) {
		return 0;
	}
	if (new_len > 0 && new_text == NULL) {
		return 0;
	}

	size_t end_byte = start_byte + old_len;
	size_t next_len = 0;
	if (!editorSizeAdd(tree->root.summary.bytes - old_len, new_len, &next_len)) {
		return 0;
	}
	int start_idx = 0;
	int end_idx = 0;
	struct editorTextChunk *insert_chunks = NULL;
	int insert_chunk_count = 0;
	if (!editorTextNodeLocateBoundary(&tree->root, start_byte, &start_idx) ||
			!editorTextNodeLocateBoundary(&tree->root, end_byte, &end_idx)) {
		return 0;
	}
	if (!editorTextNodeBuildChunkArrayFromText(new_text, new_len, &insert_chunks,
				&insert_chunk_count)) {
		return 0;
	}

	int remove_count = end_idx - start_idx;
	editorTextNodeRemoveChunkRange(&tree->root, start_idx, remove_count);
	if (!editorTextNodeInsertChunkArray(&tree->root, start_idx, insert_chunks,
				insert_chunk_count)) {
		editorTextChunkArrayFree(insert_chunks, insert_chunk_count);
		return 0;
	}
	free(insert_chunks);
	editorTextNodeRecomputeSummary(&tree->root);
	(void)next_len;
	return 1;
}
