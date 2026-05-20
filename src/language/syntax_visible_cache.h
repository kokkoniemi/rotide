#ifndef ROTIDE_LANGUAGE_SYNTAX_VISIBLE_CACHE_H
#define ROTIDE_LANGUAGE_SYNTAX_VISIBLE_CACHE_H

#include "language/syntax.h"
#include "language/syntax_worker.h"

int editorSyntaxVisibleCacheScheduleBackground(int first_row, int row_count);
int editorSyntaxVisibleCacheStoreBackgroundResult(const struct editorSyntaxWorkerResult *result);
int editorSyntaxVisibleCacheInvalidateChangedRowsFromState(void);
void editorSyntaxVisibleCacheInvalidateRowsForEdit(const struct editorSyntaxEdit *edit);

void editorSyntaxVisibleCacheInvalidate(void);
void editorSyntaxVisibleCacheFree(void);
int editorSyntaxPrepareVisibleRowSpans(int first_row, int row_count);
int editorSyntaxPrepareVisibleRowSpansForeground(int first_row, int row_count);
void editorSyntaxTestResetVisibleRowRecomputeCount(void);
int editorSyntaxTestVisibleRowRecomputeCount(void);
int editorSyntaxRowRenderSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
                               int *count_out);

#endif
