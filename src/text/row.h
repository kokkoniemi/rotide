#ifndef TEXT_ROW_H
#define TEXT_ROW_H

#include "rotide.h"

int editorRowClampCxToCharBoundary(const struct erow *row, int cx);
int editorRowPrevCharIdx(const struct erow *row, int idx);
int editorRowNextCharIdx(const struct erow *row, int idx);
int editorRowNextClusterIdx(const struct erow *row, int idx);
int editorRowPrevClusterIdx(const struct erow *row, int idx);
int editorRowClampCxToClusterBoundary(const struct erow *row, int cx);
int editorRowCxToRx(const struct erow *row, int cx);
int editorRowRxToCx(const struct erow *row, int rx);
int editorRowCxToRenderIdx(const struct erow *row, int cx);
int editorRowBuildRender(const char *chars, int size, char **render_out, int *rsize_out,
		int *display_cols_out);

#endif
