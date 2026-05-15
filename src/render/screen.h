#ifndef OUTPUT_H
#define OUTPUT_H

#include "rotide.h"
#include "render/viewport.h"
#include "render/write_buf.h"

void editorRefreshScreen(void);
int editorAppendGrayBytes(struct writeBuf *wb, const char *text, size_t len);
int editorCurrentLineHighlightApplies(int row_idx, int segment_coloff);
int editorDrawLineNumberGutter(struct writeBuf *wb, int row_idx, int segment_coloff,
		int gutter_cols);
int editorDrawFileRowWrapped(struct writeBuf *wb, size_t i, int text_cols,
		int segment_coloff);
int editorDrawFileRow(struct writeBuf *wb, size_t i, int text_cols);
void editorOutputTestResetFrameCache(void);
int editorOutputTestLastRefreshFileRowDrawCount(void);

#endif
