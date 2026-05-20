#ifndef RENDER_STATUS_BAR_H
#define RENDER_STATUS_BAR_H

#include "render/write_buf.h"

int editorDrawStatusBar(struct writeBuf *wb, int scroll_progress_percent);
int editorDrawMessageBar(struct writeBuf *wb);
int editorDrawDiagnosticPopdownMessage(struct writeBuf *wb, const char *message,
                                       int cursor_screen_row, int cursor_screen_col,
                                       int *screen_top_out, int *row_count_out);

#endif
