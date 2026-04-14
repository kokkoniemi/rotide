#include "workspace/tabs.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/task.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void editorTabStateInitEmpty(struct editorTabState *tab);
static void editorTabStateFree(struct editorTabState *tab);
static void editorTabStateCaptureActive(struct editorTabState *tab);
static void editorTabStateLoadActive(struct editorTabState *tab);
static int editorEnsureTabCapacity(int needed);
static void editorStoreActiveTab(void);
static void editorLoadActiveTab(int tab_idx);
static int editorTabCanReuseActiveEmptyBuffer(void);
static int editorTabFindReusablePreviewIndex(void);
static const char *editorTabPathAt(int idx);
static int editorTabFindOpenFileIndex(const char *path);
static void editorTaskLogClampCursor(struct editorTabState *tab);
static int editorRebuildGeneratedTabRows(struct editorTabState *tab);
static int editorTaskMutateTab(int tab_idx, int jump_to_end,
		int (*mutator)(struct editorTabState *tab, void *ctx), void *ctx);
static void editorTaskResetState(void);
static void editorTaskFinalize(int success, int exit_code);
static void editorTaskSetFinalStatus(int success);
static int editorTaskPrepareLogTab(const char *title, const char *text);
static const char *editorTabLabelFromDisplayName(const char *display_name);
static int editorSanitizedTokenDisplayCols(const char *text, int text_len, int *src_len_out);
static int editorSanitizedTextDisplayCols(const char *text, int max_cols);
static int editorTabLabelColsAt(int tab_idx);
static int editorTabWidthColsAt(int tab_idx);
static void editorTabVisibleRangeFromStart(int start_idx, int cols, int *last_idx_out);
static void editorTabsAlignViewToActiveForWidth(int cols);

static void editorTabStateInitEmpty(struct editorTabState *tab) {
	memset(tab, 0, sizeof(*tab));
	tab->tab_kind = EDITOR_TAB_FILE;
	tab->is_preview = 0;
	tab->document = NULL;
	tab->cursor_offset = 0;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	tab->syntax_state = NULL;
	tab->lsp_doc_open = 0;
	tab->lsp_doc_version = 0;
	tab->lsp_diagnostics = NULL;
	tab->lsp_diagnostic_count = 0;
	tab->lsp_diagnostic_error_count = 0;
	tab->lsp_diagnostic_warning_count = 0;
	tab->max_render_cols = 0;
	tab->max_render_cols_valid = 1;
	tab->search_match_offset = 0;
	tab->search_match_len = 0;
	tab->search_direction = 1;
	tab->search_saved_offset = 0;
}

void editorResetActiveBufferFields(void) {
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = 0;
	E.tab_title = NULL;
	E.cursor_offset = 0;
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.document = NULL;
	E.max_render_cols = 0;
	E.max_render_cols_valid = 1;
	E.dirty = 0;
	E.filename = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.syntax_state = NULL;
	E.lsp_doc_open = 0;
	E.lsp_doc_version = 0;
	E.lsp_diagnostics = NULL;
	E.lsp_diagnostic_count = 0;
	E.lsp_diagnostic_error_count = 0;
	E.lsp_diagnostic_warning_count = 0;
	E.search_query = NULL;
	E.search_match_offset = 0;
	E.search_match_len = 0;
	E.search_direction = 1;
	E.search_saved_offset = 0;
	E.selection_mode_active = 0;
	E.selection_anchor_offset = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_anchor_offset = 0;
	E.mouse_drag_started = 0;
	E.undo_history.start = 0;
	E.undo_history.len = 0;
	E.redo_history.start = 0;
	E.redo_history.len = 0;
	memset(&E.edit_pending_entry, 0, sizeof(E.edit_pending_entry));
	E.edit_pending_entry_valid = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

static void editorFreeTabRows(struct editorTabState *tab) {
	for (int i = 0; i < tab->numrows; i++) {
		free(tab->rows[i].chars);
		free(tab->rows[i].render);
	}
	free(tab->rows);
	tab->rows = NULL;
	tab->numrows = 0;
}

static void editorTabStateFree(struct editorTabState *tab) {
	if (tab->lsp_diagnostics != NULL) {
		for (int i = 0; i < tab->lsp_diagnostic_count; i++) {
			free(tab->lsp_diagnostics[i].message);
		}
		free(tab->lsp_diagnostics);
		tab->lsp_diagnostics = NULL;
	}
	editorFreeTabRows(tab);
	editorDocumentFreePtr(&tab->document);
	free(tab->filename);
	tab->filename = NULL;
	free(tab->tab_title);
	tab->tab_title = NULL;
	editorSyntaxStateDestroy(tab->syntax_state);
	tab->syntax_state = NULL;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	free(tab->search_query);
	tab->search_query = NULL;
	editorHistoryClear(&tab->undo_history);
	editorHistoryClear(&tab->redo_history);
	editorHistoryEntryFree(&tab->edit_pending_entry);
	tab->edit_pending_entry_valid = 0;
	editorTabStateInitEmpty(tab);
}

void editorFreeActiveBufferState(void) {
	if (E.lsp_diagnostics != NULL) {
		for (int i = 0; i < E.lsp_diagnostic_count; i++) {
			free(E.lsp_diagnostics[i].message);
		}
		free(E.lsp_diagnostics);
		E.lsp_diagnostics = NULL;
	}
	for (int i = 0; i < E.numrows; i++) {
		free(E.rows[i].chars);
		free(E.rows[i].render);
	}
	free(E.rows);
	E.rows = NULL;
	E.numrows = 0;
	editorDocumentFreePtr(&E.document);
	E.max_render_cols = 0;
	E.max_render_cols_valid = 1;

	free(E.filename);
	E.filename = NULL;
	free(E.tab_title);
	E.tab_title = NULL;
	editorSyntaxStateDestroy(E.syntax_state);
	E.syntax_state = NULL;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	free(E.search_query);
	E.search_query = NULL;
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	editorSyntaxVisibleCacheInvalidate();
	editorResetActiveBufferFields();
}

static void editorTabStateCaptureActive(struct editorTabState *tab) {
	editorTabStateFree(tab);

	tab->tab_kind = E.tab_kind;
	tab->is_preview = E.is_preview;
	tab->tab_title = E.tab_title;
	tab->cursor_offset = E.cursor_offset;
	if (E.document != NULL) {
		size_t cursor_offset = 0;
		if (editorBufferPosToOffset(E.cy, E.cx, &cursor_offset)) {
			tab->cursor_offset = cursor_offset;
		}
	}
	tab->cx = E.cx;
	tab->cy = E.cy;
	tab->rx = E.rx;
	tab->rowoff = E.rowoff;
	tab->coloff = E.coloff;
	tab->numrows = E.numrows;
	tab->rows = E.rows;
	tab->document = E.document;
	tab->max_render_cols = E.max_render_cols;
	tab->max_render_cols_valid = E.max_render_cols_valid;
	tab->dirty = E.dirty;
	tab->filename = E.filename;
	tab->syntax_language = E.syntax_language;
	tab->syntax_state = E.syntax_state;
	tab->lsp_doc_open = E.lsp_doc_open;
	tab->lsp_doc_version = E.lsp_doc_version;
	tab->lsp_diagnostics = E.lsp_diagnostics;
	tab->lsp_diagnostic_count = E.lsp_diagnostic_count;
	tab->lsp_diagnostic_error_count = E.lsp_diagnostic_error_count;
	tab->lsp_diagnostic_warning_count = E.lsp_diagnostic_warning_count;
	tab->search_query = E.search_query;
	tab->search_match_offset = E.search_match_offset;
	tab->search_match_len = E.search_match_len;
	tab->search_direction = E.search_direction;
	tab->search_saved_offset = E.search_saved_offset;
	tab->selection_mode_active = E.selection_mode_active;
	tab->selection_anchor_offset = E.selection_anchor_offset;
	tab->mouse_left_button_down = E.mouse_left_button_down;
	tab->mouse_drag_anchor_offset = E.mouse_drag_anchor_offset;
	tab->mouse_drag_started = E.mouse_drag_started;
	tab->undo_history = E.undo_history;
	tab->redo_history = E.redo_history;
	tab->edit_pending_entry = E.edit_pending_entry;
	tab->edit_pending_entry_valid = E.edit_pending_entry_valid;
	tab->edit_group_kind = E.edit_group_kind;
	tab->edit_pending_kind = E.edit_pending_kind;
	tab->edit_pending_mode = E.edit_pending_mode;

	editorResetActiveBufferFields();
}

static void editorTabStateLoadActive(struct editorTabState *tab) {
	E.tab_kind = tab->tab_kind;
	E.is_preview = tab->is_preview;
	E.tab_title = tab->tab_title;
	E.cursor_offset = tab->cursor_offset;
	E.cx = tab->cx;
	E.cy = tab->cy;
	E.rx = tab->rx;
	E.rowoff = tab->rowoff;
	E.coloff = tab->coloff;
	E.numrows = tab->numrows;
	E.rows = tab->rows;
	E.document = tab->document;
	E.max_render_cols = tab->max_render_cols;
	E.max_render_cols_valid = tab->max_render_cols_valid;
	E.dirty = tab->dirty;
	E.filename = tab->filename;
	E.syntax_language = tab->syntax_language;
	E.syntax_state = tab->syntax_state;
	E.lsp_doc_open = tab->lsp_doc_open;
	E.lsp_doc_version = tab->lsp_doc_version;
	E.lsp_diagnostics = tab->lsp_diagnostics;
	E.lsp_diagnostic_count = tab->lsp_diagnostic_count;
	E.lsp_diagnostic_error_count = tab->lsp_diagnostic_error_count;
	E.lsp_diagnostic_warning_count = tab->lsp_diagnostic_warning_count;
	E.search_query = tab->search_query;
	E.search_match_offset = tab->search_match_offset;
	E.search_match_len = tab->search_match_len;
	E.search_direction = tab->search_direction;
	E.search_saved_offset = tab->search_saved_offset;
	E.selection_mode_active = tab->selection_mode_active;
	E.selection_anchor_offset = tab->selection_anchor_offset;
	E.mouse_left_button_down = tab->mouse_left_button_down;
	E.mouse_drag_anchor_offset = tab->mouse_drag_anchor_offset;
	E.mouse_drag_started = tab->mouse_drag_started;
	E.undo_history = tab->undo_history;
	E.redo_history = tab->redo_history;
	E.edit_pending_entry = tab->edit_pending_entry;
	E.edit_pending_entry_valid = tab->edit_pending_entry_valid;
	E.edit_group_kind = tab->edit_group_kind;
	E.edit_pending_kind = tab->edit_pending_kind;
	E.edit_pending_mode = tab->edit_pending_mode;
	editorSyntaxVisibleCacheInvalidate();
	if (E.document != NULL) {
		if (!editorSyncCursorFromOffset(E.cursor_offset)) {
			E.cursor_offset = 0;
			E.cy = 0;
			E.cx = 0;
		}
	}

	editorTabStateInitEmpty(tab);
}

static int editorEnsureTabCapacity(int needed) {
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
		editorTabStateInitEmpty(&new_tabs[i]);
	}

	E.tabs = new_tabs;
	E.tab_capacity = new_capacity;
	return 1;
}

static void editorStoreActiveTab(void) {
	if (E.tabs == NULL || E.tab_count <= 0 ||
			E.active_tab < 0 || E.active_tab >= E.tab_count) {
		return;
	}
	if (E.is_preview && E.dirty != 0) {
		E.is_preview = 0;
	}
	editorTabStateCaptureActive(&E.tabs[E.active_tab]);
}

static void editorLoadActiveTab(int tab_idx) {
	if (E.tabs == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		editorResetActiveBufferFields();
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	editorTabStateLoadActive(&E.tabs[tab_idx]);
	if (E.tab_kind == EDITOR_TAB_TASK_LOG) {
		E.syntax_language = EDITOR_SYNTAX_NONE;
		editorSyntaxStateDestroy(E.syntax_state);
		E.syntax_state = NULL;
		E.lsp_doc_open = 0;
		E.lsp_doc_version = 0;
		E.lsp_diagnostics = NULL;
		E.lsp_diagnostic_count = 0;
		E.lsp_diagnostic_error_count = 0;
		E.lsp_diagnostic_warning_count = 0;
		editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
		return;
	}
	const char *first_line = NULL;
	if (E.numrows > 0 && E.rows != NULL) {
		first_line = E.rows[0].chars;
	}
	enum editorSyntaxLanguage detected =
			editorSyntaxDetectLanguageFromFilenameAndFirstLine(E.filename, first_line);
	if (E.syntax_language != detected || (detected != EDITOR_SYNTAX_NONE && E.syntax_state == NULL)) {
		(void)editorSyntaxParseFullActive();
	}
	editorViewportSetMode(EDITOR_VIEWPORT_FOLLOW_CURSOR);
}

int editorTabsInit(void) {
	editorTabsFreeAll();
	if (!editorEnsureTabCapacity(1)) {
		editorSetAllocFailureStatus();
		return 0;
	}

	E.tab_count = 1;
	E.active_tab = 0;
	E.tab_view_start = 0;
	editorTabStateInitEmpty(&E.tabs[0]);
	editorLoadActiveTab(0);
	return 1;
}

void editorTabsFreeAll(void) {
	if (E.task_running && E.task_pid > 0) {
		int status = 0;
		(void)kill(E.task_pid, SIGTERM);
		(void)waitpid(E.task_pid, &status, 0);
	}
	editorTaskResetState();

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			editorLspNotifyDidCloseTabState(&E.tabs[i]);
		}
	}
	editorLspShutdown();

	editorFreeActiveBufferState();

	if (E.tabs != NULL) {
		for (int i = 0; i < E.tab_count; i++) {
			editorTabStateFree(&E.tabs[i]);
		}
	}
	free(E.tabs);
	E.tabs = NULL;
	E.tab_count = 0;
	E.tab_capacity = 0;
	E.active_tab = 0;
	E.tab_view_start = 0;
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

	editorStoreActiveTab();
	int new_idx = E.tab_count;
	if (!editorEnsureTabCapacity(E.tab_count + 1)) {
		editorLoadActiveTab(E.active_tab);
		editorSetAllocFailureStatus();
		return 0;
	}

	editorTabStateInitEmpty(&E.tabs[new_idx]);
	E.tab_count++;
	E.active_tab = new_idx;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

static int editorTabCanReuseActiveEmptyBuffer(void) {
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
		if (E.rows[row_idx].size != 0) {
			return 0;
		}
	}
	return 1;
}

static int editorTabFindReusablePreviewIndex(void) {
	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		if (tab_idx == E.active_tab) {
			if (E.tab_kind == EDITOR_TAB_FILE && E.is_preview && E.dirty == 0) {
				return tab_idx;
			}
			continue;
		}
		if (E.tabs[tab_idx].tab_kind == EDITOR_TAB_FILE &&
				E.tabs[tab_idx].is_preview &&
				E.tabs[tab_idx].dirty == 0) {
			return tab_idx;
		}
	}
	return -1;
}

void editorTabPinActivePreview(void) {
	if (E.tab_kind == EDITOR_TAB_FILE) {
		E.is_preview = 0;
	}
}

int editorActiveTabIsPreview(void) {
	return E.tab_kind == EDITOR_TAB_FILE && E.is_preview;
}

int editorTabIsPreviewAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return editorActiveTabIsPreview();
	}
	return E.tabs[idx].tab_kind == EDITOR_TAB_FILE && E.tabs[idx].is_preview;
}

int editorTabOpenFileAsNew(const char *filename) {
	if (editorTabCanReuseActiveEmptyBuffer()) {
		editorOpen(filename);
		E.is_preview = 0;
		return 1;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	editorOpen(filename);
	E.is_preview = 0;
	return 1;
}

int editorTabOpenOrSwitchToFile(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}

	int existing_tab = editorTabFindOpenFileIndex(filename);
	if (existing_tab >= 0) {
		if (!editorTabSwitchToIndex(existing_tab)) {
			return 0;
		}
		E.is_preview = 0;
		return 1;
	}

	return editorTabOpenFileAsNew(filename);
}

int editorTabOpenOrSwitchToPreviewFile(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}

	int existing_tab = editorTabFindOpenFileIndex(filename);
	if (existing_tab >= 0) {
		return editorTabSwitchToIndex(existing_tab);
	}

	int preview_tab = editorTabFindReusablePreviewIndex();
	if (preview_tab >= 0) {
		if (!editorTabSwitchToIndex(preview_tab)) {
			return 0;
		}
		editorOpen(filename);
		E.is_preview = 1;
		return 1;
	}

	if (editorTabCanReuseActiveEmptyBuffer()) {
		editorOpen(filename);
		E.is_preview = 1;
		return 1;
	}
	if (!editorTabNewEmpty()) {
		return 0;
	}
	editorOpen(filename);
	E.is_preview = 1;
	return 1;
}

static const char *editorTabPathAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return NULL;
	}
	if (idx == E.active_tab) {
		return E.filename;
	}
	return E.tabs[idx].filename;
}

static int editorTabFindOpenFileIndex(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}

	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		const char *tab_path = editorTabPathAt(tab_idx);
		if (tab_path == NULL || tab_path[0] == '\0') {
			continue;
		}
		if (editorPathsReferToSameFile(path, tab_path)) {
			return tab_idx;
		}
	}

	return -1;
}

int editorTabSwitchToIndex(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return 0;
	}
	if (idx == E.active_tab) {
		return 1;
	}

	editorStoreActiveTab();
	E.active_tab = idx;
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTabSwitchByDelta(int delta) {
	if (E.tab_count <= 0) {
		return 0;
	}
	if (delta == 0 || E.tab_count == 1) {
		return 1;
	}

	int target = (E.active_tab + delta) % E.tab_count;
	if (target < 0) {
		target += E.tab_count;
	}
	return editorTabSwitchToIndex(target);
}

int editorTabCloseActive(void) {
	if (E.tab_count <= 0 || E.tabs == NULL) {
		return 0;
	}

	editorStoreActiveTab();
	int closing = E.active_tab;
	editorLspNotifyDidCloseTabState(&E.tabs[closing]);
	editorTabStateFree(&E.tabs[closing]);

	if (E.tab_count == 1) {
		editorTabStateInitEmpty(&E.tabs[0]);
		E.active_tab = 0;
		E.tab_count = 1;
		E.tab_view_start = 0;
		editorLoadActiveTab(0);
		return 1;
	}

	memmove(&E.tabs[closing], &E.tabs[closing + 1],
			sizeof(struct editorTabState) * (size_t)(E.tab_count - closing - 1));
	E.tab_count--;
	if (closing >= E.tab_count) {
		closing = E.tab_count - 1;
	}
	E.active_tab = closing;
	editorLoadActiveTab(E.active_tab);
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

const char *editorTabDisplayNameAt(int idx) {
	if (idx < 0 || idx >= E.tab_count) {
		return "[No Name]";
	}
	if (idx == E.active_tab) {
		if (E.tab_kind == EDITOR_TAB_TASK_LOG && E.tab_title != NULL && E.tab_title[0] != '\0') {
			return E.tab_title;
		}
		return E.filename != NULL ? E.filename : "[No Name]";
	}
	if (E.tabs[idx].tab_kind == EDITOR_TAB_TASK_LOG &&
			E.tabs[idx].tab_title != NULL && E.tabs[idx].tab_title[0] != '\0') {
		return E.tabs[idx].tab_title;
	}
	return E.tabs[idx].filename != NULL ? E.tabs[idx].filename : "[No Name]";
}

const char *editorActiveBufferDisplayName(void) {
	if (E.tab_kind == EDITOR_TAB_TASK_LOG && E.tab_title != NULL && E.tab_title[0] != '\0') {
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

int editorActiveTabIsReadOnly(void) {
	return E.tab_kind == EDITOR_TAB_TASK_LOG;
}

int editorActiveTaskTabIsRunning(void) {
	return E.task_running && E.task_tab_idx == E.active_tab && E.tab_kind == EDITOR_TAB_TASK_LOG;
}

static void editorTaskLogClampCursor(struct editorTabState *tab) {
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
	if (tab->cx > tab->rows[tab->cy].size) {
		tab->cx = tab->rows[tab->cy].size;
	}
	tab->cx = editorRowClampCxToClusterBoundary(&tab->rows[tab->cy], tab->cx);
}

static int editorRebuildGeneratedTabRows(struct editorTabState *tab) {
	if (tab == NULL) {
		return 0;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	if (!editorBuildFullRowsFromDocument(tab->document, &new_rows, &new_numrows)) {
		return 0;
	}

	editorFreeTabRows(tab);
	tab->max_render_cols = 0;
	tab->max_render_cols_valid = 0;
	tab->rows = new_rows;
	tab->numrows = new_numrows;
	tab->dirty = 0;
	free(tab->filename);
	tab->filename = NULL;
	editorSyntaxStateDestroy(tab->syntax_state);
	tab->syntax_state = NULL;
	tab->syntax_language = EDITOR_SYNTAX_NONE;
	tab->lsp_doc_open = 0;
	tab->lsp_doc_version = 0;
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
	editorTaskLogClampCursor(tab);
	return 1;
}

static int editorTaskMutateTab(int tab_idx, int jump_to_end,
		int (*mutator)(struct editorTabState *tab, void *ctx), void *ctx) {
	if (mutator == NULL || tab_idx < 0 || tab_idx >= E.tab_count) {
		return 0;
	}

	if (tab_idx == E.active_tab) {
		editorStoreActiveTab();
		if (!mutator(&E.tabs[tab_idx], ctx)) {
			editorLoadActiveTab(tab_idx);
			return 0;
		}
		editorLoadActiveTab(tab_idx);
		if (jump_to_end) {
			if (E.numrows > 0) {
				E.cy = E.numrows - 1;
				E.cx = E.rows[E.cy].size;
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

struct editorTaskAppendContext {
	const char *text;
	size_t len;
};

static int editorTaskAppendOutputMutator(struct editorTabState *tab, void *ctx) {
	static const char truncation_note[] = "\n[output truncated]\n";
	struct editorTaskAppendContext *append = ctx;
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
		if (append_len > 0 &&
				!editorDocumentReplaceRange(tab->document, old_len, 0,
						append->text, append_len)) {
			return 0;
		}
		E.task_output_bytes += append_len;
	}

	if (append_len < append->len) {
		if (!editorDocumentReplaceRange(tab->document, editorDocumentLength(tab->document), 0,
					truncation_note, sizeof(truncation_note) - 1)) {
			return 0;
		}
		E.task_output_truncated = 1;
	}

	if (!editorRebuildGeneratedTabRows(tab)) {
		return 0;
	}
	return 1;
}

static int editorTaskAppendOutput(int tab_idx, const char *text, size_t len, int jump_to_end) {
	struct editorTaskAppendContext ctx = {
		.text = text,
		.len = len
	};
	return editorTaskMutateTab(tab_idx, jump_to_end, editorTaskAppendOutputMutator, &ctx);
}

static void editorTaskResetState(void) {
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

static int editorTaskDrainOutput(int tab_idx, int jump_to_end, int *saw_eof_out) {
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
			if (!editorTaskAppendOutput(tab_idx, buf, (size_t)nread, jump_to_end)) {
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

static int editorTaskAppendFinalLineMutator(struct editorTabState *tab, void *ctx) {
	const char *line = ctx;
	if (tab == NULL || line == NULL) {
		return 0;
	}
	if (!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL) {
		return 0;
	}
	if (!editorDocumentReplaceRange(tab->document, editorDocumentLength(tab->document), 0,
				line, strlen(line))) {
		return 0;
	}
	return editorRebuildGeneratedTabRows(tab);
}

static void editorTaskFinalize(int success, int exit_code) {
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
		(void)editorTaskMutateTab(tab_idx, 1, editorTaskAppendFinalLineMutator, final_line);
	}

	E.task_exit_code = exit_code;
	if (success) {
		editorTaskSetFinalStatus(1);
	} else {
		editorTaskSetFinalStatus(0);
	}
	editorTaskResetState();
}

static void editorTaskSetFinalStatus(int success) {
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

static int editorTaskPrepareLogTab(const char *title, const char *text) {
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
		E.cx = E.rows[E.cy].size;
	}
	editorViewportEnsureCursorVisible();
	editorStoreActiveTab();
	editorLoadActiveTab(E.active_tab);
	return 1;
}

int editorTaskPoll(void) {
	int changed = 0;
	int saw_eof = 0;
	int status = 0;
	pid_t waited = 0;

	if (!E.task_running || E.task_tab_idx < 0) {
		return 0;
	}

	changed |= editorTaskDrainOutput(E.task_tab_idx, 1, &saw_eof);

	do {
		waited = waitpid(E.task_pid, &status, WNOHANG);
	} while (waited == -1 && errno == EINTR);

	if (waited == E.task_pid) {
		int exit_code = 1;
		changed |= editorTaskDrainOutput(E.task_tab_idx, 1, &saw_eof);
		if (WIFEXITED(status)) {
			exit_code = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			exit_code = 128 + WTERMSIG(status);
		}
		editorTaskFinalize(exit_code == 0, exit_code);
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

	(void)editorTaskDrainOutput(E.task_tab_idx, 1, NULL);
	if (WIFEXITED(status)) {
		exit_code = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exit_code = 128 + WTERMSIG(status);
	}
	editorTaskFinalize(0, exit_code);
	return 1;
}

int editorTaskStart(const char *title, const char *command,
		const char *success_status, const char *failure_status) {
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
	if (!editorTaskPrepareLogTab(title, header)) {
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
		(void)snprintf(E.task_success_status, sizeof(E.task_success_status), "%s", success_status);
	} else {
		E.task_success_status[0] = '\0';
	}
	if (failure_status != NULL) {
		(void)snprintf(E.task_failure_status, sizeof(E.task_failure_status), "%s", failure_status);
	} else {
		E.task_failure_status[0] = '\0';
	}
	editorSetStatusMsg("Running task: %s", title);
	return 1;
}

int editorTaskShowMessage(const char *title, const char *text, const char *status) {
	if (!editorTaskPrepareLogTab(title, text)) {
		return 0;
	}
	if (status != NULL && status[0] != '\0') {
		editorSetStatusMsg("%s", status);
	}
	return 1;
}

static const char *editorTabLabelFromDisplayName(const char *display_name) {
	if (display_name == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(display_name, '/');
	if (slash != NULL && slash[1] != '\0') {
		return slash + 1;
	}
	return display_name;
}

static int editorSanitizedTokenDisplayCols(const char *text, int text_len, int *src_len_out) {
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

static int editorSanitizedTextDisplayCols(const char *text, int max_cols) {
	if (text == NULL) {
		return 0;
	}

	int text_len = (int)strlen(text);
	int total_cols = 0;
	for (int idx = 0; idx < text_len;) {
		int src_len = 0;
		int token_cols = editorSanitizedTokenDisplayCols(&text[idx], text_len - idx, &src_len);
		if (max_cols >= 0 && total_cols + token_cols > max_cols) {
			break;
		}
		total_cols += token_cols;
		idx += src_len;
	}

	return total_cols;
}

static int editorTabLabelColsAt(int tab_idx) {
	const char *label = editorTabLabelFromDisplayName(editorTabDisplayNameAt(tab_idx));
	int cols = editorSanitizedTextDisplayCols(label, ROTIDE_TAB_TITLE_MAX_COLS);
	if (cols < 1) {
		cols = 1;
	}
	return cols;
}

static int editorTabWidthColsAt(int tab_idx) {
	return 6 + editorTabLabelColsAt(tab_idx);
}

static void editorTabVisibleRangeFromStart(int start_idx, int cols, int *last_idx_out) {
	int last_idx = start_idx - 1;
	if (E.tab_count <= 0 || cols <= 0 || start_idx < 0 || start_idx >= E.tab_count) {
		*last_idx_out = last_idx;
		return;
	}

	int used_cols = 0;
	for (int tab_idx = start_idx; tab_idx < E.tab_count; tab_idx++) {
		int width_cols = editorTabWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (tab_idx == start_idx && width_cols > cols) {
			width_cols = cols;
		}
		if (tab_idx > start_idx && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		used_cols += width_cols;
		last_idx = tab_idx;
		if (used_cols >= cols) {
			break;
		}
	}

	if (last_idx < start_idx && cols > 0) {
		last_idx = start_idx;
	}
	*last_idx_out = last_idx;
}

static void editorTabsAlignViewToActiveForWidth(int cols) {
	if (E.tab_count <= 0) {
		E.tab_view_start = 0;
		return;
	}
	if (E.active_tab < 0) {
		E.active_tab = 0;
	}
	if (E.active_tab >= E.tab_count) {
		E.active_tab = E.tab_count - 1;
	}

	if (E.tab_view_start < 0) {
		E.tab_view_start = 0;
	}
	if (E.tab_view_start >= E.tab_count) {
		E.tab_view_start = E.tab_count - 1;
	}

	if (cols <= 0) {
		if (E.active_tab < E.tab_view_start) {
			E.tab_view_start = E.active_tab;
		}
		return;
	}

	if (E.active_tab < E.tab_view_start) {
		E.tab_view_start = E.active_tab;
	}

	int last_visible = E.tab_view_start;
	editorTabVisibleRangeFromStart(E.tab_view_start, cols, &last_visible);
	while (E.active_tab > last_visible && E.tab_view_start < E.active_tab) {
		E.tab_view_start++;
		editorTabVisibleRangeFromStart(E.tab_view_start, cols, &last_visible);
	}
}

int editorTabBuildLayoutForWidth(int cols, struct editorTabLayoutEntry *entries, int max_entries,
		int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (E.tab_count <= 0 || cols <= 0 || max_entries == 0) {
		if (E.tab_count <= 0) {
			E.tab_view_start = 0;
		}
		return 1;
	}
	if (entries == NULL || max_entries < 0) {
		return 0;
	}

	editorTabsAlignViewToActiveForWidth(cols);
	int start_idx = E.tab_view_start;
	if (start_idx < 0) {
		start_idx = 0;
	}
	if (start_idx >= E.tab_count) {
		start_idx = E.tab_count - 1;
	}

	int used_cols = 0;
	int count = 0;
	for (int tab_idx = start_idx; tab_idx < E.tab_count && used_cols < cols; tab_idx++) {
		if (count >= max_entries) {
			break;
		}

		int width_cols = editorTabWidthColsAt(tab_idx);
		if (width_cols < 1) {
			width_cols = 1;
		}
		if (count == 0 && width_cols > cols) {
			width_cols = cols;
		}
		if (count > 0 && used_cols + width_cols > cols) {
			break;
		}
		if (width_cols <= 0) {
			break;
		}

		struct editorTabLayoutEntry *entry = &entries[count];
		entry->tab_idx = tab_idx;
		entry->start_col = used_cols;
		entry->width_cols = width_cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;

		used_cols += width_cols;
		count++;
	}

	if (count == 0) {
		struct editorTabLayoutEntry *entry = &entries[0];
		entry->tab_idx = start_idx;
		entry->start_col = 0;
		entry->width_cols = cols;
		entry->show_left_overflow = 0;
		entry->show_right_overflow = 0;
		count = 1;
	}

	if (count > 0) {
		entries[0].show_left_overflow = entries[0].tab_idx > 0;
		entries[count - 1].show_right_overflow =
				entries[count - 1].tab_idx < E.tab_count - 1;
	}

	if (count_out != NULL) {
		*count_out = count;
	}
	return 1;
}

int editorTabHitTestColumn(int col, int cols) {
	if (col < 0 || col >= cols || E.tab_count <= 0 || cols <= 0) {
		return -1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForWidth(cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
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
