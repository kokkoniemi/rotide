#ifndef ROTIDE_RENDER_SCREEN_H
#define ROTIDE_RENDER_SCREEN_H

#include "render/viewport.h"
#include "render/write_buf.h"
#include "rotide.h"

void editorRefreshScreen(void);
int editorAppendGrayBytes(struct writeBuf *wb, const char *text, size_t len);
int editorCurrentLineHighlightApplies(int row_idx, int segment_coloff);
int editorDebugStoppedLineHighlightApplies(int row_idx);
int editorDrawLineNumberGutter(struct writeBuf *wb, int row_idx, int segment_coloff,
                               int gutter_cols);
int editorDrawFileRowWrapped(struct writeBuf *wb, size_t i, int text_cols, int segment_coloff);
int editorDrawFileRow(struct writeBuf *wb, size_t i, int text_cols);
int editorCursorTerminalPosition(int *terminal_row_out, int *terminal_col_out);
int editorGitBlameIndicatorHitTest(int screen_row, int screen_col, int *row_out,
                                   int *anchor_col_out);
int editorGitBlameIndicatorTestRange(int *screen_y_out, int *start_col_out, int *end_col_out,
                                     int *anchor_col_out);
void editorOutputTestResetFrameCache(void);
int editorOutputTestLastRefreshFileRowDrawCount(void);

#endif
