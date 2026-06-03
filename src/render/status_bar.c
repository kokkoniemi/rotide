#include "render/status_bar.h"

#include "config/theme_config.h"
#include "render/ansi_style.h"
#include "render/display_text.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VT100_CLEAR_ROW_3 "\x1b[K"
#define VT100_BOLD_ON "\x1b[1m"
#define VT100_BOLD_OFF "\x1b[22m"

/*
 * Clickable debug-control buttons recorded during the most recent status-bar
 * render. Columns are 0-based offsets within the status row. The mouse layer
 * maps a left-press column to the button's action; see
 * editorStatusBarDebugButtonAt.
 */
#define STATUS_DEBUG_MAX_BUTTONS 6
struct statusDebugButton {
	int start_col;
	int end_col;
	enum editorAction action;
};
static struct statusDebugButton g_status_debug_buttons[STATUS_DEBUG_MAX_BUTTONS];
static int g_status_debug_button_count;

static void statusDebugButtonsReset(void) {
	g_status_debug_button_count = 0;
}

/*
 * Appends "<label>  " at *col (capped at max_col), records the clickable span,
 * and advances *col. A button with no room is silently dropped. Returns 0 only
 * on a write failure.
 */
static int statusDebugButton(struct writeBuf *wb, int *col, int max_col, const char *label,
                             enum editorAction action) {
	int len = (int)strlen(label);
	if (*col + len > max_col) {
		return 1;
	}
	if (!wbAppend(wb, label, (size_t)len)) {
		return 0;
	}
	if (g_status_debug_button_count < STATUS_DEBUG_MAX_BUTTONS) {
		g_status_debug_buttons[g_status_debug_button_count].start_col = *col;
		g_status_debug_buttons[g_status_debug_button_count].end_col = *col + len;
		g_status_debug_buttons[g_status_debug_button_count].action = action;
		g_status_debug_button_count++;
	}
	*col += len;
	if (*col + 2 <= max_col && !wbAppend(wb, "  ", 2)) {
		return 0;
	}
	*col += (*col + 2 <= max_col) ? 2 : 0;
	return 1;
}

/*
 * Renders the debug control segment at the left of the status bar when a DAP
 * session is active: a PAUSED/RUNNING badge then context-appropriate buttons.
 * Stopped → Cont/Over/Into/Out/Restart/Stop; running → Pause/Restart/Stop.
 * Records button spans for the mouse layer; writes the columns consumed to
 * *col_io. `max_col` bounds the segment so it never overruns the right side.
 */
static int statusBarAppendDebugSegment(struct writeBuf *wb, int max_col, int *col_io) {
	statusDebugButtonsReset();
	int col = 0;
	if (col + 1 <= max_col && !wbAppend(wb, " ", 1)) {
		return 0;
	}
	col += (col + 1 <= max_col) ? 1 : 0;

	const char *badge = E.dap_stopped ? "PAUSED" : "RUNNING";
	int blen = (int)strlen(badge);
	if (col + blen <= max_col) {
		if (!wbAppend(wb, VT100_BOLD_ON, (int)strlen(VT100_BOLD_ON)) ||
		    !wbAppend(wb, badge, (size_t)blen) ||
		    !wbAppend(wb, VT100_BOLD_OFF, (int)strlen(VT100_BOLD_OFF))) {
			return 0;
		}
		col += blen;
		if (col + 2 <= max_col && !wbAppend(wb, "  ", 2)) {
			return 0;
		}
		col += (col + 2 <= max_col) ? 2 : 0;
	}

	if (E.dap_stopped) {
		if (!statusDebugButton(wb, &col, max_col, "Cont", EDITOR_ACTION_DAP_CONTINUE) ||
		    !statusDebugButton(wb, &col, max_col, "Over", EDITOR_ACTION_DAP_STEP_OVER) ||
		    !statusDebugButton(wb, &col, max_col, "Into", EDITOR_ACTION_DAP_STEP_INTO) ||
		    !statusDebugButton(wb, &col, max_col, "Out", EDITOR_ACTION_DAP_STEP_OUT)) {
			return 0;
		}
	} else if (!statusDebugButton(wb, &col, max_col, "Pause", EDITOR_ACTION_DAP_PAUSE)) {
		return 0;
	}
	if (!statusDebugButton(wb, &col, max_col, "Restart", EDITOR_ACTION_DAP_RESTART) ||
	    !statusDebugButton(wb, &col, max_col, "Stop", EDITOR_ACTION_DAP_STOP)) {
		return 0;
	}
	*col_io = col;
	return 1;
}

int editorStatusBarDebugButtonAt(int col, int *action_out) {
	for (int i = 0; i < g_status_debug_button_count; i++) {
		if (col >= g_status_debug_buttons[i].start_col &&
		    col < g_status_debug_buttons[i].end_col) {
			if (action_out != NULL) {
				*action_out = (int)g_status_debug_buttons[i].action;
			}
			return 1;
		}
	}
	return 0;
}

int editorDrawStatusBar(struct writeBuf *wb, int scroll_progress_percent) {
	if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS)) {
		return 0;
	}
	char rightbuf[80];
	char diagbuf[48];
	const char *filename = editorActiveBufferDisplayName();
	const char *dirtyflag = "";
	diagbuf[0] = '\0';
	if (E.dirty) {
		dirtyflag = "[+]";
	}
	if (E.lsp_diagnostic_count > 0) {
		(void)snprintf(diagbuf, sizeof(diagbuf), " [E:%d W:%d]",
		               E.lsp_diagnostic_error_count, E.lsp_diagnostic_warning_count);
	}

	int progress = scroll_progress_percent;
	if (progress < 0) {
		progress = 0;
	} else if (progress > 100) {
		progress = 100;
	}
	int cursor_col = E.rx + 1;
	if (cursor_col < 1) {
		cursor_col = 1;
	}
	const char *git_branch = editorGitBranch();
	int rlen;
	if (git_branch != NULL) {
		char branch_trunc[25];
		(void)snprintf(branch_trunc, sizeof(branch_trunc), "%s", git_branch);
		const char *dirty_marker = E.git_entry_count > 0 ? "+" : "";
		rlen = snprintf(rightbuf, sizeof(rightbuf), " %s%s  %d,%d    %d%%", branch_trunc,
		                dirty_marker, E.cy + 1, cursor_col, progress);
	} else {
		rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%", E.cy + 1, cursor_col,
		                progress);
	}
	if (rlen < 0) {
		rlen = 0;
	}

	int right_start_col = E.window_cols - rlen;
	if (right_start_col < 0) {
		right_start_col = 0;
	}

	/* Debug control segment occupies the far left while a session is active;
	 * the path/diagnostics region renders into whatever space remains. */
	int debug_cols = 0;
	if (E.dap_running) {
		if (!statusBarAppendDebugSegment(wb, right_start_col, &debug_cols)) {
			return 0;
		}
	} else {
		statusDebugButtonsReset();
	}

	int dirty_cols = (int)strlen(dirtyflag);
	int diag_cols = (int)strlen(diagbuf);
	int left_budget = right_start_col - debug_cols;
	if (left_budget < 0) {
		left_budget = 0;
	}
	int reserved_for_dirty = 0;
	int include_dirty_sep = 0;
	if (dirty_cols > 0) {
		if (left_budget >= dirty_cols + 1) {
			reserved_for_dirty = dirty_cols + 1;
			include_dirty_sep = 1;
		} else if (left_budget >= dirty_cols) {
			reserved_for_dirty = dirty_cols;
		}
	}

	int path_budget = left_budget - reserved_for_dirty;
	if (path_budget < 0) {
		path_budget = 0;
	}
	if (diag_cols > 0 && path_budget >= diag_cols) {
		path_budget -= diag_cols;
	}

	int left_cols = 0;
	if (!editorAppendSanitizedStatusPath(wb, filename, path_budget, &left_cols)) {
		return 0;
	}
	if (diagbuf[0] != '\0' && left_cols < left_budget) {
		int appended = 0;
		if (!editorAppendSanitizedText(wb, diagbuf, left_budget - left_cols, &appended)) {
			return 0;
		}
		left_cols += appended;
	}

	if (reserved_for_dirty > 0) {
		if (include_dirty_sep && left_cols < left_budget) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			left_cols++;
		}

		for (int i = 0; dirtyflag[i] != '\0' && left_cols < left_budget; i++) {
			if (!wbAppend(wb, &dirtyflag[i], 1)) {
				return 0;
			}
			left_cols++;
		}
	}

	for (; left_cols < left_budget; left_cols++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}

	if (rlen > 0 && !wbAppend(wb, rightbuf, (size_t)rlen)) {
		return 0;
	}
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	return wbAppend(wb, "\r\n", 2);
}

int editorDrawMessageBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	if (E.statusmsg[0] != '\0' && time(NULL) - E.statusmsg_time < 5) {
		// Truncate by display columns after escaping, not by raw byte count.
		if (!editorAppendSanitizedText(wb, E.statusmsg, E.window_cols, NULL)) {
			return 0;
		}
	}

	return 1;
}

static int statusBarAppendCursorMove(struct writeBuf *wb, int row, int col) {
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
	if (len <= 0 || len >= (int)sizeof(buf)) {
		return 0;
	}
	return wbAppend(wb, buf, (size_t)len);
}

int editorDrawDiagnosticPopdownMessage(struct writeBuf *wb, const char *message,
                                       int cursor_screen_row, int cursor_screen_col,
                                       int *screen_top_out, int *row_count_out) {
	if (message == NULL || message[0] == '\0') {
		return 1;
	}

	int text_start_col = editorTextBodyStartColForCols(E.window_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	int terminal_col_zero = text_start_col + gutter_cols + cursor_screen_col;
	int message_cols = 0;
	char *sanitized = editorSanitizeDiagnosticMessageDup(message, &message_cols);
	if (sanitized == NULL) {
		return 0;
	}
	int available_cols = E.window_cols - text_start_col;
	int cols = message_cols + 2;
	if (cols > available_cols) {
		cols = available_cols;
	}
	if (cols < 4) {
		free(sanitized);
		return 1;
	}
	int content_cols = cols - 2;
	int row_count = editorDisplayWrapLineCount(sanitized, content_cols);
	if (row_count <= 0) {
		free(sanitized);
		return 1;
	}

	int rows_below = E.window_rows - (cursor_screen_row + 1);
	int rows_above = cursor_screen_row;
	int popdown_screen_row = -1;
	int visible_rows = row_count;
	if (row_count <= rows_below) {
		popdown_screen_row = cursor_screen_row + 1;
	} else if (row_count <= rows_above) {
		popdown_screen_row = cursor_screen_row - row_count;
	} else if (rows_below >= rows_above && rows_below > 0) {
		popdown_screen_row = cursor_screen_row + 1;
		visible_rows = rows_below;
	} else if (rows_above > 0) {
		popdown_screen_row = cursor_screen_row - rows_above;
		visible_rows = rows_above;
	} else {
		free(sanitized);
		return 1;
	}

	if (terminal_col_zero + cols > E.window_cols) {
		terminal_col_zero = E.window_cols - cols;
	}
	if (terminal_col_zero < text_start_col) {
		terminal_col_zero = text_start_col;
	}

	int terminal_col = terminal_col_zero + 1;
	int text_len = (int)strlen(sanitized);
	int start_idx = 0;
	int rows_drawn = 0;
	for (; rows_drawn < visible_rows && start_idx < text_len; rows_drawn++) {
		int terminal_row = popdown_screen_row + rows_drawn + 2;
		int end_idx = start_idx;
		int wrote = 0;
		editorDisplayWrapNextLine(sanitized, text_len, start_idx, content_cols, &end_idx,
		                          &wrote);
		if (end_idx <= start_idx) {
			break;
		}
		if (!statusBarAppendCursorMove(wb, terminal_row, terminal_col) ||
		    !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS) ||
		    !wbAppend(wb, " ", 1) ||
		    !wbAppend(wb, &sanitized[start_idx], (size_t)(end_idx - start_idx))) {
			free(sanitized);
			return 0;
		}
		int padding = cols - 1 - wrote;
		while (padding > 0) {
			if (!wbAppend(wb, " ", 1)) {
				free(sanitized);
				return 0;
			}
			padding--;
		}
		if (!editorAppendThemeReset(wb)) {
			free(sanitized);
			return 0;
		}
		start_idx = end_idx;
	}

	free(sanitized);
	if (screen_top_out != NULL) {
		*screen_top_out = popdown_screen_row + 2;
	}
	if (row_count_out != NULL) {
		*row_count_out = rows_drawn;
	}
	return 1;
}
