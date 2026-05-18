#include "bench_runner.h"

#include "editing/row_cache.h"
#include "rotide.h"
#include "text/document.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stub for the global referenced by editor support TUs the bench links. */
struct editorConfig E;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#define BENCH_DOC_BYTES (256u * 1024u)
#define BENCH_DOC_AVG_LINE_BYTES 64u

static uint64_t bench_rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t bench_rng_next(void) {
	uint64_t x = bench_rng_state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	bench_rng_state = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static char *generate_fixture(size_t bytes) {
	char *buf = (char *)malloc(bytes);
	if (buf == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < bytes; i++) {
		unsigned r = (unsigned)(bench_rng_next() % 100);
		if (r < 100u / BENCH_DOC_AVG_LINE_BYTES) {
			buf[i] = '\n';
		} else if (r < 55) {
			buf[i] = (char)('a' + (bench_rng_next() % 26));
		} else {
			buf[i] = (char)('A' + (bench_rng_next() % 26));
		}
	}
	return buf;
}

/* ------------------------------------------------------------------ */
/* Bench: document position ↔ byte roundtrip                          */
/* ------------------------------------------------------------------ */

struct positionRoundtripState {
	struct editorDocument *doc;
	size_t *probe_offsets;
	int probe_count;
};

static int setup_position_roundtrip(void **state_out) {
	struct positionRoundtripState *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return 0;
	}
	s->doc = calloc(1, sizeof(*s->doc));
	if (s->doc == NULL) {
		free(s);
		return 0;
	}
	editorDocumentInit(s->doc);
	char *fixture = generate_fixture(BENCH_DOC_BYTES);
	if (fixture == NULL || !editorDocumentResetFromString(s->doc, fixture, BENCH_DOC_BYTES)) {
		free(fixture);
		editorDocumentFree(s->doc);
		free(s->doc);
		free(s);
		return 0;
	}
	free(fixture);

	/* Pre-seeded RNG so the probe offsets are deterministic across samples. */
	uint64_t local = 0xDEADBEEFCAFEBABEULL;
	s->probe_count = 1024;
	s->probe_offsets = malloc(sizeof(size_t) * (size_t)s->probe_count);
	if (s->probe_offsets == NULL) {
		editorDocumentFree(s->doc);
		free(s->doc);
		free(s);
		return 0;
	}
	for (int i = 0; i < s->probe_count; i++) {
		local ^= local >> 12;
		local ^= local << 25;
		local ^= local >> 27;
		s->probe_offsets[i] = (size_t)(local % (uint64_t)(BENCH_DOC_BYTES));
	}

	*state_out = s;
	return 1;
}

static void op_position_roundtrip(void *state, int n) {
	struct positionRoundtripState *s = state;
	int probe_count = s->probe_count;
	for (int i = 0; i < n; i++) {
		size_t offset = s->probe_offsets[i % probe_count];
		int line = 0;
		size_t col = 0;
		if (!editorDocumentByteOffsetToPosition(s->doc, offset, &line, &col)) {
			continue;
		}
		size_t back = 0;
		(void)editorDocumentPositionToByteOffset(s->doc, line, col, &back);
		/* Force the compiler to materialise the result. */
		__asm__ volatile("" : : "r"(back) : "memory");
	}
}

static void teardown_position_roundtrip(void *state) {
	struct positionRoundtripState *s = state;
	if (s == NULL) {
		return;
	}
	free(s->probe_offsets);
	editorDocumentFree(s->doc);
	free(s->doc);
	free(s);
}

/* ------------------------------------------------------------------ */
/* Bench: row_cache splice for a small edit                           */
/* ------------------------------------------------------------------ */

struct rowCacheSpliceState {
	struct editorDocument *doc;
	struct erow *rows;
	int numrows;
	size_t edit_offset;
};

static int setup_row_cache_splice(void **state_out) {
	struct rowCacheSpliceState *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return 0;
	}
	s->doc = calloc(1, sizeof(*s->doc));
	if (s->doc == NULL) {
		free(s);
		return 0;
	}
	editorDocumentInit(s->doc);
	char *fixture = generate_fixture(BENCH_DOC_BYTES);
	if (fixture == NULL || !editorDocumentResetFromString(s->doc, fixture, BENCH_DOC_BYTES)) {
		free(fixture);
		editorDocumentFree(s->doc);
		free(s->doc);
		free(s);
		return 0;
	}
	free(fixture);

	if (!editorBuildFullRowsFromDocument(s->doc, &s->rows, &s->numrows)) {
		editorDocumentFree(s->doc);
		free(s->doc);
		free(s);
		return 0;
	}

	/* Edit at the middle of the document so the splice covers a single row
	 * surrounded by many untouched ones. */
	s->edit_offset = BENCH_DOC_BYTES / 2;

	*state_out = s;
	return 1;
}

static void op_row_cache_splice(void *state, int n) {
	struct rowCacheSpliceState *s = state;
	for (int i = 0; i < n; i++) {
		struct editorRowCacheSpliceRegion region;
		if (!editorPrepareRowCacheSpliceRegion(s->doc, s->edit_offset, 0, &region)) {
			continue;
		}
		const char ch = 'x';
		if (!editorDocumentReplaceRange(s->doc, s->edit_offset, 0, &ch, 1)) {
			continue;
		}
		int end_row = 0;
		if (!editorRowCacheSpliceEndRowForDocument(s->doc, &region, &end_row)) {
			(void)editorDocumentReplaceRange(s->doc, s->edit_offset, 1, NULL, 0);
			continue;
		}
		struct erow *replacement_rows = NULL;
		int replacement_numrows = 0;
		if (!editorBuildRowsFromDocumentRange(s->doc, region.start_row, end_row,
					&replacement_rows, &replacement_numrows)) {
			(void)editorDocumentReplaceRange(s->doc, s->edit_offset, 1, NULL, 0);
			continue;
		}
		(void)editorSpliceRowCache(&s->rows, &s->numrows, replacement_rows,
				replacement_numrows, region.start_row, region.old_end_row_exclusive);

		/* Revert to keep state stable across the inner loop so the same
		 * splice work happens each iteration. The revert is *not* timed
		 * out — it counts as part of the sample. The contribution is
		 * symmetric (insert-then-delete) so the per-op number still
		 * reflects splice cost. */
		struct editorRowCacheSpliceRegion revert_region;
		if (!editorPrepareRowCacheSpliceRegion(s->doc, s->edit_offset, 1, &revert_region)) {
			continue;
		}
		(void)editorDocumentReplaceRange(s->doc, s->edit_offset, 1, NULL, 0);
		int revert_end = 0;
		if (!editorRowCacheSpliceEndRowForDocument(s->doc, &revert_region, &revert_end)) {
			continue;
		}
		struct erow *revert_rows = NULL;
		int revert_numrows = 0;
		if (!editorBuildRowsFromDocumentRange(s->doc, revert_region.start_row, revert_end,
					&revert_rows, &revert_numrows)) {
			continue;
		}
		(void)editorSpliceRowCache(&s->rows, &s->numrows, revert_rows, revert_numrows,
				revert_region.start_row, revert_region.old_end_row_exclusive);
	}
}

static void teardown_row_cache_splice(void *state) {
	struct rowCacheSpliceState *s = state;
	if (s == NULL) {
		return;
	}
	editorFreeRowArray(s->rows, s->numrows);
	editorDocumentFree(s->doc);
	free(s->doc);
	free(s);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static const struct editorBenchCase k_cases[] = {
	{
		.name = "document_position_byte_roundtrip",
		.setup = setup_position_roundtrip,
		.op = op_position_roundtrip,
		.teardown = teardown_position_roundtrip,
		.inner_ops = 1024,
	},
	{
		.name = "row_cache_splice_small_edit",
		.setup = setup_row_cache_splice,
		.op = op_row_cache_splice,
		.teardown = teardown_row_cache_splice,
		.inner_ops = 16,
	},
};

static const int k_case_count = (int)(sizeof(k_cases) / sizeof(k_cases[0]));

static void print_usage(const char *argv0) {
	printf("usage: %s [--iterations N] [--filter SUBSTR] [--json PATH]\n", argv0);
	printf("  --iterations N    timing samples per case (default 20)\n");
	printf("  --filter SUBSTR   only run cases whose name contains SUBSTR\n");
	printf("  --json PATH       write percentile data as JSON to PATH\n");
}

int main(int argc, char **argv) {
	struct editorBenchOptions opts = {0};
	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)) {
			print_usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
			opts.iterations = atoi(argv[++i]);
			continue;
		}
		if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
			opts.filter = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
			opts.json_path = argv[++i];
			continue;
		}
		fprintf(stderr, "unknown argument: %s\n", argv[i]);
		print_usage(argv[0]);
		return 2;
	}

	printf("rotide_bench: cases=%d iterations=%d\n", k_case_count,
		opts.iterations > 0 ? opts.iterations : 20);
	return editorBenchRun(k_cases, k_case_count, &opts);
}
