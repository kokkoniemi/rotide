#include "test_grid_snapshot.h"
#include "test_helpers.h"

#include "render/screen.h"
#include "rotide.h"

#include "../vendor/libvterm/include/vterm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Encode a single Unicode codepoint as UTF-8. Returns bytes written. */
static int encode_utf8(uint32_t cp, char *out) {
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	if (cp < 0x110000) {
		out[0] = (char)(0xF0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[3] = (char)(0x80 | (cp & 0x3F));
		return 4;
	}
	return 0;
}

char *editor_grid_snapshot(size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	int rows = E.window_rows > 0 ? E.window_rows : 24;
	int cols = E.window_cols > 0 ? E.window_cols : 80;

	/* Force a full repaint. The screen-diff cache would otherwise return
	 * a delta against the previous frame, making the snapshot represent
	 * "what changed" instead of "what the user sees right now." */
	editorOutputTestResetFrameCache();

	size_t capture_len = 0;
	char *captured = refresh_screen_and_capture(&capture_len);
	if (captured == NULL) {
		return NULL;
	}

	VTerm *vt = vterm_new(rows, cols);
	if (vt == NULL) {
		free(captured);
		return NULL;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen == NULL) {
		vterm_free(vt);
		free(captured);
		return NULL;
	}
	vterm_screen_reset(screen, 1);
	if (capture_len > 0) {
		vterm_input_write(vt, captured, capture_len);
	}
	vterm_screen_flush_damage(screen);
	free(captured);
	captured = NULL;

	/* Worst case: every cell is a 4-byte codepoint + a newline per row + NUL. */
	size_t out_cap = (size_t)rows * ((size_t)cols * 4 + 1) + 1;
	char *out = (char *)malloc(out_cap);
	if (out == NULL) {
		vterm_free(vt);
		return NULL;
	}

	size_t out_pos = 0;
	for (int r = 0; r < rows; r++) {
		size_t line_start = out_pos;
		size_t last_non_space = line_start;
		int seen_non_space = 0;
		int col = 0;
		while (col < cols) {
			VTermPos pos = {.row = r, .col = col};
			VTermScreenCell cell = {0};
			if (vterm_screen_get_cell(screen, pos, &cell) == 0 || cell.width <= 0) {
				out[out_pos++] = ' ';
				col++;
				continue;
			}
			uint32_t cp = cell.chars[0];
			if (cp == 0) {
				out[out_pos++] = ' ';
			} else {
				char buf[4];
				int n = encode_utf8(cp, buf);
				if (n <= 0) {
					out[out_pos++] = '?';
				} else {
					memcpy(&out[out_pos], buf, (size_t)n);
					out_pos += (size_t)n;
				}
				if (cp != ' ') {
					seen_non_space = 1;
					last_non_space = out_pos;
				}
			}
			col += cell.width;
		}
		/* Strip trailing spaces. */
		out_pos = seen_non_space ? last_non_space : line_start;
		out[out_pos++] = '\n';
	}

	/* Strip trailing blank rows so screens shorter than the window don't
	 * pad the snapshot with newlines. */
	while (out_pos >= 2 && out[out_pos - 1] == '\n' && out[out_pos - 2] == '\n') {
		out_pos--;
	}
	out[out_pos] = '\0';

	vterm_free(vt);

	if (len_out != NULL) {
		*len_out = out_pos;
	}
	return out;
}

static const char *line_end(const char *s) {
	const char *nl = strchr(s, '\n');
	return nl != NULL ? nl : s + strlen(s);
}

int editor_grid_snapshot_diff(const char *expected, const char *actual) {
	if (expected == NULL) {
		expected = "";
	}
	if (actual == NULL) {
		actual = "";
	}
	if (strcmp(expected, actual) == 0) {
		return 0;
	}

	fprintf(stderr, "grid_snapshot_diff:\n");
	const char *ep = expected;
	const char *ap = actual;
	int line = 1;
	while (*ep != '\0' || *ap != '\0') {
		const char *eend = line_end(ep);
		const char *aend = line_end(ap);
		size_t elen = (size_t)(eend - ep);
		size_t alen = (size_t)(aend - ap);
		if (elen == alen && memcmp(ep, ap, elen) == 0) {
			fprintf(stderr, "  %3d   %.*s\n", line, (int)elen, ep);
		} else {
			fprintf(stderr, "  %3d - %.*s\n", line, (int)elen, ep);
			fprintf(stderr, "  %3d + %.*s\n", line, (int)alen, ap);
		}
		line++;
		ep = eend;
		ap = aend;
		if (*ep == '\n') {
			ep++;
		}
		if (*ap == '\n') {
			ap++;
		}
	}
	return 1;
}
