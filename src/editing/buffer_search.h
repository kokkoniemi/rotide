#ifndef ROTIDE_EDITING_BUFFER_SEARCH_H
#define ROTIDE_EDITING_BUFFER_SEARCH_H

int editorBufferFindForward(const char *query, int start_row, int start_col, int *out_row,
                            int *out_col);
int editorBufferFindBackward(const char *query, int start_row, int start_col, int *out_row,
                             int *out_col);

#endif
