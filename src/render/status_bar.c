#include "render/status_bar.h"

#include "render/ansi_style.h"
#include "render/display_text.h"
#include "workspace/git.h"
#include "workspace/tabs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VT100_CLEAR_ROW_3 "\x1b[K"

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
		rlen = snprintf(rightbuf, sizeof(rightbuf), " %s%s  %d,%d    %d%%",
				branch_trunc, dirty_marker, E.cy + 1, cursor_col, progress);
	} else {
		rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%",
				E.cy + 1, cursor_col, progress);
	}
	if (rlen < 0) {
		rlen = 0;
	}

	int right_start_col = E.window_cols - rlen;
	if (right_start_col < 0) {
		right_start_col = 0;
	}

	int dirty_cols = (int)strlen(dirtyflag);
	int diag_cols = (int)strlen(diagbuf);
	int left_budget = right_start_col;
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
	if (diagbuf[0] != '\0' && left_cols < right_start_col) {
		int appended = 0;
		if (!editorAppendSanitizedText(wb, diagbuf, right_start_col - left_cols, &appended)) {
			return 0;
		}
		left_cols += appended;
	}

	if (reserved_for_dirty > 0) {
		if (include_dirty_sep && left_cols < right_start_col) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			left_cols++;
		}

		for (int i = 0; dirtyflag[i] != '\0' && left_cols < right_start_col; i++) {
			if (!wbAppend(wb, &dirtyflag[i], 1)) {
				return 0;
			}
			left_cols++;
		}
	}

	for (; left_cols < right_start_col; left_cols++) {
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
