#include "input/actions_edit.h"

#include "editing/buffer_core.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/edit_pipeline.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "language/syntax.h"
#include "rotide.h"
#include "support/alloc.h"
#include "text/document.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void editorEditToggleSelectionMode(editorEditActionFn clear_selection_mode,
                                   editorEditActionFn align_cursor_with_row_end) {
	if (E.selection_mode_active) {
		if (clear_selection_mode != NULL) {
			clear_selection_mode();
		}
		return;
	}

	editorColumnSelectionClear();
	if (align_cursor_with_row_end != NULL) {
		align_cursor_with_row_end();
	}
	E.selection_mode_active = 1;
	E.selection_anchor_offset = E.cursor_offset;
}

int editorEditSelectAll(void) {
	if (E.numrows <= 0) {
		return 0;
	}

	int last_row = E.numrows - 1;
	int last_len = (int)editorDocumentLineLength(E.document, last_row);
	size_t end_offset = 0;
	if (!editorBufferPosToOffset(last_row, last_len, &end_offset)) {
		return 0;
	}

	editorColumnSelectionClear();
	E.selection_anchor_offset = 0;
	E.cy = last_row;
	E.cx = last_len;
	E.cursor_offset = end_offset;
	E.selection_mode_active = 1;
	return 1;
}

static int actionsEditCopyRangeToClipboard(const struct editorSelectionRange *range,
                                           size_t *copied_len_out) {
	char *copied = NULL;
	size_t copied_len = 0;
	int extracted = editorExtractRangeText(range, &copied, &copied_len);
	if (extracted <= 0) {
		return extracted;
	}

	if (!editorClipboardSet(copied, copied_len)) {
		free(copied);
		return -1;
	}
	free(copied);

	if (copied_len_out != NULL) {
		*copied_len_out = copied_len;
	}
	return 1;
}

static int actionsEditCopyColumnSelectionToClipboard(size_t *copied_len_out) {
	char *text = NULL;
	size_t len = 0;
	int rc = editorColumnSelectionExtractText(&text, &len);
	if (rc <= 0) {
		return rc;
	}
	if (!editorClipboardSet(text, len)) {
		free(text);
		return -1;
	}
	free(text);
	if (copied_len_out != NULL) {
		*copied_len_out = len;
	}
	return 1;
}

void editorEditCopySelection(editorEditActionFn clear_selection_mode) {
	if (E.column_select_active) {
		size_t copied_len = 0;
		int copied = actionsEditCopyColumnSelectionToClipboard(&copied_len);
		if (copied < 0) {
			return;
		}
		if (copied == 0) {
			editorSetStatusMsg("Selection is empty");
			return;
		}
		editorSetStatusMsg("Copied %zu bytes", copied_len);
		return;
	}

	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		editorSetStatusMsg("No selection");
		return;
	}

	size_t copied_len = 0;
	int copied = actionsEditCopyRangeToClipboard(&range, &copied_len);
	if (copied <= 0) {
		if (copied == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	if (clear_selection_mode != NULL) {
		clear_selection_mode();
	}
	editorSetStatusMsg("Copied %zu bytes", copied_len);
}

void editorEditCutSelection(editorEditActionFn clear_selection_mode) {
	if (E.column_select_active) {
		size_t copied_len = 0;
		int copied = actionsEditCopyColumnSelectionToClipboard(&copied_len);
		if (copied < 0) {
			return;
		}
		if (copied == 0) {
			editorSetStatusMsg("Selection is empty");
			return;
		}
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		int deleted = editorColumnSelectionDelete();
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		if (deleted < 0) {
			return;
		}
		editorSetStatusMsg("Cut %zu bytes", copied_len);
		return;
	}

	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		editorSetStatusMsg("No selection");
		return;
	}

	size_t copied_len = 0;
	int copied = actionsEditCopyRangeToClipboard(&range, &copied_len);
	if (copied <= 0) {
		if (copied == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	int deleted = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	if (deleted <= 0) {
		if (deleted == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	if (clear_selection_mode != NULL) {
		clear_selection_mode();
	}
	editorSetStatusMsg("Cut %zu bytes", copied_len);
}

static const char *actionsEditCommentPrefixForLanguage(enum editorSyntaxLanguage lang) {
	switch (lang) {
		case EDITOR_SYNTAX_C:
		case EDITOR_SYNTAX_CPP:
		case EDITOR_SYNTAX_GO:
		case EDITOR_SYNTAX_JAVASCRIPT:
		case EDITOR_SYNTAX_TYPESCRIPT:
		case EDITOR_SYNTAX_TSX:
		case EDITOR_SYNTAX_JAVA:
		case EDITOR_SYNTAX_RUST:
		case EDITOR_SYNTAX_CSS:
		case EDITOR_SYNTAX_CSHARP:
		case EDITOR_SYNTAX_SCALA:
		case EDITOR_SYNTAX_PHP:
			return "//";
		case EDITOR_SYNTAX_PYTHON:
		case EDITOR_SYNTAX_SHELL:
		case EDITOR_SYNTAX_RUBY:
		case EDITOR_SYNTAX_JULIA:
			return "#";
		case EDITOR_SYNTAX_HASKELL:
			return "--";
		default:
			return NULL;
	}
}

static int actionsEditCommentLeadingWhitespace(const char *chars, int size) {
	int i = 0;
	while (i < size && (chars[i] == ' ' || chars[i] == '\t')) {
		i++;
	}
	return i;
}

static int actionsEditCommentRemovalSkip(const char *chars, int size, int indent, int prefix_len) {
	int skip = prefix_len;
	if (indent + prefix_len < size && chars[indent + prefix_len] == ' ') {
		skip++;
	}
	return skip;
}

/* Returns 1 on success, 0 if a line view could not be obtained. */
static int actionsEditDetectCommentDirection(int start_row, int last_row, const char *prefix,
                                             int prefix_len, int *removing_out) {
	int removing = 1;
	for (int row = start_row; row <= last_row; row++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, row, &line)) {
			return 0;
		}
		if (line.size == 0) {
			editorLineViewRelease(&line);
			continue;
		}
		int indent = actionsEditCommentLeadingWhitespace(line.data, line.size);
		if (indent + prefix_len > line.size ||
		    strncmp(line.data + indent, prefix, (size_t)prefix_len) != 0) {
			removing = 0;
		}
		editorLineViewRelease(&line);
		if (!removing) {
			break;
		}
	}
	*removing_out = removing;
	return 1;
}

/* Returns 1 on success, 0 if a line view could not be obtained. */
static int actionsEditComputeToggledLen(int start_row, int last_row, int prefix_len, int removing,
                                        size_t old_len, size_t *new_len_out) {
	size_t new_len = old_len;
	for (int row = start_row; row <= last_row; row++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, row, &line)) {
			return 0;
		}
		if (line.size == 0) {
			editorLineViewRelease(&line);
			continue;
		}
		if (!removing) {
			new_len += (size_t)prefix_len + 1;
		} else {
			int indent = actionsEditCommentLeadingWhitespace(line.data, line.size);
			int skip = actionsEditCommentRemovalSkip(line.data, line.size, indent,
			                                         prefix_len);
			new_len -= (size_t)skip;
		}
		editorLineViewRelease(&line);
	}
	*new_len_out = new_len;
	return 1;
}

struct actionsEditToggleEditCtx {
	const struct editorSelectionRange *range;
	int last_row;
	const char *prefix;
	int prefix_len;
	int removing;
	char *new_text;
	size_t first_start;
	size_t old_len;
	size_t new_len;
	size_t before_offset;
	size_t cur_row_new_start;
	size_t cur_row_new_size;
};

/* Returns 1 on success, 0 if a line view could not be obtained. */
static int actionsEditBuildToggledText(struct actionsEditToggleEditCtx *ctx) {
	size_t out = 0;
	size_t cur_row_new_start = ctx->first_start;
	size_t cur_row_new_size = 0;

	for (int row = ctx->range->start_cy; row <= ctx->last_row; row++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, row, &line)) {
			return 0;
		}
		const char *chars = line.data;
		int size = line.size;
		size_t out_before = out;

		if (size == 0) {
			/* empty line: emit nothing */
		} else if (!ctx->removing) {
			memcpy(ctx->new_text + out, ctx->prefix, (size_t)ctx->prefix_len);
			out += (size_t)ctx->prefix_len;
			ctx->new_text[out++] = ' ';
			memcpy(ctx->new_text + out, chars, (size_t)size);
			out += (size_t)size;
		} else {
			int indent = actionsEditCommentLeadingWhitespace(chars, size);
			memcpy(ctx->new_text + out, chars, (size_t)indent);
			out += (size_t)indent;
			int skip =
			        actionsEditCommentRemovalSkip(chars, size, indent, ctx->prefix_len);
			int rest = size - indent - skip;
			if (rest > 0) {
				memcpy(ctx->new_text + out, chars + indent + skip, (size_t)rest);
				out += (size_t)rest;
			}
		}
		editorLineViewRelease(&line);

		if (row < E.cy) {
			cur_row_new_start += (out - out_before) + 1;
		} else if (row == E.cy) {
			cur_row_new_size = out - out_before;
		}

		if (row < ctx->last_row) {
			ctx->new_text[out++] = '\n';
		}
	}
	ctx->cur_row_new_start = cur_row_new_start;
	ctx->cur_row_new_size = cur_row_new_size;
	return 1;
}

/* Returns 1 on success, 0 if a line view could not be obtained. */
static int actionsEditComputeAfterCursorOffset(const struct actionsEditToggleEditCtx *ctx,
                                               size_t *after_offset_out) {
	if (E.cy < ctx->range->start_cy || E.cy > ctx->last_row || E.cy >= E.numrows) {
		ptrdiff_t net = (ptrdiff_t)ctx->new_len - (ptrdiff_t)ctx->old_len;
		*after_offset_out = (size_t)((ptrdiff_t)ctx->before_offset + net);
		return 1;
	}

	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 0;
	}
	int size = line.size;
	size_t new_cx = (size_t)E.cx;
	if (size > 0) {
		if (!ctx->removing) {
			new_cx += (size_t)ctx->prefix_len + 1;
		} else {
			int indent = actionsEditCommentLeadingWhitespace(line.data, size);
			int skip = actionsEditCommentRemovalSkip(line.data, size, indent,
			                                         ctx->prefix_len);
			if (new_cx > (size_t)indent + (size_t)skip) {
				new_cx -= (size_t)skip;
			} else {
				new_cx = (size_t)indent;
			}
		}
	}
	editorLineViewRelease(&line);
	if (new_cx > ctx->cur_row_new_size) {
		new_cx = ctx->cur_row_new_size;
	}
	*after_offset_out = ctx->cur_row_new_start + new_cx;
	return 1;
}

static int actionsEditResolveToggleRange(struct editorSelectionRange *range_out, int *last_row_out,
                                         int *had_selection_out) {
	int had_selection = editorGetSelectionRange(range_out);
	*had_selection_out = had_selection;
	if (!had_selection) {
		if (E.cy < 0 || E.cy >= E.numrows) {
			return 0;
		}
		range_out->start_cy = E.cy;
		range_out->start_cx = 0;
		range_out->end_cy = E.cy;
		range_out->end_cx = (int)editorDocumentLineLength(E.document, E.cy);
	}
	int last_row = range_out->end_cy;
	if (had_selection && last_row > range_out->start_cy && range_out->end_cx == 0) {
		last_row--;
	}
	if (range_out->start_cy < 0 || last_row >= E.numrows) {
		return 0;
	}
	*last_row_out = last_row;
	return 1;
}

void editorEditToggleCommentLines(editorEditActionFn clear_selection_mode,
                                  editorEditActionFn pin_active_preview_for_edit) {
	const char *prefix = actionsEditCommentPrefixForLanguage(E.syntax_language);
	if (prefix == NULL) {
		editorSetStatusMsg("No line comment for this language");
		return;
	}
	int prefix_len = (int)strlen(prefix);

	struct editorSelectionRange range;
	int last_row = 0;
	int had_selection = 0;
	if (!actionsEditResolveToggleRange(&range, &last_row, &had_selection)) {
		return;
	}

	int removing = 0;
	if (!actionsEditDetectCommentDirection(range.start_cy, last_row, prefix, prefix_len,
	                                       &removing)) {
		return;
	}

	size_t first_start = 0;
	size_t dummy = 0;
	size_t last_end = 0;
	if (!editorBufferLineByteRange(range.start_cy, &first_start, &dummy) ||
	    !editorBufferLineByteRange(last_row, &dummy, &last_end)) {
		return;
	}
	size_t old_len = last_end - first_start;

	size_t new_len = 0;
	if (!actionsEditComputeToggledLen(range.start_cy, last_row, prefix_len, removing, old_len,
	                                  &new_len)) {
		return;
	}

	char *new_text = editorMalloc(new_len > 0 ? new_len : 1);
	if (new_text == NULL) {
		editorSetAllocFailureStatus();
		return;
	}

	size_t before_offset = 0;
	(void)editorBufferPosToOffset(E.cy, E.cx, &before_offset);

	struct actionsEditToggleEditCtx edit_ctx = {
	        .range = &range,
	        .last_row = last_row,
	        .prefix = prefix,
	        .prefix_len = prefix_len,
	        .removing = removing,
	        .new_text = new_text,
	        .first_start = first_start,
	        .old_len = old_len,
	        .new_len = new_len,
	        .before_offset = before_offset,
	};
	if (!actionsEditBuildToggledText(&edit_ctx)) {
		free(new_text);
		editorSetAllocFailureStatus();
		return;
	}

	size_t after_offset = 0;
	if (!actionsEditComputeAfterCursorOffset(&edit_ctx, &after_offset)) {
		free(new_text);
		editorSetAllocFailureStatus();
		return;
	}

	struct editorDocumentEdit edit = {
	        .kind = EDITOR_EDIT_INSERT_TEXT,
	        .start_offset = first_start,
	        .old_len = old_len,
	        .new_text = new_text,
	        .new_len = new_len,
	        .before_cursor_offset = before_offset,
	        .after_cursor_offset = after_offset,
	        .before_dirty = E.dirty,
	        .after_dirty = E.dirty + 1,
	};

	if (pin_active_preview_for_edit != NULL) {
		pin_active_preview_for_edit();
	}
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	free(new_text);
	if (had_selection && clear_selection_mode != NULL) {
		clear_selection_mode();
	}
}

void editorEditMoveCurrentLine(int direction) {
	int cur = E.cy;
	int other = cur + direction;

	if (cur < 0 || cur >= E.numrows || other < 0 || other >= E.numrows) {
		return;
	}

	int first = direction < 0 ? other : cur;
	int second = direction < 0 ? cur : other;

	size_t first_start = 0;
	size_t first_end = 0;
	size_t second_start = 0;
	size_t second_end = 0;
	if (!editorBufferLineByteRange(first, &first_start, &first_end) ||
	    !editorBufferLineByteRange(second, &second_start, &second_end)) {
		return;
	}

	struct editorLineView first_view = {0};
	struct editorLineView second_view = {0};
	if (!editorDocumentLineView(E.document, first, &first_view)) {
		return;
	}
	if (!editorDocumentLineView(E.document, second, &second_view)) {
		editorLineViewRelease(&first_view);
		return;
	}
	int first_len = first_view.size;
	int second_len = second_view.size;

	size_t new_len = (size_t)second_len + 1 + (size_t)first_len;
	char *new_text = editorMalloc(new_len);
	if (new_text == NULL) {
		editorLineViewRelease(&second_view);
		editorLineViewRelease(&first_view);
		editorSetAllocFailureStatus();
		return;
	}
	memcpy(new_text, second_view.data, (size_t)second_len);
	new_text[second_len] = '\n';
	memcpy(new_text + second_len + 1, first_view.data, (size_t)first_len);
	editorLineViewRelease(&second_view);
	editorLineViewRelease(&first_view);

	size_t old_len = second_end - first_start;

	size_t cx = (size_t)E.cx;
	size_t after_offset;
	if (direction < 0) {
		if (cx > (size_t)second_len) {
			cx = (size_t)second_len;
		}
		after_offset = first_start + cx;
	} else {
		if (cx > (size_t)first_len) {
			cx = (size_t)first_len;
		}
		after_offset = first_start + (size_t)second_len + 1 + cx;
	}

	size_t before_offset = 0;
	(void)editorBufferPosToOffset(cur, E.cx, &before_offset);

	struct editorDocumentEdit edit = {
	        .kind = EDITOR_EDIT_INSERT_TEXT,
	        .start_offset = first_start,
	        .old_len = old_len,
	        .new_text = new_text,
	        .new_len = new_len,
	        .before_cursor_offset = before_offset,
	        .after_cursor_offset = after_offset,
	        .before_dirty = E.dirty,
	        .after_dirty = E.dirty + 1,
	};

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorApplyDocumentEdit(&edit);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);

	free(new_text);
}

void editorEditDeleteSelection(editorEditActionFn clear_selection_mode) {
	if (E.column_select_active) {
		editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
		int dirty_before = E.dirty;
		int deleted = editorColumnSelectionDelete();
		editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
		if (deleted < 0) {
			return;
		}
		return;
	}

	struct editorSelectionRange range;
	if (!editorGetSelectionRange(&range)) {
		editorSetStatusMsg("No selection");
		return;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	int dirty_before = E.dirty;
	int deleted = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	if (deleted <= 0) {
		if (deleted == 0) {
			editorSetStatusMsg("No selection");
		}
		return;
	}

	if (clear_selection_mode != NULL) {
		clear_selection_mode();
	}
}

void editorEditPasteClipboard(editorEditActionFn clear_selection_mode) {
	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	if (clip_len <= 0) {
		editorSetStatusMsg("Clipboard is empty");
		return;
	}

	if (E.column_select_active) {
		editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
		int dirty_before = E.dirty;
		int pasted = editorColumnSelectionPasteText(clip, clip_len);
		editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
		editorHistoryBreakGroup();
		if (pasted) {
			editorSetStatusMsg("Pasted %zu bytes", clip_len);
		}
		return;
	}

	struct editorSelectionRange range;
	int has_selection = editorGetSelectionRange(&range);
	int indent_cy = has_selection ? range.start_cy : E.cy;
	int indent_cx = has_selection ? range.start_cx : E.cx;
	char *indented_clip = NULL;
	size_t indented_clip_len = 0;
	int indent_result = editorBuildAutoIndentedText(clip, clip_len, indent_cy, indent_cx,
	                                                &indented_clip, &indented_clip_len);
	if (indent_result < 0) {
		return;
	}
	const char *paste_text = indent_result > 0 ? indented_clip : clip;
	size_t paste_len = indent_result > 0 ? indented_clip_len : clip_len;

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	int pasted = 0;

	if (has_selection) {
		if (clear_selection_mode != NULL) {
			clear_selection_mode();
		}
		pasted = editorReplaceRange(&range, paste_text, paste_len) > 0;
	} else {
		if (clear_selection_mode != NULL) {
			clear_selection_mode();
		}
		pasted = editorInsertText(paste_text, paste_len);
	}

	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	free(indented_clip);

	if (pasted) {
		editorSetStatusMsg("Pasted %zu bytes", clip_len);
	}
}

static void actionsEditCallIfPresent(editorEditActionFn fn) {
	if (fn != NULL) {
		fn();
	}
}

static void actionsEditPinAndRun(editorEditActionFn pin_fn, editorEditActionFn action_fn) {
	actionsEditCallIfPresent(pin_fn);
	actionsEditCallIfPresent(action_fn);
}

static void actionsEditClearPinAndRun(editorEditActionFn clear_fn, editorEditActionFn pin_fn,
                                      editorEditActionFn action_fn) {
	actionsEditCallIfPresent(clear_fn);
	actionsEditPinAndRun(pin_fn, action_fn);
}

static void actionsEditRunHistoryUndoRedo(int (*op)(void), editorEditActionFn pin_fn,
                                          editorEditActionFn clear_search_fn,
                                          int cursor_or_edit_effect_bit, int *effects) {
	editorHistoryBreakGroup();
	actionsEditCallIfPresent(pin_fn);
	if (op() == 1) {
		actionsEditCallIfPresent(clear_search_fn);
		*effects |= cursor_or_edit_effect_bit;
	}
}

static void actionsEditRunNewline(editorEditActionFn clear_selection_mode,
                                  editorEditActionFn pin_active_preview_for_edit) {
	actionsEditCallIfPresent(clear_selection_mode);
	actionsEditCallIfPresent(pin_active_preview_for_edit);
	editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
	int dirty_before = E.dirty;
	editorInsertNewline();
	editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
}

int editorHandleEditMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
                                 const struct editorEditMappedCallbacks *callbacks,
                                 int *effects_io) {
	const struct editorEditMappedCallbacks empty_callbacks = {0};
	int effects = effects_io != NULL ? *effects_io : 0;

	if (callbacks == NULL) {
		callbacks = &empty_callbacks;
	}

	switch (action) {
		case EDITOR_ACTION_NEWLINE:
			actionsEditRunNewline(callbacks->clear_selection_mode,
			                      callbacks->pin_active_preview_for_edit);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_TOGGLE_SELECTION:
			editorHistoryBreakGroup();
			actionsEditCallIfPresent(callbacks->toggle_selection_mode);
			break;
		case EDITOR_ACTION_COPY_SELECTION:
			editorHistoryBreakGroup();
			actionsEditCallIfPresent(callbacks->copy_selection);
			break;
		case EDITOR_ACTION_CUT_SELECTION:
			editorHistoryBreakGroup();
			actionsEditPinAndRun(callbacks->pin_active_preview_for_edit,
			                     callbacks->cut_selection);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_DELETE_SELECTION:
			editorHistoryBreakGroup();
			actionsEditPinAndRun(callbacks->pin_active_preview_for_edit,
			                     callbacks->delete_selection);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_PASTE:
			editorHistoryBreakGroup();
			actionsEditPinAndRun(callbacks->pin_active_preview_for_edit,
			                     callbacks->paste_clipboard);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_UNDO:
			actionsEditRunHistoryUndoRedo(
			        editorUndo, callbacks->pin_active_preview_for_edit,
			        callbacks->clear_search_state, cursor_or_edit_effect_bit, &effects);
			break;
		case EDITOR_ACTION_REDO:
			actionsEditRunHistoryUndoRedo(
			        editorRedo, callbacks->pin_active_preview_for_edit,
			        callbacks->clear_search_state, cursor_or_edit_effect_bit, &effects);
			break;
		case EDITOR_ACTION_DELETE_CHAR:
			actionsEditPinAndRun(callbacks->pin_active_preview_for_edit,
			                     callbacks->delete_char_action);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_BACKSPACE:
			actionsEditPinAndRun(callbacks->pin_active_preview_for_edit,
			                     callbacks->backspace_action);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_MOVE_LINE_UP:
			actionsEditClearPinAndRun(callbacks->clear_selection_mode,
			                          callbacks->pin_active_preview_for_edit,
			                          callbacks->move_line_up);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_MOVE_LINE_DOWN:
			actionsEditClearPinAndRun(callbacks->clear_selection_mode,
			                          callbacks->pin_active_preview_for_edit,
			                          callbacks->move_line_down);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_TOGGLE_COMMENT:
			actionsEditPinAndRun(callbacks->pin_active_preview_for_edit,
			                     callbacks->toggle_comment_lines);
			effects |= cursor_or_edit_effect_bit;
			break;
		default:
			return 0;
	}

	if (effects_io != NULL) {
		*effects_io = effects;
	}
	return 1;
}
