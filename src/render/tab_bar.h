#ifndef RENDER_TAB_BAR_H
#define RENDER_TAB_BAR_H

#include "render/write_buf.h"

int editorDrawTabBar(struct writeBuf *wb);
int editorDrawTabSlots(struct writeBuf *wb, int cols);

#endif
