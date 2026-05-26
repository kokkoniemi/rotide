#include "seed.h"
#include "test_case.h"
#include "text/text_summary.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t g_rng;

static uint64_t rngNext(void) {
	uint64_t x = g_rng;
	if (x == 0) {
		x = 0x9E3779B97F4A7C15ULL;
	}
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	g_rng = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static void fillRandomText(char *out, size_t n, unsigned newline_pct) {
	for (size_t i = 0; i < n; i++) {
		unsigned r = (unsigned)(rngNext() % 100);
		if (r < newline_pct) {
			out[i] = '\n';
		} else {
			out[i] = (char)('a' + (rngNext() % 26));
		}
	}
}

static int summariesEqual(const struct editorTextSummary *a, const struct editorTextSummary *b) {
	return a->bytes == b->bytes && a->newlines == b->newlines &&
	       a->first_line_bytes == b->first_line_bytes &&
	       a->last_line_bytes == b->last_line_bytes && a->max_line_bytes == b->max_line_bytes;
}

static void printSummary(const char *label, const struct editorTextSummary *s) {
	(void)fprintf(stderr, "  %s: bytes=%zu newlines=%d first=%zu last=%zu max=%zu\n", label,
	              s->bytes, s->newlines, s->first_line_bytes, s->last_line_bytes,
	              s->max_line_bytes);
}

static int expectEqualSummaries(const char *case_name, const struct editorTextSummary *got,
                                const struct editorTextSummary *want) {
	if (summariesEqual(got, want)) {
		return 0;
	}
	(void)fprintf(stderr, "test_text_summary: %s mismatch\n", case_name);
	printSummary("got ", got);
	printSummary("want", want);
	return -1;
}

static int test_text_summary_zero_is_identity(void) {
	const char *samples[] = {
	        "",
	        "abc",
	        "\n",
	        "\n\n",
	        "abc\n",
	        "\nabc",
	        "abc\ndef\nghi",
	        "abc\ndef",
	        "abc\n\nfoo",
	        "line one\nline two with longer content\nshort",
	};
	const int n = (int)(sizeof(samples) / sizeof(samples[0]));
	struct editorTextSummary zero;
	editorTextSummaryZero(&zero);

	for (int i = 0; i < n; i++) {
		struct editorTextSummary s;
		editorTextSummaryFromBytes(samples[i], strlen(samples[i]), &s);

		struct editorTextSummary left_id;
		editorTextSummaryMerge(&zero, &s, &left_id);
		if (expectEqualSummaries("merge(zero, s) == s", &left_id, &s) != 0) {
			return 1;
		}
		struct editorTextSummary right_id;
		editorTextSummaryMerge(&s, &zero, &right_id);
		if (expectEqualSummaries("merge(s, zero) == s", &right_id, &s) != 0) {
			return 1;
		}
	}
	return 0;
}

static void naiveSummary(const char *bytes, size_t len, struct editorTextSummary *out) {
	editorTextSummaryZero(out);
	out->bytes = len;
	size_t run = 0;
	int seen_newline = 0;
	for (size_t i = 0; i < len; i++) {
		if (bytes[i] == '\n') {
			if (!seen_newline) {
				out->first_line_bytes = run;
				seen_newline = 1;
			}
			if (run > out->max_line_bytes) {
				out->max_line_bytes = run;
			}
			out->newlines++;
			run = 0;
		} else {
			run++;
		}
	}
	if (!seen_newline) {
		out->first_line_bytes = run;
	}
	out->last_line_bytes = run;
	if (run > out->max_line_bytes) {
		out->max_line_bytes = run;
	}
}

static int test_text_summary_from_bytes_matches_naive(void) {
	g_rng = rotide_test_seed() ^ 0xA5A5A5A5A5A5A5A5ULL;
	for (int iter = 0; iter < 64; iter++) {
		size_t len = (size_t)(rngNext() % 512);
		char buf[512];
		fillRandomText(buf, len, 15);
		struct editorTextSummary got;
		struct editorTextSummary want;
		editorTextSummaryFromBytes(buf, len, &got);
		naiveSummary(buf, len, &want);
		if (expectEqualSummaries("FromBytes vs naive", &got, &want) != 0) {
			(void)fprintf(stderr, "  iter=%d len=%zu seed=0x%016llx\n", iter, len,
			              (unsigned long long)rotide_test_seed());
			return 1;
		}
	}
	return 0;
}

static int test_text_summary_merge_associativity(void) {
	g_rng = rotide_test_seed() ^ 0xBEEFC0DEDEADBEEFULL;
	for (int iter = 0; iter < 64; iter++) {
		size_t la = (size_t)(rngNext() % 128);
		size_t lb = (size_t)(rngNext() % 128);
		size_t lc = (size_t)(rngNext() % 128);
		char a[128];
		char b[128];
		char c[128];
		fillRandomText(a, la, 20);
		fillRandomText(b, lb, 20);
		fillRandomText(c, lc, 20);

		struct editorTextSummary sa;
		struct editorTextSummary sb;
		struct editorTextSummary sc;
		editorTextSummaryFromBytes(a, la, &sa);
		editorTextSummaryFromBytes(b, lb, &sb);
		editorTextSummaryFromBytes(c, lc, &sc);

		struct editorTextSummary left_first;
		struct editorTextSummary right_first;
		struct editorTextSummary sab;
		struct editorTextSummary sbc;
		editorTextSummaryMerge(&sa, &sb, &sab);
		editorTextSummaryMerge(&sab, &sc, &left_first);
		editorTextSummaryMerge(&sb, &sc, &sbc);
		editorTextSummaryMerge(&sa, &sbc, &right_first);

		if (expectEqualSummaries("merge associativity", &left_first, &right_first) != 0) {
			(void)fprintf(stderr, "  iter=%d la=%zu lb=%zu lc=%zu seed=0x%016llx\n",
			              iter, la, lb, lc, (unsigned long long)rotide_test_seed());
			return 1;
		}
	}
	return 0;
}

static int test_text_summary_merge_matches_concat(void) {
	g_rng = rotide_test_seed() ^ 0x0123456789ABCDEFULL;
	char combined[1024];
	for (int iter = 0; iter < 64; iter++) {
		size_t total = (size_t)(rngNext() % 768) + 1;
		fillRandomText(combined, total, 12);

		struct editorTextSummary whole;
		editorTextSummaryFromBytes(combined, total, &whole);

		int splits = (int)(rngNext() % 8) + 1;
		struct editorTextSummary acc;
		editorTextSummaryZero(&acc);
		size_t cursor = 0;
		for (int s = 0; s < splits; s++) {
			size_t remaining = total - cursor;
			size_t take;
			if (s == splits - 1) {
				take = remaining;
			} else {
				take = remaining == 0 ? 0 : (size_t)(rngNext() % (remaining + 1));
			}
			struct editorTextSummary piece;
			editorTextSummaryFromBytes(combined + cursor, take, &piece);
			struct editorTextSummary merged;
			editorTextSummaryMerge(&acc, &piece, &merged);
			acc = merged;
			cursor += take;
		}

		if (expectEqualSummaries("merge of pieces == whole", &acc, &whole) != 0) {
			(void)fprintf(stderr, "  iter=%d total=%zu splits=%d seed=0x%016llx\n",
			              iter, total, splits, (unsigned long long)rotide_test_seed());
			return 1;
		}
	}
	return 0;
}

static int test_text_summary_doc_level_longest_line(void) {
	const struct {
		const char *text;
		size_t want_longest;
	} cases[] = {
	        {"", 0},
	        {"abc", 3},
	        {"\n", 0},
	        {"abc\n", 3},
	        {"\nabc", 3},
	        {"abc\ndef", 3},
	        {"abc\ndefghi", 6},
	        {"short\nverylonglineinside\nx", 18},
	        {"a\nbb\nccc\ndddd\n", 4},
	};
	const int n = (int)(sizeof(cases) / sizeof(cases[0]));
	for (int i = 0; i < n; i++) {
		struct editorTextSummary s;
		editorTextSummaryFromBytes(cases[i].text, strlen(cases[i].text), &s);
		size_t doc_longest = s.max_line_bytes;
		if (s.first_line_bytes > doc_longest) {
			doc_longest = s.first_line_bytes;
		}
		if (s.last_line_bytes > doc_longest) {
			doc_longest = s.last_line_bytes;
		}
		if (doc_longest != cases[i].want_longest) {
			(void)fprintf(
			        stderr,
			        "test_text_summary: doc-level longest mismatch for %s: got=%zu "
			        "want=%zu\n",
			        cases[i].text, doc_longest, cases[i].want_longest);
			return 1;
		}
	}
	return 0;
}

const struct editorTestCase g_text_summary_tests[] = {
        {"text_summary_zero_is_identity", test_text_summary_zero_is_identity},
        {"text_summary_from_bytes_matches_naive", test_text_summary_from_bytes_matches_naive},
        {"text_summary_merge_associativity", test_text_summary_merge_associativity},
        {"text_summary_merge_matches_concat", test_text_summary_merge_matches_concat},
        {"text_summary_doc_level_longest_line", test_text_summary_doc_level_longest_line},
};

const int g_text_summary_test_count =
        (int)(sizeof(g_text_summary_tests) / sizeof(g_text_summary_tests[0]));
