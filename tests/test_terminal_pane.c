#include "language/terminal_pane.h"
#include "test_case.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vterm.h"

static int wait_for_text_in_screen(struct editorTerminalPane *t,
		const char *needle, int timeout_ms) {
	int waited = 0;
	while (waited < timeout_ms) {
		(void)editorTerminalPanePump(t);
		char buf[4096];
		VTermRect rect = {.start_row = 0, .end_row = t->rows,
				.start_col = 0, .end_col = t->cols};
		size_t n = vterm_screen_get_text(t->screen, buf, sizeof(buf) - 1, rect);
		if (n >= sizeof(buf)) {
			n = sizeof(buf) - 1;
		}
		buf[n] = '\0';
		if (strstr(buf, needle) != NULL) {
			return 1;
		}
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	return 0;
}

static int test_terminal_pane_create_rejects_null_command(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate(NULL, 80, 24);
	if (t != NULL) {
		editorTerminalPaneFree(t);
		return 1;
	}
	return 0;
}

static int test_terminal_pane_pump_captures_child_output(void) {
	struct editorTerminalPane *t =
			editorTerminalPaneCreate("printf 'rotide-vt-marker\\n'", 40, 8);
	if (t == NULL) {
		return 1;
	}
	int found = wait_for_text_in_screen(t, "rotide-vt-marker", 2000);
	editorTerminalPaneFree(t);
	return found ? 0 : 1;
}

static int test_terminal_pane_pump_marks_exit(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("true", 40, 8);
	if (t == NULL) {
		return 1;
	}
	int waited = 0;
	while (waited < 2000 && !t->exited) {
		(void)editorTerminalPanePump(t);
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	int failed = !t->exited;
	editorTerminalPaneFree(t);
	return failed;
}

static int test_terminal_pane_resize_updates_grid(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 40, 8);
	if (t == NULL) {
		return 1;
	}
	if (!editorTerminalPaneResize(t, 100, 30)) {
		editorTerminalPaneFree(t);
		return 1;
	}
	int rows = 0, cols = 0;
	vterm_get_size(t->vt, &rows, &cols);
	int failed = t->cols != 100 || t->rows != 30 || cols != 100 || rows != 30;
	editorTerminalPaneFree(t);
	return failed;
}

static int test_terminal_pane_write_forwards_to_child(void) {
	/* `cat` echoes typed bytes back through the PTY. Write "hi\n", read
	 * via pump, expect the bytes to land in the vterm screen. */
	struct editorTerminalPane *t = editorTerminalPaneCreate("cat", 40, 8);
	if (t == NULL) {
		return 1;
	}
	/* Disable echo on the slave isn't easily portable here — `cat` will
	 * receive the bytes and the tty driver will echo them back, so the
	 * screen should show them either way. Write a unique marker. */
	int written = editorTerminalPaneWrite(t, "z9marker\n", 9);
	int failed = written != 9;
	if (!failed) {
		failed = wait_for_text_in_screen(t, "z9marker", 2000) ? 0 : 1;
	}
	editorTerminalPaneFree(t);
	return failed;
}

const struct editorTestCase g_terminal_pane_tests[] = {
	{"terminal_pane_create_rejects_null_command",
			test_terminal_pane_create_rejects_null_command},
	{"terminal_pane_pump_captures_child_output",
			test_terminal_pane_pump_captures_child_output},
	{"terminal_pane_pump_marks_exit",
			test_terminal_pane_pump_marks_exit},
	{"terminal_pane_resize_updates_grid",
			test_terminal_pane_resize_updates_grid},
	{"terminal_pane_write_forwards_to_child",
			test_terminal_pane_write_forwards_to_child},
};

const int g_terminal_pane_test_count =
		(int)(sizeof(g_terminal_pane_tests) /
				sizeof(g_terminal_pane_tests[0]));
