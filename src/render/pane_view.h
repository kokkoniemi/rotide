#ifndef ROTIDE_RENDER_PANE_VIEW_H
#define ROTIDE_RENDER_PANE_VIEW_H

#include "render/write_buf.h"
#include "workspace/layout.h"

struct editorRowSyntaxSpan;

struct editorViewSnapshot {
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int wrapoff;
	size_t cursor_offset;
	int viewport_mode;
	int selection_mode_active;
	size_t selection_anchor_offset;
	int column_select_active;
	int column_select_anchor_cy;
	int column_select_anchor_rx;
	int column_select_cursor_rx;
};

int editorPaneSyntaxRowOverrideCopy(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
                                    int *span_count_out);
int editorPaneActiveBracketColsForRow(int row_idx, int out_cols[2], int *count_out);
void editorPaneSingleBracketMatchUpdate(void);
void editorPaneSingleBracketMatchClear(void);
int editorPaneWrapBodyColsOverride(void);

void editorViewSnapshotCapture(struct editorViewSnapshot *snap);
void editorViewSnapshotRestore(const struct editorViewSnapshot *snap);
void editorViewSnapshotFromPaneView(const struct editorPaneView *view);

int editorDrawFocusedPaneSlice(struct writeBuf *wb, const struct editorPaneNode *leaf,
                               int body_row_in_pane, int slice_cols);
int editorBuildSinglePaneRowLine(struct writeBuf *wb, int y, int drawer_cols, int separator_cols,
                                 int text_cols);
int editorDrawMultiPaneTabStripRow(struct writeBuf *wb);
int editorDrawMultiPaneRows(struct writeBuf *wb, const struct editorLeafLayout *layout,
                            const struct editorBorderList *borders, struct editorRect focused_rect);

/* If (screen_col, screen_row) lands on a Debug Console panel tab recorded by the
 * last render, writes the tab index (0 = Terminal, 1 = Debug Console) to
 * *tab_out and returns 1; otherwise returns 0. */
int editorDapPanelTabAt(int screen_col, int screen_row, int *tab_out);

#endif
