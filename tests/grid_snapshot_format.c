#include "grid_snapshot_format.h"

#include <stdio.h>

void editor_grid_snapshot_emit_c_string(const char *text, const char *indent, FILE *out) {
	if (text == NULL) {
		return;
	}
	if (indent == NULL) {
		indent = "";
	}
	if (text[0] == '\0') {
		fprintf(out, "%s\"\"\n", indent);
		return;
	}
	const char *p = text;
	int line_open = 0;
	while (*p != '\0') {
		if (!line_open) {
			fputs(indent, out);
			fputc('"', out);
			line_open = 1;
		}
		unsigned char c = (unsigned char)*p++;
		switch (c) {
			case '"':
				fputs("\\\"", out);
				break;
			case '\\':
				fputs("\\\\", out);
				break;
			case '\t':
				fputs("\\t", out);
				break;
			case '\r':
				fputs("\\r", out);
				break;
			case '\b':
				fputs("\\b", out);
				break;
			case '\f':
				fputs("\\f", out);
				break;
			case '\n':
				fputs("\\n\"\n", out);
				line_open = 0;
				break;
			default:
				if (c < 0x20) {
					fprintf(out, "\\%03o", c);
				} else {
					fputc(c, out);
				}
				break;
		}
	}
	if (line_open) {
		fputs("\"\n", out);
	}
}
