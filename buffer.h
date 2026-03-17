#ifndef BUFFER_H
#define BUFFER_H

#include "rotide.h"
#include <stddef.h>

char *editorRowsToStr(int *buflen);
int editorIsUtf8ContinuationByte(unsigned char c);
int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp);
int editorIsGraphemeExtendCodepoint(unsigned int cp);
int editorIsRegionalIndicatorCodepoint(unsigned int cp);
int editorCharDisplayWidth(const char *s, int len);
int editorRowClampCxToCharBoundary(struct erow *row, int cx);
int editorRowPrevCharIdx(struct erow *row, int idx);
int editorRowNextCharIdx(struct erow *row, int idx);
int editorRowNextClusterIdx(struct erow *row, int idx);
int editorRowPrevClusterIdx(struct erow *row, int idx);
int editorRowClampCxToClusterBoundary(struct erow *row, int cx);
int editorRowCxToRx(struct erow *row, int cx);
int editorRowRxToCx(struct erow *row, int rx);

void editorUpdateRow(struct erow *row);
void editorInsertRow(int idx, char *s, size_t len);
void editorDeleteRow(int idx);
void editorInsertCharAt(struct erow *row, int idx, int c);
void editorRowAppendString(struct erow *row, char *s, size_t len);
void editorDelCharAt(struct erow *row, int idx);
void editorDelCharsAt(struct erow *row, int idx, int len);

void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);

void editorOpen(char *filename);
void editorSetStatusMsg(const char *fmt, ...);
void editorSave(void);

#endif
