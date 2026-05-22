#ifndef ROTIDE_RENDER_TAB_BAR_H
#define ROTIDE_RENDER_TAB_BAR_H

#include "render/write_buf.h"

struct editorPaneNode;

int editorDrawTabBar(struct writeBuf *wb);
/* `trailing_hborder` fills cols beyond the last visible tab with `─` instead
 * of spaces — used when the strip occupies a horizontal-split border row so
 * the line still reads as a split divider past the tabs. */
int editorDrawPaneTabStrip(struct writeBuf *wb, struct editorPaneNode *leaf, int cols,
                           int trailing_hborder);
int editorDrawTabSlots(struct writeBuf *wb, int cols);

#endif
