#ifndef TERMINAL_H
#define TERMINAL_H

#include "rotide.h"

int editorClearScreen(void);
int editorResetCursorPos(void);
void editorClipboardSyncOsc52(const char *text, size_t len);
int editorConsumeMouseEvent(struct editorMouseEvent *out);
int editorRefreshWindowSize(void);
void editorQueueResizeEvent(void);
void editorRestoreTerminal(void);
void panic(const char *s);
void setDefaultMode(void);
void setRawMode(void);
int editorReadKey(void);
int readCursorPosition(int *rows, int *cols);
int readWindowSize(int *rows, int *cols);

#endif
