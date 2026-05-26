#include "support/terminal.h"

#include "debug/dap.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "language/syntax_worker.h"
#include "rotide.h"
#include "support/perf_trace.h"
#include "support/size_utils.h"
#include "terminal/terminal_pane.h"
#include "workspace/task.h"
#include "workspace/watch.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <termios.h>

/*** Terminal ***/

#define VT100_CLEAR_SCREEN_4 "\x1b[2J"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"
#define VT100_NORMAL_COLORS_3 "\x1b[m"
#define VT100_SHOW_CURSOR_6 "\x1b[?25h"
#define VT100_CURSOR_DEFAULT_5 "\x1b[0 q"
#define VT100_CURSOR_COLOR_DEFAULT "\x1b]112\x07"
#define VT100_ENABLE_MOUSE "\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h"
#define VT100_DISABLE_MOUSE "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l"
#define OSC52_PLAIN_PREFIX "\x1b]52;c;"
#define OSC52_PLAIN_SUFFIX "\a"
#define OSC52_TMUX_PREFIX "\x1bPtmux;\x1b\x1b]52;c;"
#define OSC52_TMUX_SUFFIX "\a\x1b\\"
#define OSC52_SCREEN_PREFIX "\x1bP\x1b]52;c;"
#define OSC52_SCREEN_SUFFIX "\a\x1b\\"
#define SGR_MOUSE_MAX_PAYLOAD 64

static volatile sig_atomic_t g_terminal_attrs_captured = 0;
static volatile sig_atomic_t g_terminal_raw_enabled = 0;
static volatile sig_atomic_t g_terminal_handlers_installed = 0;
static volatile sig_atomic_t g_terminal_resize_handler_installed = 0;
static volatile sig_atomic_t g_terminal_atexit_registered = 0;
static volatile sig_atomic_t g_terminal_resize_pending = 0;
static struct editorMouseEvent g_terminal_pending_mouse_event = {EDITOR_MOUSE_EVENT_NONE, 0, 0, 0};
static int g_terminal_has_pending_mouse_event = 0;

enum terminalOsc52Mode {
	TERMINAL_OSC52_MODE_AUTO = 0,
	TERMINAL_OSC52_MODE_OFF,
	TERMINAL_OSC52_MODE_FORCE
};

static int terminalWriteAll(int fd, const char *buf, size_t len) {
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

enum terminalReadByteResult {
	TERMINAL_READ_BYTE = 1,
	TERMINAL_READ_RETRY = 0,
	TERMINAL_READ_EOF = -1
};

enum { TERMINAL_KEY_CONTINUE = -1 };

static int terminalIsTtyInputClosed(void) {
	struct pollfd pfd;
	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	pfd.revents = 0;

	int polled;
	do {
		polled = poll(&pfd, 1, 0);
	} while (polled == -1 && errno == EINTR);

	if (polled == -1) {
		editorPanic("poll");
		return 1;
	}
	if (polled == 0) {
		return 0;
	}
	return (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
}

static enum terminalReadByteResult terminalReadInputByte(char *out) {
	ssize_t nread = read(STDIN_FILENO, out, 1);
	if (nread == 1) {
		return TERMINAL_READ_BYTE;
	}
	if (nread == 0) {
		if (!isatty(STDIN_FILENO)) {
			return TERMINAL_READ_EOF;
		}
		return terminalIsTtyInputClosed() ? TERMINAL_READ_EOF : TERMINAL_READ_RETRY;
	}
	if (nread == -1 && errno != EAGAIN && errno != EINTR) {
		editorPanic("read");
		return TERMINAL_READ_RETRY;
	}
	return TERMINAL_READ_RETRY;
}

static enum terminalReadByteResult terminalReadSeqByte(char *out) {
	return terminalReadInputByte(out);
}

static int terminalTakeResizeEvent(void) {
	if (!g_terminal_resize_pending) {
		return 0;
	}
	g_terminal_resize_pending = 0;
	return 1;
}

/* How long poll() may sleep before we re-check the soft-work pollers (syntax /
 * task / watch). They have no fd we can wait on; this interval is the upper
 * bound on how stale their results can get when nothing else wakes the loop. */
#define TERMINAL_SOFT_WORK_POLL_MS 50

/* Upper bound on pollfd entries (stdin + terminal panes), sized to keep the
 * pollfd[] on the stack. */
#define TERMINAL_POLL_FDS_CAP 64

/* Minimum wall-clock gap between consecutive full-screen refreshes when the
 * wake is driven by terminal-pane output (~125 FPS). Below this, we coalesce
 * further PTY output into the same frame instead of returning a flurry of
 * TERMINAL_EVENTs that each trigger a redraw. */
#define TERMINAL_MIN_FRAME_INTERVAL_MS 8

/* Last monotonic ms at which the editor finished a full refresh, or -1 if no
 * refresh has happened yet. Updated by editorMarkFrameRendered. */
static long g_terminal_last_frame_ms = -1;

static long terminalMonotonicMs(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

void editorMarkFrameRendered(void) {
	g_terminal_last_frame_ms = terminalMonotonicMs();
}

static int terminalPumpAllWithPerf(void) {
	long t0 = editorPerfEnabled() ? editorPerfMonotonicUs() : 0;
	int bytes = editorTerminalPanePumpAll(E.layout_root);
	if (editorPerfEnabled()) {
		editorPerfRecordPumpUs(editorPerfMonotonicUs() - t0);
		editorPerfRecordPumpBytes(bytes);
	}
	return bytes;
}

/* Block in poll() until stdin, any PTY master, or SIGWINCH wakes us, with at
 * most TERMINAL_SOFT_WORK_POLL_MS sleep so the soft-work pollers stay live.
 * Returns when either input is ready or the timeout fires; the caller loops
 * back and re-checks every source. */
static void terminalWaitForInput(int timeout_ms) {
	struct pollfd pfds[TERMINAL_POLL_FDS_CAP];
	nfds_t nfds = 0;
	pfds[nfds].fd = STDIN_FILENO;
	pfds[nfds].events = POLLIN;
	pfds[nfds].revents = 0;
	nfds++;
	int pty_fds[TERMINAL_POLL_FDS_CAP];
	int pty_count =
	        editorTerminalPaneCollectMasterFds(E.layout_root, pty_fds, TERMINAL_POLL_FDS_CAP);
	int pty_use = pty_count;
	if (pty_use > TERMINAL_POLL_FDS_CAP - 1) {
		pty_use = TERMINAL_POLL_FDS_CAP - 1;
	}
	for (int i = 0; i < pty_use; i++) {
		pfds[nfds].fd = pty_fds[i];
		pfds[nfds].events = POLLIN;
		pfds[nfds].revents = 0;
		nfds++;
	}
	int r;
	do {
		r = poll(pfds, nfds, timeout_ms);
	} while (r == -1 && errno == EINTR);
	if (r == -1) {
		editorPanic("poll");
	}
	if (r > 0) {
		editorPerfRecordFdsReady(r);
	}
}

static int terminalDecodeSgrMousePayload(const char *payload, struct editorMouseEvent *event_out) {
	int cb = 0;
	int cx = 0;
	int cy = 0;
	char suffix = '\0';
	int consumed = 0;
	if (sscanf(payload, "%d;%d;%d%c%n", &cb, &cx, &cy, &suffix, &consumed) != 4) { // NOLINT(cert-err34-c)
		return 0;
	}
	if (payload[consumed] != '\0') {
		return 0;
	}

	event_out->kind = EDITOR_MOUSE_EVENT_NONE;
	event_out->x = cx;
	event_out->y = cy;
	event_out->modifiers = EDITOR_MOUSE_MOD_NONE;
	// Parsed packet but unusable coordinates: ignore without treating it as parse failure.
	if (cx <= 0 || cy <= 0) {
		return 1;
	}
	if (cb & ~((3) | (4 | 8 | 16) | 32 | 64)) {
		return 1;
	}

	int button = cb & 0x03;
	int has_shift = cb & 4;
	int has_alt = cb & 8;
	int has_ctrl = cb & 16;
	int has_motion = cb & 32;
	int has_wheel = cb & 64;
	if (has_shift) {
		event_out->modifiers |= EDITOR_MOUSE_MOD_SHIFT;
	}
	if (has_alt) {
		event_out->modifiers |= EDITOR_MOUSE_MOD_ALT;
	}
	if (has_ctrl) {
		event_out->modifiers |= EDITOR_MOUSE_MOD_CTRL;
	}

	// SGR uses lowercase 'm' for release.
	if (suffix == 'm') {
		// Different terminals encode release as either button 0 or button 3.
		if (!has_motion && !has_wheel && (button == 0 || button == 3)) {
			event_out->kind = EDITOR_MOUSE_EVENT_LEFT_RELEASE;
		}
		return 1;
	}
	if (suffix != 'M') {
		return 1;
	}

	// Wheel events set bit 6 and encode direction in the low two bits.
	if (has_wheel) {
		if (has_alt || has_ctrl) {
			return 1;
		}
		int wheel_button = button;
		if (wheel_button == 0 && has_shift) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_LEFT;
		} else if (wheel_button == 1 && has_shift) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_RIGHT;
		} else if (wheel_button == 0) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_UP;
		} else if (wheel_button == 1) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_DOWN;
		} else if (wheel_button == 2) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_LEFT;
		} else if (wheel_button == 3) {
			event_out->kind = EDITOR_MOUSE_EVENT_WHEEL_RIGHT;
		}
		return 1;
	}

	if (has_motion) {
		// Forward all left-button drags including modifier-decorated ones; the
		// dispatch layer decides which modifier combos do what. Button 3 in SGR
		// motion encoding indicates motion without any button held (1003 mode).
		if (button == 0) {
			event_out->kind = EDITOR_MOUSE_EVENT_LEFT_DRAG;
		} else if (button == 3) {
			event_out->kind = EDITOR_MOUSE_EVENT_MOTION;
		}
		return 1;
	}

	if (button == 0) {
		event_out->kind = EDITOR_MOUSE_EVENT_LEFT_PRESS;
	}
	return 1;
}

static enum terminalReadByteResult terminalReadSgrMouseEvent(struct editorMouseEvent *event_out) {
	char payload[SGR_MOUSE_MAX_PAYLOAD];
	int payload_len = 0;
	char term = '\0';

	// Bound payload length so malformed streams cannot grow indefinitely.
	while (payload_len < (int)sizeof(payload) - 1) {
		enum terminalReadByteResult byte_status = terminalReadSeqByte(&term);
		if (byte_status == TERMINAL_READ_EOF) {
			return TERMINAL_READ_EOF;
		}
		if (byte_status != TERMINAL_READ_BYTE) {
			return TERMINAL_READ_RETRY;
		}
		payload[payload_len++] = term;
		if (term == 'M' || term == 'm') {
			break;
		}
	}

	if (payload_len == (int)sizeof(payload) - 1 && term != 'M' && term != 'm') {
		return TERMINAL_READ_RETRY;
	}

	payload[payload_len] = '\0';
	if (!terminalDecodeSgrMousePayload(payload, event_out)) {
		return TERMINAL_READ_RETRY;
	}
	return TERMINAL_READ_BYTE;
}

static int terminalEscapeFallback(void) {
	if (terminalTakeResizeEvent()) {
		return RESIZE_EVENT;
	}
	return '\x1b';
}

static int terminalReadSeqByteOrKey(char *out, int fallback_key) {
	enum terminalReadByteResult read_status = terminalReadSeqByte(out);
	if (read_status == TERMINAL_READ_EOF) {
		return INPUT_EOF_EVENT;
	}
	if (read_status != TERMINAL_READ_BYTE) {
		if (terminalTakeResizeEvent()) {
			return RESIZE_EVENT;
		}
		return fallback_key;
	}
	return 0;
}

static int terminalArrowKeyFromFinal(char final, int up_key, int down_key, int right_key,
                                     int left_key) {
	switch (final) {
		case 'A':
			return up_key;
		case 'B':
			return down_key;
		case 'C':
			return right_key;
		case 'D':
			return left_key;
	}
	return '\x1b';
}

static int terminalReadBracketedPasteMarker(void) {
	char fourth = '\0';
	enum terminalReadByteResult read_status = terminalReadSeqByte(&fourth);
	if (read_status == TERMINAL_READ_EOF) {
		return INPUT_EOF_EVENT;
	}
	if (read_status == TERMINAL_READ_BYTE && (fourth == '0' || fourth == '1')) {
		char fifth = '\0';
		read_status = terminalReadSeqByte(&fifth);
		if (read_status == TERMINAL_READ_EOF) {
			return INPUT_EOF_EVENT;
		}
		if (read_status == TERMINAL_READ_BYTE && fifth == '~') {
			return fourth == '0' ? BRACKETED_PASTE_START_EVENT
			                     : BRACKETED_PASTE_END_EVENT;
		}
	}
	return '\x1b';
}

static int terminalReadCsiMouseKey(void) {
	struct editorMouseEvent event;
	enum terminalReadByteResult mouse_status = terminalReadSgrMouseEvent(&event);
	if (mouse_status == TERMINAL_READ_EOF) {
		return INPUT_EOF_EVENT;
	}
	if (mouse_status != TERMINAL_READ_BYTE) {
		return terminalEscapeFallback();
	}
	if (event.kind == EDITOR_MOUSE_EVENT_NONE) {
		return TERMINAL_KEY_CONTINUE;
	}
	g_terminal_pending_mouse_event = event;
	g_terminal_has_pending_mouse_event = 1;
	return MOUSE_EVENT;
}

static int terminalReadCsiModifiedArrowKey(char second) {
	char modifier = '\0';
	int key = terminalReadSeqByteOrKey(&modifier, '\x1b');
	if (key != 0) {
		return key;
	}
	char final = '\0';
	key = terminalReadSeqByteOrKey(&final, '\x1b');
	if (key != 0) {
		return key;
	}

	if (second != '1') {
		return '\x1b';
	}
	if (modifier == '3') {
		return terminalArrowKeyFromFinal(final, ALT_ARROW_UP, ALT_ARROW_DOWN,
		                                 ALT_ARROW_RIGHT, ALT_ARROW_LEFT);
	}
	if (modifier == '4') {
		return terminalArrowKeyFromFinal(final, ALT_SHIFT_ARROW_UP, ALT_SHIFT_ARROW_DOWN,
		                                 ALT_SHIFT_ARROW_RIGHT, ALT_SHIFT_ARROW_LEFT);
	}
	if (modifier == '5') {
		return terminalArrowKeyFromFinal(final, CTRL_ARROW_UP, CTRL_ARROW_DOWN,
		                                 CTRL_ARROW_RIGHT, CTRL_ARROW_LEFT);
	}
	if (modifier == '7') {
		return terminalArrowKeyFromFinal(final, CTRL_ALT_ARROW_UP, CTRL_ALT_ARROW_DOWN,
		                                 CTRL_ALT_ARROW_RIGHT, CTRL_ALT_ARROW_LEFT);
	}
	if (modifier == '8') {
		return terminalArrowKeyFromFinal(
		        final, CTRL_SHIFT_ALT_ARROW_UP, CTRL_SHIFT_ALT_ARROW_DOWN,
		        CTRL_SHIFT_ALT_ARROW_RIGHT, CTRL_SHIFT_ALT_ARROW_LEFT);
	}
	return '\x1b';
}

static int terminalReadCsiNumericKey(char second) {
	char third = '\0';
	int key = terminalReadSeqByteOrKey(&third, '\x1b');
	if (key != 0) {
		return key;
	}
	if (second == '2' && third == '0') {
		return terminalReadBracketedPasteMarker();
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
	if (third == ';') {
		return terminalReadCsiModifiedArrowKey(second);
	}
	return '\x1b';
}

static int terminalReadCsiKey(void) {
	char second = '\0';
	int key = terminalReadSeqByteOrKey(&second, '\x1b');
	if (key != 0) {
		return key;
	}

	if (second == '<') {
		return terminalReadCsiMouseKey();
	}
	if (second >= '0' && second <= '9') {
		return terminalReadCsiNumericKey(second);
	}
	if (second == 'H') {
		return HOME_KEY;
	}
	if (second == 'F') {
		return END_KEY;
	}
	return terminalArrowKeyFromFinal(second, ARROW_UP, ARROW_DOWN, ARROW_RIGHT, ARROW_LEFT);
}

static char *terminalBase64Encode(const unsigned char *bytes, size_t len, size_t *out_len) {
	static const char base64_table[] =
	        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (out_len == NULL) {
		return NULL;
	}

	size_t group_bytes = 0;
	size_t groups = 0;
	size_t encoded_len = 0;
	size_t encoded_cap = 0;
	if (!editorSizeAdd(len, 2, &group_bytes)) {
		return NULL;
	}
	groups = group_bytes / 3;
	if (!editorSizeMul(groups, 4, &encoded_len) ||
	    !editorSizeAdd(encoded_len, 1, &encoded_cap) || encoded_len > ROTIDE_MAX_TEXT_BYTES) {
		return NULL;
	}

	char *encoded = malloc(encoded_cap);
	if (encoded == NULL) {
		return NULL;
	}

	size_t out_idx = 0;
	for (size_t i = 0; i < len; i += 3) {
		size_t remaining = len - i;
		unsigned int octet_a = bytes[i];
		unsigned int octet_b = remaining > 1 ? bytes[i + 1] : 0;
		unsigned int octet_c = remaining > 2 ? bytes[i + 2] : 0;

		encoded[out_idx++] = base64_table[(octet_a >> 2) & 0x3F];
		encoded[out_idx++] =
		        base64_table[((octet_a & 0x03) << 4) | ((octet_b >> 4) & 0x0F)];
		encoded[out_idx++] =
		        remaining > 1
		                ? base64_table[((octet_b & 0x0F) << 2) | ((octet_c >> 6) & 0x03)]
		                : '=';
		encoded[out_idx++] = remaining > 2 ? base64_table[octet_c & 0x3F] : '=';
	}

	encoded[out_idx] = '\0';
	*out_len = out_idx;
	return encoded;
}

static enum terminalOsc52Mode terminalGetOsc52Mode(void) {
	const char *mode = getenv("ROTIDE_OSC52");
	if (mode == NULL || mode[0] == '\0' || strcmp(mode, "auto") == 0) {
		return TERMINAL_OSC52_MODE_AUTO;
	}
	if (strcmp(mode, "off") == 0) {
		return TERMINAL_OSC52_MODE_OFF;
	}
	if (strcmp(mode, "force") == 0) {
		return TERMINAL_OSC52_MODE_FORCE;
	}
	return TERMINAL_OSC52_MODE_AUTO;
}

static int terminalCanUseOsc52(enum terminalOsc52Mode mode, size_t len) {
	if (mode == TERMINAL_OSC52_MODE_OFF) {
		return 0;
	}
	if (len > ROTIDE_OSC52_MAX_COPY_BYTES) {
		return 0;
	}
	if (mode == TERMINAL_OSC52_MODE_FORCE) {
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

void editorClipboardSyncOsc52(const char *text, size_t len) {
	if (len > 0 && text == NULL) {
		return;
	}

	enum terminalOsc52Mode mode = terminalGetOsc52Mode();
	if (!terminalCanUseOsc52(mode, len)) {
		return;
	}

	size_t encoded_len = 0;
	char *encoded = terminalBase64Encode((const unsigned char *)text, len, &encoded_len);
	if (encoded == NULL) {
		return;
	}

	const char *tmux = getenv("TMUX");
	const char *screen = getenv("STY");

	if (tmux != NULL && tmux[0] != '\0') {
		(void)terminalWriteAll(STDOUT_FILENO, OSC52_TMUX_PREFIX,
		                       sizeof(OSC52_TMUX_PREFIX) - 1);
		(void)terminalWriteAll(STDOUT_FILENO, encoded, encoded_len);
		(void)terminalWriteAll(STDOUT_FILENO, OSC52_TMUX_SUFFIX,
		                       sizeof(OSC52_TMUX_SUFFIX) - 1);
		free(encoded);
		return;
	}

	if (screen != NULL && screen[0] != '\0') {
		(void)terminalWriteAll(STDOUT_FILENO, OSC52_SCREEN_PREFIX,
		                       sizeof(OSC52_SCREEN_PREFIX) - 1);
		(void)terminalWriteAll(STDOUT_FILENO, encoded, encoded_len);
		(void)terminalWriteAll(STDOUT_FILENO, OSC52_SCREEN_SUFFIX,
		                       sizeof(OSC52_SCREEN_SUFFIX) - 1);
		free(encoded);
		return;
	}

	(void)terminalWriteAll(STDOUT_FILENO, OSC52_PLAIN_PREFIX, sizeof(OSC52_PLAIN_PREFIX) - 1);
	(void)terminalWriteAll(STDOUT_FILENO, encoded, encoded_len);
	(void)terminalWriteAll(STDOUT_FILENO, OSC52_PLAIN_SUFFIX, sizeof(OSC52_PLAIN_SUFFIX) - 1);
	free(encoded);
}

static void terminalClipboardSyncNative(const char *text, size_t len) {
	if (len == 0) {
		return;
	}

	const char *wayland = getenv("WAYLAND_DISPLAY");
	const char *display = getenv("DISPLAY");

	const char *cmd;
	if (wayland != NULL && wayland[0] != '\0') {
		cmd = "wl-copy 2>/dev/null";
	} else if (display != NULL && display[0] != '\0') {
		cmd = "xclip -selection clipboard 2>/dev/null"
		      " || xsel --clipboard --input 2>/dev/null";
	} else {
		return;
	}

	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return;
	}

	// Double-fork: parent waits for the intermediate child (exits quickly),
	// grandchild runs the clipboard tool (re-parented to init so it can
	// stay alive without blocking the editor).
	pid_t child = fork();
	if (child < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return;
	}

	if (child == 0) {
		pid_t grandchild = fork();
		if (grandchild != 0) {
			_exit(0);
		}
		close(pipefd[1]);
		if (dup2(pipefd[0], STDIN_FILENO) == -1) {
			_exit(1);
		}
		close(pipefd[0]);
		int devnull = open("/dev/null", O_RDWR);
		if (devnull != -1) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("sh", "sh", "-c", cmd, (char *)NULL);
		_exit(1);
	}

	close(pipefd[0]);
	size_t total = 0;
	while (total < len) {
		ssize_t n = write(pipefd[1], text + total, len - total);
		if (n <= 0) {
			if (n == -1 && errno == EINTR) {
				continue;
			}
			break;
		}
		total += (size_t)n;
	}
	close(pipefd[1]);
	waitpid(child, NULL, 0);
}

void editorClipboardSyncAll(const char *text, size_t len) {
	terminalClipboardSyncNative(text, len);
	editorClipboardSyncOsc52(text, len);
}

int editorConsumeMouseEvent(struct editorMouseEvent *out) {
	if (out == NULL || !g_terminal_has_pending_mouse_event) {
		return 0;
	}

	*out = g_terminal_pending_mouse_event;
	g_terminal_pending_mouse_event.kind = EDITOR_MOUSE_EVENT_NONE;
	g_terminal_pending_mouse_event.x = 0;
	g_terminal_pending_mouse_event.y = 0;
	g_terminal_pending_mouse_event.modifiers = EDITOR_MOUSE_MOD_NONE;
	g_terminal_has_pending_mouse_event = 0;
	return 1;
}

void editorQueueResizeEvent(void) {
	g_terminal_resize_pending = 1;
}

int editorRefreshWindowSize(void) {
	int rows = 0;
	int cols = 0;
	if (editorReadWindowSize(&rows, &cols) == -1) {
		return 0;
	}

	if (cols < 1) {
		cols = 1;
	}

	int text_rows = rows - 3;
	if (text_rows < 1) {
		text_rows = 1;
	}

	E.window_cols = cols;
	E.window_rows = text_rows;
	return 1;
}

static void terminalRestoreCursorVisualState(void) {
	(void)terminalWriteAll(STDOUT_FILENO, VT100_NORMAL_COLORS_3,
	                       sizeof(VT100_NORMAL_COLORS_3) - 1);
	(void)terminalWriteAll(STDOUT_FILENO, VT100_CURSOR_DEFAULT_5,
	                       sizeof(VT100_CURSOR_DEFAULT_5) - 1);
	(void)terminalWriteAll(STDOUT_FILENO, VT100_CURSOR_COLOR_DEFAULT,
	                       sizeof(VT100_CURSOR_COLOR_DEFAULT) - 1);
	(void)terminalWriteAll(STDOUT_FILENO, VT100_SHOW_CURSOR_6, sizeof(VT100_SHOW_CURSOR_6) - 1);
}

static void terminalRestoreInternal(void) {
	if (g_terminal_attrs_captured && g_terminal_raw_enabled) {
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_attrs) == 0) {
			g_terminal_raw_enabled = 0;
		}
	}
	(void)terminalWriteAll(STDOUT_FILENO, VT100_DISABLE_MOUSE, sizeof(VT100_DISABLE_MOUSE) - 1);
	terminalRestoreCursorVisualState();
	// Drop any queued event so a later key read cannot consume stale mouse data.
	g_terminal_pending_mouse_event.kind = EDITOR_MOUSE_EVENT_NONE;
	g_terminal_pending_mouse_event.x = 0;
	g_terminal_pending_mouse_event.y = 0;
	g_terminal_pending_mouse_event.modifiers = EDITOR_MOUSE_MOD_NONE;
	g_terminal_has_pending_mouse_event = 0;
	g_terminal_resize_pending = 0;
}

static void terminalRestoreAtExit(void) {
	terminalRestoreInternal();
}

static void terminalHandleTerminationSignal(int signo) {
	/* Tear down long-lived subsystems before restoring the terminal so
	 * adapter/server processes get a chance to exit cleanly. These calls
	 * are not strictly async-signal-safe (they touch malloc and run a
	 * brief shutdown handshake), but in practice the editor is blocked on
	 * read() when a termination signal arrives, so it is the pragmatic
	 * choice over leaking adapter children. */
	editorDapShutdown();
	editorLspShutdown();
	editorSyntaxBackgroundStop();
	editorSyntaxReleaseSharedResources();

	terminalRestoreInternal();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(signo, &sa, NULL);
	(void)raise(signo);
	_exit(128 + signo);
}

static void terminalInstallTerminationHandlers(void) {
	if (g_terminal_handlers_installed) {
		return;
	}

	const int signals[] = {SIGHUP, SIGINT, SIGTERM, SIGQUIT};
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = terminalHandleTerminationSignal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
		(void)sigaction(signals[i], &sa, NULL);
	}

	g_terminal_handlers_installed = 1;
}

static void terminalHandleResizeSignal(int signo) {
	(void)signo;
	g_terminal_resize_pending = 1;
}

static void terminalInstallResizeHandler(void) {
	if (g_terminal_resize_handler_installed) {
		return;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = terminalHandleResizeSignal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	(void)sigaction(SIGWINCH, &sa, NULL);
	g_terminal_resize_handler_installed = 1;
}

int editorClearScreen(void) {
	return write(STDOUT_FILENO, VT100_CLEAR_SCREEN_4, 4);
}

int editorResetCursorPos(void) {
	return write(STDOUT_FILENO, VT100_RESET_CURSOR_POS_3, 3);
}

void editorRestoreTerminal(void) {
	terminalRestoreInternal();
}

void editorPanic(const char *s) {
	terminalRestoreInternal();
	editorClearScreen();
	editorResetCursorPos();

	perror(s);
	exit(EXIT_FAILURE);
}

void editorSetDefaultMode(void) {
	terminalRestoreInternal();
}

void editorSetRawMode(void) {
	if (g_terminal_raw_enabled) {
		return;
	}

	if (tcgetattr(STDIN_FILENO, &E.orig_attrs) == -1) {
		editorPanic("tcgetattr");
	}
	g_terminal_attrs_captured = 1;
	// Always restore terminal settings on exit so the shell stays usable.
	if (!g_terminal_atexit_registered) {
		if (atexit(terminalRestoreAtExit) == 0) {
			g_terminal_atexit_registered = 1;
		}
	}

	struct termios attrs = E.orig_attrs;

	// lflag: disable cooked-mode line editing and signal-generating shortcuts.
	attrs.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	// iflag: keep byte stream unmodified (no flow control or CR/LF rewriting).
	attrs.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	// oflag: disable post-processing so writes are emitted exactly as provided.
	attrs.c_oflag &= ~(OPOST);
	// cflag: force 8-bit bytes. Reads are fully non-blocking — the main
	// input loop drives the wait with poll() over stdin + every PTY master
	// fd, so stdin returning 0 bytes immediately is the desired behavior.
	attrs.c_cflag |= (CS8);
	attrs.c_cc[VMIN] = 0;
	attrs.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attrs) == -1) {
		editorPanic("tcsetattr");
	}
	// Mouse enable is best-effort: unsupported terminals simply ignore the control sequence.
	(void)terminalWriteAll(STDOUT_FILENO, VT100_ENABLE_MOUSE, sizeof(VT100_ENABLE_MOUSE) - 1);
	g_terminal_raw_enabled = 1;
	(void)signal(SIGPIPE, SIG_IGN);
	terminalInstallTerminationHandlers();
	terminalInstallResizeHandler();
}

/* If a refresh fired recently, sleep out the remainder of the frame window
 * before returning a TERMINAL_EVENT. Lets bursty PTY output (fuzzers, build
 * tools spewing logs) coalesce into one redraw per frame instead of one per
 * read. Returns when the budget elapses, stdin/resize wakes us early, or
 * there's no budget to wait on. */
static void terminalCoalesceFloodFrame(void) {
	if (g_terminal_last_frame_ms < 0) {
		return;
	}
	long now = terminalMonotonicMs();
	long elapsed = now - g_terminal_last_frame_ms;
	if (elapsed >= TERMINAL_MIN_FRAME_INTERVAL_MS) {
		return;
	}
	int remaining = (int)(TERMINAL_MIN_FRAME_INTERVAL_MS - elapsed);
	terminalWaitForInput(remaining);
	/* Fold in anything that arrived during the wait so the upcoming refresh
	 * captures the full burst. */
	(void)terminalPumpAllWithPerf();
}

int editorReadKey(void) {
	while (1) {
		if (editorSyntaxBackgroundPoll()) {
			return SYNTAX_EVENT;
		}
		if (terminalTakeResizeEvent()) {
			return RESIZE_EVENT;
		}
		if (editorTaskPoll()) {
			return TASK_EVENT;
		}
		if (editorWatchPoll()) {
			return WATCH_EVENT;
		}
		if (terminalPumpAllWithPerf() > 0) {
			terminalCoalesceFloodFrame();
			return TERMINAL_EVENT;
		}

		char c;
		enum terminalReadByteResult read_status;
		while ((read_status = terminalReadInputByte(&c)) != TERMINAL_READ_BYTE) {
			if (read_status == TERMINAL_READ_EOF) {
				return INPUT_EOF_EVENT;
			}
			if (terminalTakeResizeEvent()) {
				return RESIZE_EVENT;
			}
			if (editorSyntaxBackgroundPoll()) {
				return SYNTAX_EVENT;
			}
			if (editorTaskPoll()) {
				return TASK_EVENT;
			}
			if (editorWatchPoll()) {
				return WATCH_EVENT;
			}
			if (terminalPumpAllWithPerf() > 0) {
				terminalCoalesceFloodFrame();
				return TERMINAL_EVENT;
			}
			terminalWaitForInput(TERMINAL_SOFT_WORK_POLL_MS);
		}

		if (c != '\x1b') {
			return c;
		}

		char first = '\0';
		int key = terminalReadSeqByteOrKey(&first, '\x1b');
		if (key != 0) {
			return key;
		}

		if (first == '\x1b') {
			char second = '\0';
			char third = '\0';
			key = terminalReadSeqByteOrKey(&second, '\x1b');
			if (key != 0) {
				return key;
			}
			if (second == '[') {
				key = terminalReadSeqByteOrKey(&third, '\x1b');
				if (key != 0) {
					return key;
				}
				return terminalArrowKeyFromFinal(third, ALT_ARROW_UP,
				                                 ALT_ARROW_DOWN, ALT_ARROW_RIGHT,
				                                 ALT_ARROW_LEFT);
			}
			return '\x1b';
		}

		if (first == '[') {
			key = terminalReadCsiKey();
			if (key == TERMINAL_KEY_CONTINUE) {
				continue;
			}
			return key;
		}

		if (first == 'O') {
			char second = '\0';
			key = terminalReadSeqByteOrKey(&second, EDITOR_ALT_LETTER_KEY('o'));
			if (key != 0) {
				return key;
			}
			switch (second) {
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
			}
			return EDITOR_ALT_LETTER_KEY('o');
		}

		if (isalpha((unsigned char)first)) {
			char lower = (char)tolower((unsigned char)first);
			return EDITOR_ALT_LETTER_KEY(lower);
		}
		if (first >= 1 && first <= 26) {
			char lower = (char)('a' + first - 1);
			return EDITOR_CTRL_ALT_LETTER_KEY(lower);
		}

		return '\x1b';
	}
}

int editorReadCursorPosition(int *rows, int *cols) {
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

int editorReadWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// Fallback for terminals where TIOCGWINSZ is unavailable or unset:
		// move cursor to bottom-right and query resulting coordinates.
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}
		return editorReadCursorPosition(rows, cols);
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;

	return 0;
}
