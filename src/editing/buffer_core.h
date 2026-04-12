#ifndef BUFFER_H
#define BUFFER_H

#include "rotide.h"
#include <stddef.h>

char *editorRowsToStr(size_t *buflen);
int editorBuildActiveTextSource(struct editorTextSource *source_out);
int editorBufferPosToOffset(int cy, int cx, size_t *offset_out);
int editorBufferOffsetToPos(size_t offset, int *cy_out, int *cx_out);
int editorBufferLineByteRange(int row_idx, size_t *start_byte_out, size_t *end_byte_out);
int editorBufferFindForward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col);
int editorBufferFindBackward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col);

int editorSyntaxEnabled(void);
int editorSyntaxTreeExists(void);
enum editorSyntaxLanguage editorSyntaxLanguageActive(void);
const char *editorSyntaxRootType(void);
int editorSyntaxPrepareVisibleRowSpans(int first_row, int row_count);
int editorSyntaxRowRenderSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
		int *count_out);
void editorSyntaxTestResetVisibleRowRecomputeCount(void);
int editorSyntaxTestVisibleRowRecomputeCount(void);
int editorBufferMaxRenderCols(void);

#endif
