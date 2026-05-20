#ifndef TEXT_SUMMARY_H
#define TEXT_SUMMARY_H

#include <stddef.h>

/* Associative summary carried on tree nodes and pieces during the buffer
 * refactor. max_line_bytes is the longest run of non-newline bytes within the
 * span; first/last_line_bytes are the runs anchored at the span's start/end.
 * At the document root, the longest line equals
 * max(summary.max_line_bytes, summary.first_line_bytes, summary.last_line_bytes).
 */
struct editorTextSummary {
	size_t bytes;
	int newlines;
	size_t first_line_bytes;
	size_t last_line_bytes;
	size_t max_line_bytes;
};

void editorTextSummaryZero(struct editorTextSummary *out);
void editorTextSummaryFromBytes(const char *bytes, size_t len, struct editorTextSummary *out);
void editorTextSummaryMerge(const struct editorTextSummary *left,
                            const struct editorTextSummary *right, struct editorTextSummary *out);

#endif
