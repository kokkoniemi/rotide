#include "workspace/tabs.h"

#include "debug/dap.h"
#include "editing/buffer_core.h"
#include "editing/document_bridge.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/row_cache.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "language/syntax_worker.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "terminal/terminal_pane.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/task.h"
#include "workspace/workspace_state.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define EDITOR_UNSUPPORTED_FILE_TEXT "File is unsupported\n\nBinary files are not supported.\n"

static void tabsStateInitEmpty(struct editorTabState *tab);
static void tabsStateFree(struct editorTabState *tab);
static void tabsStateCaptureActive(struct editorTabState *tab);
static void tabsStateLoadActive(struct editorTabState *tab);
static int tabsEnsureTabCapacity(int needed);
static void tabsStoreActiveTab(void);
static void tabsLoadActiveTab(int tab_idx);
static void tabsRegisterWithFocusedPane(int idx);
static int tabsCanReuseActiveEmptyBuffer(void);
static int tabsKindCanReuseAsPreview(enum editorTabKind tab_kind);
static int tabsFindReusablePreviewIndex(void);
static const char *tabsPathAt(int idx);
static int tabsFindOpenFileIndexInFocusedPane(const char *path);
static int tabsFindEditableFileIndex(const char *path);
static int tabsLoadUnsupportedFilePreview(const char *filename);
static void tabsTaskLogClampCursor(struct editorTabState *tab);
static int tabsRebuildGeneratedTabRows(struct editorTabState *tab);
static int tabsTaskMutateTab(int tab_idx, int jump_to_end,
                             int (*mutator)(struct editorTabState *tab, void *ctx), void *ctx);
static void tabsTaskResetState(void);
static void tabsTaskFinalize(int success, int exit_code);
static void tabsTaskSetFinalStatus(int success);
static int tabsTaskPrepareLogTab(const char *title, const char *text);
static const char *tabsLabelFromDisplayName(const char *display_name);
static int tabsSanitizedTokenDisplayCols(const char *text, int text_len, int *src_len_out);
static int tabsSanitizedTextDisplayCols(const char *text, int max_cols);
static int tabsLabelColsAt(int tab_idx);
static int tabsWidthColsAt(int tab_idx);
static void tabsBufferInitEmpty(struct editorBuffer *buffer);
static void tabsBufferCopy(struct editorBuffer *dst, const struct editorBuffer *src);
static void tabsBufferMove(struct editorBuffer *dst, struct editorBuffer *src);
static void tabsBufferFreeRows(struct editorBuffer *buffer);
static void tabsBufferClearOwnedState(struct editorBuffer *buffer);

static void tabsBufferInitEmpty(struct editorBuffer *buffer) {
	memset(buffer, 0, sizeof(*buffer));
	buffer->tab_kind = EDITOR_TAB_FILE;
	buffer->cursor_offset = 0;
	buffer->syntax_language = EDITOR_SYNTAX_NONE;
	buffer->search_direction = 1;
	buffer->edit_group_kind = EDITOR_EDIT_NONE;
	buffer->edit_pending_kind = EDITOR_EDIT_NONE;
	buffer->edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

static void tabsBufferCopy(struct editorBuffer *dst, const struct editorBuffer *src) {
	*dst = *src;
}

static void tabsBufferMove(struct editorBuffer *dst, struct editorBuffer *src) {
	tabsBufferCopy(dst, src);
	tabsBufferInitEmpty(src);
}

static void tabsBufferFreeRows(struct editorBuffer *buffer) {
	for (int i = 0; i < buffer->numrows; i++) {
		free(buffer->rows[i].render);
		free(buffer->rows[i].wrap_cache_segments);
	}
	free(buffer->rows);
	buffer->rows = NULL;
	buffer->numrows = 0;
}

static void tabsBufferClearOwnedState(struct editorBuffer *buffer) {
	if (buffer->lsp_diagnostics != NULL) {
		for (int i = 0; i < buffer->lsp_diagnostic_count; i++) {
			free(buffer->lsp_diagnostics[i].message);
		}
		free(buffer->lsp_diagnostics);
		buffer->lsp_diagnostics = NULL;
	}
	if (buffer->lsp_symbols != NULL) {
		editorLspFreeSymbols(buffer->lsp_symbols, buffer->lsp_symbol_count);
		buffer->lsp_symbols = NULL;
		buffer->lsp_symbol_count = 0;
	}
	tabsBufferFreeRows(buffer);
	editorDocumentFreePtr(&buffer->document);
	free(buffer->filename);
	buffer->filename = NULL;
	editorGitBlameCacheClear(buffer);
	free(buffer->tab_title);
	buffer->tab_title = NULL;
	free(buffer->git_view_line_kinds);
	buffer->git_view_line_kinds = NULL;
	buffer->git_view_line_kind_count = 0;
	free(buffer->git_view_source_path);
	buffer->git_view_source_path = NULL;
	free(buffer->git_view_regen_arg);
	buffer->git_view_regen_arg = NULL;
	buffer->git_view_regen_kind = 0;
	buffer->git_view_whole_file = 0;
	buffer->git_view_commit_amend = 0;
	editorSyntaxStateDestroy(buffer->syntax_state);
	buffer->syntax_state = NULL;
	buffer->syntax_language = EDITOR_SYNTAX_NONE;
	free(buffer->search_query);
	buffer->search_query = NULL;
	free(buffer->input_vim_search_query);
	buffer->input_vim_search_query = NULL;
	editorHistoryClear(&buffer->undo_history);
	editorHistoryClear(&buffer->redo_history);
	editorHistoryEntryFree(&buffer->edit_pending_entry);
	buffer->edit_pending_entry_valid = 0;
	tabsBufferInitEmpty(buffer);
}

static void tabsStateInitEmpty(struct editorTabState *tab) {
	tabsBufferInitEmpty(&tab->buffer);
	tab->kind = EDITOR_PANE_KIND_EDITOR;
	tab->payload = NULL;
	tab->payload_free = NULL;
}

void editorResetActiveBufferFields(void) {
	tabsBufferInitEmpty(&E.active_buffer);
}

struct editorBuffer *editorActiveBufferHandle(void) {
	return &E.active_buffer;
}

const struct editorBuffer *editorActiveBufferHandleConst(void) {
	return &E.active_buffer;
}

struct editorBuffer *editorTabBufferHandleAtMutable(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return editorActiveBufferHandle();
	}
	if (E.tabs == NULL) {
		return NULL;
	}
	return &E.tabs[idx].buffer;
}

const struct editorBuffer *editorTabBufferHandleAt(int idx) {
	return editorTabBufferHandleAtMutable(idx);
}

enum editorPaneKind editorTabKindAt(int idx) {
	if (E.tabs == NULL || idx < 0 || idx >= E.tab_count) {
		return EDITOR_PANE_KIND_EDITOR;
	}
	return E.tabs[idx].kind;
}

void *editorTabPayloadAt(int idx) {
	if (E.tabs == NULL || idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	return E.tabs[idx].payload;
}

enum editorPaneKind editorTabActiveKind(void) {
	return editorTabKindAt(E.active_tab);
}

enum editorPaneKind editorPaneActiveKind(const struct editorPaneNode *pane) {
	if (pane == NULL || pane->is_split) {
		return EDITOR_PANE_KIND_EDITOR;
	}
	/* A pane's kind is its active tab's kind; leaves no longer carry a kind for
	 * terminals/consoles (those are tabs). */
	return editorTabKindAt(pane->as.leaf.view.active_tab_idx);
}

void editorBufferAliasSnapshot(struct editorBuffer *snap) {
	if (snap == NULL) {
		return;
	}
	tabsBufferCopy(snap, editorActiveBufferHandleConst());
}

void editorBufferAliasToActive(const struct editorBuffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	tabsBufferCopy(editorActiveBufferHandle(), buffer);
}

static void tabsFreeTabRows(struct editorTabState *tab) {
	tabsBufferFreeRows(&tab->buffer);
}

static void tabsStateFree(struct editorTabState *tab) {
	if (tab->payload != NULL && tab->payload_free != NULL) {
		tab->payload_free(tab->payload);
	}
	tab->payload = NULL;
	tab->payload_free = NULL;
	tabsBufferClearOwnedState(&tab->buffer);
}

void editorFreeActiveBufferState(void) {
	tabsBufferClearOwnedState(&E.active_buffer);
	editorSyntaxVisibleCacheInvalidate();
}

static void tabsStateCaptureActive(struct editorTabState *tab) {
	tabsStateFree(tab);

	if (E.document != NULL) {
		size_t cursor_offset = 0;
		if (editorBufferPosToOffset(E.cy, E.cx, &cursor_offset)) {
			E.cursor_offset = cursor_offset;
		}
	}
	tabsBufferMove(&tab->buffer, &E.active_buffer);
}

void editorTabStateAliasSnapshot(struct editorTabState *snap) {
	if (snap == NULL) {
		return;
	}
	editorBufferAliasSnapshot(&snap->buffer);
}

void editorTabStateAliasToActive(const struct editorTabState *tab) {
	if (tab == NULL) {
		return;
	}
	editorBufferAliasToActive(&tab->buffer);
}

static void tabsStateLoadActive(struct editorTabState *tab) {
	tabsBufferMove(&E.active_buffer, &tab->buffer);
	editorSyntaxVisibleCacheInvalidate();
	if (E.document != NULL) {
		if (!editorSyncCursorFromOffset(E.cursor_offset)) {
			E.cursor_offset = 0;
			E.cy = 0;
			E.cx = 0;
		}
	}
}

static int tabsEnsureTabCapacity(int needed) {
	if (needed <= E.tab_capacity) {
		return 1;
	}

	int new_capacity = E.tab_capacity > 0 ? E.tab_capacity : 4;
	while (new_capacity < needed) {
		if (new_capacity >= ROTIDE_MAX_TABS) {
			new_capacity = ROTIDE_MAX_TABS;
			break;
		}
		new_capacity *= 2;
		if (new_capacity > ROTIDE_MAX_TABS) {
			new_capacity = ROTIDE_MAX_TABS;
		}
	}
	if (new_capacity < needed) {
		return 0;
	}

	size_t cap_size = 0;
	size_t tabs_bytes = 0;
	if (!editorIntToSize(new_capacity, &cap_size) ||
	    !editorSizeMul(sizeof(struct editorTabState), cap_size, &tabs_bytes)) {
		return 0;
	}

	struct editorTabState *new_tabs = editorRealloc(E.tabs, tabs_bytes);
	if (new_tabs == NULL) {
		return 0;
	}

	for (int i = E.tab_capacity; i < new_capacity; i++) {
		tabsStateInitEmpty(&new_tabs[i]);
	}

	E.tabs = new_tabs;
	E.tab_capacity = new_capacity;
	return 1;
}

static void tabsStoreActiveTab(void) {
	if (E.tabs == NULL || E.tab_count <= 0 || E.active_tab < 0 || E.active_tab >= E.tab_count) {
		return;
	}
	/* Non-editor tabs hold no editable buffer (E.active_buffer is detached), so
	 * there is nothing to capture back into the slot. */
	if (E.tabs[E.active_tab].kind != EDITOR_PANE_KIND_EDITOR) {
		return;
	}
	/* An edited tab is no longer a throwaway preview; drop its preview status
	 * in every pane so it is never reused or rendered as preview. */
	if (E.dirty != 0) {
		editorPaneTreeClearPreviewTab(E.layout_root, E.active_tab);
	}
	tabsStateCaptureActive(&E.tabs[E.active_tab]);
}

static void tabsLoadActiveTab(int tab_idx) {
	if (E.tabs == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		editorResetActiveBufferFields();
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	/* Whenever a tab becomes active we land it in the focused pane's
	 * membership list so the per-pane tab strip stays in sync regardless
	 * of which API created/switched the tab. */
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split) {
		(void)editorPaneViewActivateTab(&E.focused_leaf->as.leaf.view, tab_idx);
	}
	/* Non-editor active tab: E.active_buffer stays detached/empty and no editor
	 * (syntax/LSP/document) setup runs. The payload owns the tab's real state. */
	if (E.tabs[tab_idx].kind != EDITOR_PANE_KIND_EDITOR) {
		/* A re-shown terminal tab must fully repaint: its rows are "clean" but
		 * the previously-active tab painted over them. */
		if (E.tabs[tab_idx].kind == EDITOR_PANE_KIND_TERMINAL) {
			editorTerminalPaneMarkDirty(
			        (struct editorTerminalPane *)E.tabs[tab_idx].payload);
		}
		editorResetActiveBufferFields();
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	tabsStateLoadActive(&E.tabs[tab_idx]);
	if (editorActiveTabIsReadOnly() && E.tab_kind != EDITOR_TAB_GIT_DIFF) {
		E.syntax_language = EDITOR_SYNTAX_NONE;
		editorSyntaxStateDestroy(E.syntax_state);
		E.syntax_state = NULL;
		E.syntax_parse_failures = 0;
		E.lsp_doc_open = 0;
		E.lsp_doc_version = 0;
		E.lsp_eslint_doc_open = 0;
		E.lsp_eslint_doc_version = 0;
		E.lsp_diagnostics = NULL;
		E.lsp_diagnostic_count = 0;
		E.lsp_diagnostic_error_count = 0;
		E.lsp_diagnostic_warning_count = 0;
		E.lsp_symbols = NULL;
		E.lsp_symbol_count = 0;
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	char *first_line_copy = NULL;
	if (E.numrows > 0) {
		first_line_copy = editorDocumentLineDup(E.document, 0, NULL);
	}
	enum editorSyntaxLanguage detected =
	        E.tab_kind == EDITOR_TAB_GIT_DIFF
	                ? EDITOR_SYNTAX_DIFF
	                : editorSyntaxDetectLanguageFromFilenameAndFirstLine(E.filename,
	                                                                     first_line_copy);
	free(first_line_copy);
	if (E.syntax_language != detected ||
	    (detected != EDITOR_SYNTAX_NONE && E.syntax_state == NULL)) {
		(void)editorSyntaxParseFullActive();
	}
	/*
	 * If this tab was opened with deferred LSP (e.g. during session restore), the LSP server
	 * never received a didOpen for it. Send one now so diagnostics, definitions, and
	 * completion start working as soon as the user lands on the tab.
	 */
	editorLspEnsureActiveDocumentTracked();
	editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
}

int editorTabsInit(void) {
	editorTabsFreeAll();
	if (!tabsEnsureTabCapacity(1)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	E.tab_count = 1;
	E.active_tab = 0;
	tabsStateInitEmpty(&E.tabs[0]);
	tabsLoadActiveTab(0);
	return 1;
}

void editorTabsFreeAll(void) {
	if (E.task_running && E.task_pid > 0) {
		int status = 0;
		(void)kill(E.task_pid, SIGTERM);
		(void)waitpid(E.task_pid, &status, 0);
	}
	tabsTaskResetState();

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	editorLspNotifyEslintDidClose(E.filename, E.syntax_language, &E.lsp_eslint_doc_open,
	                              &E.lsp_eslint_doc_version);
	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			editorLspNotifyDidCloseTabState(&E.tabs[i]);
		}
	}
	editorLspShutdown();
	editorDapShutdown();
	editorSyntaxBackgroundStop();

	editorFreeActiveBufferState();

	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			tabsStateFree(&E.tabs[i]);
		}
	}
	free(E.tabs);
	E.tabs = NULL;
	E.tab_count = 0;
	E.tab_capacity = 0;
	E.active_tab = 0;
	editorSyntaxVisibleCacheFree();
	editorSyntaxReleaseSharedResources();
}

int editorTabNewEmpty(void) {
	if (E.tab_count >= ROTIDE_MAX_TABS) {
		editorSetStatusMsg("Tab limit reached (%d)", ROTIDE_MAX_TABS);
		return 0;
	}
	if (E.tab_count == 0) {
		return editorTabsInit();
	}

	tabsRegisterWithFocusedPane(E.active_tab);
	tabsStoreActiveTab();
	int new_idx = E.tab_count;
	if (!tabsEnsureTabCapacity(E.tab_count + 1)) {
		tabsLoadActiveTab(E.active_tab);
		editorSetAllocFailureStatus();
		return 0;
	}

	tabsStateInitEmpty(&E.tabs[new_idx]);
	E.tab_count++;
	E.active_tab = new_idx;
	tabsLoadActiveTab(E.active_tab);
	tabsRegisterWithFocusedPane(new_idx);
	return 1;
}

int editorTabAppendEmptyForPane(struct editorPaneNode *pane) {
	if (pane == NULL || pane->is_split || pane->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return -1;
	}
	if (E.tab_count >= ROTIDE_MAX_TABS) {
		return -1;
	}
	if (!tabsEnsureTabCapacity(E.tab_count + 1)) {
		return -1;
	}
	int new_idx = E.tab_count;
	tabsStateInitEmpty(&E.tabs[new_idx]);
	E.tab_count++;
	if (!editorPaneViewActivateTab(&pane->as.leaf.view, new_idx)) {
		tabsStateFree(&E.tabs[new_idx]);
		E.tab_count--;
		return -1;
	}
	return new_idx;
}

int editorTabCreateWidget(enum editorPaneKind kind, void *payload, void (*payload_free)(void *)) {
	if (kind == EDITOR_PANE_KIND_EDITOR) {
		return -1;
	}
	if (E.tab_count >= ROTIDE_MAX_TABS || !tabsEnsureTabCapacity(E.tab_count + 1)) {
		return -1;
	}
	int new_idx = E.tab_count;
	tabsStateInitEmpty(&E.tabs[new_idx]);
	E.tabs[new_idx].kind = kind;
	E.tabs[new_idx].payload = payload;
	E.tabs[new_idx].payload_free = payload_free;
	E.tab_count++;
	return new_idx;
}

int editorTabAdoptInPane(struct editorPaneNode *pane, enum editorPaneKind kind, void *payload,
                         void (*payload_free)(void *)) {
	if (pane == NULL || pane->is_split || pane->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return -1;
	}
	/* Save the outgoing editor tab before the active buffer is detached. */
	tabsStoreActiveTab();
	int new_idx = editorTabCreateWidget(kind, payload, payload_free);
	if (new_idx < 0) {
		return -1;
	}
	/* The widget becomes the pane's sole tab (mirrors today's dedicated
	 * terminal/console pane), then the global active tab. Callers use this when
	 * `pane` is (or will be) the focused leaf. */
	editorPaneViewClearTabs(&pane->as.leaf.view);
	if (!editorPaneViewActivateTab(&pane->as.leaf.view, new_idx)) {
		E.tab_count--;
		tabsStateFree(&E.tabs[new_idx]);
		return -1;
	}
	E.active_tab = new_idx;
	tabsLoadActiveTab(new_idx);
	return new_idx;
}

static struct editorPaneNode *tabsFindLeafWithTab(struct editorPaneNode *node, int idx) {
	if (node == NULL) {
		return NULL;
	}
	if (node->is_split) {
		struct editorPaneNode *found = tabsFindLeafWithTab(node->as.split.first, idx);
		return found != NULL ? found : tabsFindLeafWithTab(node->as.split.second, idx);
	}
	return editorPaneViewHasTab(&node->as.leaf.view, idx) ? node : NULL;
}

int editorTabCloseAt(int idx) {
	if (E.tabs == NULL || idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	/* Route the close through the pane that hosts the tab so the existing
	 * focused-pane close machinery (membership, fallback, payload free) runs. */
	struct editorPaneNode *host = tabsFindLeafWithTab(E.layout_root, idx);
	if (host != NULL && host != E.focused_leaf) {
		(void)editorLayoutSetFocusedLeaf(host);
	}
	if (E.active_tab != idx) {
		(void)editorTabSwitchToIndex(idx);
	}
	return editorTabCloseActive();
}

static void tabsEnsurePaneOccupancyRecursive(struct editorPaneNode *node) {
	if (node == NULL) {
		return;
	}
	if (node->is_split) {
		tabsEnsurePaneOccupancyRecursive(node->as.split.first);
		tabsEnsurePaneOccupancyRecursive(node->as.split.second);
		return;
	}
	if (node->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return;
	}
	if (node->as.leaf.view.pane_tab_count == 0) {
		(void)editorTabAppendEmptyForPane(node);
	}
}

void editorTabsEnsurePaneOccupancy(void) {
	tabsEnsurePaneOccupancyRecursive(E.layout_root);
}

int editorPaneMoveTab(struct editorPaneNode *source, struct editorPaneNode *target, int tab_idx,
                      int target_slot) {
	if (source == NULL || target == NULL || source->is_split || target->is_split ||
	    source->as.leaf.kind != EDITOR_PANE_KIND_EDITOR ||
	    target->as.leaf.kind != EDITOR_PANE_KIND_EDITOR || tab_idx < 0) {
		return 0;
	}
	struct editorPaneView *source_view = &source->as.leaf.view;
	struct editorPaneView *target_view = &target->as.leaf.view;
	int source_slot = editorPaneViewIndexOfTab(source_view, tab_idx);
	if (source_slot < 0) {
		return 0;
	}

	if (source == target) {
		if (!editorPaneViewInsertTabAt(source_view, tab_idx, target_slot)) {
			return 0;
		}
		(void)editorPaneViewActivateTab(source_view, tab_idx);
		return 1;
	}

	if (!editorPaneViewHasTab(target_view, tab_idx) &&
	    target_view->pane_tab_count >= ROTIDE_PANE_MAX_TABS) {
		return 0;
	}

	int source_next_active = source_view->pane_tab_count > 1
	                                 ? source_view->pane_tabs[source_slot == 0 ? 1 : 0]
	                                 : -1;
	if (source == E.focused_leaf) {
		editorPaneViewCaptureFromState(source_view);
	}
	editorPaneViewRemoveTab(source_view, tab_idx);
	if (source_next_active >= 0) {
		(void)editorPaneViewActivateTab(source_view, source_next_active);
	} else {
		source_view->active_tab_idx = -1;
	}
	if (!editorPaneViewInsertTabAt(target_view, tab_idx, target_slot)) {
		(void)editorPaneViewInsertTabAt(source_view, tab_idx, source_slot);
		(void)editorPaneViewActivateTab(source_view, tab_idx);
		return 0;
	}
	(void)editorPaneViewActivateTab(target_view, tab_idx);
	if (!editorLayoutSetFocusedLeaf(target)) {
		editorPaneViewRemoveTab(target_view, tab_idx);
		(void)editorPaneViewInsertTabAt(source_view, tab_idx, source_slot);
		(void)editorPaneViewActivateTab(source_view, tab_idx);
		return 0;
	}
	/* editorLayoutSetFocusedLeaf captured source's view from live E, clobbering
	 * the active_tab_idx we just set. Re-apply. */
	if (source_next_active >= 0) {
		(void)editorPaneViewActivateTab(source_view, source_next_active);
	} else {
		source_view->active_tab_idx = -1;
	}
	editorTabsEnsurePaneOccupancy();
	return 1;
}

static int tabsCanReuseActiveEmptyBuffer(void) {
	if (E.tab_count <= 0) {
		return 0;
	}
	if (E.tab_kind != EDITOR_TAB_FILE) {
		return 0;
	}
	if (E.filename != NULL && E.filename[0] != '\0') {
		return 0;
	}
	if (E.dirty != 0) {
		return 0;
	}
	for (int row_idx = 0; row_idx < E.numrows; row_idx++) {
		if (editorDocumentLineLength(E.document, row_idx) != 0) {
			return 0;
		}
	}
	return 1;
}

static int tabsKindCanReuseAsPreview(enum editorTabKind tab_kind) {
	return tab_kind == EDITOR_TAB_FILE || tab_kind == EDITOR_TAB_UNSUPPORTED_FILE ||
	       tab_kind == EDITOR_TAB_GIT_DIFF;
}

static struct editorPaneView *tabsFocusedView(void) {
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split) {
		return &E.focused_leaf->as.leaf.view;
	}
	return NULL;
}

static int tabsViewTabIsPreview(const struct editorPaneView *view, int tab_idx) {
	if (view == NULL || tab_idx < 0 || view->preview_tab_idx != tab_idx) {
		return 0;
	}
	enum editorTabKind kind;
	if (tab_idx == E.active_tab) {
		kind = E.tab_kind;
	} else if (tab_idx < E.tab_count) {
		kind = E.tabs[tab_idx].tab_kind;
	} else {
		return 0;
	}
	return tabsKindCanReuseAsPreview(kind);
}

/* Only touches the focused pane, so a tab shared across a split keeps its
 * preview status in the other pane. */
static void tabsSetActivePreview(int preview) {
	struct editorPaneView *view = tabsFocusedView();
	if (view == NULL) {
		return;
	}
	if (preview) {
		view->preview_tab_idx = E.active_tab;
	} else if (view->preview_tab_idx == E.active_tab) {
		view->preview_tab_idx = -1;
	}
}

static int tabsFindReusablePreviewIndex(void) {
	struct editorPaneView *view = tabsFocusedView();
	if (view == NULL) {
		return -1;
	}
	int idx = view->preview_tab_idx;
	if (!tabsViewTabIsPreview(view, idx)) {
		return -1;
	}
	int dirty = idx == E.active_tab ? E.dirty : E.tabs[idx].dirty;
	return dirty == 0 ? idx : -1;
}

void editorTabPinActivePreview(void) {
	tabsSetActivePreview(0);
}

int editorActiveTabIsPreview(void) {
	return tabsViewTabIsPreview(tabsFocusedView(), E.active_tab);
}

static int tabsLoadUnsupportedFilePreview(const char *filename) {
	struct editorDocument document;
	int document_inited = 0;
	char *filename_copy = NULL;
	int ok = 0;

	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	filename_copy = strdup(filename);
	if (filename_copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	editorDocumentInit(&document);
	document_inited = 1;
	if (!editorDocumentResetFromString(&document, EDITOR_UNSUPPORTED_FILE_TEXT,
	                                   strlen(EDITOR_UNSUPPORTED_FILE_TEXT))) {
		editorSetAllocFailureStatus();
		goto cleanup;
	}

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	editorLspNotifyEslintDidClose(E.filename, E.syntax_language, &E.lsp_eslint_doc_open,
	                              &E.lsp_eslint_doc_version);
	editorFreeActiveBufferState();
	E.tab_kind = EDITOR_TAB_UNSUPPORTED_FILE;
	E.filename = filename_copy;
	filename_copy = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	if (!editorRestoreActiveFromDocument(&document, 0, 0, 0, 0)) {
		goto cleanup;
	}
	tabsSetActivePreview(1);
	ok = 1;

cleanup:
	if (document_inited) {
		editorDocumentFree(&document);
	}
	free(filename_copy);
	return ok;
}

int editorTabOpenFileAsNew(const char *filename) {
	if (tabsCanReuseActiveEmptyBuffer()) {
		if (!editorOpen(filename)) {
			return 0;
		}
		tabsSetActivePreview(0);
		(void)editorWorkspaceStateRememberRecentFile(filename);
		return 1;
	}
	if (!editorFileCanOpen(filename)) {
		return 0;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	if (!editorOpen(filename)) {
		return 0;
	}
	tabsSetActivePreview(0);
	(void)editorWorkspaceStateRememberRecentFile(filename);
	return 1;
}

int editorTabOpenOrSwitchToFile(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}

	int existing_tab = tabsFindEditableFileIndex(filename);
	if (existing_tab >= 0) {
		if (!editorTabSwitchToIndex(existing_tab)) {
			return 0;
		}
		tabsSetActivePreview(0);
		(void)editorWorkspaceStateRememberRecentFile(filename);
		return 1;
	}

	return editorTabOpenFileAsNew(filename);
}

static int tabsOpenPreviewFileInner(const char *filename, int preview_tab, int is_binary,
                                    int can_open, int allow_empty_reuse) {
	if (preview_tab >= 0) {
		if (can_open) {
			if (!editorTabSwitchToIndex(preview_tab)) {
				return 0;
			}
			if (!editorOpen(filename)) {
				return 0;
			}
			tabsSetActivePreview(1);
			return 1;
		}
		if (!is_binary) {
			return 0;
		}
		if (!editorTabSwitchToIndex(preview_tab)) {
			return 0;
		}
		return tabsLoadUnsupportedFilePreview(filename);
	}

	if (allow_empty_reuse && tabsCanReuseActiveEmptyBuffer()) {
		if (can_open) {
			if (!editorOpen(filename)) {
				return 0;
			}
			tabsSetActivePreview(1);
			return 1;
		}
		if (!is_binary) {
			return 0;
		}
		return tabsLoadUnsupportedFilePreview(filename);
	}
	if (!can_open && !is_binary) {
		return 0;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	if (can_open) {
		if (!editorOpen(filename)) {
			return 0;
		}
		tabsSetActivePreview(1);
		return 1;
	}
	return tabsLoadUnsupportedFilePreview(filename);
}

int editorTabOpenOrSwitchToPreviewFile(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}

	int existing_tab = tabsFindOpenFileIndexInFocusedPane(filename);
	if (existing_tab >= 0) {
		return editorTabSwitchToIndex(existing_tab);
	}

	int is_binary = 0;
	int can_open = 0;
	if (editorFilePathLooksBinary(filename, &is_binary) && is_binary) {
		can_open = 0;
	} else {
		can_open = editorFileCanOpen(filename);
	}

	int preview_tab = tabsFindReusablePreviewIndex();
	struct editorPaneView *focused = tabsFocusedView();
	/* Reusing a preview another pane also shows would swap the file shown there
	 * too. Leave the shared tab for that pane and open the new file in a fresh
	 * preview here instead. */
	int detach_shared = -1;
	if (preview_tab >= 0 && focused != NULL &&
	    editorPaneTreeAnyOtherPaneHasTab(E.layout_root, E.focused_leaf, preview_tab)) {
		detach_shared = preview_tab;
		preview_tab = -1;
	}

	int ok = tabsOpenPreviewFileInner(filename, preview_tab, is_binary, can_open,
	                                  detach_shared < 0);
	if (ok && detach_shared >= 0) {
		editorPaneViewRemoveTab(focused, detach_shared);
	}
	return ok;
}

static const char *tabsPathAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

/* Finds the file's tab within the focused pane only. Previewing a file open
 * only in another pane must not adopt (and share) that pane's tab, so the match
 * is scoped to give the focused pane its own preview. */
static int tabsFindOpenFileIndexInFocusedPane(const char *path) {
	struct editorPaneView *view = tabsFocusedView();
	if (view == NULL || path == NULL || path[0] == '\0') {
		return -1;
	}
	for (int i = 0; i < view->pane_tab_count; i++) {
		int tab_idx = view->pane_tabs[i];
		const char *tab_path = tabsPathAt(tab_idx);
		if (tab_path != NULL && tab_path[0] != '\0' &&
		    editorPathsReferToSameFile(path, tab_path)) {
			return tab_idx;
		}
	}
	return -1;
}

static int tabsFindEditableFileIndex(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}

	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		enum editorTabKind tab_kind =
		        tab_idx == E.active_tab ? E.tab_kind : E.tabs[tab_idx].tab_kind;
		if (tab_kind != EDITOR_TAB_FILE) {
			continue;
		}
		const char *tab_path = tabsPathAt(tab_idx);
		if (tab_path == NULL || tab_path[0] == '\0') {
			continue;
		}
		if (editorPathsReferToSameFile(tab_path, path)) {
			return tab_idx;
		}
	}

	return -1;
}

static void tabsRegisterWithFocusedPane(int idx) {
	if (idx < 0 || E.focused_leaf == NULL || E.focused_leaf->is_split) {
		return;
	}
	(void)editorPaneViewActivateTab(&E.focused_leaf->as.leaf.view, idx);
}

int editorTabSwitchToIndex(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		tabsRegisterWithFocusedPane(idx);
		return 1;
	}

	tabsStoreActiveTab();
	E.active_tab = idx;
	tabsLoadActiveTab(E.active_tab);
	tabsRegisterWithFocusedPane(idx);
	return 1;
}

int editorTabSwitchByDelta(int delta) {
	if (E.tab_count <= 0) {
		return 0;
	}
	if (delta == 0) {
		return 1;
	}

	/* Cycle within the focused pane's tab membership list, not the global
	 * tab array. Fall back to global cycling when no pane is focused or
	 * the pane has no recorded tabs. */
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split &&
	    E.focused_leaf->as.leaf.view.pane_tab_count > 0) {
		struct editorPaneView *view = &E.focused_leaf->as.leaf.view;
		int local = editorPaneViewIndexOfTab(view, E.active_tab);
		if (local < 0) {
			local = 0;
		}
		if (view->pane_tab_count == 1) {
			return 1;
		}
		int target_local = (local + delta) % view->pane_tab_count;
		if (target_local < 0) {
			target_local += view->pane_tab_count;
		}
		int target = view->pane_tabs[target_local];
		return editorTabSwitchToIndex(target);
	}

	if (E.tab_count == 1) {
		return 1;
	}
	int target = (E.active_tab + delta) % E.tab_count;
	if (target < 0) {
		target += E.tab_count;
	}
	return editorTabSwitchToIndex(target);
}

/*
 * skip_active_save=1: caller has freed or shifted E.tabs[E.active_tab], so
 * editorTabSwitchToIndex must not save the now-stale globals back into that
 * slot.
 */
static int tabsCloseFocusedPaneIfEmpty(int skip_active_save) {
	struct editorPaneNode *focused = E.focused_leaf;
	if (focused == NULL || focused->is_split ||
	    focused->as.leaf.kind != EDITOR_PANE_KIND_EDITOR ||
	    focused->as.leaf.view.pane_tab_count != 0) {
		return 0;
	}
	if (E.layout_root == NULL || editorPaneTreeLeafCount(E.layout_root) <= 1) {
		return 0;
	}
	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(&E.layout_root, focused);
	if (new_focus == NULL) {
		return 0;
	}
	E.focused_leaf = new_focus;
	if (skip_active_save) {
		E.active_tab = -1;
	}
	int target = new_focus->as.leaf.view.active_tab_idx;
	if (target >= 0 && target < E.tab_count) {
		(void)editorTabSwitchToIndex(target);
	} else {
		editorResetActiveBufferFields();
	}
	(void)editorPaneViewLoadIntoState(&new_focus->as.leaf.view);
	return 1;
}

static int tabsPaneMostRecentTab(const struct editorPaneView *view) {
	if (view == NULL || view->pane_tab_count <= 0) {
		return -1;
	}
	int next_idx = editorPaneViewMostRecentTab(view);
	if (next_idx < 0) {
		next_idx = view->pane_tabs[0];
	}
	return next_idx;
}

static void tabsFinishSharedTabCloseInFocusedPane(struct editorPaneNode *focused) {
	if (focused == NULL || focused->is_split ||
	    focused->as.leaf.kind != EDITOR_PANE_KIND_EDITOR) {
		return;
	}

	if (focused->as.leaf.view.pane_tab_count > 0) {
		int next_idx = tabsPaneMostRecentTab(&focused->as.leaf.view);
		if (next_idx >= 0) {
			(void)editorTabSwitchToIndex(next_idx);
		}
		return;
	}

	if (!tabsCloseFocusedPaneIfEmpty(0)) {
		int new_idx = editorTabAppendEmptyForPane(focused);
		if (new_idx >= 0) {
			(void)editorTabSwitchToIndex(new_idx);
		}
	}
}

static void tabsResetLastTabAfterClose(void) {
	tabsStateInitEmpty(&E.tabs[0]);
	E.active_tab = 0;
	E.tab_count = 1;
	tabsLoadActiveTab(0);
	tabsRegisterWithFocusedPane(0);
}

static int tabsHandleEmptyFocusedPaneAfterGlobalClose(struct editorPaneNode *focused,
                                                      int focused_is_editor) {
	if (!focused_is_editor || focused->as.leaf.view.pane_tab_count != 0) {
		return 0;
	}
	if (tabsCloseFocusedPaneIfEmpty(1)) {
		return 1;
	}
	int new_idx = editorTabAppendEmptyForPane(focused);
	if (new_idx >= 0) {
		E.active_tab = new_idx;
		tabsLoadActiveTab(new_idx);
		return 1;
	}
	return 0;
}

static int tabsNextIndexAfterGlobalClose(int closing, struct editorPaneNode *focused,
                                         int focused_is_editor) {
	int next_idx = -1;
	if (focused_is_editor && focused->as.leaf.view.pane_tab_count > 0) {
		next_idx = tabsPaneMostRecentTab(&focused->as.leaf.view);
	}
	if (next_idx < 0 || next_idx >= E.tab_count) {
		next_idx = closing < E.tab_count ? closing : E.tab_count - 1;
	}
	return next_idx;
}

int editorTabCloseActive(void) {
	if (E.tab_count <= 0 || E.tabs == NULL) {
		return 0;
	}

	/* Pane invariant: a pane's view never shows a tab outside its own pane_tabs[] list. */
	int closing = E.active_tab;
	struct editorPaneNode *focused = E.focused_leaf;
	int focused_is_editor = (focused != NULL && !focused->is_split &&
	                         focused->as.leaf.kind == EDITOR_PANE_KIND_EDITOR);

	if (focused_is_editor) {
		editorPaneViewRemoveTab(&focused->as.leaf.view, closing);
	}

	if (editorPaneTreeAnyPaneHasTab(E.layout_root, closing)) {
		if (focused_is_editor) {
			tabsFinishSharedTabCloseInFocusedPane(focused);
		}
		return 1;
	}

	tabsStoreActiveTab();
	editorLspNotifyDidCloseTabState(&E.tabs[closing]);
	tabsStateFree(&E.tabs[closing]);

	if (E.tab_count == 1) {
		tabsResetLastTabAfterClose();
		return 1;
	}

	memmove(&E.tabs[closing], &E.tabs[closing + 1],
	        sizeof(struct editorTabState) * (size_t)(E.tab_count - closing - 1));
	E.tab_count--;
	editorPaneTreeShiftTabIndicesAfterClose(E.layout_root, closing);

	if (tabsHandleEmptyFocusedPaneAfterGlobalClose(focused, focused_is_editor)) {
		return 1;
	}

	E.active_tab = tabsNextIndexAfterGlobalClose(closing, focused, focused_is_editor);
	tabsLoadActiveTab(E.active_tab);
	tabsRegisterWithFocusedPane(E.active_tab);
	return 1;
}

int editorTabCount(void) {
	return E.tab_count;
}

int editorTabActiveIndex(void) {
	return E.active_tab;
}

int editorTabAnyDirty(void) {
	if (E.tab_count <= 0) {
		return E.dirty != 0;
	}
	if (E.dirty) {
		return 1;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (i == E.active_tab) {
			continue;
		}
		if (E.tabs[i].dirty) {
			return 1;
		}
	}
	return 0;
}

const char *editorTabFilenameAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

static int tabsKindUsesTitle(enum editorTabKind kind) {
	return kind == EDITOR_TAB_TASK_LOG || kind == EDITOR_TAB_GIT_DIFF ||
	       kind == EDITOR_TAB_GIT_COMMIT || kind == EDITOR_TAB_GIT_BRANCHES ||
	       kind == EDITOR_TAB_GIT_LOG || kind == EDITOR_TAB_GIT_STASH;
}

const char *editorTabDisplayNameAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return "[No Name]";
	}
	if (E.tabs[idx].kind == EDITOR_PANE_KIND_TERMINAL) {
		return "Terminal";
	}
	if (E.tabs[idx].kind == EDITOR_PANE_KIND_DEBUG_CONSOLE) {
		return "Debug Console";
	}
	if (idx == E.active_tab) {
		if (tabsKindUsesTitle(E.tab_kind) && E.tab_title != NULL &&
		    E.tab_title[0] != '\0') {
			return E.tab_title;
		}
		return E.filename != NULL ? E.filename : "[No Name]";
	}
	if (tabsKindUsesTitle(E.tabs[idx].tab_kind) && E.tabs[idx].tab_title != NULL &&
	    E.tabs[idx].tab_title[0] != '\0') {
		return E.tabs[idx].tab_title;
	}
	return E.tabs[idx].filename != NULL ? E.tabs[idx].filename : "[No Name]";
}

const char *editorActiveBufferDisplayName(void) {
	if (tabsKindUsesTitle(E.tab_kind) && E.tab_title != NULL && E.tab_title[0] != '\0') {
		return E.tab_title;
	}
	return E.filename != NULL ? E.filename : "[No Name]";
}

int editorTabDirtyAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return E.dirty != 0;
	}
	return E.tabs[idx].dirty != 0;
}

int editorActiveTabIsTaskLog(void) {
	return E.tab_kind == EDITOR_TAB_TASK_LOG;
}

int editorActiveTabIsUnsupportedFile(void) {
	return E.tab_kind == EDITOR_TAB_UNSUPPORTED_FILE;
}

int editorActiveTabIsReadOnly(void) {
	return E.tab_kind == EDITOR_TAB_TASK_LOG || E.tab_kind == EDITOR_TAB_UNSUPPORTED_FILE ||
	       E.tab_kind == EDITOR_TAB_GIT_DIFF || E.tab_kind == EDITOR_TAB_GIT_BRANCHES ||
	       E.tab_kind == EDITOR_TAB_GIT_LOG || E.tab_kind == EDITOR_TAB_GIT_STASH;
}

int editorActiveTaskTabIsRunning(void) {
	return E.task_running && E.task_tab_idx == E.active_tab &&
	       E.tab_kind == EDITOR_TAB_TASK_LOG;
}

static void tabsTaskLogClampCursor(struct editorTabState *tab) {
	if (tab == NULL) {
		return;
	}
	if (tab->cy < 0) {
		tab->cy = 0;
	} else if (tab->cy > tab->numrows) {
		tab->cy = tab->numrows;
	}
	if (tab->cy >= tab->numrows) {
		tab->cx = 0;
		return;
	}
	if (tab->cx < 0) {
		tab->cx = 0;
	}
	struct editorLineView line = {0};
	if (editorDocumentLineView(tab->document, tab->cy, &line)) {
		if (tab->cx > line.size) {
			tab->cx = line.size;
		}
		tab->cx = editorBytesClampCxToClusterBoundary(line.data, line.size, tab->cx);
		editorLineViewRelease(&line);
	}
}

static int tabsRebuildGeneratedTabRows(struct editorTabState *tab) {
	if (tab == NULL) {
		return 0;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	struct editorRow *new_rows = NULL;
	int new_numrows = 0;
	if (!editorBuildFullRowsFromDocument(tab->document, &new_rows, &new_numrows)) {
		return 0;
	}

	tabsFreeTabRows(tab);
	tab->rows = new_rows;
	tab->numrows = new_numrows;
	tab->dirty = 0;
	free(tab->filename);
	tab->filename = NULL;
	memset(&tab->disk_state, 0, sizeof(tab->disk_state));
	tab->disk_conflict = 0;
	editorSyntaxStateDestroy(tab->syntax_state);
	tab->syntax_state = NULL;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	tab->lsp_doc_open = 0;
	tab->lsp_doc_version = 0;
	tab->lsp_eslint_doc_open = 0;
	tab->lsp_eslint_doc_version = 0;
	if (tab->lsp_diagnostics != NULL) {
		for (int i = 0; i < tab->lsp_diagnostic_count; i++) {
			free(tab->lsp_diagnostics[i].message);
		}
		free(tab->lsp_diagnostics);
	}
	tab->lsp_diagnostics = NULL;
	tab->lsp_diagnostic_count = 0;
	tab->lsp_diagnostic_error_count = 0;
	tab->lsp_diagnostic_warning_count = 0;
	if (tab->lsp_symbols != NULL) {
		editorLspFreeSymbols(tab->lsp_symbols, tab->lsp_symbol_count);
	}
	tab->lsp_symbols = NULL;
	tab->lsp_symbol_count = 0;
	tabsTaskLogClampCursor(tab);
	return 1;
}

static int tabsTaskMutateTab(int tab_idx, int jump_to_end,
                             int (*mutator)(struct editorTabState *tab, void *ctx), void *ctx) {
	if (mutator == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		return 0;
	}

	if (tab_idx == E.active_tab) {
		tabsStoreActiveTab();
		if (!mutator(&E.tabs[tab_idx], ctx)) {
			tabsLoadActiveTab(tab_idx);
			return 0;
		}
		tabsLoadActiveTab(tab_idx);
		if (jump_to_end) {
			if (E.numrows > 0) {
				E.cy = E.numrows - 1;
				E.cx = (int)editorDocumentLineLength(E.document, E.cy);
			} else {
				E.cy = 0;
				E.cx = 0;
			}
			editorViewportEnsureCursorVisible();
		}
		return 1;
	}

	return mutator(&E.tabs[tab_idx], ctx);
}

struct tabsTaskAppendContext {
	const char *text;
	size_t len;
};

static int tabsTaskAppendOutputMutator(struct editorTabState *tab, void *ctx) {
	static const char truncation_note[] = "\n[output truncated]\n";
	struct tabsTaskAppendContext *append = ctx;
	size_t log_limit = ROTIDE_TASK_LOG_MAX_BYTES - (sizeof(truncation_note) - 1);
	size_t append_len = 0;
	size_t old_len = 0;

	if (tab == NULL || append == NULL) {
		return 0;
	}
	if (E.task_output_truncated) {
		return 1;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	old_len = editorDocumentLength(tab->document);

	if (old_len < log_limit) {
		append_len = append->len;
		if (append_len > log_limit - old_len) {
			append_len = log_limit - old_len;
		}
		if (append_len > 0 && !editorDocumentReplaceRange(tab->document, old_len, 0,
		                                                  append->text, append_len)) {
			return 0;
		}
		E.task_output_bytes += append_len;
	}

	if (append_len < append->len) {
		if (!editorDocumentReplaceRange(tab->document, editorDocumentLength(tab->document),
		                                0, truncation_note, sizeof(truncation_note) - 1)) {
			return 0;
		}
		E.task_output_truncated = 1;
	}

	if (!tabsRebuildGeneratedTabRows(tab)) {
		return 0;
	}
	return 1;
}

static int tabsTaskAppendOutput(int tab_idx, const char *text, size_t len, int jump_to_end) {
	struct tabsTaskAppendContext ctx = {.text = text, .len = len};
	return tabsTaskMutateTab(tab_idx, jump_to_end, tabsTaskAppendOutputMutator, &ctx);
}

static void tabsTaskResetState(void) {
	if (E.task_running && E.task_output_fd > STDERR_FILENO) {
		close(E.task_output_fd);
	}
	E.task_pid = 0;
	E.task_output_fd = -1;
	E.task_running = 0;
	E.task_tab_idx = -1;
	E.task_output_truncated = 0;
	E.task_output_bytes = 0;
	E.task_exit_code = 0;
	E.task_success_status[0] = '\0';
	E.task_failure_status[0] = '\0';
}

int editorTaskIsRunning(void) {
	return E.task_running;
}

int editorTaskRunningTabIndex(void) {
	return E.task_running ? E.task_tab_idx : -1;
}

static int tabsTaskDrainOutput(int tab_idx, int jump_to_end, int *saw_eof_out) {
	char buf[4096];
	int changed = 0;

	if (saw_eof_out != NULL) {
		*saw_eof_out = 0;
	}
	if (E.task_output_fd == -1) {
		if (saw_eof_out != NULL) {
			*saw_eof_out = 1;
		}
		return 0;
	}

	for (;;) {
		ssize_t nread = read(E.task_output_fd, buf, sizeof(buf));
		if (nread > 0) {
			if (!tabsTaskAppendOutput(tab_idx, buf, (size_t)nread, jump_to_end)) {
				editorSetAllocFailureStatus();
				return changed;
			}
			changed = 1;
			continue;
		}
		if (nread == 0) {
			close(E.task_output_fd);
			E.task_output_fd = -1;
			if (saw_eof_out != NULL) {
				*saw_eof_out = 1;
			}
			return 1;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return changed;
		}
		close(E.task_output_fd);
		E.task_output_fd = -1;
		if (saw_eof_out != NULL) {
			*saw_eof_out = 1;
		}
		return 1;
	}
}

static int tabsTaskAppendFinalLineMutator(struct editorTabState *tab, void *ctx) {
	const char *line = ctx;
	if (tab == NULL || line == NULL) {
		return 0;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	if (!editorDocumentReplaceRange(tab->document, editorDocumentLength(tab->document), 0, line,
	                                strlen(line))) {
		return 0;
	}
	return tabsRebuildGeneratedTabRows(tab);
}

static void tabsTaskFinalize(int success, int exit_code) {
	char final_line[96];
	int tab_idx = E.task_tab_idx;
	if (tab_idx >= 0 && tab_idx < E.tab_count) {
		if (success) {
			(void)snprintf(final_line, sizeof(final_line),
			               "\n[task completed successfully]\n");
		} else {
			(void)snprintf(final_line, sizeof(final_line),
			               "\n[task failed with exit code %d]\n", exit_code);
		}
		(void)tabsTaskMutateTab(tab_idx, 1, tabsTaskAppendFinalLineMutator, final_line);
	}

	E.task_exit_code = exit_code;
	if (success) {
		tabsTaskSetFinalStatus(1);
	} else {
		tabsTaskSetFinalStatus(0);
	}
	tabsTaskResetState();
	editorGitRefresh();
}

static void tabsTaskSetFinalStatus(int success) {
	if (success) {
		if (E.task_success_status[0] != '\0') {
			editorSetStatusMsg("%s", E.task_success_status);
			return;
		}
		editorSetStatusMsg("Task finished successfully");
		return;
	}

	if (E.task_failure_status[0] != '\0') {
		editorSetStatusMsg("%s", E.task_failure_status);
		return;
	}
	editorSetStatusMsg("Task failed");
}

static int tabsTaskPrepareLogTab(const char *title, const char *text) {
	if (title == NULL || title[0] == '\0' || text == NULL) {
		return 0;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}

	E.tab_kind = EDITOR_TAB_TASK_LOG;
	E.tab_title = strdup(title);
	if (E.tab_title == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	E.dirty = 0;
	free(E.filename);
	E.filename = NULL;
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	E.lsp_eslint_doc_open = 0;
	E.lsp_eslint_doc_version = 0;
	memset(&E.disk_state, 0, sizeof(E.disk_state));
	E.disk_conflict = 0;
	if (!editorDocumentResetActiveFromText(text, strlen(text))) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorRestoreActiveFromDocument(E.document, 0, 0, 0, 0)) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (E.numrows > 0) {
		E.cy = E.numrows - 1;
		E.cx = (int)editorDocumentLineLength(E.document, E.cy);
	}
	editorViewportEnsureCursorVisible();
	tabsStoreActiveTab();
	tabsLoadActiveTab(E.active_tab);
	return 1;
}

/* Replaces the active buffer's document with `text` (generated-tab reload). */
static int tabsResetActiveDocumentText(const char *text) {
	if (!editorDocumentResetActiveFromText(text, strlen(text))) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorRestoreActiveFromDocument(E.document, 0, 0, 0, 0)) {
		editorSetAllocFailureStatus();
		return 0;
	}
	E.dirty = 0;
	if (E.cy >= E.numrows) {
		E.cy = E.numrows > 0 ? E.numrows - 1 : 0;
	}
	E.cx = 0;
	(void)editorSyntaxParseFullActive();
	editorViewportEnsureCursorVisible();
	tabsStoreActiveTab();
	tabsLoadActiveTab(E.active_tab);
	return 1;
}

/* Converts the active tab (empty or reusable preview) into a generated tab,
 * releasing the previous occupant's file/LSP/syntax state first. */
static int tabsLoadGeneratedIntoActive(enum editorTabKind kind, const char *title,
                                       const char *text) {
	struct editorDocument document;
	char *title_copy = strdup(title);
	if (title_copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	editorDocumentInit(&document);
	if (!editorDocumentResetFromString(&document, text, strlen(text))) {
		editorSetAllocFailureStatus();
		editorDocumentFree(&document);
		free(title_copy);
		return 0;
	}

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	editorLspNotifyEslintDidClose(E.filename, E.syntax_language, &E.lsp_eslint_doc_open,
	                              &E.lsp_eslint_doc_version);
	editorFreeActiveBufferState();
	E.tab_kind = kind;
	E.tab_title = title_copy;
	/* Diff tabs behave like previews so browsing diffs reuses one tab slot;
	 * the views are deduped singletons and the commit tab holds a message
	 * being typed. */
	tabsSetActivePreview(kind == EDITOR_TAB_GIT_DIFF);
	E.dirty = 0;
	memset(&E.disk_state, 0, sizeof(E.disk_state));
	E.disk_conflict = 0;
	int ok = editorRestoreActiveFromDocument(&document, 0, 0, 0, 0);
	editorDocumentFree(&document);
	if (!ok) {
		return 0;
	}
	E.cy = 0;
	E.cx = 0;
	(void)editorSyntaxParseFullActive();
	editorViewportEnsureCursorVisible();
	tabsStoreActiveTab();
	tabsLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabOpenGenerated(enum editorTabKind kind, const char *title, const char *text) {
	if (title == NULL || title[0] == '\0' || text == NULL) {
		return 0;
	}

	/* Diff and commit tabs are keyed by title (per-file diffs coexist; a
	 * commit and its amend variant are distinct surfaces); the list views are
	 * singletons whose content regenerates on reopen. A matched commit tab is
	 * reused as-is so a typed message is never clobbered. */
	int dedupe_by_title = kind == EDITOR_TAB_GIT_DIFF || kind == EDITOR_TAB_GIT_COMMIT;
	for (int idx = 0; idx < E.tab_count; idx++) {
		const char *existing_title =
		        idx == E.active_tab ? E.tab_title : E.tabs[idx].tab_title;
		enum editorTabKind existing_kind =
		        idx == E.active_tab ? E.tab_kind : E.tabs[idx].tab_kind;
		if (existing_kind != kind) {
			continue;
		}
		if (dedupe_by_title &&
		    (existing_title == NULL || strcmp(existing_title, title) != 0)) {
			continue;
		}
		if (!editorTabSwitchToIndex(idx)) {
			return 0;
		}
		if (kind == EDITOR_TAB_GIT_COMMIT) {
			return 1;
		}
		return tabsResetActiveDocumentText(text);
	}

	if (kind == EDITOR_TAB_GIT_DIFF) {
		int preview_idx = tabsFindReusablePreviewIndex();
		if (preview_idx >= 0) {
			if (!editorTabSwitchToIndex(preview_idx)) {
				return 0;
			}
			return tabsLoadGeneratedIntoActive(kind, title, text);
		}
	}
	if (tabsCanReuseActiveEmptyBuffer()) {
		return tabsLoadGeneratedIntoActive(kind, title, text);
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	return tabsLoadGeneratedIntoActive(kind, title, text);
}

int editorTaskPoll(void) {
	int changed = 0;
	int saw_eof = 0;
	int status = 0;
	pid_t waited = 0;

	if (!E.task_running || E.task_tab_idx < 0) {
		return 0;
	}

	changed |= tabsTaskDrainOutput(E.task_tab_idx, 1, &saw_eof);

	do {
		waited = waitpid(E.task_pid, &status, WNOHANG);
	} while (waited == -1 && errno == EINTR);

	if (waited == E.task_pid) {
		int exit_code = 1;
		changed |= tabsTaskDrainOutput(E.task_tab_idx, 1, &saw_eof);
		if (WIFEXITED(status)) {
			exit_code = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			exit_code = 128 + WTERMSIG(status);
		}
		tabsTaskFinalize(exit_code == 0, exit_code);
		return 1;
	}

	if (saw_eof && E.task_output_fd == -1 && E.task_pid > 0) {
		return 1;
	}

	return changed;
}

int editorTaskTerminate(void) {
	int status = 0;
	int exit_code = 1;

	if (!E.task_running || E.task_pid <= 0) {
		return 1;
	}

	(void)kill(E.task_pid, SIGTERM);
	do {
		if (waitpid(E.task_pid, &status, 0) == E.task_pid) {
			break;
		}
	} while (errno == EINTR);

	(void)tabsTaskDrainOutput(E.task_tab_idx, 1, NULL);
	if (WIFEXITED(status)) {
		exit_code = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exit_code = 128 + WTERMSIG(status);
	}
	tabsTaskFinalize(0, exit_code);
	return 1;
}

int editorTaskStart(const char *title, const char *command, const char *success_status,
                    const char *failure_status) {
	int output_pipe[2] = {-1, -1};
	pid_t pid = 0;
	int flags = 0;
	char header[PATH_MAX + 8];

	if (title == NULL || title[0] == '\0' || command == NULL || command[0] == '\0') {
		return 0;
	}
	if (E.task_running) {
		editorSetStatusMsg("Another task is already running");
		return 0;
	}
	(void)snprintf(header, sizeof(header), "$ %s\n\n", command);
	if (!tabsTaskPrepareLogTab(title, header)) {
		return 0;
	}

	if (pipe(output_pipe) == -1) {
		editorSetStatusMsg("Unable to start task");
		return 0;
	}

	pid = fork();
	if (pid == -1) {
		close(output_pipe[0]);
		close(output_pipe[1]);
		editorSetStatusMsg("Unable to start task");
		return 0;
	}

	if (pid == 0) {
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull != -1) {
			(void)dup2(devnull, STDIN_FILENO);
			close(devnull);
		}
		(void)dup2(output_pipe[1], STDOUT_FILENO);
		(void)dup2(output_pipe[1], STDERR_FILENO);
		close(output_pipe[0]);
		close(output_pipe[1]);
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	close(output_pipe[1]);
	flags = fcntl(output_pipe[0], F_GETFL);
	if (flags != -1) {
		(void)fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
	}

	E.task_pid = pid;
	E.task_output_fd = output_pipe[0];
	E.task_running = 1;
	E.task_tab_idx = E.active_tab;
	E.task_output_truncated = 0;
	E.task_output_bytes = 0;
	E.task_exit_code = 0;
	if (success_status != NULL) {
		(void)snprintf(E.task_success_status, sizeof(E.task_success_status), "%s",
		               success_status);
	} else {
		E.task_success_status[0] = '\0';
	}
	if (failure_status != NULL) {
		(void)snprintf(E.task_failure_status, sizeof(E.task_failure_status), "%s",
		               failure_status);
	} else {
		E.task_failure_status[0] = '\0';
	}
	editorSetStatusMsg("Running task: %s", title);
	return 1;
}

int editorTaskShowMessage(const char *title, const char *text, const char *status) {
	if (!tabsTaskPrepareLogTab(title, text)) {
		return 0;
	}
	if (status != NULL && status[0] != '\0') {
		editorSetStatusMsg("%s", status);
	}
	return 1;
}

static const char *tabsLabelFromDisplayName(const char *display_name) {
	if (display_name == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(display_name, '/');
	if (slash != NULL && slash[1] != '\0') {
		return slash + 1;
	}
	return display_name;
}

static int tabsSanitizedTokenDisplayCols(const char *text, int text_len, int *src_len_out) {
	unsigned int cp = 0;
	int src_len = editorUtf8DecodeCodepoint(text, text_len, &cp);
	if (src_len <= 0) {
		src_len = 1;
	}
	if (src_len > text_len) {
		src_len = text_len;
	}
	if (src_len_out != NULL) {
		*src_len_out = src_len;
	}

	if (cp == '\t' || cp <= 0x1F || cp == 0x7F) {
		return 2;
	}
	if (cp >= 0x80 && cp <= 0x9F) {
		return 4;
	}
	return editorCharDisplayWidth(text, text_len);
}

static int tabsSanitizedTextDisplayCols(const char *text, int max_cols) {
	if (text == NULL) {
		return 0;
	}

	int text_len = (int)strlen(text);
	int total_cols = 0;
	for (int idx = 0; idx < text_len;) {
		int src_len = 0;
		int token_cols =
		        tabsSanitizedTokenDisplayCols(&text[idx], text_len - idx, &src_len);
		if (max_cols >= 0 && total_cols + token_cols > max_cols) {
			break;
		}
		total_cols += token_cols;
		idx += src_len;
	}

	return total_cols;
}

static int tabsLabelColsAt(int tab_idx) {
	const char *label = tabsLabelFromDisplayName(editorTabDisplayNameAt(tab_idx));
	int cols = tabsSanitizedTextDisplayCols(label, ROTIDE_TAB_TITLE_MAX_COLS);
	if (cols < 1) {
		cols = 1;
	}
	return cols;
}

static int tabsWidthColsAt(int tab_idx) {
	return 6 + tabsLabelColsAt(tab_idx);
}

static int tabsPaneSlotCount(const struct editorPaneView *view) {
	if (view != NULL && view->pane_tab_count > 0) {
		return view->pane_tab_count;
	}
	return E.tab_count;
}

static int tabsPaneTabAt(const struct editorPaneView *view, int slot) {
	if (slot < 0) {
		return -1;
	}
	if (view != NULL && view->pane_tab_count > 0) {
		if (slot >= view->pane_tab_count) {
			return -1;
		}
		return view->pane_tabs[slot];
	}
	if (slot >= E.tab_count) {
		return -1;
	}
	return slot;
}

static int tabsPaneSlotForTab(const struct editorPaneView *view, int tab_idx) {
	if (tab_idx < 0 || tab_idx >= E.tab_count) {
		return -1;
	}
	if (view != NULL && view->pane_tab_count > 0) {
		return editorPaneViewIndexOfTab(view, tab_idx);
	}
	return tab_idx;
}

static int tabsPaneHasVisibleSlot(const struct editorPaneView *view, int slot) {
	int tab_idx = tabsPaneTabAt(view, slot);
	return tab_idx >= 0 && tab_idx < E.tab_count;
}

static void tabsVisibleRangeFromStartForPane(const struct editorPaneView *view, int start_slot,
                                             int cols, int *last_slot_out) {
	int last_slot = start_slot - 1;
	int slot_count = tabsPaneSlotCount(view);
	if (E.tab_count <= 0 || slot_count <= 0 || cols <= 0 || start_slot < 0 ||
	    start_slot >= slot_count) {
		*last_slot_out = last_slot;
		return;
	}

	int used_cols = 0;
	for (int slot = start_slot; slot < slot_count; slot++) {
		int tab_idx = tabsPaneTabAt(view, slot);
		if (tab_idx < 0 || tab_idx >= E.tab_count) {
			continue;
		}
		int width_cols = tabsWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (slot == start_slot && width_cols > cols) {
			width_cols = cols;
		}
		if (slot > start_slot && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		used_cols += width_cols;
		last_slot = slot;
		if (used_cols >= cols) {
			break;
		}
	}

	if (last_slot < start_slot && cols > 0) {
		last_slot = start_slot;
	}
	*last_slot_out = last_slot;
}

static void tabsAlignPaneViewToActiveForWidth(struct editorPaneView *view, int cols) {
	if (view == NULL) {
		return;
	}
	int slot_count = tabsPaneSlotCount(view);
	if (E.tab_count <= 0 || slot_count <= 0) {
		view->tab_view_start = 0;
		return;
	}

	if (view->tab_view_start < 0) {
		view->tab_view_start = 0;
	}
	if (view->tab_view_start >= slot_count) {
		view->tab_view_start = slot_count - 1;
	}

	int active_slot = tabsPaneSlotForTab(view, view->active_tab_idx);
	if (active_slot < 0) {
		return;
	}

	if (cols <= 0) {
		if (active_slot < view->tab_view_start) {
			view->tab_view_start = active_slot;
		}
		return;
	}

	if (active_slot < view->tab_view_start) {
		view->tab_view_start = active_slot;
	}

	int last_visible = view->tab_view_start;
	tabsVisibleRangeFromStartForPane(view, view->tab_view_start, cols, &last_visible);
	while (active_slot > last_visible && view->tab_view_start < active_slot) {
		view->tab_view_start++;
		tabsVisibleRangeFromStartForPane(view, view->tab_view_start, cols, &last_visible);
	}
}

static int tabsWrapperView(struct editorPaneView *scratch, struct editorPaneView **view_out) {
	if (view_out == NULL) {
		return 0;
	}
	if (E.focused_leaf != NULL && !E.focused_leaf->is_split) {
		E.focused_leaf->as.leaf.view.active_tab_idx = E.active_tab;
		*view_out = &E.focused_leaf->as.leaf.view;
		return 1;
	}
	editorPaneViewInit(scratch);
	scratch->active_tab_idx = E.active_tab;
	*view_out = scratch;
	return 1;
}

enum tabsPlaceTabResult { TABS_PLACE_TAB_SKIP = 0, TABS_PLACE_TAB_PLACED, TABS_PLACE_TAB_STOP };

static enum tabsPlaceTabResult tabsTryPlaceTab(struct editorPaneView *view, int slot, int cols,
                                               struct editorTabLayoutEntry *entries,
                                               int max_entries, int *used_cols_io, int *count_io,
                                               int *first_visible_slot_io,
                                               int *last_visible_slot_io) {
	int count = *count_io;
	int used_cols = *used_cols_io;

	if (count >= max_entries) {
		return TABS_PLACE_TAB_STOP;
	}

	int tab_idx = tabsPaneTabAt(view, slot);
	if (tab_idx < 0 || tab_idx >= E.tab_count) {
		return TABS_PLACE_TAB_SKIP;
	}

	int width_cols = tabsWidthColsAt(tab_idx);
	if (width_cols < 1) {
		width_cols = 1;
	}
	if (count == 0 && width_cols > cols) {
		width_cols = cols;
	}
	if (count > 0 && used_cols + width_cols > cols) {
		return TABS_PLACE_TAB_STOP;
	}
	if (width_cols <= 0) {
		return TABS_PLACE_TAB_STOP;
	}

	struct editorTabLayoutEntry *entry = &entries[count];
	entry->tab_idx = tab_idx;
	entry->start_col = used_cols;
	entry->width_cols = width_cols;
	entry->show_left_overflow = 0;
	entry->show_right_overflow = 0;
	entry->is_active = tab_idx == view->active_tab_idx;
	entry->is_preview = tabsViewTabIsPreview(view, tab_idx);

	if (*first_visible_slot_io < 0) {
		*first_visible_slot_io = slot;
	}
	*last_visible_slot_io = slot;
	*used_cols_io = used_cols + width_cols;
	*count_io = count + 1;
	return TABS_PLACE_TAB_PLACED;
}

int editorTabBuildLayoutForPane(struct editorPaneView *view, int cols,
                                struct editorTabLayoutEntry *entries, int max_entries,
                                int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (view == NULL) {
		return 0;
	}
	if (E.tab_count <= 0 || cols <= 0 || max_entries == 0) {
		if (E.tab_count <= 0) {
			view->tab_view_start = 0;
		}
		return 1;
	}
	if (entries == NULL || max_entries < 0) {
		return 0;
	}

	tabsAlignPaneViewToActiveForWidth(view, cols);
	int slot_count = tabsPaneSlotCount(view);
	int start_slot = view->tab_view_start;
	if (start_slot < 0) {
		start_slot = 0;
	}
	if (start_slot >= slot_count) {
		start_slot = slot_count - 1;
	}
	view->tab_view_start = start_slot;

	int used_cols = 0;
	int count = 0;
	int first_visible_slot = -1;
	int last_visible_slot = -1;
	for (int slot = start_slot; slot < slot_count && used_cols < cols; slot++) {
		enum tabsPlaceTabResult result =
		        tabsTryPlaceTab(view, slot, cols, entries, max_entries, &used_cols, &count,
		                        &first_visible_slot, &last_visible_slot);
		if (result == TABS_PLACE_TAB_STOP) {
			break;
		}
	}

	if (count == 0) {
		struct editorTabLayoutEntry *entry = &entries[0];
		entry->tab_idx = tabsPaneTabAt(view, start_slot);
		entry->start_col = 0;
		entry->width_cols = cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;
		entry->is_active = entry->tab_idx == view->active_tab_idx;
		entry->is_preview = tabsViewTabIsPreview(view, entry->tab_idx);
		first_visible_slot = start_slot;
		last_visible_slot = start_slot;
		count = 1;
	}

	int has_more_left = 0;
	int has_more_right = 0;
	for (int slot = 0; slot < first_visible_slot; slot++) {
		if (tabsPaneHasVisibleSlot(view, slot)) {
			has_more_left = 1;
			break;
		}
	}
	for (int slot = last_visible_slot + 1; slot < slot_count; slot++) {
		if (tabsPaneHasVisibleSlot(view, slot)) {
			has_more_right = 1;
			break;
		}
	}
	if (count > 0) {
		entries[0].show_left_overflow = has_more_left;
		entries[count - 1].show_right_overflow = has_more_right;
	}

	if (count_out != NULL) {
		*count_out = count;
	}
	return 1;
}

int editorTabBuildLayoutForWidth(int cols, struct editorTabLayoutEntry *entries, int max_entries,
                                 int *count_out) {
	struct editorPaneView scratch;
	struct editorPaneView *view = NULL;
	if (!tabsWrapperView(&scratch, &view)) {
		return 0;
	}
	return editorTabBuildLayoutForPane(view, cols, entries, max_entries, count_out);
}

int editorTabHitTestColumnForPane(struct editorPaneView *view, int col, int cols) {
	if (col < 0 || col >= cols || E.tab_count <= 0 || cols <= 0) {
		return -1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForPane(view, cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
		return -1;
	}
	for (int i = 0; i < layout_count; i++) {
		int start_col = layout[i].start_col;
		int end_col = start_col + layout[i].width_cols;
		if (col >= start_col && col < end_col) {
			return layout[i].tab_idx;
		}
	}
	return -1;
}

int editorTabHitTestColumn(int col, int cols) {
	struct editorPaneView scratch;
	struct editorPaneView *view = NULL;
	if (!tabsWrapperView(&scratch, &view)) {
		return -1;
	}
	return editorTabHitTestColumnForPane(view, col, cols);
}

int editorTabOverflowHitTestColumnForPane(struct editorPaneView *view, int col, int cols) {
	if (col < 0 || col >= cols || E.tab_count <= 0 || cols <= 0) {
		return -1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForPane(view, cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
		return -1;
	}
	for (int i = 0; i < layout_count; i++) {
		int start_col = layout[i].start_col;
		int end_col = start_col + layout[i].width_cols;
		if (layout[i].show_right_overflow && col == end_col - 1) {
			int slot = tabsPaneSlotForTab(view, layout[i].tab_idx);
			return tabsPaneTabAt(view, slot + 1);
		}
		if (layout[i].show_left_overflow && col == start_col) {
			int slot = tabsPaneSlotForTab(view, layout[i].tab_idx);
			return tabsPaneTabAt(view, slot - 1);
		}
	}
	return -1;
}

int editorTabOverflowHitTestColumn(int col, int cols) {
	struct editorPaneView scratch;
	struct editorPaneView *view = NULL;
	if (!tabsWrapperView(&scratch, &view)) {
		return -1;
	}
	return editorTabOverflowHitTestColumnForPane(view, col, cols);
}
