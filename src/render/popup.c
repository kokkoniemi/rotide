#include "render/popup.h"

#include "rotide.h"
#include "workspace/drawer.h"

#include <stdlib.h>
#include <string.h>

static void popupReleaseItems(void) {
	if (E.popup.items == NULL) {
		return;
	}
	for (int i = 0; i < E.popup.item_count; i++) {
		free(E.popup.items[i].label);
		free(E.popup.items[i].detail);
	}
	free(E.popup.items);
	E.popup.items = NULL;
	E.popup.item_count = 0;
}

void editorPopupClose(void) {
	popupReleaseItems();
	E.popup.visible = 0;
	E.popup.kind = EDITOR_POPUP_KIND_AUTOCOMPLETE;
	E.popup.anchor_row = 0;
	E.popup.anchor_col = 0;
	E.popup.selected_index = 0;
	E.popup.row_offset = 0;
}

int editorPopupOpen(const struct editorPopupItem *items, int count, int anchor_row,
                    int anchor_col) {
	return editorPopupOpenKind(EDITOR_POPUP_KIND_AUTOCOMPLETE, items, count, anchor_row,
	                           anchor_col);
}

int editorPopupOpenKind(enum editorPopupKind kind, const struct editorPopupItem *items, int count,
                        int anchor_row, int anchor_col) {
	editorPopupClose();
	if (items == NULL || count <= 0) {
		return 0;
	}

	struct editorPopupItem *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}
	for (int i = 0; i < count; i++) {
		const char *label = items[i].label != NULL ? items[i].label : "";
		copy[i].label = strdup(label);
		if (copy[i].label == NULL) {
			for (int j = 0; j <= i; j++) {
				free(copy[j].label);
				free(copy[j].detail);
			}
			free(copy);
			return 0;
		}
		if (items[i].detail != NULL) {
			copy[i].detail = strdup(items[i].detail);
			if (copy[i].detail == NULL) {
				for (int j = 0; j <= i; j++) {
					free(copy[j].label);
					free(copy[j].detail);
				}
				free(copy);
				return 0;
			}
		}
	}

	E.popup.items = copy;
	E.popup.item_count = count;
	E.popup.kind = kind;
	E.popup.anchor_row = anchor_row;
	E.popup.anchor_col = anchor_col;
	E.popup.selected_index = 0;
	E.popup.row_offset = 0;
	E.popup.visible = 1;
	return 1;
}

int editorPopupOpenMenu(const struct editorPopupItem *items, int count, int screen_row,
                        int screen_col) {
	return editorPopupOpenMenuKind(EDITOR_POPUP_KIND_DRAWER_MENU, items, count, screen_row,
	                               screen_col);
}

int editorPopupOpenMenuKind(enum editorPopupKind kind, const struct editorPopupItem *items,
                            int count, int screen_row, int screen_col) {
	if (!editorPopupKindIsMenu(kind)) {
		return 0;
	}
	if (!editorPopupOpenKind(kind, items, count, screen_row, screen_col)) {
		return 0;
	}
	return 1;
}

int editorPopupKindIsMenu(enum editorPopupKind kind) {
	return kind == EDITOR_POPUP_KIND_DRAWER_MENU ||
	       kind == EDITOR_POPUP_KIND_EDITOR_CONTEXT_MENU ||
	       kind == EDITOR_POPUP_KIND_TAB_CONTEXT_MENU ||
	       kind == EDITOR_POPUP_KIND_LSP_LOCATION_MENU;
}

int editorPopupIsVisible(void) {
	return E.popup.visible && E.popup.item_count > 0;
}

int editorPopupItemCount(void) {
	return editorPopupIsVisible() ? E.popup.item_count : 0;
}

int editorPopupSelectedIndex(void) {
	return editorPopupIsVisible() ? E.popup.selected_index : -1;
}

const char *editorPopupSelectedLabel(void) {
	if (!editorPopupIsVisible()) {
		return NULL;
	}
	if (E.popup.selected_index < 0 || E.popup.selected_index >= E.popup.item_count) {
		return NULL;
	}
	return E.popup.items[E.popup.selected_index].label;
}

int editorPopupVisibleRowCount(void) {
	int count = editorPopupItemCount();
	if (count <= 0) {
		return 0;
	}
	if (count > EDITOR_POPUP_MAX_VISIBLE_ROWS) {
		return EDITOR_POPUP_MAX_VISIBLE_ROWS;
	}
	return count;
}

static int popupLabelDisplayWidth(const char *label) {
	if (label == NULL) {
		return 0;
	}
	int cols = 0;
	for (const unsigned char *p = (const unsigned char *)label; *p != '\0'; p++) {
		if ((*p & 0xC0) == 0x80) {
			continue;
		}
		cols++;
	}
	return cols;
}

int editorPopupContentColumns(void) {
	if (!editorPopupIsVisible()) {
		return 0;
	}
	int max_cols = 0;
	for (int i = 0; i < E.popup.item_count; i++) {
		int width = popupLabelDisplayWidth(E.popup.items[i].label);
		if (width > max_cols) {
			max_cols = width;
		}
	}
	int padded = max_cols + 2;
	if (padded > EDITOR_POPUP_MAX_COLS) {
		padded = EDITOR_POPUP_MAX_COLS;
	}
	if (padded < 4) {
		padded = 4;
	}
	return padded;
}

static void popupClampScroll(void) {
	int rows = editorPopupVisibleRowCount();
	if (rows <= 0) {
		E.popup.row_offset = 0;
		return;
	}
	int max_offset = E.popup.item_count - rows;
	if (max_offset < 0) {
		max_offset = 0;
	}
	if (E.popup.selected_index < E.popup.row_offset) {
		E.popup.row_offset = E.popup.selected_index;
	} else if (E.popup.selected_index >= E.popup.row_offset + rows) {
		E.popup.row_offset = E.popup.selected_index - rows + 1;
	}
	if (E.popup.row_offset < 0) {
		E.popup.row_offset = 0;
	}
	if (E.popup.row_offset > max_offset) {
		E.popup.row_offset = max_offset;
	}
}

static void popupClampTextScroll(void) {
	int rows = editorPopupVisibleRowCount();
	if (rows <= 0 || E.popup.item_count <= rows) {
		E.popup.row_offset = 0;
		return;
	}
	int max_offset = E.popup.item_count - rows;
	if (E.popup.row_offset < 0) {
		E.popup.row_offset = 0;
	}
	if (E.popup.row_offset > max_offset) {
		E.popup.row_offset = max_offset;
	}
}

enum editorPopupKeyResult editorPopupHandleKey(int key) {
	if (!editorPopupIsVisible()) {
		return EDITOR_POPUP_KEY_IGNORED;
	}

	if (E.popup.kind == EDITOR_POPUP_KIND_LSP_HOVER) {
		switch (key) {
			case ARROW_UP:
				E.popup.row_offset--;
				popupClampTextScroll();
				return EDITOR_POPUP_KEY_CONSUMED;
			case ARROW_DOWN:
				E.popup.row_offset++;
				popupClampTextScroll();
				return EDITOR_POPUP_KEY_CONSUMED;
			case PAGE_UP:
				E.popup.row_offset -= EDITOR_POPUP_MAX_VISIBLE_ROWS;
				popupClampTextScroll();
				return EDITOR_POPUP_KEY_CONSUMED;
			case PAGE_DOWN:
				E.popup.row_offset += EDITOR_POPUP_MAX_VISIBLE_ROWS;
				popupClampTextScroll();
				return EDITOR_POPUP_KEY_CONSUMED;
			case '\x1b':
				editorPopupClose();
				return EDITOR_POPUP_KEY_CONSUMED;
			default:
				editorPopupClose();
				return EDITOR_POPUP_KEY_DISMISSED_PASS_THROUGH;
		}
	}

	switch (key) {
		case ARROW_UP:
			if (E.popup.selected_index > 0) {
				E.popup.selected_index--;
			}
			popupClampScroll();
			return EDITOR_POPUP_KEY_CONSUMED;
		case ARROW_DOWN:
			if (E.popup.selected_index + 1 < E.popup.item_count) {
				E.popup.selected_index++;
			}
			popupClampScroll();
			return EDITOR_POPUP_KEY_CONSUMED;
		case '\r':
			return EDITOR_POPUP_KEY_ACCEPTED;
		case '\x1b':
			editorPopupClose();
			return EDITOR_POPUP_KEY_CONSUMED;
		default:
			editorPopupClose();
			return EDITOR_POPUP_KEY_DISMISSED_PASS_THROUGH;
	}
}

void editorPopupComputePlacement(int *terminal_row_out, int *terminal_col_out,
                                 int *visible_rows_out, int *cols_out, int *place_above_out) {
	int rows = editorPopupVisibleRowCount();
	int cols = editorPopupContentColumns();
	int place_above = 0;

	if (editorPopupKindIsMenu(E.popup.kind)) {
		int anchor_row = E.popup.anchor_row;
		int anchor_col = E.popup.anchor_col;
		int max_row = E.window_rows + 1;
		int top = anchor_row + 1;
		if (rows > 0 && top + rows - 1 > max_row && anchor_row - rows >= 1) {
			place_above = 1;
			top = anchor_row - rows;
		}
		if (top < 1) {
			top = 1;
		}
		if (rows > 0 && top + rows - 1 > max_row) {
			rows = max_row - top + 1;
			if (rows < 0) {
				rows = 0;
			}
		}
		int col = anchor_col;
		if (cols > 0 && col + cols - 1 > E.window_cols) {
			col = E.window_cols - cols + 1;
		}
		if (col < 1) {
			col = 1;
		}
		if (terminal_row_out != NULL) {
			*terminal_row_out = top;
		}
		if (terminal_col_out != NULL) {
			*terminal_col_out = col;
		}
		if (visible_rows_out != NULL) {
			*visible_rows_out = rows;
		}
		if (cols_out != NULL) {
			*cols_out = cols;
		}
		if (place_above_out != NULL) {
			*place_above_out = place_above;
		}
		return;
	}

	int anchor_screen_row = E.popup.anchor_row - E.rowoff;
	int below_row = anchor_screen_row + 1;
	int above_row_top = anchor_screen_row - rows;
	if (below_row + rows > E.window_rows && above_row_top >= 0) {
		place_above = 1;
	}

	int top_row;
	if (place_above) {
		top_row = above_row_top;
		if (top_row < 0) {
			top_row = 0;
		}
	} else {
		top_row = below_row;
		if (top_row + rows > E.window_rows) {
			rows = E.window_rows - top_row;
			if (rows < 0) {
				rows = 0;
			}
		}
	}

	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	int text_start_col = editorTextBodyStartColForCols(E.window_cols);
	int anchor_screen_col = text_start_col + gutter_cols + (E.popup.anchor_col - E.coloff);
	int max_col_exclusive = E.window_cols;
	if (anchor_screen_col + cols > max_col_exclusive) {
		anchor_screen_col = max_col_exclusive - cols;
	}
	if (anchor_screen_col < text_start_col) {
		anchor_screen_col = text_start_col;
	}

	if (terminal_row_out != NULL) {
		*terminal_row_out =
		        top_row + 2; // +2 to take acccount for 1-based indexing and the tab line
	}
	if (terminal_col_out != NULL) {
		*terminal_col_out = anchor_screen_col + 1;
	}
	if (visible_rows_out != NULL) {
		*visible_rows_out = rows;
	}
	if (cols_out != NULL) {
		*cols_out = cols;
	}
	if (place_above_out != NULL) {
		*place_above_out = place_above;
	}
}

int editorPopupMenuHitTest(int screen_row, int screen_col, int *item_index_out) {
	if (!editorPopupIsVisible() || !editorPopupKindIsMenu(E.popup.kind)) {
		return 0;
	}
	int terminal_row = 0;
	int terminal_col = 0;
	int rows = 0;
	int cols = 0;
	int place_above = 0;
	editorPopupComputePlacement(&terminal_row, &terminal_col, &rows, &cols, &place_above);
	if (rows <= 0 || cols <= 0) {
		return 0;
	}
	if (screen_row < terminal_row || screen_row >= terminal_row + rows) {
		return 0;
	}
	if (screen_col < terminal_col || screen_col >= terminal_col + cols) {
		return 0;
	}
	int item = E.popup.row_offset + (screen_row - terminal_row);
	if (item < 0 || item >= E.popup.item_count) {
		return 0;
	}
	if (item_index_out != NULL) {
		*item_index_out = item;
	}
	return 1;
}
