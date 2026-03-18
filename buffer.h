#ifndef BUFFER_H
#define BUFFER_H

#include "rotide.h"
#include <stddef.h>

char *editorRowsToStr(size_t *buflen);
int editorIsUtf8ContinuationByte(unsigned char c);
int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp);
int editorIsGraphemeExtendCodepoint(unsigned int cp);
int editorIsRegionalIndicatorCodepoint(unsigned int cp);
int editorCharDisplayWidth(const char *s, int len);
int editorRowClampCxToCharBoundary(const struct erow *row, int cx);
int editorRowPrevCharIdx(const struct erow *row, int idx);
int editorRowNextCharIdx(const struct erow *row, int idx);
int editorRowNextClusterIdx(const struct erow *row, int idx);
int editorRowPrevClusterIdx(const struct erow *row, int idx);
int editorRowClampCxToClusterBoundary(const struct erow *row, int cx);
int editorRowCxToRx(const struct erow *row, int cx);
int editorRowRxToCx(const struct erow *row, int rx);
int editorRowCxToRenderIdx(const struct erow *row, int cx);

void editorUpdateRow(struct erow *row);
void editorInsertRow(int idx, const char *s, size_t len);
void editorDeleteRow(int idx);
void editorInsertCharAt(struct erow *row, int idx, int c);
void editorRowAppendString(struct erow *row, const char *s, size_t len);
void editorDelCharAt(struct erow *row, int idx);
void editorDelCharsAt(struct erow *row, int idx, int len);

void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);

void editorOpen(const char *filename);
void editorSetStatusMsg(const char *fmt, ...);
void editorSave(void);
int editorRecoveryInitForCurrentDir(void);
void editorRecoveryShutdown(void);
const char *editorRecoveryPath(void);
int editorRecoveryHasSnapshot(void);
int editorRecoveryRestoreSnapshot(void);
int editorRecoveryPromptAndMaybeRestore(void);
void editorRecoveryMaybeAutosaveOnActivity(void);
void editorRecoveryCleanupOnCleanExit(void);
int editorStartupLoadRecoveryOrOpenArgs(int argc, char *argv[]);

int editorTabsInit(void);
void editorTabsFreeAll(void);
int editorTabNewEmpty(void);
int editorTabOpenFileAsNew(const char *filename);
int editorTabSwitchToIndex(int idx);
int editorTabSwitchByDelta(int delta);
int editorTabCloseActive(void);
int editorTabCount(void);
int editorTabActiveIndex(void);
int editorTabAnyDirty(void);
const char *editorTabFilenameAt(int idx);
int editorTabDirtyAt(int idx);
int editorTabVisibleSlotsForWidth(int cols);
void editorTabsAlignViewToActive(int cols);
int editorTabHitTestColumn(int col, int cols);

int editorGetSelectionRange(struct editorSelectionRange *range_out);
int editorExtractRangeText(const struct editorSelectionRange *range, char **text_out, size_t *len_out);
int editorDeleteRange(const struct editorSelectionRange *range);

int editorClipboardSet(const char *text, size_t len);
const char *editorClipboardGet(size_t *len_out);
void editorClipboardClear(void);
void editorClipboardSetExternalSink(editorClipboardExternalSink sink);

void editorHistoryReset(void);
void editorHistoryBreakGroup(void);
void editorHistoryBeginEdit(enum editorEditKind kind);
void editorHistoryCommitEdit(enum editorEditKind kind, int changed);
void editorHistoryDiscardEdit(void);
int editorUndo(void);
int editorRedo(void);

#endif
