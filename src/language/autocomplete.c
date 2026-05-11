#include "language/autocomplete.h"

#include "editing/buffer_core.h"
#include "editing/selection.h"
#include "language/lsp.h"
#include "language/lsp_protocol.h"
#include "render/popup.h"
#include "text/row.h"
#include "rotide.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static struct {
	int active;
	int request_id;
	int document_version;
	int request_cy;
	int request_cx;
	int prefix_start_cx;
	char *prefix;
	char *filename;
	struct editorLspCompletionItem *items;
	int count;
} g_autocomplete;

static int editorAutocompleteIsIdentByte(unsigned char b) {
	return isalnum(b) || b == '_' || b >= 0x80;
}

static void editorAutocompleteReset(void) {
	editorPopupClose();
	editorLspFreeCompletionItems(g_autocomplete.items, g_autocomplete.count);
	g_autocomplete.items = NULL;
	g_autocomplete.count = 0;
	free(g_autocomplete.prefix);
	g_autocomplete.prefix = NULL;
	free(g_autocomplete.filename);
	g_autocomplete.filename = NULL;
	g_autocomplete.active = 0;
	g_autocomplete.request_id = 0;
	g_autocomplete.document_version = 0;
	g_autocomplete.request_cy = 0;
	g_autocomplete.request_cx = 0;
	g_autocomplete.prefix_start_cx = 0;
}

void editorAutocompleteShutdown(void) {
	editorAutocompleteReset();
}

void editorAutocompleteCancel(void) {
	editorLspCancelCompletion();
	editorAutocompleteReset();
}

int editorAutocompleteIsVisible(void) {
	return g_autocomplete.active;
}

static int editorAutocompleteTriggerCharMatches(const char *trigger_chars, int ch) {
	if (trigger_chars == NULL || trigger_chars[0] == '\0') {
		return 0;
	}
	for (const unsigned char *p = (const unsigned char *)trigger_chars; *p != '\0'; p++) {
		if ((int)*p == ch) {
			return 1;
		}
	}
	return 0;
}

static int editorAutocompletePrefixStartCx(const struct erow *row, int cursor_cx) {
	if (row == NULL || cursor_cx <= 0) {
		return cursor_cx < 0 ? 0 : cursor_cx;
	}
	int idx = cursor_cx;
	while (idx > 0) {
		int prev = editorRowPrevCharIdx(row, idx);
		if (prev < 0 || prev >= idx) {
			break;
		}
		unsigned char b = (unsigned char)row->chars[prev];
		if (!editorAutocompleteIsIdentByte(b)) {
			break;
		}
		idx = prev;
	}
	return idx;
}

static char *editorAutocompleteCopyPrefix(const struct erow *row, int start_cx, int end_cx) {
	int len = end_cx - start_cx;
	if (row == NULL || len <= 0) {
		char *empty = strdup("");
		return empty;
	}
	if (start_cx < 0) {
		start_cx = 0;
	}
	if (end_cx > row->size) {
		end_cx = row->size;
	}
	len = end_cx - start_cx;
	if (len <= 0) {
		return strdup("");
	}
	char *out = malloc((size_t)len + 1);
	if (out == NULL) {
		return NULL;
	}
	memcpy(out, &row->chars[start_cx], (size_t)len);
	out[len] = '\0';
	return out;
}

static int editorAutocompleteItemBeginsWith(const char *label, const char *prefix) {
	if (label == NULL) {
		return 0;
	}
	if (prefix == NULL || prefix[0] == '\0') {
		return 1;
	}
	size_t pl = strlen(prefix);
	if (strncmp(label, prefix, pl) == 0) {
		return 1;
	}
	if (strncasecmp(label, prefix, pl) == 0) {
		return 1;
	}
	return 0;
}

static int editorAutocompleteShouldFire(int ch, const char *trigger_chars, int *trigger_kind_out,
		int *trigger_character_out) {
	if (trigger_kind_out != NULL) {
		*trigger_kind_out = 1;
	}
	if (trigger_character_out != NULL) {
		*trigger_character_out = 0;
	}
	if (editorAutocompleteTriggerCharMatches(trigger_chars, ch)) {
		if (trigger_kind_out != NULL) {
			*trigger_kind_out = 2;
		}
		if (trigger_character_out != NULL) {
			*trigger_character_out = ch;
		}
		return 1;
	}
	if (ch >= 0x20 && ch < 0x80 && editorAutocompleteIsIdentByte((unsigned char)ch)) {
		return 1;
	}
	return 0;
}

void editorAutocompleteOnCharInserted(int ch) {
	if (E.filename == NULL || E.filename[0] == '\0') {
		editorAutocompleteReset();
		return;
	}
	if (!editorLspCompletionEnabledForFile(E.filename, E.syntax_language)) {
		editorAutocompleteReset();
		return;
	}
	if (E.cy < 0 || E.cy >= E.numrows) {
		editorAutocompleteReset();
		return;
	}

	const char *trigger_chars =
			editorLspCompletionTriggerCharsForFile(E.filename, E.syntax_language);
	int trigger_kind = 1;
	int trigger_character = 0;
	if (!editorAutocompleteShouldFire(ch, trigger_chars, &trigger_kind, &trigger_character)) {
		editorAutocompleteCancel();
		return;
	}

	struct erow *row = &E.rows[E.cy];
	int prefix_start_cx = editorAutocompletePrefixStartCx(row, E.cx);
	char *prefix = editorAutocompleteCopyPrefix(row, prefix_start_cx, E.cx);
	if (prefix == NULL) {
		editorAutocompleteReset();
		return;
	}

	int requested = editorLspRequestCompletionAsync(E.filename, E.syntax_language, E.cy, E.cx,
			E.lsp_doc_version, prefix_start_cx, prefix, trigger_kind, trigger_character);
	free(prefix);
	if (!requested) {
		editorAutocompleteReset();
		return;
	}
}

void editorAutocompleteOnCursorMoved(void) {
	if (!g_autocomplete.active) {
		return;
	}
	if (E.cy != g_autocomplete.request_cy) {
		editorAutocompleteCancel();
		return;
	}
	if (E.cx < g_autocomplete.prefix_start_cx) {
		editorAutocompleteCancel();
		return;
	}
}

static int editorAutocompleteApplyItem(const struct editorLspCompletionItem *item,
		int prefix_start_cy, int prefix_start_cx, int cursor_cx) {
	if (item == NULL) {
		return 0;
	}
	const char *insert_text = NULL;
	int range_start_cy = prefix_start_cy;
	int range_start_cx = prefix_start_cx;
	int range_end_cy = prefix_start_cy;
	int range_end_cx = cursor_cx;

	if (item->has_text_edit && item->text_edit_new_text != NULL) {
		insert_text = item->text_edit_new_text;
		range_start_cy = item->text_edit_start_line;
		range_end_cy = item->text_edit_end_line;
		range_start_cx = editorLspProtocolCharacterToBufferColumn(range_start_cy,
				item->text_edit_start_character);
		range_end_cx = editorLspProtocolCharacterToBufferColumn(range_end_cy,
				item->text_edit_end_character);
	} else if (item->insert_text != NULL) {
		insert_text = item->insert_text;
	} else {
		insert_text = item->label;
	}
	if (insert_text == NULL) {
		return 0;
	}

	struct editorSelectionRange range = {
		.start_cy = range_start_cy,
		.start_cx = range_start_cx,
		.end_cy = range_end_cy,
		.end_cx = range_end_cx,
	};
	if (editorReplaceRange(&range, insert_text, strlen(insert_text)) < 0) {
		return 0;
	}
	return 1;
}

int editorAutocompleteAcceptSelection(void) {
	if (!editorAutocompleteIsVisible()) {
		return 0;
	}
	int idx = editorPopupSelectedIndex();
	if (idx < 0 || idx >= g_autocomplete.count) {
		editorAutocompleteReset();
		return 0;
	}
	struct editorLspCompletionItem item_copy = {0};
	if (g_autocomplete.items[idx].label != NULL) {
		item_copy.label = strdup(g_autocomplete.items[idx].label);
	}
	if (g_autocomplete.items[idx].insert_text != NULL) {
		item_copy.insert_text = strdup(g_autocomplete.items[idx].insert_text);
	}
	item_copy.has_text_edit = g_autocomplete.items[idx].has_text_edit;
	item_copy.text_edit_start_line = g_autocomplete.items[idx].text_edit_start_line;
	item_copy.text_edit_start_character = g_autocomplete.items[idx].text_edit_start_character;
	item_copy.text_edit_end_line = g_autocomplete.items[idx].text_edit_end_line;
	item_copy.text_edit_end_character = g_autocomplete.items[idx].text_edit_end_character;
	if (g_autocomplete.items[idx].text_edit_new_text != NULL) {
		item_copy.text_edit_new_text = strdup(g_autocomplete.items[idx].text_edit_new_text);
	}

	int prefix_start_cy = g_autocomplete.request_cy;
	int prefix_start_cx = g_autocomplete.prefix_start_cx;
	int cursor_cx = E.cx;
	editorAutocompleteReset();

	int ok = editorAutocompleteApplyItem(&item_copy, prefix_start_cy, prefix_start_cx, cursor_cx);
	free(item_copy.label);
	free(item_copy.insert_text);
	free(item_copy.text_edit_new_text);
	return ok;
}

void editorAutocompleteHandleCompletionResponse(int request_id, int document_version,
		int request_cy, int request_cx, int prefix_start_cx, const char *prefix,
		const char *filename, struct editorLspCompletionItem *items, int count) {
	int max_items = E.lsp_autocomplete_max_items > 0 ? E.lsp_autocomplete_max_items : 50;

	if (filename == NULL || E.filename == NULL || strcmp(filename, E.filename) != 0) {
		editorLspFreeCompletionItems(items, count);
		return;
	}
	if (document_version != E.lsp_doc_version) {
		editorLspFreeCompletionItems(items, count);
		return;
	}
	if (E.cy != request_cy) {
		editorLspFreeCompletionItems(items, count);
		return;
	}
	if (E.cx < prefix_start_cx) {
		editorLspFreeCompletionItems(items, count);
		return;
	}

	struct erow *row = (request_cy >= 0 && request_cy < E.numrows) ? &E.rows[request_cy] : NULL;
	if (row == NULL) {
		editorLspFreeCompletionItems(items, count);
		return;
	}
	int current_prefix_cx = E.cx;
	if (current_prefix_cx < prefix_start_cx || current_prefix_cx > row->size) {
		editorLspFreeCompletionItems(items, count);
		return;
	}
	char *current_prefix =
			editorAutocompleteCopyPrefix(row, prefix_start_cx, current_prefix_cx);
	if (current_prefix == NULL) {
		editorLspFreeCompletionItems(items, count);
		return;
	}

	if (prefix != NULL && current_prefix[0] != '\0' &&
			strncmp(current_prefix, prefix, strlen(prefix)) != 0) {
		free(current_prefix);
		editorLspFreeCompletionItems(items, count);
		return;
	}

	struct editorPopupItem *popup_items = NULL;
	struct editorLspCompletionItem *filtered = NULL;
	int filtered_count = 0;
	int filtered_cap = 0;

	for (int i = 0; i < count && filtered_count < max_items; i++) {
		if (!editorAutocompleteItemBeginsWith(items[i].label, current_prefix)) {
			continue;
		}
		if (filtered_count >= filtered_cap) {
			int new_cap = filtered_cap > 0 ? filtered_cap * 2 : 16;
			struct editorLspCompletionItem *grown =
					realloc(filtered, sizeof(*filtered) * (size_t)new_cap);
			if (grown == NULL) {
				free(current_prefix);
				editorLspFreeCompletionItems(filtered, filtered_count);
				free(popup_items);
				editorLspFreeCompletionItems(items, count);
				return;
			}
			filtered = grown;
			struct editorPopupItem *grown_popup =
					realloc(popup_items, sizeof(*popup_items) * (size_t)new_cap);
			if (grown_popup == NULL) {
				free(current_prefix);
				editorLspFreeCompletionItems(filtered, filtered_count);
				editorLspFreeCompletionItems(items, count);
				return;
			}
			popup_items = grown_popup;
			filtered_cap = new_cap;
		}
		filtered[filtered_count] = items[i];
		memset(&items[i], 0, sizeof(items[i]));
		popup_items[filtered_count].label = filtered[filtered_count].label;
		popup_items[filtered_count].detail = NULL;
		filtered_count++;
	}

	editorLspFreeCompletionItems(items, count);
	(void)request_id;

	if (filtered_count <= 0) {
		free(current_prefix);
		free(popup_items);
		editorLspFreeCompletionItems(filtered, filtered_count);
		editorAutocompleteReset();
		return;
	}

	editorAutocompleteReset();

	if (!editorPopupOpen(popup_items, filtered_count, request_cy, prefix_start_cx)) {
		free(popup_items);
		free(current_prefix);
		editorLspFreeCompletionItems(filtered, filtered_count);
		return;
	}
	free(popup_items);

	g_autocomplete.items = filtered;
	g_autocomplete.count = filtered_count;
	g_autocomplete.active = 1;
	g_autocomplete.request_id = request_id;
	g_autocomplete.document_version = document_version;
	g_autocomplete.request_cy = request_cy;
	g_autocomplete.request_cx = request_cx;
	g_autocomplete.prefix_start_cx = prefix_start_cx;
	g_autocomplete.prefix = current_prefix;
	g_autocomplete.filename = strdup(filename);
}
