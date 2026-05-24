#ifndef ROTIDE_SUPPORT_TERMINAL_H
#define ROTIDE_SUPPORT_TERMINAL_H

#include "rotide.h"

int editorClearScreen(void);
int editorResetCursorPos(void);
void editorClipboardSyncOsc52(const char *text, size_t len);
void editorClipboardSyncAll(const char *text, size_t len);
int editorConsumeMouseEvent(struct editorMouseEvent *out);
int editorRefreshWindowSize(void);
void editorQueueResizeEvent(void);
void editorRestoreTerminal(void);
void editorPanic(const char *s);
void editorSetDefaultMode(void);
void editorSetRawMode(void);
int editorReadKey(void);
int editorReadCursorPosition(int *rows, int *cols);
int editorReadWindowSize(int *rows, int *cols);
/* Call after a full-screen refresh completes. The input loop uses this to
 * throttle terminal-pane redraws under output flood. */
void editorMarkFrameRendered(void);

#endif
