#ifndef RENDER_DRAWER_VIEW_H
#define RENDER_DRAWER_VIEW_H

#include "render/write_buf.h"

int editorDrawDrawerSeparatorCell(struct writeBuf *wb, int separator_cols);
int editorDrawDrawerSelectionOverflow(struct writeBuf *wb, int row_idx, int drawer_cols,
		int separator_cols, int text_cols, int terminal_row, int *overlay_drawn_out);
int editorDrawDrawerRow(struct writeBuf *wb, int row_idx, int drawer_cols);

#endif
