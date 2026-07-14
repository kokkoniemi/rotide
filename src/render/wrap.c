#include "render/wrap.h"

#include "render/pane_view.h"
#include "rotide.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"

#include <stdlib.h>

static int wrapTextBodyColsForWidth(int pane_cols) {
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_cols) {
		gutter_cols = pane_cols;
	}
	int text_cols = pane_cols - gutter_cols;
	if (text_cols < 1) {
		text_cols = 1;
	}
	if (text_cols >= 3) {
		return text_cols - 2;
	}
	return text_cols;
}

static int wrapFocusedPaneTextBodyCols(void) {
	struct editorRect rect = {0};
	if (editorLayoutFocusedLeafRect(&rect) && rect.w > 0) {
		return wrapTextBodyColsForWidth(rect.w);
	}
	return editorTextBodyViewportCols(E.window_cols);
}

int editorWrapBodyCols(void) {
	int override_cols = editorPaneWrapBodyColsOverride();
	int body_cols = override_cols > 0 ? override_cols : wrapFocusedPaneTextBodyCols();
	return body_cols < 1 ? 1 : body_cols;
}

int editorWrapContinuationIndentCols(const struct editorRow *row, int body_cols) {
	if (row == NULL || body_cols <= 1 || row->rsize <= 0) {
		return 0;
	}

	int max_indent = body_cols - 1;
	int cols = 0;
	for (int i = 0; i < row->rsize && cols < max_indent;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], row->rsize - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > row->rsize - i) {
			src_len = row->rsize - i;
		}
		if (cp != ' ' && cp != '\t') {
			break;
		}
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);
		if (cols + width > max_indent) {
			break;
		}
		cols += width;
		i += src_len;
	}
	return cols;
}

static int wrapBreaksAfterCodepoint(unsigned int cp) {
	switch (cp) {
		case ' ':
		case '\t':
		case '.':
		case ',':
		case ';':
		case ':':
		case '/':
		case '\\':
		case '-':
		case '_':
		case '(':
		case '[':
		case '{':
		case ')':
		case ']':
		case '}':
		case '=':
		case '+':
		case '&':
		case '|':
			return 1;
		default:
			return 0;
	}
}

static int wrapCacheReserve(struct editorRow *row, int needed_capacity) {
	if (needed_capacity <= row->wrap_cache_capacity) {
		return 1;
	}
	int new_cap = row->wrap_cache_capacity > 0 ? row->wrap_cache_capacity : 4;
	while (new_cap < needed_capacity) {
		new_cap *= 2;
	}
	int *grown = realloc(row->wrap_cache_segments, (size_t)new_cap * sizeof(int));
	if (grown == NULL) {
		return 0;
	}
	row->wrap_cache_segments = grown;
	row->wrap_cache_capacity = new_cap;
	return 1;
}

/* Rescanning from byte zero for every segment makes long rows quadratic. The
 * rewind is bounded to one window, keeping visits per codepoint O(1) amortized. */
static void wrapEnsureCache(struct editorRow *row, int body_cols) {
	if (row == NULL) {
		return;
	}
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (row->wrap_cache_body_cols == body_cols && row->wrap_cache_segment_count > 0) {
		return;
	}

	row->wrap_cache_body_cols = body_cols;
	row->wrap_cache_indent_cols = 0;
	row->wrap_cache_segment_count = 1;
	if (!wrapCacheReserve(row, 1)) {
		row->wrap_cache_body_cols = 0;
		row->wrap_cache_segment_count = 0;
		return;
	}
	row->wrap_cache_segments[0] = 0;

	int total_cols = row->render_display_cols;
	if (total_cols <= 0 || total_cols <= body_cols) {
		return;
	}

	int indent_cols = editorWrapContinuationIndentCols(row, body_cols);
	row->wrap_cache_indent_cols = indent_cols;

	int count = 1;
	int start_col = 0;
	int available_cols = body_cols;
	int target_col = start_col + available_cols;

	/* Match editorWrapNextStartCol's priority while retaining the byte offsets
	 * needed to resume at the chosen boundary. */
	int best_break_col = -1;
	int best_break_byte = -1;
	int best_fit_col = -1;
	int best_fit_byte = -1;
	int first_advance_col = -1;
	int first_advance_byte = -1;

	int rx = 0;
	int i = 0;
	while (i < row->rsize) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], row->rsize - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > row->rsize - i) {
			src_len = row->rsize - i;
		}
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);
		int next_rx = rx + width;
		int next_byte = i + src_len;

		if (next_rx > start_col && first_advance_col == -1) {
			first_advance_col = next_rx;
			first_advance_byte = next_byte;
		}
		if (next_rx > start_col && next_rx <= target_col) {
			best_fit_col = next_rx;
			best_fit_byte = next_byte;
			if (wrapBreaksAfterCodepoint(cp)) {
				best_break_col = next_rx;
				best_break_byte = next_byte;
			}
		}

		if (next_rx > target_col) {
			int chosen_col;
			int chosen_byte;
			if (best_break_col > start_col) {
				chosen_col = best_break_col;
				chosen_byte = best_break_byte;
			} else if (best_fit_col > start_col) {
				chosen_col = best_fit_col;
				chosen_byte = best_fit_byte;
			} else if (first_advance_col > start_col) {
				chosen_col = first_advance_col;
				chosen_byte = first_advance_byte;
			} else {
				break;
			}
			if (chosen_col >= total_cols || chosen_col <= start_col) {
				break;
			}
			if (!wrapCacheReserve(row, count + 1)) {
				break;
			}
			row->wrap_cache_segments[count] = chosen_col;
			count++;

			start_col = chosen_col;
			available_cols = body_cols - indent_cols;
			if (available_cols < 1) {
				available_cols = 1;
			}
			target_col = start_col + available_cols;
			if (target_col >= total_cols) {
				break;
			}
			best_break_col = best_fit_col = first_advance_col = -1;
			best_break_byte = best_fit_byte = first_advance_byte = -1;
			/* The overflow scan may have passed the chosen break, so resume there. */
			rx = chosen_col;
			i = chosen_byte;
			continue;
		}
		rx = next_rx;
		i = next_byte;
	}
	row->wrap_cache_segment_count = count;
}

void editorWrapSegmentInfo(struct editorRow *row, int segment_idx, int body_cols,
                           int *start_col_out, int *available_cols_out, int *indent_cols_out) {
	if (start_col_out != NULL) {
		*start_col_out = 0;
	}
	if (available_cols_out != NULL) {
		*available_cols_out = body_cols < 1 ? 1 : body_cols;
	}
	if (indent_cols_out != NULL) {
		*indent_cols_out = 0;
	}
	if (row == NULL) {
		return;
	}
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (segment_idx < 0) {
		segment_idx = 0;
	}

	wrapEnsureCache(row, body_cols);
	if (row->wrap_cache_segment_count <= 0) {
		return;
	}
	if (segment_idx >= row->wrap_cache_segment_count) {
		segment_idx = row->wrap_cache_segment_count - 1;
	}

	int start_col = row->wrap_cache_segments[segment_idx];
	int current_indent = segment_idx == 0 ? 0 : row->wrap_cache_indent_cols;
	int available_cols = body_cols - current_indent;
	if (available_cols < 1) {
		available_cols = 1;
		current_indent = body_cols > 1 ? body_cols - 1 : 0;
	}

	if (start_col_out != NULL) {
		*start_col_out = start_col;
	}
	if (available_cols_out != NULL) {
		*available_cols_out = available_cols;
	}
	if (indent_cols_out != NULL) {
		*indent_cols_out = current_indent;
	}
}

int editorWrapSegmentCountForRowIndex(int row_idx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 1;
	}
	wrapEnsureCache(&E.rows[row_idx], body_cols);
	int count = E.rows[row_idx].wrap_cache_segment_count;
	return count > 0 ? count : 1;
}

/* Cache-backed equivalent of editorWrapNextStartCol() for callers that already
 * hold a cache-aligned segment start. */
int editorWrapNextStartColCached(struct editorRow *row, int start_col, int body_cols) {
	if (row == NULL) {
		return 0;
	}
	int total_cols = row->render_display_cols;
	if (total_cols <= 0 || start_col >= total_cols) {
		return total_cols;
	}
	if (body_cols < 1) {
		body_cols = 1;
	}
	wrapEnsureCache(row, body_cols);
	int count = row->wrap_cache_segment_count;
	if (count <= 0) {
		return total_cols;
	}
	/* First segment start strictly greater than start_col. */
	int lo = 0;
	int hi = count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (row->wrap_cache_segments[mid] <= start_col) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo < count ? row->wrap_cache_segments[lo] : total_cols;
}

int editorWrapCursorSegmentForRx(struct editorRow *row, int rx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (rx <= 0 || row == NULL) {
		return 0;
	}
	wrapEnsureCache(row, body_cols);
	int count = row->wrap_cache_segment_count;
	if (count <= 1) {
		return 0;
	}
	int lo = 0;
	int hi = count - 1;
	while (lo < hi) {
		int mid = lo + (hi - lo + 1) / 2;
		if (row->wrap_cache_segments[mid] < rx) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return lo;
}

void editorWrappedClampViewportOffsets(void) {
	if (!E.line_wrap_enabled) {
		E.wrapoff = 0;
		return;
	}
	E.coloff = 0;
	if (E.rowoff < 0) {
		E.rowoff = 0;
	}
	int max_rowoff = E.numrows > 0 ? E.numrows - 1 : 0;
	if (E.rowoff > max_rowoff) {
		E.rowoff = max_rowoff;
	}
	int body_cols = editorWrapBodyCols();
	int max_wrapoff = editorWrapSegmentCountForRowIndex(E.rowoff, body_cols) - 1;
	if (E.wrapoff < 0) {
		E.wrapoff = 0;
	}
	if (E.wrapoff > max_wrapoff) {
		E.wrapoff = max_wrapoff;
	}
}

void editorWrappedAdvancePosition(int *row_idx, int *segment_idx, int body_cols) {
	if (row_idx == NULL || segment_idx == NULL) {
		return;
	}
	if (*row_idx >= E.numrows) {
		return;
	}
	int segment_count = editorWrapSegmentCountForRowIndex(*row_idx, body_cols);
	if (*segment_idx + 1 < segment_count) {
		(*segment_idx)++;
		return;
	}
	(*row_idx)++;
	*segment_idx = 0;
}

void editorWrappedMoveBackPosition(int *row_idx, int *segment_idx, int body_cols) {
	if (row_idx == NULL || segment_idx == NULL || (*row_idx <= 0 && *segment_idx <= 0)) {
		return;
	}
	if (*segment_idx > 0) {
		(*segment_idx)--;
		return;
	}
	(*row_idx)--;
	*segment_idx = editorWrapSegmentCountForRowIndex(*row_idx, body_cols) - 1;
}

int editorWrappedPositionBefore(int row_a, int segment_a, int row_b, int segment_b) {
	return row_a < row_b || (row_a == row_b && segment_a < segment_b);
}

int editorWrappedDistanceForward(int from_row, int from_segment, int to_row, int to_segment,
                                 int max_distance, int body_cols, int *distance_out) {
	int row = from_row;
	int segment = from_segment;
	for (int distance = 0; distance <= max_distance; distance++) {
		if (row == to_row && segment == to_segment) {
			if (distance_out != NULL) {
				*distance_out = distance;
			}
			return 1;
		}
		editorWrappedAdvancePosition(&row, &segment, body_cols);
		if (row >= E.numrows) {
			break;
		}
	}
	return 0;
}
