#ifndef TEXT_ROW_H
#define TEXT_ROW_H

#include "rotide.h"

int editorBytesClampCxToCharBoundary(const char *bytes, int size, int cx);
int editorBytesPrevCharIdx(const char *bytes, int size, int idx);
int editorBytesNextCharIdx(const char *bytes, int size, int idx);
int editorBytesNextClusterIdx(const char *bytes, int size, int idx);
int editorBytesPrevClusterIdx(const char *bytes, int size, int idx);
int editorBytesClampCxToClusterBoundary(const char *bytes, int size, int cx);
int editorBytesCxToRx(const char *bytes, int size, int cx);
int editorBytesRxToCx(const char *bytes, int size, int rx);
int editorBytesCxToRenderIdx(const char *bytes, int size, int rsize, int cx);

int editorRowBuildRender(const char *chars, int size, char **render_out, int *rsize_out,
		int *display_cols_out);

#endif
