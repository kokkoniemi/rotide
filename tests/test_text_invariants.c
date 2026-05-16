#include "editing/document_bridge.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "editor_test_api.h"
#include "rotide.h"
#include "seed.h"
#include "test_case.h"
#include "test_helpers.h"
#include "text/document.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Differential property tests: editorDocument vs char*-and-memmove ref. */

#define INVARIANTS_MAX_DOC_LEN 16384
#define INVARIANTS_MAX_INSERT_LEN 64
#define INVARIANTS_POSITION_SAMPLES 16

struct refDoc {
	char *buf;
	size_t len;
	size_t cap;
};

struct opLog {
	int op_idx;
	unsigned kind;
	size_t start;
	size_t old_len;
	size_t new_len;
};

static int refDocInit(struct refDoc *r) {
	r->cap = 4096;
	r->buf = (char *)malloc(r->cap);
	if (r->buf == NULL) {
		return -1;
	}
	r->len = 0;
	return 0;
}

static void refDocFree(struct refDoc *r) {
	free(r->buf);
	r->buf = NULL;
	r->len = 0;
	r->cap = 0;
}

static int refDocReplace(struct refDoc *r, size_t start, size_t old_len,
		const char *text, size_t new_len) {
	if (start > r->len || start + old_len > r->len) {
		return -1;
	}
	size_t after = r->len - old_len + new_len;
	if (after + 1 > r->cap) {
		size_t new_cap = r->cap;
		while (new_cap < after + 1) {
			new_cap *= 2;
		}
		char *grown = (char *)realloc(r->buf, new_cap);
		if (grown == NULL) {
			return -1;
		}
		r->buf = grown;
		r->cap = new_cap;
	}
	char *tail = r->buf + start + old_len;
	size_t tail_len = r->len - (start + old_len);
	memmove(r->buf + start + new_len, tail, tail_len);
	memcpy(r->buf + start, text, new_len);
	r->len = after;
	return 0;
}

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

static void rngFillText(char *out, size_t n) {
	for (size_t i = 0; i < n; i++) {
		unsigned r = (unsigned)(rngNext() % 100);
		if (r < 15) {
			out[i] = '\n';
		} else if (r < 60) {
			out[i] = (char)('a' + (rngNext() % 26));
		} else {
			out[i] = (char)('A' + (rngNext() % 26));
		}
	}
}

static int dumpFirstDiff(const char *a, const char *b, size_t n, size_t *out) {
	for (size_t i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			*out = i;
			return 1;
		}
	}
	return 0;
}

static int compareBuffers(struct editorDocument *doc, struct refDoc *ref,
		uint64_t seed, const struct opLog *log) {
	size_t doc_len = editorDocumentLength(doc);
	if (doc_len != ref->len) {
		fprintf(stderr,
			"text_invariants: length mismatch after op#%d kind=%u start=%zu old=%zu new=%zu: "
			"doc=%zu ref=%zu seed=0x%016llx\n",
			log->op_idx, log->kind, log->start, log->old_len, log->new_len,
			doc_len, ref->len, (unsigned long long)seed);
		return -1;
	}
	if (ref->len == 0) {
		return 0;
	}
	char *dump = editorDocumentDupRange(doc, 0, ref->len, NULL);
	if (dump == NULL) {
		fprintf(stderr, "text_invariants: dup failed (seed=0x%016llx)\n",
			(unsigned long long)seed);
		return -1;
	}
	size_t diff_at = 0;
	if (dumpFirstDiff(dump, ref->buf, ref->len, &diff_at)) {
		fprintf(stderr,
			"text_invariants: byte mismatch after op#%d kind=%u start=%zu old=%zu new=%zu: "
			"offset=%zu doc=0x%02x ref=0x%02x seed=0x%016llx\n",
			log->op_idx, log->kind, log->start, log->old_len, log->new_len,
			diff_at, (unsigned char)dump[diff_at],
			(unsigned char)ref->buf[diff_at], (unsigned long long)seed);
		free(dump);
		return -1;
	}
	free(dump);
	return 0;
}

static int checkPositionRoundtrip(struct editorDocument *doc, struct refDoc *ref,
		uint64_t seed, const struct opLog *log) {
	for (int i = 0; i < INVARIANTS_POSITION_SAMPLES; i++) {
		size_t target = ref->len == 0 ? 0 : (size_t)(rngNext() % (ref->len + 1));
		int line_idx = -1;
		size_t column = 0;
		if (!editorDocumentByteOffsetToPosition(doc, target, &line_idx, &column)) {
			fprintf(stderr,
				"text_invariants: byte->pos failed off=%zu after op#%d "
				"seed=0x%016llx\n",
				target, log->op_idx, (unsigned long long)seed);
			return -1;
		}
		size_t back = 0;
		if (!editorDocumentPositionToByteOffset(doc, line_idx, column, &back)) {
			fprintf(stderr,
				"text_invariants: pos->byte failed line=%d col=%zu after op#%d "
				"seed=0x%016llx\n",
				line_idx, column, log->op_idx, (unsigned long long)seed);
			return -1;
		}
		if (back != target) {
			fprintf(stderr,
				"text_invariants: roundtrip drift off=%zu -> pos=(%d,%zu) -> off=%zu "
				"after op#%d seed=0x%016llx\n",
				target, line_idx, column, back, log->op_idx,
				(unsigned long long)seed);
			return -1;
		}
	}
	return 0;
}

static int checkLineCountMatchesNewlines(struct editorDocument *doc, struct refDoc *ref,
		uint64_t seed, const struct opLog *log) {
	int line_count = editorDocumentLineCount(doc);
	int newlines = 0;
	for (size_t i = 0; i < ref->len; i++) {
		if (ref->buf[i] == '\n') {
			newlines++;
		}
	}
	int expected;
	if (ref->len == 0) {
		expected = 0;
	} else {
		expected = newlines + (ref->buf[ref->len - 1] == '\n' ? 0 : 1);
	}
	if (line_count != expected) {
		fprintf(stderr,
			"text_invariants: line count mismatch after op#%d: doc=%d expected=%d "
			"newlines=%d trailing_nl=%d seed=0x%016llx\n",
			log->op_idx, line_count, expected, newlines,
			ref->len > 0 && ref->buf[ref->len - 1] == '\n',
			(unsigned long long)seed);
		return -1;
	}
	return 0;
}

static int runRandomOps(uint64_t seed, int n_ops, size_t doc_cap, const char *test_name) {
	g_rng = seed;
	struct editorDocument doc;
	editorDocumentInit(&doc);
	struct refDoc ref;
	if (refDocInit(&ref) != 0) {
		editorDocumentFree(&doc);
		return 1;
	}

	char seed_text[256];
	size_t seed_len = (size_t)(rngNext() % 128);
	rngFillText(seed_text, seed_len);
	if (!editorDocumentResetFromString(&doc, seed_text, seed_len)) {
		fprintf(stderr, "%s: reset failed seed=0x%016llx\n", test_name,
			(unsigned long long)seed);
		editorDocumentFree(&doc);
		refDocFree(&ref);
		return 1;
	}
	(void)refDocReplace(&ref, 0, 0, seed_text, seed_len);

	struct opLog init_log = {-1, 99, 0, 0, seed_len};
	if (compareBuffers(&doc, &ref, seed, &init_log) != 0 ||
		checkPositionRoundtrip(&doc, &ref, seed, &init_log) != 0 ||
		checkLineCountMatchesNewlines(&doc, &ref, seed, &init_log) != 0) {
		editorDocumentFree(&doc);
		refDocFree(&ref);
		return 1;
	}

	for (int i = 0; i < n_ops; i++) {
		size_t cur = ref.len;
		unsigned kind = (unsigned)(rngNext() % 4);
		size_t start = cur == 0 ? 0 : (size_t)(rngNext() % (cur + 1));
		size_t available = cur - start;
		size_t old_len = 0;
		if (kind == 1 || kind == 2) {
			old_len = available == 0 ? 0 : (size_t)(rngNext() % (available + 1));
		}
		size_t new_len = 0;
		if (kind != 1) {
			size_t budget = doc_cap > cur + old_len ? doc_cap - (cur - old_len) : 0;
			size_t max_insert = INVARIANTS_MAX_INSERT_LEN < budget
				? INVARIANTS_MAX_INSERT_LEN : budget;
			new_len = max_insert == 0 ? 0 : (size_t)(rngNext() % (max_insert + 1));
		}
		char ins_buf[INVARIANTS_MAX_INSERT_LEN];
		if (new_len > 0) {
			rngFillText(ins_buf, new_len);
		}

		struct opLog log = {i, kind, start, old_len, new_len};

		if (!editorDocumentReplaceRange(&doc, start, old_len, ins_buf, new_len)) {
			fprintf(stderr,
				"%s: editorDocumentReplaceRange failed op#%d start=%zu old=%zu new=%zu "
				"cur=%zu seed=0x%016llx\n",
				test_name, i, start, old_len, new_len, cur,
				(unsigned long long)seed);
			editorDocumentFree(&doc);
			refDocFree(&ref);
			return 1;
		}
		if (refDocReplace(&ref, start, old_len, ins_buf, new_len) != 0) {
			fprintf(stderr,
				"%s: ref replace failed op#%d (impossible; bug in test) seed=0x%016llx\n",
				test_name, i, (unsigned long long)seed);
			editorDocumentFree(&doc);
			refDocFree(&ref);
			return 1;
		}
		if (compareBuffers(&doc, &ref, seed, &log) != 0 ||
			checkPositionRoundtrip(&doc, &ref, seed, &log) != 0 ||
			checkLineCountMatchesNewlines(&doc, &ref, seed, &log) != 0) {
			editorDocumentFree(&doc);
			refDocFree(&ref);
			return 1;
		}
	}

	editorDocumentFree(&doc);
	refDocFree(&ref);
	return 0;
}

static uint64_t seedFor(uint64_t salt) {
	uint64_t base = rotide_test_seed();
	if (base == 0) {
		base = 0x9E3779B97F4A7C15ULL;
	}
	return base ^ salt;
}

static int test_text_invariants_small_doc(void) {
	return runRandomOps(seedFor(0x5A5A5A5A5A5A5A5AULL), 50, 1024, "small_doc");
}

static int test_text_invariants_medium_doc(void) {
	return runRandomOps(seedFor(0xCAFEBABEFACEFEEDULL), 200, 4096, "medium_doc");
}

static int test_text_invariants_burst(void) {
	return runRandomOps(seedFor(0xDEADBEEF13371337ULL), 500, INVARIANTS_MAX_DOC_LEN,
		"burst");
}

static int test_text_invariants_empty_doc_grows(void) {
	g_rng = seedFor(0x0FEDCBA987654321ULL);
	struct editorDocument doc;
	editorDocumentInit(&doc);
	struct refDoc ref;
	if (refDocInit(&ref) != 0) {
		editorDocumentFree(&doc);
		return 1;
	}
	struct opLog log = {0, 0, 0, 0, 0};
	const char *t = "hello\nworld\n";
	ASSERT_TRUE(editorDocumentReplaceRange(&doc, 0, 0, t, strlen(t)));
	(void)refDocReplace(&ref, 0, 0, t, strlen(t));
	if (compareBuffers(&doc, &ref, rotide_test_seed(), &log) != 0 ||
		checkLineCountMatchesNewlines(&doc, &ref, rotide_test_seed(), &log) != 0) {
		editorDocumentFree(&doc);
		refDocFree(&ref);
		return 1;
	}
	ASSERT_TRUE(editorDocumentReplaceRange(&doc, 0, editorDocumentLength(&doc), "", 0));
	(void)refDocReplace(&ref, 0, ref.len, "", 0);
	if (compareBuffers(&doc, &ref, rotide_test_seed(), &log) != 0) {
		editorDocumentFree(&doc);
		refDocFree(&ref);
		return 1;
	}
	editorDocumentFree(&doc);
	refDocFree(&ref);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Editor-pipeline invariants (Phase 3 items 1, 4, 5):                  */
/*   1. editorDocumentLength == sum of row sizes + newlines             */
/*   4. small edits splice, do not full-rebuild the row cache           */
/*   5. undo of K ops returns text to the pre-op snapshot               */
/* ------------------------------------------------------------------ */

static int docHasTrailingNewline(void) {
	size_t len = editorDocumentLength(E.document);
	if (len == 0) {
		return 0;
	}
	char last;
	if (!editorDocumentCopyRange(E.document, len - 1, len, &last)) {
		return 0;
	}
	return last == '\n';
}

static size_t sumRowBytesIncludingNewlines(void) {
	size_t total = 0;
	for (int r = 0; r < E.numrows; r++) {
		total += (size_t)E.rows[r].size;
	}
	if (E.numrows > 0) {
		total += (size_t)(E.numrows - 1);
		if (docHasTrailingNewline()) {
			total++;
		}
	}
	return total;
}

static int dumpEditorRows(char *out, size_t cap) {
	size_t pos = 0;
	int trailing = docHasTrailingNewline();
	for (int r = 0; r < E.numrows; r++) {
		size_t n = (size_t)E.rows[r].size;
		if (pos + n > cap) {
			return -1;
		}
		memcpy(out + pos, E.rows[r].chars, n);
		pos += n;
		if (r + 1 < E.numrows || trailing) {
			if (pos + 1 > cap) {
				return -1;
			}
			out[pos++] = '\n';
		}
	}
	return (int)pos;
}

static int byteOffsetToPos(size_t offset, int *cy_out, int *cx_out) {
	int line_idx = 0;
	size_t col = 0;
	if (!editorDocumentByteOffsetToPosition(E.document, offset, &line_idx, &col)) {
		return 0;
	}
	*cy_out = line_idx;
	*cx_out = (int)col;
	return 1;
}

static int seedEditorBuffer(const char *text, size_t len) {
	if (!editorDocumentResetActiveFromText("", 0)) {
		return 0;
	}
	if (E.document == NULL) {
		return 0;
	}
	E.cy = 0;
	E.cx = 0;
	if (len > 0) {
		if (editorInsertText(text, len) != 1) {
			return 0;
		}
	}
	editorHistoryReset();
	return 1;
}

static int assertEditorMatchesRef(struct refDoc *ref, uint64_t seed, int op_idx) {
	size_t expected_total = sumRowBytesIncludingNewlines();
	size_t doc_len = editorDocumentLength(E.document);
	if (doc_len != expected_total) {
		fprintf(stderr,
			"text_invariants pipeline: docLen=%zu != row-sum=%zu after op#%d seed=0x%016llx\n",
			doc_len, expected_total, op_idx, (unsigned long long)seed);
		return -1;
	}
	if (doc_len != ref->len) {
		fprintf(stderr,
			"text_invariants pipeline: docLen=%zu != ref=%zu after op#%d seed=0x%016llx\n",
			doc_len, ref->len, op_idx, (unsigned long long)seed);
		return -1;
	}
	char *dump = (char *)malloc(doc_len + 1);
	if (dump == NULL) {
		return -1;
	}
	int dumped = dumpEditorRows(dump, doc_len + 1);
	if (dumped < 0 || (size_t)dumped != doc_len) {
		fprintf(stderr,
			"text_invariants pipeline: row dump produced %d bytes for doc=%zu after op#%d\n",
			dumped, doc_len, op_idx);
		free(dump);
		return -1;
	}
	if (doc_len > 0 && memcmp(dump, ref->buf, doc_len) != 0) {
		size_t diff = 0;
		for (size_t i = 0; i < doc_len; i++) {
			if (dump[i] != ref->buf[i]) {
				diff = i;
				break;
			}
		}
		fprintf(stderr,
			"text_invariants pipeline: byte diff at %zu after op#%d: editor=0x%02x ref=0x%02x seed=0x%016llx\n",
			diff, op_idx, (unsigned char)dump[diff], (unsigned char)ref->buf[diff],
			(unsigned long long)seed);
		free(dump);
		return -1;
	}
	free(dump);
	return 0;
}

static int applyPipelineOp(unsigned kind, size_t start, size_t old_len,
		const char *text, size_t new_len) {
	(void)kind;
	enum editorEditKind ek = (old_len > 0 && new_len == 0)
		? EDITOR_EDIT_DELETE_TEXT
		: EDITOR_EDIT_INSERT_TEXT;
	int dirty_before = E.dirty;
	int ok = 0;
	editorHistoryBeginEdit(ek);
	if (old_len == 0 && new_len > 0) {
		int cy = 0, cx = 0;
		if (byteOffsetToPos(start, &cy, &cx)) {
			E.cy = cy;
			E.cx = cx;
			ok = editorInsertText(text, new_len) == 1;
		}
	} else {
		int s_cy = 0, s_cx = 0, e_cy = 0, e_cx = 0;
		if (byteOffsetToPos(start, &s_cy, &s_cx) &&
			byteOffsetToPos(start + old_len, &e_cy, &e_cx)) {
			struct editorSelectionRange range = {s_cy, s_cx, e_cy, e_cx};
			ok = editorReplaceRange(&range, text, new_len) >= 0;
		}
	}
	editorHistoryCommitEdit(ek, ok && E.dirty != dirty_before);
	return ok ? 0 : -1;
}

static int runPipelineOps(uint64_t seed, int n_ops, size_t doc_cap,
		size_t max_insert, int small_only, const char *test_name) {
	g_rng = seed;
	struct refDoc ref;
	if (refDocInit(&ref) != 0) {
		return 1;
	}

	char seed_text[128];
	size_t seed_len = (size_t)(rngNext() % 96);
	rngFillText(seed_text, seed_len);
	if (!seedEditorBuffer(seed_text, seed_len)) {
		fprintf(stderr, "%s: seedEditorBuffer failed seed=0x%016llx\n",
			test_name, (unsigned long long)seed);
		refDocFree(&ref);
		return 1;
	}
	if (refDocReplace(&ref, 0, 0, seed_text, seed_len) != 0) {
		refDocFree(&ref);
		return 1;
	}
	if (assertEditorMatchesRef(&ref, seed, -1) != 0) {
		refDocFree(&ref);
		return 1;
	}

	int splice_before = editorRowCacheTestSpliceUpdateCount();
	int rebuild_before = editorRowCacheTestFullRebuildCount();

	for (int i = 0; i < n_ops; i++) {
		size_t cur = ref.len;
		unsigned kind = (unsigned)(rngNext() % 4);
		size_t start = cur == 0 ? 0 : (size_t)(rngNext() % (cur + 1));
		size_t available = cur - start;
		size_t old_len = 0;
		if (kind == 1 || kind == 2) {
			size_t cap_del = small_only ? (available < 8 ? available : 8) : available;
			old_len = cap_del == 0 ? 0 : (size_t)(rngNext() % (cap_del + 1));
		}
		size_t new_len = 0;
		if (kind != 1) {
			size_t budget = doc_cap > cur ? doc_cap - cur : 0;
			size_t cap_new = max_insert < budget ? max_insert : budget;
			new_len = cap_new == 0 ? 0 : (size_t)(rngNext() % (cap_new + 1));
		}
		char ins[64];
		if (new_len > sizeof(ins)) {
			new_len = sizeof(ins);
		}
		if (new_len > 0) {
			if (small_only) {
				for (size_t j = 0; j < new_len; j++) {
					ins[j] = (char)('a' + (rngNext() % 26));
				}
			} else {
				rngFillText(ins, new_len);
			}
		}
		editorHistoryBreakGroup();
		if (applyPipelineOp(kind, start, old_len, ins, new_len) != 0) {
			fprintf(stderr,
				"%s: pipeline op#%d failed kind=%u start=%zu old=%zu new=%zu seed=0x%016llx\n",
				test_name, i, kind, start, old_len, new_len, (unsigned long long)seed);
			refDocFree(&ref);
			return 1;
		}
		if (refDocReplace(&ref, start, old_len, ins, new_len) != 0) {
			refDocFree(&ref);
			return 1;
		}
		if (assertEditorMatchesRef(&ref, seed, i) != 0) {
			refDocFree(&ref);
			return 1;
		}
	}

	if (small_only) {
		int rebuilds = editorRowCacheTestFullRebuildCount() - rebuild_before;
		int splices = editorRowCacheTestSpliceUpdateCount() - splice_before;
		if (rebuilds > 0) {
			fprintf(stderr,
				"%s: %d full-rebuild(s) during small-edit run (splices=%d seed=0x%016llx)\n",
				test_name, rebuilds, splices, (unsigned long long)seed);
			refDocFree(&ref);
			return 1;
		}
		if (n_ops > 0 && splices == 0) {
			fprintf(stderr,
				"%s: no splice updates despite %d small ops (seed=0x%016llx)\n",
				test_name, n_ops, (unsigned long long)seed);
			refDocFree(&ref);
			return 1;
		}
	}

	refDocFree(&ref);
	return 0;
}

static int test_text_invariants_pipeline_length_and_rows(void) {
	return runPipelineOps(seedFor(0x123456789ABCDEF0ULL), 80, 2048, 16, 0,
		"pipeline_length_and_rows");
}

static int test_text_invariants_pipeline_small_edits_splice(void) {
	return runPipelineOps(seedFor(0x0E1E2E3E4E5E6E7EULL), 60, 1024, 8, 1,
		"pipeline_small_edits");
}

static int test_text_invariants_pipeline_undo_redo_snapshot(void) {
	uint64_t seed = seedFor(0xFADEFADEFADEFADEULL);
	g_rng = seed;
	struct refDoc ref;
	if (refDocInit(&ref) != 0) {
		return 1;
	}
	const char *base = "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\n";
	size_t base_len = strlen(base);
	if (!seedEditorBuffer(base, base_len)) {
		fprintf(stderr, "pipeline_undo_redo: seed failed\n");
		refDocFree(&ref);
		return 1;
	}
	(void)refDocReplace(&ref, 0, 0, base, base_len);

	size_t snap_len = editorDocumentLength(E.document);
	char *snap = (char *)malloc(snap_len + 1);
	if (snap == NULL) {
		refDocFree(&ref);
		return 1;
	}
	int snap_dumped = dumpEditorRows(snap, snap_len + 1);
	if (snap_dumped < 0 || (size_t)snap_dumped != snap_len) {
		fprintf(stderr, "pipeline_undo_redo: snapshot dump failed\n");
		free(snap);
		refDocFree(&ref);
		return 1;
	}

	const int K = 12;
	for (int i = 0; i < K; i++) {
		size_t cur = editorDocumentLength(E.document);
		size_t start = cur == 0 ? 0 : (size_t)(rngNext() % (cur + 1));
		unsigned kind = (unsigned)(rngNext() % 3);
		size_t old_len = 0;
		size_t new_len = 0;
		char ins[16];
		if (kind == 0) {
			new_len = 1 + (size_t)(rngNext() % 8);
			for (size_t j = 0; j < new_len; j++) {
				ins[j] = (char)('A' + (rngNext() % 26));
			}
		} else if (kind == 1) {
			size_t avail = cur - start;
			if (avail == 0) {
				continue;
			}
			old_len = 1 + (size_t)(rngNext() % (avail < 4 ? avail : 4));
		} else {
			size_t avail = cur - start;
			old_len = avail == 0 ? 0 : 1 + (size_t)(rngNext() % (avail < 4 ? avail : 4));
			new_len = 1 + (size_t)(rngNext() % 8);
			for (size_t j = 0; j < new_len; j++) {
				ins[j] = (char)('a' + (rngNext() % 26));
			}
		}
		editorHistoryBreakGroup();
		if (applyPipelineOp(kind, start, old_len, ins, new_len) != 0) {
			fprintf(stderr, "pipeline_undo_redo: op#%d failed\n", i);
			free(snap);
			refDocFree(&ref);
			return 1;
		}
	}

	int undo_calls = 0;
	while (editorUndo() == 1) {
		undo_calls++;
		if (undo_calls > K + 4) {
			break;
		}
	}

	size_t after_len = editorDocumentLength(E.document);
	if (after_len != snap_len) {
		fprintf(stderr,
			"pipeline_undo_redo: length after undo=%zu, snapshot=%zu (undos=%d, seed=0x%016llx)\n",
			after_len, snap_len, undo_calls, (unsigned long long)seed);
		free(snap);
		refDocFree(&ref);
		return 1;
	}
	char *after = (char *)malloc(after_len + 1);
	if (after == NULL) {
		free(snap);
		refDocFree(&ref);
		return 1;
	}
	int after_dumped = dumpEditorRows(after, after_len + 1);
	if (after_dumped < 0 || (size_t)after_dumped != after_len) {
		fprintf(stderr, "pipeline_undo_redo: after-dump failed\n");
		free(after);
		free(snap);
		refDocFree(&ref);
		return 1;
	}
	if (memcmp(snap, after, snap_len) != 0) {
		size_t diff = 0;
		for (size_t i = 0; i < snap_len; i++) {
			if (snap[i] != after[i]) {
				diff = i;
				break;
			}
		}
		fprintf(stderr,
			"pipeline_undo_redo: byte diff at %zu (snap=0x%02x after=0x%02x undos=%d seed=0x%016llx)\n",
			diff, (unsigned char)snap[diff], (unsigned char)after[diff],
			undo_calls, (unsigned long long)seed);
		free(after);
		free(snap);
		refDocFree(&ref);
		return 1;
	}
	free(after);
	free(snap);
	refDocFree(&ref);
	return 0;
}

const struct editorTestCase g_text_invariants_tests[] = {
	{"text_invariants_empty_doc_grows_and_shrinks", test_text_invariants_empty_doc_grows},
	{"text_invariants_small_doc_random_ops", test_text_invariants_small_doc},
	{"text_invariants_medium_doc_random_ops", test_text_invariants_medium_doc},
	{"text_invariants_burst_500_ops", test_text_invariants_burst},
	{"text_invariants_pipeline_length_and_rows", test_text_invariants_pipeline_length_and_rows},
	{"text_invariants_pipeline_small_edits_splice", test_text_invariants_pipeline_small_edits_splice},
	{"text_invariants_pipeline_undo_redo_snapshot", test_text_invariants_pipeline_undo_redo_snapshot},
};

const int g_text_invariants_test_count =
	(int)(sizeof(g_text_invariants_tests) / sizeof(g_text_invariants_tests[0]));
