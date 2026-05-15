#include "editing/buffer_core.h"

#include "editing/document_bridge.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/row_cache.h"
#include "editing/selection.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "language/syntax_worker.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "text/document.h"
#include "text/row.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct editorDocument *editorDocumentAlloc(void);
static int editorSyntaxByteRangeToVisibleRows(size_t start_byte, size_t end_byte,
		int *start_row_out, int *end_row_exclusive_out);
static int editorSyntaxVisibleCacheInvalidateChangedRowsFromState(void);
static void editorSyntaxVisibleCacheInvalidateRowsForEdit(const struct editorSyntaxEdit *edit);
static void editorSyntaxDisableWithStatus(const char *message);
int editorSyntaxParseFullActive(void);
void editorSyntaxVisibleCacheInvalidate(void);
static void editorSyntaxVisibleCacheInvalidateRows(int start_row, int end_row_exclusive);
static void editorSyntaxReportStatusIfNeeded(void);
static void editorSyntaxReportBudgetStatusIfNeeded(void);
static void editorSyntaxReportQueryUnavailableStatusIfNeeded(void);
static int editorSyntaxScheduleBackgroundActive(int first_row, int row_count);
static int editorSyntaxVisibleCacheStoreBackgroundResult(
		const struct editorSyntaxWorkerResult *result);
static int editorLspActiveBufferTracked(void);
void editorLspNotifyDidSaveActive(void);
void editorLspNotifyDidCloseTabState(struct editorTabState *tab);

static uint64_t g_syntax_generation_counter = 0;

static uint64_t editorSyntaxNextGeneration(void) {
	if (g_syntax_generation_counter == UINT64_MAX) {
		g_syntax_generation_counter = 1;
	}
	return ++g_syntax_generation_counter;
}

#define ROTIDE_SYNTAX_PARSE_FAILURE_LIMIT 3
#define ROTIDE_SYNTAX_BACKGROUND_MIN_OVERSCAN_ROWS 64
#define ROTIDE_SYNTAX_BACKGROUND_MAX_OVERSCAN_ROWS 256

void editorSetAllocFailureStatus(void) {
	editorSetStatusMsg("Out of memory");
}

void editorSetOperationTooLargeStatus(void) {
	editorSetStatusMsg("Operation too large");
}

void editorSetFileTooLargeStatus(void) {
	editorSetStatusMsg("File too large");
}

static const char *editorSyntaxPerformanceStatusForMode(enum editorSyntaxPerformanceMode mode) {
	switch (mode) {
		case EDITOR_SYNTAX_PERF_DEGRADED_PREDICATES:
			return "Tree-sitter degraded (large file: predicates/locals limited)";
		case EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS:
			return "Tree-sitter degraded (large file: HTML injections disabled)";
		case EDITOR_SYNTAX_PERF_DISABLED:
			return "Tree-sitter disabled (file too large for syntax)";
		case EDITOR_SYNTAX_PERF_NORMAL:
		default:
			return NULL;
	}
}

static int editorSyntaxConfigurePerformanceForLength(size_t source_len, int set_status_on_change) {
	if (E.syntax_state == NULL) {
		return 1;
	}

	enum editorSyntaxPerformanceMode old_mode =
			editorSyntaxStatePerformanceMode(E.syntax_state);
	if (!editorSyntaxStateConfigureForSourceLength(E.syntax_state, source_len)) {
		editorSyntaxDisableWithStatus("Tree-sitter disabled (file too large for syntax)");
		return 0;
	}

	enum editorSyntaxPerformanceMode new_mode =
			editorSyntaxStatePerformanceMode(E.syntax_state);
	if (set_status_on_change && new_mode != old_mode) {
		const char *status = editorSyntaxPerformanceStatusForMode(new_mode);
		if (status != NULL) {
			editorSetStatusMsg("%s", status);
		}
	}

	return 1;
}

static void editorSyntaxReportBudgetStatusIfNeeded(void) {
	if (E.syntax_state == NULL) {
		return;
	}

	int parse_budget_exceeded = 0;
	int query_budget_exceeded = 0;
	if (!editorSyntaxStateConsumeBudgetEvents(E.syntax_state, &parse_budget_exceeded,
				&query_budget_exceeded)) {
		return;
	}

	static time_t last_report_time = 0;
	time_t now = time(NULL);
	if (!editorSyntaxTestBudgetOverridesEnabled() &&
			last_report_time != 0 && now - last_report_time < 2) {
		return;
	}
	last_report_time = now;

	if (parse_budget_exceeded && query_budget_exceeded) {
		editorSetStatusMsg("Tree-sitter throttled (parse/query budget)");
	} else if (parse_budget_exceeded) {
		editorSetStatusMsg("Tree-sitter parse throttled (budget)");
	} else if (query_budget_exceeded) {
		editorSetStatusMsg("Tree-sitter highlight throttled (budget)");
	}
}

static void editorSyntaxReportQueryUnavailableStatusIfNeeded(void) {
	if (E.syntax_state == NULL) {
		return;
	}

	enum editorSyntaxLanguage language = EDITOR_SYNTAX_NONE;
	enum editorSyntaxQueryKind kind = EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT;
	if (!editorSyntaxStateConsumeQueryUnavailableEvent(E.syntax_state, &language, &kind)) {
		return;
	}
	(void)language;

	if (kind == EDITOR_SYNTAX_QUERY_KIND_INJECTION) {
		editorSetStatusMsg("Tree-sitter injection query unavailable");
	} else {
		editorSetStatusMsg("Tree-sitter highlight query unavailable");
	}
}

static void editorSyntaxReportLimitStatusIfNeeded(void) {
	if (E.syntax_state == NULL) {
		return;
	}

	struct editorSyntaxLimitEvent event = {0};
	if (!editorSyntaxStateConsumeLimitEvent(E.syntax_state, &event)) {
		return;
	}

	if (event.kind == EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED) {
		editorSetStatusMsg("Tree-sitter injection depth limit reached");
	} else if (event.kind == EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL) {
		editorSetStatusMsg("Tree-sitter injection slot limit reached");
	} else if (event.kind == EDITOR_SYNTAX_LIMIT_EVENT_PARSE_FAILED) {
		editorSetStatusMsg("Tree-sitter parse failed (will retry)");
	} else if (event.kind == EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR) {
		editorSetStatusMsg("Tree-sitter parse tree has errors");
	} else {
		editorSetStatusMsg("Tree-sitter syntax spans truncated");
	}
}

static void editorSyntaxReportStatusIfNeeded(void) {
	editorSyntaxReportBudgetStatusIfNeeded();
	editorSyntaxReportQueryUnavailableStatusIfNeeded();
	editorSyntaxReportLimitStatusIfNeeded();
}

static void editorSyntaxDeactivateActive(void) {
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.syntax_parse_failures = 0;
	E.syntax_background_pending = 0;
	E.syntax_revision++;
	E.syntax_generation = editorSyntaxNextGeneration();
	editorSyntaxVisibleCacheInvalidate();
}

static void editorSyntaxDisableWithStatus(const char *message) {
	editorSyntaxDeactivateActive();
	if (message != NULL && message[0] != '\0') {
		editorSetStatusMsg("%s", message);
	}
}

static void editorSyntaxResetParseFailures(void) {
	E.syntax_parse_failures = 0;
}

static int editorSyntaxRecordParseFailureActive(void) {
	if (E.syntax_parse_failures < ROTIDE_SYNTAX_PARSE_FAILURE_LIMIT) {
		E.syntax_parse_failures++;
	}
	if (E.syntax_state != NULL) {
		editorSyntaxStateRecordParseFailed(E.syntax_state, E.syntax_parse_failures);
	}
	editorSetStatusMsg("Tree-sitter parse failed (will retry)");
	editorSyntaxVisibleCacheInvalidate();
	if (E.syntax_parse_failures >= ROTIDE_SYNTAX_PARSE_FAILURE_LIMIT) {
		editorSyntaxDisableWithStatus("Tree-sitter disabled (parse failed)");
	}
	return 0;
}

int editorSyntaxBackgroundPoll(void) {
	struct editorSyntaxWorkerResult *result = editorSyntaxWorkerTakeResult();
	if (result == NULL) {
		return 0;
	}

	if (result->language != E.syntax_language ||
			result->revision != E.syntax_revision ||
			result->generation != E.syntax_generation) {
		editorSyntaxWorkerResultDestroy(result);
		return 0;
	}

	E.syntax_background_pending = 0;
	if (!result->parsed || result->state == NULL) {
		editorSyntaxWorkerResultDestroy(result);
		(void)editorSyntaxRecordParseFailureActive();
		return 1;
	}

	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = result->state;
	result->state = NULL;
	if (!editorSyntaxVisibleCacheStoreBackgroundResult(result)) {
		editorSetAllocFailureStatus();
	}
	editorSyntaxResetParseFailures();
	editorSyntaxReportStatusIfNeeded();
	editorSyntaxWorkerResultDestroy(result);
	return 1;
}

int editorSyntaxBackgroundFlushForTests(void) {
	while (editorSyntaxWorkerHasWork()) {
		editorSyntaxWorkerWaitForIdle();
		editorSyntaxBackgroundPoll();
	}
	editorSyntaxBackgroundPoll();
	return 1;
}

static int editorSyntaxOffsetToU32(size_t offset, uint32_t *out) {
	if (out == NULL || offset > UINT32_MAX) {
		return 0;
	}
	*out = (uint32_t)offset;
	return 1;
}

static struct editorDocument *editorDocumentAlloc(void) {
	struct editorDocument *document = editorMalloc(sizeof(*document));
	if (document == NULL) {
		return NULL;
	}
	editorDocumentInit(document);
	return document;
}

static int editorSyntaxReconfigureForFilename(void) {
	const char *first_line = NULL;
	if (E.numrows > 0 && E.rows != NULL) {
		first_line = E.rows[0].chars;
	}

	enum editorSyntaxLanguage wanted = E.tab_kind == EDITOR_TAB_GIT_DIFF ?
			EDITOR_SYNTAX_DIFF :
			editorSyntaxDetectLanguageFromFilenameAndFirstLine(E.filename, first_line);
	if (wanted == EDITOR_SYNTAX_NONE) {
		editorSyntaxDeactivateActive();
		return 1;
	}

	if (editorSyntaxBackgroundEnabled()) {
		if (E.syntax_language == wanted && E.syntax_generation != 0) {
			return 1;
		}
		editorSyntaxStateDestroy(E.syntax_state);
		E.syntax_state = NULL;
		E.syntax_language = wanted;
		E.syntax_generation = editorSyntaxNextGeneration();
		E.syntax_revision = 0;
		E.syntax_background_pending = 0;
		editorSyntaxResetParseFailures();
		editorSyntaxVisibleCacheInvalidate();
		return 1;
	}

	if (E.syntax_state != NULL && E.syntax_language == wanted) {
		return 1;
	}

	editorSyntaxDeactivateActive();
	E.syntax_state = editorSyntaxStateCreate(wanted);
	if (E.syntax_state == NULL) {
		editorSetStatusMsg("Tree-sitter disabled (parser init failed)");
		return 0;
	}

	E.syntax_language = wanted;
	editorSyntaxResetParseFailures();
	return 1;
}

int editorSyntaxParseFullActive(void) {
	if (!editorSyntaxReconfigureForFilename()) {
		return 0;
	}
	if (editorSyntaxBackgroundEnabled()) {
		if (E.syntax_language == EDITOR_SYNTAX_NONE) {
			return 1;
		}
		E.syntax_revision++;
		return editorSyntaxScheduleBackgroundActive(E.rowoff, E.window_rows);
	}
	if (E.syntax_state == NULL) {
		return 1;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		editorSyntaxDisableWithStatus("Tree-sitter disabled (buffer too large)");
		return 0;
	}

	if (!editorSyntaxConfigurePerformanceForLength(source.length, 1)) {
		return 0;
	}

	int parsed = editorSyntaxStateParseFull(E.syntax_state, &source);
	if (!parsed) {
		return editorSyntaxRecordParseFailureActive();
	}
	editorSyntaxResetParseFailures();
	editorSyntaxReportStatusIfNeeded();
	editorSyntaxVisibleCacheInvalidate();
	return 1;
}

int editorSyntaxApplyIncrementalEditActive(const struct editorSyntaxEdit *edit,
		const char *inserted_text,
		size_t inserted_len) {
	if (E.syntax_language == EDITOR_SYNTAX_NONE) {
		return 1;
	}

	if (inserted_len > 0 && inserted_text == NULL) {
		return 0;
	}

	if (editorSyntaxBackgroundEnabled()) {
		E.syntax_revision++;
		return editorSyntaxScheduleBackgroundActive(E.rowoff, E.window_rows);
	}

	if (E.syntax_state == NULL) {
		return 1;
	}

	if (E.syntax_parse_failures == 0 && edit != NULL &&
			editorSyntaxStateHasTree(E.syntax_state)) {
		size_t old_len = editorSyntaxStateSourceLength(E.syntax_state);
		if (edit->old_end_byte >= edit->start_byte &&
				(size_t)edit->old_end_byte <= old_len) {
			size_t removed_len = (size_t)(edit->old_end_byte - edit->start_byte);
			if (removed_len <= old_len) {
				size_t new_len = old_len - removed_len + inserted_len;
				if (!editorSyntaxConfigurePerformanceForLength(new_len, 1)) {
					return 0;
				}
				struct editorTextSource source = {0};
				if (editorBuildActiveTextSource(&source) &&
						editorSyntaxStateApplyEditAndParse(E.syntax_state, edit, &source)) {
					editorSyntaxResetParseFailures();
					editorSyntaxVisibleCacheInvalidateRowsForEdit(edit);
					if (!editorSyntaxVisibleCacheInvalidateChangedRowsFromState()) {
						editorSyntaxVisibleCacheInvalidate();
					}
					editorSyntaxReportStatusIfNeeded();
					return 1;
				}
			}
		}
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		editorSyntaxDisableWithStatus("Tree-sitter disabled (buffer too large)");
		return 0;
	}

	if (!editorSyntaxConfigurePerformanceForLength(source.length, 1)) {
		return 0;
	}

	int parsed = editorSyntaxStateParseFull(E.syntax_state, &source);
	if (!parsed) {
		return editorSyntaxRecordParseFailureActive();
	}
	editorSyntaxResetParseFailures();
	editorSyntaxReportStatusIfNeeded();
	editorSyntaxVisibleCacheInvalidate();
	return 1;
}

static int editorLspActiveBufferTracked(void) {
	return editorLspFileEnabled(E.filename, E.syntax_language);
}

static int editorLspActiveBufferTrackedForEslint(void) {
	return editorLspEslintEnabledForFile(E.filename, E.syntax_language);
}

void editorLspNotifyDidSaveActive(void) {
	if (!editorLspActiveBufferTracked() && !editorLspActiveBufferTrackedForEslint()) {
		return;
	}

	char *full_text = NULL;
	size_t full_text_len = 0;
	if ((editorLspActiveBufferTracked() && !E.lsp_doc_open) ||
			(editorLspActiveBufferTrackedForEslint() && !E.lsp_eslint_doc_open)) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			free(full_text);
			return;
		}
		if (editorLspActiveBufferTracked()) {
			(void)editorLspEnsureDocumentOpen(E.filename, E.syntax_language,
					&E.lsp_doc_open, &E.lsp_doc_version,
					full_text != NULL ? full_text : "", full_text_len);
		}
		if (editorLspActiveBufferTrackedForEslint()) {
			(void)editorLspEnsureEslintDocumentOpen(E.filename, E.syntax_language,
					&E.lsp_eslint_doc_open, &E.lsp_eslint_doc_version,
					full_text != NULL ? full_text : "", full_text_len);
		}
	}
	free(full_text);
	if (editorLspActiveBufferTracked()) {
		(void)editorLspNotifyDidSave(E.filename, E.syntax_language,
				&E.lsp_doc_open, &E.lsp_doc_version);
	}
	if (editorLspActiveBufferTrackedForEslint()) {
		(void)editorLspNotifyEslintDidSave(E.filename, E.syntax_language,
				&E.lsp_eslint_doc_open, &E.lsp_eslint_doc_version);
	}
}

void editorLspNotifyDidCloseTabState(struct editorTabState *tab) {
	if (tab == NULL) {
		return;
	}
	editorLspNotifyDidClose(tab->filename, tab->syntax_language,
			&tab->lsp_doc_open, &tab->lsp_doc_version);
	editorLspNotifyEslintDidClose(tab->filename, tab->syntax_language,
			&tab->lsp_eslint_doc_open, &tab->lsp_eslint_doc_version);
}

char *editorRowsToStr(size_t *buflen) {
	if (buflen == NULL) {
		errno = EINVAL;
		return NULL;
	}
	*buflen = 0;

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		errno = EIO;
		return NULL;
	}
	return editorTextSourceDupRange(&source, 0, source.length, buflen);
}

struct editorVisibleSyntaxCache {
	int prepared;
	int first_row;
	int row_count;
	int row_capacity;
	const struct editorSyntaxState *state;
	enum editorSyntaxLanguage language;
	uint64_t revision;
	uint64_t generation;
	int *span_counts;
	uint8_t *row_dirty;
	struct editorRowSyntaxSpan *spans;
};

static struct editorVisibleSyntaxCache g_visible_syntax_cache = {0};
static int g_visible_syntax_row_recompute_count = 0;

void editorSyntaxVisibleCacheInvalidate(void) {
	g_visible_syntax_cache.prepared = 0;
	g_visible_syntax_cache.first_row = 0;
	g_visible_syntax_cache.row_count = 0;
	g_visible_syntax_cache.state = NULL;
	g_visible_syntax_cache.language = EDITOR_SYNTAX_NONE;
	g_visible_syntax_cache.revision = 0;
	g_visible_syntax_cache.generation = 0;
}

static void editorSyntaxVisibleCacheInvalidateRows(int start_row, int end_row_exclusive) {
	if (!g_visible_syntax_cache.prepared || g_visible_syntax_cache.row_dirty == NULL ||
			start_row >= end_row_exclusive) {
		return;
	}

	int cache_start = g_visible_syntax_cache.first_row;
	int cache_end = g_visible_syntax_cache.first_row + g_visible_syntax_cache.row_count;
	if (end_row_exclusive <= cache_start || start_row >= cache_end) {
		return;
	}
	if (start_row < cache_start) {
		start_row = cache_start;
	}
	if (end_row_exclusive > cache_end) {
		end_row_exclusive = cache_end;
	}

	for (int row = start_row; row < end_row_exclusive; row++) {
		int rel_row = row - cache_start;
		g_visible_syntax_cache.row_dirty[rel_row] = 1;
	}
}

void editorSyntaxVisibleCacheFree(void) {
	free(g_visible_syntax_cache.span_counts);
	free(g_visible_syntax_cache.row_dirty);
	free(g_visible_syntax_cache.spans);
	memset(&g_visible_syntax_cache, 0, sizeof(g_visible_syntax_cache));
}

static int editorSyntaxVisibleCacheEnsureCapacity(int row_count) {
	if (row_count <= g_visible_syntax_cache.row_capacity) {
		return 1;
	}
	if (row_count <= 0) {
		return 1;
	}

	size_t counts_bytes = 0;
	size_t dirty_bytes = 0;
	size_t span_rows = 0;
	size_t spans_bytes = 0;
	if (!editorIntToSize(row_count, &span_rows) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.span_counts), span_rows, &counts_bytes) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.row_dirty), span_rows, &dirty_bytes) ||
			!editorSizeMul(span_rows, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_rows) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.spans), span_rows, &spans_bytes)) {
		return 0;
	}

	int *new_counts = editorRealloc(g_visible_syntax_cache.span_counts, counts_bytes);
	if (new_counts == NULL) {
		return 0;
	}
	uint8_t *new_dirty = editorRealloc(g_visible_syntax_cache.row_dirty, dirty_bytes);
	if (new_dirty == NULL) {
		return 0;
	}
	struct editorRowSyntaxSpan *new_spans = editorRealloc(g_visible_syntax_cache.spans, spans_bytes);
	if (new_spans == NULL) {
		return 0;
	}

	g_visible_syntax_cache.span_counts = new_counts;
	g_visible_syntax_cache.row_dirty = new_dirty;
	g_visible_syntax_cache.spans = new_spans;
	g_visible_syntax_cache.row_capacity = row_count;
	return 1;
}

static int editorSyntaxVisibleCacheStoreBackgroundResult(
		const struct editorSyntaxWorkerResult *result) {
	if (result == NULL || result->row_count < 0) {
		return 0;
	}
	if (!editorSyntaxVisibleCacheEnsureCapacity(result->row_count)) {
		return 0;
	}

	g_visible_syntax_cache.prepared = 1;
	g_visible_syntax_cache.first_row = result->first_row;
	g_visible_syntax_cache.row_count = result->row_count;
	g_visible_syntax_cache.state = E.syntax_state;
	g_visible_syntax_cache.language = result->language;
	g_visible_syntax_cache.revision = result->revision;
	g_visible_syntax_cache.generation = result->generation;
	if (result->row_count <= 0) {
		return 1;
	}

	size_t rows_size = 0;
	size_t counts_bytes = 0;
	size_t dirty_bytes = 0;
	size_t span_rows = 0;
	size_t spans_bytes = 0;
	if (!editorIntToSize(result->row_count, &rows_size) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.span_counts), rows_size,
				&counts_bytes) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.row_dirty), rows_size,
				&dirty_bytes) ||
			!editorSizeMul(rows_size, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_rows) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.spans), span_rows, &spans_bytes)) {
		return 0;
	}
	if (result->span_counts != NULL) {
		memcpy(g_visible_syntax_cache.span_counts, result->span_counts, counts_bytes);
	} else {
		memset(g_visible_syntax_cache.span_counts, 0, counts_bytes);
	}
	memset(g_visible_syntax_cache.row_dirty, 0, dirty_bytes);
	if (result->spans != NULL) {
		memcpy(g_visible_syntax_cache.spans, result->spans, spans_bytes);
	}
	g_visible_syntax_row_recompute_count += result->row_count;
	return 1;
}

static int editorSyntaxNormalizeVisibleRows(int *first_row_in_out, int *row_count_in_out) {
	if (first_row_in_out == NULL || row_count_in_out == NULL) {
		return 0;
	}
	int first_row = *first_row_in_out;
	int row_count = *row_count_in_out;
	if (row_count <= 0 || E.numrows <= 0) {
		*first_row_in_out = 0;
		*row_count_in_out = 0;
		return 1;
	}
	if (first_row < 0) {
		row_count += first_row;
		first_row = 0;
	}
	if (first_row >= E.numrows || row_count <= 0) {
		*first_row_in_out = 0;
		*row_count_in_out = 0;
		return 1;
	}
	if (first_row + row_count > E.numrows) {
		row_count = E.numrows - first_row;
	}
	if (row_count < 0) {
		row_count = 0;
	}
	*first_row_in_out = first_row;
	*row_count_in_out = row_count;
	return 1;
}

static int editorSyntaxRowRangeCovers(int cached_first, int cached_count,
		int visible_first, int visible_count) {
	if (visible_count <= 0) {
		return 1;
	}
	if (cached_count <= 0 || cached_first > visible_first) {
		return 0;
	}
	return cached_first + cached_count >= visible_first + visible_count;
}

static int editorSyntaxBackgroundExpandRows(int *first_row_in_out, int *row_count_in_out) {
	if (first_row_in_out == NULL || row_count_in_out == NULL) {
		return 0;
	}
	if (!editorSyntaxNormalizeVisibleRows(first_row_in_out, row_count_in_out)) {
		return 0;
	}
	if (*row_count_in_out <= 0) {
		return 1;
	}

	int overscan = *row_count_in_out;
	if (overscan < ROTIDE_SYNTAX_BACKGROUND_MIN_OVERSCAN_ROWS) {
		overscan = ROTIDE_SYNTAX_BACKGROUND_MIN_OVERSCAN_ROWS;
	}
	if (overscan > ROTIDE_SYNTAX_BACKGROUND_MAX_OVERSCAN_ROWS) {
		overscan = ROTIDE_SYNTAX_BACKGROUND_MAX_OVERSCAN_ROWS;
	}

	int visible_first = *first_row_in_out;
	int visible_end = visible_first + *row_count_in_out;
	int expanded_first = visible_first - overscan;
	if (expanded_first < 0) {
		expanded_first = 0;
	}
	int expanded_end = visible_end + overscan;
	if (expanded_end < visible_end || expanded_end > E.numrows) {
		expanded_end = E.numrows;
	}

	*first_row_in_out = expanded_first;
	*row_count_in_out = expanded_end - expanded_first;
	return 1;
}

static int editorSyntaxScheduleBackgroundActive(int first_row, int row_count) {
	if (!editorSyntaxBackgroundEnabled()) {
		return 0;
	}
	if (E.syntax_language == EDITOR_SYNTAX_NONE) {
		return 1;
	}
	if (!editorSyntaxBackgroundExpandRows(&first_row, &row_count)) {
		return 0;
	}
	if (E.syntax_background_pending &&
			E.syntax_pending_revision == E.syntax_revision &&
			E.syntax_pending_first_row == first_row &&
			E.syntax_pending_row_count == row_count) {
		return 1;
	}

	size_t text_len = 0;
	char *text = editorDupActiveTextSource(&text_len);
	if (text == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct editorSyntaxWorkerJob job = {
		.language = E.syntax_language,
		.revision = E.syntax_revision,
		.generation = E.syntax_generation,
		.first_row = first_row,
		.row_count = row_count,
		.text = text,
		.text_len = text_len
	};
	if (!editorSyntaxWorkerSchedule(&job)) {
		free(text);
		return 0;
	}
	E.syntax_background_pending = 1;
	E.syntax_pending_revision = E.syntax_revision;
	E.syntax_pending_first_row = first_row;
	E.syntax_pending_row_count = row_count;
	return 1;
}

static void editorSyntaxVisibleCacheMarkRowsDirty(int rel_start, int rel_end_exclusive) {
	if (rel_start < 0) {
		rel_start = 0;
	}
	if (rel_end_exclusive > g_visible_syntax_cache.row_count) {
		rel_end_exclusive = g_visible_syntax_cache.row_count;
	}
	if (rel_start >= rel_end_exclusive) {
		return;
	}

	for (int rel_row = rel_start; rel_row < rel_end_exclusive; rel_row++) {
		g_visible_syntax_cache.span_counts[rel_row] = 0;
		g_visible_syntax_cache.row_dirty[rel_row] = 1;
	}
}

static int editorSyntaxVisibleCacheSlide(int first_row, int row_count) {
	if (!g_visible_syntax_cache.prepared || g_visible_syntax_cache.row_count <= 0) {
		return 0;
	}

	int old_first = g_visible_syntax_cache.first_row;
	int old_count = g_visible_syntax_cache.row_count;
	int old_end = old_first + old_count;
	int new_end = first_row + row_count;
	int overlap_start = old_first > first_row ? old_first : first_row;
	int overlap_end = old_end < new_end ? old_end : new_end;
	if (overlap_start >= overlap_end) {
		return 0;
	}

	int src_rel = overlap_start - old_first;
	int dst_rel = overlap_start - first_row;
	int overlap_count = overlap_end - overlap_start;
	size_t overlap_size = 0;
	size_t count_bytes = 0;
	size_t dirty_bytes = 0;
	size_t span_count = 0;
	size_t span_bytes = 0;
	if (!editorIntToSize(overlap_count, &overlap_size) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.span_counts), overlap_size,
					&count_bytes) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.row_dirty), overlap_size,
					&dirty_bytes) ||
			!editorSizeMul(overlap_size, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_count) ||
			!editorSizeMul(sizeof(*g_visible_syntax_cache.spans), span_count, &span_bytes)) {
		return 0;
	}

	memmove(&g_visible_syntax_cache.span_counts[dst_rel],
			&g_visible_syntax_cache.span_counts[src_rel], count_bytes);
	memmove(&g_visible_syntax_cache.row_dirty[dst_rel],
			&g_visible_syntax_cache.row_dirty[src_rel], dirty_bytes);
	memmove(&g_visible_syntax_cache.spans[dst_rel * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW],
			&g_visible_syntax_cache.spans[src_rel * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW],
			span_bytes);

	g_visible_syntax_cache.first_row = first_row;
	g_visible_syntax_cache.row_count = row_count;
	editorSyntaxVisibleCacheMarkRowsDirty(0, dst_rel);
	editorSyntaxVisibleCacheMarkRowsDirty(dst_rel + overlap_count, row_count);
	return 1;
}

static int editorSyntaxByteRangeToVisibleRows(size_t start_byte, size_t end_byte,
		int *start_row_out, int *end_row_exclusive_out) {
	if (start_row_out == NULL || end_row_exclusive_out == NULL) {
		return 0;
	}
	*start_row_out = 0;
	*end_row_exclusive_out = 0;

	if (E.numrows <= 0) {
		return 1;
	}
	size_t total = 0;
	if (!editorBufferPosToOffset(E.numrows, 0, &total)) {
		return 0;
	}
	if (start_byte > total) {
		start_byte = total;
	}
	if (end_byte > total) {
		end_byte = total;
	}
	if (end_byte < start_byte) {
		end_byte = start_byte;
	}

	size_t end_lookup = end_byte > start_byte ? end_byte - 1 : start_byte;
	int start_row = 0;
	int start_cx = 0;
	int end_row = 0;
	int end_cx = 0;
	if (!editorBufferOffsetToPos(start_byte, &start_row, &start_cx) ||
			!editorBufferOffsetToPos(end_lookup, &end_row, &end_cx)) {
		return 0;
	}
	(void)start_cx;
	(void)end_cx;
	if (start_row == E.numrows && E.numrows > 0) {
		start_row = E.numrows - 1;
	}
	if (end_row == E.numrows && E.numrows > 0) {
		end_row = E.numrows - 1;
	}

	int end_row_exclusive = end_row + 1;
	if (start_row < 0) {
		start_row = 0;
	}
	if (start_row > E.numrows) {
		start_row = E.numrows;
	}
	if (end_row_exclusive < start_row) {
		end_row_exclusive = start_row;
	}
	if (end_row_exclusive > E.numrows) {
		end_row_exclusive = E.numrows;
	}

	*start_row_out = start_row;
	*end_row_exclusive_out = end_row_exclusive;
	return 1;
}

static int editorSyntaxVisibleCacheInvalidateChangedRowsFromState(void) {
	if (E.syntax_state == NULL || E.numrows <= 0) {
		return 1;
	}

	int range_count = 0;
	if (!editorSyntaxStateCopyLastChangedRanges(E.syntax_state, NULL, 0, &range_count)) {
		return 0;
	}
	if (range_count <= 0) {
		return 1;
	}

	size_t range_count_size = 0;
	size_t range_bytes = 0;
	if (!editorIntToSize(range_count, &range_count_size) ||
			!editorSizeMul(sizeof(struct editorSyntaxByteRange), range_count_size, &range_bytes)) {
		return 0;
	}

	struct editorSyntaxByteRange *ranges = editorMalloc(range_bytes);
	if (ranges == NULL) {
		return 0;
	}

	int copied_total = 0;
	if (!editorSyntaxStateCopyLastChangedRanges(E.syntax_state, ranges, range_count,
				&copied_total)) {
		free(ranges);
		return 0;
	}
	int copied = range_count;
	if (copied_total < copied) {
		copied = copied_total;
	}

	for (int i = 0; i < copied; i++) {
		int start_row = 0;
		int end_row_exclusive = 0;
		if (!editorSyntaxByteRangeToVisibleRows((size_t)ranges[i].start_byte,
					(size_t)ranges[i].end_byte, &start_row, &end_row_exclusive)) {
			free(ranges);
			return 0;
		}
		if (start_row > 0) {
			start_row--;
		}
		if (end_row_exclusive < E.numrows) {
			end_row_exclusive++;
		}
		editorSyntaxVisibleCacheInvalidateRows(start_row, end_row_exclusive);
	}

	free(ranges);
	return 1;
}

static void editorSyntaxVisibleCacheInvalidateRowsForEdit(const struct editorSyntaxEdit *edit) {
	if (edit == NULL || E.numrows <= 0) {
		return;
	}

	int start_row = (int)edit->start_point.row;
	int old_end_row = (int)edit->old_end_point.row;
	int new_end_row = (int)edit->new_end_point.row;

	int min_row = start_row;
	if (old_end_row < min_row) {
		min_row = old_end_row;
	}
	if (new_end_row < min_row) {
		min_row = new_end_row;
	}
	int max_row = start_row;
	if (old_end_row > max_row) {
		max_row = old_end_row;
	}
	if (new_end_row > max_row) {
		max_row = new_end_row;
	}

	if (min_row < 0) {
		min_row = 0;
	}
	if (max_row < min_row) {
		max_row = min_row;
	}
	if (max_row >= E.numrows) {
		max_row = E.numrows - 1;
	}
	editorSyntaxVisibleCacheInvalidateRows(min_row, max_row + 1);
}

static int editorSyntaxBuildVisibleSpanCache(int first_row, int row_count) {
	if (row_count <= 0 || E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE ||
			E.numrows <= 0) {
		editorSyntaxVisibleCacheInvalidate();
		g_visible_syntax_cache.prepared = 1;
		return 1;
	}

	if (first_row < 0) {
		row_count += first_row;
		first_row = 0;
	}
	if (first_row >= E.numrows || row_count <= 0) {
		editorSyntaxVisibleCacheInvalidate();
		g_visible_syntax_cache.prepared = 1;
		return 1;
	}
	if (first_row + row_count > E.numrows) {
		row_count = E.numrows - first_row;
	}
	if (row_count <= 0) {
		editorSyntaxVisibleCacheInvalidate();
		g_visible_syntax_cache.prepared = 1;
		return 1;
	}
	if (!editorSyntaxVisibleCacheEnsureCapacity(row_count)) {
		return 0;
	}
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	int cache_identity_matches = g_visible_syntax_cache.prepared &&
			g_visible_syntax_cache.state == E.syntax_state &&
			g_visible_syntax_cache.language == E.syntax_language &&
			g_visible_syntax_cache.revision == E.syntax_revision &&
			g_visible_syntax_cache.generation == E.syntax_generation;
	if (!cache_identity_matches ||
			g_visible_syntax_cache.first_row != first_row ||
			g_visible_syntax_cache.row_count != row_count) {
		g_visible_syntax_cache.prepared = 1;
		g_visible_syntax_cache.state = E.syntax_state;
		g_visible_syntax_cache.language = E.syntax_language;
		g_visible_syntax_cache.revision = E.syntax_revision;
		g_visible_syntax_cache.generation = E.syntax_generation;
		if (!cache_identity_matches ||
				!editorSyntaxVisibleCacheSlide(first_row, row_count)) {
			g_visible_syntax_cache.first_row = first_row;
			g_visible_syntax_cache.row_count = row_count;
			editorSyntaxVisibleCacheMarkRowsDirty(0, row_count);
		}
	}

	for (int rel_row = 0; rel_row < row_count; rel_row++) {
		if (!g_visible_syntax_cache.row_dirty[rel_row]) {
			continue;
		}

		int row_idx = first_row + rel_row;
		struct erow *row = &E.rows[row_idx];
		int span_base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
		g_visible_syntax_cache.span_counts[rel_row] = 0;

		size_t row_start_offset = 0;
		size_t row_end_offset = 0;
		if (!editorBufferLineByteRange(row_idx, &row_start_offset, &row_end_offset)) {
			return 0;
		}
		uint32_t start_byte = 0;
		uint32_t end_byte = 0;
		if (!editorSyntaxOffsetToU32(row_start_offset, &start_byte) ||
				!editorSyntaxOffsetToU32(row_end_offset, &end_byte) ||
				start_byte >= end_byte) {
			g_visible_syntax_cache.row_dirty[rel_row] = 0;
			continue;
		}

		int capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW * 3;
		if (capture_limit < ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
			capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
		}
		size_t cap_size = 0;
		size_t cap_bytes = 0;
		if (!editorIntToSize(capture_limit, &cap_size) ||
				!editorSizeMul(sizeof(struct editorSyntaxCapture), cap_size, &cap_bytes)) {
			return 0;
		}

		struct editorSyntaxCapture *captures = editorMalloc(cap_bytes);
		if (captures == NULL) {
			return 0;
		}

		int capture_count = 0;
		if (!editorSyntaxStateCollectCapturesForRange(E.syntax_state, &source, start_byte, end_byte,
					captures, capture_limit, &capture_count)) {
			free(captures);
			return 0;
		}

		for (int cap_idx = 0; cap_idx < capture_count; cap_idx++) {
			struct editorSyntaxCapture capture = captures[cap_idx];
			if (capture.highlight_class == EDITOR_SYNTAX_HL_NONE ||
					capture.end_byte <= capture.start_byte) {
				continue;
			}

			int slot = g_visible_syntax_cache.span_counts[rel_row];
			if (slot >= ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
				editorSyntaxStateRecordCaptureTruncated(E.syntax_state, row_idx);
				continue;
			}

			int local_start = (int)(capture.start_byte - start_byte);
			int local_end = (int)(capture.end_byte - start_byte);
			if (local_start < 0) {
				local_start = 0;
			}
			if (local_start > row->size) {
				local_start = row->size;
			}
			if (local_end < 0) {
				local_end = 0;
			}
			if (local_end > row->size) {
				local_end = row->size;
			}

			local_start = editorRowClampCxToCharBoundary(row, local_start);
			local_end = editorRowClampCxToCharBoundary(row, local_end);
			if (local_end <= local_start && local_end < row->size) {
				local_end = editorRowNextCharIdx(row, local_end);
			}
			if (local_end <= local_start) {
				continue;
			}

			int render_start = editorRowCxToRenderIdx(row, local_start);
			int render_end = editorRowCxToRenderIdx(row, local_end);
			if (render_end <= render_start) {
				continue;
			}

			g_visible_syntax_cache.spans[span_base + slot].start_render_idx = render_start;
			g_visible_syntax_cache.spans[span_base + slot].end_render_idx = render_end;
			g_visible_syntax_cache.spans[span_base + slot].highlight_class = capture.highlight_class;
			g_visible_syntax_cache.span_counts[rel_row] = slot + 1;
		}

		free(captures);
		g_visible_syntax_cache.row_dirty[rel_row] = 0;
		g_visible_syntax_row_recompute_count++;
	}

	editorSyntaxReportStatusIfNeeded();
	return 1;
}

static void editorClampCursorForRows(int target_cy, int target_cx,
		const struct erow *rows, int numrows, int *cy_out, int *cx_out) {
	int cy = target_cy;
	int cx = target_cx;

	if (cy < 0) {
		cy = 0;
	} else if (cy > numrows) {
		cy = numrows;
	}

	if (cy < numrows) {
		const struct erow *row = &rows[cy];
		if (cx < 0) {
			cx = 0;
		}
		if (cx > row->size) {
			cx = row->size;
		}
		cx = editorRowClampCxToClusterBoundary(row, cx);
		if (cx < 0) {
			cx = 0;
		}
		if (cx > row->size) {
			cx = row->size;
		}
	} else {
		cx = 0;
	}

	*cy_out = cy;
	*cx_out = cx;
}

int editorRestoreActiveFromDocument(const struct editorDocument *document,
		int target_cy, int target_cx, int dirty, int parse_syntax) {
	struct editorDocument *new_document = NULL;
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	int new_cy = 0;
	int new_cx = 0;
	size_t new_offset = 0;

	if (document == NULL) {
		return 0;
	}

	new_document = editorDocumentAlloc();
	if (new_document == NULL ||
			!editorDocumentResetFromDocument(new_document, document) ||
			!editorBuildFullRowsFromDocument(new_document, &new_rows, &new_numrows)) {
		editorFreeRowArray(new_rows, new_numrows);
		editorDocumentFreePtr(&new_document);
		editorSetAllocFailureStatus();
		return 0;
	}

	editorClampCursorForRows(target_cy, target_cx, new_rows, new_numrows, &new_cy, &new_cx);

	struct erow *old_rows = E.rows;
	int old_numrows = E.numrows;
	struct editorDocument *old_document = E.document;

	E.rows = new_rows;
	E.numrows = new_numrows;
	E.document = new_document;
	E.max_render_cols_valid = 0;
	E.cy = new_cy;
	E.cx = new_cx;
	E.dirty = dirty;
	editorClearSelectionState();
	editorSyntaxVisibleCacheInvalidate();
	if (!editorBufferPosToOffset(E.cy, E.cx, &new_offset)) {
		new_offset = 0;
	}
	E.cursor_offset = new_offset;
	editorDocumentStatsRecordFullRebuild();
	if (parse_syntax) {
		(void)editorSyntaxParseFullActive();
	}

	editorFreeRowArray(old_rows, old_numrows);
	editorDocumentFreePtr(&old_document);
	return 1;
}

int editorBufferMaxRenderCols(void) {
	if (E.max_render_cols_valid) {
		return E.max_render_cols;
	}

	int max_cols = 0;
	for (int i = 0; i < E.numrows; i++) {
		if (E.rows[i].render_display_cols > max_cols) {
			max_cols = E.rows[i].render_display_cols;
		}
	}
	E.max_render_cols = max_cols;
	E.max_render_cols_valid = 1;
	return max_cols;
}

int editorSyntaxEnabled(void) {
	if (editorSyntaxBackgroundEnabled()) {
		return E.syntax_language != EDITOR_SYNTAX_NONE;
	}
	return E.syntax_state != NULL && E.syntax_language != EDITOR_SYNTAX_NONE;
}

int editorSyntaxTreeExists(void) {
	if (E.syntax_state == NULL) {
		return 0;
	}
	return editorSyntaxStateHasTree(E.syntax_state);
}

enum editorSyntaxLanguage editorSyntaxLanguageActive(void) {
	return E.syntax_language;
}

const char *editorSyntaxRootType(void) {
	if (E.syntax_state == NULL) {
		return NULL;
	}
	return editorSyntaxStateRootType(E.syntax_state);
}

int editorSyntaxPrepareVisibleRowSpans(int first_row, int row_count) {
	if (editorSyntaxBackgroundEnabled()) {
		editorSyntaxBackgroundPoll();
		if (E.syntax_language == EDITOR_SYNTAX_NONE) {
			editorSyntaxVisibleCacheInvalidate();
			return 1;
		}
		if (!editorSyntaxNormalizeVisibleRows(&first_row, &row_count)) {
			return 0;
		}
		if (g_visible_syntax_cache.prepared &&
				g_visible_syntax_cache.state == E.syntax_state &&
				g_visible_syntax_cache.language == E.syntax_language &&
				g_visible_syntax_cache.revision == E.syntax_revision &&
				g_visible_syntax_cache.generation == E.syntax_generation &&
				editorSyntaxRowRangeCovers(g_visible_syntax_cache.first_row,
					g_visible_syntax_cache.row_count, first_row, row_count)) {
			return 1;
		}
		if (E.syntax_background_pending &&
				E.syntax_pending_revision == E.syntax_revision &&
				editorSyntaxRowRangeCovers(E.syntax_pending_first_row,
					E.syntax_pending_row_count, first_row, row_count)) {
			return 1;
		}
		return editorSyntaxScheduleBackgroundActive(first_row, row_count);
	}
	return editorSyntaxBuildVisibleSpanCache(first_row, row_count);
}

int editorSyntaxPrepareVisibleRowSpansForeground(int first_row, int row_count) {
	return editorSyntaxBuildVisibleSpanCache(first_row, row_count);
}

void editorSyntaxTestResetVisibleRowRecomputeCount(void) {
	g_visible_syntax_row_recompute_count = 0;
}

int editorSyntaxTestVisibleRowRecomputeCount(void) {
	return g_visible_syntax_row_recompute_count;
}

void editorDocumentTestResetStats(void) {
	editorDocumentStatsReset();
	editorRowCacheStatsReset();
}

int editorDocumentTestFullRebuildCount(void) {
	return editorDocumentStatsFullRebuildCount();
}

int editorDocumentTestIncrementalUpdateCount(void) {
	return editorDocumentStatsIncrementalUpdateCount();
}

int editorSyntaxRowRenderSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
		int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (row_idx < 0 || row_idx >= E.numrows || max_spans < 0 ||
			(max_spans > 0 && spans == NULL)) {
		return 0;
	}
	if (max_spans == 0 || E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE) {
		return 1;
	}

	if (editorSyntaxBackgroundEnabled()) {
		if (g_visible_syntax_cache.prepared &&
				g_visible_syntax_cache.state == E.syntax_state &&
				g_visible_syntax_cache.language == E.syntax_language &&
				g_visible_syntax_cache.revision == E.syntax_revision &&
				g_visible_syntax_cache.generation == E.syntax_generation &&
				row_idx >= g_visible_syntax_cache.first_row &&
				row_idx < g_visible_syntax_cache.first_row + g_visible_syntax_cache.row_count) {
			int rel_row = row_idx - g_visible_syntax_cache.first_row;
			int cached_count = g_visible_syntax_cache.span_counts[rel_row];
			if (cached_count > max_spans) {
				cached_count = max_spans;
			}
			if (cached_count > 0) {
				size_t count_size = 0;
				size_t copy_bytes = 0;
				if (!editorIntToSize(cached_count, &count_size) ||
						!editorSizeMul(sizeof(*spans), count_size, &copy_bytes)) {
					return 0;
				}
				int base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
				memcpy(spans, &g_visible_syntax_cache.spans[base], copy_bytes);
			}
			if (count_out != NULL) {
				*count_out = cached_count;
			}
		}
		return 1;
	}

	if (g_visible_syntax_cache.prepared &&
			g_visible_syntax_cache.state == E.syntax_state &&
			g_visible_syntax_cache.language == E.syntax_language &&
			g_visible_syntax_cache.revision == E.syntax_revision &&
			g_visible_syntax_cache.generation == E.syntax_generation &&
			row_idx >= g_visible_syntax_cache.first_row &&
			row_idx < g_visible_syntax_cache.first_row + g_visible_syntax_cache.row_count) {
		int rel_row = row_idx - g_visible_syntax_cache.first_row;
		int cached_count = g_visible_syntax_cache.span_counts[rel_row];
		if (cached_count > max_spans) {
			cached_count = max_spans;
		}
		if (cached_count > 0) {
			size_t count_size = 0;
			size_t copy_bytes = 0;
			if (!editorIntToSize(cached_count, &count_size) ||
					!editorSizeMul(sizeof(*spans), count_size, &copy_bytes)) {
				return 0;
			}
			int base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
			memcpy(spans, &g_visible_syntax_cache.spans[base], copy_bytes);
		}
		if (count_out != NULL) {
			*count_out = cached_count;
		}
		return 1;
	}

	size_t row_start_offset = 0;
	size_t row_end_offset = 0;
	if (!editorBufferLineByteRange(row_idx, &row_start_offset, &row_end_offset)) {
		return 0;
	}

	uint32_t start_byte = 0;
	uint32_t end_byte = 0;
	if (!editorSyntaxOffsetToU32(row_start_offset, &start_byte) ||
			!editorSyntaxOffsetToU32(row_end_offset, &end_byte) ||
			start_byte >= end_byte) {
		return 1;
	}
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	int capture_limit = max_spans;
	if (capture_limit > ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
		capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
	}

	struct editorSyntaxCapture captures[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int capture_count = 0;
	if (!editorSyntaxStateCollectCapturesForRange(E.syntax_state, &source, start_byte, end_byte,
				captures, capture_limit, &capture_count)) {
		return 0;
	}

	struct erow *row = &E.rows[row_idx];
	int out_count = 0;
	for (int i = 0; i < capture_count && out_count < max_spans; i++) {
		if (captures[i].highlight_class == EDITOR_SYNTAX_HL_NONE ||
				captures[i].end_byte <= captures[i].start_byte) {
			continue;
		}

		int local_start = (int)(captures[i].start_byte - start_byte);
		int local_end = (int)(captures[i].end_byte - start_byte);
		if (local_start < 0) {
			local_start = 0;
		}
		if (local_start > row->size) {
			local_start = row->size;
		}
		if (local_end < 0) {
			local_end = 0;
		}
		if (local_end > row->size) {
			local_end = row->size;
		}

		local_start = editorRowClampCxToCharBoundary(row, local_start);
		local_end = editorRowClampCxToCharBoundary(row, local_end);
		if (local_end <= local_start && local_end < row->size) {
			local_end = editorRowNextCharIdx(row, local_end);
		}
		if (local_end <= local_start) {
			continue;
		}

		int render_start = editorRowCxToRenderIdx(row, local_start);
		int render_end = editorRowCxToRenderIdx(row, local_end);
		if (render_end <= render_start) {
			continue;
		}

		spans[out_count].start_render_idx = render_start;
		spans[out_count].end_render_idx = render_end;
		spans[out_count].highlight_class = captures[i].highlight_class;
		out_count++;
	}

	if (count_out != NULL) {
		*count_out = out_count;
	}
	return 1;
}
