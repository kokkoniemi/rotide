#ifndef ROTIDE_WORKSPACE_TABS_H
#define ROTIDE_WORKSPACE_TABS_H

#include "rotide.h"

struct editorPaneView;
struct editorPaneNode;

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

/*
 * Pane-tab kind accessors. editorTabKindAt/editorTabActiveKind read the global
 * tab list; editorPaneActiveKind reports a pane's active-tab kind, bridging the
 * legacy leaf kind during the leaf-kind -> tab-kind migration. All return
 * EDITOR_PANE_KIND_EDITOR for out-of-range / split / NULL inputs.
 */
enum editorPaneKind editorTabKindAt(int idx);
enum editorPaneKind editorTabActiveKind(void);
enum editorPaneKind editorPaneActiveKind(const struct editorPaneNode *pane);
void *editorTabPayloadAt(int idx);

/*
 * Create a non-editor tab (kind != EDITOR) owning `payload` and make it the sole
 * active tab of `pane` (an editor leaf). `payload_free` releases it on close.
 * Returns the new tab index or -1. editorTabCloseAt closes any tab by global
 * index, routing through its host pane's close path. */
int editorTabCreateWidget(enum editorPaneKind kind, void *payload,
                          void (*payload_free)(void *payload));
int editorTabAdoptInPane(struct editorPaneNode *pane, enum editorPaneKind kind, void *payload,
                         void (*payload_free)(void *payload));
int editorTabCloseAt(int idx);

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

/* Invariant: every editor leaf in E.layout_root has pane_tab_count >= 1 once
 * an operation has settled. Operations that may transiently empty a pane
 * call editorTabsEnsurePaneOccupancy before returning; it walks the tree
 * and backfills empty leaves. Renderers and navigation rely on this. */
int editorTabAppendEmptyForPane(struct editorPaneNode *pane);
void editorTabsEnsurePaneOccupancy(void);

/* Insert and activate a terminal beside the focused pane's active tab. */
int editorTabNewTerminalBesideActive(const char *command);

/* Cross-pane move transfers focus to target. Same-pane call reorders. */
int editorPaneMoveTab(struct editorPaneNode *source, struct editorPaneNode *target, int tab_idx,
                      int target_slot);
int editorTabOpenFileAsNew(const char *filename);
int editorTabOpenOrSwitchToFile(const char *filename);
int editorTabOpenOrSwitchToPreviewFile(const char *filename);
int editorTabOpenGenerated(enum editorTabKind kind, const char *title, const char *text);
int editorTabSwitchToIndex(int idx);
int editorTabSwitchByDelta(int delta);
int editorTabCloseActive(void);
int editorTabCount(void);
int editorTabActiveIndex(void);
int editorTabAnyDirty(void);
int editorActiveTabIsPreview(void);
void editorTabPinActivePreview(void);
const char *editorTabFilenameAt(int idx);
const char *editorTabDisplayNameAt(int idx);
int editorTabDirtyAt(int idx);

/* Rendering and hit-testing helpers keep tab bar layout outside the renderer. */
int editorTabBuildLayoutForPane(struct editorPaneView *view, int cols,
                                struct editorTabLayoutEntry *entries, int max_entries,
                                int *count_out);
int editorTabHitTestColumnForPane(struct editorPaneView *view, int col, int cols);
int editorTabOverflowHitTestColumnForPane(struct editorPaneView *view, int col, int cols);
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
