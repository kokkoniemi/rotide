#ifndef RENDER_DISPLAY_TEXT_H
#define RENDER_DISPLAY_TEXT_H

#include "render/write_buf.h"

int editorDisplayTextCols(const char *text);
void editorDisplayWrapNextLine(const char *text, int text_len, int start_idx, int max_cols,
		int *end_idx_out, int *cols_out);
int editorDisplayWrapLineCount(const char *text, int max_cols);

int editorAppendDisplayPrefix(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out);
int editorAppendDisplaySuffix(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out);
int editorAppendDisplaySlice(struct writeBuf *wb, const char *text, int start_col, int max_cols,
		int *written_cols_out);

char *editorSanitizeTextRangeDup(const char *text, int text_len, int *cols_out);
char *editorSanitizeTextDup(const char *text, int *cols_out);
char *editorSanitizeDiagnosticMessageDup(const char *text, int *cols_out);

int editorAppendSanitizedText(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out);
int editorAppendSanitizedMiddleTruncated(struct writeBuf *wb, const char *text, int max_cols,
		int *written_cols_out);
int editorAppendSanitizedStatusPath(struct writeBuf *wb, const char *path, int max_cols,
		int *written_cols_out);

#endif
