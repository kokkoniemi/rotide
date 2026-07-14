#include "render/wrap.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "text/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent single-segment oracle: a straightforward O(rsize) scan from the
 * segment start that returns the next wrap boundary. */
static int wrap_breaks_after_codepoint(unsigned int cp) {
	switch (cp) {
		case ' ':
		case '\t':
		case '.':
		case ',':
		case ';':
		case ':':
		case '/':
		case '\\':
		case '-':
		case '_':
		case '(':
		case '[':
		case '{':
		case ')':
		case ']':
		case '}':
		case '=':
		case '+':
		case '&':
		case '|':
			return 1;
		default:
			return 0;
	}
}

static int wrap_next_start_col_oracle(const struct editorRow *row, int start_col,
                                      int available_cols, int total_cols) {
	if (row == NULL || available_cols <= 0 || start_col < 0 || start_col >= total_cols) {
		return total_cols;
	}
	if (start_col + available_cols >= total_cols) {
		return total_cols;
	}

	int target_col = start_col + available_cols;
	int best_break_col = -1;
	int best_fit_col = -1;
	int first_advance_col = -1;
	int rx = 0;
	for (int i = 0; i < row->rsize;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], row->rsize - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > row->rsize - i) {
			src_len = row->rsize - i;
		}
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);
		int next_rx = rx + width;
		if (next_rx > start_col && first_advance_col == -1) {
			first_advance_col = next_rx;
		}
		if (next_rx > start_col && next_rx <= target_col) {
			best_fit_col = next_rx;
			if (wrap_breaks_after_codepoint(cp)) {
				best_break_col = next_rx;
			}
		}
		if (next_rx > target_col) {
			break;
		}
		rx = next_rx;
		i += src_len;
	}

	if (best_break_col > start_col) {
		return best_break_col;
	}
	if (best_fit_col > start_col) {
		return best_fit_col;
	}
	if (first_advance_col > start_col) {
		return first_advance_col;
	}
	return total_cols;
}

/* Builds the full reference segmentation by repeatedly asking the oracle for the
 * next break from each segment start -- the algorithm the cache builder replaced. */
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
		int next_start =
		        wrap_next_start_col_oracle(row, start_col, available_cols, total_cols);
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
		(void)fprintf(stderr, "wrap mismatch row=%d body_cols=%d: count actual=%d ref=%d\n",
		              row_idx, body_cols, actual_count, ref_count);
		return 1;
	}
	int total_cols = row->render_display_cols;
	for (int seg = 0; seg < ref_count; seg++) {
		int start_col = 0;
		editorWrapSegmentInfo(row, seg, body_cols, &start_col, NULL, NULL);
		if (start_col != reference[seg]) {
			(void)fprintf(stderr,
			              "wrap mismatch row=%d body_cols=%d seg=%d: start actual=%d "
			              "ref=%d\n",
			              row_idx, body_cols, seg, start_col, reference[seg]);
			return 1;
		}
		/* The render path uses the cache-backed lookup to size each drawn
		 * segment; it must return the next boundary (or total_cols at the end). */
		int cached_next = editorWrapNextStartColCached(row, start_col, body_cols);
		int expected_next = seg + 1 < ref_count ? reference[seg + 1] : total_cols;
		if (cached_next != expected_next) {
			(void)fprintf(stderr,
			              "cached-next mismatch row=%d body_cols=%d seg=%d: actual=%d "
			              "expected=%d\n",
			              row_idx, body_cols, seg, cached_next, expected_next);
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
	static const char *tokens[] = {"a",    "b", "z", " ", "  ",
	                               "\t",   ".", ",", "-", "_",
	                               "(",    ")", "=", "é", "\xe4\xb8\xad" /* CJK */,
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
		for (size_t k = 0; k < tlen; k++) {
			buf[pos++] = tok[k];
		}
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
		(void)fprintf(stderr, "long line segment count actual=%d expected=%d\n", count,
		              expected);
		failed = 1;
	}
	if (!failed) {
		failed = wrap_check_row_against_reference(0, body_cols);
	}
	clear_editor_state();
	return failed;
}

const struct editorTestCase g_wrap_tests[] = {
        {"wrap_cache_matches_reference_varied_rows", test_wrap_cache_matches_reference_varied_rows},
        {"wrap_cache_matches_reference_pseudorandom",
         test_wrap_cache_matches_reference_pseudorandom},
        {"wrap_cache_long_line_is_linear", test_wrap_cache_long_line_is_linear},
};

const int g_wrap_test_count = (int)(sizeof(g_wrap_tests) / sizeof(g_wrap_tests[0]));
