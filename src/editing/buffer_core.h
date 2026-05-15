#ifndef BUFFER_H
#define BUFFER_H

#include "rotide.h"
#include "editing/buffer_search.h"
#include "editing/document_bridge.h"
#include "editing/document_position.h"
#include "editing/row_cache.h"
#include "editing/text_source.h"
#include <stddef.h>

/* Canonical edit descriptor for text mutations.
 * All normal editing, undo, redo, syntax updates, LSP notifications, cursor
 * synchronization, and dirty-state transitions route through this shape.
 */
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

char *editorRowsToStr(size_t *buflen);

int editorSyntaxEnabled(void);
int editorSyntaxTreeExists(void);
enum editorSyntaxLanguage editorSyntaxLanguageActive(void);
const char *editorSyntaxRootType(void);
int editorSyntaxPrepareVisibleRowSpans(int first_row, int row_count);
int editorSyntaxPrepareVisibleRowSpansForeground(int first_row, int row_count);
int editorSyntaxRowRenderSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
		int *count_out);
int editorSyntaxBackgroundPoll(void);
int editorSyntaxBackgroundFlushForTests(void);
void editorSyntaxTestResetVisibleRowRecomputeCount(void);
int editorSyntaxTestVisibleRowRecomputeCount(void);
int editorBufferMaxRenderCols(void);

void editorSetAllocFailureStatus(void);
void editorSetOperationTooLargeStatus(void);
void editorSetFileTooLargeStatus(void);
int editorSyntaxParseFullActive(void);
void editorLspNotifyDidCloseTabState(struct editorTabState *tab);
void editorLspNotifyDidSaveActive(void);
void editorSyntaxVisibleCacheInvalidate(void);
void editorSyntaxVisibleCacheFree(void);
int editorRestoreActiveFromDocument(const struct editorDocument *document,
		int target_cy, int target_cx, int dirty, int parse_syntax);

/* The single active-buffer mutation path. */
int editorApplyDocumentEdit(const struct editorDocumentEdit *edit);

#endif
