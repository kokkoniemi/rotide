#include "buffer.h"

#include "input.h"
#include "terminal.h"

/*** File io ***/

#define NEWLINE_CHAR_WIDTH 1

char *editorRowsToStr(int *buflen) {
	int total = 0;
	for (int i = 0; i < E.numrows; i++) {
		total += E.rows[i].size + NEWLINE_CHAR_WIDTH;
	}

	char *buf = malloc(total);
	char *p = buf;
	for (int i = 0; i < E.numrows; i++) {
		memcpy(p, E.rows[i].chars, E.rows[i].size);
		p += E.rows[i].size;
		*p = '\n';
		p++;
	}

	*buflen = total;
	return buf;
}

int editorIsUtf8ContinuationByte(unsigned char c) {
	return (c & 0xC0) == 0x80;
}

int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp) {
	if (len <= 0) {
		*cp = 0;
		return 0;
	}

	unsigned char b0 = (unsigned char)s[0];
	if (b0 < 0x80) {
		*cp = b0;
		return 1;
	}

	int expected_len = 0;
	unsigned int codepoint = 0;
	unsigned int min_codepoint = 0;

	// Determine UTF-8 sequence length and initial payload bits from leading byte.
	if ((b0 & 0xE0) == 0xC0) {
		expected_len = 2;
		codepoint = b0 & 0x1F;
		min_codepoint = 0x80;
	} else if ((b0 & 0xF0) == 0xE0) {
		expected_len = 3;
		codepoint = b0 & 0x0F;
		min_codepoint = 0x800;
	} else if ((b0 & 0xF8) == 0xF0) {
		expected_len = 4;
		codepoint = b0 & 0x07;
		min_codepoint = 0x10000;
	} else {
		*cp = b0;
		return 1;
	}

	// If sequence is truncated or malformed, treat first byte as standalone.
	if (len < expected_len) {
		*cp = b0;
		return 1;
	}

	for (int i = 1; i < expected_len; i++) {
		unsigned char bx = (unsigned char)s[i];
		if (!editorIsUtf8ContinuationByte(bx)) {
			*cp = b0;
			return 1;
		}
			codepoint = (codepoint << 6) | (unsigned int)(bx & 0x3F);
	}

	// Reject overlong forms, surrogate range, and out-of-range codepoints.
	if (codepoint < min_codepoint || codepoint > 0x10FFFF ||
			(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
		*cp = b0;
		return 1;
	}

	*cp = codepoint;
	return expected_len;
}

int editorIsRegionalIndicatorCodepoint(unsigned int cp) {
	return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

int editorIsGraphemeExtendCodepoint(unsigned int cp) {
	// Unicode combining mark blocks that should stay in the same grapheme.
	if ((cp >= 0x0300 && cp <= 0x036F) ||
			(cp >= 0x1AB0 && cp <= 0x1AFF) ||
			(cp >= 0x1DC0 && cp <= 0x1DFF) ||
			(cp >= 0x20D0 && cp <= 0x20FF) ||
			(cp >= 0xFE20 && cp <= 0xFE2F)) {
		return 1;
	}
	// Variation selectors modify the previous glyph and should not split clusters.
	if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
			(cp >= 0xE0100 && cp <= 0xE01EF)) {
		return 1;
	}
	// Emoji skin-tone modifiers are attached to the previous emoji.
	if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
		return 1;
	}
	// Keep ZWNJ with the current cluster for cursor stepping consistency.
	if (cp == 0x200C) {
		return 1;
	}
	if (cp > (unsigned int)WCHAR_MAX) {
		return 0;
	}
	// Fallback: many libc locales report combining marks with width 0.
	wchar_t wc = (wchar_t)cp;
	int width = wcwidth(wc);
	return width == 0 && cp != 0x200D;
}

int editorCharDisplayWidth(const char *s, int len) {
	unsigned char c = s[0];
	if (c < 0x80) {
		return 1;
	}
	if (editorIsUtf8ContinuationByte(c)) {
		return 0;
	}

	mbstate_t ps = {0};
	wchar_t wc;
	size_t read = mbrtowc(&wc, s, len, &ps);
	if (read == (size_t)-1 || read == (size_t)-2) {
		return 1;
	}
	int width = wcwidth(wc);
	if (width < 0) {
		return 1;
	}
	return width;
}

int editorRowClampCxToCharBoundary(struct erow *row, int cx) {
	if (cx < 0) {
		return 0;
	}
	if (cx > row->size) {
		cx = row->size;
	}
	while (cx > 0 && cx < row->size &&
			editorIsUtf8ContinuationByte((unsigned char)row->chars[cx])) {
		cx--;
	}
	return cx;
}

int editorRowPrevCharIdx(struct erow *row, int idx) {
	if (idx <= 0) {
		return 0;
	}
	idx = editorRowClampCxToCharBoundary(row, idx);
	idx--;
	while (idx > 0 && editorIsUtf8ContinuationByte((unsigned char)row->chars[idx])) {
		idx--;
	}
	return idx;
}

int editorRowNextCharIdx(struct erow *row, int idx) {
	if (idx >= row->size) {
		return row->size;
	}
	idx = editorRowClampCxToCharBoundary(row, idx);
	unsigned int cp = 0;
	int step = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &cp);
	if (step <= 0) {
		step = 1;
	}
	if (idx + step > row->size) {
		return row->size;
	}
	return idx + step;
}

int editorRowNextClusterIdx(struct erow *row, int idx) {
	idx = editorRowClampCxToCharBoundary(row, idx);
	if (idx >= row->size) {
		return row->size;
	}

	unsigned int cp = 0;
	int cp_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &cp);
	if (cp_len <= 0) {
		cp_len = 1;
	}
	idx += cp_len;

	if (editorIsRegionalIndicatorCodepoint(cp)) {
		if (idx < row->size) {
			unsigned int next_cp = 0;
			int next_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &next_cp);
			if (next_len <= 0) {
				next_len = 1;
			}
			if (editorIsRegionalIndicatorCodepoint(next_cp)) {
				idx += next_len;
			}
		}
		return idx;
	}

	while (idx < row->size) {
		unsigned int next_cp = 0;
		int next_len = editorUtf8DecodeCodepoint(&row->chars[idx], row->size - idx, &next_cp);
		if (next_len <= 0) {
			next_len = 1;
		}

		if (editorIsGraphemeExtendCodepoint(next_cp)) {
			idx += next_len;
			continue;
		}

		if (next_cp == 0x200D) {
			int after_zwj = idx + next_len;
			idx = after_zwj;
			if (idx >= row->size) {
				return row->size;
			}

			int linked_len = editorUtf8DecodeCodepoint(
					&row->chars[idx], row->size - idx, &next_cp);
			if (linked_len <= 0) {
				linked_len = 1;
			}
			idx += linked_len;
			continue;
		}

		break;
	}

	return idx;
}

int editorRowPrevClusterIdx(struct erow *row, int idx) {
	idx = editorRowClampCxToCharBoundary(row, idx);
	if (idx <= 0) {
		return 0;
	}

	int prev = 0;
	int scan = 0;
	while (scan < idx) {
		prev = scan;
		scan = editorRowNextClusterIdx(row, scan);
		if (scan <= prev) {
			return prev;
		}
	}

	return prev;
}

int editorRowClampCxToClusterBoundary(struct erow *row, int cx) {
	cx = editorRowClampCxToCharBoundary(row, cx);
	if (cx <= 0) {
		return 0;
	}

	int boundary = 0;
	while (boundary < cx) {
		int next_boundary = editorRowNextClusterIdx(row, boundary);
		if (next_boundary > cx || next_boundary <= boundary) {
			break;
		}
		boundary = next_boundary;
	}

	return boundary;
}

int editorRowCxToRx(struct erow *row, int cx) {
	int rx = 0;
	cx = editorRowClampCxToClusterBoundary(row, cx);
	for (int j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') {
			rx += (ROTIDE_TAB_WIDTH - 1) - (rx % ROTIDE_TAB_WIDTH);
			rx++;
			continue;
		}
		rx += editorCharDisplayWidth(&row->chars[j], row->size - j);
	}
	return rx;
}

void editorUpdateRow(struct erow *row) {
	int tabs = 0;
	for (int i = 0; i < row->size; i++) {
		if (row->chars[i] == '\t') {
			tabs++;
		}
	}

	free(row->render);
	row->render = malloc(row->size + tabs * (ROTIDE_TAB_WIDTH - 1) + 1);

	int idx = 0;
	for (int i = 0; i < row->size; i++) {
		if (row->chars[i] == '\t') {
			do {
				row->render[idx++] = ' ';
			} while (idx % ROTIDE_TAB_WIDTH != 0);
			continue;
		}
		row->render[idx++] = row->chars[i];
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorInsertRow(int idx, char *s, size_t len) {
	if (idx < 0 || idx > E.numrows) {
		return;
	}

	E.rows = realloc(E.rows, sizeof(struct erow) * (E.numrows + 1));
	memmove(&E.rows[idx + 1], &E.rows[idx], sizeof(struct erow) * (E.numrows - idx));

	E.rows[idx].size = len;
	E.rows[idx].chars = malloc(len + 1);
	memcpy(E.rows[idx].chars, s, len);
	E.rows[idx].chars[len] = '\0';
	E.numrows++;
	E.dirty++;

	E.rows[idx].rsize = 0;
	E.rows[idx].render = NULL;
	editorUpdateRow(&E.rows[idx]);
}

void editorDeleteRow(int idx) {
	if (idx < 0 || idx > E.numrows) {
		return;
	}
	struct erow *row = &E.rows[idx];
	free(row->render);
	free(row->chars);

	memmove(row, &E.rows[idx + 1], sizeof(struct erow) * (E.numrows - idx - 1));
	E.numrows--;
	E.dirty++;
}

void editorInsertCharAt(struct erow *row, int idx, int c) {
	if (idx < 0 || row->size < idx) {
		idx = row->size;
	}

	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[idx + 1], &row->chars[idx], row->size - idx + 1);
	row->size++;
	row->chars[idx] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(struct erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorDelCharAt(struct erow *row, int idx) {
	if (idx < 0 || row->size < idx) {
		return;
	}
	memmove(&row->chars[idx], &row->chars[idx + 1], row->size - idx);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

void editorDelCharsAt(struct erow *row, int idx, int len) {
	if (idx < 0 || len <= 0 || idx + len > row->size) {
		return;
	}
	memmove(&row->chars[idx], &row->chars[idx + len], row->size - idx - len + 1);
	row->size -= len;
	editorUpdateRow(row);
	E.dirty++;
}

void editorInsertChar(int c) {
	if (E.cy == E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	editorInsertCharAt(&E.rows[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewline(void) {
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
		E.cy++;
		return;
	}

	struct erow *row = &E.rows[E.cy];
	int split_idx = editorRowClampCxToClusterBoundary(row, E.cx);
	editorInsertRow(E.cy + 1, &row->chars[split_idx], row->size - split_idx);
	row = &E.rows[E.cy];
	row->size = split_idx;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.cy++;
	E.cx = 0;
}

void editorDelChar(void) {
	if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
		return;
	}
	struct erow *row = &E.rows[E.cy];

	if (E.cx > 0) {
		int prev_cx = editorRowPrevClusterIdx(row, E.cx);
		editorDelCharsAt(row, prev_cx, E.cx - prev_cx);
		E.cx = prev_cx;
		return;
	}

	E.cx = E.rows[E.cy - 1].size;
	editorRowAppendString(&E.rows[E.cy - 1], row->chars, row->size);
	editorDeleteRow(E.cy);
	E.cy--;
}

void editorOpen(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		panic("fopen");
	}

	char *l = NULL;
	size_t lcap = 0;
	ssize_t llen;
	while ((llen = getline(&l, &lcap, fp)) != -1) {
		while (llen > 0 && (l[llen - 1] == '\n' || l[llen - 1] == '\r')) {
			llen--;
		}

		editorInsertRow(E.numrows, l, llen);
	}

	free(l);
	fclose(fp);
	E.dirty = 0;
}

void editorSetStatusMsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorSave(void) {
	if (E.filename == NULL) {
		if ((E.filename = editorPrompt("Save as: %s")) == NULL) {
			editorSetStatusMsg("Save aborted");
			return;
		}
	}

	int len;
	char *buf = editorRowsToStr(&len);

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		goto err_free_buf;
	}
	if (ftruncate(fd, len) == -1) {
		goto err_close_fd;
	}
	if (write(fd, buf, len) == -1) {
		goto err_close_fd;
	}

	E.dirty = 0;
	close(fd);
	free(buf);
	editorSetStatusMsg("%d bytes written to disk", len);
	return;

err_close_fd:
	close(fd);
err_free_buf:
	free(buf);

	editorSetStatusMsg("Save failed! Error: %s", strerror(errno));
}
