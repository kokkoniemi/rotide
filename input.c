#include "input.h"

#include <ctype.h>
#include <stdlib.h>

#include "buffer.h"
#include "output.h"
#include "terminal.h"

/*** Input ***/

static int quit_confirmed = 0;

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
		rowlen = row->size;
		E.cx = editorRowClampCxToClusterBoundary(row, E.cx);
	}
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

char *editorPrompt(char *prompt) {
	size_t bufmax = 128;
	char *buf = malloc(bufmax);

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMsg(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) {
				buflen--;
				buf[buflen] = '\0';
			}
		} else if (c == '\x1b') {
			editorSetStatusMsg("");
			free(buf);
			return NULL;
		} else if (c == '\r' && buflen != 0) {
			editorSetStatusMsg("");
			return buf;
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufmax - 1) {
				bufmax *= 2;
				buf = realloc(buf, bufmax);
			}
			buf[buflen] = c;
			buflen++;
			buf[buflen] = '\0';
		}
	}
}

static void editorMoveCursor(int k) {
	switch (k) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				if (E.cy < E.numrows) {
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
			for (int i = 0; i < E.window_rows; i++) {
				editorMoveCursor(ARROW_UP);
			}
			break;
		case PAGE_DOWN:
			E.cy = E.rowoff + E.window_rows - 1;
			if (E.cy > E.numrows) {
				E.cy = E.numrows;
			}
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
