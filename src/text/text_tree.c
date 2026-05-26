#include "text/text_tree.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/text_buffer.h"
#include "text/text_summary.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Insert and bulk-load operations split their bytes into pieces of this size.
 * Smaller = more pieces but cheaper mid-piece splits later (each split rescans
 * the slice for its summary). 1024 keeps a 1 MB load to ~1000 pieces, which
 * fits in ~3 tree levels at FANOUT 16. Bump if profiles show too many tiny
 * pieces on huge files; shrink if mid-piece edit summary recomputes dominate.
 */
#define EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES 1024

/* Hard cap on descent depth. With FANOUT 16 a depth of 16 already addresses
 * ~1.8e19 entries, well past any addressable doc — generous enough to absorb
 * temporarily unbalanced trees mid-rebalance without false-positive failures.
 */
#define EDITOR_TEXT_TREE_MAX_DEPTH 16

/* Coalesce inserts up to this size into the preceding piece. Sized for typing
 * runs; larger inserts (paste, bulk load) stay as their own pieces so future
 * mid-piece splits don't have to rescan giant slices for summaries.
 */
#define EDITOR_TEXT_TREE_COALESCE_MAX_BYTES 64

struct textTreeDescentEntry {
	struct editorTextNode *node;
	int child_idx;
};

static const char *pieceBytes(const struct editorTextChunk *piece) {
	return piece->buf->bytes + piece->offset;
}

static void pieceComputeSummary(struct editorTextChunk *piece) {
	editorTextSummaryFromBytes(pieceBytes(piece), piece->len, &piece->summary);
}

/* Initialise a piece as a slice into `buf`. Retains the buffer. */
static void pieceInitFromBuffer(struct editorTextChunk *out, struct editorTextBuffer *buf,
                                size_t offset, size_t len) {
	out->buf = editorTextBufferRetain(buf);
	out->offset = offset;
	out->len = len;
	pieceComputeSummary(out);
}

static void pieceDestroy(struct editorTextChunk *piece) {
	editorTextBufferRelease(piece->buf);
	piece->buf = NULL;
	piece->offset = 0;
	piece->len = 0;
	editorTextSummaryZero(&piece->summary);
}

static struct editorTextNode *textNodeAlloc(unsigned char is_leaf) {
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

static void textNodeFree(struct editorTextNode *node) {
	if (node == NULL) {
		return;
	}
	if (node->is_leaf) {
		for (int i = 0; i < node->count; i++) {
			pieceDestroy(&node->u.pieces[i]);
		}
	} else {
		for (int i = 0; i < node->count; i++) {
			textNodeFree(node->u.children[i]);
		}
	}
	free(node);
}

static void textNodeRecomputeSummary(struct editorTextNode *node) {
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
static void textNodeRecomputeFromLeaves(const struct editorTextNode *node,
                                        struct editorTextSummary *out) {
	struct editorTextSummary acc;
	editorTextSummaryZero(&acc);
	if (node->is_leaf) {
		for (int i = 0; i < node->count; i++) {
			struct editorTextSummary piece_sum;
			editorTextSummaryFromBytes(pieceBytes(&node->u.pieces[i]),
			                           node->u.pieces[i].len, &piece_sum);
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &piece_sum, &merged);
			acc = merged;
		}
	} else {
		for (int i = 0; i < node->count; i++) {
			struct editorTextSummary child_sum;
			textNodeRecomputeFromLeaves(node->u.children[i], &child_sum);
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &child_sum, &merged);
			acc = merged;
		}
	}
	*out = acc;
}

static int textTreeSummariesEqual(const struct editorTextSummary *a,
                                  const struct editorTextSummary *b) {
	return a->bytes == b->bytes && a->newlines == b->newlines &&
	       a->first_line_bytes == b->first_line_bytes &&
	       a->last_line_bytes == b->last_line_bytes && a->max_line_bytes == b->max_line_bytes;
}

static void textTreeAssertSummaryConsistent(const struct editorTextTree *tree) {
	if (tree == NULL || tree->root == NULL) {
		return;
	}
	struct editorTextSummary recomputed;
	textNodeRecomputeFromLeaves(tree->root, &recomputed);
	assert(textTreeSummariesEqual(&recomputed, &tree->root->summary));
}
#else
static void textTreeAssertSummaryConsistent(const struct editorTextTree *tree) {
	(void)tree;
}
#endif

static int textTreeDescend(const struct editorTextTree *tree, size_t byte_offset,
                           struct textTreeDescentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH],
                           int *path_len_out, struct editorTextNode **leaf_out,
                           int *leaf_piece_idx_out, size_t *byte_offset_in_piece_out) {
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

/* Typing fast path: if the new slice is a contiguous extension of the
 * neighbour piece already sitting in the same buffer at the insertion point,
 * just grow that piece's len. Avoids spawning a fresh piece per keystroke and
 * keeps editors with bursty typing close to one-piece-per-typing-run.
 */
static int textLeafCoalesceOnInsert(struct editorTextNode *leaf, int piece_idx, size_t local_off,
                                    struct editorTextBuffer *buf, size_t buf_offset,
                                    size_t buf_len) {
	if (buf_len > EDITOR_TEXT_TREE_COALESCE_MAX_BYTES) {
		return 0;
	}
	/* Only attempt coalescing within the current leaf. When the insertion
	 * point lands at piece_idx == 0 the predecessor lives in the previous
	 * leaf (different sibling, descent doesn't expose it); skip rather than
	 * pay the cross-leaf walk for a marginal piece-count win.
	 */
	struct editorTextChunk *neighbor = NULL;
	if (piece_idx == leaf->count || local_off == 0) {
		if (piece_idx == 0) {
			return 0;
		}
		neighbor = &leaf->u.pieces[piece_idx - 1];
	} else if (local_off == leaf->u.pieces[piece_idx].len) {
		neighbor = &leaf->u.pieces[piece_idx];
	} else {
		return 0;
	}
	if (neighbor->buf != buf || neighbor->offset + neighbor->len != buf_offset) {
		return 0;
	}
	/* Merge summaries incrementally — recomputing from scratch would make
	 * sequential 1 KB appends quadratic in the resulting piece length.
	 */
	struct editorTextSummary added;
	editorTextSummaryFromBytes(buf->bytes + buf_offset, buf_len, &added);
	struct editorTextSummary merged;
	editorTextSummaryMerge(&neighbor->summary, &added, &merged);
	neighbor->summary = merged;
	neighbor->len += buf_len;
	return 1;
}

/* Insert one new piece (a slice into `buf`) at (leaf, piece_idx, local_off).
 * The leaf may temporarily exceed FANOUT (bounded by NODE_SLACK); caller must
 * rebalance. Returns 1 on success, 0 on capacity exhaustion.
 */
static int textLeafInsertPiece(struct editorTextNode *leaf, int piece_idx, size_t local_off,
                               struct editorTextBuffer *buf, size_t buf_offset, size_t buf_len) {
	if (buf_len == 0) {
		return 1;
	}

	if (textLeafCoalesceOnInsert(leaf, piece_idx, local_off, buf, buf_offset, buf_len)) {
		return 1;
	}

	if (piece_idx == leaf->count || local_off == 0) {
		if (leaf->count + 1 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
			return 0;
		}
		for (int i = leaf->count; i > piece_idx; i--) {
			leaf->u.pieces[i] = leaf->u.pieces[i - 1];
		}
		pieceInitFromBuffer(&leaf->u.pieces[piece_idx], buf, buf_offset, buf_len);
		leaf->count++;
		return 1;
	}

	struct editorTextChunk *target = &leaf->u.pieces[piece_idx];
	if (local_off == target->len) {
		if (leaf->count + 1 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
			return 0;
		}
		for (int i = leaf->count; i > piece_idx + 1; i--) {
			leaf->u.pieces[i] = leaf->u.pieces[i - 1];
		}
		pieceInitFromBuffer(&leaf->u.pieces[piece_idx + 1], buf, buf_offset, buf_len);
		leaf->count++;
		return 1;
	}

	/* Mid-piece insert: keep target as the left slice (no ref change), then
	 * insert the new piece and a fresh right slice sharing target's buffer. */
	if (leaf->count + 2 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
		return 0;
	}
	struct editorTextBuffer *target_buf = target->buf;
	size_t target_offset = target->offset;
	size_t right_len = target->len - local_off;

	for (int i = leaf->count + 1; i > piece_idx + 2; i--) {
		leaf->u.pieces[i] = leaf->u.pieces[i - 2];
	}
	target->len = local_off;
	pieceComputeSummary(target);
	pieceInitFromBuffer(&leaf->u.pieces[piece_idx + 1], buf, buf_offset, buf_len);
	pieceInitFromBuffer(&leaf->u.pieces[piece_idx + 2], target_buf, target_offset + local_off,
	                    right_len);
	leaf->count += 2;
	return 1;
}

/* Delete `take` bytes starting at (leaf, piece_idx, local_off). Caller
 * guarantees the delete stays within this single piece. Returns 1 on success,
 * 0 on capacity exhaustion.
 */
static int textLeafDeleteWithinPiece(struct editorTextNode *leaf, int piece_idx, size_t local_off,
                                     size_t take) {
	if (take == 0) {
		return 1;
	}
	struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];

	if (local_off == 0 && take == piece->len) {
		pieceDestroy(piece);
		for (int i = piece_idx; i < leaf->count - 1; i++) {
			leaf->u.pieces[i] = leaf->u.pieces[i + 1];
		}
		leaf->count--;
		memset(&leaf->u.pieces[leaf->count], 0, sizeof(leaf->u.pieces[0]));
		return 1;
	}

	if (local_off == 0) {
		piece->offset += take;
		piece->len -= take;
		pieceComputeSummary(piece);
		return 1;
	}

	if (local_off + take == piece->len) {
		piece->len = local_off;
		pieceComputeSummary(piece);
		return 1;
	}

	/* Middle delete: keep the original piece as the left slice and split off a
	 * new right slice sharing the same buffer. */
	if (leaf->count + 1 > EDITOR_TEXT_TREE_NODE_CAPACITY) {
		return 0;
	}
	struct editorTextBuffer *buf = piece->buf;
	size_t base_offset = piece->offset;
	size_t right_len = piece->len - local_off - take;

	for (int i = leaf->count; i > piece_idx + 1; i--) {
		leaf->u.pieces[i] = leaf->u.pieces[i - 1];
	}
	piece->len = local_off;
	pieceComputeSummary(piece);
	pieceInitFromBuffer(&leaf->u.pieces[piece_idx + 1], buf, base_offset + local_off + take,
	                    right_len);
	leaf->count++;
	return 1;
}

static int textTreeMergeNode(struct editorTextNode *left, struct editorTextNode *right) {
	if (left == NULL || right == NULL || left->is_leaf != right->is_leaf) {
		return 0;
	}
	if ((int)left->count + (int)right->count > EDITOR_TEXT_TREE_FANOUT) {
		return 0;
	}
	if (left->is_leaf) {
		for (int i = 0; i < right->count; i++) {
			left->u.pieces[left->count + i] = right->u.pieces[i];
			memset(&right->u.pieces[i], 0, sizeof(right->u.pieces[0]));
		}
	} else {
		for (int i = 0; i < right->count; i++) {
			left->u.children[left->count + i] = right->u.children[i];
			right->u.children[i] = NULL;
		}
	}
	left->count = (unsigned char)((int)left->count + (int)right->count);
	right->count = 0;
	textNodeRecomputeSummary(left);
	return 1;
}

static struct editorTextNode *textTreeSplitNode(struct editorTextNode *node) {
	struct editorTextNode *sibling = textNodeAlloc(node->is_leaf);
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
	textNodeRecomputeSummary(node);
	textNodeRecomputeSummary(sibling);
	return sibling;
}

static int textTreeRebalance(struct editorTextTree *tree,
                             struct textTreeDescentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH],
                             int path_len, struct editorTextNode *leaf) {
	textNodeRecomputeSummary(leaf);

	struct editorTextNode *pending_sibling = NULL;
	struct editorTextNode *current = leaf;

	if (current->count > EDITOR_TEXT_TREE_FANOUT) {
		pending_sibling = textTreeSplitNode(current);
		if (pending_sibling == NULL) {
			return 0;
		}
	}
	int current_empty = (current->count == 0);

	for (int level = path_len - 1; level >= 0; level--) {
		struct editorTextNode *parent = path[level].node;
		int child_idx = path[level].child_idx;

		if (current_empty) {
			textNodeFree(parent->u.children[child_idx]);
			for (int i = child_idx; i < parent->count - 1; i++) {
				parent->u.children[i] = parent->u.children[i + 1];
			}
			parent->u.children[parent->count - 1] = NULL;
			parent->count--;
		} else if (current->count > 0 && current->count < EDITOR_TEXT_TREE_FANOUT / 2) {
			struct editorTextNode *merged_with = NULL;
			int collapsed_child = -1;
			if (child_idx + 1 < parent->count &&
			    textTreeMergeNode(current, parent->u.children[child_idx + 1])) {
				merged_with = parent->u.children[child_idx + 1];
				collapsed_child = child_idx + 1;
			} else if (child_idx > 0 &&
			           textTreeMergeNode(parent->u.children[child_idx - 1], current)) {
				merged_with = current;
				collapsed_child = child_idx;
				current = parent->u.children[child_idx - 1];
			}
			if (merged_with != NULL) {
				textNodeFree(merged_with);
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
		textNodeRecomputeSummary(parent);

		if (parent->count > EDITOR_TEXT_TREE_FANOUT) {
			pending_sibling = textTreeSplitNode(parent);
			if (pending_sibling == NULL) {
				return 0;
			}
		}
		current = parent;
	}

	if (pending_sibling != NULL) {
		struct editorTextNode *new_root = textNodeAlloc(0);
		if (new_root == NULL) {
			textNodeFree(pending_sibling);
			return 0;
		}
		new_root->u.children[0] = tree->root;
		new_root->u.children[1] = pending_sibling;
		new_root->count = 2;
		textNodeRecomputeSummary(new_root);
		tree->root = new_root;
	} else if (current_empty && tree->root == current && !current->is_leaf) {
		textNodeFree(tree->root);
		tree->root = textNodeAlloc(1);
		if (tree->root == NULL) {
			return 0;
		}
	} else if (tree->root == current && !current->is_leaf && current->count == 1) {
		struct editorTextNode *only = current->u.children[0];
		current->u.children[0] = NULL;
		textNodeFree(current);
		tree->root = only;
	}
	return 1;
}

int editorTextTreeInit(struct editorTextTree *tree) {
	if (tree == NULL) {
		return 0;
	}
	tree->root = textNodeAlloc(1);
	tree->add_buf = editorTextBufferAlloc(0);
	if (tree->root == NULL || tree->add_buf == NULL) {
		textNodeFree(tree->root);
		editorTextBufferRelease(tree->add_buf);
		tree->root = NULL;
		tree->add_buf = NULL;
		return 0;
	}
	return 1;
}

void editorTextTreeFree(struct editorTextTree *tree) {
	if (tree == NULL) {
		return;
	}
	textNodeFree(tree->root);
	tree->root = NULL;
	editorTextBufferRelease(tree->add_buf);
	tree->add_buf = NULL;
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

	struct textTreeDescentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
	int path_len = 0;
	struct editorTextNode *leaf = NULL;
	int piece_idx = 0;
	size_t local_off = 0;
	if (!textTreeDescend(tree, byte_index, path, &path_len, &leaf, &piece_idx, &local_off)) {
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
	return pieceBytes(piece) + local_off;
}

int editorTextTreeCopyRange(const struct editorTextTree *tree, size_t start_byte, size_t end_byte,
                            char *dst) {
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

char *editorTextTreeDupRange(const struct editorTextTree *tree, size_t start_byte, size_t end_byte,
                             size_t *len_out) {
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

/* Lazily fills in any tree fields editorTextTreeInit may have failed to
 * allocate. Mutation entry points call this so callers that ignored an Init
 * failure still get a chance to recover when memory pressure eases.
 */
static int textTreeEnsureInitialised(struct editorTextTree *tree) {
	if (tree == NULL) {
		return 0;
	}
	if (tree->root == NULL) {
		tree->root = textNodeAlloc(1);
		if (tree->root == NULL) {
			return 0;
		}
	}
	if (tree->add_buf == NULL) {
		tree->add_buf = editorTextBufferAlloc(0);
		if (tree->add_buf == NULL) {
			return 0;
		}
	}
	return 1;
}

/* Insert one piece slice at byte offset `at`. */
static int textTreeInsertOnePiece(struct editorTextTree *tree, size_t at,
                                  struct editorTextBuffer *buf, size_t buf_offset, size_t buf_len) {
	struct textTreeDescentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
	int path_len = 0;
	struct editorTextNode *leaf = NULL;
	int piece_idx = 0;
	size_t local_off = 0;
	if (!textTreeDescend(tree, at, path, &path_len, &leaf, &piece_idx, &local_off)) {
		return 0;
	}
	if (!textLeafInsertPiece(leaf, piece_idx, local_off, buf, buf_offset, buf_len)) {
		return 0;
	}
	if (!textTreeRebalance(tree, path, path_len, leaf)) {
		return 0;
	}
	return 1;
}

static int textTreeDeleteWithinPiece(struct editorTextTree *tree, size_t at, size_t take) {
	struct textTreeDescentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
	int path_len = 0;
	struct editorTextNode *leaf = NULL;
	int piece_idx = 0;
	size_t local_off = 0;
	if (!textTreeDescend(tree, at, path, &path_len, &leaf, &piece_idx, &local_off)) {
		return 0;
	}
	if (piece_idx >= leaf->count) {
		return 0;
	}
	struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];
	if (local_off + take > piece->len) {
		return 0;
	}
	if (!textLeafDeleteWithinPiece(leaf, piece_idx, local_off, take)) {
		return 0;
	}
	if (!textTreeRebalance(tree, path, path_len, leaf)) {
		return 0;
	}
	return 1;
}

int editorTextTreeReplaceRange(struct editorTextTree *tree, size_t start_byte, size_t old_len,
                               const char *new_text, size_t new_len) {
	if (tree == NULL) {
		return 0;
	}
	if (!textTreeEnsureInitialised(tree)) {
		return 0;
	}
	size_t total = tree->root->summary.bytes;
	if (start_byte > total || old_len > total - start_byte) {
		return 0;
	}
	if (new_len > 0 && new_text == NULL) {
		return 0;
	}

	/* Stage the inserted bytes in the add buffer first; if this fails we
	 * return before touching the tree. Piece slices (refcount bumps + summary
	 * arithmetic) cannot fail; the only remaining alloc path is internal-node
	 * splits inside the rebalance walk below. A split failure mid-loop leaves
	 * the tree structurally valid (summaries are recomputed at each step and
	 * no piece refs leak) but with byte content in an intermediate state
	 * between old and new. Callers that need strict atomicity must layer
	 * their own revert on top — see editorApplyDocumentEdit.
	 */
	size_t insert_base = 0;
	if (new_len > 0) {
		if (!textTreeEnsureInitialised(tree)) {
			return 0;
		}
		if (!editorTextBufferAppend(tree->add_buf, new_text, new_len, &insert_base)) {
			return 0;
		}
	}

	/* Delete the old range piece-by-piece. */
	size_t pos = start_byte;
	size_t remaining = old_len;
	while (remaining > 0) {
		struct textTreeDescentEntry path[EDITOR_TEXT_TREE_MAX_DEPTH];
		int path_len = 0;
		struct editorTextNode *leaf = NULL;
		int piece_idx = 0;
		size_t local_off = 0;
		if (!textTreeDescend(tree, pos, path, &path_len, &leaf, &piece_idx, &local_off)) {
			return 0;
		}
		if (piece_idx >= leaf->count) {
			return 0;
		}
		struct editorTextChunk *piece = &leaf->u.pieces[piece_idx];
		size_t in_piece_avail = piece->len - local_off;
		size_t take = in_piece_avail < remaining ? in_piece_avail : remaining;
		if (!textTreeDeleteWithinPiece(tree, pos, take)) {
			return 0;
		}
		remaining -= take;
	}

	/* Insert new pieces. Chunking keeps individual pieces small enough that
	 * future mid-piece edits stay cheap and leaves don't accumulate giant
	 * slices that resist coalescing.
	 */
	size_t inserted = 0;
	while (inserted < new_len) {
		size_t chunk = new_len - inserted;
		if (chunk > EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES) {
			chunk = EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES;
		}
		if (!textTreeInsertOnePiece(tree, start_byte + inserted, tree->add_buf,
		                            insert_base + inserted, chunk)) {
			return 0;
		}
		inserted += chunk;
	}

	textTreeAssertSummaryConsistent(tree);
	return 1;
}

int editorTextTreeReserveAddBufCapacity(struct editorTextTree *tree, size_t additional_bytes) {
	if (tree == NULL) {
		return 0;
	}
	if (!textTreeEnsureInitialised(tree)) {
		return 0;
	}
	size_t min_cap = 0;
	if (!editorSizeAdd(tree->add_buf->len, additional_bytes, &min_cap)) {
		return 0;
	}
	return editorTextBufferReserve(tree->add_buf, min_cap);
}

int editorTextTreeAppend(struct editorTextTree *tree, const char *text, size_t len) {
	if (tree == NULL || (len > 0 && text == NULL)) {
		return 0;
	}
	if (!textTreeEnsureInitialised(tree)) {
		return 0;
	}
	return editorTextTreeReplaceRange(tree, tree->root->summary.bytes, 0, text, len);
}

/* Build a fresh tree containing the bytes already loaded into `original`,
 * chunked into pieces that share the buffer. The tree retains the buffer for
 * its pieces but does not steal `original`'s ref; caller still owns its ref.
 */
static int textTreeBuildFromOriginal(struct editorTextTree *out,
                                     struct editorTextBuffer *original) {
	if (!editorTextTreeInit(out)) {
		return 0;
	}
	size_t total = original->len;
	size_t offset = 0;
	while (offset < total) {
		size_t chunk = total - offset;
		if (chunk > EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES) {
			chunk = EDITOR_TEXT_TREE_INSERT_CHUNK_BYTES;
		}
		if (!textTreeInsertOnePiece(out, out->root->summary.bytes, original, offset,
		                            chunk)) {
			editorTextTreeFree(out);
			return 0;
		}
		offset += chunk;
	}
	return 1;
}

int editorTextTreeResetFromString(struct editorTextTree *tree, const char *text, size_t len) {
	if (tree == NULL) {
		return 0;
	}
	if (len > 0 && text == NULL) {
		return 0;
	}

	struct editorTextBuffer *original = editorTextBufferAlloc(len);
	if (original == NULL) {
		return 0;
	}
	if (len > 0 && !editorTextBufferAppend(original, text, len, NULL)) {
		editorTextBufferRelease(original);
		return 0;
	}

	struct editorTextTree rebuilt;
	if (!textTreeBuildFromOriginal(&rebuilt, original)) {
		editorTextBufferRelease(original);
		return 0;
	}
	editorTextBufferRelease(original);

	editorTextTreeFree(tree);
	*tree = rebuilt;
	return 1;
}

int editorTextTreeResetFromTextSource(struct editorTextTree *tree,
                                      const struct editorTextSource *source) {
	if (tree == NULL || source == NULL || source->read == NULL) {
		return 0;
	}

	struct editorTextBuffer *original = editorTextBufferAlloc(source->length);
	if (original == NULL) {
		return 0;
	}

	size_t offset = 0;
	while (offset < source->length) {
		uint32_t chunk_len = 0;
		const char *chunk = source->read(source, offset, &chunk_len);
		if (chunk == NULL || chunk_len == 0) {
			editorTextBufferRelease(original);
			return 0;
		}
		size_t remaining = source->length - offset;
		if ((size_t)chunk_len > remaining ||
		    !editorTextBufferAppend(original, chunk, (size_t)chunk_len, NULL)) {
			editorTextBufferRelease(original);
			return 0;
		}
		offset += (size_t)chunk_len;
	}

	struct editorTextTree rebuilt;
	if (!textTreeBuildFromOriginal(&rebuilt, original)) {
		editorTextBufferRelease(original);
		return 0;
	}
	editorTextBufferRelease(original);

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
	const char *bytes = pieceBytes(piece);
	int found = 0;
	for (size_t i = 0; i < piece->len; i++) {
		if (bytes[i] != '\n') {
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

static void textNodeCollectStats(const struct editorTextNode *node, int depth,
                                 struct editorTextTreeStats *out) {
	if (node == NULL) {
		return;
	}
	if (depth > out->max_depth) {
		out->max_depth = depth;
	}
	if (node->is_leaf) {
		out->leaf_count++;
		out->piece_count += node->count;
		return;
	}
	out->internal_node_count++;
	for (int i = 0; i < node->count; i++) {
		textNodeCollectStats(node->u.children[i], depth + 1, out);
	}
}

void editorTextTreeCollectStats(const struct editorTextTree *tree,
                                struct editorTextTreeStats *out) {
	if (out == NULL) {
		return;
	}
	out->leaf_count = 0;
	out->internal_node_count = 0;
	out->piece_count = 0;
	out->max_depth = 0;
	out->total_bytes = 0;
	if (tree == NULL || tree->root == NULL) {
		return;
	}
	textNodeCollectStats(tree->root, 0, out);
	out->total_bytes = tree->root->summary.bytes;
}

int editorTextTreeLineForByte(const struct editorTextTree *tree, size_t byte, int *line_idx_out) {
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
	const char *bytes = pieceBytes(piece);
	for (size_t i = 0; i < remaining; i++) {
		if (bytes[i] == '\n') {
			acc_newlines++;
		}
	}
	*line_idx_out = acc_newlines;
	return 1;
}
