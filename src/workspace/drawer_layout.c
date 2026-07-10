#include "rotide.h"
#include "workspace/drawer.h"

#include <stddef.h>

static int drawerLayoutClampWidthForCols(int desired_width, int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	if (desired_width < 1) {
		desired_width = 1;
	}

	int max_drawer = total_cols - 2;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	if (desired_width > max_drawer) {
		desired_width = max_drawer;
	}

	return desired_width;
}

static int drawerLayoutDefaultMaxWidthForCols(int total_cols) {
	if (total_cols <= 1) {
		return 0;
	}
	if (total_cols == 2) {
		return 1;
	}

	int min_text_cols = total_cols / 2;
	if (min_text_cols < 1) {
		min_text_cols = 1;
	}
	int max_drawer = total_cols - 1 - min_text_cols;
	if (max_drawer < 1) {
		max_drawer = 1;
	}
	return max_drawer;
}

int editorDrawerCollapsedToggleWidthForCols(int total_cols) {
	if (total_cols <= 0) {
		return 0;
	}
	if (total_cols < ROTIDE_DRAWER_COLLAPSED_WIDTH) {
		return total_cols;
	}
	return ROTIDE_DRAWER_COLLAPSED_WIDTH;
}

int editorDrawerWidthForCols(int total_cols) {
	if (editorDrawerIsCollapsed()) {
		return editorDrawerCollapsedToggleWidthForCols(total_cols);
	}

	int desired_width = E.drawer_width_cols;
	if (desired_width <= 0) {
		desired_width = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}

	int width = drawerLayoutClampWidthForCols(desired_width, total_cols);
	if (!E.drawer_width_user_set) {
		int default_max = drawerLayoutDefaultMaxWidthForCols(total_cols);
		if (width > default_max) {
			width = default_max;
		}
	}
	return width;
}

int editorDrawerSeparatorWidthForCols(int total_cols) {
	if (editorDrawerIsCollapsed()) {
		return 0;
	}
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	if (drawer_cols <= 0) {
		return 0;
	}
	return total_cols - drawer_cols >= 2 ? 1 : 0;
}

int editorDrawerTextStartColForCols(int total_cols) {
	int drawer_cols = editorDrawerWidthForCols(total_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(total_cols);
	return drawer_cols + separator_cols;
}

int editorDrawerTextViewportCols(int total_cols) {
	if (total_cols <= 1) {
		return 1;
	}
	int text_cols = total_cols - editorDrawerTextStartColForCols(total_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}
	return text_cols;
}

static int drawerLayoutLineNumberDigitCols(void) {
	int rows = E.numrows > 0 ? E.numrows : 1;
	if (E.tab_kind == EDITOR_TAB_GIT_DIFF && E.git_view_line_numbers != NULL) {
		rows = 1;
		for (int i = 0; i < E.git_view_line_kind_count; i++) {
			if (E.git_view_line_numbers[i] > rows) {
				rows = E.git_view_line_numbers[i];
			}
		}
	}
	int digits = 1;
	while (rows >= 10) {
		rows /= 10;
		digits++;
	}
	return digits;
}

int editorLineNumberGutterColsForCols(int total_cols) {
	if (!E.line_numbers_enabled) {
		return 0;
	}

	int text_cols = editorDrawerTextViewportCols(total_cols);
	if (text_cols <= 1) {
		return 0;
	}

	int gutter_cols = drawerLayoutLineNumberDigitCols() + 1;
	if (gutter_cols >= text_cols) {
		gutter_cols = text_cols - 1;
	}
	return gutter_cols;
}

int editorTextBodyStartColForCols(int total_cols) {
	int text_start = editorDrawerTextStartColForCols(total_cols);
	int text_cols = editorDrawerTextViewportCols(total_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(total_cols);
	text_start += gutter_cols;
	text_cols -= gutter_cols;
	if (text_cols >= 3) {
		return text_start + 1;
	}
	return text_start;
}

int editorTextBodyViewportCols(int total_cols) {
	int text_cols = editorDrawerTextViewportCols(total_cols);
	text_cols -= editorLineNumberGutterColsForCols(total_cols);
	if (text_cols < 1) {
		text_cols = 1;
	}
	if (text_cols >= 3) {
		return text_cols - 2;
	}
	return text_cols;
}

int editorDrawerSetWidthForCols(int width, int total_cols) {
	int clamped = drawerLayoutClampWidthForCols(width, total_cols);
	E.drawer_width_user_set = 1;
	if (E.drawer_width_cols == clamped) {
		return 0;
	}
	E.drawer_width_cols = clamped;
	return 1;
}

int editorDrawerResizeByDeltaForCols(int delta, int total_cols) {
	int current = editorDrawerIsCollapsed() ? E.drawer_width_cols
	                                        : editorDrawerWidthForCols(total_cols);
	if (current <= 0) {
		current = ROTIDE_DRAWER_DEFAULT_WIDTH;
	}
	return editorDrawerSetWidthForCols(current + delta, total_cols);
}
