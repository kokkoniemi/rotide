#include "text/text_tree.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/text_summary.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES 1024
#define EDITOR_TEXT_TREE_MAX_DEPTH 32

struct descentEntry {
	struct editorTextNode *node;
	int child_idx;
};

/* ------------------------------------------------------------------ */
/* Node lifecycle                                                     */
/* ------------------------------------------------------------------ */

static struct editorTextNode *editorTextNodeAlloc(unsigned char is_leaf) {
	struct editorTextNode *node = editorMalloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	node->is_leaf = is_leaf;
	node->count = 0;
	editorTextSummaryZero(&node->summary);
	memset(&node->u, 0, sizeof(node->u));
	return node;
}

static void editorTextNodeFreeRecursive(struct editorTextNode *node) {
	if (node == NULL) {
		return;
	}
	if (node->is_leaf) {
		for (int i = 0; i < node->count; i++) {
			free(node->u.pieces[i].bytes);
		}
	} else {
		for (int i = 0; i < node->count; i++) {
			editorTextNodeFreeRecursive(node->u.children[i]);
		}
	}
	free(node);
}

/* ------------------------------------------------------------------ */
/* Pieces                                                             */
/* ------------------------------------------------------------------ */

static void pieceComputeSummary(struct editorTextChunk *piece) {
	editorTextSummaryFromBytes(piece->bytes, piece->len, &piece->summary);
}

static int pieceInitFromBytes(struct editorTextChunk *out, const char *src, size_t len) {
	out->bytes = NULL;
	out->len = 0;
	editorTextSummaryZero(&out->summary);
	if (len == 0) {
		return 1;
	}
	char *copy = editorMalloc(len);
	if (copy == NULL) {
		return 0;
	}
	memcpy(copy, src, len);
	out->bytes = copy;
	out->len = len;
	pieceComputeSummary(out);
	return 1;
}

/* ------------------------------------------------------------------ */
/* Summary recomputation                                              */
/* ------------------------------------------------------------------ */

static void editorTextNodeRecomputeSummary(struct editorTextNode *node) {
	struct editorTextSummary acc;
	editorTextSummaryZero(&acc);
	if (node == NULL) {
		return;
	}
	if (node->is_leaf) {
		for (int i = 0; i < node->count; i++) {
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &node->u.pieces[i].summary, &merged);
			acc = merged;
		}
	} else {
		for (int i = 0; i < node->count; i++) {
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &node->u.children[i]->summary, &merged);
			acc = merged;
		}
	}
	node->summary = acc;
}

#ifdef ROTIDE_TEXT_TREE_DEEP_CHECK
static void editorTextNodeRecomputeFromLeaves(const struct editorTextNode *node,
		struct editorTextSummary *out) {
	struct editorTextSummary acc;
	editorTextSummaryZero(&acc);
	if (node->is_leaf) {
		for (int i = 0; i < node->count; i++) {
			struct editorTextSummary piece_sum;
			editorTextSummaryFromBytes(node->u.pieces[i].bytes,
				node->u.pieces[i].len, &piece_sum);
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &piece_sum, &merged);
			acc = merged;
		}
	} else {
		for (int i = 0; i < node->count; i++) {
			struct editorTextSummary child_sum;
			editorTextNodeRecomputeFromLeaves(node->u.children[i], &child_sum);
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &child_sum, &merged);
			acc = merged;
		}
	}
	*out = acc;
}

static int summariesEqual(const struct editorTextSummary *a,
		const struct editorTextSummary *b) {
	return a->bytes == b->bytes && a->newlines == b->newlines &&
		a->first_line_bytes == b->first_line_bytes &&
		a->last_line_bytes == b->last_line_bytes &&
		a->max_line_bytes == b->max_line_bytes;
}

/* Recomputes the root summary from scratch and asserts it matches the
 * maintained value. O(N) per edit, so only on for deep-check builds.
 */
static void editorTextTreeAssertSummaryConsistent(const struct editorTextTree *tree) {
	if (tree == NULL || tree->root == NULL) {
		return;
	}
	struct editorTextSummary recomputed;
	editorTextNodeRecomputeFromLeaves(tree->root, &recomputed);
	assert(summariesEqual(&recomputed, &tree->root->summary));
}
#else
static void editorTextTreeAssertSummaryConsistent(const struct editorTextTree *tree) {
	(void)tree;
}
#endif

/* ------------------------------------------------------------------ */
/* Descent                                                            */
/* ------------------------------------------------------------------ */

/* Descend from root to a leaf carrying byte_offset along the way.
 * On return, byte_offset_out is the local offset within the chosen leaf piece,
 * leaf_piece_idx_out is the chosen piece index (may equal leaf->count for
 * past-the-end), and path[0..*path_len_out-1] records the internal-node spine
 * (path entries do NOT include the leaf itself).
 */
static int editorTextTreeDescend(const struct editorTextTree *tree, size_t byte_offset,
		struct descentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH], int *path_len_out,
		struct editorTextNode **leaf_out, int *leaf_piece_idx_out,
		size_t *byte_offset_in_piece_out) {
	if (tree == NULL || tree->root == NULL) {
		return 0;
	}
	if (byte_offset > tree->root->summary.bytes) {
		return 0;
	}

	struct editorTextNode *cur = tree->root;
	int depth = 0;
	size_t remaining = byte_offset;

	while (!cur->is_leaf) {
		if (depth >= EDITOR_TEXT_TREE_MAX_DEPTH) {
			return 0;
		}
		if (cur->count == 0) {
			/* Empty internal node — degenerate; treat as no descent possible. */
			return 0;
		}
		int idx = 0;
		size_t accum = 0;
		while (idx < cur->count) {
			size_t cb = cur->u.children[idx]->summary.bytes;
			if (remaining < accum + cb) {
				break;
			}
			accum += cb;
			idx++;
		}
		if (idx == cur->count) {
			idx = cur->count - 1;
			accum -= cur->u.children[idx]->summary.bytes;
		}
		path[depth].node = cur;
		path[depth].child_idx = idx;
		depth++;
		remaining -= accum;
		cur = cur->u.children[idx];
	}

	int piece_idx = 0;
	while (piece_idx < cur->count) {
		size_t pl = cur->u.pieces[piece_idx].len;
		if (remaining < pl) {
			break;
		}
		remaining -= pl;
		piece_idx++;
	}

	*path_len_out = depth;
	*leaf_out = cur;
	*leaf_piece_idx_out = piece_idx;
	*byte_offset_in_piece_out = remaining;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Leaf-level mutations                                               */
/* ------------------------------------------------------------------ */

/* Insert `len` new bytes at (leaf, piece_idx, local_off). The leaf may
 * temporarily exceed FANOUT (bounded by NODE_SLACK); caller must rebalance.
 * Returns 1 on success, 0 on alloc failure (leaf is left untouched on failure).
 */
static int editorTextLeafInsertBytes(struct editorTextNode *leaf, int piece_idx,
		size_t local_off, const char *bytes, size_t len) {
	if (len == 0) {
		return 1;
	}

	if (piece_idx == leaf->count || local_off == 0) {
		/* Insert before piece_idx (or at end). One new slot. */
		if (leaf->count + 1 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
			return 0;
		}
		struct editorTextChunk new_piece;
		if (!pieceInitFromBytes(&new_piece, bytes, len)) {
			return 0;
		}
		for (int i = leaf->count; i > piece_idx; i--) {
			leaf->u.pieces[i] = leaf->u.pieces[i - 1];
		}
		leaf->u.pieces[piece_idx] = new_piece;
		leaf->count++;
		return 1;
	}

	struct editorTextChunk *target = &leaf->u.pieces[piece_idx];
	if (local_off == target->len) {
		/* Boundary at end of piece: insert after piece_idx. */
		if (leaf->count + 1 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
			return 0;
		}
		struct editorTextChunk new_piece;
		if (!pieceInitFromBytes(&new_piece, bytes, len)) {
			return 0;
		}
		for (int i = leaf->count; i > piece_idx + 1; i--) {
			leaf->u.pieces[i] = leaf->u.pieces[i - 1];
		}
		leaf->u.pieces[piece_idx + 1] = new_piece;
		leaf->count++;
		return 1;
	}

	/* Split piece_idx in two, with new piece in between. */
	if (leaf->count + 2 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
		return 0;
	}
	size_t left_len = local_off;
	size_t right_len = target->len - local_off;

	char *left_bytes = editorMalloc(left_len);
	char *right_bytes = editorMalloc(right_len);
	char *new_bytes = editorMalloc(len);
	if (left_bytes == NULL || right_bytes == NULL || new_bytes == NULL) {
		free(left_bytes);
		free(right_bytes);
		free(new_bytes);
		return 0;
	}
	memcpy(left_bytes, target->bytes, left_len);
	memcpy(right_bytes, target->bytes + local_off, right_len);
	memcpy(new_bytes, bytes, len);

	free(target->bytes);

	for (int i = leaf->count + 1; i > piece_idx + 2; i--) {
		leaf->u.pieces[i] = leaf->u.pieces[i - 2];
	}

	leaf->u.pieces[piece_idx].bytes = left_bytes;
	leaf->u.pieces[piece_idx].len = left_len;
	pieceComputeSummary(&leaf->u.pieces[piece_idx]);

	leaf->u.pieces[piece_idx + 1].bytes = new_bytes;
	leaf->u.pieces[piece_idx + 1].len = len;
	pieceComputeSummary(&leaf->u.pieces[piece_idx + 1]);

	leaf->u.pieces[piece_idx + 2].bytes = right_bytes;
	leaf->u.pieces[piece_idx + 2].len = right_len;
	pieceComputeSummary(&leaf->u.pieces[piece_idx + 2]);

	leaf->count += 2;
	return 1;
}

/* Delete `take` bytes starting at (leaf, piece_idx, local_off). Caller guarantees
 * the delete stays within this single piece (local_off + take <= piece->len).
 * Returns 1 on success, 0 on alloc failure (leaf is left untouched).
 */
static int editorTextLeafDeleteWithinPiece(struct editorTextNode *leaf, int piece_idx,
		size_t local_off, size_t take) {
	if (take == 0) {
		return 1;
	}
	struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];

	if (local_off == 0 && take == piece->len) {
		free(piece->bytes);
		for (int i = piece_idx; i < leaf->count - 1; i++) {
			leaf->u.pieces[i] = leaf->u.pieces[i + 1];
		}
		leaf->count--;
		memset(&leaf->u.pieces[leaf->count], 0, sizeof(leaf->u.pieces[0]));
		return 1;
	}

	if (local_off == 0) {
		size_t new_len = piece->len - take;
		char *new_bytes = editorMalloc(new_len);
		if (new_bytes == NULL) {
			return 0;
		}
		memcpy(new_bytes, piece->bytes + take, new_len);
		free(piece->bytes);
		piece->bytes = new_bytes;
		piece->len = new_len;
		pieceComputeSummary(piece);
		return 1;
	}

	if (local_off + take == piece->len) {
		size_t new_len = local_off;
		char *new_bytes = editorMalloc(new_len);
		if (new_bytes == NULL) {
			return 0;
		}
		memcpy(new_bytes, piece->bytes, new_len);
		free(piece->bytes);
		piece->bytes = new_bytes;
		piece->len = new_len;
		pieceComputeSummary(piece);
		return 1;
	}

	/* Middle delete: split piece into [0, local_off) and [local_off+take, len). */
	if (leaf->count + 1 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
		return 0;
	}
	size_t left_len = local_off;
	size_t right_len = piece->len - local_off - take;
	char *left_bytes = editorMalloc(left_len);
	char *right_bytes = editorMalloc(right_len);
	if (left_bytes == NULL || right_bytes == NULL) {
		free(left_bytes);
		free(right_bytes);
		return 0;
	}
	memcpy(left_bytes, piece->bytes, left_len);
	memcpy(right_bytes, piece->bytes + local_off + take, right_len);
	free(piece->bytes);

	for (int i = leaf->count; i > piece_idx + 1; i--) {
		leaf->u.pieces[i] = leaf->u.pieces[i - 1];
	}

	leaf->u.pieces[piece_idx].bytes = left_bytes;
	leaf->u.pieces[piece_idx].len = left_len;
	pieceComputeSummary(&leaf->u.pieces[piece_idx]);

	leaf->u.pieces[piece_idx + 1].bytes = right_bytes;
	leaf->u.pieces[piece_idx + 1].len = right_len;
	pieceComputeSummary(&leaf->u.pieces[piece_idx + 1]);

	leaf->count++;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Split helpers                                                      */
/* ------------------------------------------------------------------ */

/* Merge `right` into `left` if they are the same kind and the combined entries
 * fit in a single node. Returns 1 if merged (caller must detach `right` from
 * its parent and free it), 0 otherwise.
 */
static int editorTextTreeMergeNode(struct editorTextNode *left,
		struct editorTextNode *right) {
	if (left == NULL || right == NULL || left->is_leaf != right->is_leaf) {
		return 0;
	}
	if ((int)left->count + (int)right->count > EDITOR_TEXT_TREE_FANOUT) {
		return 0;
	}
	if (left->is_leaf) {
		for (int i = 0; i < right->count; i++) {
			left->u.pieces[left->count + i] = right->u.pieces[i];
			right->u.pieces[i].bytes = NULL;
			right->u.pieces[i].len = 0;
			editorTextSummaryZero(&right->u.pieces[i].summary);
		}
	} else {
		for (int i = 0; i < right->count; i++) {
			left->u.children[left->count + i] = right->u.children[i];
			right->u.children[i] = NULL;
		}
	}
	left->count = (unsigned char)((int)left->count + (int)right->count);
	right->count = 0;
	editorTextNodeRecomputeSummary(left);
	return 1;
}

static struct editorTextNode *editorTextTreeSplitNode(struct editorTextNode *node) {
	struct editorTextNode *sibling = editorTextNodeAlloc(node->is_leaf);
	if (sibling == NULL) {
		return NULL;
	}
	int mid = node->count / 2;
	int moved = node->count - mid;

	if (node->is_leaf) {
		for (int i = 0; i < moved; i++) {
			sibling->u.pieces[i] = node->u.pieces[mid + i];
			memset(&node->u.pieces[mid + i], 0, sizeof(node->u.pieces[0]));
		}
	} else {
		for (int i = 0; i < moved; i++) {
			sibling->u.children[i] = node->u.children[mid + i];
			node->u.children[mid + i] = NULL;
		}
	}
	sibling->count = (unsigned char)moved;
	node->count = (unsigned char)mid;
	editorTextNodeRecomputeSummary(node);
	editorTextNodeRecomputeSummary(sibling);
	return sibling;
}

/* ------------------------------------------------------------------ */
/* Rebalance walk                                                     */
/* ------------------------------------------------------------------ */

/* After modifying the leaf at `leaf`, walks back up the recorded path:
 *   - Splits any node whose count > FANOUT (cascading new siblings up).
 *   - Removes empty leaves and empty internal nodes from their parents.
 *   - Recomputes the summary at every node on the path.
 * If the root splits, a new root is created. If the root collapses to a single
 * internal child, the child becomes the new root (depth shrinks).
 * Returns 1 on success, 0 on allocation failure.
 */
static int editorTextTreeRebalance(struct editorTextTree *tree,
		struct descentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH], int path_len,
		struct editorTextNode *leaf) {
	editorTextNodeRecomputeSummary(leaf);

	struct editorTextNode *pending_sibling = NULL;
	struct editorTextNode *current = leaf;

	if (current->count > EDITOR_TEXT_TREE_FANOUT) {
		pending_sibling = editorTextTreeSplitNode(current);
		if (pending_sibling == NULL) {
			return 0;
		}
	}
	int current_empty = (current->count == 0);

	for (int level = path_len - 1; level >= 0; level--) {
		struct editorTextNode *parent = path[level].node;
		int child_idx = path[level].child_idx;

		if (current_empty) {
			editorTextNodeFreeRecursive(parent->u.children[child_idx]);
			for (int i = child_idx; i < parent->count - 1; i++) {
				parent->u.children[i] = parent->u.children[i + 1];
			}
			parent->u.children[parent->count - 1] = NULL;
			parent->count--;
		} else if (current->count > 0 &&
				current->count < EDITOR_TEXT_TREE_FANOUT / 2) {
			/* Underflowed sub-FANOUT/2; try merging with a sibling so depth
			 * stays bounded after many deletes.
			 */
			struct editorTextNode *merged_with = NULL;
			int collapsed_child = -1;
			if (child_idx + 1 < parent->count &&
					editorTextTreeMergeNode(current,
						parent->u.children[child_idx + 1])) {
				merged_with = parent->u.children[child_idx + 1];
				collapsed_child = child_idx + 1;
			} else if (child_idx > 0 &&
					editorTextTreeMergeNode(parent->u.children[child_idx - 1],
						current)) {
				merged_with = current;
				collapsed_child = child_idx;
				/* `current` was absorbed; the path entry now references the
				 * absorbing left sibling for summary updates above.
				 */
				current = parent->u.children[child_idx - 1];
			}
			if (merged_with != NULL) {
				editorTextNodeFreeRecursive(merged_with);
				for (int i = collapsed_child; i < parent->count - 1; i++) {
					parent->u.children[i] = parent->u.children[i + 1];
				}
				parent->u.children[parent->count - 1] = NULL;
				parent->count--;
			}
		}

		if (pending_sibling != NULL) {
			int insert_at = current_empty ? child_idx : child_idx + 1;
			for (int i = parent->count; i > insert_at; i--) {
				parent->u.children[i] = parent->u.children[i - 1];
			}
			parent->u.children[insert_at] = pending_sibling;
			parent->count++;
			pending_sibling = NULL;
		}

		current_empty = (parent->count == 0);
		editorTextNodeRecomputeSummary(parent);

		if (parent->count > EDITOR_TEXT_TREE_FANOUT) {
			pending_sibling = editorTextTreeSplitNode(parent);
			if (pending_sibling == NULL) {
				return 0;
			}
		}
		current = parent;
	}

	if (pending_sibling != NULL) {
		struct editorTextNode *new_root = editorTextNodeAlloc(0);
		if (new_root == NULL) {
			editorTextNodeFreeRecursive(pending_sibling);
			return 0;
		}
		new_root->u.children[0] = tree->root;
		new_root->u.children[1] = pending_sibling;
		new_root->count = 2;
		editorTextNodeRecomputeSummary(new_root);
		tree->root = new_root;
	} else if (current_empty && tree->root == current && !current->is_leaf) {
		/* Root became empty internal node — replace with an empty leaf. */
		editorTextNodeFreeRecursive(tree->root);
		tree->root = editorTextNodeAlloc(1);
		if (tree->root == NULL) {
			return 0;
		}
	} else if (tree->root == current && !current->is_leaf && current->count == 1) {
		/* Root has a single internal child — collapse to reduce depth. */
		struct editorTextNode *only = current->u.children[0];
		current->u.children[0] = NULL;
		editorTextNodeFreeRecursive(current);
		tree->root = only;
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* Tree-level reads                                                    */
/* ------------------------------------------------------------------ */

void editorTextTreeInit(struct editorTextTree *tree) {
	if (tree == NULL) {
		return;
	}
	tree->root = editorTextNodeAlloc(1);
}

void editorTextTreeFree(struct editorTextTree *tree) {
	if (tree == NULL) {
		return;
	}
	editorTextNodeFreeRecursive(tree->root);
	tree->root = NULL;
}

size_t editorTextTreeLength(const struct editorTextTree *tree) {
	if (tree == NULL || tree->root == NULL) {
		return 0;
	}
	return tree->root->summary.bytes;
}

const struct editorTextSummary *editorTextTreeSummary(const struct editorTextTree *tree) {
	if (tree == NULL || tree->root == NULL) {
		return NULL;
	}
	return &tree->root->summary;
}

const char *editorTextTreeRead(const struct editorTextTree *tree, size_t byte_index,
		uint32_t *bytes_read) {
	if (bytes_read == NULL) {
		return NULL;
	}
	*bytes_read = 0;
	if (tree == NULL || tree->root == NULL || byte_index >= tree->root->summary.bytes) {
		return NULL;
	}

	struct descentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
	int path_len = 0;
	struct editorTextNode *leaf = NULL;
	int piece_idx = 0;
	size_t local_off = 0;
	if (!editorTextTreeDescend(tree, byte_index, path, &path_len, &leaf, &piece_idx,
			&local_off)) {
		return NULL;
	}
	if (piece_idx >= leaf->count) {
		return NULL;
	}
	struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];
	size_t remaining = piece->len - local_off;
	if (remaining > UINT32_MAX) {
		remaining = UINT32_MAX;
	}
	*bytes_read = (uint32_t)remaining;
	return piece->bytes + local_off;
}

int editorTextTreeCopyRange(const struct editorTextTree *tree, size_t start_byte,
		size_t end_byte, char *dst) {
	if (tree == NULL || tree->root == NULL || dst == NULL || start_byte > end_byte ||
			end_byte > tree->root->summary.bytes) {
		return 0;
	}
	size_t copied = 0;
	size_t want = end_byte - start_byte;
	while (copied < want) {
		uint32_t avail = 0;
		const char *ptr = editorTextTreeRead(tree, start_byte + copied, &avail);
		if (ptr == NULL || avail == 0) {
			return 0;
		}
		size_t take = avail < want - copied ? avail : want - copied;
		memcpy(dst + copied, ptr, take);
		copied += take;
	}
	return 1;
}

char *editorTextTreeDupRange(const struct editorTextTree *tree, size_t start_byte,
		size_t end_byte, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (tree == NULL || tree->root == NULL || start_byte > end_byte ||
			end_byte > tree->root->summary.bytes) {
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

/* ------------------------------------------------------------------ */
/* Mutation                                                           */
/* ------------------------------------------------------------------ */

static int editorTextTreeEnsureRoot(struct editorTextTree *tree) {
	if (tree == NULL) {
		return 0;
	}
	if (tree->root == NULL) {
		tree->root = editorTextNodeAlloc(1);
		if (tree->root == NULL) {
			return 0;
		}
	}
	return 1;
}

/* Insert up to one CHUNK_BYTES-sized piece at byte offset `at`. */
static int editorTextTreeInsertOnePiece(struct editorTextTree *tree, size_t at,
		const char *bytes, size_t len) {
	struct descentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
	int path_len = 0;
	struct editorTextNode *leaf = NULL;
	int piece_idx = 0;
	size_t local_off = 0;
	if (!editorTextTreeDescend(tree, at, path, &path_len, &leaf, &piece_idx, &local_off)) {
		return 0;
	}
	if (!editorTextLeafInsertBytes(leaf, piece_idx, local_off, bytes, len)) {
		return 0;
	}
	if (!editorTextTreeRebalance(tree, path, path_len, leaf)) {
		return 0;
	}
	return 1;
}

/* Delete bytes [at, at + len) where the entire span lies inside a single piece. */
static int editorTextTreeDeleteWithinPiece(struct editorTextTree *tree, size_t at,
		size_t take) {
	struct descentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
	int path_len = 0;
	struct editorTextNode *leaf = NULL;
	int piece_idx = 0;
	size_t local_off = 0;
	if (!editorTextTreeDescend(tree, at, path, &path_len, &leaf, &piece_idx, &local_off)) {
		return 0;
	}
	if (piece_idx >= leaf->count) {
		return 0;
	}
	struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];
	if (local_off + take > piece->len) {
		return 0;
	}
	if (!editorTextLeafDeleteWithinPiece(leaf, piece_idx, local_off, take)) {
		return 0;
	}
	if (!editorTextTreeRebalance(tree, path, path_len, leaf)) {
		return 0;
	}
	return 1;
}

/* Pre-stage the byte buffers for each new piece so OOM is detected before the
 * tree is touched. editorApplyDocumentEdit cannot roll back a partially-applied
 * document on a later pipeline failure, so the storage layer must mutate
 * atomically.
 */
struct stagedPieces {
	char **bytes;
	size_t *lens;
	int count;
};

static void stagedPiecesFree(struct stagedPieces *s) {
	if (s == NULL || s->bytes == NULL) {
		return;
	}
	for (int i = 0; i < s->count; i++) {
		free(s->bytes[i]);
	}
	free(s->bytes);
	free(s->lens);
	s->bytes = NULL;
	s->lens = NULL;
	s->count = 0;
}

static int stagedPiecesBuild(const char *new_text, size_t new_len,
		struct stagedPieces *out) {
	out->bytes = NULL;
	out->lens = NULL;
	out->count = 0;
	if (new_len == 0) {
		return 1;
	}
	size_t needed = (new_len + EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES - 1) /
		EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES;
	if (!editorSizeWithinInt(needed)) {
		return 0;
	}
	out->bytes = editorMalloc(needed * sizeof(*out->bytes));
	if (out->bytes == NULL) {
		return 0;
	}
	out->lens = editorMalloc(needed * sizeof(*out->lens));
	if (out->lens == NULL) {
		free(out->bytes);
		out->bytes = NULL;
		return 0;
	}
	for (size_t i = 0; i < needed; i++) {
		out->bytes[i] = NULL;
		out->lens[i] = 0;
	}

	size_t offset = 0;
	int idx = 0;
	while (offset < new_len) {
		size_t chunk = new_len - offset;
		if (chunk > EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES) {
			chunk = EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES;
		}
		char *copy = editorMalloc(chunk);
		if (copy == NULL) {
			out->count = idx;
			stagedPiecesFree(out);
			return 0;
		}
		memcpy(copy, new_text + offset, chunk);
		out->bytes[idx] = copy;
		out->lens[idx] = chunk;
		idx++;
		offset += chunk;
	}
	out->count = idx;
	return 1;
}

int editorTextTreeReplaceRange(struct editorTextTree *tree, size_t start_byte, size_t old_len,
		const char *new_text, size_t new_len) {
	if (tree == NULL) {
		return 0;
	}
	if (!editorTextTreeEnsureRoot(tree)) {
		return 0;
	}
	size_t total = tree->root->summary.bytes;
	if (start_byte > total || old_len > total - start_byte) {
		return 0;
	}
	if (new_len > 0 && new_text == NULL) {
		return 0;
	}

	struct stagedPieces staged;
	if (!stagedPiecesBuild(new_text, new_len, &staged)) {
		return 0;
	}

	size_t pos = start_byte;
	size_t remaining = old_len;
	while (remaining > 0) {
		struct descentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
		int path_len = 0;
		struct editorTextNode *leaf = NULL;
		int piece_idx = 0;
		size_t local_off = 0;
		if (!editorTextTreeDescend(tree, pos, path, &path_len, &leaf, &piece_idx,
				&local_off)) {
			stagedPiecesFree(&staged);
			return 0;
		}
		if (piece_idx >= leaf->count) {
			stagedPiecesFree(&staged);
			return 0;
		}
		struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];
		size_t in_piece_avail = piece->len - local_off;
		size_t take = in_piece_avail < remaining ? in_piece_avail : remaining;
		if (!editorTextTreeDeleteWithinPiece(tree, pos, take)) {
			stagedPiecesFree(&staged);
			return 0;
		}
		remaining -= take;
	}

	size_t inserted = 0;
	for (int i = 0; i < staged.count; i++) {
		if (!editorTextTreeInsertOnePiece(tree, start_byte + inserted,
				staged.bytes[i], staged.lens[i])) {
			stagedPiecesFree(&staged);
			return 0;
		}
		inserted += staged.lens[i];
	}

	stagedPiecesFree(&staged);
	editorTextTreeAssertSummaryConsistent(tree);
	return 1;
}

int editorTextTreeAppend(struct editorTextTree *tree, const char *text, size_t len) {
	if (tree == NULL || (len > 0 && text == NULL)) {
		return 0;
	}
	if (!editorTextTreeEnsureRoot(tree)) {
		return 0;
	}
	return editorTextTreeReplaceRange(tree, tree->root->summary.bytes, 0, text, len);
}

int editorTextTreeResetFromString(struct editorTextTree *tree, const char *text, size_t len) {
	if (tree == NULL) {
		return 0;
	}
	if (len > 0 && text == NULL) {
		return 0;
	}

	struct editorTextTree rebuilt;
	editorTextTreeInit(&rebuilt);
	if (rebuilt.root == NULL) {
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

int editorTextTreeLocateLine(const struct editorTextTree *tree, int line_idx,
		size_t *start_byte_out) {
	if (start_byte_out == NULL || tree == NULL || tree->root == NULL || line_idx < 0) {
		return 0;
	}
	if (line_idx == 0) {
		*start_byte_out = 0;
		return 1;
	}
	if (line_idx > tree->root->summary.newlines) {
		return 0;
	}

	struct editorTextNode *cur = tree->root;
	int remaining = line_idx;
	size_t acc_bytes = 0;
	int depth = 0;

	while (!cur->is_leaf) {
		if (depth >= EDITOR_TEXT_TREE_MAX_DEPTH || cur->count == 0) {
			return 0;
		}
		int idx = 0;
		while (idx < cur->count) {
			int cnl = cur->u.children[idx]->summary.newlines;
			if (remaining <= cnl) {
				break;
			}
			remaining -= cnl;
			acc_bytes += cur->u.children[idx]->summary.bytes;
			idx++;
		}
		if (idx >= cur->count) {
			return 0;
		}
		cur = cur->u.children[idx];
		depth++;
	}

	int piece_idx = 0;
	while (piece_idx < cur->count) {
		int pnl = cur->u.pieces[piece_idx].summary.newlines;
		if (remaining <= pnl) {
			break;
		}
		remaining -= pnl;
		acc_bytes += cur->u.pieces[piece_idx].len;
		piece_idx++;
	}
	if (piece_idx >= cur->count) {
		return 0;
	}

	const struct editorTextChunk *piece = &cur->u.pieces[piece_idx];
	int found = 0;
	for (size_t i = 0; i < piece->len; i++) {
		if (piece->bytes[i] != '\n') {
			continue;
		}
		found++;
		if (found == remaining) {
			*start_byte_out = acc_bytes + i + 1;
			return 1;
		}
	}
	return 0;
}

int editorTextTreeLineForByte(const struct editorTextTree *tree, size_t byte,
		int *line_idx_out) {
	if (line_idx_out == NULL || tree == NULL || tree->root == NULL) {
		return 0;
	}
	if (byte >= tree->root->summary.bytes) {
		return 0;
	}

	struct editorTextNode *cur = tree->root;
	size_t remaining = byte;
	int acc_newlines = 0;
	int depth = 0;

	while (!cur->is_leaf) {
		if (depth >= EDITOR_TEXT_TREE_MAX_DEPTH || cur->count == 0) {
			return 0;
		}
		int idx = 0;
		while (idx < cur->count) {
			size_t cb = cur->u.children[idx]->summary.bytes;
			if (remaining < cb) {
				break;
			}
			remaining -= cb;
			acc_newlines += cur->u.children[idx]->summary.newlines;
			idx++;
		}
		if (idx >= cur->count) {
			return 0;
		}
		cur = cur->u.children[idx];
		depth++;
	}

	int piece_idx = 0;
	while (piece_idx < cur->count) {
		size_t pl = cur->u.pieces[piece_idx].len;
		if (remaining < pl) {
			break;
		}
		remaining -= pl;
		acc_newlines += cur->u.pieces[piece_idx].summary.newlines;
		piece_idx++;
	}
	if (piece_idx >= cur->count) {
		return 0;
	}

	const struct editorTextChunk *piece = &cur->u.pieces[piece_idx];
	for (size_t i = 0; i < remaining; i++) {
		if (piece->bytes[i] == '\n') {
			acc_newlines++;
		}
	}
	*line_idx_out = acc_newlines;
	return 1;
}

int editorTextTreeResetFromTextSource(struct editorTextTree *tree,
		const struct editorTextSource *source) {
	if (tree == NULL || source == NULL || source->read == NULL) {
		return 0;
	}

	struct editorTextTree rebuilt;
	editorTextTreeInit(&rebuilt);
	if (rebuilt.root == NULL) {
		return 0;
	}

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
