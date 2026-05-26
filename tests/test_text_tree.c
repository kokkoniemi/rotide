#include "test_case.h"
#include "test_helpers.h"
#include "text/text_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int tree_byte_at(const struct editorTextTree *tree, size_t off, char *out) {
	uint32_t avail = 0;
	const char *ptr = editorTextTreeRead(tree, off, &avail);
	if (ptr == NULL || avail == 0) {
		return 0;
	}
	*out = *ptr;
	return 1;
}

static int tree_matches_string(const struct editorTextTree *tree, const char *expected,
                               size_t expected_len) {
	if (editorTextTreeLength(tree) != expected_len) {
		(void)fprintf(stderr, "tree_len=%zu expected=%zu\n", editorTextTreeLength(tree),
		        expected_len);
		return 0;
	}
	for (size_t i = 0; i < expected_len; i++) {
		char b = 0;
		if (!tree_byte_at(tree, i, &b)) {
			(void)fprintf(stderr, "tree_byte_at(%zu) failed\n", i);
			return 0;
		}
		if (b != expected[i]) {
			(void)fprintf(stderr, "byte diff at %zu: tree=0x%02x expected=0x%02x\n", i,
			        (unsigned char)b, (unsigned char)expected[i]);
			return 0;
		}
	}
	return 1;
}

/* Force a leaf split: insert one byte at a time so each insert lands inside a
 * piece and produces a piece-split. After enough inserts the leaf overflows
 * FANOUT and editorTextTreeSplitNode runs.
 */
static int test_text_tree_leaf_split_grows_tree(void) {
	struct editorTextTree tree;
	editorTextTreeInit(&tree);
	ASSERT_TRUE(tree.root != NULL);

	const char *seed = "abcdefghij";
	size_t seed_len = strlen(seed);
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, seed, seed_len));
	/* Single leaf, single piece at this point. */

	char expected[256];
	memcpy(expected, seed, seed_len);
	expected[seed_len] = '\0';
	size_t cur_len = seed_len;

	/* Insert into the middle 40 times — each mid-piece insert turns 1 piece
	 * into 3, so the leaf will overflow after a few rounds and split.
	 */
	for (int i = 0; i < 40; i++) {
		size_t at = cur_len / 2;
		char c = (char)('A' + (i % 26));
		ASSERT_TRUE(editorTextTreeReplaceRange(&tree, at, 0, &c, 1));
		memmove(expected + at + 1, expected + at, cur_len - at);
		expected[at] = c;
		cur_len++;
		ASSERT_TRUE(tree_matches_string(&tree, expected, cur_len));
	}

	/* After many mid-piece inserts, the root must have become an internal
	 * node (depth >= 1) because the single leaf overflowed and split.
	 */
	ASSERT_TRUE(tree.root->is_leaf == 0);
	ASSERT_TRUE(tree.root->count >= 2);

	editorTextTreeFree(&tree);
	return 0;
}

/* Force enough inserts to grow the tree to depth >= 3, exercising both leaf
 * and internal-node splits.
 */
static int test_text_tree_split_grows_internal_nodes(void) {
	struct editorTextTree tree;
	editorTextTreeInit(&tree);
	ASSERT_TRUE(tree.root != NULL);

	char buf[4096];
	for (int i = 0; i < (int)sizeof(buf); i++) {
		buf[i] = (char)('a' + (i % 26));
	}
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, buf, sizeof(buf)));

	size_t cur_len = sizeof(buf);
	char expected[8192];
	memcpy(expected, buf, cur_len);

	/* 600 mid-piece inserts produce thousands of pieces — enough to force at
	 * least two levels of internal splits at FANOUT 16.
	 */
	for (int i = 0; i < 600; i++) {
		size_t at = (size_t)((i * 37u) % cur_len);
		char c = (char)('0' + (i % 10));
		ASSERT_TRUE(editorTextTreeReplaceRange(&tree, at, 0, &c, 1));
		memmove(expected + at + 1, expected + at, cur_len - at);
		expected[at] = c;
		cur_len++;
	}
	ASSERT_TRUE(tree_matches_string(&tree, expected, cur_len));
	ASSERT_TRUE(tree.root->is_leaf == 0);

	editorTextTreeFree(&tree);
	return 0;
}

/* Build a wide tree, then drain it from the middle one byte at a time. Both
 * the leaf-empty cleanup path and the merge-with-sibling path get exercised.
 * Tree must remain consistent and eventually collapse to a single empty leaf.
 */
static int test_text_tree_merge_collapses_after_drain(void) {
	struct editorTextTree tree;
	editorTextTreeInit(&tree);

	size_t initial = 4096;
	char *seed = (char *)malloc(initial);
	ASSERT_TRUE(seed != NULL);
	for (size_t i = 0; i < initial; i++) {
		seed[i] = (char)('a' + (int)(i % 26));
	}
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, seed, initial));
	free(seed);

	for (int i = 0; i < 600; i++) {
		size_t at = (size_t)((i * 53u) % editorTextTreeLength(&tree));
		char c = (char)('0' + (i % 10));
		ASSERT_TRUE(editorTextTreeReplaceRange(&tree, at, 0, &c, 1));
	}
	ASSERT_TRUE(editorTextTreeLength(&tree) == initial + 600);
	ASSERT_TRUE(tree.root->is_leaf == 0);

	size_t cur_len = editorTextTreeLength(&tree);
	while (cur_len > 0) {
		size_t at = cur_len > 1 ? cur_len / 2 : 0;
		ASSERT_TRUE(editorTextTreeReplaceRange(&tree, at, 1, NULL, 0));
		cur_len--;
	}

	ASSERT_TRUE(editorTextTreeLength(&tree) == 0);
	ASSERT_TRUE(tree.root->is_leaf == 1);
	ASSERT_TRUE(tree.root->count == 0);

	editorTextTreeFree(&tree);
	return 0;
}

/* Naive reference: byte position immediately after the line_idx-th '\n'. */
static int ref_line_start_byte(const char *text, size_t len, int line_idx, size_t *out) {
	if (line_idx == 0) {
		*out = 0;
		return 1;
	}
	int seen = 0;
	for (size_t i = 0; i < len; i++) {
		if (text[i] != '\n') {
			continue;
		}
		seen++;
		if (seen == line_idx) {
			*out = i + 1;
			return 1;
		}
	}
	return 0;
}

static int ref_line_for_byte(const char *text, size_t byte) {
	int line = 0;
	for (size_t i = 0; i < byte; i++) {
		if (text[i] == '\n') {
			line++;
		}
	}
	return line;
}

static int test_text_tree_locate_line_matches_naive(void) {
	const char *text = "alpha\nbeta\n\ngamma\n\ndelta";
	size_t len = strlen(text);
	struct editorTextTree tree;
	editorTextTreeInit(&tree);
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, text, len));

	int newlines = editorTextTreeSummary(&tree)->newlines;
	for (int i = 0; i <= newlines; i++) {
		size_t tree_start = 0;
		ASSERT_TRUE(editorTextTreeLocateLine(&tree, i, &tree_start));
		size_t ref_start = 0;
		ASSERT_TRUE(ref_line_start_byte(text, len, i, &ref_start));
		ASSERT_EQ_INT((int)ref_start, (int)tree_start);
	}

	/* Out of range. */
	size_t dummy = 0;
	ASSERT_TRUE(!editorTextTreeLocateLine(&tree, newlines + 1, &dummy));
	ASSERT_TRUE(!editorTextTreeLocateLine(&tree, -1, &dummy));

	editorTextTreeFree(&tree);
	return 0;
}

static int test_text_tree_line_for_byte_matches_naive(void) {
	const char *text = "alpha\nbeta\n\ngamma\n\ndelta";
	size_t len = strlen(text);
	struct editorTextTree tree;
	editorTextTreeInit(&tree);
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, text, len));

	for (size_t b = 0; b < len; b++) {
		int line = -1;
		ASSERT_TRUE(editorTextTreeLineForByte(&tree, b, &line));
		ASSERT_EQ_INT(ref_line_for_byte(text, b), line);
	}
	int dummy = 0;
	ASSERT_TRUE(!editorTextTreeLineForByte(&tree, len, &dummy));

	editorTextTreeFree(&tree);
	return 0;
}

/* Force a deep tree, then verify LocateLine/LineForByte are still correct. */
static int test_text_tree_line_queries_survive_splits(void) {
	struct editorTextTree tree;
	editorTextTreeInit(&tree);

	/* Build "line0\nline1\n...line99\n" — many newlines so descent matters. */
	char buf[1024];
	size_t off = 0;
	for (int i = 0; i < 100; i++) {
		size_t remaining = sizeof(buf) - off;
		int n = snprintf(buf + off, remaining, "line%d\n", i);
		ASSERT_TRUE(n > 0 && (size_t)n < remaining);
		off += (size_t)n;
	}
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, buf, off));

	/* Insert one byte at the start many times to force splits. */
	for (int i = 0; i < 200; i++) {
		char c = 'x';
		ASSERT_TRUE(editorTextTreeReplaceRange(&tree, 0, 0, &c, 1));
	}

	/* Snapshot the actual bytes and re-verify with naive ref. */
	size_t total = editorTextTreeLength(&tree);
	char *snap = (char *)malloc(total);
	ASSERT_TRUE(snap != NULL);
	ASSERT_TRUE(editorTextTreeCopyRange(&tree, 0, total, snap));

	int newlines = editorTextTreeSummary(&tree)->newlines;
	for (int i = 0; i <= newlines; i++) {
		size_t tree_start = 0;
		ASSERT_TRUE(editorTextTreeLocateLine(&tree, i, &tree_start));
		size_t ref_start = 0;
		ASSERT_TRUE(ref_line_start_byte(snap, total, i, &ref_start));
		ASSERT_EQ_INT((int)ref_start, (int)tree_start);
	}
	for (size_t b = 0; b < total; b += 7) {
		int line = -1;
		ASSERT_TRUE(editorTextTreeLineForByte(&tree, b, &line));
		ASSERT_EQ_INT(ref_line_for_byte(snap, b), line);
	}

	free(snap);
	editorTextTreeFree(&tree);
	return 0;
}

/* Append-style typing should collapse onto a single piece via the add-buf
 * fast path, not a piece per keystroke. */
static int test_text_tree_typing_fast_path_coalesces(void) {
	struct editorTextTree tree;
	editorTextTreeInit(&tree);

	for (int i = 0; i < 200; i++) {
		char c = (char)('a' + (i % 26));
		ASSERT_TRUE(
		        editorTextTreeReplaceRange(&tree, editorTextTreeLength(&tree), 0, &c, 1));
	}
	ASSERT_TRUE(editorTextTreeLength(&tree) == 200);

	struct editorTextTreeStats stats;
	editorTextTreeCollectStats(&tree, &stats);
	/* All 200 keystrokes append contiguously into add_buf, so they collapse
	 * into a single piece. */
	ASSERT_EQ_INT(1, stats.piece_count);
	ASSERT_EQ_INT(1, stats.leaf_count);

	editorTextTreeFree(&tree);
	return 0;
}

/* Bursty random typing keeps the piece count proportional to logical edit
 * runs, not edit calls. */
static int test_text_tree_random_edits_bounded_piece_count(void) {
	struct editorTextTree tree;
	editorTextTreeInit(&tree);
	const char *seed = "hello world\n";
	ASSERT_TRUE(editorTextTreeResetFromString(&tree, seed, strlen(seed)));

	for (int i = 0; i < 500; i++) {
		size_t at = (size_t)((i * 73u) % (editorTextTreeLength(&tree) + 1));
		char c = (char)('A' + (i % 26));
		ASSERT_TRUE(editorTextTreeReplaceRange(&tree, at, 0, &c, 1));
	}

	struct editorTextTreeStats stats;
	editorTextTreeCollectStats(&tree, &stats);
	/* Per-insert worst case is +2 pieces (mid-piece split into left + new +
	 * right), so 500 random-position inserts bound at ~1000. The 1500 limit
	 * leaves headroom for descent-tie behaviour where the random offset
	 * coincides with a piece boundary and the insert lands on the right side
	 * of the split; this regression-detects gross changes (e.g., a single
	 * insert spawning many pieces, or CollectStats double-counting) without
	 * being brittle to small coalescing/split heuristic tweaks.
	 */
	ASSERT_TRUE(stats.piece_count < 1500);
	ASSERT_TRUE(stats.max_depth <= 4);

	editorTextTreeFree(&tree);
	return 0;
}

const struct editorTestCase g_text_tree_tests[] = {
        {"text_tree_leaf_split_grows_tree", test_text_tree_leaf_split_grows_tree},
        {"text_tree_split_grows_internal_nodes", test_text_tree_split_grows_internal_nodes},
        {"text_tree_merge_collapses_after_drain", test_text_tree_merge_collapses_after_drain},
        {"text_tree_locate_line_matches_naive", test_text_tree_locate_line_matches_naive},
        {"text_tree_line_for_byte_matches_naive", test_text_tree_line_for_byte_matches_naive},
        {"text_tree_line_queries_survive_splits", test_text_tree_line_queries_survive_splits},
        {"text_tree_typing_fast_path_coalesces", test_text_tree_typing_fast_path_coalesces},
        {"text_tree_random_edits_bounded_piece_count",
         test_text_tree_random_edits_bounded_piece_count},
};

const int g_text_tree_test_count = (int)(sizeof(g_text_tree_tests) / sizeof(g_text_tree_tests[0]));
