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
		(void)fprintf(out, "%s\"\"\n", indent);
		return;
	}
	const char *p = text;
	int line_open = 0;
	while (*p != '\0') {
		if (!line_open) {
			(void)fputs(indent, out);
			(void)fputc('"', out);
			line_open = 1;
		}
		unsigned char c = (unsigned char)*p++;
		switch (c) {
			case '"':
				(void)fputs("\\\"", out);
				break;
			case '\\':
				(void)fputs("\\\\", out);
				break;
			case '\t':
				(void)fputs("\\t", out);
				break;
			case '\r':
				(void)fputs("\\r", out);
				break;
			case '\b':
				(void)fputs("\\b", out);
				break;
			case '\f':
				(void)fputs("\\f", out);
				break;
			case '\n':
				(void)fputs("\\n\"\n", out);
				line_open = 0;
				break;
			default:
				if (c < 0x20) {
					(void)fprintf(out, "\\%03o", c);
				} else {
					(void)fputc(c, out);
				}
				break;
		}
	}
	if (line_open) {
		(void)fputs("\"\n", out);
	}
}
