#ifndef EDITOR_INTERNAL_H
#define EDITOR_INTERNAL_H

#include "rotide.h"

void editorSetAllocFailureStatus(void);
int editorTabKindSupportsDocument(enum editorTabKind tab_kind);
void editorDocumentFreePtr(struct editorDocument **document_in_out);
int editorDocumentEnsureActiveCurrent(void);
int editorDocumentResetActiveFromText(const char *text, size_t len);
int editorTabDocumentEnsureCurrent(struct editorTabState *tab);
int editorSyntaxParseFullActive(void);
void editorLspNotifyDidCloseTabState(struct editorTabState *tab);
void editorSyntaxVisibleCacheInvalidate(void);
void editorSyntaxVisibleCacheFree(void);
void editorFreeRowArray(struct erow *rows, int numrows);
int editorBuildFullRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out);
int editorSyncCursorFromOffset(size_t target_offset);
int editorRestoreActiveFromDocument(const struct editorDocument *document,
		int target_cy, int target_cx, int dirty, int parse_syntax);
void editorHistoryEntryFree(struct editorHistoryEntry *entry);
void editorHistoryClear(struct editorHistory *history);
void editorResetActiveBufferFields(void);
void editorFreeActiveBufferState(void);
int editorTabOpenOrSwitchToPreviewFile(const char *filename);

#endif
