#ifndef RENDER_PANE_VIEW_H
#define RENDER_PANE_VIEW_H

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
int editorPaneWrapBodyColsOverride(void);

void editorViewSnapshotCapture(struct editorViewSnapshot *snap);
void editorViewSnapshotRestore(const struct editorViewSnapshot *snap);
void editorViewSnapshotFromPaneView(const struct editorPaneView *view);

int editorDrawFocusedPaneSlice(struct writeBuf *wb, const struct editorPaneNode *leaf,
                               int body_row_in_pane, int slice_cols);
int editorBuildSinglePaneRowLine(struct writeBuf *wb, int y, int drawer_cols, int separator_cols,
                                 int text_cols);
int editorDrawMultiPaneRows(struct writeBuf *wb, const struct editorLeafLayout *layout,
                            const struct editorBorderList *borders, struct editorRect focused_rect);

#endif
