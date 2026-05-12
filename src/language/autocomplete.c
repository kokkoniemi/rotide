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
	int *visible_indices;
	int visible_count;
	int visible_capacity;
} g_autocomplete;

static int editorAutocompleteIsIdentByte(unsigned char b) {
	return isalnum(b) || b == '_' || b >= 0x80;
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

static void editorAutocompleteReset(void) {
	editorPopupClose();
	editorLspFreeCompletionItems(g_autocomplete.items, g_autocomplete.count);
	g_autocomplete.items = NULL;
	g_autocomplete.count = 0;
	free(g_autocomplete.visible_indices);
	g_autocomplete.visible_indices = NULL;
	g_autocomplete.visible_count = 0;
	g_autocomplete.visible_capacity = 0;
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

int editorAutocompleteWouldRefilter(int ch) {
	if (!g_autocomplete.active) {
		return 0;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		return 0;
	}
	if (!editorLspCompletionEnabledForFile(E.filename, E.syntax_language)) {
		return 0;
	}
	const char *trigger_chars =
			editorLspCompletionTriggerCharsForFile(E.filename, E.syntax_language);
	if (editorAutocompleteTriggerCharMatches(trigger_chars, ch)) {
		return 1;
	}
	if (ch >= 0x20 && ch < 0x80 && editorAutocompleteIsIdentByte((unsigned char)ch)) {
		return 1;
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

static const char *editorAutocompleteStripLeadingNonIdent(const char *label) {
	if (label == NULL) {
		return NULL;
	}
	const char *p = label;
	while (*p != '\0') {
		unsigned char b = (unsigned char)*p;
		if (editorAutocompleteIsIdentByte(b)) {
			break;
		}
		p++;
	}
	return p;
}

static const char *editorAutocompleteItemMatchText(const struct editorLspCompletionItem *item) {
	if (item == NULL) {
		return NULL;
	}
	if (item->filter_text != NULL && item->filter_text[0] != '\0') {
		return item->filter_text;
	}
	if (item->insert_text != NULL && item->insert_text[0] != '\0') {
		return item->insert_text;
	}
	return editorAutocompleteStripLeadingNonIdent(item->label);
}

static int editorAutocompleteMatchBeginsWith(const char *text, const char *prefix) {
	if (text == NULL) {
		return 0;
	}
	if (prefix == NULL || prefix[0] == '\0') {
		return 1;
	}
	size_t pl = strlen(prefix);
	if (strncmp(text, prefix, pl) == 0) {
		return 1;
	}
	if (strncasecmp(text, prefix, pl) == 0) {
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

/*
 * Rebuild the popup from the current raw items, filtering by g_autocomplete.prefix.
 * Returns 1 if the popup is still shown after filtering, 0 otherwise (no matches).
 */
static int editorAutocompleteRefreshFiltered(int anchor_row, int anchor_col) {
	int max_items = E.lsp_autocomplete_max_items > 0 ? E.lsp_autocomplete_max_items : 100;

	struct editorPopupItem *popup_items = NULL;
	int *visible_indices = NULL;
	int visible_count = 0;
	int cap = 0;

	for (int i = 0; i < g_autocomplete.count && visible_count < max_items; i++) {
		const char *match_text = editorAutocompleteItemMatchText(&g_autocomplete.items[i]);
		if (!editorAutocompleteMatchBeginsWith(match_text, g_autocomplete.prefix)) {
			continue;
		}
		if (visible_count >= cap) {
			int new_cap = cap > 0 ? cap * 2 : 16;
			struct editorPopupItem *grown_popup =
					realloc(popup_items, sizeof(*popup_items) * (size_t)new_cap);
			if (grown_popup == NULL) {
				free(popup_items);
				free(visible_indices);
				return 0;
			}
			popup_items = grown_popup;
			int *grown_indices = realloc(visible_indices, sizeof(*visible_indices) * (size_t)new_cap);
			if (grown_indices == NULL) {
				free(popup_items);
				free(visible_indices);
				return 0;
			}
			visible_indices = grown_indices;
			cap = new_cap;
		}
		/*
		 * Servers like clangd put a leading space or category marker (e.g. "•") in the
		 * label for ranking; strip those so the popup shows the bare symbol the user is
		 * trying to complete.
		 */
		const char *display = editorAutocompleteStripLeadingNonIdent(
				g_autocomplete.items[i].label);
		if (display == NULL || display[0] == '\0') {
			display = g_autocomplete.items[i].label;
		}
		popup_items[visible_count].label = (char *)display;
		popup_items[visible_count].detail = NULL;
		visible_indices[visible_count] = i;
		visible_count++;
	}

	if (visible_count <= 0) {
		free(popup_items);
		free(visible_indices);
		editorPopupClose();
		free(g_autocomplete.visible_indices);
		g_autocomplete.visible_indices = NULL;
		g_autocomplete.visible_count = 0;
		g_autocomplete.visible_capacity = 0;
		g_autocomplete.active = 0;
		return 0;
	}

	if (!editorPopupOpen(popup_items, visible_count, anchor_row, anchor_col)) {
		free(popup_items);
		free(visible_indices);
		return 0;
	}
	free(popup_items);
	free(g_autocomplete.visible_indices);
	g_autocomplete.visible_indices = visible_indices;
	g_autocomplete.visible_count = visible_count;
	g_autocomplete.visible_capacity = cap;
	g_autocomplete.active = 1;
	return 1;
}

void editorAutocompleteOnCharInserted(int ch) {
	/*
	 * If the popup was closed externally (e.g. via Escape through the popup key handler)
	 * the autocomplete state can drift out of sync — visible() would still return true and
	 * the refilter branch below would resurrect the popup on the next keystroke. Lazily
	 * clear the state so typing after dismissal triggers a fresh request instead.
	 */
	if (g_autocomplete.active && !editorPopupIsVisible()) {
		editorAutocompleteReset();
	}
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

	/*
	 * If the popup is already visible and the user typed an identifier character that just
	 * extends the existing prefix on the same row, refilter the visible items immediately so
	 * the popup narrows without waiting for the network response. A fresh request still goes
	 * out so the server can return more specific items, which will replace the filtered set
	 * when it arrives.
	 */
	if (g_autocomplete.active && g_autocomplete.filename != NULL &&
			E.filename != NULL && strcmp(g_autocomplete.filename, E.filename) == 0 &&
			g_autocomplete.request_cy == E.cy &&
			g_autocomplete.prefix_start_cx == prefix_start_cx &&
			g_autocomplete.prefix != NULL &&
			strncmp(prefix, g_autocomplete.prefix, strlen(g_autocomplete.prefix)) == 0) {
		free(g_autocomplete.prefix);
		g_autocomplete.prefix = strdup(prefix);
		g_autocomplete.request_cx = E.cx;
		(void)editorAutocompleteRefreshFiltered(E.cy, prefix_start_cx);
	}

	int requested = editorLspRequestCompletionAsync(E.filename, E.syntax_language, E.cy, E.cx,
			E.lsp_doc_version, prefix_start_cx, prefix, trigger_kind, trigger_character);
	free(prefix);
	if (!requested) {
		if (!g_autocomplete.active) {
			editorAutocompleteReset();
		}
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
	} else if (item->insert_text != NULL && item->insert_text[0] != '\0') {
		insert_text = item->insert_text;
	} else if (item->filter_text != NULL && item->filter_text[0] != '\0') {
		/* Use filterText (e.g. clangd's bare symbol name) when label has decoration. */
		insert_text = item->filter_text;
	} else {
		insert_text = editorAutocompleteStripLeadingNonIdent(item->label);
		if (insert_text == NULL || insert_text[0] == '\0') {
			insert_text = item->label;
		}
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
	int popup_idx = editorPopupSelectedIndex();
	if (popup_idx < 0 || popup_idx >= g_autocomplete.visible_count) {
		editorAutocompleteReset();
		return 0;
	}
	int raw_idx = g_autocomplete.visible_indices[popup_idx];
	if (raw_idx < 0 || raw_idx >= g_autocomplete.count) {
		editorAutocompleteReset();
		return 0;
	}
	struct editorLspCompletionItem item_copy = {0};
	if (g_autocomplete.items[raw_idx].label != NULL) {
		item_copy.label = strdup(g_autocomplete.items[raw_idx].label);
	}
	if (g_autocomplete.items[raw_idx].filter_text != NULL) {
		item_copy.filter_text = strdup(g_autocomplete.items[raw_idx].filter_text);
	}
	if (g_autocomplete.items[raw_idx].insert_text != NULL) {
		item_copy.insert_text = strdup(g_autocomplete.items[raw_idx].insert_text);
	}
	item_copy.has_text_edit = g_autocomplete.items[raw_idx].has_text_edit;
	item_copy.text_edit_start_line = g_autocomplete.items[raw_idx].text_edit_start_line;
	item_copy.text_edit_start_character = g_autocomplete.items[raw_idx].text_edit_start_character;
	item_copy.text_edit_end_line = g_autocomplete.items[raw_idx].text_edit_end_line;
	item_copy.text_edit_end_character = g_autocomplete.items[raw_idx].text_edit_end_character;
	if (g_autocomplete.items[raw_idx].text_edit_new_text != NULL) {
		item_copy.text_edit_new_text = strdup(g_autocomplete.items[raw_idx].text_edit_new_text);
	}

	int prefix_start_cy = g_autocomplete.request_cy;
	int prefix_start_cx = g_autocomplete.prefix_start_cx;
	int cursor_cx = E.cx;
	editorAutocompleteReset();

	int ok = editorAutocompleteApplyItem(&item_copy, prefix_start_cy, prefix_start_cx, cursor_cx);
	free(item_copy.label);
	free(item_copy.filter_text);
	free(item_copy.insert_text);
	free(item_copy.text_edit_new_text);
	return ok;
}

void editorAutocompleteHandleCompletionResponse(int request_id, int document_version,
		int request_cy, int request_cx, int prefix_start_cx, const char *prefix,
		const char *filename, struct editorLspCompletionItem *items, int count) {
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

	editorAutocompleteReset();

	g_autocomplete.items = items;
	g_autocomplete.count = count;
	g_autocomplete.request_id = request_id;
	g_autocomplete.document_version = document_version;
	g_autocomplete.request_cy = request_cy;
	g_autocomplete.request_cx = request_cx;
	g_autocomplete.prefix_start_cx = prefix_start_cx;
	g_autocomplete.prefix = current_prefix;
	g_autocomplete.filename = strdup(filename);

	if (!editorAutocompleteRefreshFiltered(request_cy, prefix_start_cx)) {
		editorAutocompleteReset();
		return;
	}
}
