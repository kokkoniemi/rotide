#ifndef EDITOR_TABS_H
#define EDITOR_TABS_H

#include "rotide.h"

void editorResetActiveBufferFields(void);
void editorFreeActiveBufferState(void);

int editorTabsInit(void);
void editorTabsFreeAll(void);
int editorTabNewEmpty(void);
int editorTabOpenFileAsNew(const char *filename);
int editorTabOpenOrSwitchToFile(const char *filename);
int editorTabOpenOrSwitchToPreviewFile(const char *filename);
int editorTabOpenGitDiff(const char *title, const char *diff_text);
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
int editorActiveTabIsUnsupportedFile(void);
int editorActiveTabIsReadOnly(void);
int editorActiveTaskTabIsRunning(void);
const char *editorActiveBufferDisplayName(void);

#endif
