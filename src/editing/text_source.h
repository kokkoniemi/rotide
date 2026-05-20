#ifndef ROTIDE_EDITING_TEXT_SOURCE_H
#define ROTIDE_EDITING_TEXT_SOURCE_H

#include "rotide.h"

int editorBuildActiveTextSource(struct editorTextSource *source_out);
char *editorDupActiveTextSource(size_t *len_out);

void editorActiveTextSourceBuildTestResetCount(void);
int editorActiveTextSourceBuildTestCount(void);
void editorActiveTextSourceDupTestResetCount(void);
int editorActiveTextSourceDupTestCount(void);

#endif
