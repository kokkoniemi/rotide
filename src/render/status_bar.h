#ifndef ROTIDE_RENDER_STATUS_BAR_H
#define ROTIDE_RENDER_STATUS_BAR_H

#include "render/write_buf.h"

int editorDrawStatusBar(struct writeBuf *wb, int scroll_progress_percent);

/*
 * If `col` (0-based column within the status row) falls on a debug-control
 * button recorded by the most recent editorDrawStatusBar, writes the button's
 * editorAction (as int) to *action_out and returns 1; otherwise returns 0.
 */
int editorStatusBarDebugButtonAt(int col, int *action_out);
int editorDrawMessageBar(struct writeBuf *wb);
int editorDrawDiagnosticPopdownMessage(struct writeBuf *wb, const char *message,
                                       int cursor_screen_row, int cursor_screen_col,
                                       int *screen_top_out, int *row_count_out);

#endif
