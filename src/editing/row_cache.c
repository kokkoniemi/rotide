#include "editing/row_cache.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/document.h"
#include "text/row.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int g_row_cache_full_rebuild_count = 0;
static int g_row_cache_splice_update_count = 0;

void editorFreeRowArray(struct editorRow *rows, int numrows) {
	for (int i = 0; i < numrows; i++) {
		free(rows[i].render);
		free(rows[i].wrap_cache_segments);
	}
	free(rows);
}

static int rowCacheInitRestoredRow(struct editorRow *row, const char *s, size_t len) {
	int row_size = 0;

	if (!editorSizeToInt(len, &row_size)) {
		return 0;
	}

	char *row_render = NULL;
	int row_rsize = 0;
	int row_display_cols = 0;
	if (!editorRowBuildRender(s, row_size, &row_render, &row_rsize, &row_display_cols)) {
		return 0;
	}

	row->rsize = row_rsize;
	row->render_display_cols = row_display_cols;
	row->render = row_render;
	row->wrap_cache_body_cols = 0;
	row->wrap_cache_segment_count = 0;
	row->wrap_cache_indent_cols = 0;
	row->wrap_cache_capacity = 0;
	row->wrap_cache_segments = NULL;
	return 1;
}

int editorBuildRowsFromDocumentRange(const struct editorDocument *document, int start_row,
                                     int end_row_exclusive, struct editorRow **rows_out,
                                     int *numrows_out) {
	struct editorRow *rows = NULL;
	int numrows = 0;

	if (document == NULL || rows_out == NULL || numrows_out == NULL || start_row < 0 ||
	    end_row_exclusive < start_row) {
		return 0;
	}

	int line_count = editorDocumentLineCount(document);
	if (end_row_exclusive > line_count) {
		return 0;
	}
	/* One upfront allocation: growing the array one row per line degrades
	 * full rebuilds to O(lines^2) copying under allocators that cannot
	 * extend in place (e.g. Fil-C's GC heap). */
	if (end_row_exclusive > start_row) {
		size_t row_bytes = 0;
		if (!editorSizeMul(sizeof(struct editorRow),
		                   (size_t)(end_row_exclusive - start_row), &row_bytes)) {
			return 0;
		}
		rows = editorMalloc(row_bytes);
		if (rows == NULL) {
			return 0;
		}
	}
	for (int line_idx = start_row; line_idx < end_row_exclusive; line_idx++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(document, line_idx, &line)) {
			editorFreeRowArray(rows, numrows);
			return 0;
		}
		int ok = rowCacheInitRestoredRow(&rows[numrows], line.data, (size_t)line.size);
		editorLineViewRelease(&line);
		if (!ok) {
			editorFreeRowArray(rows, numrows);
			return 0;
		}
		numrows++;
	}

	*rows_out = rows;
	*numrows_out = numrows;
	return 1;
}

static int rowCacheBuildRowsFromDocument(const struct editorDocument *document,
                                         struct editorRow **rows_out, int *numrows_out) {
	if (document == NULL) {
		return 0;
	}
	return editorBuildRowsFromDocumentRange(document, 0, editorDocumentLineCount(document),
	                                        rows_out, numrows_out);
}

int editorBuildFullRowsFromDocument(const struct editorDocument *document,
                                    struct editorRow **rows_out, int *numrows_out) {
	if (!rowCacheBuildRowsFromDocument(document, rows_out, numrows_out)) {
		return 0;
	}
	g_row_cache_full_rebuild_count++;
	return 1;
}

static int rowCacheApplySignedByteDelta(size_t value, size_t old_total, size_t new_total,
                                        size_t *out) {
	if (out == NULL) {
		return 0;
	}
	if (new_total >= old_total) {
		return editorSizeAdd(value, new_total - old_total, out);
	}
	size_t delta = old_total - new_total;
	if (value < delta) {
		return 0;
	}
	*out = value - delta;
	return 1;
}

int editorPrepareRowCacheSpliceRegion(const struct editorDocument *document, size_t start_offset,
                                      size_t old_len,
                                      struct editorRowCacheSpliceRegion *region_out) {
	size_t old_total = 0;
	size_t first_lookup = 0;
	size_t last_lookup = 0;
	size_t old_end_offset = 0;
	int start_row = 0;
	int end_row = 0;

	if (document == NULL || region_out == NULL) {
		return 0;
	}

	old_total = editorDocumentLength(document);
	if (start_offset > old_total || old_len > old_total - start_offset) {
		return 0;
	}
	old_end_offset = start_offset + old_len;

	if (old_total == 0) {
		*region_out = (struct editorRowCacheSpliceRegion){.start_row = 0,
		                                                  .old_end_row_exclusive = 0,
		                                                  .prefix_start = 0,
		                                                  .suffix_start_old = 0,
		                                                  .old_total = 0};
		return 1;
	}

	first_lookup = start_offset;
	if (first_lookup == old_total) {
		first_lookup = old_total - 1;
	}
	last_lookup = old_len > 0 ? start_offset + old_len - 1 : first_lookup;

	if (!editorDocumentLineIndexForByteOffset(document, first_lookup, &start_row) ||
	    !editorDocumentLineIndexForByteOffset(document, last_lookup, &end_row) ||
	    !editorDocumentLineStartByte(document, start_row, &region_out->prefix_start)) {
		return 0;
	}
	if (old_len > 0 && old_end_offset < old_total) {
		int boundary_row = 0;
		size_t boundary_start = 0;
		if (!editorDocumentLineIndexForByteOffset(document, old_end_offset,
		                                          &boundary_row) ||
		    !editorDocumentLineStartByte(document, boundary_row, &boundary_start)) {
			return 0;
		}
		if (boundary_start == old_end_offset && boundary_row > end_row) {
			end_row = boundary_row;
		}
	}

	region_out->start_row = start_row;
	region_out->old_end_row_exclusive = end_row + 1;
	region_out->old_total = old_total;
	if (region_out->old_end_row_exclusive < editorDocumentLineCount(document) &&
	    !editorDocumentLineStartByte(document, region_out->old_end_row_exclusive,
	                                 &region_out->suffix_start_old)) {
		return 0;
	}
	if (region_out->old_end_row_exclusive >= editorDocumentLineCount(document)) {
		region_out->suffix_start_old = old_total;
	}
	return 1;
}

int editorRowCacheSpliceEndRowForDocument(const struct editorDocument *document,
                                          const struct editorRowCacheSpliceRegion *region,
                                          int *end_row_exclusive_out) {
	size_t new_total = 0;
	size_t new_suffix_start = 0;
	size_t last_lookup = 0;
	int last_row = 0;

	if (document == NULL || region == NULL || end_row_exclusive_out == NULL) {
		return 0;
	}

	new_total = editorDocumentLength(document);
	if (new_total == 0) {
		*end_row_exclusive_out = 0;
		return 1;
	}
	if (!rowCacheApplySignedByteDelta(region->suffix_start_old, region->old_total, new_total,
	                                  &new_suffix_start) ||
	    new_suffix_start > new_total) {
		return 0;
	}

	if (new_suffix_start > region->prefix_start) {
		last_lookup = new_suffix_start - 1;
	} else if (region->prefix_start < new_total) {
		last_lookup = region->prefix_start;
	} else {
		last_lookup = new_total - 1;
	}
	if (!editorDocumentLineIndexForByteOffset(document, last_lookup, &last_row)) {
		return 0;
	}
	*end_row_exclusive_out = last_row + 1;
	if (*end_row_exclusive_out < region->start_row) {
		*end_row_exclusive_out = region->start_row;
	}
	return 1;
}

int editorSpliceRowCache(struct editorRow **rows_in_out, int *numrows_in_out,
                         struct editorRow *replacement_rows, int replacement_numrows, int start_row,
                         int old_end_row_exclusive) {
	int numrows = 0;
	struct editorRow *rows = NULL;
	int remove_count = 0;
	int tail_count = 0;
	int new_numrows = 0;
	struct editorRow *grown = NULL;

	if (rows_in_out == NULL || numrows_in_out == NULL) {
		return 0;
	}
	rows = *rows_in_out;
	numrows = *numrows_in_out;
	if (start_row < 0 || old_end_row_exclusive < start_row || old_end_row_exclusive > numrows ||
	    replacement_numrows < 0 || (replacement_numrows > 0 && replacement_rows == NULL)) {
		return 0;
	}

	remove_count = old_end_row_exclusive - start_row;
	tail_count = numrows - old_end_row_exclusive;
	if (start_row > INT_MAX - replacement_numrows ||
	    start_row + replacement_numrows > INT_MAX - tail_count) {
		return 0;
	}
	new_numrows = start_row + replacement_numrows + tail_count;

	if (new_numrows > numrows) {
		size_t row_count_size = 0;
		size_t row_bytes = 0;
		if (!editorIntToSize(new_numrows, &row_count_size) ||
		    !editorSizeMul(sizeof(*rows), row_count_size, &row_bytes)) {
			return 0;
		}
		grown = editorRealloc(rows, row_bytes);
		if (grown == NULL) {
			return 0;
		}
		rows = grown;
	}

	for (int i = start_row; i < old_end_row_exclusive; i++) {
		free(rows[i].render);
		free(rows[i].wrap_cache_segments);
		rows[i].render = NULL;
		rows[i].wrap_cache_segments = NULL;
		rows[i].wrap_cache_capacity = 0;
		rows[i].wrap_cache_body_cols = 0;
		rows[i].wrap_cache_segment_count = 0;
		rows[i].wrap_cache_indent_cols = 0;
	}

	if (tail_count > 0 && replacement_numrows != remove_count) {
		memmove(&rows[start_row + replacement_numrows], &rows[old_end_row_exclusive],
		        sizeof(*rows) * (size_t)tail_count);
	}
	for (int i = 0; i < replacement_numrows; i++) {
		rows[start_row + i] = replacement_rows[i];
	}

	if (new_numrows == 0) {
		free(rows);
		rows = NULL;
	} else if (new_numrows < numrows) {
		size_t row_count_size = 0;
		size_t row_bytes = 0;
		if (!editorIntToSize(new_numrows, &row_count_size) ||
		    !editorSizeMul(sizeof(*rows), row_count_size, &row_bytes)) {
			return 0;
		}
		grown = editorRealloc(rows, row_bytes);
		if (grown != NULL) {
			rows = grown;
		}
	}

	*rows_in_out = rows;
	*numrows_in_out = new_numrows;
	g_row_cache_splice_update_count++;
	free(replacement_rows);
	return 1;
}

void editorRowCacheStatsReset(void) {
	g_row_cache_full_rebuild_count = 0;
	g_row_cache_splice_update_count = 0;
}

int editorRowCacheTestFullRebuildCount(void) {
	return g_row_cache_full_rebuild_count;
}

int editorRowCacheTestSpliceUpdateCount(void) {
	return g_row_cache_splice_update_count;
}
