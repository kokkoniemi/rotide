#include "language/terminal_pane.h"
#include "test_case.h"
#include "test_helpers.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rotide.h"
#include "vterm.h"
#include "workspace/layout.h"

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

static int test_terminal_pane_resize_all_to_layout_updates_grids(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	struct editorPaneNode *terminal_leaf =
			editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t =
			(struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	int before_rows = t->rows;
	int before_cols = t->cols;

	E.window_cols = 160;
	E.window_rows = 60;
	editorTerminalPaneResizeAllToLayout(E.layout_root);

	int failed = t->rows <= before_rows || t->cols <= before_cols;
	return failed;
}

static int test_terminal_pane_open_split_replaces_sibling_kind(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
			editorTerminalPaneOpenSplit("sleep 2", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	int failed = E.focused_leaf != terminal_leaf ||
			terminal_leaf->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL ||
			terminal_leaf->as.leaf.kind_state == NULL ||
			terminal_leaf->as.leaf.kind_state_free != editorTerminalPaneFree ||
			editorPaneTreeLeafCount(E.layout_root) != 2 ||
			!editorPaneNodeContainsLeaf(E.layout_root, original);
	return failed;
}

static int test_terminal_pane_open_vertical_split_replaces_sibling_kind(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
			editorTerminalPaneOpenSplit("sleep 2", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	int failed = E.focused_leaf != terminal_leaf ||
			terminal_leaf->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL ||
			terminal_leaf->as.leaf.kind_state == NULL ||
			terminal_leaf->as.leaf.kind_state_free != editorTerminalPaneFree ||
			editorPaneTreeLeafCount(E.layout_root) != 2 ||
			!editorPaneNodeContainsLeaf(E.layout_root, original);
	return failed;
}

static int test_terminal_pane_close_exited_removes_leaf_and_restores_focus(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	E.window_cols = 80;
	E.window_rows = 24;
	struct editorPaneNode *terminal_leaf =
			editorTerminalPaneOpenSplit("true", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t =
			(struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
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
	if (!t->exited) {
		return 1;
	}
	struct editorPaneNode *focus = E.focused_leaf;
	int closed = editorTerminalPaneCloseExited(&E.layout_root, &focus, NULL);
	int failed = closed != 1 || editorPaneTreeLeafCount(E.layout_root) != 1 ||
			focus != original || !editorPaneNodeContainsLeaf(E.layout_root, original);
	E.focused_leaf = focus;
	return failed;
}

static int test_terminal_pane_send_key_writes_printable_byte(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("cat", 40, 8);
	if (t == NULL) {
		return 1;
	}
	int written = 0;
	written |= editorTerminalPaneSendKey(t, 'Z');
	written |= editorTerminalPaneSendKey(t, '\r');
	if (!written) {
		editorTerminalPaneFree(t);
		return 1;
	}
	int found = wait_for_text_in_screen(t, "Z", 2000);
	editorTerminalPaneFree(t);
	return found ? 0 : 1;
}

static int test_terminal_pane_send_key_handles_control(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("cat", 40, 8);
	if (t == NULL) {
		return 1;
	}
	(void)editorTerminalPaneSendKey(t, 'A');
	(void)editorTerminalPaneSendKey(t, 'B');
	(void)editorTerminalPaneSendKey(t, '\r');
	int wrote = editorTerminalPaneSendKey(t, 0x04); /* ^D = EOF for cat */
	int saw_ab = wait_for_text_in_screen(t, "AB", 2000);
	int waited = 0;
	while (waited < 2000 && !t->exited) {
		(void)editorTerminalPanePump(t);
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited += 20;
	}
	int failed = !wrote || !saw_ab || !t->exited;
	editorTerminalPaneFree(t);
	return failed;
}

static int test_terminal_pane_mouse_tracking_disabled_by_default(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 40, 8);
	if (t == NULL) {
		return 1;
	}
	int sent = editorTerminalPaneSendMouseButton(t, 1, 1, 0, 0, 0);
	editorTerminalPaneFree(t);
	return sent != 0;
}

static int test_terminal_pane_mouse_tracking_enabled_via_decset(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 40, 8);
	if (t == NULL) {
		return 1;
	}
	/* Simulate the child enabling SGR mouse tracking by writing the DECSET
	 * 1000 + 1006 sequence through vterm's parser. The settermprop
	 * callback should bump terminal->mouse_tracking. */
	const char *enable_mouse = "\x1b[?1000h\x1b[?1006h";
	vterm_input_write(t->vt, enable_mouse, strlen(enable_mouse));
	int tracking = t->mouse_tracking;
	int sent_off = 0;
	if (tracking <= 0) {
		editorTerminalPaneFree(t);
		return 1;
	}
	int sent = editorTerminalPaneSendMouseButton(t, 1, 1, 2, 3, 0);

	/* Disable and verify SendMouseButton no longer forwards. */
	const char *disable_mouse = "\x1b[?1000l";
	vterm_input_write(t->vt, disable_mouse, strlen(disable_mouse));
	sent_off = editorTerminalPaneSendMouseButton(t, 1, 1, 2, 3, 0);

	editorTerminalPaneFree(t);
	return sent != 1 || sent_off != 0;
}

static int test_terminal_pane_cursor_props_follow_decset_sequences(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 40, 8);
	if (t == NULL) {
		return 1;
	}
	/* DECTCEM off + DECSCUSR steady bar. */
	const char *hide_and_bar = "\x1b[?25l\x1b[6 q";
	vterm_input_write(t->vt, hide_and_bar, strlen(hide_and_bar));
	if (t->cursor_visible != 0 || t->cursor_blink != 0 ||
			t->cursor_shape != VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
		editorTerminalPaneFree(t);
		return 1;
	}
	/* DECTCEM on + DECSCUSR blinking block. */
	const char *show_and_block = "\x1b[?25h\x1b[1 q";
	vterm_input_write(t->vt, show_and_block, strlen(show_and_block));
	int failed = t->cursor_visible != 1 || t->cursor_blink != 1 ||
			t->cursor_shape != VTERM_PROP_CURSORSHAPE_BLOCK;
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
	{"terminal_pane_resize_all_to_layout_updates_grids",
			test_terminal_pane_resize_all_to_layout_updates_grids},
	{"terminal_pane_open_split_replaces_sibling_kind",
			test_terminal_pane_open_split_replaces_sibling_kind},
	{"terminal_pane_open_vertical_split_replaces_sibling_kind",
			test_terminal_pane_open_vertical_split_replaces_sibling_kind},
	{"terminal_pane_close_exited_removes_leaf_and_restores_focus",
			test_terminal_pane_close_exited_removes_leaf_and_restores_focus},
	{"terminal_pane_send_key_writes_printable_byte",
			test_terminal_pane_send_key_writes_printable_byte},
	{"terminal_pane_send_key_handles_control",
			test_terminal_pane_send_key_handles_control},
	{"terminal_pane_mouse_tracking_disabled_by_default",
			test_terminal_pane_mouse_tracking_disabled_by_default},
	{"terminal_pane_mouse_tracking_enabled_via_decset",
			test_terminal_pane_mouse_tracking_enabled_via_decset},
	{"terminal_pane_cursor_props_follow_decset_sequences",
			test_terminal_pane_cursor_props_follow_decset_sequences},
	{"terminal_pane_write_forwards_to_child",
			test_terminal_pane_write_forwards_to_child},
};

const int g_terminal_pane_test_count =
		(int)(sizeof(g_terminal_pane_tests) /
				sizeof(g_terminal_pane_tests[0]));
