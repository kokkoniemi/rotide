#include "render/terminal_view.h"

#include "config/theme_config.h"
#include "render/ansi_style.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "text/utf8.h"
#include "vterm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h> // NOLINT(misc-include-cleaner) — provides WIFEXITED/WEXITSTATUS/WTERMSIG, which clang-tidy maps to <bits/waitstatus.h>

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
		if (idx < 16) {
			struct editorThemeColor resolved = editorThemeResolveAnsi(idx, is_fg);
			if (!editorThemeColorIsDefault(resolved)) {
				return is_fg ? editorAppendThemeForeground(wb, resolved)
				             : editorAppendThemeBackground(wb, resolved);
			}
			/* Both the palette slot and the fallback fg/bg are
			 * default — emit the raw ANSI code so the host
			 * terminal supplies a color. */
			if (idx < 8) {
				n = snprintf(esc, sizeof(esc), "\x1b[%um",
				             (unsigned)((is_fg ? 30 : 40) + idx));
			} else {
				n = snprintf(esc, sizeof(esc), "\x1b[%um",
				             (unsigned)((is_fg ? 90 : 100) + (idx - 8)));
			}
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

static int terminalViewTryAdvanceCleanRow(struct writeBuf *wb,
                                          const struct editorTerminalPane *terminal,
                                          int row_in_pane, int slice_cols, int *advanced_out) {
	*advanced_out = 0;
	if (terminal->row_dirty == NULL || row_in_pane < 0 ||
	    row_in_pane >= terminal->row_dirty_cap || terminal->row_dirty[row_in_pane]) {
		return 1;
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	char esc[16];
	int n = snprintf(esc, sizeof(esc), "\x1b[%dC", slice_cols);
	if (n <= 0 || n >= (int)sizeof(esc)) {
		return 1;
	}
	if (!wbAppend(wb, esc, (size_t)n)) {
		return 0;
	}
	*advanced_out = 1;
	return 1;
}

static void terminalViewMarkRowClean(struct editorTerminalPane *terminal, int row_in_pane) {
	if (terminal->row_dirty != NULL && row_in_pane >= 0 &&
	    row_in_pane < terminal->row_dirty_cap) {
		terminal->row_dirty[row_in_pane] = 0;
	}
}

struct terminalViewRowCtx {
	int log_row;
	int row_cols;
	int have_row;
	VTermScreenCell *row_buf;
};

static struct terminalViewRowCtx terminalViewBuildRowCtx(struct editorTerminalPane *terminal,
                                                         int row_in_pane, int slice_cols) {
	struct terminalViewRowCtx ctx = {
	        .log_row = row_in_pane - terminal->scroll_offset,
	        .row_cols = terminal->cols,
	        .have_row = 0,
	        .row_buf = NULL,
	};
	if (ctx.row_cols <= 0) {
		ctx.row_cols = slice_cols;
	}
	if (ctx.row_cols <= 0) {
		return ctx;
	}
	ctx.row_buf = editorTerminalPaneEnsureRenderRowScratch(terminal, ctx.row_cols);
	if (ctx.row_buf == NULL) {
		return ctx;
	}
	memset(ctx.row_buf, 0, (size_t)ctx.row_cols * sizeof(*ctx.row_buf));
	ctx.have_row = editorTerminalPaneGetLogRow(terminal, ctx.log_row, ctx.row_buf);
	return ctx;
}

static int terminalViewEmitSpacesToSlice(struct writeBuf *wb, int *emitted, int slice_cols) {
	while (*emitted < slice_cols) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		(*emitted)++;
	}
	return 1;
}

static int terminalViewEmitCellText(struct writeBuf *wb, int in_bounds,
                                    const VTermScreenCell *cell) {
	if (!in_bounds || cell->chars[0] == 0) {
		return wbAppend(wb, " ", 1);
	}
	for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell->chars[i] != 0; i++) {
		char utf8[4];
		int n = editorUtf8EncodeCodepoint(cell->chars[i], utf8);
		if (n > 0 && !wbAppend(wb, utf8, (size_t)n)) {
			return 0;
		}
	}
	return 1;
}

static int terminalViewDrawRowCells(struct writeBuf *wb, struct editorTerminalPane *terminal,
                                    const struct terminalViewRowCtx *ctx, int col_in_pane,
                                    int slice_cols) {
	int emitted = 0;
	int col = col_in_pane;
	int last_was_plain = 0;
	int any_styled_emitted = 0;
	int last_was_selected = 0;
	while (emitted < slice_cols) {
		VTermScreenCell cell;
		int in_bounds = ctx->have_row && col >= 0 && col < ctx->row_cols;
		if (!in_bounds) {
			memset(&cell, 0, sizeof(cell));
		} else {
			cell = ctx->row_buf[col];
		}
		int cell_width = cell.width;
		if (cell_width < 1) {
			cell_width = 1;
		}
		if (emitted + cell_width > slice_cols) {
			return terminalViewEmitSpacesToSlice(wb, &emitted, slice_cols);
		}

		int selected = in_bounds &&
		               editorTerminalPaneSelectionContains(terminal, ctx->log_row, col);
		int plain = terminalViewCellIsPlain(&cell);
		if (plain) {
			if ((any_styled_emitted && !last_was_plain) ||
			    last_was_selected != selected) {
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
		if (!terminalViewEmitCellText(wb, in_bounds, &cell)) {
			return 0;
		}
		emitted += cell_width;
		col += cell_width;
	}
	return 1;
}

int editorDrawTerminalCells(struct writeBuf *wb, struct editorTerminalPane *terminal,
                            int row_in_pane, int col_in_pane, int slice_cols) {
	if (terminal == NULL || terminal->screen == NULL || slice_cols <= 0) {
		return 1;
	}
	if (terminal->exited && terminal->rows > 0 && row_in_pane == terminal->rows - 1) {
		int advanced = 0;
		if (!terminalViewTryAdvanceCleanRow(wb, terminal, row_in_pane, slice_cols,
		                                    &advanced)) {
			return 0;
		}
		if (advanced) {
			return 1;
		}
		int ok = terminalViewDrawExitStatusRow(wb, terminal, col_in_pane, slice_cols);
		if (ok) {
			terminalViewMarkRowClean(terminal, row_in_pane);
		}
		return ok;
	}
	int advanced = 0;
	if (!terminalViewTryAdvanceCleanRow(wb, terminal, row_in_pane, slice_cols, &advanced)) {
		return 0;
	}
	if (advanced) {
		return 1;
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	terminalViewMarkRowClean(terminal, row_in_pane);
	struct terminalViewRowCtx row_ctx =
	        terminalViewBuildRowCtx(terminal, row_in_pane, slice_cols);
	if (!terminalViewDrawRowCells(wb, terminal, &row_ctx, col_in_pane, slice_cols)) {
		return 0;
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	return 1;
}
