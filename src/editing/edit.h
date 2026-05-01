#ifndef EDITING_EDIT_H
#define EDITING_EDIT_H

#include "rotide.h"

#include <stdarg.h>
#include <stddef.h>

int editorInsertText(const char *text, size_t len);
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);

int editorFileCanOpen(const char *filename);
int editorOpen(const char *filename);
int editorFilePathLooksBinary(const char *filename, int *binary_out);
void editorSetStatusMsg(const char *fmt, ...);
void editorSave(void);

#endif
