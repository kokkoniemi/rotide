#ifndef RENDER_PANE_VIEW_H
#define RENDER_PANE_VIEW_H

#include "render/write_buf.h"
#include "workspace/layout.h"

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

void editorViewSnapshotCapture(struct editorViewSnapshot *snap);
void editorViewSnapshotRestore(const struct editorViewSnapshot *snap);
void editorViewSnapshotFromPaneView(const struct editorPaneView *view);

int editorDrawMultiPaneRows(struct writeBuf *wb,
		const struct editorLeafLayout *layout,
		const struct editorBorderList *borders,
		struct editorRect focused_rect);

#endif
