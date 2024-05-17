#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ROTIDE_VERSION "0.0.1"


struct erow {
	int size;
	char *chars;
};

struct editorConfig {
	int window_rows;
	int window_cols;
	int cx;
	int cy;
	int numrows;
	struct erow *rows;
	struct termios orig_attrs;
};

struct editorConfig E;

enum editorKey {
	ARROW_LEFT = 90000,
	ARROW_DOWN,
	ARROW_UP,
	ARROW_RIGHT,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY
};

/*** Terminal ***/

#define VT100_CLEAR_SCREEN_4  "\x1b[2J"
#define VT100_CLEAR_ROW_3 "\x1b[K"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"
#define VT100_HIDE_CURSOR_6 "\x1b[?25l"
#define VT100_SHOW_CURSOR_6 "\x1b[?25h"

int editorClearScreen() {
	return write(STDOUT_FILENO, VT100_CLEAR_SCREEN_4, 4);
}

int editorResetCursorPos() {
	return write(STDOUT_FILENO, VT100_RESET_CURSOR_POS_3, 3);
}

void panic(const char *s) {
	editorClearScreen();
	editorResetCursorPos();

	perror(s);
	exit(EXIT_FAILURE);
}

void quit() {
	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_SUCCESS);
}


void setDefaultMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_attrs) == -1) {
		panic("tcsetattr");	
	}
}

void setRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_attrs) == -1) {
		panic("tcgetattr");
	}
	atexit(setDefaultMode);

	struct termios attrs = E.orig_attrs;

	attrs.c_lflag &= ~(
		ECHO |    // input is not echoed back
		ICANON |  // read by bits instead of lines
		ISIG |    // disable signals (SIGINT, SIGTERM, SIGTSTP etc.)
		IEXTEN	  // disable implementation defined input controlling.
	);
	attrs.c_iflag &= ~(
		IXON |    // disable start/stop control (C-s, C-q) 
		ICRNL |   // disable CR to NL mapping
		BRKINT |  // disable signal interrupt on break condition
		INPCK |	  // disable parity checking 
		ISTRIP	  // disable input stripping
	);
	attrs.c_oflag &= ~(
		OPOST     // disable output processing
	);
	attrs.c_cflag |= (CS8); // set character size to eight bits
	attrs.c_cc[VMIN] = 0;   // set min number of bytes for read()
	attrs.c_cc[VTIME] = 1;  // set max time before read() returns
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attrs) == -1) {
		panic("tcsetattr");
	};
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) {
			panic("read");
		}
	}

	// arrow keys and page up/down
	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) {
			return '\x1b';
		}
		if (read(STDIN_FILENO, &seq[1], 1) != 1) {
			return '\x1b';
		}

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) {
					return '\x1b';
				}
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1':    // seq "<esc>[1~"
							return HOME_KEY;
						case '3':    // seq "<esc>[3~"
							return DEL_KEY;
						case '4':    // seq "<esc>[4~"
							return END_KEY;
						case '5':    // seq "<esc>[5~"
							return PAGE_UP;
						case '6':    // seq "<esc>[6~"
							return PAGE_DOWN;
						case '7':    // seq "<esc>[7~"
							return HOME_KEY;
						case '8':    // seq "<esc>[8~"
							return END_KEY;
					}
				}
			}

			switch (seq[1]) {
				case 'A':    // seq "<esc>[A"
					return ARROW_UP;
				case 'B':    // seq "<esc>[B"
					return ARROW_DOWN;
				case 'C':    // seq "<esc>[C"
					return ARROW_RIGHT;
				case 'D':    // seq "<esc>[D"
					return ARROW_LEFT;
				case 'H':    // seq "<esc>[H"
					return HOME_KEY;
				case 'F':    // seq "<esc>[F"
					return END_KEY;
			}
		}

		if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': // seq "<esc>OH"
					return HOME_KEY;
				case 'F': // seq "<esc>OF"
					return END_KEY;
			}
		}

		return '\x1b';
	}

	return c;
}

int readCursorPosition(int *rows, int *cols) {
	char buf[32];
	
	// write vt100 cursor position to stdout
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
		return -1;
	}
	// position is in form: "<esc>[24;80R"
	for (unsigned int i = 0; i < sizeof(buf) - 1; i++) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) {
			break;
		}
		if (buf[i] == 'R') {
			buf[i] = '\0';
			break;
		}
	}
	if (buf[0] != '\x1b' || buf[1] != '[') {
		return -1;
	}
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
		return -1;
	}

	return 0;
}

int readWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// if ioctl fails, set cursor pos to bottom right and read manually
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}
		return readCursorPosition(rows, cols);
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;

	return 0;
}

/*** File io ***/

void editorAppendRow(char *s, size_t len) {
	E.rows = realloc(E.rows, sizeof(struct erow) * (E.numrows + 1));
	
	int i = E.numrows;	
	E.rows[i].size = len;
	E.rows[i].chars = malloc(len + 1);
	memcpy(E.rows[i].chars, s, len);
	E.rows[i].chars[len] = '\0';
	E.numrows++;
}

void editorOpen(char *filename) {
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		panic("fopen");
	}

	char *l = NULL;
	size_t lcap = 0;
	ssize_t llen;
	while ((llen = getline(&l, &lcap, fp)) != -1) {
		while(llen > 0 && (l[llen - 1] == '\n' || l[llen - 1] == '\r')) {
			llen--;
		}

		editorAppendRow(l, llen);
	}

	free(l);
	fclose(fp);
}

/*** Write buffer ***/

struct writeBuf {
	char *b;
	int len;	
};

#define WRITEBUF_INIT {NULL, 0}

int wbAppend(struct writeBuf *wb, const char *s, int len) {
	char *new = realloc(wb->b, wb->len + len);

	if (new == NULL) {
		return 0;
	}

	memcpy(&new[wb->len], s, len);
	wb->b = new;
	wb->len += len;

	return len;
}

void wbFree(struct writeBuf *wb) {
	free(wb->b);
}

/*** Output ***/

void editorDrawGreeting(struct writeBuf *wb) {
	char greet[80];
	int greetlen = snprintf(greet, sizeof(greet),
				"RotIDE editor - version %s", ROTIDE_VERSION);
	if (greetlen > E.window_cols) {
		greetlen = E.window_cols;
	}
	int pad = (E.window_cols - greetlen) / 2;
	if (pad) {
		wbAppend(wb, "~", 1);
		pad--;
	}
	while(pad--) {
		wbAppend(wb, " ", 1);
	}
	wbAppend(wb, greet, greetlen);
}

void editorDrawRows(struct writeBuf *wb) {
	int y;
	for (y = 0; y < E.window_rows; y++) {
		if (y < E.numrows) {
			wbAppend(
				wb,
				E.rows[y].chars,
				(E.rows[y].size > E.window_cols) ? E.window_cols : E.rows[y].size
			);
		} else if (E.numrows == 0 && y == E.window_rows / 3) {
			editorDrawGreeting(wb);
		} else {
			wbAppend(wb, "~", 1);
		}

		wbAppend(wb, VT100_CLEAR_ROW_3, 3);

		if (y < E.window_rows - 1) {
			wbAppend(wb, "\r\n", 2);
		}
	}
}

// TODO: more beautiful appending vs. writing to stdout
void editorRefreshScreen() {
	struct writeBuf wb = WRITEBUF_INIT;

	wbAppend(&wb, VT100_HIDE_CURSOR_6, 6);
	wbAppend(&wb, VT100_RESET_CURSOR_POS_3, 3);
	
	editorDrawRows(&wb);

	char buf[32];
	int buflen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	wbAppend(&wb, buf, (buflen > E.window_cols) ? E.window_cols : buflen);

	wbAppend(&wb, VT100_SHOW_CURSOR_6, 6);

	write(STDOUT_FILENO, wb.b, wb.len);
	wbFree(&wb);
}

/*** Input ***/

void editorMoveCursor(int k) {
	switch (k) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
			if (E.cx != E.window_cols - 1) {
				E.cx++;
			}
			break;
		case ARROW_DOWN:
			if (E.cy != E.window_rows - 1) {
				E.cy++;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
	}
}

void editorProcessKeypress() {
	int c = editorReadKey();
	
	switch (c) {
		case CTRL_KEY('q'):
			quit();
			break;
		case HOME_KEY:
			E.cy = 0;
			break;
		case END_KEY:
			E.cy = E.window_rows - 1;
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			for (int i = 0; i < E.window_rows; i++) {
				editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.numrows = 0;
	E.rows = NULL;

	if (readWindowSize(&E.window_rows, &E.window_cols) == -1) {
		panic("readWindowSize");
	}
}

int main(int argc, char *argv[]) {
	setRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();	
	}

	return EXIT_SUCCESS;
}

