#include "debug/dap_output.h"

#include "rotide.h"

#include <string.h>

void editorDapOutputClear(void) {
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
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
}

const char *editorDapOutputText(void) {
	return E.dap_output;
}

size_t editorDapOutputLength(void) {
	return E.dap_output_len;
}
