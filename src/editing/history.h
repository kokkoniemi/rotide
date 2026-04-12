#ifndef EDITING_HISTORY_H
#define EDITING_HISTORY_H

#include "rotide.h"

#include <stddef.h>

struct editorDocumentEdit;

void editorHistoryReset(void);
void editorHistoryBreakGroup(void);
void editorHistoryBeginEdit(enum editorEditKind kind);
void editorHistoryCommitEdit(enum editorEditKind kind, int changed);
void editorHistoryDiscardEdit(void);
int editorUndo(void);
int editorRedo(void);

void editorHistoryEntryFree(struct editorHistoryEntry *entry);
void editorHistoryClear(struct editorHistory *history);
int editorHistoryRecordPendingEditFromOperation(enum editorEditKind kind,
		const struct editorDocumentEdit *edit, const char *removed_text, size_t removed_len);

#endif
