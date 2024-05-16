#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct editorConfig {
	int windowRows;
	int windowCols;
	struct termios orig_attrs;
};

struct editorConfig E;

int editorClearScreen() {
	return write(STDOUT_FILENO, "\x1b[2J", 4);
}

int editorResetCursorPos() {
	return write(STDOUT_FILENO, "\x1b[H", 3);
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

char editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) {
			panic("read");
		}
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

void editorDrawRows() {
	int y;
	for (y = 0; y < E.windowRows; y++) {
		write(STDOUT_FILENO, "~", 1);
		if (y < E.windowRows - 1) {
			write(STDOUT_FILENO, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	editorClearScreen();
	editorResetCursorPos();
	
	editorDrawRows();

	editorResetCursorPos();
}


void editorProcessKeypress() {
	char c = editorReadKey();
	
	switch (c) {
		case CTRL_KEY('q'):
			quit();
			break;
	}
}

void initEditor() {
	if (readWindowSize(&E.windowRows, &E.windowCols) == -1) {
		panic("readWindowSize");
	}
}

int main() {
	setRawMode();
	initEditor();

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();	
	}

	return EXIT_SUCCESS;
}

