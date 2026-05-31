#ifndef ROTIDE_RENDER_POPUP_H
#define ROTIDE_RENDER_POPUP_H

#include "rotide.h"

#define EDITOR_POPUP_MAX_VISIBLE_ROWS 8
#define EDITOR_POPUP_MAX_COLS 60

enum editorPopupKeyResult {
	EDITOR_POPUP_KEY_IGNORED = 0,
	EDITOR_POPUP_KEY_CONSUMED,
	EDITOR_POPUP_KEY_ACCEPTED,
	EDITOR_POPUP_KEY_DISMISSED_PASS_THROUGH
};

int editorPopupOpen(const struct editorPopupItem *items, int count, int anchor_row, int anchor_col);
int editorPopupOpenMenuKind(enum editorPopupKind kind, const struct editorPopupItem *items,
                            int count, int screen_row, int screen_col);
int editorPopupOpenMenu(const struct editorPopupItem *items, int count, int screen_row,
                        int screen_col);
int editorPopupKindIsMenu(enum editorPopupKind kind);
int editorPopupMenuHitTest(int screen_row, int screen_col, int *item_index_out);
void editorPopupClose(void);
int editorPopupIsVisible(void);
int editorPopupSelectedIndex(void);
const char *editorPopupSelectedLabel(void);
int editorPopupItemCount(void);

int editorPopupVisibleRowCount(void);
int editorPopupContentColumns(void);
void editorPopupComputePlacement(int *terminal_row_out, int *terminal_col_out,
                                 int *visible_rows_out, int *cols_out, int *place_above_out);

enum editorPopupKeyResult editorPopupHandleKey(int key);

#endif
