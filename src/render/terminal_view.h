#ifndef ROTIDE_RENDER_TERMINAL_VIEW_H
#define ROTIDE_RENDER_TERMINAL_VIEW_H

#include "render/write_buf.h"
#include "terminal/terminal_pane.h"

int editorDrawTerminalCells(struct writeBuf *wb, struct editorTerminalPane *terminal,
                            int row_in_pane, int col_in_pane, int slice_cols);

#endif
