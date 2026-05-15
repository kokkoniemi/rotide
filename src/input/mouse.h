#ifndef ROTIDE_INPUT_MOUSE_H
#define ROTIDE_INPUT_MOUSE_H

#include "rotide.h"

void editorResetDrawerClickTracking(void);
void editorResetTextClickTracking(void);
void editorResetTabClickTracking(void);

int editorMouseIsOverDrawer(const struct editorMouseEvent *event);
int editorClearHoverLinkState(void);
int editorHandleMouseWheel(const struct editorMouseEvent *event);
int editorHandleMouseEventInTerminalPane(const struct editorMouseEvent *event);

#endif
