#ifndef RENDER_TERMINAL_VIEW_H
#define RENDER_TERMINAL_VIEW_H

#include "terminal/terminal_pane.h"
#include "render/write_buf.h"

int editorDrawTerminalCells(struct writeBuf *wb, struct editorTerminalPane *terminal,
		int row_in_pane, int col_in_pane, int slice_cols);

#endif
