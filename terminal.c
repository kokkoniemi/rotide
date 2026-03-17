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
#define VT100_ENABLE_MOUSE "\x1b[?1000h\x1b[?1002h\x1b[?1006h"
#define VT100_DISABLE_MOUSE "\x1b[?1000l\x1b[?1002l\x1b[?1006l"
#define OSC52_PLAIN_PREFIX "\x1b]52;c;"
#define OSC52_PLAIN_SUFFIX "\a"
#define OSC52_TMUX_PREFIX "\x1bPtmux;\x1b\x1b]52;c;"
#define OSC52_TMUX_SUFFIX "\a\x1b\\"
#define OSC52_SCREEN_PREFIX "\x1bP\x1b]52;c;"
#define OSC52_SCREEN_SUFFIX "\a\x1b\\"
#define SGR_MOUSE_MAX_PAYLOAD 64

static volatile sig_atomic_t terminal_attrs_captured = 0;
static volatile sig_atomic_t terminal_raw_enabled = 0;
static volatile sig_atomic_t terminal_handlers_installed = 0;
static volatile sig_atomic_t terminal_restore_atexit_registered = 0;
static struct editorMouseEvent pending_mouse_event = {EDITOR_MOUSE_EVENT_NONE, 0, 0};
static int has_pending_mouse_event = 0;

enum editorOsc52Mode {
	EDITOR_OSC52_MODE_AUTO = 0,
	EDITOR_OSC52_MODE_OFF,
	EDITOR_OSC52_MODE_FORCE
};

static int editorWriteAll(int fd, const char *buf, size_t len) {
	while (len > 0) {
		ssize_t written = write(fd, buf, len);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (written == 0) {
			return 0;
		}
		buf += written;
		len -= (size_t)written;
	}
	return 1;
}

static int editorReadSeqByte(char *out) {
	ssize_t nread = read(STDIN_FILENO, out, 1);
	if (nread == 1) {
		return 1;
	}
	if (nread == -1 && errno != EAGAIN) {
		panic("read");
	}
	return 0;
}

static int editorDecodeSgrMousePayload(const char *payload, struct editorMouseEvent *event_out) {
	int cb = 0;
	int cx = 0;
	int cy = 0;
	char suffix = '\0';
	int consumed = 0;
	if (sscanf(payload, "%d;%d;%d%c%n", &cb, &cx, &cy, &suffix, &consumed) != 4) {
		return 0;
	}
	if (payload[consumed] != '\0') {
		return 0;
	}

	event_out->kind = EDITOR_MOUSE_EVENT_NONE;
	event_out->x = cx;
	event_out->y = cy;
	// Parsed packet but unusable coordinates: ignore without treating it as parse failure.
	if (cx <= 0 || cy <= 0) {
		return 1;
	}
	if (cb & ~((3) | (4 | 8 | 16) | 32 | 64)) {
		return 1;
	}

	int button = cb & 0x03;
	int has_modifiers = cb & (4 | 8 | 16);
	int has_motion = cb & 32;
	int has_wheel = cb & 64;
	if (has_modifiers) {
		return 1;
	}

	// SGR uses lowercase 'm' for release.
	if (suffix == 'm') {
		if (!has_motion && !has_wheel && button == 0) {
			event_out->kind = EDITOR_MOUSE_EVENT_LEFT_RELEASE;
		}
		return 1;
	}
	if (suffix != 'M') {
		return 1;
	}

	// Wheel events set bit 6 and encode direction in the low two bits.
	if (has_wheel) {
		int wheel_button = button;
		if (wheel_button == 0) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_UP;
		} else if (wheel_button == 1) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_DOWN;
		}
		return 1;
	}

	if (has_motion) {
		if (button == 0) {
			event_out->kind = EDITOR_MOUSE_EVENT_LEFT_DRAG;
		}
		return 1;
	}

	if (button == 0) {
		event_out->kind = EDITOR_MOUSE_EVENT_LEFT_PRESS;
	}
	return 1;
}

static int editorReadSgrMouseEvent(struct editorMouseEvent *event_out) {
	char payload[SGR_MOUSE_MAX_PAYLOAD];
	int payload_len = 0;
	char term = '\0';

	// Bound payload length so malformed streams cannot grow indefinitely.
	while (payload_len < (int)sizeof(payload) - 1) {
		if (!editorReadSeqByte(&term)) {
			return 0;
		}
		payload[payload_len++] = term;
		if (term == 'M' || term == 'm') {
			break;
		}
	}

	if (payload_len == (int)sizeof(payload) - 1 && term != 'M' && term != 'm') {
		return 0;
	}

	payload[payload_len] = '\0';
	return editorDecodeSgrMousePayload(payload, event_out);
}

static char *editorBase64Encode(const unsigned char *bytes, int len, size_t *out_len) {
	static const char base64_table[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (len < 0 || out_len == NULL) {
		return NULL;
	}

	size_t encoded_len = ((size_t)len + 2) / 3 * 4;
	char *encoded = malloc(encoded_len + 1);
	if (encoded == NULL) {
		return NULL;
	}

	size_t out_idx = 0;
	for (int i = 0; i < len; i += 3) {
		int remaining = len - i;
		unsigned int octet_a = bytes[i];
		unsigned int octet_b = remaining > 1 ? bytes[i + 1] : 0;
		unsigned int octet_c = remaining > 2 ? bytes[i + 2] : 0;

		encoded[out_idx++] = base64_table[(octet_a >> 2) & 0x3F];
		encoded[out_idx++] = base64_table[((octet_a & 0x03) << 4) | ((octet_b >> 4) & 0x0F)];
		encoded[out_idx++] = remaining > 1 ?
				base64_table[((octet_b & 0x0F) << 2) | ((octet_c >> 6) & 0x03)] : '=';
		encoded[out_idx++] = remaining > 2 ? base64_table[octet_c & 0x3F] : '=';
	}

	encoded[out_idx] = '\0';
	*out_len = out_idx;
	return encoded;
}

static enum editorOsc52Mode editorGetOsc52Mode(void) {
	const char *mode = getenv("ROTIDE_OSC52");
	if (mode == NULL || mode[0] == '\0' || strcmp(mode, "auto") == 0) {
		return EDITOR_OSC52_MODE_AUTO;
	}
	if (strcmp(mode, "off") == 0) {
		return EDITOR_OSC52_MODE_OFF;
	}
	if (strcmp(mode, "force") == 0) {
		return EDITOR_OSC52_MODE_FORCE;
	}
	return EDITOR_OSC52_MODE_AUTO;
}

static int editorCanUseOsc52(enum editorOsc52Mode mode, int len) {
	if (mode == EDITOR_OSC52_MODE_OFF) {
		return 0;
	}
	if (len > ROTIDE_OSC52_MAX_COPY_BYTES) {
		return 0;
	}
	if (mode == EDITOR_OSC52_MODE_FORCE) {
		return 1;
	}
	if (!isatty(STDOUT_FILENO)) {
		return 0;
	}
	const char *term = getenv("TERM");
	if (term != NULL && strcmp(term, "dumb") == 0) {
		return 0;
	}
	return 1;
}

void editorClipboardSyncOsc52(const char *text, int len) {
	if (len < 0 || (len > 0 && text == NULL)) {
		return;
	}

	enum editorOsc52Mode mode = editorGetOsc52Mode();
	if (!editorCanUseOsc52(mode, len)) {
		return;
	}

	size_t encoded_len = 0;
	char *encoded = editorBase64Encode((const unsigned char *)text, len, &encoded_len);
	if (encoded == NULL) {
		return;
	}

	const char *tmux = getenv("TMUX");
	const char *screen = getenv("STY");

	if (tmux != NULL && tmux[0] != '\0') {
		(void)editorWriteAll(STDOUT_FILENO, OSC52_TMUX_PREFIX, sizeof(OSC52_TMUX_PREFIX) - 1);
		(void)editorWriteAll(STDOUT_FILENO, encoded, encoded_len);
		(void)editorWriteAll(STDOUT_FILENO, OSC52_TMUX_SUFFIX, sizeof(OSC52_TMUX_SUFFIX) - 1);
		free(encoded);
		return;
	}

	if (screen != NULL && screen[0] != '\0') {
		(void)editorWriteAll(STDOUT_FILENO, OSC52_SCREEN_PREFIX, sizeof(OSC52_SCREEN_PREFIX) - 1);
		(void)editorWriteAll(STDOUT_FILENO, encoded, encoded_len);
		(void)editorWriteAll(STDOUT_FILENO, OSC52_SCREEN_SUFFIX, sizeof(OSC52_SCREEN_SUFFIX) - 1);
		free(encoded);
		return;
	}

	(void)editorWriteAll(STDOUT_FILENO, OSC52_PLAIN_PREFIX, sizeof(OSC52_PLAIN_PREFIX) - 1);
	(void)editorWriteAll(STDOUT_FILENO, encoded, encoded_len);
	(void)editorWriteAll(STDOUT_FILENO, OSC52_PLAIN_SUFFIX, sizeof(OSC52_PLAIN_SUFFIX) - 1);
	free(encoded);
}

int editorConsumeMouseEvent(struct editorMouseEvent *out) {
	if (out == NULL || !has_pending_mouse_event) {
		return 0;
	}

	*out = pending_mouse_event;
	pending_mouse_event.kind = EDITOR_MOUSE_EVENT_NONE;
	pending_mouse_event.x = 0;
	pending_mouse_event.y = 0;
	has_pending_mouse_event = 0;
	return 1;
}

static void editorRestoreCursorVisualState(void) {
	(void)editorWriteAll(STDOUT_FILENO, VT100_CURSOR_DEFAULT_5, sizeof(VT100_CURSOR_DEFAULT_5) - 1);
	(void)editorWriteAll(STDOUT_FILENO, VT100_SHOW_CURSOR_6, sizeof(VT100_SHOW_CURSOR_6) - 1);
}

static void editorRestoreTerminalInternal(void) {
	if (terminal_attrs_captured && terminal_raw_enabled) {
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_attrs) == 0) {
			terminal_raw_enabled = 0;
		}
	}
	(void)editorWriteAll(STDOUT_FILENO, VT100_DISABLE_MOUSE, sizeof(VT100_DISABLE_MOUSE) - 1);
	editorRestoreCursorVisualState();
	// Drop any queued event so a later key read cannot consume stale mouse data.
	pending_mouse_event.kind = EDITOR_MOUSE_EVENT_NONE;
	pending_mouse_event.x = 0;
	pending_mouse_event.y = 0;
	has_pending_mouse_event = 0;
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
	// Mouse enable is best-effort: unsupported terminals simply ignore the control sequence.
	(void)editorWriteAll(STDOUT_FILENO, VT100_ENABLE_MOUSE, sizeof(VT100_ENABLE_MOUSE) - 1);
	terminal_raw_enabled = 1;
	editorInstallTerminationHandlers();
}

int editorReadKey(void) {
	while (1) {
		int nread;
		char c;
		while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
			if (nread == -1 && errno != EAGAIN) {
				panic("read");
			}
		}

		if (c != '\x1b') {
			return c;
		}

		char first = '\0';
		char second = '\0';
		// Parse common ANSI escape sequences used by arrow/home/end/page keys.
		// If the sequence is incomplete, treat it as a plain Escape keypress.
		if (!editorReadSeqByte(&first)) {
			return '\x1b';
		}
		if (!editorReadSeqByte(&second)) {
			return '\x1b';
		}

		if (first == '[') {
			if (second == '<') {
				struct editorMouseEvent event;
				if (!editorReadSgrMouseEvent(&event)) {
					return '\x1b';
				}
				if (event.kind == EDITOR_MOUSE_EVENT_NONE) {
					// Valid mouse packet we intentionally ignore (release/drag/unsupported button).
					continue;
				}
				pending_mouse_event = event;
				has_pending_mouse_event = 1;
				return MOUSE_EVENT;
			}

			if (second >= '0' && second <= '9') {
				char third = '\0';
				if (!editorReadSeqByte(&third)) {
					return '\x1b';
				}
				if (third == '~') {
					switch (second) {
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

			switch (second) {
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

		if (first == 'O') {
			switch (second) {
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
			}
		}

		return '\x1b';
	}
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
