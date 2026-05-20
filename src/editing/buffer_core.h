#ifndef ROTIDE_EDITING_BUFFER_CORE_H
#define ROTIDE_EDITING_BUFFER_CORE_H

#include "editing/buffer_search.h"
#include "editing/document_bridge.h"
#include "editing/document_position.h"
#include "editing/edit_pipeline.h"
#include "editing/row_cache.h"
#include "editing/text_source.h"
#include "language/syntax_visible_cache.h"
#include "rotide.h"

#include <stddef.h>

char *editorRowsToStr(size_t *buflen);

int editorSyntaxEnabled(void);
int editorSyntaxTreeExists(void);
enum editorSyntaxLanguage editorSyntaxLanguageActive(void);
const char *editorSyntaxRootType(void);
int editorSyntaxBackgroundPoll(void);
int editorSyntaxBackgroundFlushForTests(void);
int editorBufferMaxRenderCols(void);

void editorSetAllocFailureStatus(void);
void editorSetOperationTooLargeStatus(void);
void editorSetFileTooLargeStatus(void);
int editorSyntaxParseFullActive(void);
void editorLspNotifyDidCloseTabState(struct editorTabState *tab);
void editorLspNotifyDidSaveActive(void);
int editorRestoreActiveFromDocument(const struct editorDocument *document, int target_cy,
                                    int target_cx, int dirty, int parse_syntax);

#endif
