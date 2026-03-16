#include "terminal.h"

#include "buffer.h"

/*** Terminal ***/

#define VT100_CLEAR_SCREEN_4 "\x1b[2J"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"

int editorClearScreen(void) {
	return write(STDOUT_FILENO, VT100_CLEAR_SCREEN_4, 4);
}

int editorResetCursorPos(void) {
	return write(STDOUT_FILENO, VT100_RESET_CURSOR_POS_3, 3);
}

void panic(const char *s) {
	editorClearScreen();
	editorResetCursorPos();

	perror(s);
	exit(EXIT_FAILURE);
}

void setDefaultMode(void) {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_attrs) == -1) {
		panic("tcsetattr");
	}
}

void setRawMode(void) {
	if (tcgetattr(STDIN_FILENO, &E.orig_attrs) == -1) {
		panic("tcgetattr");
	}
	atexit(setDefaultMode);

	struct termios attrs = E.orig_attrs;

	attrs.c_lflag &= ~(
		ECHO |
		ICANON |
		ISIG |
		IEXTEN
	);
	attrs.c_iflag &= ~(
		IXON |
		ICRNL |
		BRKINT |
		INPCK |
		ISTRIP
	);
	attrs.c_oflag &= ~(
		OPOST
	);
	attrs.c_cflag |= (CS8);
	attrs.c_cc[VMIN] = 0;
	attrs.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attrs) == -1) {
		panic("tcsetattr");
	}
}

int editorReadKey(void) {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) {
			panic("read");
		}
	}

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
						case '1':
							return HOME_KEY;
						case '3':
							return DEL_KEY;
						case '4':
							return END_KEY;
						case '5':
							return PAGE_UP;
						case '6':
							return PAGE_DOWN;
						case '7':
							return HOME_KEY;
						case '8':
							return END_KEY;
					}
				}
			}

			switch (seq[1]) {
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
			}
		}

		if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
			}
		}

		return '\x1b';
	}

	return c;
}

int readCursorPosition(int *rows, int *cols) {
	char buf[32];

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
		return -1;
	}
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
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}
		return readCursorPosition(rows, cols);
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;

	return 0;
}
