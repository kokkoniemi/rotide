#ifndef ROTIDE_RENDER_WRAP_H
#define ROTIDE_RENDER_WRAP_H

#include "rotide.h"

int editorWrapBodyCols(void);
int editorWrapContinuationIndentCols(const struct editorRow *row, int body_cols);
int editorWrapNextStartCol(const struct editorRow *row, int start_col, int available_cols,
                           int total_cols);
void editorWrapSegmentInfo(struct editorRow *row, int segment_idx, int body_cols,
                           int *start_col_out, int *available_cols_out, int *indent_cols_out);
int editorWrapSegmentCountForRowIndex(int row_idx, int body_cols);
int editorWrapCursorSegmentForRx(struct editorRow *row, int rx, int body_cols);
void editorWrappedClampViewportOffsets(void);
void editorWrappedAdvancePosition(int *row_idx, int *segment_idx, int body_cols);
void editorWrappedMoveBackPosition(int *row_idx, int *segment_idx, int body_cols);
int editorWrappedPositionBefore(int row_a, int segment_a, int row_b, int segment_b);
int editorWrappedDistanceForward(int from_row, int from_segment, int to_row, int to_segment,
                                 int max_distance, int body_cols, int *distance_out);

#endif
