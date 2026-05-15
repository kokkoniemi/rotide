#include "render/tab_bar.h"

#include "render/ansi_style.h"
#include "render/display_text.h"
#include "render/drawer_view.h"
#include "workspace/drawer.h"
#include "workspace/tabs.h"
#include <string.h>

#define VT100_ITALIC_ON_4 "\x1b[3m"
#define VT100_ITALIC_OFF_5 "\x1b[23m"
#define VT100_CLEAR_ROW_3 "\x1b[K"

static const char *editorTabLabelFromDisplayName(const char *display_name) {
	if (display_name == NULL) {
		return "[No Name]";
	}
	const char *slash = strrchr(display_name, '/');
	if (slash != NULL && slash[1] != '\0') {
		return slash + 1;
	}
	return display_name;
}

int editorDrawTabSlots(struct writeBuf *wb, int cols) {
	if (cols <= 0) {
		return 1;
	}

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	if (!editorTabBuildLayoutForWidth(cols, layout, ROTIDE_MAX_TABS, &layout_count)) {
		return 0;
	}

	int active = editorTabActiveIndex();
	int drawn_cols = 0;
	for (int i = 0; i < layout_count; i++) {
		const struct editorTabLayoutEntry *entry = &layout[i];
		int tab_idx = entry->tab_idx;
		int slot_width = entry->width_cols;
		if (slot_width <= 0) {
			continue;
		}
		int is_active = tab_idx == active;
		if (is_active && !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_TAB_ACTIVE)) {
			return 0;
		}

		int content_width = slot_width;
		if (entry->show_right_overflow && content_width > 0) {
			content_width--;
		}

		int slot_cols = 0;
		char marker = entry->show_left_overflow ? '<' : ' ';
		if (slot_cols < content_width && !wbAppend(wb, &marker, 1)) {
			return 0;
		}
		if (slot_cols < content_width) {
			slot_cols++;
		}

		char dirty = ' ';
		if (editorTabDirtyAt(tab_idx)) {
			dirty = '*';
		}
		if (slot_cols < content_width && !wbAppend(wb, &dirty, 1)) {
			return 0;
		}
		if (slot_cols < content_width) {
			slot_cols++;
		}

		if (slot_cols < content_width && !wbAppend(wb, " ", 1)) {
			return 0;
		}
		if (slot_cols < content_width) {
			slot_cols++;
		}

		if (slot_cols < content_width) {
			const char *label = editorTabLabelFromDisplayName(editorTabDisplayNameAt(tab_idx));
			int is_preview = editorTabIsPreviewAt(tab_idx);
			int right_pad_cols = 3;
			int label_cols = content_width - slot_cols - right_pad_cols;
			if (label_cols < 0) {
				label_cols = 0;
			}
			int written = 0;
			if (is_preview && !wbAppend(wb, VT100_ITALIC_ON_4, 4)) {
				return 0;
			}
			if (!editorAppendSanitizedMiddleTruncated(wb, label, label_cols, &written)) {
				return 0;
			}
			if (is_preview && !wbAppend(wb, VT100_ITALIC_OFF_5, 5)) {
				return 0;
			}
			slot_cols += written;

			while (right_pad_cols > 0 && slot_cols < content_width) {
				if (!wbAppend(wb, " ", 1)) {
					return 0;
				}
				slot_cols++;
				right_pad_cols--;
			}
		}

		while (slot_cols < content_width) {
			char pad = ' ';
			if (!wbAppend(wb, &pad, 1)) {
				return 0;
			}
			slot_cols++;
		}
		if (entry->show_right_overflow) {
			char overflow = '>';
			if (!wbAppend(wb, &overflow, 1)) {
				return 0;
			}
			slot_cols++;
		}

		if (is_active && !editorAppendThemeReset(wb)) {
			return 0;
		}

		drawn_cols += slot_width;
	}

	while (drawn_cols < cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		drawn_cols++;
	}

	return 1;
}

int editorDrawTabBar(struct writeBuf *wb) {
	if (E.window_cols <= 0) {
		return wbAppend(wb, "\r\n", 2);
	}

	if (editorDrawerIsCollapsed()) {
		int toggle_cols = editorDrawerCollapsedToggleWidthForCols(E.window_cols);
		if (!editorDrawDrawerRow(wb, 0, toggle_cols)) {
			return 0;
		}
		if (!editorDrawTabSlots(wb, E.window_cols - toggle_cols)) {
			return 0;
		}
		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			return 0;
		}
		return wbAppend(wb, "\r\n", 2);
	}

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	if (!editorDrawDrawerRow(wb, 0, drawer_cols)) {
		return 0;
	}
	if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
		return 0;
	}
	if (!editorDrawTabSlots(wb, text_cols)) {
		return 0;
	}

	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	return wbAppend(wb, "\r\n", 2);
}
