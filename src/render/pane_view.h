#ifndef RENDER_PANE_VIEW_H
#define RENDER_PANE_VIEW_H

#include "render/write_buf.h"
#include "workspace/layout.h"

int editorDrawMultiPaneRows(struct writeBuf *wb,
		const struct editorLeafLayout *layout,
		const struct editorBorderList *borders,
		struct editorRect focused_rect);

#endif
