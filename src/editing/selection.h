#ifndef EDITING_SELECTION_H
#define EDITING_SELECTION_H

#include "rotide.h"

#include <stddef.h>

void editorClearSelectionState(void);
int editorGetSelectionRange(struct editorSelectionRange *range_out);
int editorExtractRangeText(const struct editorSelectionRange *range, char **text_out, size_t *len_out);
int editorDeleteRange(const struct editorSelectionRange *range);

void editorColumnSelectionClear(void);
int editorColumnSelectionGetRect(struct editorColumnSelectionRect *rect_out);
int editorColumnSelectionRowSpan(int row_idx, int left_rx, int right_rx, int *cx_start_out,
		int *cx_end_out);
int editorColumnSelectionExtractText(char **text_out, size_t *len_out);
int editorColumnSelectionDelete(void);
int editorColumnSelectionDeleteForward(void);
int editorColumnSelectionInsertChar(int c);
int editorColumnSelectionPasteText(const char *text, size_t len);
int editorColumnSelectionBackspace(void);

int editorClipboardSet(const char *text, size_t len);
const char *editorClipboardGet(size_t *len_out);
void editorClipboardClear(void);
void editorClipboardSetExternalSink(editorClipboardExternalSink sink);

#endif
