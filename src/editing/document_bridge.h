#ifndef EDITING_DOCUMENT_BRIDGE_H
#define EDITING_DOCUMENT_BRIDGE_H

#include "rotide.h"

int editorTabKindSupportsDocument(enum editorTabKind tab_kind);
void editorDocumentFreePtr(struct editorDocument **document_in_out);
int editorDocumentResetActiveFromText(const char *text, size_t len);
int editorDocumentEnsureActiveCurrent(void);
int editorBufferDocumentEnsureCurrent(struct editorBuffer *buffer);
int editorTabDocumentEnsureCurrent(struct editorTabState *tab);

void editorDocumentStatsReset(void);
void editorDocumentStatsRecordFullRebuild(void);
void editorDocumentStatsRecordIncrementalUpdate(void);
int editorDocumentStatsFullRebuildCount(void);
int editorDocumentStatsIncrementalUpdateCount(void);

void editorTextTreeStatsReset(void);
void editorTextTreeStatsRecordFullRebuild(void);
void editorTextTreeStatsRecordIncrementalUpdate(void);
int editorTextTreeStatsFullRebuildCount(void);
int editorTextTreeStatsIncrementalUpdateCount(void);

#endif
