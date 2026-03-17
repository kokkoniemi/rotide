#include "input.h"

#include "alloc.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>

#include "buffer.h"
#include "output.h"
#include "terminal.h"

/*** Input ***/

static int quit_confirmed = 0;

static size_t editorPromptPrevDeleteIdx(const char *buf, size_t buflen) {
	if (buflen == 0) {
		return 0;
	}

	size_t seq_start = buflen - 1;
	while (seq_start > 0 &&
			editorIsUtf8ContinuationByte((unsigned char)buf[seq_start])) {
		seq_start--;
	}

	unsigned int cp = 0;
	int seq_len = editorUtf8DecodeCodepoint(&buf[seq_start], (int)(buflen - seq_start), &cp);
	if (seq_len > 1 && seq_start + (size_t)seq_len == buflen) {
		return seq_start;
	}

	return buflen - 1;
}

static void quit(void) {
	if (E.dirty && !quit_confirmed) {
		editorSetStatusMsg("File has unsaved changes. Press Ctrl-Q again to quit");
		quit_confirmed = 1;
		return;
	}

	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_SUCCESS);
}

static void editorAlignCursorWithRowEnd(void) {
	int rowlen = 0;
	if (E.numrows > E.cy) {
		struct erow *row = &E.rows[E.cy];
		// Never leave the cursor in the middle of a UTF-8 grapheme.
		rowlen = row->size;
		E.cx = editorRowClampCxToClusterBoundary(row, E.cx);
	}
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

char *editorPrompt(char *prompt) {
	size_t bufmax = 128;
	char *buf = editorMalloc(bufmax);
	if (buf == NULL) {
		editorSetStatusMsg("Out of memory");
		return NULL;
	}

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMsg(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) {
				buflen = editorPromptPrevDeleteIdx(buf, buflen);
				buf[buflen] = '\0';
			}
		} else if (c == '\x1b') {
			editorSetStatusMsg("");
			free(buf);
			return NULL;
		} else if (c == '\r' && buflen != 0) {
			editorSetStatusMsg("");
			return buf;
		} else if (c >= CHAR_MIN && c <= CHAR_MAX) {
			unsigned char byte = (unsigned char)c;
			// Keep non-ASCII bytes verbatim; only filter ASCII control bytes.
			if (byte >= 0x80 || !iscntrl(byte)) {
				if (buflen == bufmax - 1) {
					size_t new_bufmax = bufmax * 2;
					char *new_buf = editorRealloc(buf, new_bufmax);
					if (new_buf == NULL) {
						free(buf);
						editorSetStatusMsg("Out of memory");
						return NULL;
					}
					buf = new_buf;
					bufmax = new_bufmax;
				}
				buf[buflen] = (char)byte;
				buflen++;
				buf[buflen] = '\0';
			}
		}
	}
}

static void editorMoveCursor(int k) {
	int target_rx = 0;
	if ((k == ARROW_UP || k == ARROW_DOWN) && E.cy < E.numrows) {
		target_rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}

	switch (k) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				if (E.cy < E.numrows) {
					// Step by grapheme cluster instead of byte index.
					E.cx = editorRowPrevClusterIdx(&E.rows[E.cy], E.cx);
				} else {
					E.cx--;
				}
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.rows[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (E.numrows > E.cy && E.cx < E.rows[E.cy].size) {
				// Step by grapheme cluster instead of byte index.
				E.cx = editorRowNextClusterIdx(&E.rows[E.cy], E.cx);
			} else if (E.numrows > E.cy && E.cx == E.rows[E.cy].size) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) {
				E.cy++;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
	}

	if ((k == ARROW_UP || k == ARROW_DOWN) && E.cy < E.numrows) {
		E.cx = editorRowRxToCx(&E.rows[E.cy], target_rx);
	}

	editorAlignCursorWithRowEnd();
}

void editorProcessKeypress(void) {
	int c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			quit();
			return;
		case CTRL_KEY('s'):
			editorSave();
			break;
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if (E.cy < E.numrows) {
				E.cx = E.rows[E.cy].size;
			}
			break;
		case PAGE_UP:
			E.cy = E.rowoff;
			// Reuse arrow movement so cursor clamping behavior stays consistent.
			for (int i = 0; i < E.window_rows; i++) {
				editorMoveCursor(ARROW_UP);
			}
			break;
		case PAGE_DOWN:
			E.cy = E.rowoff + E.window_rows - 1;
			if (E.cy > E.numrows) {
				E.cy = E.numrows;
			}
			// Reuse arrow movement so cursor clamping behavior stays consistent.
			for (int i = 0; i < E.window_rows; i++) {
				editorMoveCursor(ARROW_DOWN);
			}
			break;
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
		case '\r':
			editorInsertNewline();
			break;
		case '\x1b':
		case CTRL_KEY('l'):
			break;
		case DEL_KEY:
			editorMoveCursor(ARROW_RIGHT);
			[[fallthrough]];
		case BACKSPACE:
		case CTRL_KEY('h'):
			editorDelChar();
			break;
		default:
			editorInsertChar(c);
			break;
	}

	quit_confirmed = 0;
}
