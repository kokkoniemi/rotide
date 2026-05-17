#include "editing/edit_pipeline.h"

#include "editing/buffer_core.h"
#include "editing/document_bridge.h"
#include "editing/document_position.h"
#include "editing/history.h"
#include "editing/post_edit_notify.h"
#include "editing/row_cache.h"
#include "language/syntax.h"
#include "language/syntax_worker.h"
#include "support/size_utils.h"
#include "text/document.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static int editorSyntaxOffsetToU32(size_t offset, uint32_t *out) {
	if (out == NULL || offset > UINT32_MAX) {
		return 0;
	}
	*out = (uint32_t)offset;
	return 1;
}

static int editorSyntaxPointFromPosition(int cy, int cx, struct editorSyntaxPoint *out) {
	if (out == NULL || cy < 0 || cx < 0) {
		return 0;
	}
	out->row = (uint32_t)cy;
	out->column = (uint32_t)cx;
	return 1;
}

static int editorActiveDocumentCurrent(const struct editorDocument **document_out) {
	if (document_out == NULL || !editorTabKindSupportsDocument(E.tab_kind) ||
			!editorDocumentEnsureActiveCurrent() || E.document == NULL) {
		return 0;
	}
	*document_out = E.document;
	return 1;
}

static int editorAdvancePositionByText(int start_row, size_t start_col, const char *text,
		size_t len, int *row_out, size_t *col_out) {
	int row = start_row;
	size_t col = start_col;

	if (row_out == NULL || col_out == NULL || start_row < 0 || (len > 0 && text == NULL)) {
		return 0;
	}

	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			if (row == INT_MAX) {
				return 0;
			}
			row++;
			col = 0;
			continue;
		}
		if (!editorSizeAdd(col, 1, &col)) {
			return 0;
		}
	}

	*row_out = row;
	*col_out = col;
	return 1;
}

static int editorBuildSyntaxEditForDocumentEdit(const struct editorDocument *document,
		size_t start_offset, size_t old_len, const char *new_text, size_t new_len,
		struct editorSyntaxEdit *edit_out) {
	size_t old_end_offset = 0;
	size_t new_end_offset = 0;
	int start_row = 0;
	int old_end_row = 0;
	int new_end_row = 0;
	size_t start_col = 0;
	size_t old_end_col = 0;
	size_t new_end_col = 0;
	int start_col_int = 0;
	int old_end_col_int = 0;
	int new_end_col_int = 0;

	if (document == NULL || edit_out == NULL || (new_len > 0 && new_text == NULL) ||
			!editorSizeAdd(start_offset, old_len, &old_end_offset) ||
			!editorSizeAdd(start_offset, new_len, &new_end_offset) ||
			old_end_offset > editorDocumentLength(document)) {
		return 0;
	}

	if (!editorDocumentByteOffsetToPosition(document, start_offset, &start_row, &start_col) ||
			!editorDocumentByteOffsetToPosition(document, old_end_offset, &old_end_row, &old_end_col) ||
			!editorAdvancePositionByText(start_row, start_col,
					new_len > 0 ? new_text : "", new_len, &new_end_row, &new_end_col) ||
			!editorSizeToInt(start_col, &start_col_int) ||
			!editorSizeToInt(old_end_col, &old_end_col_int) ||
			!editorSizeToInt(new_end_col, &new_end_col_int) ||
			!editorSyntaxOffsetToU32(start_offset, &edit_out->start_byte) ||
			!editorSyntaxOffsetToU32(old_end_offset, &edit_out->old_end_byte) ||
			!editorSyntaxOffsetToU32(new_end_offset, &edit_out->new_end_byte) ||
			!editorSyntaxPointFromPosition(start_row, start_col_int, &edit_out->start_point) ||
			!editorSyntaxPointFromPosition(old_end_row, old_end_col_int, &edit_out->old_end_point) ||
			!editorSyntaxPointFromPosition(new_end_row, new_end_col_int, &edit_out->new_end_point)) {
		return 0;
	}

	return 1;
}

int editorApplyDocumentEdit(const struct editorDocumentEdit *edit) {
	const struct editorDocument *active_document = NULL;
	size_t old_end_offset = 0;
	struct editorSyntaxEdit syntax_edit = {0};
	int syntax_track = 0;
	char *removed_text = NULL;
	struct editorRowCacheSpliceRegion row_region = {0};
	struct erow *replacement_rows = NULL;
	int replacement_numrows = 0;
	int replacement_end_row_exclusive = 0;

	if (edit == NULL || (edit->new_len > 0 && edit->new_text == NULL)) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (!editorActiveDocumentCurrent(&active_document) ||
			active_document == NULL || E.document == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorSizeAdd(edit->start_offset, edit->old_len, &old_end_offset) ||
			old_end_offset > editorDocumentLength(active_document)) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (edit->old_len > 0) {
		removed_text = editorDocumentDupRange(active_document, edit->start_offset, old_end_offset, NULL);
		if (removed_text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
	}
	if (!editorPrepareRowCacheSpliceRegion(active_document, edit->start_offset, edit->old_len,
				&row_region)) {
		free(removed_text);
		editorSetOperationTooLargeStatus();
		return 0;
	}

	if ((E.syntax_state != NULL || editorSyntaxBackgroundEnabled()) &&
			E.syntax_language != EDITOR_SYNTAX_NONE) {
		syntax_track = editorBuildSyntaxEditForDocumentEdit(active_document,
				edit->start_offset, edit->old_len,
				edit->new_len > 0 ? edit->new_text : "", edit->new_len, &syntax_edit);
	}

	if (!editorDocumentReplaceRange(E.document, edit->start_offset, edit->old_len,
				edit->new_len > 0 ? edit->new_text : "", edit->new_len)) {
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}
	if (!editorRowCacheSpliceEndRowForDocument(E.document, &row_region,
				&replacement_end_row_exclusive) ||
			!editorBuildRowsFromDocumentRange(E.document, row_region.start_row,
					replacement_end_row_exclusive, &replacement_rows, &replacement_numrows) ||
			!editorSpliceRowCache(&E.rows, &E.numrows, replacement_rows, replacement_numrows,
					row_region.start_row, row_region.old_end_row_exclusive)) {
		/* Storage was mutated; revert so callers see no partial state. The
		 * compensating replace re-installs `removed_text` over the just-
		 * inserted span and is allocation-free (tree pieces only refcount
		 * the existing buffers).
		 */
		(void)editorDocumentReplaceRange(E.document, edit->start_offset, edit->new_len,
				removed_text != NULL ? removed_text : "", edit->old_len);
		editorFreeRowArray(replacement_rows, replacement_numrows);
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}

	if (!editorSyncCursorFromOffset(edit->after_cursor_offset)) {
		free(removed_text);
		editorSetAllocFailureStatus();
		return 0;
	}
	E.dirty = edit->after_dirty;
	if ((E.syntax_state == NULL && !editorSyntaxBackgroundEnabled()) ||
			E.syntax_language == EDITOR_SYNTAX_NONE) {
		editorSyntaxVisibleCacheInvalidate();
	}

	editorDocumentStatsRecordIncrementalUpdate();
	if (E.edit_pending_mode == EDITOR_EDIT_PENDING_CAPTURED &&
			E.edit_pending_kind != EDITOR_EDIT_NONE) {
		if (!editorHistoryRecordPendingEditFromOperation(E.edit_pending_kind, edit,
					removed_text, edit->old_len)) {
			E.edit_pending_mode = EDITOR_EDIT_PENDING_SKIPPED;
			E.edit_group_kind = EDITOR_EDIT_NONE;
		}
	}
	const char *inserted_text = edit->new_len > 0 ? edit->new_text : "";
	editorNotifyPostEditLanguage(syntax_track, syntax_track ? &syntax_edit : NULL,
			inserted_text, edit->new_len);
	free(removed_text);
	return 1;
}
