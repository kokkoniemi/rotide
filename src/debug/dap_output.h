#ifndef ROTIDE_DEBUG_DAP_OUTPUT_H
#define ROTIDE_DEBUG_DAP_OUTPUT_H

#include <stddef.h>

void editorDapOutputClear(void);
void editorDapOutputAppend(const char *text);
const char *editorDapOutputText(void);
size_t editorDapOutputLength(void);
int editorDapOutputLineCount(void);
int editorDapOutputLine(int index, const char **line_out, int *len_out);

#endif
