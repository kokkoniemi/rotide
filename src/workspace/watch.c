#include "workspace/watch.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/git.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

enum {
	EDITOR_WATCH_FILE_POLL_MS = 250,
	EDITOR_WATCH_GIT_POLL_MS = 1000
};

static long long g_file_watch_last_poll_ms = 0;
static long long g_git_watch_last_poll_ms = 0;

static long long editorWatchMonotonicMillis(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static int editorWatchTimeEqual(struct timespec left, struct timespec right) {
	return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}

static int editorWatchDiskStateEqual(const struct editorFileDiskState *left,
		const struct editorFileDiskState *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}
	if (left->known != right->known || left->exists != right->exists) {
		return 0;
	}
	if (!left->known || !left->exists) {
		return 1;
	}
	return left->dev == right->dev && left->ino == right->ino &&
			left->size == right->size &&
			editorWatchTimeEqual(left->mtime, right->mtime) &&
			editorWatchTimeEqual(left->ctime, right->ctime);
}

static int editorWatchReadDiskState(const char *filename,
		struct editorFileDiskState *state_out) {
	struct stat st;

	if (state_out == NULL) {
		return 0;
	}
	memset(state_out, 0, sizeof(*state_out));
	state_out->known = 1;

	if (filename == NULL || filename[0] == '\0') {
		return 1;
	}
	if (stat(filename, &st) != 0) {
		if (errno == ENOENT || errno == ENOTDIR) {
			return 1;
		}
		return 0;
	}

	state_out->exists = 1;
	state_out->dev = st.st_dev;
	state_out->ino = st.st_ino;
	state_out->size = st.st_size;
	state_out->mtime = st.st_mtim;
	state_out->ctime = st.st_ctim;
	return 1;
}

static void editorWatchClearActiveHistory(void) {
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

static void editorWatchClearTabHistory(struct editorTabState *tab) {
	if (tab == NULL) {
		return;
	}
	editorHistoryClear(&tab->undo_history);
	editorHistoryClear(&tab->redo_history);
	editorHistoryEntryFree(&tab->edit_pending_entry);
	tab->edit_pending_entry_valid = 0;
	tab->edit_group_kind = EDITOR_EDIT_NONE;
	tab->edit_pending_kind = EDITOR_EDIT_NONE;
	tab->edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

static void editorWatchFreeTabDiagnostics(struct editorTabState *tab) {
	if (tab == NULL || tab->lsp_diagnostics == NULL) {
		return;
	}
	for (int i = 0; i < tab->lsp_diagnostic_count; i++) {
		free(tab->lsp_diagnostics[i].message);
	}
	free(tab->lsp_diagnostics);
	tab->lsp_diagnostics = NULL;
	tab->lsp_diagnostic_count = 0;
	tab->lsp_diagnostic_error_count = 0;
	tab->lsp_diagnostic_warning_count = 0;
}

static int editorWatchReadDocument(const char *filename, struct editorDocument *document,
		char **text_out, size_t *text_len_out) {
	char *text = NULL;
	size_t text_len = 0;

	if (document == NULL || text_out == NULL || text_len_out == NULL) {
		return 0;
	}
	*text_out = NULL;
	*text_len_out = 0;

	if (!editorReadFileToText(filename, &text, &text_len)) {
		return 0;
	}
	if (!editorDocumentResetFromString(document, text, text_len)) {
		free(text);
		editorSetAllocFailureStatus();
		return 0;
	}

	*text_out = text;
	*text_len_out = text_len;
	return 1;
}

static void editorWatchNotifyActiveReload(const char *text, size_t text_len) {
	(void)editorLspNotifyDidChange(E.filename, E.syntax_language, &E.lsp_doc_open,
			&E.lsp_doc_version, NULL, NULL, 0, text, text_len);
	(void)editorLspNotifyEslintDidChange(E.filename, E.syntax_language,
			&E.lsp_eslint_doc_open, &E.lsp_eslint_doc_version, NULL, NULL, 0, text,
			text_len);
}

static int editorWatchReloadActiveFile(const struct editorFileDiskState *observed) {
	struct editorDocument document;
	char *text = NULL;
	size_t text_len = 0;
	size_t saved_offset = E.cursor_offset;
	size_t new_len = 0;
	int ok = 0;

	editorDocumentInit(&document);
	if (!editorWatchReadDocument(E.filename, &document, &text, &text_len)) {
		goto cleanup;
	}

	if (!editorBufferPosToOffset(E.cy, E.cx, &saved_offset)) {
		saved_offset = E.cursor_offset;
	}
	if (!editorRestoreActiveFromDocument(&document, 0, 0, 0, 1)) {
		goto cleanup;
	}
	new_len = editorDocumentLength(E.document);
	if (saved_offset > new_len) {
		saved_offset = new_len;
	}
	(void)editorSyncCursorFromOffsetByteBoundary(saved_offset);
	editorWatchClearActiveHistory();
	E.disk_state = *observed;
	E.disk_conflict = 0;
	editorWatchNotifyActiveReload(text, text_len);
	editorViewportEnsureCursorVisible();
	editorSetStatusMsg("Reloaded %s from disk", E.filename);
	ok = 1;

cleanup:
	free(text);
	editorDocumentFree(&document);
	return ok;
}

static int editorWatchReloadTabFile(struct editorTabState *tab,
		const struct editorFileDiskState *observed) {
	struct editorDocument document;
	struct editorDocument *new_document = NULL;
	struct erow *new_rows = NULL;
	int new_numrows = 0;
	char *text = NULL;
	size_t text_len = 0;
	size_t cursor_offset = 0;
	size_t cursor_column = 0;
	enum editorSyntaxLanguage old_language = EDITOR_SYNTAX_NONE;
	enum editorSyntaxLanguage new_language = EDITOR_SYNTAX_NONE;
	int ok = 0;

	if (tab == NULL || observed == NULL) {
		return 0;
	}

	editorDocumentInit(&document);
	if (!editorWatchReadDocument(tab->filename, &document, &text, &text_len)) {
		goto cleanup;
	}
	new_document = editorMalloc(sizeof(*new_document));
	if (new_document != NULL) {
		editorDocumentInit(new_document);
	}
	if (new_document == NULL ||
			!editorDocumentResetFromDocument(new_document, &document) ||
			!editorBuildFullRowsFromDocument(new_document, &new_rows, &new_numrows)) {
		editorFreeRowArray(new_rows, new_numrows);
		editorDocumentFreePtr(&new_document);
		editorSetAllocFailureStatus();
		goto cleanup;
	}

	cursor_offset = tab->cursor_offset;
	if (cursor_offset > editorDocumentLength(new_document)) {
		cursor_offset = editorDocumentLength(new_document);
	}
	if (!editorDocumentByteOffsetToPosition(new_document, cursor_offset, &tab->cy,
				&cursor_column)) {
		tab->cy = 0;
		tab->cx = 0;
		cursor_offset = 0;
	} else if (cursor_column > (size_t)INT_MAX) {
		tab->cx = INT_MAX;
	} else {
		tab->cx = (int)cursor_column;
	}
	if (tab->cy < new_numrows) {
		tab->cx = editorRowClampCxToClusterBoundary(&new_rows[tab->cy], tab->cx);
	}
	tab->cursor_offset = cursor_offset;

	editorFreeRowArray(tab->rows, tab->numrows);
	editorDocumentFreePtr(&tab->document);
	tab->rows = new_rows;
	tab->numrows = new_numrows;
	tab->document = new_document;
	new_rows = NULL;
	new_numrows = 0;
	new_document = NULL;
	tab->max_render_cols = 0;
	tab->max_render_cols_valid = 0;
	tab->dirty = 0;
	tab->disk_state = *observed;
	tab->disk_conflict = 0;
	old_language = tab->syntax_language;
	new_language = editorSyntaxDetectLanguageFromFilenameAndFirstLine(tab->filename,
			tab->numrows > 0 ? tab->rows[0].chars : NULL);
	if (old_language != new_language) {
		editorLspNotifyDidClose(tab->filename, old_language, &tab->lsp_doc_open,
				&tab->lsp_doc_version);
		editorLspNotifyEslintDidClose(tab->filename, old_language,
				&tab->lsp_eslint_doc_open, &tab->lsp_eslint_doc_version);
	}
	tab->syntax_language = new_language;
	editorSyntaxStateDestroy(tab->syntax_state);
	tab->syntax_state = NULL;
	tab->syntax_parse_failures = 0;
	tab->syntax_revision = 0;
	tab->syntax_generation = 0;
	tab->syntax_background_pending = 0;
	tab->syntax_pending_revision = 0;
	tab->syntax_pending_first_row = 0;
	tab->syntax_pending_row_count = 0;
	if (tab->lsp_doc_open) {
		(void)editorLspNotifyDidChange(tab->filename, tab->syntax_language,
				&tab->lsp_doc_open, &tab->lsp_doc_version, NULL, NULL, 0, text,
				text_len);
	}
	if (tab->lsp_eslint_doc_open) {
		(void)editorLspNotifyEslintDidChange(tab->filename, tab->syntax_language,
				&tab->lsp_eslint_doc_open, &tab->lsp_eslint_doc_version, NULL, NULL,
				0, text, text_len);
	}
	editorWatchFreeTabDiagnostics(tab);
	editorWatchClearTabHistory(tab);
	editorSetStatusMsg("Reloaded %s from disk", tab->filename);
	ok = 1;

cleanup:
	editorFreeRowArray(new_rows, new_numrows);
	editorDocumentFreePtr(&new_document);
	free(text);
	editorDocumentFree(&document);
	return ok;
}

void editorWatchRefreshActiveBaseline(void) {
	struct editorFileDiskState observed;
	if (E.tab_kind != EDITOR_TAB_FILE || E.filename == NULL || E.filename[0] == '\0') {
		memset(&E.disk_state, 0, sizeof(E.disk_state));
		E.disk_conflict = 0;
		return;
	}
	if (editorWatchReadDiskState(E.filename, &observed)) {
		E.disk_state = observed;
		E.disk_conflict = 0;
	}
}

int editorWatchActiveHasDiskConflict(void) {
	return E.tab_kind == EDITOR_TAB_FILE && E.disk_conflict;
}

static int editorWatchHandleChangedActive(const struct editorFileDiskState *observed) {
	if (observed == NULL) {
		return 0;
	}
	if (E.dirty != 0) {
		E.disk_state = *observed;
		if (!E.disk_conflict) {
			E.disk_conflict = 1;
			editorSetStatusMsg("File changed on disk; save will ask before overwriting");
			return 1;
		}
		return 0;
	}
	if (!observed->exists) {
		E.disk_state = *observed;
		E.disk_conflict = 0;
		editorSetStatusMsg("File deleted on disk: %s", E.filename);
		return 1;
	}
	return editorWatchReloadActiveFile(observed);
}

static int editorWatchHandleChangedTab(struct editorTabState *tab,
		const struct editorFileDiskState *observed) {
	if (tab == NULL || observed == NULL) {
		return 0;
	}
	if (tab->dirty != 0) {
		tab->disk_state = *observed;
		if (!tab->disk_conflict) {
			tab->disk_conflict = 1;
			editorSetStatusMsg("File changed on disk; save will ask before overwriting");
			return 1;
		}
		return 0;
	}
	if (!observed->exists) {
		tab->disk_state = *observed;
		tab->disk_conflict = 0;
		editorSetStatusMsg("File deleted on disk: %s", tab->filename);
		return 1;
	}
	return editorWatchReloadTabFile(tab, observed);
}

static int editorWatchPollActiveTab(void) {
	struct editorFileDiskState observed;

	if (E.tab_kind != EDITOR_TAB_FILE || E.filename == NULL || E.filename[0] == '\0') {
		return 0;
	}
	if (!editorWatchReadDiskState(E.filename, &observed)) {
		return 0;
	}
	if (!E.disk_state.known) {
		E.disk_state = observed;
		return 0;
	}
	if (editorWatchDiskStateEqual(&E.disk_state, &observed)) {
		return 0;
	}
	return editorWatchHandleChangedActive(&observed);
}

static int editorWatchPollInactiveTab(struct editorTabState *tab) {
	struct editorFileDiskState observed;

	if (tab == NULL || tab->tab_kind != EDITOR_TAB_FILE ||
			tab->filename == NULL || tab->filename[0] == '\0') {
		return 0;
	}
	if (!editorWatchReadDiskState(tab->filename, &observed)) {
		return 0;
	}
	if (!tab->disk_state.known) {
		tab->disk_state = observed;
		return 0;
	}
	if (editorWatchDiskStateEqual(&tab->disk_state, &observed)) {
		return 0;
	}
	return editorWatchHandleChangedTab(tab, &observed);
}

int editorWatchPollNow(void) {
	int changed = 0;

	changed |= editorWatchPollActiveTab();
	for (int i = 0; i < E.tab_count; i++) {
		if (i == E.active_tab) {
			continue;
		}
		changed |= editorWatchPollInactiveTab(&E.tabs[i]);
	}
	editorGitRefresh();
	return changed;
}

int editorWatchPoll(void) {
	long long now = editorWatchMonotonicMillis();
	int changed = 0;

	if (now == 0) {
		return editorWatchPollNow();
	}
	if (g_file_watch_last_poll_ms == 0 ||
			now - g_file_watch_last_poll_ms >= EDITOR_WATCH_FILE_POLL_MS) {
		g_file_watch_last_poll_ms = now;
		changed |= editorWatchPollActiveTab();
		for (int i = 0; i < E.tab_count; i++) {
			if (i == E.active_tab) {
				continue;
			}
			changed |= editorWatchPollInactiveTab(&E.tabs[i]);
		}
	}
	if (g_git_watch_last_poll_ms == 0) {
		g_git_watch_last_poll_ms = now;
	} else if (now - g_git_watch_last_poll_ms >= EDITOR_WATCH_GIT_POLL_MS) {
		g_git_watch_last_poll_ms = now;
		editorGitRefresh();
		if (E.git_repo_root != NULL) {
			changed = 1;
		}
	}
	return changed;
}

void editorWatchTestReset(void) {
	g_file_watch_last_poll_ms = 0;
	g_git_watch_last_poll_ms = 0;
}
