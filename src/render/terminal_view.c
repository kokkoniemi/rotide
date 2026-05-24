#include "render/terminal_view.h"

#include "render/ansi_style.h"
#include "text/utf8.h"
#include "vterm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int terminalViewAppendColorSgr(struct writeBuf *wb, const VTermColor *color, int is_fg) {
	char esc[32];
	int n;
	if (is_fg ? VTERM_COLOR_IS_DEFAULT_FG(color) : VTERM_COLOR_IS_DEFAULT_BG(color)) {
		struct editorThemeColor theme_color =
		        E.theme.ui[is_fg ? EDITOR_THEME_UI_FOREGROUND : EDITOR_THEME_UI_BACKGROUND];
		if (!editorThemeColorIsDefault(theme_color)) {
			return is_fg ? editorAppendThemeForeground(wb, theme_color)
			             : editorAppendThemeBackground(wb, theme_color);
		}
		n = snprintf(esc, sizeof(esc), "\x1b[%dm", is_fg ? 39 : 49);
	} else if (VTERM_COLOR_IS_RGB(color)) {
		n = snprintf(esc, sizeof(esc), "\x1b[%d;2;%u;%u;%um", is_fg ? 38 : 48,
		             (unsigned)color->rgb.red, (unsigned)color->rgb.green,
		             (unsigned)color->rgb.blue);
	} else if (VTERM_COLOR_IS_INDEXED(color)) {
		unsigned idx = color->indexed.idx;
		if (idx < 8) {
			n = snprintf(esc, sizeof(esc), "\x1b[%um",
			             (unsigned)((is_fg ? 30 : 40) + idx));
		} else if (idx < 16) {
			n = snprintf(esc, sizeof(esc), "\x1b[%um",
			             (unsigned)((is_fg ? 90 : 100) + (idx - 8)));
		} else {
			n = snprintf(esc, sizeof(esc), "\x1b[%d;5;%um", is_fg ? 38 : 48, idx);
		}
	} else {
		return 1;
	}
	if (n <= 0 || n >= (int)sizeof(esc)) {
		return 0;
	}
	return wbAppend(wb, esc, (size_t)n);
}

static int terminalViewCellIsPlain(const VTermScreenCell *cell) {
	if (cell->attrs.bold || cell->attrs.italic ||
	    cell->attrs.underline != VTERM_UNDERLINE_OFF || cell->attrs.blink ||
	    cell->attrs.reverse || cell->attrs.conceal || cell->attrs.strike) {
		return 0;
	}
	return VTERM_COLOR_IS_DEFAULT_FG(&cell->fg) && VTERM_COLOR_IS_DEFAULT_BG(&cell->bg);
}

static int terminalViewDrawCellAttrs(struct writeBuf *wb, const VTermScreenCell *cell) {
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	if (terminalViewCellIsPlain(cell)) {
		return 1;
	}
	if (cell->attrs.bold && !wbAppend(wb, "\x1b[1m", 4)) {
		return 0;
	}
	if (cell->attrs.italic && !wbAppend(wb, "\x1b[3m", 4)) {
		return 0;
	}
	if (cell->attrs.underline != VTERM_UNDERLINE_OFF && !wbAppend(wb, "\x1b[4m", 4)) {
		return 0;
	}
	if (cell->attrs.blink && !wbAppend(wb, "\x1b[5m", 4)) {
		return 0;
	}
	if (cell->attrs.reverse && !wbAppend(wb, "\x1b[7m", 4)) {
		return 0;
	}
	if (cell->attrs.conceal && !wbAppend(wb, "\x1b[8m", 4)) {
		return 0;
	}
	if (cell->attrs.strike && !wbAppend(wb, "\x1b[9m", 4)) {
		return 0;
	}
	if (!terminalViewAppendColorSgr(wb, &cell->fg, 1) ||
	    !terminalViewAppendColorSgr(wb, &cell->bg, 0)) {
		return 0;
	}
	return 1;
}

static int terminalViewDrawExitStatusRow(struct writeBuf *wb,
                                         const struct editorTerminalPane *terminal, int col_in_pane,
                                         int slice_cols) {
	char banner[128];
	int n;
	if (WIFEXITED(terminal->exit_status)) {
		n = snprintf(banner, sizeof(banner), "[exited: status %d]",
		             WEXITSTATUS(terminal->exit_status));
	} else if (WIFSIGNALED(terminal->exit_status)) {
		n = snprintf(banner, sizeof(banner), "[exited: signal %d]",
		             WTERMSIG(terminal->exit_status));
	} else {
		n = snprintf(banner, sizeof(banner), "[exited]");
	}
	if (n < 0) {
		return 0;
	}
	int banner_len = n < (int)sizeof(banner) ? n : (int)sizeof(banner) - 1;
	if (!wbAppend(wb, "\x1b[7m", 4)) {
		return 0;
	}
	int emitted = 0;
	for (int i = 0; emitted < slice_cols && i < banner_len; i++) {
		if (i < col_in_pane) {
			continue;
		}
		if (!wbAppend(wb, &banner[i], 1)) {
			return 0;
		}
		emitted++;
	}
	while (emitted < slice_cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		emitted++;
	}
	return editorAppendThemeReset(wb);
}

int editorDrawTerminalCells(struct writeBuf *wb, struct editorTerminalPane *terminal,
                            int row_in_pane, int col_in_pane, int slice_cols) {
	if (terminal == NULL || terminal->screen == NULL || slice_cols <= 0) {
		return 1;
	}
	if (terminal->exited && terminal->rows > 0 && row_in_pane == terminal->rows - 1) {
		return terminalViewDrawExitStatusRow(wb, terminal, col_in_pane, slice_cols);
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	/* Translate pane-local row into the pane's log-row coordinate so we can
	 * pull from scrollback when the user has scrolled up, and so selection
	 * checks (which use log-row) line up. */
	int log_row = row_in_pane - terminal->scroll_offset;
	int row_cols = terminal->cols;
	if (row_cols <= 0) {
		row_cols = slice_cols;
	}
	VTermScreenCell *row_buf = NULL;
	int have_row = 0;
	if (row_cols > 0) {
		/* Pane-scoped scratch — avoids a malloc/free per drawn row per frame. */
		row_buf = editorTerminalPaneEnsureRenderRowScratch(terminal, row_cols);
		if (row_buf != NULL) {
			memset(row_buf, 0, (size_t)row_cols * sizeof(*row_buf));
			have_row = editorTerminalPaneGetLogRow(terminal, log_row, row_buf);
		}
	}
	int emitted = 0;
	int col = col_in_pane;
	int last_was_plain = 0;
	int any_styled_emitted = 0;
	int last_was_selected = 0;
	while (emitted < slice_cols) {
		VTermScreenCell cell;
		int in_bounds = have_row && col >= 0 && col < row_cols;
		if (!in_bounds) {
			memset(&cell, 0, sizeof(cell));
		} else {
			cell = row_buf[col];
		}
		int cell_width = cell.width;
		if (cell_width < 1) {
			cell_width = 1;
		}
		if (emitted + cell_width > slice_cols) {
			while (emitted < slice_cols) {
				if (!wbAppend(wb, " ", 1)) {
					return 0;
				}
				emitted++;
			}
			break;
		}
		int selected = in_bounds &&
		               editorTerminalPaneSelectionContains(terminal, log_row, col);
		int plain = terminalViewCellIsPlain(&cell);
		if (plain) {
			if ((any_styled_emitted && !last_was_plain) || last_was_selected != selected) {
				if (!editorAppendThemeReset(wb)) {
					return 0;
				}
				last_was_plain = 0;
				any_styled_emitted = 0;
			}
		} else {
			if (!terminalViewDrawCellAttrs(wb, &cell)) {
				return 0;
			}
			any_styled_emitted = 1;
		}
		if (selected && !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
			return 0;
		}
		last_was_plain = plain && !selected;
		last_was_selected = selected;
		if (!in_bounds || cell.chars[0] == 0) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		} else {
			for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
				char utf8[4];
				int n = editorUtf8EncodeCodepoint(cell.chars[i], utf8);
				if (n > 0 && !wbAppend(wb, utf8, (size_t)n)) {
					return 0;
				}
			}
		}
		emitted += cell_width;
		col += cell_width;
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	return 1;
}
