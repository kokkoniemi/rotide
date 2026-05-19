#include "bench_runner.h"
#include "test_helpers.h"

#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/row_cache.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "render/viewport.h"
#include "render/wrap.h"
#include "rotide.h"
#include "text/document.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Stub for the global referenced by editor support TUs the bench links. */
struct editorConfig E;

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

struct wrapRecomputeState {
	struct editorDocument *doc;
	struct erow *rows;
	int numrows;
};

#define WRAP_BENCH_LINES 1000
#define WRAP_BENCH_LINE_BYTES 120  /* exceeds 80-col body, so each line wraps */
#define WRAP_BENCH_BODY_COLS 80

static char *generate_wrap_fixture(void) {
	size_t total = (size_t)WRAP_BENCH_LINES * ((size_t)WRAP_BENCH_LINE_BYTES + 1);
	char *buf = malloc(total);
	if (buf == NULL) {
		return NULL;
	}
	size_t pos = 0;
	for (int line = 0; line < WRAP_BENCH_LINES; line++) {
		for (int j = 0; j < WRAP_BENCH_LINE_BYTES; j++) {
			buf[pos++] = (char)('a' + ((line + j) % 26));
		}
		buf[pos++] = '\n';
	}
	return buf;
}

static int setup_wrap_recompute(void **state_out) {
	struct wrapRecomputeState *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return 0;
	}
	s->doc = calloc(1, sizeof(*s->doc));
	if (s->doc == NULL) {
		free(s);
		return 0;
	}
	editorDocumentInit(s->doc);
	char *fixture = generate_wrap_fixture();
	size_t fixture_len = (size_t)WRAP_BENCH_LINES * ((size_t)WRAP_BENCH_LINE_BYTES + 1);
	if (fixture == NULL ||
			!editorDocumentResetFromString(s->doc, fixture, fixture_len)) {
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

	/* The wrap helpers read E.rows/E.numrows globally. */
	E.rows = s->rows;
	E.numrows = s->numrows;

	*state_out = s;
	return 1;
}

static void op_wrap_recompute(void *state, int n) {
	struct wrapRecomputeState *s = state;
	for (int i = 0; i < n; i++) {
		/* Invalidate every row's wrap cache. The recompute is what we are
		 * timing, but the invalidate is a few-byte write so its overhead
		 * is negligible relative to the recompute pass below. */
		for (int r = 0; r < s->numrows; r++) {
			s->rows[r].wrap_cache_body_cols = 0;
			s->rows[r].wrap_cache_segment_count = 0;
		}
		for (int r = 0; r < s->numrows; r++) {
			(void)editorWrapSegmentCountForRowIndex(r, WRAP_BENCH_BODY_COLS);
		}
	}
}

static void teardown_wrap_recompute(void *state) {
	struct wrapRecomputeState *s = state;
	if (s == NULL) {
		return;
	}
	E.rows = NULL;
	E.numrows = 0;
	editorFreeRowArray(s->rows, s->numrows);
	editorDocumentFree(s->doc);
	free(s->doc);
	free(s);
}

struct syntaxIncrementalState {
	char *text;
	size_t text_len;
	struct editorSyntaxState *syntax;
};

#define SYNTAX_BENCH_LINES 5000

static char *generate_c_fixture(size_t *len_out) {
	/* Synthetic-but-parseable C: blocks of int variable declarations.
	 * Tree-sitter will parse this quickly, so the bench measures the
	 * incremental edit path rather than the absolute parse cost. */
	size_t cap = (size_t)SYNTAX_BENCH_LINES * 32;
	char *buf = malloc(cap);
	if (buf == NULL) {
		return NULL;
	}
	size_t pos = 0;
	for (int i = 0; i < SYNTAX_BENCH_LINES; i++) {
		int written = snprintf(buf + pos, cap - pos,
			"int variable_%05d = %d;\n", i, i);
		if (written < 0 || (size_t)written >= cap - pos) {
			free(buf);
			return NULL;
		}
		pos += (size_t)written;
	}
	*len_out = pos;
	return buf;
}

static void bytes_to_point(const char *text, size_t byte,
		struct editorSyntaxPoint *out) {
	uint32_t row = 0;
	uint32_t col = 0;
	for (size_t i = 0; i < byte; i++) {
		if (text[i] == '\n') {
			row++;
			col = 0;
		} else {
			col++;
		}
	}
	out->row = row;
	out->column = col;
}

static int setup_syntax_incremental(void **state_out) {
	struct syntaxIncrementalState *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return 0;
	}
	s->text = generate_c_fixture(&s->text_len);
	if (s->text == NULL) {
		free(s);
		return 0;
	}
	s->syntax = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	if (s->syntax == NULL) {
		free(s->text);
		free(s);
		return 0;
	}
	struct editorTextSource src = {0};
	editorTextSourceInitString(&src, s->text, s->text_len);
	if (!editorSyntaxStateParseFull(s->syntax, &src)) {
		editorSyntaxStateDestroy(s->syntax);
		free(s->text);
		free(s);
		return 0;
	}
	*state_out = s;
	return 1;
}

static void op_syntax_incremental(void *state, int n) {
	struct syntaxIncrementalState *s = state;
	/* Insert one byte near the middle and undo, alternating. The position
	 * stays inside a single line so tree-sitter's incremental path can
	 * reuse most of the tree. */
	size_t edit_byte = s->text_len / 2;
	while (edit_byte > 0 && s->text[edit_byte] == '\n') {
		edit_byte--;
	}

	for (int i = 0; i < n; i++) {
		/* Forward edit: insert 'x' at edit_byte. */
		struct editorSyntaxEdit edit;
		edit.start_byte = (uint32_t)edit_byte;
		edit.old_end_byte = (uint32_t)edit_byte;
		edit.new_end_byte = (uint32_t)(edit_byte + 1);
		bytes_to_point(s->text, edit_byte, &edit.start_point);
		edit.old_end_point = edit.start_point;

		/* Splice the byte into the buffer for tree-sitter to read. */
		memmove(s->text + edit_byte + 1, s->text + edit_byte,
			s->text_len - edit_byte);
		s->text[edit_byte] = 'x';
		s->text_len++;
		bytes_to_point(s->text, edit_byte + 1, &edit.new_end_point);

		struct editorTextSource src = {0};
		editorTextSourceInitString(&src, s->text, s->text_len);
		(void)editorSyntaxStateApplyEditAndParse(s->syntax, &edit, &src);

		/* Reverse edit: delete the byte. */
		struct editorSyntaxEdit revert;
		revert.start_byte = (uint32_t)edit_byte;
		revert.old_end_byte = (uint32_t)(edit_byte + 1);
		revert.new_end_byte = (uint32_t)edit_byte;
		bytes_to_point(s->text, edit_byte, &revert.start_point);
		bytes_to_point(s->text, edit_byte + 1, &revert.old_end_point);

		memmove(s->text + edit_byte, s->text + edit_byte + 1,
			s->text_len - edit_byte - 1);
		s->text_len--;
		revert.new_end_point = revert.start_point;

		editorTextSourceInitString(&src, s->text, s->text_len);
		(void)editorSyntaxStateApplyEditAndParse(s->syntax, &revert, &src);
	}
}

static void teardown_syntax_incremental(void *state) {
	struct syntaxIncrementalState *s = state;
	if (s == NULL) {
		return;
	}
	editorSyntaxStateDestroy(s->syntax);
	free(s->text);
	free(s);
}

/* `editorRefreshScreen` writes to STDOUT_FILENO. The bench cares about
 * the in-process work — frame assembly, cache comparison, writeBuf
 * construction — not the syscall latency, so we redirect stdout to
 * /dev/null for the duration of the case. */

struct screenDiffState {
	int saved_stdout;
	int devnull;
};

static int screen_diff_redirect_stdout(struct screenDiffState *s) {
	s->saved_stdout = dup(STDOUT_FILENO);
	if (s->saved_stdout == -1) {
		return 0;
	}
	s->devnull = open("/dev/null", O_WRONLY);
	if (s->devnull == -1) {
		close(s->saved_stdout);
		return 0;
	}
	if (dup2(s->devnull, STDOUT_FILENO) == -1) {
		close(s->devnull);
		close(s->saved_stdout);
		return 0;
	}
	return 1;
}

static void screen_diff_restore_stdout(struct screenDiffState *s) {
	if (s->saved_stdout != -1) {
		(void)dup2(s->saved_stdout, STDOUT_FILENO);
		(void)close(s->saved_stdout);
		s->saved_stdout = -1;
	}
	if (s->devnull != -1) {
		(void)close(s->devnull);
		s->devnull = -1;
	}
}

/* Build a small editor state (24 × 80 view over a ~100-line document)
 * and prime the frame cache with one initial render. Both screen-diff
 * cases share the same setup so the cache state at the start of the
 * timed loop is identical. */
static int screen_diff_setup_common(struct screenDiffState **state_out) {
	struct screenDiffState *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return 0;
	}
	s->saved_stdout = -1;
	s->devnull = -1;

	reset_editor_state();
	E.window_rows = 24;
	E.window_cols = 80;

	for (int i = 0; i < 100; i++) {
		char line[80];
		int len = snprintf(line, sizeof(line),
				"line %3d: lorem ipsum dolor sit amet, consectetur adipiscing", i);
		add_row_bytes(line, (size_t)len);
	}
	E.cy = 0;
	E.cx = 0;
	(void)editorBufferPosToOffset(0, 0, &E.cursor_offset);

	if (!screen_diff_redirect_stdout(s)) {
		free(s);
		return 0;
	}

	editorViewportUpdateForFrame();
	editorRefreshScreen();
	*state_out = s;
	return 1;
}

static int setup_screen_diff_unchanged(void **state_out) {
	struct screenDiffState *s = NULL;
	if (!screen_diff_setup_common(&s)) {
		return 0;
	}
	*state_out = s;
	return 1;
}

static void op_screen_diff_unchanged(void *state, int n) {
	(void)state;
	for (int i = 0; i < n; i++) {
		editorViewportUpdateForFrame();
		editorRefreshScreen();
	}
}

static void teardown_screen_diff_unchanged(void *state) {
	struct screenDiffState *s = state;
	if (s == NULL) {
		return;
	}
	screen_diff_restore_stdout(s);
	free(s);
}

static int setup_screen_diff_one_row(void **state_out) {
	return setup_screen_diff_unchanged(state_out);
}

static void op_screen_diff_one_row(void *state, int n) {
	(void)state;
	/* Each iteration: insert one char on row 5, refresh, then undo,
	 * refresh again. Two `editorRefreshScreen` calls per inner op,
	 * one of which has exactly one dirty row. The frame cache should
	 * touch only that row on the redraw. */
	for (int i = 0; i < n; i++) {
		E.cy = 5;
		E.cx = 0;
		(void)editorBufferPosToOffset(E.cy, E.cx, &E.cursor_offset);
		editorInsertChar('x');
		editorViewportUpdateForFrame();
		editorRefreshScreen();

		editorUndo();
		editorViewportUpdateForFrame();
		editorRefreshScreen();
	}
}

static void teardown_screen_diff_one_row(void *state) {
	teardown_screen_diff_unchanged(state);
}

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
	{
		.name = "wrap_recompute_1k_lines",
		.setup = setup_wrap_recompute,
		.op = op_wrap_recompute,
		.teardown = teardown_wrap_recompute,
		.inner_ops = 1,
	},
	{
		.name = "syntax_incremental_5k_lines_c",
		.setup = setup_syntax_incremental,
		.op = op_syntax_incremental,
		.teardown = teardown_syntax_incremental,
		.inner_ops = 4,
	},
	{
		/* All rows match the frame cache after the priming refresh, so
		 * the timed loop measures cache-hit traversal cost. */
		.name = "screen_diff_unchanged_frame",
		.setup = setup_screen_diff_unchanged,
		.op = op_screen_diff_unchanged,
		.teardown = teardown_screen_diff_unchanged,
		.inner_ops = 8,
	},
	{
		/* inner_ops = 4 means 4 forward edits + 4 undos = 8 refreshes
		 * per sample, with exactly one row dirty per refresh. */
		.name = "screen_diff_one_row_changed",
		.setup = setup_screen_diff_one_row,
		.op = op_screen_diff_one_row,
		.teardown = teardown_screen_diff_one_row,
		.inner_ops = 4,
	},
};

static const int k_case_count = (int)(sizeof(k_cases) / sizeof(k_cases[0]));

static void print_usage(const char *argv0) {
	printf("usage: %s [--iterations N] [--filter SUBSTR] [--json PATH] [--metrics-out PATH]\n",
		argv0);
	printf("  --iterations N      timing samples per case (default 20)\n");
	printf("  --filter SUBSTR     only run cases whose name contains SUBSTR\n");
	printf("  --json PATH         write percentile data as JSON to PATH\n");
	printf("  --metrics-out PATH  append one JSONL row per bench to PATH\n");
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
		if (strcmp(argv[i], "--metrics-out") == 0 && i + 1 < argc) {
			opts.metrics_path = argv[++i];
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
