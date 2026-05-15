#ifndef RENDER_STATUS_BAR_H
#define RENDER_STATUS_BAR_H

#include "render/write_buf.h"

int editorDrawStatusBar(struct writeBuf *wb, int scroll_progress_percent);
int editorDrawMessageBar(struct writeBuf *wb);

#endif
