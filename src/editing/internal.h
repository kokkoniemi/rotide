#ifndef EDITOR_INTERNAL_H
#define EDITOR_INTERNAL_H

#include "rotide.h"

struct editorDocumentEdit {
	enum editorEditKind kind;
	size_t start_offset;
	size_t old_len;
	const char *new_text;
	size_t new_len;
	size_t before_cursor_offset;
	size_t after_cursor_offset;
	int before_dirty;
	int after_dirty;
};

void editorSetAllocFailureStatus(void);
void editorSetOperationTooLargeStatus(void);
void editorSetFileTooLargeStatus(void);
int editorTabKindSupportsDocument(enum editorTabKind tab_kind);
void editorDocumentFreePtr(struct editorDocument **document_in_out);
int editorDocumentEnsureActiveCurrent(void);
int editorDocumentResetActiveFromText(const char *text, size_t len);
int editorTabDocumentEnsureCurrent(struct editorTabState *tab);
int editorSyntaxParseFullActive(void);
void editorLspNotifyDidCloseTabState(struct editorTabState *tab);
void editorLspNotifyDidSaveActive(void);
void editorSyntaxVisibleCacheInvalidate(void);
void editorSyntaxVisibleCacheFree(void);
void editorFreeRowArray(struct erow *rows, int numrows);
int editorBuildFullRowsFromDocument(const struct editorDocument *document,
		struct erow **rows_out, int *numrows_out);
int editorSyncCursorFromOffset(size_t target_offset);
int editorSyncCursorFromOffsetByteBoundary(size_t target_offset);
int editorRestoreActiveFromDocument(const struct editorDocument *document,
		int target_cy, int target_cx, int dirty, int parse_syntax);
int editorApplyDocumentEdit(const struct editorDocumentEdit *edit);
char *editorDupActiveTextSource(size_t *len_out);
void editorClearSelectionState(void);
void editorHistoryEntryFree(struct editorHistoryEntry *entry);
void editorHistoryClear(struct editorHistory *history);
int editorHistoryRecordPendingEditFromOperation(enum editorEditKind kind,
		const struct editorDocumentEdit *edit, const char *removed_text, size_t removed_len);
void editorResetActiveBufferFields(void);
void editorFreeActiveBufferState(void);
int editorTabOpenOrSwitchToPreviewFile(const char *filename);

#endif
