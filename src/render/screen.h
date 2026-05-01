#ifndef OUTPUT_H
#define OUTPUT_H

#include "rotide.h"

void editorRefreshScreen(void);
void editorViewportSetMode(enum editorViewportMode mode);
void editorViewportScrollByRows(int delta_rows);
void editorViewportScrollByCols(int delta_cols);
void editorViewportEnsureCursorVisible(void);
int editorViewportTextScreenRowToBufferRow(int screen_row, int *row_idx_out,
		int *segment_coloff_out);
void editorOutputTestResetFrameCache(void);
int editorOutputTestLastRefreshFileRowDrawCount(void);

#endif
