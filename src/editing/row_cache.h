#ifndef EDITING_ROW_CACHE_H
#define EDITING_ROW_CACHE_H

#include "rotide.h"

struct editorRowCacheSpliceRegion {
	int start_row;
	int old_end_row_exclusive;
	size_t prefix_start;
	size_t suffix_start_old;
	size_t old_total;
};

void editorFreeRowArray(struct erow *rows, int numrows);
int editorBuildRowsFromDocumentRange(const struct editorDocument *document, int start_row,
                                     int end_row_exclusive, struct erow **rows_out,
                                     int *numrows_out);
int editorBuildFullRowsFromDocument(const struct editorDocument *document, struct erow **rows_out,
                                    int *numrows_out);
int editorPrepareRowCacheSpliceRegion(const struct editorDocument *document, size_t start_offset,
                                      size_t old_len,
                                      struct editorRowCacheSpliceRegion *region_out);
int editorRowCacheSpliceEndRowForDocument(const struct editorDocument *document,
                                          const struct editorRowCacheSpliceRegion *region,
                                          int *end_row_exclusive_out);
int editorSpliceRowCache(struct erow **rows_in_out, int *numrows_in_out,
                         struct erow *replacement_rows, int replacement_numrows, int start_row,
                         int old_end_row_exclusive);

void editorRowCacheStatsReset(void);
int editorRowCacheTestFullRebuildCount(void);
int editorRowCacheTestSpliceUpdateCount(void);

#endif
