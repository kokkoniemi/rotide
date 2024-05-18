#include <stddef.h>
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
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ROTIDE_VERSION "0.0.1"
#define ROTIDE_TAB_WIDTH 8

// TODO: split to separate .c and .h files

void editorSetStatusMsg(const char *fmt, ...); 
void editorRefreshScreen();
char *editorPrompt(char *prompt);

struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
};

struct editorConfig {
	int window_rows;
	int window_cols;
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int numrows;
	struct erow *rows;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_attrs;
};

struct editorConfig E;

enum editorKey {
	BACKSPACE = 127,
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
#define VT100_INVERTED_COLORS_4 "\x1b[7m"
#define VT100_NORMAL_COLORS_3 "\x1b[m"


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



static int quit_confirmed = 0;
void quit() {
	if (E.dirty && !quit_confirmed) {
		editorSetStatusMsg("File has unsaved changes. Press Ctrl-Q again to quit");
		quit_confirmed = 1;
		return;
	}

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

#define NEWLINE_CHAR_WIDTH 1

// TODO: would it be more consistent with other functions if this returns the
// length and alters string in place?
char* editorRowsToStr(int *buflen) {
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

int editorRowCxToRx(struct erow *row, int cx) {
	int rx = 0;
	for (int j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') {
			rx += (ROTIDE_TAB_WIDTH - 1) - (rx % ROTIDE_TAB_WIDTH);
		}
		rx++;
	}
	return rx;
}

// TODO: change name. e.g., editorUpdateRowRenderParams etc.
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
	memmove(&E.rows[idx +1], &E.rows[idx], sizeof(struct erow) * (E.numrows - idx));

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

// TODO: rename to editerDelCharAt etc.
void editorDeleteCharAt(struct erow *row, int idx) {
	if (idx < 0 || row->size < idx) {
		return;
	}
	memmove(&row->chars[idx], &row->chars[idx + 1], row->size - idx);
	row->size--;
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

void editorInsertNewline() {
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
		E.cy++;
		return;
	}

	struct erow *row = &E.rows[E.cy];
	editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
	row = &E.rows[E.cy]; // reassign needed because of realloc in editorInsertRow
	row->size = E.cx;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.cy++;
	E.cx = 0;
}

void editorDelChar() {
	if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
		return;
	}
	struct erow *row = &E.rows[E.cy];
	
	if (E.cx > 0) {
		editorDeleteCharAt(row, E.cx - 1);
		E.cx--;
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
		while(llen > 0 && (l[llen - 1] == '\n' || l[llen - 1] == '\r')) {
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

// TODO: Initially, write to temp file and rename that to actual file
// when write succeed
void editorSave() {
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

void editorDrawFileRow(struct writeBuf *wb, size_t i) {
	int len = E.rows[i].rsize - E.coloff;
	if (len < 0) {
		len = 0;
	}
	if (E.window_cols < len) {
		len = E.window_cols;
	}
	wbAppend(wb, &E.rows[i].render[E.coloff], len);
}

void editorDrawRows(struct writeBuf *wb) {
	int y;
	for (y = 0; y < E.window_rows; y++) {
		int y_offset = y + E.rowoff;
		
		if (y_offset < E.numrows) {
			editorDrawFileRow(wb, y_offset);
		} else if (E.numrows == 0 && y == E.window_rows / 3) {
			editorDrawGreeting(wb);
		} else {
			wbAppend(wb, "~", 1);
		}

		wbAppend(wb, VT100_CLEAR_ROW_3, 3);

		wbAppend(wb, "\r\n", 2);
	}
}

// TODO: Draw similar ruler than vim has
void editorDrawStatusBar(struct writeBuf *wb) {
	wbAppend(wb, VT100_INVERTED_COLORS_4, 4);
	char leftbuf[80], rightbuf[80];
	char *filename = E.filename;
	if (filename == NULL) {
		filename = "[No Name]";
	}
	char *dirtyflag = "";
	if (E.dirty) {
		dirtyflag = "[+]";
	}
	
	int llen = snprintf(leftbuf, sizeof(leftbuf), "%.20s %s",
			filename, dirtyflag);
	int rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%",
				E.cy + 1, E.cx + 1, (int)((float)E.cy / (E.numrows - 1) * 100));
	if (llen > E.window_cols) {
		llen = E.window_cols;
	}
	wbAppend(wb, leftbuf, llen);

	for (; llen < E.window_cols - rlen; llen++) {
		wbAppend(wb, " ", 1);
	}

	wbAppend(wb, rightbuf, rlen);
	wbAppend(wb, VT100_NORMAL_COLORS_3, 3);
	wbAppend(wb, "\r\n", 2);
}

void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
	}

	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	} else if (E.cy >= E.rowoff + E.window_rows) {
		E.rowoff = E.cy - E.window_rows + 1;
	}

	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	} else if (E.rx >= E.coloff + E.window_cols) {
		E.coloff = E.rx - E.window_cols + 1;
	}
}

void editorDrawMessageBar(struct writeBuf *wb) {
	wbAppend(wb, VT100_CLEAR_ROW_3, 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.window_cols) {
		msglen = E.window_cols;
	}
	if (msglen && time(NULL) - E.statusmsg_time < 5) {
		wbAppend(wb, E.statusmsg, msglen);
	}
}

// TODO: more beautiful appending vs. writing to stdout
void editorRefreshScreen() {
	editorScroll();

	struct writeBuf wb = WRITEBUF_INIT;

	wbAppend(&wb, VT100_HIDE_CURSOR_6, 6);
	wbAppend(&wb, VT100_RESET_CURSOR_POS_3, 3);
	
	editorDrawRows(&wb);
	editorDrawStatusBar(&wb);
	editorDrawMessageBar(&wb);

	char buf[32];
	int buflen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
			(E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	wbAppend(&wb, buf, (buflen > E.window_cols) ? E.window_cols : buflen);

	wbAppend(&wb, VT100_SHOW_CURSOR_6, 6);

	write(STDOUT_FILENO, wb.b, wb.len);
	wbFree(&wb);
}


/*** Input ***/

// Prevents cursor going past the row end when navigating to shorter line
void editorAlignCursorWithRowEnd() {
	int rowlen = 0;
	if (E.numrows > E.cy) {
		rowlen = E.rows[E.cy].size;
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

void editorMoveCursor(int k) {
	switch (k) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.rows[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (E.numrows > E.cy && E.cx < E.rows[E.cy].size) {
				E.cx++;
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

void editorProcessKeypress() {
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

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (readWindowSize(&E.window_rows, &E.window_cols) == -1) {
		panic("readWindowSize");
	}
	E.window_rows -= 2;
}

int main(int argc, char *argv[]) {
	setRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMsg("Help: Ctrl-S = save; Ctrl-Q = quit");

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();	
	}

	return EXIT_SUCCESS;
}

