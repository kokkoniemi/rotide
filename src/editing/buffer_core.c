#include "editing/buffer_core.h"

#include "editing/document_bridge.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/row_cache.h"
#include "editing/selection.h"
#include "editing/syntax_runtime.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
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
static void editorSyntaxDisableWithStatus(const char *message);
int editorSyntaxParseFullActive(void);
static void editorSyntaxReportStatusIfNeeded(void);
static void editorSyntaxReportBudgetStatusIfNeeded(void);
static void editorSyntaxReportQueryUnavailableStatusIfNeeded(void);
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

void editorSyntaxRuntimeReportStatusIfNeeded(void) {
	editorSyntaxReportStatusIfNeeded();
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
		return editorSyntaxVisibleCacheScheduleBackground(E.rowoff, E.window_rows);
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
		return editorSyntaxVisibleCacheScheduleBackground(E.rowoff, E.window_rows);
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
	/* Approximation: byte count of the longest line, derived in O(1) from the
	 * tree summary. The render layer treats tabs/wide glyphs conservatively;
	 * for horizontal scrolling clamps this is plenty.
	 */
	size_t max_bytes = editorDocumentMaxLineBytes(E.document);
	return max_bytes > (size_t)INT_MAX ? INT_MAX : (int)max_bytes;
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

void editorDocumentTestResetStats(void) {
	editorDocumentStatsReset();
	editorTextTreeStatsReset();
	editorRowCacheStatsReset();
}

int editorDocumentTestFullRebuildCount(void) {
	return editorDocumentStatsFullRebuildCount();
}

int editorDocumentTestIncrementalUpdateCount(void) {
	return editorDocumentStatsIncrementalUpdateCount();
}

int editorTextTreeTestFullRebuildCount(void) {
	return editorTextTreeStatsFullRebuildCount();
}

int editorTextTreeTestIncrementalUpdateCount(void) {
	return editorTextTreeStatsIncrementalUpdateCount();
}
