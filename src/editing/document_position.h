#ifndef EDITING_DOCUMENT_POSITION_H
#define EDITING_DOCUMENT_POSITION_H

#include "rotide.h"

int editorBufferPosToOffset(int cy, int cx, size_t *offset_out);
int editorBufferOffsetToPos(size_t offset, int *cy_out, int *cx_out);
int editorBufferLineByteRange(int row_idx, size_t *start_byte_out, size_t *end_byte_out);
int editorSyncCursorFromOffset(size_t target_offset);
int editorSyncCursorFromOffsetByteBoundary(size_t target_offset);

#endif
