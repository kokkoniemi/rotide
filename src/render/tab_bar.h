#ifndef ROTIDE_RENDER_TAB_BAR_H
#define ROTIDE_RENDER_TAB_BAR_H

#include "render/write_buf.h"

struct editorPaneNode;

int editorDrawTabBar(struct writeBuf *wb);
int editorDrawPaneTabStrip(struct writeBuf *wb, struct editorPaneNode *leaf, int cols);
int editorDrawTabSlots(struct writeBuf *wb, int cols);

#endif
