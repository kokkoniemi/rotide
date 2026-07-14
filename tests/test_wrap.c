#include "rotide.h"
#include "render/wrap.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep editorWrapNextStartCol's established behavior as the oracle for the
 * single-pass cache builder. */
static int wrap_reference_segments(struct editorRow *row, int body_cols, int *out, int max_out) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	out[0] = 0;
	int total_cols = row->render_display_cols;
	if (total_cols <= 0 || total_cols <= body_cols) {
		return 1;
	}
	int indent_cols = editorWrapContinuationIndentCols(row, body_cols);
	int start_col = 0;
	int count = 1;
	while (start_col < total_cols && count < max_out) {
		int current_indent = count == 1 ? 0 : indent_cols;
		int available_cols = body_cols - current_indent;
		if (available_cols < 1) {
			available_cols = 1;
		}
		int next_start = editorWrapNextStartCol(row, start_col, available_cols, total_cols);
		if (next_start >= total_cols || next_start <= start_col) {
			break;
		}
		out[count++] = next_start;
		start_col = next_start;
	}
	return count;
}

#define WRAP_MAX_SEGMENTS 65536

static int wrap_check_row_against_reference(int row_idx, int body_cols) {
	static int reference[WRAP_MAX_SEGMENTS];
	struct editorRow *row = &E.rows[row_idx];
	int ref_count = wrap_reference_segments(row, body_cols, reference, WRAP_MAX_SEGMENTS);

	row->wrap_cache_body_cols = 0;
	row->wrap_cache_segment_count = 0;
	int actual_count = editorWrapSegmentCountForRowIndex(row_idx, body_cols);

	if (actual_count != ref_count) {
		fprintf(stderr, "wrap mismatch row=%d body_cols=%d: count actual=%d ref=%d\n",
		        row_idx, body_cols, actual_count, ref_count);
		return 1;
	}
	for (int seg = 0; seg < ref_count; seg++) {
		int start_col = 0;
		editorWrapSegmentInfo(row, seg, body_cols, &start_col, NULL, NULL);
		if (start_col != reference[seg]) {
			fprintf(stderr,
			        "wrap mismatch row=%d body_cols=%d seg=%d: start actual=%d ref=%d\n",
			        row_idx, body_cols, seg, start_col, reference[seg]);
			return 1;
		}
	}
	return 0;
}

static int wrap_check_all_rows(void) {
	static const int body_cols_cases[] = {1, 2, 3, 5, 8, 20, 40, 78, 80, 200};
	for (int r = 0; r < E.numrows; r++) {
		for (size_t b = 0; b < sizeof(body_cols_cases) / sizeof(body_cols_cases[0]); b++) {
			if (wrap_check_row_against_reference(r, body_cols_cases[b])) {
				return 1;
			}
		}
	}
	return 0;
}

/* Deterministic pseudo-random content covering break chars, spaces, tabs, and
 * multibyte / wide glyphs so the fit/break/force-advance branches all fire. */
static void wrap_fill_pseudorandom(char *buf, size_t len, unsigned int seed) {
	static const char *tokens[] = {"a",  "b",  "z",  " ", "  ", "\t", ".", ",",
	                               "-",  "_",  "(",  ")", "=",  "é", "\xe4\xb8\xad" /* CJK */,
	                               "long", "x"};
	size_t pos = 0;
	unsigned int state = seed * 2654435761u + 1u;
	while (pos + 8 < len) {
		state = state * 1103515245u + 12345u;
		const char *tok = tokens[(state >> 16) % (sizeof(tokens) / sizeof(tokens[0]))];
		size_t tlen = strlen(tok);
		if (pos + tlen >= len) {
			break;
		}
		memcpy(buf + pos, tok, tlen);
		pos += tlen;
	}
	while (pos < len) {
		buf[pos++] = 'a';
	}
	buf[len - 1] = 'a'; /* avoid a trailing partial multibyte sequence */
}

static int test_wrap_cache_matches_reference_varied_rows(void) {
	reset_editor_state();
	add_row("");
	add_row("short");
	add_row("word word word word word word word word word word word word word");
	add_row("nospacessssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss");
	add_row("\ttab\tindented\tcontinuation\tlines\twith\ttabs\tand\tmore\ttabs\there");
	add_row("    leading spaces then a long run of words to exercise indent cols here too");
	add_row("mix-of.break;chars/that=trigger+word&boundaries|inside(a)long[line]{here}now");
	add_row("wide中文字符mixed with ascii 中文 and more中文字符padding to force wrapping now");

	int failed = wrap_check_all_rows();
	clear_editor_state();
	return failed;
}

static int test_wrap_cache_matches_reference_pseudorandom(void) {
	reset_editor_state();
	static char buf[4096];
	for (unsigned int seed = 0; seed < 40; seed++) {
		size_t len = 8 + (seed * 101) % (sizeof(buf) - 16);
		wrap_fill_pseudorandom(buf, len, seed);
		add_row_bytes(buf, len);
	}
	int failed = wrap_check_all_rows();
	clear_editor_state();
	return failed;
}

/* A large break-free line guards the cache's linear-time constraint and makes
 * the expected segment count exact. */
static int test_wrap_cache_long_line_is_linear(void) {
	reset_editor_state();
	const int line_len = 400000;
	char *buf = malloc((size_t)line_len);
	if (buf == NULL) {
		clear_editor_state();
		return 1;
	}
	for (int i = 0; i < line_len; i++) {
		buf[i] = (char)('a' + (i % 26)); /* no break chars -> pure greedy fit */
	}
	add_row_bytes(buf, (size_t)line_len);
	free(buf);

	int failed = 0;
	const int body_cols = 80;
	int count = editorWrapSegmentCountForRowIndex(0, body_cols);
	int expected = (line_len + body_cols - 1) / body_cols;
	if (count != expected) {
		fprintf(stderr, "long line segment count actual=%d expected=%d\n", count, expected);
		failed = 1;
	}
	if (!failed) {
		failed = wrap_check_row_against_reference(0, body_cols);
	}
	clear_editor_state();
	return failed;
}

const struct editorTestCase g_wrap_tests[] = {
        {"wrap_cache_matches_reference_varied_rows",
         test_wrap_cache_matches_reference_varied_rows},
        {"wrap_cache_matches_reference_pseudorandom",
         test_wrap_cache_matches_reference_pseudorandom},
        {"wrap_cache_long_line_is_linear", test_wrap_cache_long_line_is_linear},
};

const int g_wrap_test_count = (int)(sizeof(g_wrap_tests) / sizeof(g_wrap_tests[0]));
