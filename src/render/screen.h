#ifndef OUTPUT_H
#define OUTPUT_H

#include "rotide.h"
#include "render/write_buf.h"

void editorRefreshScreen(void);
void editorViewportSetMode(enum editorViewportMode mode);
void editorViewportScrollByRows(int delta_rows);
void editorViewportScrollByCols(int delta_cols);
void editorViewportEnsureCursorVisible(void);
void editorViewportCenterCursor(void);
int editorViewportTextScreenRowToBufferRow(int screen_row, int *row_idx_out,
		int *segment_coloff_out);
int editorViewportTextScreenRowToBufferPosition(int screen_row, int *row_idx_out,
		int *segment_coloff_out, int *segment_indent_cols_out);
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
