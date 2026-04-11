#ifndef BUFFER_H
#define BUFFER_H

#include "rotide.h"
#include <stddef.h>

char *editorRowsToStr(size_t *buflen);
int editorBuildActiveTextSource(struct editorTextSource *source_out);
int editorBufferPosToOffset(int cy, int cx, size_t *offset_out);
int editorBufferOffsetToPos(size_t offset, int *cy_out, int *cx_out);
int editorBufferLineByteRange(int row_idx, size_t *start_byte_out, size_t *end_byte_out);
int editorBufferFindForward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col);
int editorBufferFindBackward(const char *query, int start_row, int start_col, int *out_row,
		int *out_col);
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

void editorInsertChar(int c);
int editorInsertText(const char *text, size_t len);
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
int editorDrawerInitForStartup(int argc, char *argv[], int restored_session);
void editorDrawerShutdown(void);
int editorDrawerIsCollapsed(void);
int editorDrawerSetCollapsed(int collapsed);
int editorDrawerToggleCollapsed(void);
int editorDrawerWidthForCols(int total_cols);
int editorDrawerSeparatorWidthForCols(int total_cols);
int editorDrawerTextStartColForCols(int total_cols);
int editorDrawerTextViewportCols(int total_cols);
int editorTextBodyStartColForCols(int total_cols);
int editorTextBodyViewportCols(int total_cols);
int editorDrawerSetWidthForCols(int width, int total_cols);
int editorDrawerResizeByDeltaForCols(int delta, int total_cols);
int editorDrawerVisibleCount(void);
int editorDrawerGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out);
void editorDrawerClampViewport(int viewport_rows);
int editorDrawerMoveSelectionBy(int delta, int viewport_rows);
int editorDrawerScrollBy(int delta, int viewport_rows);
int editorDrawerExpandSelection(int viewport_rows);
int editorDrawerCollapseSelection(int viewport_rows);
int editorDrawerToggleSelectionExpanded(int viewport_rows);
int editorDrawerSelectVisibleIndex(int visible_idx, int viewport_rows);
int editorDrawerSelectedIsDirectory(void);
int editorDrawerOpenSelectedFileInTab(void);
int editorDrawerOpenSelectedFileInPreviewTab(void);
const char *editorDrawerRootPath(void);

int editorTabsInit(void);
void editorTabsFreeAll(void);
int editorTabNewEmpty(void);
int editorTabOpenFileAsNew(const char *filename);
int editorTabOpenOrSwitchToFile(const char *filename);
int editorTabSwitchToIndex(int idx);
int editorTabSwitchByDelta(int delta);
int editorTabCloseActive(void);
int editorTabCount(void);
int editorTabActiveIndex(void);
int editorTabAnyDirty(void);
int editorActiveTabIsPreview(void);
int editorTabIsPreviewAt(int idx);
void editorTabPinActivePreview(void);
const char *editorTabFilenameAt(int idx);
const char *editorTabDisplayNameAt(int idx);
int editorTabDirtyAt(int idx);
int editorTabBuildLayoutForWidth(int cols, struct editorTabLayoutEntry *entries, int max_entries,
		int *count_out);
int editorTabHitTestColumn(int col, int cols);
int editorActiveTabIsTaskLog(void);
int editorActiveTabIsReadOnly(void);
int editorActiveTaskTabIsRunning(void);
const char *editorActiveBufferDisplayName(void);

int editorTaskStart(const char *title, const char *command,
		const char *success_status, const char *failure_status);
int editorTaskPoll(void);
int editorTaskIsRunning(void);
int editorTaskRunningTabIndex(void);
int editorTaskTerminate(void);

int editorSyntaxEnabled(void);
int editorSyntaxTreeExists(void);
enum editorSyntaxLanguage editorSyntaxLanguageActive(void);
const char *editorSyntaxRootType(void);
int editorSyntaxPrepareVisibleRowSpans(int first_row, int row_count);
int editorSyntaxRowRenderSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
		int *count_out);
void editorSyntaxTestResetVisibleRowRecomputeCount(void);
int editorSyntaxTestVisibleRowRecomputeCount(void);
void editorDocumentTestResetStats(void);
int editorDocumentTestFullRebuildCount(void);
int editorDocumentTestIncrementalUpdateCount(void);
void editorActiveTextSourceBuildTestResetCount(void);
int editorActiveTextSourceBuildTestCount(void);
void editorActiveTextSourceDupTestResetCount(void);
int editorActiveTextSourceDupTestCount(void);
int editorBufferMaxRenderCols(void);

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
