#ifndef EDITING_SELECTION_H
#define EDITING_SELECTION_H

#include "rotide.h"

#include <stddef.h>

void editorClearSelectionState(void);
int editorGetSelectionRange(struct editorSelectionRange *range_out);
int editorExtractRangeText(const struct editorSelectionRange *range, char **text_out, size_t *len_out);
int editorDeleteRange(const struct editorSelectionRange *range);

int editorClipboardSet(const char *text, size_t len);
const char *editorClipboardGet(size_t *len_out);
void editorClipboardClear(void);
void editorClipboardSetExternalSink(editorClipboardExternalSink sink);

#endif
