#ifndef EDITING_EDIT_H
#define EDITING_EDIT_H

#include "rotide.h"

#include <stdarg.h>
#include <stddef.h>

int editorInsertText(const char *text, size_t len);
int editorBuildAutoIndentedText(const char *text, size_t len, int indent_cy, int indent_cx,
                                char **text_out, size_t *len_out);
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);

int editorFileCanOpen(const char *filename);
int editorOpen(const char *filename);
void editorOpenSetDeferLsp(int defer);
int editorFilePathLooksBinary(const char *filename, int *binary_out);
int editorReadFileToText(const char *filename, char **text_out, size_t *len_out);
void editorSetStatusMsg(const char *fmt, ...);
void editorSave(void);

#endif
