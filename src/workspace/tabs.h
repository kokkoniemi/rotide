#ifndef EDITOR_TABS_H
#define EDITOR_TABS_H

#include "rotide.h"

/* Active-buffer helpers move state between E and editorTabState. File tabs own
 * editable documents; task-log, unsupported-file, and Git-diff tabs are
 * read-only/non-normal save targets.
 */
void editorResetActiveBufferFields(void);
void editorFreeActiveBufferState(void);
struct editorBuffer *editorActiveBufferHandle(void);
const struct editorBuffer *editorActiveBufferHandleConst(void);
struct editorBuffer *editorTabBufferHandleAtMutable(int idx);
const struct editorBuffer *editorTabBufferHandleAt(int idx);

/* Render-time active buffer aliasing (buffer-level API). */
void editorBufferAliasSnapshot(struct editorBuffer *snap);
void editorBufferAliasToActive(const struct editorBuffer *buffer);

/*
 * Render-time tab aliasing.
 *
 * Legacy wrappers over the buffer-level alias API. Keep callers moving while
 * modules migrate from tab-state copies to buffer handles.
 *
 * These helpers are render-only: no LSP/syntax notifications, no
 * disk-state probing, no edit history mutations. Callers must guarantee
 * neither E nor the aliased tab is modified between snapshot and restore.
 */
void editorTabStateAliasSnapshot(struct editorTabState *snap);
void editorTabStateAliasToActive(const struct editorTabState *tab);

/* Tab lifecycle and navigation. Preview tabs may be reused until pinned. */
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

/* Rendering and hit-testing helpers keep tab bar layout outside the renderer. */
int editorTabBuildLayoutForWidth(int cols, struct editorTabLayoutEntry *entries, int max_entries,
		int *count_out);
int editorTabHitTestColumn(int col, int cols);
int editorTabOverflowHitTestColumn(int col, int cols);
int editorActiveTabIsTaskLog(void);
int editorActiveTabIsUnsupportedFile(void);
int editorActiveTabIsReadOnly(void);
int editorActiveTaskTabIsRunning(void);
const char *editorActiveBufferDisplayName(void);

#endif
