#include "editing/history.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include <string.h>
#include <stdlib.h>

void editorHistoryEntryFree(struct editorHistoryEntry *entry) {
	free(entry->removed_text);
	free(entry->inserted_text);
	memset(entry, 0, sizeof(*entry));
}

void editorHistoryClear(struct editorHistory *history) {
	for (int i = 0; i < history->len; i++) {
		int idx = (history->start + i) % ROTIDE_UNDO_HISTORY_LIMIT;
		editorHistoryEntryFree(&history->entries[idx]);
	}
	history->start = 0;
	history->len = 0;
}

static void editorHistoryPushNewest(struct editorHistory *history,
		struct editorHistoryEntry *entry) {
	int slot = 0;
	if (history->len < ROTIDE_UNDO_HISTORY_LIMIT) {
		slot = (history->start + history->len) % ROTIDE_UNDO_HISTORY_LIMIT;
		history->len++;
	} else {
		slot = history->start;
		editorHistoryEntryFree(&history->entries[slot]);
		history->start = (history->start + 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	}

	history->entries[slot] = *entry;
	memset(entry, 0, sizeof(*entry));
}

static int editorHistoryPopNewest(struct editorHistory *history,
		struct editorHistoryEntry *entry) {
	if (history->len == 0) {
		return 0;
	}

	int idx = (history->start + history->len - 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	*entry = history->entries[idx];
	memset(&history->entries[idx], 0, sizeof(history->entries[idx]));
	history->len--;
	if (history->len == 0) {
		history->start = 0;
	}
	return 1;
}

static struct editorHistoryEntry *editorHistoryNewest(struct editorHistory *history) {
	if (history == NULL || history->len == 0) {
		return NULL;
	}
	int idx = (history->start + history->len - 1) % ROTIDE_UNDO_HISTORY_LIMIT;
	return &history->entries[idx];
}

static int editorHistoryDupSlice(const char *text, size_t len, char **dst_out) {
	char *dup = NULL;

	if (dst_out == NULL) {
		return 0;
	}
	*dst_out = NULL;
	if (len == 0) {
		return 1;
	}

	size_t cap = 0;
	if (!editorSizeAdd(len, 1, &cap)) {
		return 0;
	}
	dup = editorMalloc(cap);
	if (dup == NULL) {
		return 0;
	}
	memcpy(dup, text, len);
	dup[len] = '\0';
	*dst_out = dup;
	return 1;
}

static int editorHistoryAppendText(char **text_in_out, size_t *len_in_out,
		const char *append, size_t append_len) {
	size_t old_len = 0;
	size_t new_len = 0;
	size_t cap = 0;
	char *grown = NULL;

	if (text_in_out == NULL || len_in_out == NULL) {
		return 0;
	}
	old_len = *len_in_out;
	if (!editorSizeAdd(old_len, append_len, &new_len) ||
			!editorSizeAdd(new_len, 1, &cap)) {
		return 0;
	}
	grown = editorRealloc(*text_in_out, cap);
	if (grown == NULL) {
		return 0;
	}
	if (append_len > 0 && append != NULL) {
		memcpy(grown + old_len, append, append_len);
	}
	grown[new_len] = '\0';
	*text_in_out = grown;
	*len_in_out = new_len;
	return 1;
}

static int editorHistoryTryMergeInsert(struct editorHistory *history,
		const struct editorHistoryEntry *entry) {
	struct editorHistoryEntry *latest = editorHistoryNewest(history);
	int append_at_end = 0;
	int append_before_trailing_newline = 0;
	if (latest == NULL || entry == NULL) {
		return 0;
	}
	if (latest->kind != EDITOR_EDIT_INSERT_TEXT ||
			entry->kind != EDITOR_EDIT_INSERT_TEXT ||
			latest->removed_len != 0 ||
			entry->removed_len != 0 ||
			latest->after_cursor_offset != entry->before_cursor_offset) {
		return 0;
	}

	append_at_end = latest->start_offset + latest->inserted_len == entry->start_offset;
	append_before_trailing_newline = latest->inserted_len > 0 &&
			latest->inserted_text != NULL &&
			latest->inserted_text[latest->inserted_len - 1] == '\n' &&
			latest->start_offset + latest->inserted_len - 1 == entry->start_offset;
	if (!append_at_end && !append_before_trailing_newline) {
		return 0;
	}

	if (append_at_end) {
		if (!editorHistoryAppendText(&latest->inserted_text, &latest->inserted_len,
					entry->inserted_text, entry->inserted_len)) {
			return 0;
		}
	} else {
		size_t prefix_len = latest->inserted_len - 1;
		size_t merged_len = 0;
		size_t cap = 0;
		if (!editorSizeAdd(prefix_len, entry->inserted_len, &merged_len) ||
				!editorSizeAdd(merged_len, 1, &merged_len) ||
				!editorSizeAdd(merged_len, 1, &cap)) {
			return 0;
		}
		char *grown = editorRealloc(latest->inserted_text, cap);
		if (grown == NULL) {
			return 0;
		}
		latest->inserted_text = grown;
		memmove(latest->inserted_text + prefix_len + entry->inserted_len,
				latest->inserted_text + prefix_len, 2);
		if (entry->inserted_len > 0 && entry->inserted_text != NULL) {
			memcpy(latest->inserted_text + prefix_len, entry->inserted_text, entry->inserted_len);
		}
		latest->inserted_len = merged_len;
	}

	latest->after_cursor_offset = entry->after_cursor_offset;
	latest->after_dirty = entry->after_dirty;
	return 1;
}

int editorHistoryRecordPendingEditFromOperation(enum editorEditKind kind,
		const struct editorDocumentEdit *edit, const char *removed_text, size_t removed_len) {
	struct editorHistoryEntry entry = {0};
	if (edit == NULL) {
		return 0;
	}

	entry.kind = kind;
	entry.start_offset = edit->start_offset;
	entry.removed_len = removed_len;
	entry.inserted_len = edit->new_len;
	entry.before_cursor_offset = edit->before_cursor_offset;
	entry.after_cursor_offset = edit->after_cursor_offset;
	entry.before_dirty = edit->before_dirty;
	entry.after_dirty = edit->after_dirty;

	if (!editorHistoryDupSlice(removed_text != NULL ? removed_text : "", removed_len,
				&entry.removed_text) ||
			!editorHistoryDupSlice(edit->new_len > 0 ? edit->new_text : "", edit->new_len,
				&entry.inserted_text)) {
		editorHistoryEntryFree(&entry);
		return 0;
	}

	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry = entry;
	E.edit_pending_entry_valid = 1;
	return 1;
}

static int editorApplyHistoryEntry(const struct editorHistoryEntry *entry, int inverse) {
	if (entry == NULL) {
		return -1;
	}

	struct editorDocumentEdit edit = {
		.kind = entry->kind,
		.start_offset = entry->start_offset,
		.old_len = inverse ? entry->inserted_len : entry->removed_len,
		.new_text = inverse ? entry->removed_text : entry->inserted_text,
		.new_len = inverse ? entry->removed_len : entry->inserted_len,
		.before_cursor_offset = inverse ? entry->after_cursor_offset : entry->before_cursor_offset,
		.after_cursor_offset = inverse ? entry->before_cursor_offset : entry->after_cursor_offset,
		.before_dirty = inverse ? entry->after_dirty : entry->before_dirty,
		.after_dirty = inverse ? entry->before_dirty : entry->after_dirty
	};
	return editorApplyDocumentEdit(&edit) ? 1 : -1;
}

void editorHistoryReset(void) {
	editorHistoryClear(&E.undo_history);
	editorHistoryClear(&E.redo_history);
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
	editorClearSelectionState();
}

void editorHistoryBreakGroup(void) {
	E.edit_group_kind = EDITOR_EDIT_NONE;
}

void editorHistoryBeginEdit(enum editorEditKind kind) {
	editorHistoryDiscardEdit();
	E.edit_pending_kind = kind;

	if (kind != EDITOR_EDIT_INSERT_TEXT) {
		E.edit_group_kind = EDITOR_EDIT_NONE;
	}
	E.edit_pending_mode = EDITOR_EDIT_PENDING_CAPTURED;
}

void editorHistoryCommitEdit(enum editorEditKind kind, int changed) {
	enum editorEditPendingMode mode = E.edit_pending_mode;
	int recorded = 0;
	if (!changed) {
		editorHistoryDiscardEdit();
		E.edit_group_kind = EDITOR_EDIT_NONE;
		return;
	}

	editorHistoryClear(&E.redo_history);

	if (mode == EDITOR_EDIT_PENDING_CAPTURED &&
			E.edit_pending_kind == kind &&
			E.edit_pending_entry_valid) {
		struct editorHistoryEntry entry = E.edit_pending_entry;
		memset(&E.edit_pending_entry, 0, sizeof(E.edit_pending_entry));
		E.edit_pending_entry_valid = 0;
		recorded = 1;
		if (!(kind == EDITOR_EDIT_INSERT_TEXT &&
				E.edit_group_kind == EDITOR_EDIT_INSERT_TEXT &&
				editorHistoryTryMergeInsert(&E.undo_history, &entry))) {
			editorHistoryPushNewest(&E.undo_history, &entry);
		}
		editorHistoryEntryFree(&entry);
	}

	if (kind == EDITOR_EDIT_INSERT_TEXT && mode == EDITOR_EDIT_PENDING_CAPTURED && recorded) {
		E.edit_group_kind = EDITOR_EDIT_INSERT_TEXT;
	} else {
		E.edit_group_kind = EDITOR_EDIT_NONE;
	}

	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

void editorHistoryDiscardEdit(void) {
	editorHistoryEntryFree(&E.edit_pending_entry);
	E.edit_pending_entry_valid = 0;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
}

int editorUndo(void) {
	editorHistoryBreakGroup();
	editorHistoryDiscardEdit();

	struct editorHistoryEntry target = {0};
	if (!editorHistoryPopNewest(&E.undo_history, &target)) {
		editorSetStatusMsg("Nothing to undo");
		return 0;
	}

	if (editorApplyHistoryEntry(&target, 1) != 1) {
		editorHistoryPushNewest(&E.undo_history, &target);
		return -1;
	}

	editorHistoryPushNewest(&E.redo_history, &target);
	return 1;
}

int editorRedo(void) {
	editorHistoryBreakGroup();
	editorHistoryDiscardEdit();

	struct editorHistoryEntry target = {0};
	if (!editorHistoryPopNewest(&E.redo_history, &target)) {
		editorSetStatusMsg("Nothing to redo");
		return 0;
	}

	if (editorApplyHistoryEntry(&target, 0) != 1) {
		editorHistoryPushNewest(&E.redo_history, &target);
		return -1;
	}

	editorHistoryPushNewest(&E.undo_history, &target);
	return 1;
}
