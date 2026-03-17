#include "terminal.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*** Terminal ***/

#define VT100_CLEAR_SCREEN_4 "\x1b[2J"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"
#define VT100_SHOW_CURSOR_6 "\x1b[?25h"
#define VT100_CURSOR_DEFAULT_5 "\x1b[0 q"

static volatile sig_atomic_t terminal_attrs_captured = 0;
static volatile sig_atomic_t terminal_raw_enabled = 0;
static volatile sig_atomic_t terminal_handlers_installed = 0;
static volatile sig_atomic_t terminal_restore_atexit_registered = 0;

static void editorRestoreCursorVisualState(void) {
	(void)write(STDOUT_FILENO, VT100_CURSOR_DEFAULT_5, 5);
	(void)write(STDOUT_FILENO, VT100_SHOW_CURSOR_6, 6);
}

static void editorRestoreTerminalInternal(void) {
	if (terminal_attrs_captured && terminal_raw_enabled) {
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_attrs) == 0) {
			terminal_raw_enabled = 0;
		}
	}
	editorRestoreCursorVisualState();
}

static void editorRestoreTerminalAtExit(void) {
	editorRestoreTerminalInternal();
}

static void editorHandleTerminationSignal(int signo) {
	editorRestoreTerminalInternal();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(signo, &sa, NULL);
	(void)raise(signo);
	_exit(128 + signo);
}

static void editorInstallTerminationHandlers(void) {
	if (terminal_handlers_installed) {
		return;
	}

	const int signals[] = {SIGHUP, SIGINT, SIGTERM, SIGQUIT};
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = editorHandleTerminationSignal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
		(void)sigaction(signals[i], &sa, NULL);
	}

	terminal_handlers_installed = 1;
}

int editorClearScreen(void) {
	return write(STDOUT_FILENO, VT100_CLEAR_SCREEN_4, 4);
}

int editorResetCursorPos(void) {
	return write(STDOUT_FILENO, VT100_RESET_CURSOR_POS_3, 3);
}

void editorRestoreTerminal(void) {
	editorRestoreTerminalInternal();
}

void panic(const char *s) {
	editorRestoreTerminalInternal();
	editorClearScreen();
	editorResetCursorPos();

	perror(s);
	exit(EXIT_FAILURE);
}

void setDefaultMode(void) {
	editorRestoreTerminalInternal();
}

void setRawMode(void) {
	if (terminal_raw_enabled) {
		return;
	}

	if (tcgetattr(STDIN_FILENO, &E.orig_attrs) == -1) {
		panic("tcgetattr");
	}
	terminal_attrs_captured = 1;
	// Always restore terminal settings on exit so the shell stays usable.
	if (!terminal_restore_atexit_registered) {
		if (atexit(editorRestoreTerminalAtExit) == 0) {
			terminal_restore_atexit_registered = 1;
		}
	}

	struct termios attrs = E.orig_attrs;

	// lflag: disable cooked-mode line editing and signal-generating shortcuts.
	attrs.c_lflag &= ~(
		ECHO |
		ICANON |
		ISIG |
		IEXTEN
	);
	// iflag: keep byte stream unmodified (no flow control or CR/LF rewriting).
	attrs.c_iflag &= ~(
		IXON |
		ICRNL |
		BRKINT |
		INPCK |
		ISTRIP
	);
	// oflag: disable post-processing so writes are emitted exactly as provided.
	attrs.c_oflag &= ~(
		OPOST
	);
	// cflag: force 8-bit bytes and non-blocking-ish reads with short timeout.
	attrs.c_cflag |= (CS8);
	attrs.c_cc[VMIN] = 0;
	attrs.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attrs) == -1) {
		panic("tcsetattr");
	}
	terminal_raw_enabled = 1;
	editorInstallTerminationHandlers();
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

		// Parse common ANSI escape sequences used by arrow/home/end/page keys.
		// If the sequence is incomplete, treat it as a plain Escape keypress.
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
	enum { CURSOR_POS_MAX_RESPONSE = 31 };
	size_t i = 0;
	char c = '\0';
	int row = 0;
	int col = 0;
	int saw_row_digit = 0;
	int saw_col_digit = 0;
	int phase = 0;

	// Ask terminal for cursor position: ESC [ rows ; cols R
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
		return -1;
	}
	for (; i < CURSOR_POS_MAX_RESPONSE; i++) {
		if (read(STDIN_FILENO, &c, 1) != 1) {
			return -1;
		}

		switch (phase) {
			case 0:
				if (c != '\x1b') {
					return -1;
				}
				phase = 1;
				break;
			case 1:
				if (c != '[') {
					return -1;
				}
				phase = 2;
				break;
			case 2:
				if (c >= '0' && c <= '9') {
					int digit = c - '0';
					if (row > (INT_MAX - digit) / 10) {
						return -1;
					}
					row = row * 10 + digit;
					saw_row_digit = 1;
					break;
				}
				if (c == ';' && saw_row_digit) {
					phase = 3;
					break;
				}
				return -1;
			default:
				if (c >= '0' && c <= '9') {
					int digit = c - '0';
					if (col > (INT_MAX - digit) / 10) {
						return -1;
					}
					col = col * 10 + digit;
					saw_col_digit = 1;
					break;
				}
				if (c == 'R' && saw_col_digit) {
					*rows = row;
					*cols = col;
					return 0;
				}
				return -1;
		}
	}

	return -1;
}

int readWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// Fallback for terminals where TIOCGWINSZ is unavailable or unset:
		// move cursor to bottom-right and query resulting coordinates.
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}
		return readCursorPosition(rows, cols);
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;

	return 0;
}
