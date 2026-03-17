#include "output.h"

#include "alloc.h"
#include "buffer.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*** Write buffer ***/

struct writeBuf {
	char *b;
	int len;
};

#define WRITEBUF_INIT {NULL, 0}

#define VT100_CLEAR_ROW_3 "\x1b[K"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"
#define VT100_HIDE_CURSOR_6 "\x1b[?25l"
#define VT100_SHOW_CURSOR_6 "\x1b[?25h"
#define VT100_CURSOR_STEADY_BAR_5 "\x1b[6 q"
#define VT100_INVERTED_COLORS_4 "\x1b[7m"
#define VT100_NORMAL_COLORS_3 "\x1b[m"

static int wbAppend(struct writeBuf *wb, const char *s, int len) {
	if (len <= 0) {
		return 1;
	}

	char *new = editorRealloc(wb->b, (size_t)wb->len + (size_t)len);

	if (new == NULL) {
		return 0;
	}

	memcpy(&new[wb->len], s, len);
	wb->b = new;
	wb->len += len;

	return 1;
}

static void wbFree(struct writeBuf *wb) {
	free(wb->b);
}

/*** Output ***/

static int editorWriteAllToStdout(const char *buf, int len) {
	if (len <= 0) {
		return 1;
	}

	errno = 0;
	int total = 0;
	while (total < len) {
		ssize_t written = write(STDOUT_FILENO, buf + total, (size_t)(len - total));
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (written == 0) {
			errno = 0;
			return 0;
		}
		total += (int)written;
	}
	return 1;
}

static int editorDrawGreeting(struct writeBuf *wb) {
	char greet[80];
	int greetlen = snprintf(greet, sizeof(greet),
				"RotIDE editor - version %s", ROTIDE_VERSION);
	if (greetlen > E.window_cols) {
		greetlen = E.window_cols;
	}
	int pad = (E.window_cols - greetlen) / 2;
	if (pad) {
		if (!wbAppend(wb, "~", 1)) {
			return 0;
		}
		pad--;
	}
	while (pad--) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return wbAppend(wb, greet, greetlen);
}

static void editorRenderSliceBounds(const struct erow *row, int coloff, int cols, int *start_out,
		int *end_out) {
	int rx = 0;
	int start = -1;
	int end = row->rsize;

	// Map display columns back to byte offsets in row->render.
	// This keeps horizontal scrolling aligned even when wide/zero-width
	// codepoints appear in the rendered data.
	for (int i = 0; i < row->rsize; i++) {
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);

		if (start == -1 && rx + width > coloff) {
			start = i;
		}
		if (start != -1 && width > 0 && rx + width > coloff + cols) {
			end = i;
			break;
		}

		rx += width;
	}

	*start_out = start;
	*end_out = end;
}

static void editorMatchCharsToRenderSpan(const struct erow *row, int match_start, int match_len,
		int *render_start_out, int *render_end_out) {
	if (match_start < 0) {
		match_start = 0;
	}
	if (match_start > row->size) {
		match_start = row->size;
	}

	int match_end = match_start + match_len;
	if (match_end < match_start) {
		match_end = match_start;
	}
	if (match_end > row->size) {
		match_end = row->size;
	}

	int chars_idx = 0;
	int render_idx = 0;
	int rx = 0;
	int render_start = -1;
	int render_end = -1;

	while (chars_idx < row->size) {
		if (chars_idx == match_start) {
			render_start = render_idx;
		}
		if (chars_idx == match_end) {
			render_end = render_idx;
			break;
		}

		if (row->chars[chars_idx] == '\t') {
			do {
				render_idx++;
				rx++;
			} while (rx % ROTIDE_TAB_WIDTH != 0);
			chars_idx++;
			continue;
		}

		render_idx++;
		rx += editorCharDisplayWidth(&row->chars[chars_idx], row->size - chars_idx);
		chars_idx++;
	}

	if (render_start == -1) {
		render_start = render_idx;
	}
	if (render_end == -1) {
		render_end = render_idx;
	}
	if (render_end < render_start) {
		render_end = render_start;
	}

	*render_start_out = render_start;
	*render_end_out = render_end;
}

static int editorSelectionSpanForRow(int row_idx, int *start_out, int *end_out) {
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	struct editorSelectionRange selection;
	if (!editorGetSelectionRange(&selection)) {
		return 0;
	}
	if (row_idx < selection.start_cy || row_idx > selection.end_cy) {
		return 0;
	}

	int start = 0;
	int end = E.rows[row_idx].size;
	if (selection.start_cy == selection.end_cy) {
		start = selection.start_cx;
		end = selection.end_cx;
	} else {
		if (row_idx == selection.start_cy) {
			start = selection.start_cx;
		}
		if (row_idx == selection.end_cy && selection.end_cy < E.numrows) {
			end = selection.end_cx;
		}
	}

	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static int editorDrawRenderSlice(struct writeBuf *wb, struct erow *row, int row_idx, int coloff,
		int cols) {
	if (cols <= 0 || coloff < 0 || row->rsize <= 0) {
		return 1;
	}

	int start = -1;
	int end = row->rsize;
	editorRenderSliceBounds(row, coloff, cols, &start, &end);
	if (start == -1 || end <= start) {
		return 1;
	}

	int highlight_start_chars = -1;
	int highlight_len_chars = 0;

	int selection_start = 0;
	int selection_end = 0;
	if (editorSelectionSpanForRow(row_idx, &selection_start, &selection_end)) {
		highlight_start_chars = selection_start;
		highlight_len_chars = selection_end - selection_start;
	} else if (E.search_match_row == row_idx && E.search_match_len > 0) {
		highlight_start_chars = E.search_match_start;
		highlight_len_chars = E.search_match_len;
	}

	if (highlight_len_chars <= 0) {
		return wbAppend(wb, &row->render[start], end - start);
	}

	int match_render_start = 0;
	int match_render_end = 0;
	editorMatchCharsToRenderSpan(row, highlight_start_chars, highlight_len_chars,
			&match_render_start, &match_render_end);
	if (match_render_end <= match_render_start) {
		return wbAppend(wb, &row->render[start], end - start);
	}

	int highlight_start = start > match_render_start ? start : match_render_start;
	int highlight_end = end < match_render_end ? end : match_render_end;
	if (highlight_end <= highlight_start) {
		return wbAppend(wb, &row->render[start], end - start);
	}

	if (highlight_start > start &&
			!wbAppend(wb, &row->render[start], highlight_start - start)) {
		return 0;
	}
	if (!wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
		return 0;
	}
	if (!wbAppend(wb, &row->render[highlight_start], highlight_end - highlight_start)) {
		return 0;
	}
	if (!wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
		return 0;
	}
	if (highlight_end < end &&
			!wbAppend(wb, &row->render[highlight_end], end - highlight_end)) {
		return 0;
	}

	return 1;
}

static int editorDrawFileRow(struct writeBuf *wb, size_t i) {
	return editorDrawRenderSlice(wb, &E.rows[i], (int)i, E.coloff, E.window_cols);
}

static int editorDrawRows(struct writeBuf *wb) {
	for (int y = 0; y < E.window_rows; y++) {
		int y_offset = y + E.rowoff;

		if (y_offset < E.numrows) {
			if (!editorDrawFileRow(wb, y_offset)) {
				return 0;
			}
		} else if (E.numrows == 0 && y == E.window_rows / 3) {
			if (!editorDrawGreeting(wb)) {
				return 0;
			}
		} else {
			if (!wbAppend(wb, "~", 1)) {
				return 0;
			}
		}

		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			return 0;
		}
		if (!wbAppend(wb, "\r\n", 2)) {
			return 0;
		}
	}

	return 1;
}

static int editorDrawStatusBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_INVERTED_COLORS_4, 4)) {
		return 0;
	}
	char leftbuf[80], rightbuf[80];
	char *filename = E.filename;
	if (filename == NULL) {
		filename = "[No Name]";
	}
	char *dirtyflag = "";
	if (E.dirty) {
		dirtyflag = "[+]";
	}

	int llen = snprintf(leftbuf, sizeof(leftbuf), "%.20s %s",
			filename, dirtyflag);
	int progress = 0;
	if (E.numrows == 1) {
		progress = 100;
	} else if (E.numrows > 1) {
		progress = (int)((float)E.cy / (E.numrows - 1) * 100);
	}
	if (progress < 0) {
		progress = 0;
	}
	if (progress > 100) {
		progress = 100;
	}
	int cursor_col = E.rx + 1;
	if (cursor_col < 1) {
		cursor_col = 1;
	}
	int rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%",
				E.cy + 1, cursor_col, progress);
	if (llen > E.window_cols) {
		llen = E.window_cols;
	}
	if (!wbAppend(wb, leftbuf, llen)) {
		return 0;
	}

	for (; llen < E.window_cols - rlen; llen++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}

	if (!wbAppend(wb, rightbuf, rlen)) {
		return 0;
	}
	if (!wbAppend(wb, VT100_NORMAL_COLORS_3, 3)) {
		return 0;
	}
	return wbAppend(wb, "\r\n", 2);
}

void editorScroll(void) {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}

	// Keep the cursor visible vertically and horizontally by moving
	// the window origin just enough to include the current position.
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	} else if (E.cy >= E.rowoff + E.window_rows) {
		E.rowoff = E.cy - E.window_rows + 1;
	}

	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	} else if (E.rx >= E.coloff + E.window_cols) {
		E.coloff = E.rx - E.window_cols + 1;
	}
}

static int editorDrawMessageBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	int msglen = strlen(E.statusmsg);
	if (msglen > E.window_cols) {
		msglen = E.window_cols;
	}
	if (msglen && time(NULL) - E.statusmsg_time < 5) {
		if (!wbAppend(wb, E.statusmsg, msglen)) {
			return 0;
		}
	}

	return 1;
}

void editorRefreshScreen(void) {
	editorScroll();

	struct writeBuf wb = WRITEBUF_INIT;

	// Build a full frame in memory and write once to reduce terminal flicker.
	if (!wbAppend(&wb, VT100_HIDE_CURSOR_6, 6) ||
			!wbAppend(&wb, VT100_CURSOR_STEADY_BAR_5, 5) ||
			!wbAppend(&wb, VT100_RESET_CURSOR_POS_3, 3)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}

	if (!editorDrawRows(&wb) || !editorDrawStatusBar(&wb) ||
			!editorDrawMessageBar(&wb)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}

	char buf[32];
	int buflen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
			(E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	if (buflen > 0 && buflen < (int)sizeof(buf)) {
		if (!wbAppend(&wb, buf, buflen)) {
			wbFree(&wb);
			editorSetStatusMsg("Out of memory");
			return;
		}
	}

	if (!wbAppend(&wb, VT100_SHOW_CURSOR_6, 6)) {
		wbFree(&wb);
		editorSetStatusMsg("Out of memory");
		return;
	}

	if (!editorWriteAllToStdout(wb.b, wb.len)) {
		int saved_errno = errno;
		wbFree(&wb);
		if (saved_errno != 0) {
			editorSetStatusMsg("Output write failed: %s", strerror(saved_errno));
		} else {
			editorSetStatusMsg("Output write failed");
		}
		return;
	}

	wbFree(&wb);
}
