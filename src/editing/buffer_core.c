#include "editing/buffer_core.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/save_syscalls.h"
#include "support/terminal.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/tabs.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct editorRowCacheSpliceRegion;
static const char *editorDocumentTextSourceRead(const struct editorTextSource *source,
		size_t byte_index, uint32_t *bytes_read);
static int editorSyntaxByteRangeToVisibleRows(size_t start_byte, size_t end_byte,
		int *start_row_out, int *end_row_exclusive_out);
static int editorSyntaxVisibleCacheInvalidateChangedRowsFromState(void);
static void editorSyntaxVisibleCacheInvalidateRowsForEdit(const struct editorSyntaxEdit *edit);
static void editorSyntaxDisableWithStatus(const char *message);
int editorSyntaxParseFullActive(void);
void editorSyntaxVisibleCacheInvalidate(void);
static void editorSyntaxVisibleCacheInvalidateRows(int start_row, int end_row_exclusive);
static void editorSyntaxReportBudgetStatusIfNeeded(void);
static int editorLspActiveBufferTracked(void);
char *editorDupActiveTextSource(size_t *len_out);
static void editorLspNotifyDidChangeActive(const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_len);
void editorLspNotifyDidSaveActive(void);
void editorLspNotifyDidCloseTabState(struct editorTabState *tab);
int editorTabKindSupportsDocument(enum editorTabKind tab_kind);
static int editorDocumentStateResetFromText(struct editorDocument **document_in_out,
		enum editorTabKind tab_kind, const char *text, size_t len);
int editorDocumentEnsureActiveCurrent(void);
int editorDocumentResetActiveFromText(const char *text, size_t len);
int editorTabDocumentEnsureCurrent(struct editorTabState *tab);
static int editorActiveDocumentCurrent(const struct editorDocument **document_out);
static int editorBuildRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out);
static int editorBuildRowsFromDocumentRange(const struct editorDocument *document,
		int start_row, int end_row_exclusive, struct erow **rows_out, int *numrows_out);
int editorBuildFullRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out);
static int editorApplySignedByteDelta(size_t value, size_t old_total, size_t new_total,
		size_t *out);
static int editorPrepareRowCacheSpliceRegion(const struct editorDocument *document,
		size_t start_offset, size_t old_len, struct editorRowCacheSpliceRegion *region_out);
static int editorRowCacheSpliceEndRowForDocument(const struct editorDocument *document,
		const struct editorRowCacheSpliceRegion *region, int *end_row_exclusive_out);
static int editorSpliceRowCache(struct erow *replacement_rows, int replacement_numrows,
		int start_row, int old_end_row_exclusive);
int editorSyncCursorFromOffset(size_t target_offset);
static int editorCursorPositionForOffset(const struct editorDocument *document,
		const struct erow *rows, int numrows, size_t offset, int *cy_out, int *cx_out,
		size_t *normalized_offset_out);
static int editorBuildSyntaxEditForDocumentEdit(const struct editorDocument *document,
		size_t start_offset, size_t old_len, const char *new_text, size_t new_len,
		struct editorSyntaxEdit *edit_out);
int editorApplyDocumentEdit(const struct editorDocumentEdit *edit);
static int editorTextSourceFindForwardInRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, const char *query, int from_idx, int *out_idx);
static int editorTextSourceFindBackwardInRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, const char *query, int before_idx, int *out_idx);

static int g_document_full_rebuild_count = 0;
static int g_document_incremental_update_count = 0;
static int g_active_text_source_build_count = 0;
static int g_active_text_source_dup_count = 0;
static int g_row_cache_full_rebuild_count = 0;
static int g_row_cache_splice_update_count = 0;

struct editorRowCacheSpliceRegion {
	int start_row;
	int old_end_row_exclusive;
	size_t prefix_start;
	size_t suffix_start_old;
	size_t old_total;
};

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

static void editorSyntaxDeactivateActive(void) {
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	editorSyntaxVisibleCacheInvalidate();
}

static void editorSyntaxDisableWithStatus(const char *message) {
	editorSyntaxDeactivateActive();
	if (message != NULL && message[0] != '\0') {
		editorSetStatusMsg("%s", message);
	}
}

static int editorSyntaxOffsetToU32(size_t offset, uint32_t *out) {
	if (out == NULL || offset > UINT32_MAX) {
		return 0;
	}
	*out = (uint32_t)offset;
	return 1;
}

static int editorSyntaxPointFromPosition(int cy, int cx, struct editorSyntaxPoint *out) {
	if (out == NULL || cy < 0 || cx < 0) {
		return 0;
	}
	out->row = (uint32_t)cy;
	out->column = (uint32_t)cx;
	return 1;
}

int editorBuildActiveTextSource(struct editorTextSource *source_out) {
	if (source_out == NULL) {
		return 0;
	}
	g_active_text_source_build_count++;
	if (!editorTabKindSupportsDocument(E.tab_kind) ||
			!editorDocumentEnsureActiveCurrent() || E.document == NULL) {
		return 0;
	}
	source_out->read = editorDocumentTextSourceRead;
	source_out->context = E.document;
	source_out->length = editorDocumentLength(E.document);
	return 1;
}

char *editorDupActiveTextSource(size_t *len_out) {
	struct editorTextSource source = {0};
	g_active_text_source_dup_count++;
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (!editorBuildActiveTextSource(&source)) {
		if (errno == 0) {
			errno = EIO;
		}
		return NULL;
	}

	if (source.length > ROTIDE_MAX_TEXT_BYTES) {
		errno = EOVERFLOW;
		return NULL;
	}

	size_t cap = 0;
	if (!editorSizeAdd(source.length, 1, &cap)) {
		errno = EOVERFLOW;
		return NULL;
	}

	char *dup = editorMalloc(cap);
	if (dup == NULL) {
		errno = ENOMEM;
		if (len_out != NULL) {
			*len_out = source.length;
		}
		return NULL;
	}
	if (source.length > 0 &&
			!editorTextSourceCopyRange(&source, 0, source.length, dup)) {
		free(dup);
		errno = EIO;
		return NULL;
	}
	dup[source.length] = '\0';
	if (len_out != NULL) {
		*len_out = source.length;
	}
	errno = 0;
	return dup;
}

static const char *editorDocumentTextSourceRead(const struct editorTextSource *source,
		size_t byte_index, uint32_t *bytes_read) {
	const struct editorDocument *document = source != NULL ? source->context : NULL;
	return editorDocumentRead(document, byte_index, bytes_read);
}

int editorTabKindSupportsDocument(enum editorTabKind tab_kind) {
	return tab_kind == EDITOR_TAB_FILE || tab_kind == EDITOR_TAB_TASK_LOG;
}

static struct editorDocument *editorDocumentAlloc(void) {
	struct editorDocument *document = editorMalloc(sizeof(*document));
	if (document == NULL) {
		return NULL;
	}
	editorDocumentInit(document);
	return document;
}

void editorDocumentFreePtr(struct editorDocument **document_in_out) {
	if (document_in_out == NULL || *document_in_out == NULL) {
		return;
	}
	editorDocumentFree(*document_in_out);
	free(*document_in_out);
	*document_in_out = NULL;
}

static int editorDocumentStateResetFromText(struct editorDocument **document_in_out,
		enum editorTabKind tab_kind, const char *text, size_t len) {
	if (document_in_out == NULL) {
		return 0;
	}
	if (!editorTabKindSupportsDocument(tab_kind)) {
		editorDocumentFreePtr(document_in_out);
		return 1;
	}
	if (*document_in_out == NULL) {
		*document_in_out = editorDocumentAlloc();
		if (*document_in_out == NULL) {
			return 0;
		}
	}
	if (!editorDocumentResetFromString(*document_in_out, text, len)) {
		return 0;
	}
	g_document_full_rebuild_count++;
	return 1;
}

static int editorDocumentEnsureForTab(enum editorTabKind tab_kind,
		struct editorDocument **document_in_out) {
	if (document_in_out == NULL) {
		return 0;
	}
	if (!editorTabKindSupportsDocument(tab_kind)) {
		editorDocumentFreePtr(document_in_out);
		return 1;
	}
	if (*document_in_out != NULL) {
		return 1;
	}
	return editorDocumentStateResetFromText(document_in_out, tab_kind, "", 0);
}

int editorDocumentResetActiveFromText(const char *text, size_t len) {
	return editorDocumentStateResetFromText(&E.document, E.tab_kind, text, len);
}

int editorDocumentEnsureActiveCurrent(void) {
	return editorDocumentEnsureForTab(E.tab_kind, &E.document) && E.document != NULL;
}

int editorTabDocumentEnsureCurrent(struct editorTabState *tab) {
	if (tab == NULL) {
		return 0;
	}
	return editorDocumentEnsureForTab(tab->tab_kind, &tab->document) && tab->document != NULL;
}

static int editorActiveDocumentCurrent(const struct editorDocument **document_out) {
	if (document_out == NULL || !editorTabKindSupportsDocument(E.tab_kind) ||
			!editorDocumentEnsureActiveCurrent() || E.document == NULL) {
		return 0;
	}
	*document_out = E.document;
	return 1;
}

static int editorSyntaxReconfigureForFilename(void) {
	const char *first_line = NULL;
	if (E.numrows > 0 && E.rows != NULL) {
		first_line = E.rows[0].chars;
	}

	enum editorSyntaxLanguage wanted =
			editorSyntaxDetectLanguageFromFilenameAndFirstLine(E.filename, first_line);
	if (wanted == EDITOR_SYNTAX_NONE) {
		editorSyntaxDeactivateActive();
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
	return 1;
}

int editorSyntaxParseFullActive(void) {
	if (!editorSyntaxReconfigureForFilename()) {
		return 0;
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
		editorSyntaxDisableWithStatus("Tree-sitter disabled (parse failed)");
		return 0;
	}
	editorSyntaxReportBudgetStatusIfNeeded();
	editorSyntaxVisibleCacheInvalidate();
	return 1;
}

static int editorSyntaxApplyIncrementalEditActive(const struct editorSyntaxEdit *edit,
		const char *inserted_text,
		size_t inserted_len) {
	if (E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE) {
		return 1;
	}

	if (inserted_len > 0 && inserted_text == NULL) {
		return 0;
	}

	if (edit != NULL && editorSyntaxStateHasTree(E.syntax_state)) {
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
						editorSyntaxVisibleCacheInvalidateRowsForEdit(edit);
						if (!editorSyntaxVisibleCacheInvalidateChangedRowsFromState()) {
							editorSyntaxVisibleCacheInvalidate();
						}
						editorSyntaxReportBudgetStatusIfNeeded();
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
		editorSyntaxDisableWithStatus("Tree-sitter disabled (parse failed)");
		return 0;
	}
	editorSyntaxReportBudgetStatusIfNeeded();
	editorSyntaxVisibleCacheInvalidate();
	return 1;
}

static int editorLspActiveBufferTracked(void) {
	return E.lsp_enabled &&
			E.filename != NULL &&
			E.filename[0] != '\0' &&
			E.syntax_language == EDITOR_SYNTAX_GO;
}

static void editorLspNotifyDidChangeActive(const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_len) {
	if (!editorLspActiveBufferTracked()) {
		return;
	}

	char *full_text = NULL;
	size_t full_text_len = 0;
	if (!E.lsp_doc_open) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			free(full_text);
			return;
		}
	}

	(void)editorLspNotifyDidChange(E.filename, E.syntax_language,
			&E.lsp_doc_open, &E.lsp_doc_version, edit, inserted_text, inserted_len,
			full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
}

void editorLspNotifyDidSaveActive(void) {
	if (!editorLspActiveBufferTracked()) {
		return;
	}

	char *full_text = NULL;
	size_t full_text_len = 0;
	if (!E.lsp_doc_open) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			free(full_text);
			return;
		}
		(void)editorLspEnsureDocumentOpen(E.filename, E.syntax_language,
				&E.lsp_doc_open, &E.lsp_doc_version,
				full_text != NULL ? full_text : "", full_text_len);
	}
	free(full_text);
	(void)editorLspNotifyDidSave(E.filename, E.syntax_language,
			&E.lsp_doc_open, &E.lsp_doc_version);
}

void editorLspNotifyDidCloseTabState(struct editorTabState *tab) {
	if (tab == NULL) {
		return;
	}
	editorLspNotifyDidClose(tab->filename, tab->syntax_language,
			&tab->lsp_doc_open, &tab->lsp_doc_version);
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

void editorFreeRowArray(struct erow *rows, int numrows);

struct editorVisibleSyntaxCache {
	int prepared;
	int first_row;
	int row_count;
	int row_capacity;
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

int editorBufferPosToOffset(int cy, int cx, size_t *offset_out) {
	if (cy < 0 || cy > E.numrows || cx < 0 || offset_out == NULL) {
		return 0;
	}
	const struct editorDocument *document = NULL;
	if (!editorActiveDocumentCurrent(&document)) {
		return 0;
	}
	size_t offset = 0;
	size_t column = 0;
	if (!editorIntToSize(cx, &column) ||
			!editorDocumentPositionToByteOffset(document, cy, column, &offset) ||
			offset > ROTIDE_MAX_TEXT_BYTES) {
		return 0;
	}
	*offset_out = offset;
	return 1;
}

int editorBufferOffsetToPos(size_t offset, int *cy_out, int *cx_out) {
	if (cy_out == NULL || cx_out == NULL) {
		return 0;
	}

	const struct editorDocument *document = NULL;
	if (!editorActiveDocumentCurrent(&document)) {
		return 0;
	}
	int line_idx = 0;
	size_t column = 0;
	if (!editorDocumentByteOffsetToPosition(document, offset, &line_idx, &column)) {
		return 0;
	}
	int cx = 0;
	if (!editorSizeToInt(column, &cx)) {
		return 0;
	}
	*cy_out = line_idx;
	*cx_out = cx;
	return 1;
}

int editorBufferLineByteRange(int row_idx, size_t *start_byte_out, size_t *end_byte_out) {
	if (start_byte_out == NULL || end_byte_out == NULL || row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	const struct editorDocument *document = NULL;
	if (!editorActiveDocumentCurrent(&document)) {
		return 0;
	}
	return editorDocumentLineStartByte(document, row_idx, start_byte_out) &&
			editorDocumentLineEndByte(document, row_idx, end_byte_out);
}

static int editorCursorPositionForOffset(const struct editorDocument *document,
		const struct erow *rows, int numrows, size_t offset, int *cy_out, int *cx_out,
		size_t *normalized_offset_out) {
	size_t document_len = 0;
	int cy = 0;
	size_t column = 0;
	int cx = 0;

	if (document == NULL || cy_out == NULL || cx_out == NULL || normalized_offset_out == NULL ||
			numrows < 0 || (numrows > 0 && rows == NULL)) {
		return 0;
	}

	document_len = editorDocumentLength(document);
	if (offset > document_len) {
		offset = document_len;
	}
	if (!editorDocumentByteOffsetToPosition(document, offset, &cy, &column)) {
		return 0;
	}
	if (!editorSizeToInt(column, &cx)) {
		return 0;
	}

	if (cy < 0) {
		cy = 0;
	}
	if (cy > numrows) {
		cy = numrows;
	}

	if (cy < numrows) {
		size_t line_start = 0;
		size_t cx_size = 0;
		if (cx < 0) {
			cx = 0;
		}
		if (cx > rows[cy].size) {
			cx = rows[cy].size;
		}
		cx = editorRowClampCxToClusterBoundary(&rows[cy], cx);
		if (cx < 0) {
			cx = 0;
		}
		if (cx > rows[cy].size) {
			cx = rows[cy].size;
		}
		if (!editorDocumentLineStartByte(document, cy, &line_start) ||
				!editorIntToSize(cx, &cx_size) ||
				!editorSizeAdd(line_start, cx_size, &offset)) {
			return 0;
		}
	} else {
		cx = 0;
		offset = document_len;
	}

	*cy_out = cy;
	*cx_out = cx;
	*normalized_offset_out = offset;
	return 1;
}

static int editorAdvancePositionByText(int start_row, size_t start_col, const char *text,
		size_t len, int *row_out, size_t *col_out) {
	int row = start_row;
	size_t col = start_col;

	if (row_out == NULL || col_out == NULL || start_row < 0 || (len > 0 && text == NULL)) {
		return 0;
	}

	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			if (row == INT_MAX) {
				return 0;
			}
			row++;
			col = 0;
			continue;
		}
		if (!editorSizeAdd(col, 1, &col)) {
			return 0;
		}
	}

	*row_out = row;
	*col_out = col;
	return 1;
}

static int editorBuildSyntaxEditForDocumentEdit(const struct editorDocument *document,
		size_t start_offset, size_t old_len, const char *new_text, size_t new_len,
		struct editorSyntaxEdit *edit_out) {
	size_t old_end_offset = 0;
	size_t new_end_offset = 0;
	int start_row = 0;
	int old_end_row = 0;
	int new_end_row = 0;
	size_t start_col = 0;
	size_t old_end_col = 0;
	size_t new_end_col = 0;
	int start_col_int = 0;
	int old_end_col_int = 0;
	int new_end_col_int = 0;

	if (document == NULL || edit_out == NULL || (new_len > 0 && new_text == NULL) ||
			!editorSizeAdd(start_offset, old_len, &old_end_offset) ||
			!editorSizeAdd(start_offset, new_len, &new_end_offset) ||
			old_end_offset > editorDocumentLength(document)) {
		return 0;
	}

	if (!editorDocumentByteOffsetToPosition(document, start_offset, &start_row, &start_col) ||
			!editorDocumentByteOffsetToPosition(document, old_end_offset, &old_end_row, &old_end_col) ||
			!editorAdvancePositionByText(start_row, start_col,
					new_len > 0 ? new_text : "", new_len, &new_end_row, &new_end_col) ||
			!editorSizeToInt(start_col, &start_col_int) ||
			!editorSizeToInt(old_end_col, &old_end_col_int) ||
			!editorSizeToInt(new_end_col, &new_end_col_int) ||
			!editorSyntaxOffsetToU32(start_offset, &edit_out->start_byte) ||
			!editorSyntaxOffsetToU32(old_end_offset, &edit_out->old_end_byte) ||
			!editorSyntaxOffsetToU32(new_end_offset, &edit_out->new_end_byte) ||
			!editorSyntaxPointFromPosition(start_row, start_col_int, &edit_out->start_point) ||
			!editorSyntaxPointFromPosition(old_end_row, old_end_col_int, &edit_out->old_end_point) ||
			!editorSyntaxPointFromPosition(new_end_row, new_end_col_int, &edit_out->new_end_point)) {
		return 0;
	}

	return 1;
}

int editorApplyDocumentEdit(const struct editorDocumentEdit *edit) {
	const struct editorDocument *active_document = NULL;
	size_t old_end_offset = 0;
	struct editorSyntaxEdit syntax_edit = {0};
	int syntax_track = 0;
	char *removed_text = NULL;
	struct editorRowCacheSpliceRegion row_region = {0};
	struct erow *replacement_rows = NULL;
	int replacement_numrows = 0;
	int replacement_end_row_exclusive = 0;

	if (edit == NULL || (edit->new_len > 0 && edit->new_text == NULL)) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (!editorActiveDocumentCurrent(&active_document) || active_document == NULL || E.document == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorSizeAdd(edit->start_offset, edit->old_len, &old_end_offset) ||
			old_end_offset > editorDocumentLength(active_document)) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (edit->old_len > 0) {
		removed_text = editorDocumentDupRange(active_document, edit->start_offset, old_end_offset, NULL);
		if (removed_text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
	}
	if (!editorPrepareRowCacheSpliceRegion(active_document, edit->start_offset, edit->old_len,
				&row_region)) {
		free(removed_text);
		editorSetOperationTooLargeStatus();
		return 0;
	}

	if (E.syntax_state != NULL && E.syntax_language != EDITOR_SYNTAX_NONE) {
		syntax_track = editorBuildSyntaxEditForDocumentEdit(active_document,
				edit->start_offset, edit->old_len,
				edit->new_len > 0 ? edit->new_text : "", edit->new_len, &syntax_edit);
	}

	if (!editorDocumentReplaceRange(E.document, edit->start_offset, edit->old_len,
				edit->new_len > 0 ? edit->new_text : "", edit->new_len) ||
			!editorRowCacheSpliceEndRowForDocument(E.document, &row_region,
					&replacement_end_row_exclusive) ||
			!editorBuildRowsFromDocumentRange(E.document, row_region.start_row,
					replacement_end_row_exclusive, &replacement_rows, &replacement_numrows) ||
			!editorSpliceRowCache(replacement_rows, replacement_numrows, row_region.start_row,
					row_region.old_end_row_exclusive)) {
		editorFreeRowArray(replacement_rows, replacement_numrows);
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}

	E.max_render_cols_valid = 0;
	if (!editorSyncCursorFromOffset(edit->after_cursor_offset)) {
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}
	E.dirty = edit->after_dirty;
	if (E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE) {
		editorSyntaxVisibleCacheInvalidate();
	}

	g_document_incremental_update_count++;
	if (E.edit_pending_mode == EDITOR_EDIT_PENDING_CAPTURED &&
			E.edit_pending_kind != EDITOR_EDIT_NONE) {
		if (!editorHistoryRecordPendingEditFromOperation(E.edit_pending_kind, edit,
					removed_text, edit->old_len)) {
			E.edit_pending_mode = EDITOR_EDIT_PENDING_SKIPPED;
			E.edit_group_kind = EDITOR_EDIT_NONE;
		}
	}
	const char *inserted_text = edit->new_len > 0 ? edit->new_text : "";
	if (syntax_track) {
		(void)editorSyntaxApplyIncrementalEditActive(&syntax_edit, inserted_text, edit->new_len);
	} else {
		(void)editorSyntaxApplyIncrementalEditActive(NULL, inserted_text, edit->new_len);
	}
	editorLspNotifyDidChangeActive(syntax_track ? &syntax_edit : NULL, inserted_text, edit->new_len);
	free(removed_text);
	return 1;
}

static int editorTextSourceFindForwardInRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, const char *query, int from_idx, int *out_idx) {
	if (source == NULL || query == NULL || out_idx == NULL || from_idx < 0 ||
			end_byte < start_byte) {
		return 0;
	}

	size_t line_len = end_byte - start_byte;
	size_t from = 0;
	if (!editorIntToSize(from_idx, &from) || from > line_len) {
		return 0;
	}

	size_t text_len = 0;
	char *text = editorTextSourceDupRange(source, start_byte, end_byte, &text_len);
	if (text == NULL) {
		return 0;
	}

	const char *match = strstr(text + from, query);
	if (match == NULL) {
		free(text);
		return 0;
	}

	*out_idx = (int)(match - text);
	free(text);
	return 1;
}

static int editorTextSourceFindBackwardInRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, const char *query, int before_idx, int *out_idx) {
	if (source == NULL || query == NULL || out_idx == NULL || end_byte < start_byte) {
		return 0;
	}

	size_t line_len = end_byte - start_byte;
	size_t before = 0;
	if (before_idx < 0) {
		before = 0;
	} else if (!editorIntToSize(before_idx, &before)) {
		return 0;
	}
	if (before > line_len) {
		before = line_len;
	}

	size_t text_len = 0;
	char *text = editorTextSourceDupRange(source, start_byte, end_byte, &text_len);
	if (text == NULL) {
		return 0;
	}

	int last = -1;
	const char *scan = text;
	while (1) {
		const char *match = strstr(scan, query);
		if (match == NULL) {
			break;
		}

		int idx = (int)(match - text);
		if (idx >= (int)before) {
			break;
		}
		last = idx;
		scan = match + 1;
	}

	free(text);
	if (last == -1) {
		return 0;
	}
	*out_idx = last;
	return 1;
}

int editorBufferFindForward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col) {
	if (query == NULL || out_row == NULL || out_col == NULL || E.numrows == 0) {
		return 0;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	if (start_row < 0 || start_row >= E.numrows) {
		start_row = 0;
		start_col = -1;
	}

	size_t line_start = 0;
	size_t line_end = 0;
	int col = 0;
	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
			editorTextSourceFindForwardInRange(&source, line_start, line_end, query,
					start_col + 1, &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	for (int offset = 1; offset < E.numrows; offset++) {
		int row = (start_row + offset) % E.numrows;
		if (editorBufferLineByteRange(row, &line_start, &line_end) &&
				editorTextSourceFindForwardInRange(&source, line_start, line_end, query, 0, &col)) {
			*out_row = row;
			*out_col = col;
			return 1;
		}
	}

	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
			editorTextSourceFindForwardInRange(&source, line_start, line_end, query, 0, &col) &&
			col <= start_col) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	return 0;
}

int editorBufferFindBackward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col) {
	if (query == NULL || out_row == NULL || out_col == NULL || E.numrows == 0) {
		return 0;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	if (start_row < 0 || start_row >= E.numrows) {
		start_row = E.numrows - 1;
		start_col = E.rows[start_row].size;
	}

	size_t line_start = 0;
	size_t line_end = 0;
	int col = 0;
	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
			editorTextSourceFindBackwardInRange(&source, line_start, line_end, query,
					start_col, &col)) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	for (int offset = 1; offset < E.numrows; offset++) {
		int row = (start_row - offset + E.numrows) % E.numrows;
		if (editorBufferLineByteRange(row, &line_start, &line_end) &&
				editorTextSourceFindBackwardInRange(&source, line_start, line_end, query,
						E.rows[row].size + 1, &col)) {
			*out_row = row;
			*out_col = col;
			return 1;
		}
	}

	if (editorBufferLineByteRange(start_row, &line_start, &line_end) &&
			editorTextSourceFindBackwardInRange(&source, line_start, line_end, query,
					(int)(line_end - line_start + 1), &col) &&
			col > start_col) {
		*out_row = start_row;
		*out_col = col;
		return 1;
	}

	return 0;
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

	if (!g_visible_syntax_cache.prepared ||
			g_visible_syntax_cache.first_row != first_row ||
			g_visible_syntax_cache.row_count != row_count) {
		memset(g_visible_syntax_cache.span_counts, 0, sizeof(*g_visible_syntax_cache.span_counts) *
				(size_t)row_count);
		memset(g_visible_syntax_cache.row_dirty, 1, sizeof(*g_visible_syntax_cache.row_dirty) *
				(size_t)row_count);
		g_visible_syntax_cache.prepared = 1;
		g_visible_syntax_cache.first_row = first_row;
		g_visible_syntax_cache.row_count = row_count;
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

	editorSyntaxReportBudgetStatusIfNeeded();
	return 1;
}

void editorFreeRowArray(struct erow *rows, int numrows) {
	for (int i = 0; i < numrows; i++) {
		free(rows[i].chars);
		free(rows[i].render);
	}
	free(rows);
}

static int editorAppendRestoredRow(struct erow **rows, int *numrows, const char *s, size_t len) {
	int row_size = 0;
	size_t row_cap = 0;
	size_t numrows_size = 0;
	size_t new_numrows = 0;
	size_t row_bytes = 0;

	if (!editorSizeToInt(len, &row_size) ||
			!editorSizeAdd(len, 1, &row_cap) ||
			!editorIntToSize(*numrows, &numrows_size) ||
			!editorSizeAdd(numrows_size, 1, &new_numrows) ||
			!editorSizeMul(sizeof(struct erow), new_numrows, &row_bytes)) {
		return 0;
	}

	char *row_chars = editorMalloc(row_cap);
	if (row_chars == NULL) {
		return 0;
	}
	memcpy(row_chars, s, len);
	row_chars[len] = '\0';

	char *row_render = NULL;
	int row_rsize = 0;
	int row_display_cols = 0;
	if (!editorRowBuildRender(row_chars, row_size, &row_render, &row_rsize,
				&row_display_cols)) {
		free(row_chars);
		return 0;
	}

	struct erow *new_rows = editorRealloc(*rows, row_bytes);
	if (new_rows == NULL) {
		free(row_render);
		free(row_chars);
		return 0;
	}

	*rows = new_rows;
	(*rows)[*numrows].size = row_size;
	(*rows)[*numrows].rsize = row_rsize;
	(*rows)[*numrows].render_display_cols = row_display_cols;
	(*rows)[*numrows].chars = row_chars;
	(*rows)[*numrows].render = row_render;
	(*numrows)++;
	return 1;
}

static int editorBuildRowsFromDocumentRange(const struct editorDocument *document,
		int start_row, int end_row_exclusive, struct erow **rows_out, int *numrows_out) {
	struct erow *rows = NULL;
	int numrows = 0;

	if (document == NULL || rows_out == NULL || numrows_out == NULL ||
			start_row < 0 || end_row_exclusive < start_row) {
		return 0;
	}

	int line_count = editorDocumentLineCount(document);
	if (end_row_exclusive > line_count) {
		return 0;
	}
	for (int line_idx = start_row; line_idx < end_row_exclusive; line_idx++) {
		size_t line_start = 0;
		size_t line_end = 0;
		if (!editorDocumentLineStartByte(document, line_idx, &line_start) ||
				!editorDocumentLineEndByte(document, line_idx, &line_end)) {
			editorFreeRowArray(rows, numrows);
			return 0;
		}

		size_t line_len = line_end - line_start;
		char *line_text = NULL;
		if (line_len > 0) {
			line_text = editorDocumentDupRange(document, line_start, line_end, NULL);
			if (line_text == NULL) {
				editorFreeRowArray(rows, numrows);
				return 0;
			}
		}

		if (!editorAppendRestoredRow(&rows, &numrows,
					line_text != NULL ? line_text : "", line_len)) {
			free(line_text);
			editorFreeRowArray(rows, numrows);
			return 0;
		}
		free(line_text);
	}

	*rows_out = rows;
	*numrows_out = numrows;
	return 1;
}

static int editorBuildRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out) {
	if (document == NULL) {
		return 0;
	}
	return editorBuildRowsFromDocumentRange(document, 0, editorDocumentLineCount(document),
			rows_out, numrows_out);
}

int editorBuildFullRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out) {
	if (!editorBuildRowsFromDocument(document, rows_out, numrows_out)) {
		return 0;
	}
	g_row_cache_full_rebuild_count++;
	return 1;
}

static int editorApplySignedByteDelta(size_t value, size_t old_total, size_t new_total,
		size_t *out) {
	if (out == NULL) {
		return 0;
	}
	if (new_total >= old_total) {
		return editorSizeAdd(value, new_total - old_total, out);
	}
	size_t delta = old_total - new_total;
	if (value < delta) {
		return 0;
	}
	*out = value - delta;
	return 1;
}

static int editorPrepareRowCacheSpliceRegion(const struct editorDocument *document,
		size_t start_offset, size_t old_len, struct editorRowCacheSpliceRegion *region_out) {
	size_t old_total = 0;
	size_t first_lookup = 0;
	size_t last_lookup = 0;
	size_t old_end_offset = 0;
	int start_row = 0;
	int end_row = 0;

	if (document == NULL || region_out == NULL) {
		return 0;
	}

	old_total = editorDocumentLength(document);
	if (start_offset > old_total || old_len > old_total - start_offset) {
		return 0;
	}
	old_end_offset = start_offset + old_len;

	if (old_total == 0) {
		*region_out = (struct editorRowCacheSpliceRegion) {
			.start_row = 0,
			.old_end_row_exclusive = 0,
			.prefix_start = 0,
			.suffix_start_old = 0,
			.old_total = 0
		};
		return 1;
	}

	first_lookup = start_offset;
	if (first_lookup == old_total) {
		first_lookup = old_total - 1;
	}
	last_lookup = old_len > 0 ? start_offset + old_len - 1 : first_lookup;

	if (!editorDocumentLineIndexForByteOffset(document, first_lookup, &start_row) ||
			!editorDocumentLineIndexForByteOffset(document, last_lookup, &end_row) ||
			!editorDocumentLineStartByte(document, start_row, &region_out->prefix_start)) {
		return 0;
	}
	if (old_len > 0 && old_end_offset < old_total) {
		int boundary_row = 0;
		size_t boundary_start = 0;
		if (!editorDocumentLineIndexForByteOffset(document, old_end_offset, &boundary_row) ||
				!editorDocumentLineStartByte(document, boundary_row, &boundary_start)) {
			return 0;
		}
		if (boundary_start == old_end_offset && boundary_row > end_row) {
			end_row = boundary_row;
		}
	}

	region_out->start_row = start_row;
	region_out->old_end_row_exclusive = end_row + 1;
	region_out->old_total = old_total;
	if (region_out->old_end_row_exclusive < editorDocumentLineCount(document) &&
			!editorDocumentLineStartByte(document, region_out->old_end_row_exclusive,
					&region_out->suffix_start_old)) {
		return 0;
	}
	if (region_out->old_end_row_exclusive >= editorDocumentLineCount(document)) {
		region_out->suffix_start_old = old_total;
	}
	return 1;
}

static int editorRowCacheSpliceEndRowForDocument(const struct editorDocument *document,
		const struct editorRowCacheSpliceRegion *region, int *end_row_exclusive_out) {
	size_t new_total = 0;
	size_t new_suffix_start = 0;
	size_t last_lookup = 0;
	int last_row = 0;

	if (document == NULL || region == NULL || end_row_exclusive_out == NULL) {
		return 0;
	}

	new_total = editorDocumentLength(document);
	if (new_total == 0) {
		*end_row_exclusive_out = 0;
		return 1;
	}
	if (!editorApplySignedByteDelta(region->suffix_start_old, region->old_total, new_total,
				&new_suffix_start) || new_suffix_start > new_total) {
		return 0;
	}

	if (new_suffix_start > region->prefix_start) {
		last_lookup = new_suffix_start - 1;
	} else if (region->prefix_start < new_total) {
		last_lookup = region->prefix_start;
	} else {
		last_lookup = new_total - 1;
	}
	if (!editorDocumentLineIndexForByteOffset(document, last_lookup, &last_row)) {
		return 0;
	}
	*end_row_exclusive_out = last_row + 1;
	if (*end_row_exclusive_out < region->start_row) {
		*end_row_exclusive_out = region->start_row;
	}
	return 1;
}

static int editorSpliceRowCache(struct erow *replacement_rows, int replacement_numrows,
		int start_row, int old_end_row_exclusive) {
	int remove_count = 0;
	int tail_count = 0;
	int new_numrows = 0;
	struct erow *grown = NULL;

	if (start_row < 0 || old_end_row_exclusive < start_row ||
			old_end_row_exclusive > E.numrows || replacement_numrows < 0 ||
			(replacement_numrows > 0 && replacement_rows == NULL)) {
		return 0;
	}

	remove_count = old_end_row_exclusive - start_row;
	tail_count = E.numrows - old_end_row_exclusive;
	if (start_row > INT_MAX - replacement_numrows ||
			start_row + replacement_numrows > INT_MAX - tail_count) {
		return 0;
	}
	new_numrows = start_row + replacement_numrows + tail_count;

	if (new_numrows > E.numrows) {
		size_t row_count_size = 0;
		size_t row_bytes = 0;
		if (!editorIntToSize(new_numrows, &row_count_size) ||
				!editorSizeMul(sizeof(*E.rows), row_count_size, &row_bytes)) {
			return 0;
		}
		grown = editorRealloc(E.rows, row_bytes);
		if (grown == NULL) {
			return 0;
		}
		E.rows = grown;
	}

	for (int i = start_row; i < old_end_row_exclusive; i++) {
		free(E.rows[i].chars);
		free(E.rows[i].render);
		E.rows[i].chars = NULL;
		E.rows[i].render = NULL;
	}

	if (tail_count > 0 && replacement_numrows != remove_count) {
		memmove(&E.rows[start_row + replacement_numrows], &E.rows[old_end_row_exclusive],
				sizeof(*E.rows) * (size_t)tail_count);
	}
	for (int i = 0; i < replacement_numrows; i++) {
		E.rows[start_row + i] = replacement_rows[i];
	}

	if (new_numrows == 0) {
		free(E.rows);
		E.rows = NULL;
	} else if (new_numrows < E.numrows) {
		size_t row_count_size = 0;
		size_t row_bytes = 0;
		if (!editorIntToSize(new_numrows, &row_count_size) ||
				!editorSizeMul(sizeof(*E.rows), row_count_size, &row_bytes)) {
			return 0;
		}
		grown = editorRealloc(E.rows, row_bytes);
		if (grown != NULL) {
			E.rows = grown;
		}
	}

	E.numrows = new_numrows;
	g_row_cache_splice_update_count++;
	free(replacement_rows);
	return 1;
}

int editorSyncCursorFromOffset(size_t target_offset) {
	size_t normalized_offset = 0;
	int new_cy = 0;
	int new_cx = 0;

	if (E.document == NULL ||
			!editorCursorPositionForOffset(E.document, E.rows, E.numrows, target_offset,
					&new_cy, &new_cx, &normalized_offset)) {
		return 0;
	}

	E.cursor_offset = normalized_offset;
	E.cy = new_cy;
	E.cx = new_cx;
	return 1;
}

int editorSyncCursorFromOffsetByteBoundary(size_t target_offset) {
	size_t document_len = 0;
	size_t normalized_offset = 0;
	int new_cy = 0;
	size_t column = 0;
	int new_cx = 0;

	if (E.document == NULL) {
		return 0;
	}

	document_len = editorDocumentLength(E.document);
	if (target_offset > document_len) {
		target_offset = document_len;
	}
	if (!editorDocumentByteOffsetToPosition(E.document, target_offset, &new_cy, &column) ||
			!editorSizeToInt(column, &new_cx)) {
		return 0;
	}

	if (new_cy < 0) {
		new_cy = 0;
	}
	if (new_cy > E.numrows) {
		new_cy = E.numrows;
	}

	if (new_cy < E.numrows) {
		if (new_cx < 0) {
			new_cx = 0;
		}
		if (new_cx > E.rows[new_cy].size) {
			new_cx = E.rows[new_cy].size;
		}
		new_cx = editorRowClampCxToCharBoundary(&E.rows[new_cy], new_cx);
		if (new_cx < 0) {
			new_cx = 0;
		}
		if (new_cx > E.rows[new_cy].size) {
			new_cx = E.rows[new_cy].size;
		}

		size_t line_start = 0;
		size_t cx_size = 0;
		if (!editorDocumentLineStartByte(E.document, new_cy, &line_start) ||
				!editorIntToSize(new_cx, &cx_size) ||
				!editorSizeAdd(line_start, cx_size, &normalized_offset)) {
			return 0;
		}
	} else {
		new_cx = 0;
		normalized_offset = document_len;
	}

	E.cursor_offset = normalized_offset;
	E.cy = new_cy;
	E.cx = new_cx;
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
	g_document_full_rebuild_count++;
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
	return editorSyntaxBuildVisibleSpanCache(first_row, row_count);
}

void editorSyntaxTestResetVisibleRowRecomputeCount(void) {
	g_visible_syntax_row_recompute_count = 0;
}

int editorSyntaxTestVisibleRowRecomputeCount(void) {
	return g_visible_syntax_row_recompute_count;
}

void editorDocumentTestResetStats(void) {
	g_document_full_rebuild_count = 0;
	g_document_incremental_update_count = 0;
	g_row_cache_full_rebuild_count = 0;
	g_row_cache_splice_update_count = 0;
}

int editorDocumentTestFullRebuildCount(void) {
	return g_document_full_rebuild_count;
}

int editorDocumentTestIncrementalUpdateCount(void) {
	return g_document_incremental_update_count;
}

int editorRowCacheTestFullRebuildCount(void) {
	return g_row_cache_full_rebuild_count;
}

int editorRowCacheTestSpliceUpdateCount(void) {
	return g_row_cache_splice_update_count;
}

void editorActiveTextSourceBuildTestResetCount(void) {
	g_active_text_source_build_count = 0;
}

int editorActiveTextSourceBuildTestCount(void) {
	return g_active_text_source_build_count;
}

void editorActiveTextSourceDupTestResetCount(void) {
	g_active_text_source_dup_count = 0;
}

int editorActiveTextSourceDupTestCount(void) {
	return g_active_text_source_dup_count;
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

	if (g_visible_syntax_cache.prepared &&
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
