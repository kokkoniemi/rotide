#include "render/wrap.h"

#include "editing/buffer_core.h"
#include "render/pane_view.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include <stdlib.h>

static int editorPaneTextBodyViewportColsForWidth(int pane_cols) {
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

static int editorFocusedPaneTextBodyViewportCols(void) {
	struct editorRect rect = {0};
	if (editorLayoutFocusedLeafRect(&rect) && rect.w > 0) {
		return editorPaneTextBodyViewportColsForWidth(rect.w);
	}
	return editorTextBodyViewportCols(E.window_cols);
}

int editorWrapBodyCols(void) {
	int override_cols = editorPaneWrapBodyColsOverride();
	int body_cols = override_cols > 0 ? override_cols :
			editorFocusedPaneTextBodyViewportCols();
	return body_cols < 1 ? 1 : body_cols;
}

int editorWrapContinuationIndentCols(const struct erow *row, int body_cols) {
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

static int editorWrapBreaksAfterCodepoint(unsigned int cp) {
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

int editorWrapNextStartCol(const struct erow *row, int start_col, int available_cols,
		int total_cols) {
	if (row == NULL || available_cols <= 0 || start_col < 0 || start_col >= total_cols) {
		return total_cols;
	}
	if (start_col + available_cols >= total_cols) {
		return total_cols;
	}

	int target_col = start_col + available_cols;
	int best_break_col = -1;
	int best_fit_col = -1;
	int first_advance_col = -1;
	int rx = 0;
	for (int i = 0; i < row->rsize;) {
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
		if (next_rx > start_col && first_advance_col == -1) {
			first_advance_col = next_rx;
		}
		if (next_rx > start_col && next_rx <= target_col) {
			best_fit_col = next_rx;
			if (editorWrapBreaksAfterCodepoint(cp)) {
				best_break_col = next_rx;
			}
		}
		if (next_rx > target_col) {
			break;
		}
		rx = next_rx;
		i += src_len;
	}

	if (best_break_col > start_col) {
		return best_break_col;
	}
	if (best_fit_col > start_col) {
		return best_fit_col;
	}
	if (first_advance_col > start_col) {
		return first_advance_col;
	}
	return total_cols;
}

static int editorWrapCacheReserve(struct erow *row, int needed_capacity) {
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

static void editorWrapEnsureCache(struct erow *row, int body_cols) {
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
	if (!editorWrapCacheReserve(row, 1)) {
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

	int start_col = 0;
	int count = 1;
	while (start_col < total_cols) {
		int current_indent = count == 1 ? 0 : indent_cols;
		int available_cols = body_cols - current_indent;
		if (available_cols < 1) {
			available_cols = 1;
		}
		int next_start = editorWrapNextStartCol(row, start_col, available_cols, total_cols);
		if (next_start >= total_cols || next_start <= start_col) {
			break;
		}
		if (!editorWrapCacheReserve(row, count + 1)) {
			break;
		}
		row->wrap_cache_segments[count] = next_start;
		count++;
		start_col = next_start;
	}
	row->wrap_cache_segment_count = count;
}

void editorWrapSegmentInfo(struct erow *row, int segment_idx, int body_cols,
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

	editorWrapEnsureCache(row, body_cols);
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
	editorWrapEnsureCache(&E.rows[row_idx], body_cols);
	int count = E.rows[row_idx].wrap_cache_segment_count;
	return count > 0 ? count : 1;
}

int editorWrapCursorSegmentForRx(struct erow *row, int rx, int body_cols) {
	if (body_cols < 1) {
		body_cols = 1;
	}
	if (rx <= 0 || row == NULL) {
		return 0;
	}
	editorWrapEnsureCache(row, body_cols);
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
