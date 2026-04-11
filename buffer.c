#include "buffer.h"

#include "alloc.h"
#include "document.h"
#include "input.h"
#include "lsp.h"
#include "output.h"
#include "save_syscalls.h"
#include "size_utils.h"
#include "syntax.h"
#include "terminal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

/*** File io ***/

#define NEWLINE_CHAR_WIDTH 1

static char *editorPathJoin(const char *left, const char *right);
struct editorDocumentEdit;
static const char *editorDocumentTextSourceRead(const struct editorTextSource *source,
		size_t byte_index, uint32_t *bytes_read);
static int editorSyntaxByteRangeToVisibleRows(size_t start_byte, size_t end_byte,
		int *start_row_out, int *end_row_exclusive_out);
static int editorSyntaxVisibleCacheInvalidateChangedRowsFromState(void);
static void editorSyntaxVisibleCacheInvalidateRowsForEdit(const struct editorSyntaxEdit *edit);
static void editorSyntaxDisableWithStatus(const char *message);
static void editorSyntaxVisibleCacheInvalidate(void);
static void editorSyntaxVisibleCacheInvalidateRows(int start_row, int end_row_exclusive);
static void editorSyntaxReportBudgetStatusIfNeeded(void);
static int editorTabFindOpenFileIndex(const char *path);
static int editorLspActiveBufferTracked(void);
static char *editorDupActiveTextSource(size_t *len_out);
static void editorLspNotifyDidChangeActive(const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_len);
static void editorLspNotifyDidSaveActive(void);
static void editorLspNotifyDidCloseTabState(struct editorTabState *tab);
static int editorRebuildGeneratedTabRows(struct editorTabState *tab);
static void editorTaskSetFinalStatus(int success);
static void editorTaskResetState(void);
static int editorTabKindSupportsDocument(enum editorTabKind tab_kind);
static int editorDocumentStateResetFromText(struct editorDocument **document_in_out,
		enum editorTabKind tab_kind, const char *text, size_t len);
static int editorDocumentMirrorEnsureActiveCurrent(void);
static int editorDocumentMirrorResetActiveFromText(const char *text, size_t len);
static int editorTabDocumentEnsureCurrent(struct editorTabState *tab);
static int editorActiveDocumentCurrent(const struct editorDocument **document_out);
static int editorBuildRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out);
static int editorCursorPositionForOffset(const struct editorDocument *document,
		const struct erow *rows, int numrows, size_t offset, int *cy_out, int *cx_out,
		size_t *normalized_offset_out);
static int editorHistoryDupSlice(const char *text, size_t len, char **dst_out);
static void editorHistoryEntryFree(struct editorHistoryEntry *entry);
static int editorBuildSyntaxEditForDocumentEdit(const struct editorDocument *document,
		size_t start_offset, size_t old_len, const char *new_text, size_t new_len,
		struct editorSyntaxEdit *edit_out);
static int editorApplyDocumentEdit(const struct editorDocumentEdit *edit);
static int editorReadNormalizedFileToText(FILE *fp, char **text_out, size_t *len_out);
static int editorTextSourceFindForwardInRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, const char *query, int from_idx, int *out_idx);
static int editorTextSourceFindBackwardInRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, const char *query, int before_idx, int *out_idx);

static int g_document_mirror_full_rebuild_count = 0;
static int g_document_mirror_incremental_update_count = 0;
static int g_document_mirror_row_source_rebuild_count = 0;
static int g_active_text_source_build_count = 0;
static int g_active_text_source_dup_count = 0;
static int g_snapshot_capture_document_clone_count = 0;
static int g_snapshot_capture_text_source_build_count = 0;

struct editorDocumentEdit {
	enum editorEditKind kind;
	size_t start_offset;
	size_t old_len;
	const char *new_text;
	size_t new_len;
	size_t before_cursor_offset;
	size_t after_cursor_offset;
	int before_dirty;
	int after_dirty;
};

static void editorSetAllocFailureStatus(void) {
	editorSetStatusMsg("Out of memory");
}

static void editorSetOperationTooLargeStatus(void) {
	editorSetStatusMsg("Operation too large");
}

static void editorSetFileTooLargeStatus(void) {
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
			!editorDocumentMirrorEnsureActiveCurrent() || E.document == NULL) {
		return 0;
	}
	source_out->read = editorDocumentTextSourceRead;
	source_out->context = E.document;
	source_out->length = editorDocumentLength(E.document);
	return 1;
}

static char *editorDupActiveTextSource(size_t *len_out) {
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

static int editorTabKindSupportsDocument(enum editorTabKind tab_kind) {
	return tab_kind == EDITOR_TAB_FILE || tab_kind == EDITOR_TAB_TASK_LOG;
}

static struct editorDocument *editorDocumentMirrorAlloc(void) {
	struct editorDocument *document = editorMalloc(sizeof(*document));
	if (document == NULL) {
		return NULL;
	}
	editorDocumentInit(document);
	return document;
}

static void editorDocumentMirrorFree(struct editorDocument **document_in_out) {
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
		editorDocumentMirrorFree(document_in_out);
		return 1;
	}
	if (*document_in_out == NULL) {
		*document_in_out = editorDocumentMirrorAlloc();
		if (*document_in_out == NULL) {
			return 0;
		}
	}
	if (!editorDocumentResetFromString(*document_in_out, text, len)) {
		return 0;
	}
	g_document_mirror_full_rebuild_count++;
	return 1;
}

static int editorDocumentEnsureForTab(enum editorTabKind tab_kind,
		struct editorDocument **document_in_out) {
	if (document_in_out == NULL) {
		return 0;
	}
	if (!editorTabKindSupportsDocument(tab_kind)) {
		editorDocumentMirrorFree(document_in_out);
		return 1;
	}
	if (*document_in_out != NULL) {
		return 1;
	}
	return editorDocumentStateResetFromText(document_in_out, tab_kind, "", 0);
}

static int editorDocumentMirrorResetActiveFromText(const char *text, size_t len) {
	return editorDocumentStateResetFromText(&E.document, E.tab_kind, text, len);
}

static int editorDocumentMirrorEnsureActiveCurrent(void) {
	return editorDocumentEnsureForTab(E.tab_kind, &E.document) && E.document != NULL;
}

static int editorTabDocumentEnsureCurrent(struct editorTabState *tab) {
	if (tab == NULL) {
		return 0;
	}
	return editorDocumentEnsureForTab(tab->tab_kind, &tab->document) && tab->document != NULL;
}

static int editorReadNormalizedFileToText(FILE *fp, char **text_out, size_t *len_out) {
	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len = 0;
	char *text = NULL;
	size_t text_len = 0;

	if (text_out == NULL || len_out == NULL || fp == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		size_t normalized_len = 0;
		size_t row_total = 0;
		size_t next_total = 0;
		size_t next_cap = 0;
		char *grown = NULL;

		if (!editorSsizeToSize(line_len, &normalized_len)) {
			editorSetFileTooLargeStatus();
			break;
		}
		while (normalized_len > 0 &&
				(line[normalized_len - 1] == '\n' || line[normalized_len - 1] == '\r')) {
			normalized_len--;
		}

		if (!editorSizeAdd(normalized_len, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(text_len, row_total, &next_total) ||
				next_total > ROTIDE_MAX_TEXT_BYTES ||
				!editorSizeAdd(next_total, 1, &next_cap)) {
			editorSetFileTooLargeStatus();
			break;
		}

		grown = editorRealloc(text, next_cap);
		if (grown == NULL) {
			free(text);
			free(line);
			editorSetAllocFailureStatus();
			return 0;
		}
		text = grown;
		if (normalized_len > 0) {
			memcpy(text + text_len, line, normalized_len);
		}
		text[text_len + normalized_len] = '\n';
		text[next_total] = '\0';
		text_len = next_total;
	}

	free(line);
	if (text == NULL) {
		text = editorMalloc(1);
		if (text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
		text[0] = '\0';
	}

	*text_out = text;
	*len_out = text_len;
	return 1;
}

static int editorActiveDocumentCurrent(const struct editorDocument **document_out) {
	if (document_out == NULL || !editorTabKindSupportsDocument(E.tab_kind) ||
			!editorDocumentMirrorEnsureActiveCurrent() || E.document == NULL) {
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

static int editorSyntaxParseFullActive(void) {
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

static void editorLspNotifyDidSaveActive(void) {
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

static void editorLspNotifyDidCloseTabState(struct editorTabState *tab) {
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

int editorIsUtf8ContinuationByte(unsigned char c) {
	return (c & 0xC0) == 0x80;
}

int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp) {
	if (len <= 0) {
		*cp = 0;
		return 0;
	}

	unsigned char b0 = (unsigned char)s[0];
	if (b0 < 0x80) {
		*cp = b0;
		return 1;
	}

	int expected_len = 0;
	unsigned int codepoint = 0;
	unsigned int min_codepoint = 0;

	// Determine UTF-8 sequence length and initial payload bits from leading byte.
	if ((b0 & 0xE0) == 0xC0) {
		expected_len = 2;
		codepoint = b0 & 0x1F;
		min_codepoint = 0x80;
	} else if ((b0 & 0xF0) == 0xE0) {
		expected_len = 3;
		codepoint = b0 & 0x0F;
		min_codepoint = 0x800;
	} else if ((b0 & 0xF8) == 0xF0) {
		expected_len = 4;
		codepoint = b0 & 0x07;
		min_codepoint = 0x10000;
	} else {
		*cp = b0;
		return 1;
	}

	// If sequence is truncated or malformed, treat first byte as standalone.
	if (len < expected_len) {
		*cp = b0;
		return 1;
	}

	for (int i = 1; i < expected_len; i++) {
		unsigned char bx = (unsigned char)s[i];
		if (!editorIsUtf8ContinuationByte(bx)) {
			*cp = b0;
			return 1;
		}
			codepoint = (codepoint << 6) | (unsigned int)(bx & 0x3F);
	}

	// Reject overlong forms, surrogate range, and out-of-range codepoints.
	if (codepoint < min_codepoint || codepoint > 0x10FFFF ||
			(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
		*cp = b0;
		return 1;
	}

	*cp = codepoint;
	return expected_len;
}

int editorIsRegionalIndicatorCodepoint(unsigned int cp) {
	return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

int editorIsGraphemeExtendCodepoint(unsigned int cp) {
	// Unicode combining mark blocks that should stay in the same grapheme.
	if ((cp >= 0x0300 && cp <= 0x036F) ||
			(cp >= 0x1AB0 && cp <= 0x1AFF) ||
			(cp >= 0x1DC0 && cp <= 0x1DFF) ||
			(cp >= 0x20D0 && cp <= 0x20FF) ||
			(cp >= 0xFE20 && cp <= 0xFE2F)) {
		return 1;
	}
	// Variation selectors modify the previous glyph and should not split clusters.
	if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
			(cp >= 0xE0100 && cp <= 0xE01EF)) {
		return 1;
	}
	// Emoji skin-tone modifiers are attached to the previous emoji.
	if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
		return 1;
	}
	// Keep ZWNJ with the current cluster for cursor stepping consistency.
	if (cp == 0x200C) {
		return 1;
	}
	if (cp > (unsigned int)WCHAR_MAX) {
		return 0;
	}
	// Fallback: many libc locales report combining marks with width 0.
	wchar_t wc = (wchar_t)cp;
	int width = wcwidth(wc);
	return width == 0 && cp != 0x200D;
}

int editorCharDisplayWidth(const char *s, int len) {
	unsigned char c = s[0];
	if (c < 0x80) {
		return 1;
	}
	// Continuation bytes are part of a previous codepoint and should not
	// advance visual columns when scanning byte-by-byte.
	if (editorIsUtf8ContinuationByte(c)) {
		return 0;
	}

	mbstate_t ps = {0};
	wchar_t wc;
	size_t read = mbrtowc(&wc, s, len, &ps);
	if (read == (size_t)-1 || read == (size_t)-2) {
		return 1;
	}
	int width = wcwidth(wc);
	if (width < 0) {
		return 1;
	}
	return width;
}

int editorRowClampCxToCharBoundary(const struct erow *row, int cx) {
	if (cx < 0) {
		return 0;
	}
	if (cx > row->size) {
		cx = row->size;
	}
	while (cx > 0 && cx < row->size &&
			editorIsUtf8ContinuationByte((unsigned char)row->chars[cx])) {
		cx--;
	}
	return cx;
}

int editorRowPrevCharIdx(const struct erow *row, int idx) {
	if (idx <= 0) {
		return 0;
	}
	idx = editorRowClampCxToCharBoundary(row, idx);
	idx--;
	while (idx > 0 && editorIsUtf8ContinuationByte((unsigned char)row->chars[idx])) {
		idx--;
	}
	return idx;
}

int editorRowNextCharIdx(const struct erow *row, int idx) {
	if (idx >= row->size) {
		return row->size;
	}
	idx = editorRowClampCxToCharBoundary(row, idx);
	unsigned int cp = 0;
	int step = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &cp);
	if (step <= 0) {
		step = 1;
	}
	if (idx + step > row->size) {
		return row->size;
	}
	return idx + step;
}

int editorRowNextClusterIdx(const struct erow *row, int idx) {
	idx = editorRowClampCxToCharBoundary(row, idx);
	if (idx >= row->size) {
		return row->size;
	}

	unsigned int cp = 0;
	int cp_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &cp);
	if (cp_len <= 0) {
		cp_len = 1;
	}
	idx += cp_len;

	// Pair regional indicators into one cluster so flag emojis step as one unit.
	if (editorIsRegionalIndicatorCodepoint(cp)) {
		if (idx < row->size) {
			unsigned int next_cp = 0;
			int next_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &next_cp);
			if (next_len <= 0) {
				next_len = 1;
			}
			if (editorIsRegionalIndicatorCodepoint(next_cp)) {
				idx += next_len;
			}
		}
		return idx;
	}

	while (idx < row->size) {
		unsigned int next_cp = 0;
		int next_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &next_cp);
		if (next_len <= 0) {
			next_len = 1;
		}

		if (editorIsGraphemeExtendCodepoint(next_cp)) {
			idx += next_len;
			continue;
		}

		// Keep ZWJ-linked emoji sequences in a single grapheme cluster.
		if (next_cp == 0x200D) {
			int after_zwj = idx + next_len;
			idx = after_zwj;
			if (idx >= row->size) {
				return row->size;
			}

			int linked_len = editorUtf8DecodeCodepoint(
					&row->chars[idx], row->size - idx, &next_cp);
			if (linked_len <= 0) {
				linked_len = 1;
			}
			idx += linked_len;
			continue;
		}

		break;
	}

	return idx;
}

int editorRowPrevClusterIdx(const struct erow *row, int idx) {
	idx = editorRowClampCxToCharBoundary(row, idx);
	if (idx <= 0) {
		return 0;
	}

	int prev = 0;
	int scan = 0;
	while (scan < idx) {
		prev = scan;
		scan = editorRowNextClusterIdx(row, scan);
		if (scan <= prev) {
			return prev;
		}
	}

	return prev;
}

int editorRowClampCxToClusterBoundary(const struct erow *row, int cx) {
	cx = editorRowClampCxToCharBoundary(row, cx);
	if (cx <= 0) {
		return 0;
	}

	int boundary = 0;
	while (boundary < cx) {
		int next_boundary = editorRowNextClusterIdx(row, boundary);
		if (next_boundary > cx || next_boundary <= boundary) {
			break;
		}
		boundary = next_boundary;
	}

	return boundary;
}

static char editorHexUpperDigit(unsigned int value) {
	return value < 10 ? (char)('0' + value) : (char)('A' + (value - 10));
}

// Convert the next source token into render-space metadata.
// This is the single source of truth for:
// 1) bytes consumed from row->chars,
// 2) bytes produced in row->render,
// 3) display columns occupied on screen.
// Keeping these together ensures render-building, cursor math, and highlight
// mapping stay consistent when controls are escaped.
static int editorBuildRenderToken(const char *s, int len, int rx, int expand_tabs,
		char *render_out, int *src_len_out, int *render_len_out, int *width_out) {
	if (s == NULL || len <= 0) {
		return 0;
	}

	unsigned int cp = 0;
	int src_len = editorUtf8DecodeCodepoint(s, len, &cp);
	if (src_len <= 0) {
		// Invalid leading byte: consume one byte so callers always make progress.
		src_len = 1;
	}
	if (src_len > len) {
		src_len = len;
	}

	if (cp == '\t' && expand_tabs) {
		int spaces = ROTIDE_TAB_WIDTH - (rx % ROTIDE_TAB_WIDTH);
		if (spaces <= 0) {
			spaces = ROTIDE_TAB_WIDTH;
		}
		if (render_out != NULL) {
			for (int i = 0; i < spaces; i++) {
				render_out[i] = ' ';
			}
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = spaces;
		}
		if (width_out != NULL) {
			*width_out = spaces;
		}
		return 1;
	}

	if (cp <= 0x1F) {
		if (render_out != NULL) {
			render_out[0] = '^';
			render_out[1] = (char)('@' + (int)cp);
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = 2;
		}
		if (width_out != NULL) {
			*width_out = 2;
		}
		return 1;
	}

	if (cp == 0x7F) {
		if (render_out != NULL) {
			render_out[0] = '^';
			render_out[1] = '?';
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = 2;
		}
		if (width_out != NULL) {
			*width_out = 2;
		}
		return 1;
	}

	if (cp >= 0x80 && cp <= 0x9F) {
		// Render C1 controls as ASCII text so raw bytes never reach the terminal.
		if (render_out != NULL) {
			render_out[0] = '\\';
			render_out[1] = 'x';
			render_out[2] = editorHexUpperDigit((cp >> 4) & 0x0F);
			render_out[3] = editorHexUpperDigit(cp & 0x0F);
		}
		if (src_len_out != NULL) {
			*src_len_out = src_len;
		}
		if (render_len_out != NULL) {
			*render_len_out = 4;
		}
		if (width_out != NULL) {
			*width_out = 4;
		}
		return 1;
	}

	if (render_out != NULL) {
		memcpy(render_out, s, (size_t)src_len);
	}
	if (src_len_out != NULL) {
		*src_len_out = src_len;
	}
	if (render_len_out != NULL) {
		*render_len_out = src_len;
	}
	if (width_out != NULL) {
		*width_out = editorCharDisplayWidth(s, len);
	}
	return 1;
}

int editorRowCxToRx(const struct erow *row, int cx) {
	int rx = 0;
	cx = editorRowClampCxToClusterBoundary(row, cx);
	for (int idx = 0; idx < cx && idx < row->size;) {
		int src_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&row->chars[idx], row->size - idx, rx, 1, NULL,
				&src_len, NULL, &token_width)) {
			break;
		}
		if (src_len <= 0) {
			break;
		}
		rx += token_width;
		idx += src_len;
	}
	return rx;
}

int editorRowRxToCx(const struct erow *row, int rx) {
	if (rx <= 0) {
		return 0;
	}

	int cx = 0;
	int cur_rx = 0;
	while (cx < row->size) {
		int next_cx = editorRowNextClusterIdx(row, cx);
		if (next_cx <= cx) {
			break;
		}

		int cluster_width = 0;
		// Compute width for the whole grapheme cluster so cursor positions never
		// land inside a cluster even when escaped controls widen render output.
		for (int idx = cx; idx < next_cx;) {
			int src_len = 0;
			int token_width = 0;
			if (!editorBuildRenderToken(&row->chars[idx], row->size - idx, cur_rx + cluster_width,
					1, NULL, &src_len, NULL, &token_width)) {
				break;
			}
			if (src_len <= 0) {
				break;
			}
			cluster_width += token_width;
			idx += src_len;
		}

		if (cur_rx + cluster_width > rx) {
			return cx;
		}

		cur_rx += cluster_width;
		cx = next_cx;
	}

	return row->size;
}

int editorRowCxToRenderIdx(const struct erow *row, int cx) {
	int clamped_cx = editorRowClampCxToClusterBoundary(row, cx);
	int render_idx = 0;
	int rx = 0;
	// Map logical char-space boundaries to byte offsets in row->render using
	// the exact same tokenization as render construction and rx/cx conversion.
	for (int idx = 0; idx < clamped_cx && idx < row->size;) {
		int src_len = 0;
		int render_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&row->chars[idx], row->size - idx, rx, 1, NULL,
				&src_len, &render_len, &token_width)) {
			break;
		}
		if (src_len <= 0) {
			break;
		}
		render_idx += render_len;
		rx += token_width;
		idx += src_len;
	}
	if (render_idx > row->rsize) {
		render_idx = row->rsize;
	}
	return render_idx;
}

static int editorBuildRender(const char *chars, int size, char **render_out, int *rsize_out,
		int *display_cols_out) {
	size_t render_cap = 1;
	int rx = 0;
	// Capacity prepass mirrors the write pass token-for-token.
	for (int idx = 0; idx < size;) {
		int src_len = 0;
		int render_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&chars[idx], size - idx, rx, 1, NULL, &src_len,
				&render_len, &token_width)) {
			return 0;
		}
		if (src_len <= 0) {
			return 0;
		}
		size_t token_len = 0;
		if (!editorIntToSize(render_len, &token_len)) {
			return 0;
		}
		if (!editorSizeAdd(render_cap, token_len, &render_cap)) {
			return 0;
		}
		if (render_cap - 1 > ROTIDE_MAX_TEXT_BYTES) {
			return 0;
		}
		rx += token_width;
		idx += src_len;
	}

	char *render = editorMalloc(render_cap);
	if (render == NULL) {
		return 0;
	}

	size_t out_idx = 0;
	rx = 0;
	for (int idx = 0; idx < size;) {
		// Largest escaped token written here is a tab expansion (TAB_WIDTH spaces).
		char token[ROTIDE_TAB_WIDTH];
		int src_len = 0;
		int render_len = 0;
		int token_width = 0;
		if (!editorBuildRenderToken(&chars[idx], size - idx, rx, 1, token, &src_len,
				&render_len, &token_width)) {
			free(render);
			return 0;
		}
		size_t token_len = 0;
		if (!editorIntToSize(render_len, &token_len)) {
			free(render);
			return 0;
		}
		memcpy(&render[out_idx], token, token_len);
		out_idx += token_len;
		rx += token_width;
		idx += src_len;
	}
	render[out_idx] = '\0';

	int out_idx_int = 0;
	if (!editorSizeToInt(out_idx, &out_idx_int)) {
		free(render);
		return 0;
	}
	*render_out = render;
	*rsize_out = out_idx_int;
	if (display_cols_out != NULL) {
		*display_cols_out = rx;
	}
	return 1;
}

static void editorFreeRowArray(struct erow *rows, int numrows);

/*** Selection and clipboard ***/

static void editorClearSelectionState(void) {
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
}

static int editorPosComesBefore(int left_cy, int left_cx, int right_cy, int right_cx) {
	if (left_cy != right_cy) {
		return left_cy < right_cy;
	}
	return left_cx < right_cx;
}

static void editorClampPositionToBuffer(int *cy, int *cx) {
	if (*cy < 0) {
		*cy = 0;
	}
	if (*cy > E.numrows) {
		*cy = E.numrows;
	}

	if (*cy == E.numrows) {
		*cx = 0;
		return;
	}

	struct erow *row = &E.rows[*cy];
	if (*cx < 0) {
		*cx = 0;
	}
	if (*cx > row->size) {
		*cx = row->size;
	}
	*cx = editorRowClampCxToClusterBoundary(row, *cx);
	if (*cx > row->size) {
		*cx = row->size;
	}
}

static int editorNormalizeRange(const struct editorSelectionRange *range,
		struct editorSelectionRange *out) {
	if (range == NULL || out == NULL) {
		return 0;
	}

	int start_cy = range->start_cy;
	int start_cx = range->start_cx;
	int end_cy = range->end_cy;
	int end_cx = range->end_cx;
	editorClampPositionToBuffer(&start_cy, &start_cx);
	editorClampPositionToBuffer(&end_cy, &end_cx);

	if (editorPosComesBefore(end_cy, end_cx, start_cy, start_cx)) {
		int tmp_cy = start_cy;
		int tmp_cx = start_cx;
		start_cy = end_cy;
		start_cx = end_cx;
		end_cy = tmp_cy;
		end_cx = tmp_cx;
	}

	if (start_cy == end_cy && start_cx == end_cx) {
		return 0;
	}

	out->start_cy = start_cy;
	out->start_cx = start_cx;
	out->end_cy = end_cy;
	out->end_cx = end_cx;
	return 1;
}

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

static void editorSyntaxVisibleCacheInvalidate(void) {
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

static void editorSyntaxVisibleCacheFree(void) {
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

static int editorHistoryRecordPendingEditFromOperation(enum editorEditKind kind,
		const struct editorDocumentEdit *edit, const char *removed_text, size_t removed_len) {
	struct editorHistoryEntry entry = {0};
	if (edit == NULL) {
		return 0;
	}

	entry.kind = kind;
	entry.start_offset = edit->start_offset;
	entry.removed_len = removed_len;
	entry.inserted_len = edit->new_len;
	entry.before_cursor_offset = edit->before_cursor_offset;
	entry.after_cursor_offset = edit->after_cursor_offset;
	entry.before_dirty = edit->before_dirty;
	entry.after_dirty = edit->after_dirty;

	if (!editorHistoryDupSlice(removed_text != NULL ? removed_text : "", removed_len,
				&entry.removed_text) ||
			!editorHistoryDupSlice(edit->new_len > 0 ? edit->new_text : "", edit->new_len,
				&entry.inserted_text)) {
		editorHistoryEntryFree(&entry);
		return 0;
	}

	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry = entry;
	E.edit_pending_entry_valid = 1;
	return 1;
}

static int editorApplyDocumentEdit(const struct editorDocumentEdit *edit) {
	const struct editorDocument *active_document = NULL;
	size_t old_end_offset = 0;
	struct editorDocument *replacement_document = NULL;
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	size_t normalized_offset = 0;
	int new_cy = 0;
	int new_cx = 0;
	struct editorSyntaxEdit syntax_edit = {0};
	int syntax_track = 0;
	char *removed_text = NULL;

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

	if (E.syntax_state != NULL && E.syntax_language != EDITOR_SYNTAX_NONE) {
		syntax_track = editorBuildSyntaxEditForDocumentEdit(active_document,
				edit->start_offset, edit->old_len,
				edit->new_len > 0 ? edit->new_text : "", edit->new_len, &syntax_edit);
	}

	replacement_document = editorDocumentMirrorAlloc();
	if (replacement_document == NULL) {
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorDocumentResetFromDocument(replacement_document, active_document) ||
			!editorDocumentReplaceRange(replacement_document, edit->start_offset, edit->old_len,
					edit->new_len > 0 ? edit->new_text : "", edit->new_len) ||
			!editorBuildRowsFromDocument(replacement_document, &new_rows, &new_numrows) ||
				!editorCursorPositionForOffset(replacement_document, new_rows, new_numrows,
					edit->after_cursor_offset, &new_cy, &new_cx, &normalized_offset)) {
		editorFreeRowArray(new_rows, new_numrows);
		editorDocumentMirrorFree(&replacement_document);
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}

	struct erow *old_rows = E.rows;
	int old_numrows = E.numrows;
	struct editorDocument *old_document = E.document;

	E.rows = new_rows;
	E.numrows = new_numrows;
	E.document = replacement_document;
	E.max_render_cols_valid = 0;
	E.cy = new_cy;
	E.cx = new_cx;
	E.cursor_offset = normalized_offset;
	E.preferred_rx = 0;
	E.dirty = edit->after_dirty;
	if (E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE) {
		editorSyntaxVisibleCacheInvalidate();
	}

	editorFreeRowArray(old_rows, old_numrows);
	editorDocumentMirrorFree(&old_document);

	g_document_mirror_incremental_update_count++;
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

int editorGetSelectionRange(struct editorSelectionRange *range_out) {
	if (range_out == NULL || !E.selection_mode_active) {
		return 0;
	}

	int anchor_cy = 0;
	int anchor_cx = 0;
	if (!editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_cy, &anchor_cx)) {
		return 0;
	}

	struct editorSelectionRange range = {
		.start_cy = anchor_cy,
		.start_cx = anchor_cx,
		.end_cy = E.cy,
		.end_cx = E.cx
	};
	if (!editorNormalizeRange(&range, range_out)) {
		return 0;
	}

	return 1;
}

int editorExtractRangeText(const struct editorSelectionRange *range, char **text_out,
		size_t *len_out) {
	if (text_out == NULL || len_out == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	struct editorSelectionRange normalized;
	if (!editorNormalizeRange(range, &normalized)) {
		return 0;
	}

	errno = 0;
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		if (errno == EOVERFLOW) {
			editorSetOperationTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return -1;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	if (!editorBufferPosToOffset(normalized.start_cy, normalized.start_cx, &start_offset) ||
			!editorBufferPosToOffset(normalized.end_cy, normalized.end_cx, &end_offset) ||
			end_offset < start_offset || end_offset > source.length) {
		editorSetOperationTooLargeStatus();
		return -1;
	}
	size_t selected_len = end_offset - start_offset;
	if (selected_len == 0) {
		return 0;
	}

	char *selected = editorTextSourceDupRange(&source, start_offset, end_offset, len_out);
	if (selected == NULL) {
		editorSetAllocFailureStatus();
		return -1;
	}

	*text_out = selected;
	return 1;
}

int editorDeleteRange(const struct editorSelectionRange *range) {
	struct editorSelectionRange normalized;
	if (!editorNormalizeRange(range, &normalized)) {
		return 0;
	}

	size_t start_offset = 0;
	size_t end_offset = 0;
	if (!editorBufferPosToOffset(normalized.start_cy, normalized.start_cx, &start_offset) ||
			!editorBufferPosToOffset(normalized.end_cy, normalized.end_cx, &end_offset) ||
			end_offset < start_offset) {
		editorSetOperationTooLargeStatus();
		return -1;
	}
	size_t removed_len = end_offset - start_offset;
	if (removed_len == 0) {
		return 0;
	}
	size_t before_cursor_offset = start_offset;
	if (!editorBufferPosToOffset(E.cy, E.cx, &before_cursor_offset)) {
		before_cursor_offset = start_offset;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_DELETE_TEXT,
		.start_offset = start_offset,
		.old_len = removed_len,
		.new_text = "",
		.new_len = 0,
		.before_cursor_offset = before_cursor_offset,
		.after_cursor_offset = start_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + 1
	};
	if (!editorApplyDocumentEdit(&edit)) {
		return -1;
	}
	return 1;
}

int editorClipboardSet(const char *text, size_t len) {
	if (len > ROTIDE_MAX_TEXT_BYTES) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	char *new_text = NULL;
	if (len > 0) {
		if (text == NULL) {
			return 0;
		}
		size_t cap = 0;
		if (!editorSizeAdd(len, 1, &cap)) {
			editorSetOperationTooLargeStatus();
			return 0;
		}
		new_text = editorMalloc(cap);
		if (new_text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
		memcpy(new_text, text, len);
		new_text[len] = '\0';
	}

	free(E.clipboard_text);
	E.clipboard_text = new_text;
	E.clipboard_textlen = len;

	if (E.clipboard_external_sink != NULL) {
		const char *sink_text = E.clipboard_text;
		if (sink_text == NULL) {
			sink_text = "";
		}
		E.clipboard_external_sink(sink_text, E.clipboard_textlen);
	}

	return 1;
}

const char *editorClipboardGet(size_t *len_out) {
	if (len_out != NULL) {
		*len_out = E.clipboard_textlen;
	}
	if (E.clipboard_text != NULL) {
		return E.clipboard_text;
	}
	return "";
}

void editorClipboardClear(void) {
	free(E.clipboard_text);
	E.clipboard_text = NULL;
	E.clipboard_textlen = 0;
}

void editorClipboardSetExternalSink(editorClipboardExternalSink sink) {
	E.clipboard_external_sink = sink;
}

/*** History ***/

static void editorHistoryEntryFree(struct editorHistoryEntry *entry) {
	free(entry->removed_text);
	free(entry->inserted_text);
	memset(entry, 0, sizeof(*entry));
}

static void editorHistoryClear(struct editorHistory *history) {
	for (int i = 0; i < history->len; i++) {
		int idx = (history->start + i) % ROTIDE_UNDO_HISTORY_LIMIT;
		editorHistoryEntryFree(&history->entries[idx]);
	}
	history->start = 0;
	history->len = 0;
}

static void editorHistoryPushNewest(struct editorHistory *history,
		struct editorHistoryEntry *entry) {
	int slot = 0;
	if (history->len < ROTIDE_UNDO_HISTORY_LIMIT) {
		slot = (history->start + history->len) % ROTIDE_UNDO_HISTORY_LIMIT;
		history->len++;
	} else {
		slot = history->start;
		editorHistoryEntryFree(&history->entries[slot]);
		history->start = (history->start + 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	}

	history->entries[slot] = *entry;
	memset(entry, 0, sizeof(*entry));
}

static int editorHistoryPopNewest(struct editorHistory *history,
		struct editorHistoryEntry *entry) {
	if (history->len == 0) {
		return 0;
	}

	int idx = (history->start + history->len - 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	*entry = history->entries[idx];
	memset(&history->entries[idx], 0, sizeof(history->entries[idx]));
	history->len--;
	if (history->len == 0) {
		history->start = 0;
	}
	return 1;
}

static struct editorHistoryEntry *editorHistoryNewest(struct editorHistory *history) {
	if (history == NULL || history->len == 0) {
		return NULL;
	}
	int idx = (history->start + history->len - 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	return &history->entries[idx];
}

static int editorHistoryDupSlice(const char *text, size_t len, char **dst_out) {
	char *dup = NULL;

	if (dst_out == NULL) {
		return 0;
	}
	*dst_out = NULL;
	if (len == 0) {
		return 1;
	}

	size_t cap = 0;
	if (!editorSizeAdd(len, 1, &cap)) {
		return 0;
	}
	dup = editorMalloc(cap);
	if (dup == NULL) {
		return 0;
	}
	memcpy(dup, text, len);
	dup[len] = '\0';
	*dst_out = dup;
	return 1;
}

static int editorHistoryAppendText(char **text_in_out, size_t *len_in_out,
		const char *append, size_t append_len) {
	size_t old_len = 0;
	size_t new_len = 0;
	size_t cap = 0;
	char *grown = NULL;

	if (text_in_out == NULL || len_in_out == NULL) {
		return 0;
	}
	old_len = *len_in_out;
	if (!editorSizeAdd(old_len, append_len, &new_len) ||
			!editorSizeAdd(new_len, 1, &cap)) {
		return 0;
	}
	grown = editorRealloc(*text_in_out, cap);
	if (grown == NULL) {
		return 0;
	}
	if (append_len > 0 && append != NULL) {
		memcpy(grown + old_len, append, append_len);
	}
	grown[new_len] = '\0';
	*text_in_out = grown;
	*len_in_out = new_len;
	return 1;
}

static int editorHistoryTryMergeInsert(struct editorHistory *history,
		const struct editorHistoryEntry *entry) {
	struct editorHistoryEntry *latest = editorHistoryNewest(history);
	int append_at_end = 0;
	int append_before_trailing_newline = 0;
	if (latest == NULL || entry == NULL) {
		return 0;
	}
	if (latest->kind != EDITOR_EDIT_INSERT_TEXT ||
			entry->kind != EDITOR_EDIT_INSERT_TEXT ||
			latest->removed_len != 0 ||
			entry->removed_len != 0 ||
			latest->after_cursor_offset != entry->before_cursor_offset) {
		return 0;
	}

	append_at_end = latest->start_offset + latest->inserted_len == entry->start_offset;
	append_before_trailing_newline = latest->inserted_len > 0 &&
			latest->inserted_text != NULL &&
			latest->inserted_text[latest->inserted_len - 1] == '\n' &&
			latest->start_offset + latest->inserted_len - 1 == entry->start_offset;
	if (!append_at_end && !append_before_trailing_newline) {
		return 0;
	}

	if (append_at_end) {
		if (!editorHistoryAppendText(&latest->inserted_text, &latest->inserted_len,
					entry->inserted_text, entry->inserted_len)) {
			return 0;
		}
	} else {
		size_t prefix_len = latest->inserted_len - 1;
		size_t merged_len = 0;
		size_t cap = 0;
		if (!editorSizeAdd(prefix_len, entry->inserted_len, &merged_len) ||
				!editorSizeAdd(merged_len, 1, &merged_len) ||
				!editorSizeAdd(merged_len, 1, &cap)) {
			return 0;
		}
		char *grown = editorRealloc(latest->inserted_text, cap);
		if (grown == NULL) {
			return 0;
		}
		latest->inserted_text = grown;
		memmove(latest->inserted_text + prefix_len + entry->inserted_len,
				latest->inserted_text + prefix_len, 2);
		if (entry->inserted_len > 0 && entry->inserted_text != NULL) {
			memcpy(latest->inserted_text + prefix_len, entry->inserted_text, entry->inserted_len);
		}
		latest->inserted_len = merged_len;
	}

	latest->after_cursor_offset = entry->after_cursor_offset;
	latest->after_dirty = entry->after_dirty;
	return 1;
}

static void editorFreeRowArray(struct erow *rows, int numrows) {
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
	if (!editorBuildRender(row_chars, row_size, &row_render, &row_rsize, &row_display_cols)) {
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

static int editorBuildRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out) {
	struct erow *rows = NULL;
	int numrows = 0;

	if (document == NULL || rows_out == NULL || numrows_out == NULL) {
		return 0;
	}

	int line_count = editorDocumentLineCount(document);
	for (int line_idx = 0; line_idx < line_count; line_idx++) {
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

static int editorRestoreActiveFromDocument(const struct editorDocument *document,
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

	new_document = editorDocumentMirrorAlloc();
	if (new_document == NULL ||
			!editorDocumentResetFromDocument(new_document, document) ||
			!editorBuildRowsFromDocument(new_document, &new_rows, &new_numrows)) {
		editorFreeRowArray(new_rows, new_numrows);
		editorDocumentMirrorFree(&new_document);
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
	E.preferred_rx = 0;
	g_document_mirror_full_rebuild_count++;
	if (parse_syntax) {
		(void)editorSyntaxParseFullActive();
	}

	editorFreeRowArray(old_rows, old_numrows);
	editorDocumentMirrorFree(&old_document);
	return 1;
}

static int editorApplyHistoryEntry(const struct editorHistoryEntry *entry, int inverse) {
	if (entry == NULL) {
		return -1;
	}

	struct editorDocumentEdit edit = {
		.kind = entry->kind,
		.start_offset = entry->start_offset,
		.old_len = inverse ? entry->inserted_len : entry->removed_len,
		.new_text = inverse ? entry->removed_text : entry->inserted_text,
		.new_len = inverse ? entry->removed_len : entry->inserted_len,
		.before_cursor_offset = inverse ? entry->after_cursor_offset : entry->before_cursor_offset,
		.after_cursor_offset = inverse ? entry->before_cursor_offset : entry->after_cursor_offset,
		.before_dirty = inverse ? entry->after_dirty : entry->before_dirty,
		.after_dirty = inverse ? entry->before_dirty : entry->after_dirty
	};
	return editorApplyDocumentEdit(&edit) ? 1 : -1;
}

static void editorTabStateInitEmpty(struct editorTabState *tab) {
	memset(tab, 0, sizeof(*tab));
	tab->tab_kind = EDITOR_TAB_FILE;
	tab->is_preview = 0;
	tab->document = NULL;
	tab->cursor_offset = 0;
	tab->preferred_rx = 0;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	tab->syntax_state = NULL;
	tab->lsp_doc_open = 0;
	tab->lsp_doc_version = 0;
	tab->max_render_cols = 0;
	tab->max_render_cols_valid = 1;
	tab->search_match_offset = 0;
	tab->search_match_len = 0;
	tab->search_direction = 1;
	tab->search_saved_offset = 0;
}

static void editorResetActiveBufferFields(void) {
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = 0;
	E.tab_title = NULL;
	E.cursor_offset = 0;
	E.preferred_rx = 0;
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.document = NULL;
	E.max_render_cols = 0;
	E.max_render_cols_valid = 1;
	E.dirty = 0;
	E.filename = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.syntax_state = NULL;
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	E.search_query = NULL;
	E.search_match_offset = 0;
	E.search_match_len = 0;
	E.search_direction = 1;
	E.search_saved_offset = 0;
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_anchor_offset = 0;
	E.mouse_drag_started = 0;
	E.undo_history.start = 0;
	E.undo_history.len = 0;
	E.redo_history.start = 0;
	E.redo_history.len = 0;
	memset(&E.edit_pending_entry, 0, sizeof(E.edit_pending_entry));
	E.edit_pending_entry_valid = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

static void editorFreeTabRows(struct editorTabState *tab) {
	for (int i = 0; i < tab->numrows; i++) {
		free(tab->rows[i].chars);
		free(tab->rows[i].render);
	}
	free(tab->rows);
	tab->rows = NULL;
	tab->numrows = 0;
}

static void editorTabStateFree(struct editorTabState *tab) {
	editorFreeTabRows(tab);
	editorDocumentMirrorFree(&tab->document);
	free(tab->filename);
	tab->filename = NULL;
	free(tab->tab_title);
	tab->tab_title = NULL;
	editorSyntaxStateDestroy(tab->syntax_state);
	tab->syntax_state = NULL;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	free(tab->search_query);
	tab->search_query = NULL;
	editorHistoryClear(&tab->undo_history);
	editorHistoryClear(&tab->redo_history);
	editorHistoryEntryFree(&tab->edit_pending_entry);
	tab->edit_pending_entry_valid = 0;
	editorTabStateInitEmpty(tab);
}

static void editorFreeActiveBufferState(void) {
	for (int i = 0; i < E.numrows; i++) {
		free(E.rows[i].chars);
		free(E.rows[i].render);
	}
	free(E.rows);
	E.rows = NULL;
	E.numrows = 0;
	editorDocumentMirrorFree(&E.document);
	E.max_render_cols = 0;
	E.max_render_cols_valid = 1;

	free(E.filename);
	E.filename = NULL;
	free(E.tab_title);
	E.tab_title = NULL;
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	free(E.search_query);
	E.search_query = NULL;
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	editorSyntaxVisibleCacheInvalidate();
	editorResetActiveBufferFields();
}

static void editorTabStateCaptureActive(struct editorTabState *tab) {
	editorTabStateFree(tab);

	tab->tab_kind = E.tab_kind;
	tab->is_preview = E.is_preview;
	tab->tab_title = E.tab_title;
	tab->cursor_offset = E.cursor_offset;
	tab->preferred_rx = E.preferred_rx;
	tab->cx = E.cx;
	tab->cy = E.cy;
	tab->rx = E.rx;
	tab->rowoff = E.rowoff;
	tab->coloff = E.coloff;
	tab->numrows = E.numrows;
	tab->rows = E.rows;
	tab->document = E.document;
	tab->max_render_cols = E.max_render_cols;
	tab->max_render_cols_valid = E.max_render_cols_valid;
	tab->dirty = E.dirty;
	tab->filename = E.filename;
	tab->syntax_language = E.syntax_language;
	tab->syntax_state = E.syntax_state;
	tab->lsp_doc_open = E.lsp_doc_open;
	tab->lsp_doc_version = E.lsp_doc_version;
	tab->search_query = E.search_query;
	tab->search_match_offset = E.search_match_offset;
	tab->search_match_len = E.search_match_len;
	tab->search_direction = E.search_direction;
	tab->search_saved_offset = E.search_saved_offset;
	tab->selection_mode_active = E.selection_mode_active;
	tab->selection_anchor_offset = E.selection_anchor_offset;
	tab->mouse_left_button_down = E.mouse_left_button_down;
	tab->mouse_drag_anchor_offset = E.mouse_drag_anchor_offset;
	tab->mouse_drag_started = E.mouse_drag_started;
	tab->undo_history = E.undo_history;
	tab->redo_history = E.redo_history;
	tab->edit_pending_entry = E.edit_pending_entry;
	tab->edit_pending_entry_valid = E.edit_pending_entry_valid;
	tab->edit_group_kind = E.edit_group_kind;
	tab->edit_pending_kind = E.edit_pending_kind;
	tab->edit_pending_mode = E.edit_pending_mode;

	editorResetActiveBufferFields();
}

static void editorTabStateLoadActive(struct editorTabState *tab) {
	E.tab_kind = tab->tab_kind;
	E.is_preview = tab->is_preview;
	E.tab_title = tab->tab_title;
	E.cursor_offset = tab->cursor_offset;
	E.preferred_rx = tab->preferred_rx;
	E.cx = tab->cx;
	E.cy = tab->cy;
	E.rx = tab->rx;
	E.rowoff = tab->rowoff;
	E.coloff = tab->coloff;
	E.numrows = tab->numrows;
	E.rows = tab->rows;
	E.document = tab->document;
	E.max_render_cols = tab->max_render_cols;
	E.max_render_cols_valid = tab->max_render_cols_valid;
	E.dirty = tab->dirty;
	E.filename = tab->filename;
	E.syntax_language = tab->syntax_language;
	E.syntax_state = tab->syntax_state;
	E.lsp_doc_open = tab->lsp_doc_open;
	E.lsp_doc_version = tab->lsp_doc_version;
	E.search_query = tab->search_query;
	E.search_match_offset = tab->search_match_offset;
	E.search_match_len = tab->search_match_len;
	E.search_direction = tab->search_direction;
	E.search_saved_offset = tab->search_saved_offset;
	E.selection_mode_active = tab->selection_mode_active;
	E.selection_anchor_offset = tab->selection_anchor_offset;
	E.mouse_left_button_down = tab->mouse_left_button_down;
	E.mouse_drag_anchor_offset = tab->mouse_drag_anchor_offset;
	E.mouse_drag_started = tab->mouse_drag_started;
	E.undo_history = tab->undo_history;
	E.redo_history = tab->redo_history;
	E.edit_pending_entry = tab->edit_pending_entry;
	E.edit_pending_entry_valid = tab->edit_pending_entry_valid;
	E.edit_group_kind = tab->edit_group_kind;
	E.edit_pending_kind = tab->edit_pending_kind;
	E.edit_pending_mode = tab->edit_pending_mode;
	editorSyntaxVisibleCacheInvalidate();

	editorTabStateInitEmpty(tab);
}

static int editorEnsureTabCapacity(int needed) {
	if (needed <= E.tab_capacity) {
		return 1;
	}

	int new_capacity = E.tab_capacity > 0 ? E.tab_capacity : 4;
	while (new_capacity < needed) {
		if (new_capacity >= ROTIDE_MAX_TABS) {
			new_capacity = ROTIDE_MAX_TABS;
			break;
		}
		new_capacity *= 2;
		if (new_capacity > ROTIDE_MAX_TABS) {
			new_capacity = ROTIDE_MAX_TABS;
		}
	}
	if (new_capacity < needed) {
		return 0;
	}

	size_t cap_size = 0;
	size_t tabs_bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
			!editorSizeMul(sizeof(struct editorTabState), cap_size, &tabs_bytes)) {
		return 0;
	}

	struct editorTabState *new_tabs = editorRealloc(E.tabs, tabs_bytes);
	if (new_tabs == NULL) {
		return 0;
	}

	for (int i = E.tab_capacity; i < new_capacity; i++) {
		editorTabStateInitEmpty(&new_tabs[i]);
	}

	E.tabs = new_tabs;
	E.tab_capacity = new_capacity;
	return 1;
}

static void editorStoreActiveTab(void) {
	if (E.tabs == NULL || E.tab_count <= 0 ||
			E.active_tab < 0 || E.active_tab >= E.tab_count) {
		return;
	}
	if (E.is_preview && E.dirty != 0) {
		E.is_preview = 0;
	}
	editorTabStateCaptureActive(&E.tabs[E.active_tab]);
}

static void editorLoadActiveTab(int tab_idx) {
	if (E.tabs == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		editorResetActiveBufferFields();
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	editorTabStateLoadActive(&E.tabs[tab_idx]);
	if (E.tab_kind == EDITOR_TAB_TASK_LOG) {
		E.syntax_language = EDITOR_SYNTAX_NONE;
		editorSyntaxStateDestroy(E.syntax_state);
		E.syntax_state = NULL;
		E.lsp_doc_open = 0;
		E.lsp_doc_version = 0;
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	const char *first_line = NULL;
	if (E.numrows > 0 && E.rows != NULL) {
		first_line = E.rows[0].chars;
	}
	enum editorSyntaxLanguage detected =
			editorSyntaxDetectLanguageFromFilenameAndFirstLine(E.filename, first_line);
	if (E.syntax_language != detected || (detected != EDITOR_SYNTAX_NONE && E.syntax_state == NULL)) {
		(void)editorSyntaxParseFullActive();
	}
	editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
}

int editorTabsInit(void) {
	editorTabsFreeAll();
	if (!editorEnsureTabCapacity(1)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	E.tab_count = 1;
	E.active_tab = 0;
	E.tab_view_start = 0;
	editorTabStateInitEmpty(&E.tabs[0]);
	editorLoadActiveTab(0);
	return 1;
}

void editorTabsFreeAll(void) {
	if (E.task_running && E.task_pid > 0) {
		int status = 0;
		(void)kill(E.task_pid, SIGTERM);
		(void)waitpid(E.task_pid, &status, 0);
	}
	editorTaskResetState();

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			editorLspNotifyDidCloseTabState(&E.tabs[i]);
		}
	}
	editorLspShutdown();

	editorFreeActiveBufferState();

	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			editorTabStateFree(&E.tabs[i]);
		}
	}
	free(E.tabs);
	E.tabs = NULL;
	E.tab_count = 0;
	E.tab_capacity = 0;
	E.active_tab = 0;
	E.tab_view_start = 0;
	editorSyntaxVisibleCacheFree();
	editorSyntaxReleaseSharedResources();
}

int editorTabNewEmpty(void) {
	if (E.tab_count >= ROTIDE_MAX_TABS) {
		editorSetStatusMsg("Tab limit reached (%d)", ROTIDE_MAX_TABS);
		return 0;
	}
	if (E.tab_count == 0) {
		return editorTabsInit();
	}

	editorStoreActiveTab();
	int new_idx = E.tab_count;
	if (!editorEnsureTabCapacity(E.tab_count + 1)) {
		editorLoadActiveTab(E.active_tab);
		editorSetAllocFailureStatus();
		return 0;
	}

	editorTabStateInitEmpty(&E.tabs[new_idx]);
	E.tab_count++;
	E.active_tab = new_idx;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

static int editorTabCanReuseActiveEmptyBuffer(void) {
	if (E.tab_count <= 0) {
		return 0;
	}
	if (E.tab_kind != EDITOR_TAB_FILE) {
		return 0;
	}
	if (E.filename != NULL && E.filename[0] != '\0') {
		return 0;
	}
	if (E.dirty != 0) {
		return 0;
	}
	for (int row_idx = 0; row_idx < E.numrows; row_idx++) {
		if (E.rows[row_idx].size != 0) {
			return 0;
		}
	}
	return 1;
}

static int editorTabFindReusablePreviewIndex(void) {
	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		if (tab_idx == E.active_tab) {
			if (E.tab_kind == EDITOR_TAB_FILE && E.is_preview && E.dirty == 0) {
				return tab_idx;
			}
			continue;
		}
		if (E.tabs[tab_idx].tab_kind == EDITOR_TAB_FILE &&
				E.tabs[tab_idx].is_preview &&
				E.tabs[tab_idx].dirty == 0) {
			return tab_idx;
		}
	}
	return -1;
}

void editorTabPinActivePreview(void) {
	if (E.tab_kind == EDITOR_TAB_FILE) {
		E.is_preview = 0;
	}
}

int editorActiveTabIsPreview(void) {
	return E.tab_kind == EDITOR_TAB_FILE && E.is_preview;
}

int editorTabIsPreviewAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return editorActiveTabIsPreview();
	}
	return E.tabs[idx].tab_kind == EDITOR_TAB_FILE && E.tabs[idx].is_preview;
}

int editorTabOpenFileAsNew(const char *filename) {
	if (editorTabCanReuseActiveEmptyBuffer()) {
		editorOpen(filename);
		E.is_preview = 0;
		return 1;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	editorOpen(filename);
	E.is_preview = 0;
	return 1;
}

int editorTabOpenOrSwitchToFile(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}

	int existing_tab = editorTabFindOpenFileIndex(filename);
	if (existing_tab >= 0) {
		if (!editorTabSwitchToIndex(existing_tab)) {
			return 0;
		}
		E.is_preview = 0;
		return 1;
	}

	return editorTabOpenFileAsNew(filename);
}

static int editorTabOpenOrSwitchToPreviewFile(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}

	int existing_tab = editorTabFindOpenFileIndex(filename);
	if (existing_tab >= 0) {
		return editorTabSwitchToIndex(existing_tab);
	}

	int preview_tab = editorTabFindReusablePreviewIndex();
	if (preview_tab >= 0) {
		if (!editorTabSwitchToIndex(preview_tab)) {
			return 0;
		}
		editorOpen(filename);
		E.is_preview = 1;
		return 1;
	}

	if (editorTabCanReuseActiveEmptyBuffer()) {
		editorOpen(filename);
		E.is_preview = 1;
		return 1;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	editorOpen(filename);
	E.is_preview = 1;
	return 1;
}

static const char *editorTabPathAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

static int editorPathsReferToSameFile(const char *left, const char *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}

	struct stat left_st;
	struct stat right_st;
	if (stat(left, &left_st) == 0 && stat(right, &right_st) == 0) {
		return left_st.st_dev == right_st.st_dev && left_st.st_ino == right_st.st_ino;
	}

	return strcmp(left, right) == 0;
}

static int editorTabFindOpenFileIndex(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}

	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		const char *tab_path = editorTabPathAt(tab_idx);
		if (tab_path == NULL || tab_path[0] == '\0') {
			continue;
		}
		if (editorPathsReferToSameFile(path, tab_path)) {
			return tab_idx;
		}
	}

	return -1;
}

int editorTabSwitchToIndex(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return 1;
	}

	editorStoreActiveTab();
	E.active_tab = idx;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabSwitchByDelta(int delta) {
	if (E.tab_count <= 0) {
		return 0;
	}
	if (delta == 0 || E.tab_count == 1) {
		return 1;
	}

	int target = (E.active_tab + delta) % E.tab_count;
	if (target < 0) {
		target += E.tab_count;
	}
	return editorTabSwitchToIndex(target);
}

int editorTabCloseActive(void) {
	if (E.tab_count <= 0 || E.tabs == NULL) {
		return 0;
	}

	editorStoreActiveTab();
	int closing = E.active_tab;
	editorLspNotifyDidCloseTabState(&E.tabs[closing]);
	editorTabStateFree(&E.tabs[closing]);

	if (E.tab_count == 1) {
		editorTabStateInitEmpty(&E.tabs[0]);
		E.active_tab = 0;
		E.tab_count = 1;
		E.tab_view_start = 0;
		editorLoadActiveTab(0);
		return 1;
	}

	memmove(&E.tabs[closing], &E.tabs[closing + 1],
			sizeof(struct editorTabState) * (size_t)(E.tab_count - closing - 1));
	E.tab_count--;
	if (closing >= E.tab_count) {
		closing = E.tab_count - 1;
	}
	E.active_tab = closing;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabCount(void) {
	return E.tab_count;
}

int editorTabActiveIndex(void) {
	return E.active_tab;
}

int editorTabAnyDirty(void) {
	if (E.tab_count <= 0) {
		return E.dirty != 0;
	}
	if (E.dirty) {
		return 1;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (i == E.active_tab) {
			continue;
		}
		if (E.tabs[i].dirty) {
			return 1;
		}
	}
	return 0;
}

const char *editorTabFilenameAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

const char *editorTabDisplayNameAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return "[No Name]";
	}
	if (idx == E.active_tab) {
		if (E.tab_kind == EDITOR_TAB_TASK_LOG && E.tab_title != NULL && E.tab_title[0] != '\0') {
			return E.tab_title;
		}
		return E.filename != NULL ? E.filename : "[No Name]";
	}
	if (E.tabs[idx].tab_kind == EDITOR_TAB_TASK_LOG &&
			E.tabs[idx].tab_title != NULL && E.tabs[idx].tab_title[0] != '\0') {
		return E.tabs[idx].tab_title;
	}
	return E.tabs[idx].filename != NULL ? E.tabs[idx].filename : "[No Name]";
}

const char *editorActiveBufferDisplayName(void) {
	if (E.tab_kind == EDITOR_TAB_TASK_LOG && E.tab_title != NULL && E.tab_title[0] != '\0') {
		return E.tab_title;
	}
	return E.filename != NULL ? E.filename : "[No Name]";
}

int editorTabDirtyAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return E.dirty != 0;
	}
	return E.tabs[idx].dirty != 0;
}

int editorActiveTabIsTaskLog(void) {
	return E.tab_kind == EDITOR_TAB_TASK_LOG;
}

int editorActiveTabIsReadOnly(void) {
	return E.tab_kind == EDITOR_TAB_TASK_LOG;
}

int editorActiveTaskTabIsRunning(void) {
	return E.task_running && E.task_tab_idx == E.active_tab && E.tab_kind == EDITOR_TAB_TASK_LOG;
}

static void editorTaskLogClampCursor(struct editorTabState *tab) {
	if (tab == NULL) {
		return;
	}
	if (tab->cy < 0) {
		tab->cy = 0;
	} else if (tab->cy > tab->numrows) {
		tab->cy = tab->numrows;
	}
	if (tab->cy >= tab->numrows) {
		tab->cx = 0;
		return;
	}
	if (tab->cx < 0) {
		tab->cx = 0;
	}
	if (tab->cx > tab->rows[tab->cy].size) {
		tab->cx = tab->rows[tab->cy].size;
	}
	tab->cx = editorRowClampCxToClusterBoundary(&tab->rows[tab->cy], tab->cx);
}

static int editorRebuildGeneratedTabRows(struct editorTabState *tab) {
	if (tab == NULL) {
		return 0;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	if (!editorBuildRowsFromDocument(tab->document, &new_rows, &new_numrows)) {
		return 0;
	}

	editorFreeTabRows(tab);
	tab->max_render_cols = 0;
	tab->max_render_cols_valid = 0;
	tab->rows = new_rows;
	tab->numrows = new_numrows;
	tab->dirty = 0;
	free(tab->filename);
	tab->filename = NULL;
	editorSyntaxStateDestroy(tab->syntax_state);
	tab->syntax_state = NULL;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	tab->lsp_doc_open = 0;
	tab->lsp_doc_version = 0;
	editorTaskLogClampCursor(tab);
	return 1;
}

static int editorTaskMutateTab(int tab_idx, int jump_to_end,
		int (*mutator)(struct editorTabState *tab, void *ctx), void *ctx) {
	if (mutator == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		return 0;
	}

	if (tab_idx == E.active_tab) {
		editorStoreActiveTab();
		if (!mutator(&E.tabs[tab_idx], ctx)) {
			editorLoadActiveTab(tab_idx);
			return 0;
		}
		editorLoadActiveTab(tab_idx);
		if (jump_to_end) {
			if (E.numrows > 0) {
				E.cy = E.numrows - 1;
				E.cx = E.rows[E.cy].size;
			} else {
				E.cy = 0;
				E.cx = 0;
			}
			editorViewportEnsureCursorVisible();
		}
		return 1;
	}

	return mutator(&E.tabs[tab_idx], ctx);
}

struct editorTaskAppendContext {
	const char *text;
	size_t len;
};

static int editorTaskAppendOutputMutator(struct editorTabState *tab, void *ctx) {
	static const char truncation_note[] = "\n[output truncated]\n";
	struct editorTaskAppendContext *append = ctx;
	size_t log_limit = ROTIDE_TASK_LOG_MAX_BYTES - (sizeof(truncation_note) - 1);
	size_t append_len = 0;
	size_t old_len = 0;

	if (tab == NULL || append == NULL) {
		return 0;
	}
	if (E.task_output_truncated) {
		return 1;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	old_len = editorDocumentLength(tab->document);

	if (old_len < log_limit) {
		append_len = append->len;
		if (append_len > log_limit - old_len) {
			append_len = log_limit - old_len;
		}
		if (append_len > 0 &&
				!editorDocumentReplaceRange(tab->document, old_len, 0,
						append->text, append_len)) {
			return 0;
		}
		E.task_output_bytes += append_len;
	}

	if (append_len < append->len) {
		if (!editorDocumentReplaceRange(tab->document, editorDocumentLength(tab->document), 0,
					truncation_note, sizeof(truncation_note) - 1)) {
			return 0;
		}
		E.task_output_truncated = 1;
	}

	if (!editorRebuildGeneratedTabRows(tab)) {
		return 0;
	}
	return 1;
}

static int editorTaskAppendOutput(int tab_idx, const char *text, size_t len, int jump_to_end) {
	struct editorTaskAppendContext ctx = {
		.text = text,
		.len = len
	};
	return editorTaskMutateTab(tab_idx, jump_to_end, editorTaskAppendOutputMutator, &ctx);
}

static void editorTaskResetState(void) {
	if (E.task_running && E.task_output_fd > STDERR_FILENO) {
		close(E.task_output_fd);
	}
	E.task_pid = 0;
	E.task_output_fd = -1;
	E.task_running = 0;
	E.task_tab_idx = -1;
	E.task_output_truncated = 0;
	E.task_output_bytes = 0;
	E.task_exit_code = 0;
	E.task_success_status[0] = '\0';
	E.task_failure_status[0] = '\0';
}

int editorTaskIsRunning(void) {
	return E.task_running;
}

int editorTaskRunningTabIndex(void) {
	return E.task_running ? E.task_tab_idx : -1;
}

static int editorTaskDrainOutput(int tab_idx, int jump_to_end, int *saw_eof_out) {
	char buf[4096];
	int changed = 0;

	if (saw_eof_out != NULL) {
		*saw_eof_out = 0;
	}
	if (E.task_output_fd == -1) {
		if (saw_eof_out != NULL) {
			*saw_eof_out = 1;
		}
		return 0;
	}

	for (;;) {
		ssize_t nread = read(E.task_output_fd, buf, sizeof(buf));
		if (nread > 0) {
			if (!editorTaskAppendOutput(tab_idx, buf, (size_t)nread, jump_to_end)) {
				editorSetAllocFailureStatus();
				return changed;
			}
			changed = 1;
			continue;
		}
		if (nread == 0) {
			close(E.task_output_fd);
			E.task_output_fd = -1;
			if (saw_eof_out != NULL) {
				*saw_eof_out = 1;
			}
			return 1;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return changed;
		}
		close(E.task_output_fd);
		E.task_output_fd = -1;
		if (saw_eof_out != NULL) {
			*saw_eof_out = 1;
		}
		return 1;
	}
}

static int editorTaskAppendFinalLineMutator(struct editorTabState *tab, void *ctx) {
	const char *line = ctx;
	if (tab == NULL || line == NULL) {
		return 0;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	if (!editorDocumentReplaceRange(tab->document, editorDocumentLength(tab->document), 0,
				line, strlen(line))) {
		return 0;
	}
	return editorRebuildGeneratedTabRows(tab);
}

static void editorTaskFinalize(int success, int exit_code) {
	char final_line[96];
	int tab_idx = E.task_tab_idx;
	if (tab_idx >= 0 && tab_idx < E.tab_count) {
		if (success) {
			(void)snprintf(final_line, sizeof(final_line),
					"\n[task completed successfully]\n");
		} else {
			(void)snprintf(final_line, sizeof(final_line),
					"\n[task failed with exit code %d]\n", exit_code);
		}
		(void)editorTaskMutateTab(tab_idx, 1, editorTaskAppendFinalLineMutator, final_line);
	}

	E.task_exit_code = exit_code;
	if (success) {
		editorTaskSetFinalStatus(1);
	} else {
		editorTaskSetFinalStatus(0);
	}
	editorTaskResetState();
}

static void editorTaskSetFinalStatus(int success) {
	if (success) {
		if (E.task_success_status[0] != '\0') {
			editorSetStatusMsg("%s", E.task_success_status);
			return;
		}
		editorSetStatusMsg("Task finished successfully");
		return;
	}

	if (E.task_failure_status[0] != '\0') {
		editorSetStatusMsg("%s", E.task_failure_status);
		return;
	}
	editorSetStatusMsg("Task failed");
}

int editorTaskPoll(void) {
	int changed = 0;
	int saw_eof = 0;
	int status = 0;
	pid_t waited = 0;

	if (!E.task_running || E.task_tab_idx < 0) {
		return 0;
	}

	changed |= editorTaskDrainOutput(E.task_tab_idx, 1, &saw_eof);

	do {
		waited = waitpid(E.task_pid, &status, WNOHANG);
	} while (waited == -1 && errno == EINTR);

	if (waited == E.task_pid) {
		int exit_code = 1;
		changed |= editorTaskDrainOutput(E.task_tab_idx, 1, &saw_eof);
		if (WIFEXITED(status)) {
			exit_code = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			exit_code = 128 + WTERMSIG(status);
		}
		editorTaskFinalize(exit_code == 0, exit_code);
		return 1;
	}

	if (saw_eof && E.task_output_fd == -1 && E.task_pid > 0) {
		return 1;
	}

	return changed;
}

int editorTaskTerminate(void) {
	int status = 0;
	int exit_code = 1;

	if (!E.task_running || E.task_pid <= 0) {
		return 1;
	}

	(void)kill(E.task_pid, SIGTERM);
	do {
		if (waitpid(E.task_pid, &status, 0) == E.task_pid) {
			break;
		}
	} while (errno == EINTR);

	(void)editorTaskDrainOutput(E.task_tab_idx, 1, NULL);
	if (WIFEXITED(status)) {
		exit_code = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exit_code = 128 + WTERMSIG(status);
	}
	editorTaskFinalize(0, exit_code);
	return 1;
}

int editorTaskStart(const char *title, const char *command,
		const char *success_status, const char *failure_status) {
	int output_pipe[2] = {-1, -1};
	pid_t pid = 0;
	int flags = 0;
	char header[PATH_MAX + 8];

	if (title == NULL || title[0] == '\0' || command == NULL || command[0] == '\0') {
		return 0;
	}
	if (E.task_running) {
		editorSetStatusMsg("Another task is already running");
		return 0;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}

	E.tab_kind = EDITOR_TAB_TASK_LOG;
	E.tab_title = strdup(title);
	if (E.tab_title == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	E.dirty = 0;
	free(E.filename);
	E.filename = NULL;
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	(void)snprintf(header, sizeof(header), "$ %s\n\n", command);
	if (!editorDocumentMirrorResetActiveFromText(header, strlen(header))) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorRestoreActiveFromDocument(E.document, 0, 0, 0, 0)) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (E.numrows > 0) {
		E.cy = E.numrows - 1;
		E.cx = E.rows[E.cy].size;
	}
	editorViewportEnsureCursorVisible();
	editorStoreActiveTab();
	editorLoadActiveTab(E.active_tab);

	if (pipe(output_pipe) == -1) {
		editorSetStatusMsg("Unable to start task");
		return 0;
	}

	pid = fork();
	if (pid == -1) {
		close(output_pipe[0]);
		close(output_pipe[1]);
		editorSetStatusMsg("Unable to start task");
		return 0;
	}

	if (pid == 0) {
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull != -1) {
			(void)dup2(devnull, STDIN_FILENO);
			close(devnull);
		}
		(void)dup2(output_pipe[1], STDOUT_FILENO);
		(void)dup2(output_pipe[1], STDERR_FILENO);
		close(output_pipe[0]);
		close(output_pipe[1]);
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	close(output_pipe[1]);
	flags = fcntl(output_pipe[0], F_GETFL);
	if (flags != -1) {
		(void)fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
	}

	E.task_pid = pid;
	E.task_output_fd = output_pipe[0];
	E.task_running = 1;
	E.task_tab_idx = E.active_tab;
	E.task_output_truncated = 0;
	E.task_output_bytes = 0;
	E.task_exit_code = 0;
	if (success_status != NULL) {
		(void)snprintf(E.task_success_status, sizeof(E.task_success_status), "%s", success_status);
	} else {
		E.task_success_status[0] = '\0';
	}
	if (failure_status != NULL) {
		(void)snprintf(E.task_failure_status, sizeof(E.task_failure_status), "%s", failure_status);
	} else {
		E.task_failure_status[0] = '\0';
	}
	editorSetStatusMsg("Running task: %s", title);
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

void editorDocumentMirrorTestResetStats(void) {
	g_document_mirror_full_rebuild_count = 0;
	g_document_mirror_incremental_update_count = 0;
	g_document_mirror_row_source_rebuild_count = 0;
}

int editorDocumentMirrorTestFullRebuildCount(void) {
	return g_document_mirror_full_rebuild_count;
}

int editorDocumentMirrorTestIncrementalUpdateCount(void) {
	return g_document_mirror_incremental_update_count;
}

int editorDocumentMirrorTestRowSourceRebuildCount(void) {
	return g_document_mirror_row_source_rebuild_count;
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

void editorSnapshotCaptureTestResetStats(void) {
	g_snapshot_capture_document_clone_count = 0;
	g_snapshot_capture_text_source_build_count = 0;
}

int editorSnapshotCaptureTestDocumentCloneCount(void) {
	return g_snapshot_capture_document_clone_count;
}

int editorSnapshotCaptureTestTextSourceBuildCount(void) {
	return g_snapshot_capture_text_source_build_count;
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

static const char *editorTabLabelFromDisplayName(const char *display_name) {
	if (display_name == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(display_name, '/');
	if (slash != NULL && slash[1] != '\0') {
		return slash + 1;
	}
	return display_name;
}

static int editorSanitizedTokenDisplayCols(const char *text, int text_len, int *src_len_out) {
	unsigned int cp = 0;
	int src_len = editorUtf8DecodeCodepoint(text, text_len, &cp);
	if (src_len <= 0) {
		src_len = 1;
	}
	if (src_len > text_len) {
		src_len = text_len;
	}
	if (src_len_out != NULL) {
		*src_len_out = src_len;
	}

	if (cp == '\t' || cp <= 0x1F || cp == 0x7F) {
		return 2;
	}
	if (cp >= 0x80 && cp <= 0x9F) {
		return 4;
	}
	return editorCharDisplayWidth(text, text_len);
}

static int editorSanitizedTextDisplayCols(const char *text, int max_cols) {
	if (text == NULL) {
		return 0;
	}

	int text_len = (int)strlen(text);
	int total_cols = 0;
	for (int idx = 0; idx < text_len;) {
		int src_len = 0;
		int token_cols = editorSanitizedTokenDisplayCols(&text[idx], text_len - idx, &src_len);
		if (max_cols >= 0 && total_cols + token_cols > max_cols) {
			break;
		}
		total_cols += token_cols;
		idx += src_len;
	}

	return total_cols;
}

static int editorTabLabelColsAt(int tab_idx) {
	const char *label = editorTabLabelFromDisplayName(editorTabDisplayNameAt(tab_idx));
	int cols = editorSanitizedTextDisplayCols(label, ROTIDE_TAB_TITLE_MAX_COLS);
	if (cols < 1) {
		cols = 1;
	}
	return cols;
}

static int editorTabWidthColsAt(int tab_idx) {
	// marker + dirty + left label pad + label + matching right pad
	return 6 + editorTabLabelColsAt(tab_idx);
}

static void editorTabVisibleRangeFromStart(int start_idx, int cols, int *last_idx_out) {
	int last_idx = start_idx - 1;
	if (E.tab_count <= 0 || cols <= 0 || start_idx < 0 || start_idx >= E.tab_count) {
		*last_idx_out = last_idx;
		return;
	}

	int used_cols = 0;
	for (int tab_idx = start_idx; tab_idx < E.tab_count; tab_idx++) {
		int width_cols = editorTabWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (tab_idx == start_idx && width_cols > cols) {
			width_cols = cols;
		}
		if (tab_idx > start_idx && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		used_cols += width_cols;
		last_idx = tab_idx;
		if (used_cols >= cols) {
			break;
		}
	}

	if (last_idx < start_idx && cols > 0) {
		last_idx = start_idx;
	}
	*last_idx_out = last_idx;
}

static void editorTabsAlignViewToActiveForWidth(int cols) {
	if (E.tab_count <= 0) {
		E.tab_view_start = 0;
		return;
	}
	if (E.active_tab < 0) {
		E.active_tab = 0;
	}
	if (E.active_tab >= E.tab_count) {
		E.active_tab = E.tab_count - 1;
	}

	if (E.tab_view_start < 0) {
		E.tab_view_start = 0;
	}
	if (E.tab_view_start >= E.tab_count) {
		E.tab_view_start = E.tab_count - 1;
	}

	if (cols <= 0) {
		if (E.active_tab < E.tab_view_start) {
			E.tab_view_start = E.active_tab;
		}
		return;
	}

	if (E.active_tab < E.tab_view_start) {
		E.tab_view_start = E.active_tab;
	}

	int last_visible = E.tab_view_start;
	editorTabVisibleRangeFromStart(E.tab_view_start, cols, &last_visible);
	while (E.active_tab > last_visible && E.tab_view_start < E.active_tab) {
		E.tab_view_start++;
		editorTabVisibleRangeFromStart(E.tab_view_start, cols, &last_visible);
	}
}

int editorTabBuildLayoutForWidth(int cols, struct editorTabLayoutEntry *entries, int max_entries,
		int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (E.tab_count <= 0 || cols <= 0 || max_entries == 0) {
		if (E.tab_count <= 0) {
			E.tab_view_start = 0;
		}
		return 1;
	}
	if (entries == NULL || max_entries < 0) {
		return 0;
	}

	editorTabsAlignViewToActiveForWidth(cols);
	int start_idx = E.tab_view_start;
	if (start_idx < 0) {
		start_idx = 0;
	}
	if (start_idx >= E.tab_count) {
		start_idx = E.tab_count - 1;
	}

	int used_cols = 0;
	int count = 0;
	for (int tab_idx = start_idx; tab_idx < E.tab_count && used_cols < cols; tab_idx++) {
		if (count >= max_entries) {
			break;
		}

		int width_cols = editorTabWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (count == 0 && width_cols > cols) {
			width_cols = cols;
		}
		if (count > 0 && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		struct editorTabLayoutEntry *entry = &entries[count];
		entry->tab_idx = tab_idx;
		entry->start_col = used_cols;
		entry->width_cols = width_cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;

		used_cols += width_cols;
		count++;
	}

	if (count == 0) {
		struct editorTabLayoutEntry *entry = &entries[0];
		entry->tab_idx = start_idx;
		entry->start_col = 0;
		entry->width_cols = cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;
		count = 1;
	}

	if (count > 0) {
		entries[0].show_left_overflow = entries[0].tab_idx > 0;
		entries[count - 1].show_right_overflow =
				entries[count - 1].tab_idx < E.tab_count - 1;
	}

	if (count_out != NULL) {
		*count_out = count;
	}
	return 1;
}

int editorTabHitTestColumn(int col, int cols) {
	if (col < 0 || col >= cols || E.tab_count <= 0 || cols <= 0) {
		return -1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForWidth(cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
		return -1;
	}
	for (int i = 0; i < layout_count; i++) {
		int start_col = layout[i].start_col;
		int end_col = start_col + layout[i].width_cols;
		if (col >= start_col && col < end_col) {
			return layout[i].tab_idx;
		}
	}
	return -1;
}

/*** Drawer ***/

struct editorDrawerNode {
	char *name;
	char *path;
	int is_dir;
	int is_expanded;
	int scanned;
	int scan_error;
	struct editorDrawerNode *parent;
	struct editorDrawerNode **children;
	int child_count;
};

struct editorDrawerLookup {
	struct editorDrawerNode *node;
	int depth;
	int visible_idx;
	int parent_visible_idx;
};

static char *editorDrawerPathJoin(const char *left, const char *right) {
	size_t left_len = strlen(left);
	while (left_len > 1 && left[left_len - 1] == '/') {
		left_len--;
	}

	while (right[0] == '/' && right[1] != '\0') {
		right++;
	}
	size_t right_len = strlen(right);

	int need_slash = 1;
	if (left_len == 0 || (left_len == 1 && left[0] == '/')) {
		need_slash = 0;
	}

	size_t total = 0;
	if (!editorSizeAdd(left_len, right_len, &total) ||
			(need_slash && !editorSizeAdd(total, 1, &total)) ||
			!editorSizeAdd(total, 1, &total)) {
		return NULL;
	}

	char *path = editorMalloc(total);
	if (path == NULL) {
		return NULL;
	}

	size_t write_idx = 0;
	if (left_len > 0) {
		memcpy(path, left, left_len);
		write_idx += left_len;
	}
	if (need_slash) {
		path[write_idx++] = '/';
	}
	if (right_len > 0) {
		memcpy(path + write_idx, right, right_len);
		write_idx += right_len;
	}
	path[write_idx] = '\0';
	return path;
}

static char *editorDrawerBasenameDup(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return strdup(".");
	}

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}
	if (len == 1 && path[0] == '/') {
		return strdup("/");
	}

	size_t start = len;
	while (start > 0 && path[start - 1] != '/') {
		start--;
	}

	size_t name_len = len - start;
	char *name = editorMalloc(name_len + 1);
	if (name == NULL) {
		return NULL;
	}
	memcpy(name, path + start, name_len);
	name[name_len] = '\0';
	return name;
}

static char *editorDrawerDirnameDup(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return strdup(".");
	}

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/') {
		len--;
	}

	size_t slash = len;
	while (slash > 0 && path[slash - 1] != '/') {
		slash--;
	}
	if (slash == 0) {
		return strdup(".");
	}
	if (slash == 1) {
		return strdup("/");
	}

	size_t dir_len = slash - 1;
	char *dir = editorMalloc(dir_len + 1);
	if (dir == NULL) {
		return NULL;
	}
	memcpy(dir, path, dir_len);
	dir[dir_len] = '\0';
	return dir;
}

static char *editorDrawerGetCwd(void) {
	char *cwd = getcwd(NULL, 0);
	if (cwd != NULL) {
		return cwd;
	}

	cwd = strdup(".");
	if (cwd == NULL) {
		editorSetAllocFailureStatus();
	}
	return cwd;
}

static char *editorDrawerResolveRootPathForStartup(int argc, char *argv[], int restored_session) {
	char *cwd = editorDrawerGetCwd();
	if (cwd == NULL) {
		return NULL;
	}

	if (restored_session || argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
		return cwd;
	}

	char *absolute = NULL;
	if (argv[1][0] == '/') {
		absolute = strdup(argv[1]);
	} else {
		absolute = editorDrawerPathJoin(cwd, argv[1]);
	}
	free(cwd);
	if (absolute == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	char *dir = editorDrawerDirnameDup(absolute);
	free(absolute);
	if (dir == NULL) {
		editorSetAllocFailureStatus();
		return NULL;
	}

	char *resolved = realpath(dir, NULL);
	if (resolved != NULL) {
		free(dir);
		return resolved;
	}

	return dir;
}

static struct editorDrawerNode *editorDrawerNodeNew(const char *name, const char *path, int is_dir,
		struct editorDrawerNode *parent) {
	struct editorDrawerNode *node = editorMalloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}

	node->name = strdup(name);
	node->path = strdup(path);
	if (node->name == NULL || node->path == NULL) {
		free(node->name);
		free(node->path);
		free(node);
		return NULL;
	}

	node->is_dir = is_dir;
	node->is_expanded = 0;
	node->scanned = 0;
	node->scan_error = 0;
	node->parent = parent;
	node->children = NULL;
	node->child_count = 0;
	return node;
}

static void editorDrawerNodeFree(struct editorDrawerNode *node) {
	if (node == NULL) {
		return;
	}

	for (int i = 0; i < node->child_count; i++) {
		editorDrawerNodeFree(node->children[i]);
	}
	free(node->children);
	free(node->name);
	free(node->path);
	free(node);
}

static int editorDrawerNodeCmp(const void *a, const void *b) {
	const struct editorDrawerNode *left = *(const struct editorDrawerNode * const *)a;
	const struct editorDrawerNode *right = *(const struct editorDrawerNode * const *)b;

	if (left->is_dir != right->is_dir) {
		return right->is_dir - left->is_dir;
	}

	int ci_cmp = strcasecmp(left->name, right->name);
	if (ci_cmp != 0) {
		return ci_cmp;
	}
	return strcmp(left->name, right->name);
}

static int editorDrawerEnsureScanned(struct editorDrawerNode *node) {
	if (node == NULL || !node->is_dir || node->scanned) {
		return 1;
	}

	DIR *dir = opendir(node->path);
	if (dir == NULL) {
		node->scanned = 1;
		node->scan_error = 1;
		editorSetStatusMsg("Drawer scan failed: %s", strerror(errno));
		return 0;
	}

	struct editorDrawerNode **children = NULL;
	int child_count = 0;
	int child_capacity = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
			continue;
		}

		char *child_path = editorDrawerPathJoin(node->path, entry->d_name);
		if (child_path == NULL) {
			editorSetAllocFailureStatus();
			break;
		}

		struct stat st;
		int is_dir = 0;
		if (lstat(child_path, &st) == 0) {
			is_dir = S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
		}

		struct editorDrawerNode *child = editorDrawerNodeNew(entry->d_name, child_path, is_dir, node);
		free(child_path);
		if (child == NULL) {
			editorSetAllocFailureStatus();
			break;
		}

		if (child_count >= child_capacity) {
			int new_capacity = child_capacity > 0 ? child_capacity * 2 : 8;
			size_t cap_size = 0;
			size_t bytes = 0;
			if (!editorIntToSize(new_capacity, &cap_size) ||
					!editorSizeMul(sizeof(*children), cap_size, &bytes)) {
				editorDrawerNodeFree(child);
				editorSetAllocFailureStatus();
				break;
			}

			struct editorDrawerNode **grown = editorRealloc(children, bytes);
			if (grown == NULL) {
				editorDrawerNodeFree(child);
				editorSetAllocFailureStatus();
				break;
			}
			children = grown;
			child_capacity = new_capacity;
		}

		children[child_count++] = child;
	}

	(void)closedir(dir);

	if (child_count > 1) {
		qsort(children, (size_t)child_count, sizeof(*children), editorDrawerNodeCmp);
	}

	node->children = children;
	node->child_count = child_count;
	node->scanned = 1;
	return 1;
}

static int editorDrawerCountVisibleFromNode(struct editorDrawerNode *node) {
	if (node == NULL) {
		return 0;
	}

	int count = 1;
	if (!node->is_dir || !node->is_expanded) {
		return count;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		count += editorDrawerCountVisibleFromNode(node->children[i]);
	}
	return count;
}

static int editorDrawerLookupByVisibleIndexRecursive(struct editorDrawerNode *node, int depth,
		int parent_visible_idx, int *cursor, int target_visible_idx,
		struct editorDrawerLookup *lookup_out) {
	if (node == NULL || cursor == NULL || lookup_out == NULL) {
		return 0;
	}

	int current = *cursor;
	if (current == target_visible_idx) {
		lookup_out->node = node;
		lookup_out->depth = depth;
		lookup_out->visible_idx = current;
		lookup_out->parent_visible_idx = parent_visible_idx;
		return 1;
	}

	(*cursor)++;
	if (!node->is_dir || !node->is_expanded) {
		return 0;
	}

	(void)editorDrawerEnsureScanned(node);
	for (int i = 0; i < node->child_count; i++) {
		if (editorDrawerLookupByVisibleIndexRecursive(node->children[i], depth + 1, current, cursor,
					target_visible_idx, lookup_out)) {
			return 1;
		}
	}
	return 0;
}

static int editorDrawerLookupByVisibleIndex(int visible_idx, struct editorDrawerLookup *lookup_out) {
	if (lookup_out == NULL || E.drawer_root == NULL || visible_idx < 0) {
		return 0;
	}

	int cursor = 0;
	return editorDrawerLookupByVisibleIndexRecursive(E.drawer_root, 0, -1, &cursor, visible_idx,
			lookup_out);
}

void editorDrawerClampViewport(int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		E.drawer_selected_index = 0;
		E.drawer_rowoff = 0;
		return;
	}

	if (E.drawer_selected_index < 0) {
		E.drawer_selected_index = 0;
	}
	if (E.drawer_selected_index >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}
	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	if (E.drawer_rowoff > max_rowoff) {
		E.drawer_rowoff = max_rowoff;
	}
	if (E.drawer_rowoff < 0) {
		E.drawer_rowoff = 0;
	}
}

static void editorDrawerClampSelectionAndScroll(int viewport_rows) {
	editorDrawerClampViewport(viewport_rows);

	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		return;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}
	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	if (E.drawer_selected_index < E.drawer_rowoff) {
		E.drawer_rowoff = E.drawer_selected_index;
	}
	if (E.drawer_selected_index >= E.drawer_rowoff + viewport_rows) {
		E.drawer_rowoff = E.drawer_selected_index - viewport_rows + 1;
	}

	if (E.drawer_rowoff > max_rowoff) {
		E.drawer_rowoff = max_rowoff;
	}
	if (E.drawer_rowoff < 0) {
		E.drawer_rowoff = 0;
	}
}

static int editorDrawerClampWidthForCols(int desired_width, int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	if (desired_width < 1) {
		desired_width = 1;
	}

	int max_drawer = total_cols - 2;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	if (desired_width > max_drawer) {
		desired_width = max_drawer;
	}

	return desired_width;
}

static int editorDrawerDefaultMaxWidthForCols(int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	int min_text_cols = total_cols / 2;
	if (min_text_cols < 1) {
		min_text_cols = 1;
	}
	int max_drawer = total_cols - 1 - min_text_cols;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	return max_drawer;
}

int editorDrawerIsCollapsed(void) {
	return E.drawer_collapsed != 0;
}

int editorDrawerSetCollapsed(int collapsed) {
	int new_collapsed = collapsed != 0;
	if (E.drawer_collapsed == new_collapsed) {
		return 0;
	}

	E.drawer_collapsed = new_collapsed;
	E.drawer_resize_active = 0;
	if (new_collapsed && E.pane_focus == EDITOR_PANE_DRAWER) {
		E.pane_focus = EDITOR_PANE_TEXT;
	}
	return 1;
}

int editorDrawerToggleCollapsed(void) {
	return editorDrawerSetCollapsed(!editorDrawerIsCollapsed());
}

int editorDrawerWidthForCols(int total_cols) {
	if (editorDrawerIsCollapsed()) {
		return editorDrawerClampWidthForCols(ROTIDE_DRAWER_COLLAPSED_WIDTH, total_cols);
	}

	int desired_width = E.drawer_width_cols;
	if (desired_width <= 0) {
		desired_width = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}

	int width = editorDrawerClampWidthForCols(desired_width, total_cols);
	if (!E.drawer_width_user_set) {
		int default_max = editorDrawerDefaultMaxWidthForCols(total_cols);
		if (width > default_max) {
			width = default_max;
		}
	}
	return width;
}

int editorDrawerSeparatorWidthForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	if (drawer_cols <= 0) {
		return 0;
	}
	return total_cols - drawer_cols >= 2 ? 1 : 0;
}

int editorDrawerTextStartColForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(total_cols);
	return drawer_cols + separator_cols;
}

int editorDrawerTextViewportCols(int total_cols) {
	if (total_cols <= 1) {
		return 1;
	}
	int text_cols = total_cols - editorDrawerTextStartColForCols(total_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}
	return text_cols;
}

int editorTextBodyStartColForCols(int total_cols) {
	int text_start = editorDrawerTextStartColForCols(total_cols);
	int text_cols = editorDrawerTextViewportCols(total_cols);
	if (text_cols >= 3) {
		return text_start + 1;
	}
	return text_start;
}

int editorTextBodyViewportCols(int total_cols) {
	int text_cols = editorDrawerTextViewportCols(total_cols);
	if (text_cols >= 3) {
		return text_cols - 2;
	}
	return text_cols;
}

int editorDrawerSetWidthForCols(int width, int total_cols) {
	int clamped = editorDrawerClampWidthForCols(width, total_cols);
	E.drawer_width_user_set = 1;
	if (E.drawer_width_cols == clamped) {
		return 0;
	}
	E.drawer_width_cols = clamped;
	return 1;
}

int editorDrawerResizeByDeltaForCols(int delta, int total_cols) {
	int current = editorDrawerIsCollapsed() ? E.drawer_width_cols :
			editorDrawerWidthForCols(total_cols);
	if (current <= 0) {
		current = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}
	return editorDrawerSetWidthForCols(current + delta, total_cols);
}

int editorDrawerVisibleCount(void) {
	return editorDrawerCountVisibleFromNode(E.drawer_root);
}

int editorDrawerGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}

	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(visible_idx, &lookup)) {
		return 0;
	}

	view_out->name = lookup.node->name;
	view_out->path = lookup.node->path;
	view_out->depth = lookup.depth;
	view_out->is_dir = lookup.node->is_dir;
	view_out->is_expanded = lookup.node->is_expanded;
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->has_scan_error = lookup.node->scan_error;
	view_out->is_root = lookup.node == E.drawer_root;
	view_out->parent_visible_idx = lookup.parent_visible_idx;
	if (lookup.node->parent != NULL && lookup.node->parent->child_count > 0 &&
			lookup.node->parent->children[lookup.node->parent->child_count - 1] == lookup.node) {
		view_out->is_last_sibling = 1;
	} else {
		view_out->is_last_sibling = lookup.node->parent == NULL;
	}
	view_out->is_active_file = !lookup.node->is_dir && E.filename != NULL &&
			editorPathsReferToSameFile(lookup.node->path, E.filename);
	return 1;
}

int editorDrawerMoveSelectionBy(int delta, int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		return 0;
	}

	if (delta < 0 && E.drawer_selected_index + delta < 0) {
		E.drawer_selected_index = 0;
	} else if (delta > 0 && E.drawer_selected_index + delta >= visible_count) {
		E.drawer_selected_index = visible_count - 1;
	} else {
		E.drawer_selected_index += delta;
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerScrollBy(int delta, int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_count <= 0) {
		E.drawer_rowoff = 0;
		return 0;
	}

	if (viewport_rows < 1) {
		viewport_rows = 1;
	}

	int max_rowoff = visible_count - viewport_rows;
	if (max_rowoff < 0) {
		max_rowoff = 0;
	}

	int old_rowoff = E.drawer_rowoff;
	int new_rowoff = E.drawer_rowoff + delta;
	if (new_rowoff < 0) {
		new_rowoff = 0;
	}
	if (new_rowoff > max_rowoff) {
		new_rowoff = max_rowoff;
	}
	E.drawer_rowoff = new_rowoff;
	return E.drawer_rowoff != old_rowoff;
}

int editorDrawerExpandSelection(int viewport_rows) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}

	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	lookup.node->is_expanded = 1;
	(void)editorDrawerEnsureScanned(lookup.node);
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerCollapseSelection(int viewport_rows) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}

	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}

	if (lookup.node->is_dir && lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	if (lookup.parent_visible_idx >= 0) {
		E.drawer_selected_index = lookup.parent_visible_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	return 0;
}

int editorDrawerToggleSelectionExpanded(int viewport_rows) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}
	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}

	if (lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
	} else {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectVisibleIndex(int visible_idx, int viewport_rows) {
	int visible_count = editorDrawerVisibleCount();
	if (visible_idx < 0 || visible_idx >= visible_count) {
		return 0;
	}

	E.drawer_selected_index = visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectedIsDirectory(void) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node->is_dir;
}

int editorDrawerOpenSelectedFileInTab(void) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.node->is_dir || lookup.node->path == NULL || lookup.node->path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToFile(lookup.node->path);
}

int editorDrawerOpenSelectedFileInPreviewTab(void) {
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.node->is_dir || lookup.node->path == NULL || lookup.node->path[0] == '\0') {
		return 0;
	}
	return editorTabOpenOrSwitchToPreviewFile(lookup.node->path);
}

const char *editorDrawerRootPath(void) {
	return E.drawer_root_path;
}

void editorDrawerShutdown(void) {
	editorDrawerNodeFree(E.drawer_root);
	E.drawer_root = NULL;
	free(E.drawer_root_path);
	E.drawer_root_path = NULL;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	E.drawer_resize_active = 0;
	E.drawer_collapsed = 0;
	E.pane_focus = EDITOR_PANE_TEXT;
}

int editorDrawerInitForStartup(int argc, char *argv[], int restored_session) {
	editorDrawerShutdown();

	char *root_path = editorDrawerResolveRootPathForStartup(argc, argv, restored_session);
	if (root_path == NULL) {
		return 0;
	}

	char *root_name = editorDrawerBasenameDup(root_path);
	if (root_name == NULL) {
		free(root_path);
		editorSetAllocFailureStatus();
		return 0;
	}

	struct editorDrawerNode *root = editorDrawerNodeNew(root_name, root_path, 1, NULL);
	free(root_name);
	if (root == NULL) {
		free(root_path);
		editorSetAllocFailureStatus();
		return 0;
	}
	root->is_expanded = 1;

	E.drawer_root_path = root_path;
	E.drawer_root = root;
	E.drawer_selected_index = 0;
	E.drawer_rowoff = 0;
	E.drawer_last_click_visible_idx = -1;
	E.drawer_last_click_ms = 0;
	if (E.drawer_width_cols <= 0) {
		E.drawer_width_cols = ROTIDE_DRAWER_DEFAULT_WIDTH;
		E.drawer_width_user_set = 0;
	}
	E.drawer_collapsed = 0;
	E.drawer_resize_active = 0;
	E.pane_focus = EDITOR_PANE_TEXT;
	editorDrawerClampSelectionAndScroll(E.window_rows + 1);
	return 1;
}

void editorHistoryReset(void) {
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
	editorClearSelectionState();
}

void editorHistoryBreakGroup(void) {
	E.edit_group_kind = EDITOR_EDIT_NONE;
}

void editorHistoryDiscardEdit(void);

void editorHistoryBeginEdit(enum editorEditKind kind) {
	editorHistoryDiscardEdit();
	E.edit_pending_kind = kind;

	if (kind != EDITOR_EDIT_INSERT_TEXT) {
		E.edit_group_kind = EDITOR_EDIT_NONE;
	}
	E.edit_pending_mode = EDITOR_EDIT_PENDING_CAPTURED;
}

void editorHistoryCommitEdit(enum editorEditKind kind, int changed) {
	enum editorEditPendingMode mode = E.edit_pending_mode;
	int recorded = 0;
	if (!changed) {
		editorHistoryDiscardEdit();
		E.edit_group_kind = EDITOR_EDIT_NONE;
		return;
	}

	editorHistoryClear(&E.redo_history);

	if (mode == EDITOR_EDIT_PENDING_CAPTURED &&
			E.edit_pending_kind == kind &&
			E.edit_pending_entry_valid) {
		struct editorHistoryEntry entry = E.edit_pending_entry;
		memset(&E.edit_pending_entry, 0, sizeof(E.edit_pending_entry));
		E.edit_pending_entry_valid = 0;
		recorded = 1;
		if (!(kind == EDITOR_EDIT_INSERT_TEXT &&
				E.edit_group_kind == EDITOR_EDIT_INSERT_TEXT &&
				editorHistoryTryMergeInsert(&E.undo_history, &entry))) {
			editorHistoryPushNewest(&E.undo_history, &entry);
		}
		editorHistoryEntryFree(&entry);
	}

	if (kind == EDITOR_EDIT_INSERT_TEXT && mode == EDITOR_EDIT_PENDING_CAPTURED && recorded) {
		E.edit_group_kind = EDITOR_EDIT_INSERT_TEXT;
	} else {
		E.edit_group_kind = EDITOR_EDIT_NONE;
	}

	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

void editorHistoryDiscardEdit(void) {
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

int editorUndo(void) {
	editorHistoryBreakGroup();
	editorHistoryDiscardEdit();

	struct editorHistoryEntry target = {0};
	if (!editorHistoryPopNewest(&E.undo_history, &target)) {
		editorSetStatusMsg("Nothing to undo");
		return 0;
	}

	if (editorApplyHistoryEntry(&target, 1) != 1) {
		editorHistoryPushNewest(&E.undo_history, &target);
		return -1;
	}

	editorHistoryPushNewest(&E.redo_history, &target);
	return 1;
}

int editorRedo(void) {
	editorHistoryBreakGroup();
	editorHistoryDiscardEdit();

	struct editorHistoryEntry target = {0};
	if (!editorHistoryPopNewest(&E.redo_history, &target)) {
		editorSetStatusMsg("Nothing to redo");
		return 0;
	}

	if (editorApplyHistoryEntry(&target, 0) != 1) {
		editorHistoryPushNewest(&E.redo_history, &target);
		return -1;
	}

	editorHistoryPushNewest(&E.undo_history, &target);
	return 1;
}

static int editorRebuildRowRender(struct erow *row) {
	char *new_render = NULL;
	int new_rsize = 0;
	int new_display_cols = 0;
	if (!editorBuildRender(row->chars, row->size, &new_render, &new_rsize, &new_display_cols)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	int old_display_cols = row->render_display_cols;
	free(row->render);
	row->render = new_render;
	row->rsize = new_rsize;
	row->render_display_cols = new_display_cols;
	if (E.max_render_cols_valid) {
		if (new_display_cols > E.max_render_cols) {
			E.max_render_cols = new_display_cols;
		} else if (old_display_cols == E.max_render_cols && new_display_cols < old_display_cols) {
			E.max_render_cols_valid = 0;
		}
	}
	return 1;
}

void editorUpdateRow(struct erow *row) {
	(void)editorRebuildRowRender(row);
}

static int editorFindRowIndex(const struct erow *row) {
	if (row == NULL || E.rows == NULL || E.numrows <= 0) {
		return -1;
	}
	for (int row_idx = 0; row_idx < E.numrows; row_idx++) {
		if (&E.rows[row_idx] == row) {
			return row_idx;
		}
	}
	return -1;
}

static int editorApplyLowLevelDocumentEdit(enum editorEditKind kind,
		size_t start_offset, size_t old_len, const char *new_text, size_t new_len) {
	size_t cursor_offset = 0;
	if (!editorBufferPosToOffset(E.cy, E.cx, &cursor_offset)) {
		cursor_offset = 0;
	}

	int after_dirty = E.dirty;
	if (after_dirty < INT_MAX) {
		after_dirty++;
	}

	struct editorDocumentEdit edit = {
		.kind = kind,
		.start_offset = start_offset,
		.old_len = old_len,
		.new_text = new_text != NULL ? new_text : "",
		.new_len = new_len,
		.before_cursor_offset = cursor_offset,
		.after_cursor_offset = cursor_offset,
		.before_dirty = E.dirty,
		.after_dirty = after_dirty
	};
	return editorApplyDocumentEdit(&edit);
}

void editorInsertRow(int idx, const char *s, size_t len) {
	if (idx < 0 || idx > E.numrows) {
		return;
	}
	if ((len > 0 && s == NULL) ||
			len > ROTIDE_MAX_TEXT_BYTES || !editorSizeWithinInt(len)) {
		editorSetOperationTooLargeStatus();
		return;
	}

	size_t start_offset = 0;
	size_t insert_len = 0;
	if (!editorBufferPosToOffset(idx, 0, &start_offset) ||
			!editorSizeAdd(len, 1, &insert_len)) {
		editorSetOperationTooLargeStatus();
		return;
	}

	char *inserted = editorMalloc(insert_len);
	if (inserted == NULL) {
		editorSetAllocFailureStatus();
		return;
	}
	if (len > 0) {
		memcpy(inserted, s, len);
	}
	inserted[len] = '\n';
	(void)editorApplyLowLevelDocumentEdit(EDITOR_EDIT_NEWLINE, start_offset, 0, inserted, insert_len);
	free(inserted);
}

void editorDeleteRow(int idx) {
	if (idx < 0 || idx >= E.numrows) {
		return;
	}
	struct erow *row = &E.rows[idx];
	size_t row_len = 0;
	size_t old_len = 0;
	size_t start_offset = 0;
	if (!editorIntToSize(row->size, &row_len) ||
			!editorSizeAdd(row_len, NEWLINE_CHAR_WIDTH, &old_len) ||
			!editorBufferPosToOffset(idx, 0, &start_offset)) {
		editorSetOperationTooLargeStatus();
		return;
	}
	(void)editorApplyLowLevelDocumentEdit(EDITOR_EDIT_DELETE_TEXT, start_offset, old_len, "", 0);
}

void editorInsertCharAt(struct erow *row, int idx, int c) {
	int row_idx = editorFindRowIndex(row);
	if (row_idx < 0) {
		return;
	}
	if (idx < 0 || row->size < idx) {
		idx = row->size;
	}
	size_t start_offset = 0;
	if (!editorBufferPosToOffset(row_idx, idx, &start_offset)) {
		editorSetOperationTooLargeStatus();
		return;
	}
	char inserted = (char)c;
	(void)editorApplyLowLevelDocumentEdit(EDITOR_EDIT_INSERT_TEXT, start_offset, 0, &inserted, 1);
}

void editorRowAppendString(struct erow *row, const char *s, size_t len) {
	int row_idx = editorFindRowIndex(row);
	if (row_idx < 0) {
		return;
	}
	if (len > 0 && s == NULL) {
		editorSetOperationTooLargeStatus();
		return;
	}
	size_t start_offset = 0;
	if (!editorBufferPosToOffset(row_idx, row->size, &start_offset)) {
		editorSetOperationTooLargeStatus();
		return;
	}
	(void)editorApplyLowLevelDocumentEdit(EDITOR_EDIT_INSERT_TEXT, start_offset, 0,
			len > 0 ? s : "", len);
}

void editorDelCharAt(struct erow *row, int idx) {
	int row_idx = editorFindRowIndex(row);
	if (row_idx < 0 || idx < 0 || row->size <= idx) {
		return;
	}
	size_t start_offset = 0;
	if (!editorBufferPosToOffset(row_idx, idx, &start_offset)) {
		editorSetOperationTooLargeStatus();
		return;
	}
	(void)editorApplyLowLevelDocumentEdit(EDITOR_EDIT_DELETE_TEXT, start_offset, 1, "", 0);
}

void editorDelCharsAt(struct erow *row, int idx, int len) {
	int row_idx = editorFindRowIndex(row);
	if (row_idx < 0 || idx < 0 || len <= 0 || idx > row->size || len > row->size - idx) {
		return;
	}
	size_t start_offset = 0;
	if (!editorBufferPosToOffset(row_idx, idx, &start_offset)) {
		editorSetOperationTooLargeStatus();
		return;
	}
	(void)editorApplyLowLevelDocumentEdit(EDITOR_EDIT_DELETE_TEXT, start_offset, (size_t)len, "", 0);
}

int editorInsertText(const char *text, size_t len) {
	int insert_cx = 0;
	size_t start_offset = 0;
	size_t after_offset = 0;
	int dirty_delta = 0;
	int sim_cy = 0;
	int sim_cx = 0;
	int sim_numrows = 0;

	if (len == 0) {
		return 0;
	}
	if (text == NULL || len > ROTIDE_MAX_TEXT_BYTES || E.cy < 0 || E.cy > E.numrows) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (E.cy < E.numrows) {
		insert_cx = editorRowClampCxToClusterBoundary(&E.rows[E.cy], E.cx);
	} else {
		insert_cx = 0;
	}
	if (!editorBufferPosToOffset(E.cy, insert_cx, &start_offset) ||
			!editorSizeAdd(start_offset, len, &after_offset) ||
			after_offset > ROTIDE_MAX_TEXT_BYTES) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	sim_cy = E.cy;
	sim_cx = insert_cx;
	sim_numrows = E.numrows;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			dirty_delta++;
			sim_numrows++;
			sim_cy++;
			sim_cx = 0;
			continue;
		}
		dirty_delta++;
		if (sim_cy == sim_numrows) {
			dirty_delta++;
			sim_numrows++;
		}
		sim_cx++;
	}
	if (dirty_delta <= 0 || E.dirty > INT_MAX - dirty_delta) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_INSERT_TEXT,
		.start_offset = start_offset,
		.old_len = 0,
		.new_text = text,
		.new_len = len,
		.before_cursor_offset = start_offset,
		.after_cursor_offset = after_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	return editorApplyDocumentEdit(&edit);
}

void editorInsertChar(int c) {
	int insert_cx = 0;
	size_t start_offset = 0;
	char inserted_text[2] = {(char)c, '\n'};
	size_t inserted_len = 1;
	int dirty_delta = 1;

	if (E.cy < 0 || E.cy > E.numrows) {
		return;
	}
	if (E.cy < E.numrows) {
		insert_cx = editorRowClampCxToClusterBoundary(&E.rows[E.cy], E.cx);
	} else {
		insert_cx = 0;
	}
	if (!editorBufferPosToOffset(E.cy, insert_cx, &start_offset)) {
		return;
	}
	if (E.cy == E.numrows) {
		inserted_len = 2;
		dirty_delta = 2;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_INSERT_TEXT,
		.start_offset = start_offset,
		.old_len = 0,
		.new_text = inserted_text,
		.new_len = inserted_len,
		.before_cursor_offset = start_offset,
		.after_cursor_offset = start_offset + 1,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	(void)editorApplyDocumentEdit(&edit);
}

void editorInsertNewline(void) {
	int split_idx = 0;
	if (E.cy < E.numrows) {
		split_idx = editorRowClampCxToClusterBoundary(&E.rows[E.cy], E.cx);
	}

	size_t start_offset = 0;
	if (!editorBufferPosToOffset(E.cy, split_idx, &start_offset)) {
		return;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_NEWLINE,
		.start_offset = start_offset,
		.old_len = 0,
		.new_text = "\n",
		.new_len = 1,
		.before_cursor_offset = start_offset,
		.after_cursor_offset = start_offset + 1,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + 1
	};
	(void)editorApplyDocumentEdit(&edit);
}

void editorDelChar(void) {
	if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
		return;
	}
	size_t before_cursor_offset = 0;
	size_t start_offset = 0;
	size_t end_offset = 0;
	size_t old_len = 0;
	int dirty_delta = 1;

	if (!editorBufferPosToOffset(E.cy, E.cx, &before_cursor_offset)) {
		before_cursor_offset = 0;
	}

	if (E.cx > 0) {
		struct erow *row = &E.rows[E.cy];
		int cur_cx = editorRowClampCxToClusterBoundary(row, E.cx);
		int prev_cx = editorRowPrevClusterIdx(row, cur_cx);
		if (!editorBufferPosToOffset(E.cy, prev_cx, &start_offset) ||
				!editorBufferPosToOffset(E.cy, cur_cx, &end_offset) ||
				end_offset <= start_offset) {
			return;
		}
	} else {
		int merge_col = E.rows[E.cy - 1].size;
		if (!editorBufferPosToOffset(E.cy - 1, merge_col, &start_offset) ||
				!editorBufferPosToOffset(E.cy, 0, &end_offset) ||
				end_offset <= start_offset) {
			return;
		}
		dirty_delta = 2;
	}

	old_len = end_offset - start_offset;
	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_DELETE_TEXT,
		.start_offset = start_offset,
		.old_len = old_len,
		.new_text = "",
		.new_len = 0,
		.before_cursor_offset = before_cursor_offset,
		.after_cursor_offset = start_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	(void)editorApplyDocumentEdit(&edit);
}

void editorOpen(const char *filename) {
	int was_preview = E.is_preview;
	FILE *fp = NULL;
	char *text = NULL;
	size_t text_len = 0;
	struct editorDocument document;
	int document_inited = 0;

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	editorFreeActiveBufferState();
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = was_preview;
	E.filename = strdup(filename);
	if (E.filename == NULL) {
		editorSetAllocFailureStatus();
		return;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		panic("fopen");
	}
	if (!editorReadNormalizedFileToText(fp, &text, &text_len)) {
		fclose(fp);
		return;
	}
	fclose(fp);

	editorDocumentInit(&document);
	document_inited = 1;
	if (!editorDocumentResetFromString(&document, text, text_len)) {
		editorSetAllocFailureStatus();
		goto cleanup;
	}

	if (!editorRestoreActiveFromDocument(&document, 0, 0, 0, 1)) {
		goto cleanup;
	}

cleanup:
	if (document_inited) {
		editorDocumentFree(&document);
	}
	free(text);
}

void editorSetStatusMsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static int editorWriteAll(int fd, const char *buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (written == 0) {
			errno = EIO;
			return -1;
		}
		total += (size_t)written;
	}
	return 0;
}

static char *editorTempPathForTarget(const char *target) {
	const char *slash = strrchr(target, '/');
	const char *basename = target;
	size_t dir_len = 0;
	static const char suffix[] = ".rotide-tmp-XXXXXX";

	if (slash != NULL) {
		basename = slash + 1;
		dir_len = (size_t)(slash - target + 1);
	}

	size_t base_len = strlen(basename);
	size_t total_len = 0;
	if (!editorSizeAdd(dir_len, base_len, &total_len) ||
			!editorSizeAdd(total_len, sizeof(suffix), &total_len)) {
		return NULL;
	}
	char *tmp_path = editorMalloc(total_len);
	if (tmp_path == NULL) {
		return NULL;
	}

	if (dir_len > 0) {
		memcpy(tmp_path, target, dir_len);
	}
	memcpy(tmp_path + dir_len, basename, base_len);
	memcpy(tmp_path + dir_len + base_len, suffix, sizeof(suffix));

	return tmp_path;
}

static int editorOpenParentDirForTarget(const char *target) {
	const char *slash = strrchr(target, '/');
	if (slash == NULL) {
		return editorSaveOpenDir(".");
	}
	if (slash == target) {
		return editorSaveOpenDir("/");
	}

	size_t dir_len = (size_t)(slash - target);
	size_t dir_cap = 0;
	if (!editorSizeAdd(dir_len, 1, &dir_cap)) {
		errno = ENOMEM;
		return -1;
	}
	char *dir_path = editorMalloc(dir_cap);
	if (dir_path == NULL) {
		errno = ENOMEM;
		return -1;
	}

	memcpy(dir_path, target, dir_len);
	dir_path[dir_len] = '\0';
	int dir_fd = editorSaveOpenDir(dir_path);
	free(dir_path);
	return dir_fd;
}

static int editorSaveCleanupOnError(int *fd, int *dir_fd, const char *tmp_path,
		int tmp_created, int tmp_renamed, int *cleanup_errno) {
	int first_cleanup_errno = 0;

	if (*fd != -1) {
		if (editorSaveClose(*fd) == -1 && first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
		*fd = -1;
	}
	if (*dir_fd != -1) {
		if (editorSaveClose(*dir_fd) == -1 && first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
		*dir_fd = -1;
	}

	if (tmp_path != NULL && tmp_created && !tmp_renamed) {
		if (editorSaveUnlink(tmp_path) == -1 && errno != ENOENT &&
				first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
	}

	if (cleanup_errno != NULL) {
		*cleanup_errno = first_cleanup_errno;
	}
	return first_cleanup_errno == 0 ? 0 : -1;
}

static const char *editorSaveFailureClass(int errnum) {
	switch (errnum) {
		case EACCES:
		case EPERM:
			return "permission denied";
		case ENOENT:
		case ENOTDIR:
			return "missing path";
		case EROFS:
			return "read-only filesystem";
		case ENOSPC:
#ifdef EDQUOT
		case EDQUOT:
#endif
			return "no space left";
		default:
			return "system error";
	}
}

static void editorSetSaveFailureStatus(int saved_errno, int cleanup_errno) {
	const char *error_class = editorSaveFailureClass(saved_errno);
	const char *error_text = strerror(saved_errno);

	if (cleanup_errno != 0) {
		editorSetStatusMsg("Save failed: %s (%s); cleanup failed (%s)", error_class,
				error_text, strerror(cleanup_errno));
		return;
	}

	editorSetStatusMsg("Save failed: %s (%s)", error_class, error_text);
}

static mode_t editorDefaultCreateMode(void) {
	mode_t mask = umask(0);
	umask(mask);
	return 0644 & ~mask;
}

void editorSave(void) {
	if (editorActiveTabIsTaskLog()) {
		editorSetStatusMsg("Task logs cannot be saved");
		return;
	}

	if (E.filename == NULL) {
		if ((E.filename = editorPrompt("Save as: %s")) == NULL) {
			if (E.statusmsg[0] == '\0') {
				editorSetStatusMsg("Save aborted");
			}
			return;
		}
		(void)editorSyntaxParseFullActive();
	}

	size_t len = 0;
	errno = 0;
	char *buf = editorDupActiveTextSource(&len);
	char *tmp_path = editorTempPathForTarget(E.filename);
	int fd = -1;
	int dir_fd = -1;
	int tmp_created = 0;
	int tmp_renamed = 0;
	mode_t mode = editorDefaultCreateMode();
	struct stat st;

	if (buf == NULL && (len > 0 || errno != 0)) {
		free(tmp_path);
		if (errno == EOVERFLOW) {
			editorSetFileTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return;
	}

	if (stat(E.filename, &st) == 0) {
		mode = st.st_mode & 0777;
	}

	if (tmp_path == NULL) {
		free(buf);
		editorSetAllocFailureStatus();
		return;
	}

	// Write to a sibling temp file and atomically replace the target on success.
	// This avoids leaving a partially written file if the save path fails midway.
	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, mode) == -1) {
		goto err;
	}
	if (editorWriteAll(fd, buf, len) == -1) {
		goto err;
	}
	if (editorSaveFsync(fd) == -1) {
		goto err;
	}
	if (editorSaveClose(fd) == -1) {
		fd = -1;
		goto err;
	}
	fd = -1;
	if (editorSaveRename(tmp_path, E.filename) == -1) {
		goto err;
	}
	tmp_renamed = 1;

	dir_fd = editorOpenParentDirForTarget(E.filename);
	if (dir_fd == -1) {
		goto err;
	}
	if (editorSaveFsync(dir_fd) == -1) {
		goto err;
	}
	if (editorSaveClose(dir_fd) == -1) {
		dir_fd = -1;
		goto err;
	}
	dir_fd = -1;

	E.dirty = 0;
	free(tmp_path);
	free(buf);
	editorLspNotifyDidSaveActive();
	editorSetStatusMsg("%zu bytes written to disk", len);
	return;

err: {
	int saved_errno = errno;
	int cleanup_errno = 0;
	(void)editorSaveCleanupOnError(&fd, &dir_fd, tmp_path, tmp_created, tmp_renamed,
			&cleanup_errno);
	free(tmp_path);
	free(buf);
	editorSetSaveFailureStatus(saved_errno, cleanup_errno);
}
}

/*** Recovery ***/

#define ROTIDE_RECOVERY_MAGIC "RTRECOV1"
#define ROTIDE_RECOVERY_MAGIC_LEN 8
#define ROTIDE_RECOVERY_VERSION 2U
#define ROTIDE_RECOVERY_VERSION_ROWS 1U
#define ROTIDE_RECOVERY_AUTOSAVE_DEBOUNCE_SECONDS 5
#define ROTIDE_RECOVERY_MAX_FILENAME_BYTES 4096

struct editorRecoveryRow {
	char *chars;
	size_t len;
};

struct editorRecoveryTab {
	int cx;
	int cy;
	int rowoff;
	int coloff;
	char *filename;
	char *text;
	size_t textlen;
	int row_count;
	struct editorRecoveryRow *rows;
};

struct editorRecoverySession {
	int tab_count;
	int active_tab;
	struct editorRecoveryTab *tabs;
};

struct editorRecoveryTabView {
	int cx;
	int cy;
	int rowoff;
	int coloff;
	enum editorTabKind tab_kind;
	struct editorDocument *document;
	const char *filename;
};

struct editorRecoveryRowsTextSource {
	const struct editorRecoveryRow *rows;
	int row_count;
	size_t *row_starts;
	size_t total_len;
};

enum editorRecoveryLoadStatus {
	EDITOR_RECOVERY_LOAD_OK = 0,
	EDITOR_RECOVERY_LOAD_NOT_FOUND,
	EDITOR_RECOVERY_LOAD_INVALID,
	EDITOR_RECOVERY_LOAD_OOM,
	EDITOR_RECOVERY_LOAD_IO
};

enum editorReadExactStatus {
	EDITOR_READ_EXACT_OK = 1,
	EDITOR_READ_EXACT_EOF = 0,
	EDITOR_READ_EXACT_ERR = -1
};

static void editorRecoverySessionFree(struct editorRecoverySession *session) {
	if (session == NULL || session->tabs == NULL) {
		return;
	}

	for (int tab_idx = 0; tab_idx < session->tab_count; tab_idx++) {
		struct editorRecoveryTab *tab = &session->tabs[tab_idx];
		free(tab->text);
		tab->text = NULL;
		tab->textlen = 0;
		for (int row_idx = 0; row_idx < tab->row_count; row_idx++) {
			free(tab->rows[row_idx].chars);
			tab->rows[row_idx].chars = NULL;
			tab->rows[row_idx].len = 0;
		}
		free(tab->rows);
		tab->rows = NULL;
		tab->row_count = 0;
		free(tab->filename);
		tab->filename = NULL;
	}

	free(session->tabs);
	session->tabs = NULL;
	session->tab_count = 0;
	session->active_tab = 0;
}

static uint64_t editorRecoveryHashPath(const char *path) {
	uint64_t hash = UINT64_C(1469598103934665603);
	const unsigned char *p = (const unsigned char *)path;
	while (*p != '\0') {
		hash ^= (uint64_t)*p;
		hash *= UINT64_C(1099511628211);
		p++;
	}
	return hash;
}

static char *editorPathJoin(const char *left, const char *right) {
	size_t left_len = strlen(left);
	size_t right_len = strlen(right);
	size_t total = 0;
	if (!editorSizeAdd(left_len, 1, &total) ||
			!editorSizeAdd(total, right_len, &total) ||
			!editorSizeAdd(total, 1, &total)) {
		return NULL;
	}

	char *path = editorMalloc(total);
	if (path == NULL) {
		return NULL;
	}

	memcpy(path, left, left_len);
	path[left_len] = '/';
	memcpy(path + left_len + 1, right, right_len);
	path[left_len + 1 + right_len] = '\0';
	return path;
}

static int editorEnsureDirectoryExists(const char *path, mode_t mode) {
	if (mkdir(path, mode) == 0) {
		return 1;
	}
	if (errno != EEXIST) {
		return 0;
	}

	struct stat st;
	if (stat(path, &st) == -1) {
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static char *editorRecoveryBuildPathForBase(const char *base, uint64_t hash) {
	char name[128];
	int written = snprintf(name, sizeof(name), "rotide-recovery-u%lu-%016llx.swap",
			(unsigned long)getuid(), (unsigned long long)hash);
	if (written <= 0 || (size_t)written >= sizeof(name)) {
		return NULL;
	}
	return editorPathJoin(base, name);
}

static char *editorResolveRecoveryPath(void) {
	char *cwd = getcwd(NULL, 0);
	if (cwd == NULL) {
		cwd = strdup(".");
		if (cwd == NULL) {
			return NULL;
		}
	}

	uint64_t cwd_hash = editorRecoveryHashPath(cwd);
	free(cwd);

	char *recovery_path = NULL;
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *dot_rotide = editorPathJoin(home, ".rotide");
		char *recovery_dir = NULL;
		if (dot_rotide != NULL) {
			recovery_dir = editorPathJoin(dot_rotide, "recovery");
		}

		if (dot_rotide != NULL && recovery_dir != NULL &&
				editorEnsureDirectoryExists(dot_rotide, 0700) &&
				editorEnsureDirectoryExists(recovery_dir, 0700)) {
			recovery_path = editorRecoveryBuildPathForBase(recovery_dir, cwd_hash);
		}

		free(dot_rotide);
		free(recovery_dir);
	}

	if (recovery_path == NULL) {
		recovery_path = editorRecoveryBuildPathForBase("/tmp", cwd_hash);
	}

	return recovery_path;
}

int editorRecoveryInitForCurrentDir(void) {
	free(E.recovery_path);
	E.recovery_path = editorResolveRecoveryPath();
	E.recovery_last_autosave_time = 0;
	return E.recovery_path != NULL;
}

void editorRecoveryShutdown(void) {
	free(E.recovery_path);
	E.recovery_path = NULL;
	E.recovery_last_autosave_time = 0;
}

const char *editorRecoveryPath(void) {
	return E.recovery_path;
}

int editorRecoveryHasSnapshot(void) {
	if (E.recovery_path == NULL) {
		return 0;
	}
	struct stat st;
	if (stat(E.recovery_path, &st) == -1) {
		return 0;
	}
	return S_ISREG(st.st_mode);
}

void editorRecoveryCleanupOnCleanExit(void) {
	if (E.recovery_path == NULL) {
		return;
	}
	(void)unlink(E.recovery_path);
	E.recovery_last_autosave_time = 0;
}

static int editorRecoveryReadExact(int fd, void *buf, size_t len) {
	char *dst = (char *)buf;
	size_t total = 0;
	while (total < len) {
		ssize_t nread = read(fd, dst + total, len - total);
		if (nread == 0) {
			return EDITOR_READ_EXACT_EOF;
		}
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return EDITOR_READ_EXACT_ERR;
		}
		total += (size_t)nread;
	}
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryWriteU32(int fd, uint32_t value) {
	unsigned char bytes[4];
	bytes[0] = (unsigned char)(value & 0xFFU);
	bytes[1] = (unsigned char)((value >> 8) & 0xFFU);
	bytes[2] = (unsigned char)((value >> 16) & 0xFFU);
	bytes[3] = (unsigned char)((value >> 24) & 0xFFU);
	return editorWriteAll(fd, (const char *)bytes, sizeof(bytes)) == 0;
}

static int editorRecoveryWriteI32(int fd, int32_t value) {
	return editorRecoveryWriteU32(fd, (uint32_t)value);
}

static int editorRecoveryReadU32(int fd, uint32_t *value_out) {
	unsigned char bytes[4];
	int read_status = editorRecoveryReadExact(fd, bytes, sizeof(bytes));
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	*value_out = (uint32_t)bytes[0] |
			((uint32_t)bytes[1] << 8) |
			((uint32_t)bytes[2] << 16) |
			((uint32_t)bytes[3] << 24);
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryReadI32(int fd, int32_t *value_out) {
	uint32_t raw = 0;
	int read_status = editorRecoveryReadU32(fd, &raw);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	*value_out = (int32_t)raw;
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryGetTabView(int idx, struct editorRecoveryTabView *view_out) {
	if (view_out == NULL) {
		errno = EINVAL;
		return 0;
	}

	int tab_count = E.tab_count > 0 ? E.tab_count : 1;
	int active_tab = E.active_tab;
	if (tab_count == 1) {
		active_tab = 0;
	}
	if (idx < 0 || idx >= tab_count) {
		errno = EINVAL;
		return 0;
	}

	if (idx == active_tab) {
		if (editorTabKindSupportsDocument(E.tab_kind) &&
				(!editorDocumentMirrorEnsureActiveCurrent() || E.document == NULL)) {
			errno = EIO;
			return 0;
		}
		view_out->cx = E.cx;
		view_out->cy = E.cy;
		view_out->rowoff = E.rowoff;
		view_out->coloff = E.coloff;
		view_out->tab_kind = E.tab_kind;
		view_out->document = E.document;
		view_out->filename = E.filename;
		return 1;
	}

	struct editorTabState *tab = &E.tabs[idx];
	if (editorTabKindSupportsDocument(tab->tab_kind) &&
			(!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL)) {
		errno = EIO;
		return 0;
	}
	view_out->cx = tab->cx;
	view_out->cy = tab->cy;
	view_out->rowoff = tab->rowoff;
	view_out->coloff = tab->coloff;
	view_out->tab_kind = tab->tab_kind;
	view_out->document = tab->document;
	view_out->filename = tab->filename;
	return 1;
}

static void editorRecoveryRowsTextSourceFree(struct editorRecoveryRowsTextSource *state) {
	if (state == NULL) {
		return;
	}
	free(state->row_starts);
	state->row_starts = NULL;
	state->rows = NULL;
	state->row_count = 0;
	state->total_len = 0;
}

static const char *editorRecoveryRowsTextSourceRead(const struct editorTextSource *source,
		size_t byte_index, uint32_t *bytes_read) {
	static const char newline[] = "\n";
	const struct editorRecoveryRowsTextSource *state = source != NULL ? source->context : NULL;
	if (bytes_read == NULL) {
		return NULL;
	}
	*bytes_read = 0;
	if (state == NULL || state->row_starts == NULL || state->row_count < 0 ||
			byte_index >= state->total_len) {
		return NULL;
	}

	int lo = 0;
	int hi = state->row_count;
	while (lo + 1 < hi) {
		int mid = lo + (hi - lo) / 2;
		if (state->row_starts[mid] <= byte_index) {
			lo = mid;
		} else {
			hi = mid;
		}
	}

	int row_idx = lo;
	size_t row_start = state->row_starts[row_idx];
	size_t row_size = state->rows[row_idx].len;
	if (byte_index < row_start + row_size) {
		size_t remaining = row_size - (byte_index - row_start);
		if (remaining > UINT32_MAX) {
			remaining = UINT32_MAX;
		}
		*bytes_read = (uint32_t)remaining;
		return state->rows[row_idx].chars + (byte_index - row_start);
	}
	if (byte_index == row_start + row_size) {
		*bytes_read = 1;
		return newline;
	}
	return NULL;
}

static int editorRecoveryRowsBuildTextSource(const struct editorRecoveryRow *rows, int row_count,
		struct editorRecoveryRowsTextSource *state_out, struct editorTextSource *source_out) {
	if (state_out == NULL || source_out == NULL || row_count < 0 ||
			(row_count > 0 && rows == NULL)) {
		return 0;
	}
	memset(state_out, 0, sizeof(*state_out));
	memset(source_out, 0, sizeof(*source_out));

	size_t total = 0;
	size_t starts_count = 0;
	size_t starts_bytes = 0;
	if (!editorIntToSize(row_count + 1, &starts_count) ||
			!editorSizeMul(sizeof(*state_out->row_starts), starts_count, &starts_bytes)) {
		errno = EOVERFLOW;
		return 0;
	}
	state_out->row_starts = editorMalloc(starts_bytes);
	if (state_out->row_starts == NULL) {
		errno = ENOMEM;
		return 0;
	}

	for (int row_idx = 0; row_idx < row_count; row_idx++) {
		size_t row_total = 0;
		state_out->row_starts[row_idx] = total;
		if (!editorSizeAdd(rows[row_idx].len, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(total, row_total, &total) ||
				total > ROTIDE_MAX_TEXT_BYTES) {
			editorRecoveryRowsTextSourceFree(state_out);
			errno = EOVERFLOW;
			return 0;
		}
	}
	state_out->row_starts[row_count] = total;
	state_out->rows = rows;
	state_out->row_count = row_count;
	state_out->total_len = total;

	source_out->read = editorRecoveryRowsTextSourceRead;
	source_out->context = state_out;
	source_out->length = total;
	errno = 0;
	return 1;
}

static char *editorRecoveryDupTabText(const struct editorRecoveryTabView *view, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (view == NULL) {
		errno = EINVAL;
		return NULL;
	}
	if (view->document == NULL) {
		errno = EIO;
		return NULL;
	}

	size_t text_len = editorDocumentLength(view->document);
	if (text_len > ROTIDE_MAX_TEXT_BYTES) {
		errno = EOVERFLOW;
		return NULL;
	}
	errno = 0;
	return editorDocumentDupRange(view->document, 0, text_len, len_out);
}

static int editorRecoveryWriteSessionToFd(int fd) {
	int tab_count = E.tab_count > 0 ? E.tab_count : 1;
	int active_tab = E.active_tab;
	if (tab_count == 1) {
		active_tab = 0;
	}
	if (tab_count < 1 || tab_count > ROTIDE_MAX_TABS ||
			active_tab < 0 || active_tab >= tab_count) {
		errno = EINVAL;
		return 0;
	}

	if (editorWriteAll(fd, ROTIDE_RECOVERY_MAGIC, ROTIDE_RECOVERY_MAGIC_LEN) == -1 ||
			!editorRecoveryWriteU32(fd, ROTIDE_RECOVERY_VERSION) ||
			!editorRecoveryWriteU32(fd, (uint32_t)tab_count) ||
			!editorRecoveryWriteU32(fd, (uint32_t)active_tab)) {
		if (errno == 0) {
			errno = EIO;
		}
		return 0;
	}

	for (int tab_idx = 0; tab_idx < tab_count; tab_idx++) {
		struct editorRecoveryTabView view;
		if (!editorRecoveryGetTabView(tab_idx, &view)) {
			return 0;
		}

		size_t filename_len = 0;
		if (view.filename != NULL) {
			filename_len = strlen(view.filename);
			if (filename_len > ROTIDE_RECOVERY_MAX_FILENAME_BYTES) {
				errno = EOVERFLOW;
				return 0;
			}
		}

		if (!editorRecoveryWriteI32(fd, (int32_t)view.cx) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.cy) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.rowoff) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.coloff) ||
				!editorRecoveryWriteU32(fd, (uint32_t)filename_len)) {
			if (errno == 0) {
				errno = EIO;
			}
			return 0;
		}
		if (filename_len > 0 && editorWriteAll(fd, view.filename, filename_len) == -1) {
			return 0;
		}

		size_t text_len = 0;
		char *text = editorRecoveryDupTabText(&view, &text_len);
		if (text == NULL && view.document != NULL && editorDocumentLength(view.document) > 0) {
			return 0;
		}
		if (!editorRecoveryWriteU32(fd, (uint32_t)text_len)) {
			free(text);
			if (errno == 0) {
				errno = EIO;
			}
			return 0;
		}
		if (text_len > 0 && editorWriteAll(fd, text, text_len) == -1) {
			free(text);
			return 0;
		}
		free(text);
	}

	return 1;
}

static int editorRecoveryWriteSnapshotAtomic(void) {
	if (E.recovery_path == NULL) {
		errno = EINVAL;
		return 0;
	}

	char *tmp_path = editorTempPathForTarget(E.recovery_path);
	if (tmp_path == NULL) {
		errno = ENOMEM;
		return 0;
	}

	int fd = -1;
	int tmp_created = 0;
	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, 0600) == -1) {
		goto err;
	}
	if (!editorRecoveryWriteSessionToFd(fd)) {
		if (errno == 0) {
			errno = EIO;
		}
		goto err;
	}
	if (fsync(fd) == -1) {
		goto err;
	}
	if (close(fd) == -1) {
		fd = -1;
		goto err;
	}
	fd = -1;

	if (rename(tmp_path, E.recovery_path) == -1) {
		goto err;
	}

	free(tmp_path);
	return 1;

err: {
	int saved_errno = errno;
	if (fd != -1) {
		(void)close(fd);
	}
	if (tmp_created) {
		(void)unlink(tmp_path);
	}
	free(tmp_path);
	errno = saved_errno;
	return 0;
}
}

static int editorRecoveryTabReadRows(int fd, struct editorRecoveryTab *tab) {
	uint32_t row_count_u32 = 0;
	int read_status = editorRecoveryReadU32(fd, &row_count_u32);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	if (row_count_u32 > (uint32_t)INT_MAX) {
		return EDITOR_READ_EXACT_EOF;
	}

	tab->row_count = (int)row_count_u32;
	if (tab->row_count == 0) {
		tab->rows = NULL;
		return EDITOR_READ_EXACT_OK;
	}

	size_t rows_bytes = 0;
	if (!editorSizeMul(sizeof(struct editorRecoveryRow), (size_t)tab->row_count, &rows_bytes)) {
		return EDITOR_READ_EXACT_EOF;
	}
	tab->rows = editorMalloc(rows_bytes);
	if (tab->rows == NULL) {
		errno = ENOMEM;
		return EDITOR_READ_EXACT_ERR;
	}
	memset(tab->rows, 0, rows_bytes);

	size_t total_bytes = 0;
	for (int row_idx = 0; row_idx < tab->row_count; row_idx++) {
		uint32_t row_len_u32 = 0;
		read_status = editorRecoveryReadU32(fd, &row_len_u32);
		if (read_status != EDITOR_READ_EXACT_OK) {
			return read_status;
		}
		if (row_len_u32 > (uint32_t)INT_MAX) {
			return EDITOR_READ_EXACT_EOF;
		}

		size_t row_len = (size_t)row_len_u32;
		size_t row_total = 0;
		if (!editorSizeAdd(row_len, NEWLINE_CHAR_WIDTH, &row_total) ||
				!editorSizeAdd(total_bytes, row_total, &total_bytes) ||
				total_bytes > ROTIDE_MAX_TEXT_BYTES) {
			return EDITOR_READ_EXACT_EOF;
		}

		size_t row_alloc = 0;
		if (!editorSizeAdd(row_len, 1, &row_alloc)) {
			return EDITOR_READ_EXACT_EOF;
		}
		tab->rows[row_idx].chars = editorMalloc(row_alloc);
		if (tab->rows[row_idx].chars == NULL) {
			errno = ENOMEM;
			return EDITOR_READ_EXACT_ERR;
		}
		if (row_len > 0) {
			read_status = editorRecoveryReadExact(fd, tab->rows[row_idx].chars, row_len);
			if (read_status != EDITOR_READ_EXACT_OK) {
				return read_status;
			}
		}
		tab->rows[row_idx].chars[row_len] = '\0';
		tab->rows[row_idx].len = row_len;
	}

	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryTabReadText(int fd, struct editorRecoveryTab *tab) {
	uint32_t text_len_u32 = 0;
	int read_status = editorRecoveryReadU32(fd, &text_len_u32);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}

	size_t text_len = (size_t)text_len_u32;
	size_t alloc = 0;
	if (!editorSizeAdd(text_len, 1, &alloc)) {
		return EDITOR_READ_EXACT_EOF;
	}
	tab->text = editorMalloc(alloc);
	if (tab->text == NULL) {
		errno = ENOMEM;
		return EDITOR_READ_EXACT_ERR;
	}
	if (text_len > 0) {
		read_status = editorRecoveryReadExact(fd, tab->text, text_len);
		if (read_status != EDITOR_READ_EXACT_OK) {
			return read_status;
		}
	}
	tab->text[text_len] = '\0';
	tab->textlen = text_len;
	return EDITOR_READ_EXACT_OK;
}

static enum editorRecoveryLoadStatus editorRecoveryLoadSessionFromPath(const char *path,
		struct editorRecoverySession *session_out) {
	memset(session_out, 0, sizeof(*session_out));

	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT) {
			return EDITOR_RECOVERY_LOAD_NOT_FOUND;
		}
		return EDITOR_RECOVERY_LOAD_IO;
	}

	enum editorRecoveryLoadStatus status = EDITOR_RECOVERY_LOAD_INVALID;
	char magic[ROTIDE_RECOVERY_MAGIC_LEN];
	int read_status = editorRecoveryReadExact(fd, magic, sizeof(magic));
	if (read_status != EDITOR_READ_EXACT_OK) {
		status = read_status == EDITOR_READ_EXACT_ERR ? EDITOR_RECOVERY_LOAD_IO :
				EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (memcmp(magic, ROTIDE_RECOVERY_MAGIC, ROTIDE_RECOVERY_MAGIC_LEN) != 0) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}

	uint32_t version = 0;
	uint32_t tab_count_u32 = 0;
	uint32_t active_tab_u32 = 0;
	if (editorRecoveryReadU32(fd, &version) != EDITOR_READ_EXACT_OK ||
			editorRecoveryReadU32(fd, &tab_count_u32) != EDITOR_READ_EXACT_OK ||
			editorRecoveryReadU32(fd, &active_tab_u32) != EDITOR_READ_EXACT_OK) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if ((version != ROTIDE_RECOVERY_VERSION &&
				version != ROTIDE_RECOVERY_VERSION_ROWS) ||
			tab_count_u32 < 1 ||
			tab_count_u32 > ROTIDE_MAX_TABS ||
			active_tab_u32 >= tab_count_u32) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}

	session_out->tab_count = (int)tab_count_u32;
	session_out->active_tab = (int)active_tab_u32;
	size_t tabs_bytes = 0;
	if (!editorSizeMul(sizeof(struct editorRecoveryTab), (size_t)session_out->tab_count,
				&tabs_bytes)) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	session_out->tabs = editorMalloc(tabs_bytes);
	if (session_out->tabs == NULL) {
		status = EDITOR_RECOVERY_LOAD_OOM;
		goto out;
	}
	memset(session_out->tabs, 0, tabs_bytes);

	for (int tab_idx = 0; tab_idx < session_out->tab_count; tab_idx++) {
		struct editorRecoveryTab *tab = &session_out->tabs[tab_idx];
		int32_t cx = 0;
		int32_t cy = 0;
		int32_t rowoff = 0;
		int32_t coloff = 0;
		if (editorRecoveryReadI32(fd, &cx) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &cy) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &rowoff) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &coloff) != EDITOR_READ_EXACT_OK) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		tab->cx = (int)cx;
		tab->cy = (int)cy;
		tab->rowoff = (int)rowoff;
		tab->coloff = (int)coloff;

		uint32_t filename_len_u32 = 0;
		if (editorRecoveryReadU32(fd, &filename_len_u32) != EDITOR_READ_EXACT_OK) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		if (filename_len_u32 > ROTIDE_RECOVERY_MAX_FILENAME_BYTES) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		if (filename_len_u32 > 0) {
			size_t filename_len = (size_t)filename_len_u32;
			size_t filename_alloc = 0;
			if (!editorSizeAdd(filename_len, 1, &filename_alloc)) {
				status = EDITOR_RECOVERY_LOAD_INVALID;
				goto out;
			}
			tab->filename = editorMalloc(filename_alloc);
			if (tab->filename == NULL) {
				status = EDITOR_RECOVERY_LOAD_OOM;
				goto out;
			}
			read_status = editorRecoveryReadExact(fd, tab->filename, filename_len);
			if (read_status != EDITOR_READ_EXACT_OK) {
				status = read_status == EDITOR_READ_EXACT_ERR ? EDITOR_RECOVERY_LOAD_IO :
						EDITOR_RECOVERY_LOAD_INVALID;
				goto out;
			}
			tab->filename[filename_len] = '\0';
		}

		if (version == ROTIDE_RECOVERY_VERSION_ROWS) {
			read_status = editorRecoveryTabReadRows(fd, tab);
		} else {
			read_status = editorRecoveryTabReadText(fd, tab);
		}
		if (read_status != EDITOR_READ_EXACT_OK) {
			if (read_status == EDITOR_READ_EXACT_ERR) {
				status = errno == ENOMEM ? EDITOR_RECOVERY_LOAD_OOM :
						EDITOR_RECOVERY_LOAD_IO;
			} else {
				status = EDITOR_RECOVERY_LOAD_INVALID;
			}
			goto out;
		}
	}

	char trailing = '\0';
	ssize_t trailing_read;
	do {
		trailing_read = read(fd, &trailing, 1);
	} while (trailing_read == -1 && errno == EINTR);
	if (trailing_read == 1) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (trailing_read == -1) {
		status = EDITOR_RECOVERY_LOAD_IO;
		goto out;
	}

	status = EDITOR_RECOVERY_LOAD_OK;

out:
	if (close(fd) == -1 && status == EDITOR_RECOVERY_LOAD_OK) {
		status = EDITOR_RECOVERY_LOAD_IO;
	}
	if (status != EDITOR_RECOVERY_LOAD_OK) {
		editorRecoverySessionFree(session_out);
	}
	return status;
}

static void editorRecoveryClampActiveCursorAndScroll(const struct editorRecoveryTab *tab) {
	if (tab->cy < 0) {
		E.cy = 0;
	} else {
		E.cy = tab->cy;
	}
	if (E.numrows == 0) {
		E.cy = 0;
		E.cx = 0;
		E.rowoff = 0;
		E.coloff = tab->coloff < 0 ? 0 : tab->coloff;
		return;
	}
	if (E.cy >= E.numrows) {
		E.cy = E.numrows - 1;
	}

	struct erow *row = &E.rows[E.cy];
	int target_cx = tab->cx;
	if (target_cx < 0) {
		target_cx = 0;
	}
	if (target_cx > row->size) {
		target_cx = row->size;
	}
	E.cx = editorRowClampCxToClusterBoundary(row, target_cx);

	if (tab->rowoff < 0) {
		E.rowoff = 0;
	} else {
		E.rowoff = tab->rowoff;
	}
	if (E.rowoff >= E.numrows) {
		E.rowoff = E.numrows - 1;
	}
	E.coloff = tab->coloff < 0 ? 0 : tab->coloff;
}

static int editorRecoveryPopulateActiveFromTab(const struct editorRecoveryTab *tab) {
	struct editorDocument document;
	int document_inited = 0;
	int ok = 0;
	free(E.filename);
	E.filename = NULL;
	if (tab->filename != NULL) {
		E.filename = strdup(tab->filename);
		if (E.filename == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
	}

	editorDocumentInit(&document);
	document_inited = 1;
	if (tab->text != NULL) {
		if (!editorDocumentResetFromString(&document, tab->text, tab->textlen)) {
			editorSetAllocFailureStatus();
			goto cleanup;
		}
	} else {
		struct editorRecoveryRowsTextSource state;
		struct editorTextSource source = {0};
		if (!editorRecoveryRowsBuildTextSource(tab->rows, tab->row_count, &state, &source) ||
				!editorDocumentResetFromTextSource(&document, &source)) {
			editorRecoveryRowsTextSourceFree(&state);
			editorSetAllocFailureStatus();
			goto cleanup;
		}
		editorRecoveryRowsTextSourceFree(&state);
	}

	if (!editorRestoreActiveFromDocument(&document, tab->cy, tab->cx, 1, 1)) {
		goto cleanup;
	}

	editorRecoveryClampActiveCursorAndScroll(tab);
	if (!editorBufferPosToOffset(E.cy, E.cx, &E.cursor_offset)) {
		E.cursor_offset = 0;
	}
	E.preferred_rx = 0;
	ok = 1;

cleanup:
	if (document_inited) {
		editorDocumentFree(&document);
	}
	return ok;
}

static int editorRecoveryApplySession(const struct editorRecoverySession *session) {
	if (session == NULL || session->tab_count < 1) {
		return 0;
	}

	if (!editorTabsInit()) {
		return 0;
	}

	for (int tab_idx = 0; tab_idx < session->tab_count; tab_idx++) {
		if (tab_idx > 0 && !editorTabNewEmpty()) {
			(void)editorTabsInit();
			return 0;
		}

		if (!editorRecoveryPopulateActiveFromTab(&session->tabs[tab_idx])) {
			(void)editorTabsInit();
			return 0;
		}
	}

	if (!editorTabSwitchToIndex(session->active_tab)) {
		(void)editorTabsInit();
		return 0;
	}

	return 1;
}

int editorRecoveryRestoreSnapshot(void) {
	if (E.recovery_path == NULL) {
		return 0;
	}

	struct editorRecoverySession session;
	enum editorRecoveryLoadStatus load_status =
			editorRecoveryLoadSessionFromPath(E.recovery_path, &session);
	if (load_status == EDITOR_RECOVERY_LOAD_NOT_FOUND) {
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_OOM) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_IO) {
		editorSetStatusMsg("Recovery load failed (%s)", strerror(errno));
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_INVALID) {
		(void)unlink(E.recovery_path);
		editorSetStatusMsg("Recovery data was invalid and was discarded");
		return 0;
	}

	int applied = editorRecoveryApplySession(&session);
	editorRecoverySessionFree(&session);
	if (!applied) {
		return 0;
	}

	editorSetStatusMsg("Recovered previous session");
	return 1;
}

static int editorRecoveryPromptRestoreChoice(void) {
	while (1) {
		editorSetStatusMsg("Recovery data found. Restore previous session? (y/N)");
		editorRefreshScreen();

		int key = editorReadKey();
		if (key == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (key == MOUSE_EVENT) {
			continue;
		}
		if (key == INPUT_EOF_EVENT) {
			return 0;
		}
		if (key == 'y' || key == 'Y') {
			return 1;
		}
		if (key == 'n' || key == 'N' || key == '\x1b' || key == '\r') {
			return 0;
		}
	}
}

int editorRecoveryPromptAndMaybeRestore(void) {
	if (!editorRecoveryHasSnapshot()) {
		return 0;
	}

	if (editorRecoveryPromptRestoreChoice()) {
		return editorRecoveryRestoreSnapshot();
	}

	editorRecoveryCleanupOnCleanExit();
	editorSetStatusMsg("Discarded recovery data");
	return 0;
}

void editorRecoveryMaybeAutosaveOnActivity(void) {
	if (E.recovery_path == NULL) {
		return;
	}

	if (!editorTabAnyDirty()) {
		editorRecoveryCleanupOnCleanExit();
		return;
	}

	time_t now = time(NULL);
	if (now != (time_t)-1 &&
			E.recovery_last_autosave_time != 0 &&
			now - E.recovery_last_autosave_time < ROTIDE_RECOVERY_AUTOSAVE_DEBOUNCE_SECONDS) {
		return;
	}

	if (!editorRecoveryWriteSnapshotAtomic()) {
		int saved_errno = errno;
		if (saved_errno != 0) {
			editorSetStatusMsg("Recovery autosave failed (%s)", strerror(saved_errno));
		} else {
			editorSetStatusMsg("Recovery autosave failed");
		}
		if (now != (time_t)-1) {
			E.recovery_last_autosave_time = now;
		}
		return;
	}

	if (now != (time_t)-1) {
		E.recovery_last_autosave_time = now;
	}
}

int editorStartupLoadRecoveryOrOpenArgs(int argc, char *argv[]) {
	int restored = editorRecoveryPromptAndMaybeRestore();
	if (!restored && argc >= 2) {
		editorOpen(argv[1]);
		for (int i = 2; i < argc; i++) {
			if (!editorTabOpenFileAsNew(argv[i])) {
				break;
			}
		}
		(void)editorTabSwitchToIndex(0);
	}
	return restored;
}
