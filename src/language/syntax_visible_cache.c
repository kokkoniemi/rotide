#include "language/syntax_visible_cache.h"

#include "editing/document_position.h"
#include "editing/syntax_runtime.h"
#include "editing/text_source.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/document.h"
#include "text/row.h"
#include "language/syntax.h"
#include "language/syntax_worker.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROTIDE_SYNTAX_BACKGROUND_MIN_OVERSCAN_ROWS 64
#define ROTIDE_SYNTAX_BACKGROUND_MAX_OVERSCAN_ROWS 256

void editorSetAllocFailureStatus(void);

struct syntaxVisibleCacheState {
	int prepared;
	int first_row;
	int row_count;
	int row_capacity;
	const struct editorSyntaxState *state;
	enum editorSyntaxLanguage language;
	uint64_t revision;
	uint64_t generation;
	int *span_counts;
	uint8_t *row_dirty;
	struct editorRowSyntaxSpan *spans;
};

static struct syntaxVisibleCacheState g_syntax_visible_cache_state = {0};
static int g_syntax_visible_cache_row_recompute_count = 0;

static int syntaxVisibleCacheOffsetToU32(size_t offset, uint32_t *out) {
	if (out == NULL || offset > UINT32_MAX) {
		return 0;
	}
	*out = (uint32_t)offset;
	return 1;
}

void editorSyntaxVisibleCacheInvalidate(void) {
	g_syntax_visible_cache_state.prepared = 0;
	g_syntax_visible_cache_state.first_row = 0;
	g_syntax_visible_cache_state.row_count = 0;
	g_syntax_visible_cache_state.state = NULL;
	g_syntax_visible_cache_state.language = EDITOR_SYNTAX_NONE;
	g_syntax_visible_cache_state.revision = 0;
	g_syntax_visible_cache_state.generation = 0;
}

static void syntaxVisibleCacheInvalidateRows(int start_row, int end_row_exclusive) {
	if (!g_syntax_visible_cache_state.prepared ||
	    g_syntax_visible_cache_state.row_dirty == NULL || start_row >= end_row_exclusive) {
		return;
	}

	int cache_start = g_syntax_visible_cache_state.first_row;
	int cache_end =
	        g_syntax_visible_cache_state.first_row + g_syntax_visible_cache_state.row_count;
	if (end_row_exclusive <= cache_start || start_row >= cache_end) {
		return;
	}
	if (start_row < cache_start) {
		start_row = cache_start;
	}
	if (end_row_exclusive > cache_end) {
		end_row_exclusive = cache_end;
	}

	for (int row = start_row; row < end_row_exclusive; row++) {
		int rel_row = row - cache_start;
		g_syntax_visible_cache_state.row_dirty[rel_row] = 1;
	}
}

void editorSyntaxVisibleCacheFree(void) {
	free(g_syntax_visible_cache_state.span_counts);
	free(g_syntax_visible_cache_state.row_dirty);
	free(g_syntax_visible_cache_state.spans);
	memset(&g_syntax_visible_cache_state, 0, sizeof(g_syntax_visible_cache_state));
}

static int syntaxVisibleCacheEnsureCapacity(int row_count) {
	if (row_count <= g_syntax_visible_cache_state.row_capacity) {
		return 1;
	}
	if (row_count <= 0) {
		return 1;
	}

	size_t counts_bytes = 0;
	size_t dirty_bytes = 0;
	size_t span_rows = 0;
	size_t spans_bytes = 0;
	if (!editorIntToSize(row_count, &span_rows) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.span_counts), span_rows,
	                   &counts_bytes) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.row_dirty), span_rows,
	                   &dirty_bytes) ||
	    !editorSizeMul(span_rows, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_rows) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.spans), span_rows, &spans_bytes)) {
		return 0;
	}

	int *new_counts = editorRealloc(g_syntax_visible_cache_state.span_counts, counts_bytes);
	if (new_counts == NULL) {
		return 0;
	}
	uint8_t *new_dirty = editorRealloc(g_syntax_visible_cache_state.row_dirty, dirty_bytes);
	if (new_dirty == NULL) {
		return 0;
	}
	struct editorRowSyntaxSpan *new_spans =
	        editorRealloc(g_syntax_visible_cache_state.spans, spans_bytes);
	if (new_spans == NULL) {
		return 0;
	}

	g_syntax_visible_cache_state.span_counts = new_counts;
	g_syntax_visible_cache_state.row_dirty = new_dirty;
	g_syntax_visible_cache_state.spans = new_spans;
	g_syntax_visible_cache_state.row_capacity = row_count;
	return 1;
}

int editorSyntaxVisibleCacheStoreBackgroundResult(const struct editorSyntaxWorkerResult *result) {
	if (result == NULL || result->row_count < 0) {
		return 0;
	}
	if (!syntaxVisibleCacheEnsureCapacity(result->row_count)) {
		return 0;
	}

	g_syntax_visible_cache_state.prepared = 1;
	g_syntax_visible_cache_state.first_row = result->first_row;
	g_syntax_visible_cache_state.row_count = result->row_count;
	g_syntax_visible_cache_state.state = E.syntax_state;
	g_syntax_visible_cache_state.language = result->language;
	g_syntax_visible_cache_state.revision = result->revision;
	g_syntax_visible_cache_state.generation = result->generation;
	if (result->row_count <= 0) {
		return 1;
	}

	size_t rows_size = 0;
	size_t counts_bytes = 0;
	size_t dirty_bytes = 0;
	size_t span_rows = 0;
	size_t spans_bytes = 0;
	if (!editorIntToSize(result->row_count, &rows_size) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.span_counts), rows_size,
	                   &counts_bytes) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.row_dirty), rows_size,
	                   &dirty_bytes) ||
	    !editorSizeMul(rows_size, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_rows) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.spans), span_rows, &spans_bytes)) {
		return 0;
	}
	if (result->span_counts != NULL) {
		memcpy(g_syntax_visible_cache_state.span_counts, result->span_counts, counts_bytes);
	} else {
		memset(g_syntax_visible_cache_state.span_counts, 0, counts_bytes);
	}
	memset(g_syntax_visible_cache_state.row_dirty, 0, dirty_bytes);
	if (result->spans != NULL) {
		memcpy(g_syntax_visible_cache_state.spans, result->spans, spans_bytes);
	}
	g_syntax_visible_cache_row_recompute_count += result->row_count;
	return 1;
}

static int syntaxVisibleCacheNormalizeRows(int *first_row_in_out, int *row_count_in_out) {
	if (first_row_in_out == NULL || row_count_in_out == NULL) {
		return 0;
	}
	int first_row = *first_row_in_out;
	int row_count = *row_count_in_out;
	if (row_count <= 0 || E.numrows <= 0) {
		*first_row_in_out = 0;
		*row_count_in_out = 0;
		return 1;
	}
	if (first_row < 0) {
		row_count += first_row;
		first_row = 0;
	}
	if (first_row >= E.numrows || row_count <= 0) {
		*first_row_in_out = 0;
		*row_count_in_out = 0;
		return 1;
	}
	if (first_row + row_count > E.numrows) {
		row_count = E.numrows - first_row;
	}
	if (row_count < 0) {
		row_count = 0;
	}
	*first_row_in_out = first_row;
	*row_count_in_out = row_count;
	return 1;
}

static int syntaxVisibleCacheRowRangeCovers(int cached_first, int cached_count, int visible_first,
                                            int visible_count) {
	if (visible_count <= 0) {
		return 1;
	}
	if (cached_count <= 0 || cached_first > visible_first) {
		return 0;
	}
	return cached_first + cached_count >= visible_first + visible_count;
}

static int syntaxVisibleCacheExpandBackgroundRows(int *first_row_in_out, int *row_count_in_out) {
	if (first_row_in_out == NULL || row_count_in_out == NULL) {
		return 0;
	}
	if (!syntaxVisibleCacheNormalizeRows(first_row_in_out, row_count_in_out)) {
		return 0;
	}
	if (*row_count_in_out <= 0) {
		return 1;
	}

	int overscan = *row_count_in_out;
	if (overscan < ROTIDE_SYNTAX_BACKGROUND_MIN_OVERSCAN_ROWS) {
		overscan = ROTIDE_SYNTAX_BACKGROUND_MIN_OVERSCAN_ROWS;
	}
	if (overscan > ROTIDE_SYNTAX_BACKGROUND_MAX_OVERSCAN_ROWS) {
		overscan = ROTIDE_SYNTAX_BACKGROUND_MAX_OVERSCAN_ROWS;
	}

	int visible_first = *first_row_in_out;
	int visible_end = visible_first + *row_count_in_out;
	int expanded_first = visible_first - overscan;
	if (expanded_first < 0) {
		expanded_first = 0;
	}
	int expanded_end = visible_end + overscan;
	if (expanded_end < visible_end || expanded_end > E.numrows) {
		expanded_end = E.numrows;
	}

	*first_row_in_out = expanded_first;
	*row_count_in_out = expanded_end - expanded_first;
	return 1;
}

int editorSyntaxVisibleCacheScheduleBackground(int first_row, int row_count) {
	if (!editorSyntaxBackgroundEnabled()) {
		return 0;
	}
	if (E.syntax_language == EDITOR_SYNTAX_NONE) {
		return 1;
	}
	if (!syntaxVisibleCacheExpandBackgroundRows(&first_row, &row_count)) {
		return 0;
	}
	if (E.syntax_background_pending && E.syntax_pending_revision == E.syntax_revision &&
	    E.syntax_pending_first_row == first_row && E.syntax_pending_row_count == row_count) {
		return 1;
	}

	size_t text_len = 0;
	char *text = editorDupActiveTextSource(&text_len);
	if (text == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct editorSyntaxWorkerJob job = {.language = E.syntax_language,
	                                    .revision = E.syntax_revision,
	                                    .generation = E.syntax_generation,
	                                    .first_row = first_row,
	                                    .row_count = row_count,
	                                    .text = text,
	                                    .text_len = text_len};
	if (!editorSyntaxWorkerSchedule(&job)) {
		free(text);
		return 0;
	}
	E.syntax_background_pending = 1;
	E.syntax_pending_revision = E.syntax_revision;
	E.syntax_pending_first_row = first_row;
	E.syntax_pending_row_count = row_count;
	return 1;
}

static void syntaxVisibleCacheMarkRowsDirty(int rel_start, int rel_end_exclusive) {
	if (rel_start < 0) {
		rel_start = 0;
	}
	if (rel_end_exclusive > g_syntax_visible_cache_state.row_count) {
		rel_end_exclusive = g_syntax_visible_cache_state.row_count;
	}
	if (rel_start >= rel_end_exclusive) {
		return;
	}

	for (int rel_row = rel_start; rel_row < rel_end_exclusive; rel_row++) {
		g_syntax_visible_cache_state.span_counts[rel_row] = 0;
		g_syntax_visible_cache_state.row_dirty[rel_row] = 1;
	}
}

static int syntaxVisibleCacheSlide(int first_row, int row_count) {
	if (!g_syntax_visible_cache_state.prepared || g_syntax_visible_cache_state.row_count <= 0) {
		return 0;
	}

	int old_first = g_syntax_visible_cache_state.first_row;
	int old_count = g_syntax_visible_cache_state.row_count;
	int old_end = old_first + old_count;
	int new_end = first_row + row_count;
	int overlap_start = old_first > first_row ? old_first : first_row;
	int overlap_end = old_end < new_end ? old_end : new_end;
	if (overlap_start >= overlap_end) {
		return 0;
	}

	int src_rel = overlap_start - old_first;
	int dst_rel = overlap_start - first_row;
	int overlap_count = overlap_end - overlap_start;
	size_t overlap_size = 0;
	size_t count_bytes = 0;
	size_t dirty_bytes = 0;
	size_t span_count = 0;
	size_t span_bytes = 0;
	if (!editorIntToSize(overlap_count, &overlap_size) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.span_counts), overlap_size,
	                   &count_bytes) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.row_dirty), overlap_size,
	                   &dirty_bytes) ||
	    !editorSizeMul(overlap_size, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_count) ||
	    !editorSizeMul(sizeof(*g_syntax_visible_cache_state.spans), span_count, &span_bytes)) {
		return 0;
	}

	memmove(&g_syntax_visible_cache_state.span_counts[dst_rel],
	        &g_syntax_visible_cache_state.span_counts[src_rel], count_bytes);
	memmove(&g_syntax_visible_cache_state.row_dirty[dst_rel],
	        &g_syntax_visible_cache_state.row_dirty[src_rel], dirty_bytes);
	memmove(&g_syntax_visible_cache_state.spans[dst_rel * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW],
	        &g_syntax_visible_cache_state.spans[src_rel * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW],
	        span_bytes);

	g_syntax_visible_cache_state.first_row = first_row;
	g_syntax_visible_cache_state.row_count = row_count;
	syntaxVisibleCacheMarkRowsDirty(0, dst_rel);
	syntaxVisibleCacheMarkRowsDirty(dst_rel + overlap_count, row_count);
	return 1;
}

static int syntaxVisibleCacheByteRangeToRows(size_t start_byte, size_t end_byte, int *start_row_out,
                                             int *end_row_exclusive_out) {
	if (start_row_out == NULL || end_row_exclusive_out == NULL) {
		return 0;
	}
	*start_row_out = 0;
	*end_row_exclusive_out = 0;

	if (E.numrows <= 0) {
		return 1;
	}
	size_t total = 0;
	if (!editorBufferPosToOffset(E.numrows, 0, &total)) {
		return 0;
	}
	if (start_byte > total) {
		start_byte = total;
	}
	if (end_byte > total) {
		end_byte = total;
	}
	if (end_byte < start_byte) {
		end_byte = start_byte;
	}

	size_t end_lookup = end_byte > start_byte ? end_byte - 1 : start_byte;
	int start_row = 0;
	int start_cx = 0;
	int end_row = 0;
	int end_cx = 0;
	if (!editorBufferOffsetToPos(start_byte, &start_row, &start_cx) ||
	    !editorBufferOffsetToPos(end_lookup, &end_row, &end_cx)) {
		return 0;
	}
	(void)start_cx;
	(void)end_cx;
	if (start_row == E.numrows && E.numrows > 0) {
		start_row = E.numrows - 1;
	}
	if (end_row == E.numrows && E.numrows > 0) {
		end_row = E.numrows - 1;
	}

	int end_row_exclusive = end_row + 1;
	if (start_row < 0) {
		start_row = 0;
	}
	if (start_row > E.numrows) {
		start_row = E.numrows;
	}
	if (end_row_exclusive < start_row) {
		end_row_exclusive = start_row;
	}
	if (end_row_exclusive > E.numrows) {
		end_row_exclusive = E.numrows;
	}

	*start_row_out = start_row;
	*end_row_exclusive_out = end_row_exclusive;
	return 1;
}

int editorSyntaxVisibleCacheInvalidateChangedRowsFromState(void) {
	if (E.syntax_state == NULL || E.numrows <= 0) {
		return 1;
	}

	int range_count = 0;
	if (!editorSyntaxStateCopyLastChangedRanges(E.syntax_state, NULL, 0, &range_count)) {
		return 0;
	}
	if (range_count <= 0) {
		return 1;
	}

	size_t range_count_size = 0;
	size_t range_bytes = 0;
	if (!editorIntToSize(range_count, &range_count_size) ||
	    !editorSizeMul(sizeof(struct editorSyntaxByteRange), range_count_size, &range_bytes)) {
		return 0;
	}

	struct editorSyntaxByteRange *ranges = editorMalloc(range_bytes);
	if (ranges == NULL) {
		return 0;
	}

	int copied_total = 0;
	if (!editorSyntaxStateCopyLastChangedRanges(E.syntax_state, ranges, range_count,
	                                            &copied_total)) {
		free(ranges);
		return 0;
	}
	int copied = range_count;
	if (copied_total < copied) {
		copied = copied_total;
	}

	for (int i = 0; i < copied; i++) {
		int start_row = 0;
		int end_row_exclusive = 0;
		if (!syntaxVisibleCacheByteRangeToRows((size_t)ranges[i].start_byte,
		                                       (size_t)ranges[i].end_byte, &start_row,
		                                       &end_row_exclusive)) {
			free(ranges);
			return 0;
		}
		if (start_row > 0) {
			start_row--;
		}
		if (end_row_exclusive < E.numrows) {
			end_row_exclusive++;
		}
		syntaxVisibleCacheInvalidateRows(start_row, end_row_exclusive);
	}

	free(ranges);
	return 1;
}

void editorSyntaxVisibleCacheInvalidateRowsForEdit(const struct editorSyntaxEdit *edit) {
	if (edit == NULL || E.numrows <= 0) {
		return;
	}

	int start_row = (int)edit->start_point.row;
	int old_end_row = (int)edit->old_end_point.row;
	int new_end_row = (int)edit->new_end_point.row;

	int min_row = start_row;
	if (old_end_row < min_row) {
		min_row = old_end_row;
	}
	if (new_end_row < min_row) {
		min_row = new_end_row;
	}
	int max_row = start_row;
	if (old_end_row > max_row) {
		max_row = old_end_row;
	}
	if (new_end_row > max_row) {
		max_row = new_end_row;
	}

	if (min_row < 0) {
		min_row = 0;
	}
	if (max_row < min_row) {
		max_row = min_row;
	}
	if (max_row >= E.numrows) {
		max_row = E.numrows - 1;
	}
	syntaxVisibleCacheInvalidateRows(min_row, max_row + 1);
}

static int syntaxVisibleCacheBuildSpans(int first_row, int row_count) {
	if (row_count <= 0 || E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE ||
	    E.numrows <= 0) {
		editorSyntaxVisibleCacheInvalidate();
		g_syntax_visible_cache_state.prepared = 1;
		return 1;
	}

	if (first_row < 0) {
		row_count += first_row;
		first_row = 0;
	}
	if (first_row >= E.numrows || row_count <= 0) {
		editorSyntaxVisibleCacheInvalidate();
		g_syntax_visible_cache_state.prepared = 1;
		return 1;
	}
	if (first_row + row_count > E.numrows) {
		row_count = E.numrows - first_row;
	}
	if (row_count <= 0) {
		editorSyntaxVisibleCacheInvalidate();
		g_syntax_visible_cache_state.prepared = 1;
		return 1;
	}
	if (!syntaxVisibleCacheEnsureCapacity(row_count)) {
		return 0;
	}
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	int cache_identity_matches = g_syntax_visible_cache_state.prepared &&
	                             g_syntax_visible_cache_state.state == E.syntax_state &&
	                             g_syntax_visible_cache_state.language == E.syntax_language &&
	                             g_syntax_visible_cache_state.revision == E.syntax_revision &&
	                             g_syntax_visible_cache_state.generation == E.syntax_generation;
	if (!cache_identity_matches || g_syntax_visible_cache_state.first_row != first_row ||
	    g_syntax_visible_cache_state.row_count != row_count) {
		g_syntax_visible_cache_state.prepared = 1;
		g_syntax_visible_cache_state.state = E.syntax_state;
		g_syntax_visible_cache_state.language = E.syntax_language;
		g_syntax_visible_cache_state.revision = E.syntax_revision;
		g_syntax_visible_cache_state.generation = E.syntax_generation;
		if (!cache_identity_matches || !syntaxVisibleCacheSlide(first_row, row_count)) {
			g_syntax_visible_cache_state.first_row = first_row;
			g_syntax_visible_cache_state.row_count = row_count;
			syntaxVisibleCacheMarkRowsDirty(0, row_count);
		}
	}

	for (int rel_row = 0; rel_row < row_count; rel_row++) {
		if (!g_syntax_visible_cache_state.row_dirty[rel_row]) {
			continue;
		}

		int row_idx = first_row + rel_row;
		struct editorRow *row = &E.rows[row_idx];
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, row_idx, &line)) {
			return 0;
		}
		int span_base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
		g_syntax_visible_cache_state.span_counts[rel_row] = 0;

		size_t row_start_offset = 0;
		size_t row_end_offset = 0;
		if (!editorBufferLineByteRange(row_idx, &row_start_offset, &row_end_offset)) {
			return 0;
		}
		uint32_t start_byte = 0;
		uint32_t end_byte = 0;
		if (!syntaxVisibleCacheOffsetToU32(row_start_offset, &start_byte) ||
		    !syntaxVisibleCacheOffsetToU32(row_end_offset, &end_byte) ||
		    start_byte >= end_byte) {
			editorLineViewRelease(&line);
			g_syntax_visible_cache_state.row_dirty[rel_row] = 0;
			continue;
		}

		int capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW * 3;
		if (capture_limit < ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
			capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
		}
		size_t cap_size = 0;
		size_t cap_bytes = 0;
		if (!editorIntToSize(capture_limit, &cap_size) ||
		    !editorSizeMul(sizeof(struct editorSyntaxCapture), cap_size, &cap_bytes)) {
			editorLineViewRelease(&line);
			return 0;
		}

		struct editorSyntaxCapture *captures = editorMalloc(cap_bytes);
		if (captures == NULL) {
			editorLineViewRelease(&line);
			return 0;
		}

		int capture_count = 0;
		if (!editorSyntaxStateCollectCapturesForRange(E.syntax_state, &source, start_byte,
		                                              end_byte, captures, capture_limit,
		                                              &capture_count)) {
			editorLineViewRelease(&line);
			free(captures);
			return 0;
		}

		for (int cap_idx = 0; cap_idx < capture_count; cap_idx++) {
			struct editorSyntaxCapture capture = captures[cap_idx];
			if (capture.highlight_class == EDITOR_SYNTAX_HL_NONE ||
			    capture.end_byte <= capture.start_byte) {
				continue;
			}

			int slot = g_syntax_visible_cache_state.span_counts[rel_row];
			if (slot >= ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
				editorSyntaxStateRecordCaptureTruncated(E.syntax_state, row_idx);
				continue;
			}

			int local_start = (int)(capture.start_byte - start_byte);
			int local_end = (int)(capture.end_byte - start_byte);
			if (local_start < 0) {
				local_start = 0;
			}
			if (local_start > line.size) {
				local_start = line.size;
			}
			if (local_end < 0) {
				local_end = 0;
			}
			if (local_end > line.size) {
				local_end = line.size;
			}

			local_start =
			        editorBytesClampCxToCharBoundary(line.data, line.size, local_start);
			local_end =
			        editorBytesClampCxToCharBoundary(line.data, line.size, local_end);
			if (local_end <= local_start && local_end < line.size) {
				local_end = editorBytesNextCharIdx(line.data, line.size, local_end);
			}
			if (local_end <= local_start) {
				continue;
			}

			int render_start = editorBytesCxToRenderIdx(line.data, line.size,
			                                            row->rsize, local_start);
			int render_end = editorBytesCxToRenderIdx(line.data, line.size, row->rsize,
			                                          local_end);
			if (render_end <= render_start) {
				continue;
			}

			g_syntax_visible_cache_state.spans[span_base + slot].start_render_idx =
			        render_start;
			g_syntax_visible_cache_state.spans[span_base + slot].end_render_idx =
			        render_end;
			g_syntax_visible_cache_state.spans[span_base + slot].highlight_class =
			        capture.highlight_class;
			g_syntax_visible_cache_state.span_counts[rel_row] = slot + 1;
		}

		editorLineViewRelease(&line);
		free(captures);
		g_syntax_visible_cache_state.row_dirty[rel_row] = 0;
		g_syntax_visible_cache_row_recompute_count++;
	}

	editorSyntaxRuntimeReportStatusIfNeeded();
	return 1;
}

int editorSyntaxPrepareVisibleRowSpans(int first_row, int row_count) {
	if (editorSyntaxBackgroundEnabled()) {
		editorSyntaxBackgroundPoll();
		if (E.syntax_language == EDITOR_SYNTAX_NONE) {
			editorSyntaxVisibleCacheInvalidate();
			return 1;
		}
		if (!syntaxVisibleCacheNormalizeRows(&first_row, &row_count)) {
			return 0;
		}
		if (g_syntax_visible_cache_state.prepared &&
		    g_syntax_visible_cache_state.state == E.syntax_state &&
		    g_syntax_visible_cache_state.language == E.syntax_language &&
		    g_syntax_visible_cache_state.revision == E.syntax_revision &&
		    g_syntax_visible_cache_state.generation == E.syntax_generation &&
		    syntaxVisibleCacheRowRangeCovers(g_syntax_visible_cache_state.first_row,
		                                     g_syntax_visible_cache_state.row_count,
		                                     first_row, row_count)) {
			return 1;
		}
		if (E.syntax_background_pending && E.syntax_pending_revision == E.syntax_revision &&
		    syntaxVisibleCacheRowRangeCovers(E.syntax_pending_first_row,
		                                     E.syntax_pending_row_count, first_row,
		                                     row_count)) {
			return 1;
		}
		return editorSyntaxVisibleCacheScheduleBackground(first_row, row_count);
	}
	return syntaxVisibleCacheBuildSpans(first_row, row_count);
}

int editorSyntaxPrepareVisibleRowSpansForeground(int first_row, int row_count) {
	return syntaxVisibleCacheBuildSpans(first_row, row_count);
}

void editorSyntaxTestResetVisibleRowRecomputeCount(void) {
	g_syntax_visible_cache_row_recompute_count = 0;
}

int editorSyntaxTestVisibleRowRecomputeCount(void) {
	return g_syntax_visible_cache_row_recompute_count;
}

int editorSyntaxRowRenderSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
                               int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (row_idx < 0 || row_idx >= E.numrows || max_spans < 0 ||
	    (max_spans > 0 && spans == NULL)) {
		return 0;
	}
	if (max_spans == 0 || E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE) {
		return 1;
	}

	if (editorSyntaxBackgroundEnabled()) {
		if (g_syntax_visible_cache_state.prepared &&
		    g_syntax_visible_cache_state.state == E.syntax_state &&
		    g_syntax_visible_cache_state.language == E.syntax_language &&
		    g_syntax_visible_cache_state.revision == E.syntax_revision &&
		    g_syntax_visible_cache_state.generation == E.syntax_generation &&
		    row_idx >= g_syntax_visible_cache_state.first_row &&
		    row_idx < g_syntax_visible_cache_state.first_row +
		                      g_syntax_visible_cache_state.row_count) {
			int rel_row = row_idx - g_syntax_visible_cache_state.first_row;
			int cached_count = g_syntax_visible_cache_state.span_counts[rel_row];
			if (cached_count > max_spans) {
				cached_count = max_spans;
			}
			if (cached_count > 0) {
				size_t count_size = 0;
				size_t copy_bytes = 0;
				if (!editorIntToSize(cached_count, &count_size) ||
				    !editorSizeMul(sizeof(*spans), count_size, &copy_bytes)) {
					return 0;
				}
				int base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
				memcpy(spans, &g_syntax_visible_cache_state.spans[base],
				       copy_bytes);
			}
			if (count_out != NULL) {
				*count_out = cached_count;
			}
		}
		return 1;
	}

	if (g_syntax_visible_cache_state.prepared &&
	    g_syntax_visible_cache_state.state == E.syntax_state &&
	    g_syntax_visible_cache_state.language == E.syntax_language &&
	    g_syntax_visible_cache_state.revision == E.syntax_revision &&
	    g_syntax_visible_cache_state.generation == E.syntax_generation &&
	    row_idx >= g_syntax_visible_cache_state.first_row &&
	    row_idx < g_syntax_visible_cache_state.first_row +
	                      g_syntax_visible_cache_state.row_count) {
		int rel_row = row_idx - g_syntax_visible_cache_state.first_row;
		int cached_count = g_syntax_visible_cache_state.span_counts[rel_row];
		if (cached_count > max_spans) {
			cached_count = max_spans;
		}
		if (cached_count > 0) {
			size_t count_size = 0;
			size_t copy_bytes = 0;
			if (!editorIntToSize(cached_count, &count_size) ||
			    !editorSizeMul(sizeof(*spans), count_size, &copy_bytes)) {
				return 0;
			}
			int base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
			memcpy(spans, &g_syntax_visible_cache_state.spans[base], copy_bytes);
		}
		if (count_out != NULL) {
			*count_out = cached_count;
		}
		return 1;
	}

	size_t row_start_offset = 0;
	size_t row_end_offset = 0;
	if (!editorBufferLineByteRange(row_idx, &row_start_offset, &row_end_offset)) {
		return 0;
	}

	uint32_t start_byte = 0;
	uint32_t end_byte = 0;
	if (!syntaxVisibleCacheOffsetToU32(row_start_offset, &start_byte) ||
	    !syntaxVisibleCacheOffsetToU32(row_end_offset, &end_byte) || start_byte >= end_byte) {
		return 1;
	}
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	int capture_limit = max_spans;
	if (capture_limit > ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
		capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
	}

	struct editorSyntaxCapture captures[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int capture_count = 0;
	if (!editorSyntaxStateCollectCapturesForRange(E.syntax_state, &source, start_byte, end_byte,
	                                              captures, capture_limit, &capture_count)) {
		return 0;
	}

	struct editorRow *row = &E.rows[row_idx];
	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, row_idx, &line)) {
		return 0;
	}
	int out_count = 0;
	for (int i = 0; i < capture_count && out_count < max_spans; i++) {
		if (captures[i].highlight_class == EDITOR_SYNTAX_HL_NONE ||
		    captures[i].end_byte <= captures[i].start_byte) {
			continue;
		}

		int local_start = (int)(captures[i].start_byte - start_byte);
		int local_end = (int)(captures[i].end_byte - start_byte);
		if (local_start < 0) {
			local_start = 0;
		}
		if (local_start > line.size) {
			local_start = line.size;
		}
		if (local_end < 0) {
			local_end = 0;
		}
		if (local_end > line.size) {
			local_end = line.size;
		}

		local_start = editorBytesClampCxToCharBoundary(line.data, line.size, local_start);
		local_end = editorBytesClampCxToCharBoundary(line.data, line.size, local_end);
		if (local_end <= local_start && local_end < line.size) {
			local_end = editorBytesNextCharIdx(line.data, line.size, local_end);
		}
		if (local_end <= local_start) {
			continue;
		}

		int render_start =
		        editorBytesCxToRenderIdx(line.data, line.size, row->rsize, local_start);
		int render_end =
		        editorBytesCxToRenderIdx(line.data, line.size, row->rsize, local_end);
		if (render_end <= render_start) {
			continue;
		}

		spans[out_count].start_render_idx = render_start;
		spans[out_count].end_render_idx = render_end;
		spans[out_count].highlight_class = captures[i].highlight_class;
		out_count++;
	}
	editorLineViewRelease(&line);

	if (count_out != NULL) {
		*count_out = out_count;
	}
	return 1;
}
