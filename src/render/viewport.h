#ifndef RENDER_VIEWPORT_H
#define RENDER_VIEWPORT_H

#include "rotide.h"

int editorViewportFocusedPaneBodyRows(void);
int editorViewportFocusedPaneTextBodyCols(void);
int editorViewportTextScreenRowToBufferRow(int screen_row, int *row_idx_out,
		int *segment_coloff_out);
int editorViewportTextScreenRowToBufferPosition(int screen_row, int *row_idx_out,
		int *segment_coloff_out, int *segment_indent_cols_out);
void editorViewportSetMode(enum editorViewportMode mode);
void editorViewportScrollByRows(int delta_rows);
void editorViewportScrollByCols(int delta_cols);
void editorViewportEnsureCursorVisible(void);
void editorViewportCenterCursor(void);
void editorViewportUpdateForFrame(void);

#endif
