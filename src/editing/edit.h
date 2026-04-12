#ifndef EDITING_EDIT_H
#define EDITING_EDIT_H

#include "rotide.h"

#include <stdarg.h>
#include <stddef.h>

int editorInsertText(const char *text, size_t len);
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);

void editorOpen(const char *filename);
void editorSetStatusMsg(const char *fmt, ...);
void editorSave(void);

#endif
