#include "text/text_summary.h"

#include <string.h>

void editorTextSummaryZero(struct editorTextSummary *out) {
	if (out == NULL) {
		return;
	}
	out->bytes = 0;
	out->newlines = 0;
	out->first_line_bytes = 0;
	out->last_line_bytes = 0;
	out->max_line_bytes = 0;
}

void editorTextSummaryFromBytes(const char *bytes, size_t len,
		struct editorTextSummary *out) {
	if (out == NULL) {
		return;
	}
	editorTextSummaryZero(out);
	if (len == 0 || bytes == NULL) {
		return;
	}

	out->bytes = len;

	size_t run = 0;
	int seen_newline = 0;
	for (size_t i = 0; i < len; i++) {
		if (bytes[i] == '\n') {
			if (!seen_newline) {
				out->first_line_bytes = run;
				seen_newline = 1;
			}
			if (run > out->max_line_bytes) {
				out->max_line_bytes = run;
			}
			out->newlines++;
			run = 0;
		} else {
			run++;
		}
	}

	if (!seen_newline) {
		out->first_line_bytes = run;
	}
	out->last_line_bytes = run;
	if (run > out->max_line_bytes) {
		out->max_line_bytes = run;
	}
}

void editorTextSummaryMerge(const struct editorTextSummary *left,
		const struct editorTextSummary *right, struct editorTextSummary *out) {
	if (out == NULL || left == NULL || right == NULL) {
		return;
	}

	struct editorTextSummary merged;
	merged.bytes = left->bytes + right->bytes;
	merged.newlines = left->newlines + right->newlines;

	size_t boundary = left->last_line_bytes + right->first_line_bytes;

	merged.first_line_bytes = left->newlines == 0
		? left->bytes + right->first_line_bytes
		: left->first_line_bytes;
	merged.last_line_bytes = right->newlines == 0
		? left->last_line_bytes + right->bytes
		: right->last_line_bytes;

	merged.max_line_bytes = left->max_line_bytes;
	if (right->max_line_bytes > merged.max_line_bytes) {
		merged.max_line_bytes = right->max_line_bytes;
	}
	if (boundary > merged.max_line_bytes) {
		merged.max_line_bytes = boundary;
	}

	*out = merged;
}
