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
int editorResolveMouseToBufferOffset(const struct editorMouseEvent *event,
		int clamp_to_viewport, size_t *offset_out);
int editorMoveCursorToMouse(const struct editorMouseEvent *event, int clamp_to_viewport);
int editorHandleMouseMotion(const struct editorMouseEvent *event);
int editorHandleMouseLeftDrag(const struct editorMouseEvent *event);
int editorHandleMouseLeftRelease(void);
int editorHandleMouseTopRowTabClick(const struct editorMouseEvent *event, long long now_ms);
int editorDrawerHeaderModeForColumn(int mouse_col, int drawer_cols,
		enum editorDrawerMode *mode_out);

#endif
