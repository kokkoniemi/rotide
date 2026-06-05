#include "debug/dap_output.h"

#include "debug/dap.h"
#include "rotide.h"

#include <string.h>

static void dapOutputRebuildLineIndex(void) {
	E.dap_output_line_count = 0;
	if (E.dap_output_len == 0) {
		return;
	}
	E.dap_output_line_start[E.dap_output_line_count++] = 0;
	for (size_t i = 0; i < E.dap_output_len; i++) {
		if (E.dap_output[i] == '\n' && i + 1 < E.dap_output_len &&
		    E.dap_output_line_count < ROTIDE_DAP_OUTPUT_MAX_LINES) {
			E.dap_output_line_start[E.dap_output_line_count++] = i + 1;
		}
	}
}

void editorDapOutputClear(void) {
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	E.dap_output_line_count = 0;
}

void editorDapOutputAppend(const char *text) {
	if (text == NULL || text[0] == '\0') {
		return;
	}
	size_t len = strlen(text);
	if (len >= sizeof(E.dap_output)) {
		text += len - (sizeof(E.dap_output) - 1);
		len = strlen(text);
		E.dap_output_len = 0;
	}
	if (E.dap_output_len + len >= sizeof(E.dap_output)) {
		size_t remove = E.dap_output_len + len - (sizeof(E.dap_output) - 1);
		memmove(E.dap_output, E.dap_output + remove, E.dap_output_len - remove);
		E.dap_output_len -= remove;
	}
	memcpy(E.dap_output + E.dap_output_len, text, len);
	E.dap_output_len += len;
	E.dap_output[E.dap_output_len] = '\0';
	dapOutputRebuildLineIndex();
}

const char *editorDapOutputText(void) {
	return E.dap_output;
}

size_t editorDapOutputLength(void) {
	return E.dap_output_len;
}

int editorDapOutputLineCount(void) {
	return E.dap_output_line_count;
}

int editorDapOutputLine(int index, const char **line_out, int *len_out) {
	if (line_out == NULL || len_out == NULL) {
		return 0;
	}
	*line_out = NULL;
	*len_out = 0;
	if (index < 0 || index >= E.dap_output_line_count) {
		return 0;
	}
	size_t start = E.dap_output_line_start[index];
	size_t end = index + 1 < E.dap_output_line_count ? E.dap_output_line_start[index + 1] - 1
	                                                 : E.dap_output_len;
	if (end > start && E.dap_output[end - 1] == '\n') {
		end--;
	}
	*line_out = &E.dap_output[start];
	*len_out = (int)(end - start);
	return 1;
}
