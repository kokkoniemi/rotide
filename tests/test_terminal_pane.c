#include "input/mouse.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "test_case.h"
#include "vterm.h"
#include "workspace/layout.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int wait_for_text_in_screen(struct editorTerminalPane *t, const char *needle,
                                   int timeout_ms) {
	int waited = 0;
	while (waited < timeout_ms) {
		(void)editorTerminalPanePump(t);
		char buf[4096];
		VTermRect rect = {
		        .start_row = 0, .end_row = t->rows, .start_col = 0, .end_col = t->cols};
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
	int rows = 0;
	int cols = 0;
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

static int test_terminal_pane_mouse_drag_resizes_terminal_pane(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	int before_cols = t->cols;
	int before_rows = t->rows;

	if (E.layout_root == NULL || !E.layout_root->is_split) {
		return 1;
	}
	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		return 1;
	}
	E.split_resize_active = 1;
	E.split_resize_node = E.layout_root;
	E.mouse_left_button_down = 1;

	int target_x = viewport.x + (int)((double)(viewport.w - 1) * 0.25);
	struct editorMouseEvent event = {
	        .kind = EDITOR_MOUSE_EVENT_LEFT_DRAG,
	        .x = target_x + 1,
	        .y = viewport.y + 2,
	        .modifiers = 0,
	};
	(void)editorHandleMouseLeftDrag(&event);

	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	E.mouse_left_button_down = 0;

	/* The focused (newly-created) terminal pane is the right/second child,
	 * so shrinking the left half grows the terminal's column count.*/
	int failed = (t->cols == before_cols && t->rows == before_rows);
	return failed;
}

static int test_terminal_pane_mouse_drag_shrinks_terminal_pane(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	if (E.layout_root == NULL || !E.layout_root->is_split) {
		return 1;
	}

	/* Grow first, then attempt to shrink — the shrink drag moves the mouse
	 * into the terminal pane area, which used to be intercepted by the
	 * terminal pane mouse handler. */
	E.layout_root->as.split.ratio = 0.25;
	editorTerminalPaneResizeAllToLayout(E.layout_root);
	int grown_cols = t->cols;

	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		return 1;
	}
	E.split_resize_active = 1;
	E.split_resize_node = E.layout_root;
	E.mouse_left_button_down = 1;

	int target_x = viewport.x + (int)((double)(viewport.w - 1) * 0.75);
	struct editorMouseEvent event = {
	        .kind = EDITOR_MOUSE_EVENT_LEFT_DRAG,
	        .x = target_x + 1,
	        .y = viewport.y + 2,
	        .modifiers = 0,
	};
	/* Mirror editorHandleMouseEventDispatch: the terminal-pane interceptor
	 * runs first, and a non-zero return short-circuits the drag handler.
	 * That short-circuit used to freeze the border. */
	if (!editorHandleMouseEventInTerminalPane(&event)) {
		(void)editorHandleMouseLeftDrag(&event);
	}

	E.split_resize_active = 0;
	E.split_resize_node = NULL;
	E.mouse_left_button_down = 0;

	int failed = t->cols >= grown_cols;
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

/* Regression: a CSI sequence with more semicolon-separated arguments than
 * CSI_ARGS_MAX (16 in the vendored libvterm) used to overrun the parser's
 * args[] array and clobber the callbacks pointer that follows it, causing
 * a segfault inside do_csi. Excess separators must be folded into the last
 * slot instead of writing past the array. */
static int test_terminal_pane_csi_excess_args_does_not_crash(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const char crash[] = "\x1b[1;;;;;;;;;;;;;;;;;;;;;;;;;;;E5\x1b[1E5frr0m7";
	vterm_input_write(vt, crash, sizeof(crash) - 1);
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

/* Regression: argument digits that would overflow a long in `args *= 10`
 * must be saturated rather than tripping UBSan. */
static int test_terminal_pane_csi_huge_arg_does_not_overflow(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const char huge[] = "\x1b[33333333333333333333H";
	vterm_input_write(vt, huge, sizeof(huge) - 1);
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

/* Regression: a DCS sequence with an embedded ESC followed by C0/NUL bytes
 * used to leave the parser with string_start advanced past pos and in_esc
 * still set, so the end-of-input fixup underflowed size_t (0 - 1) and the
 * DECRQSS handler walked off the end of the input. */
static int test_terminal_pane_dcs_embedded_esc_does_not_overread(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const char crash[] = "\x1bP$q\x11\x1b\x1f\x1f\x1f\x1f\x1f\x00\x1f";
	vterm_input_write(vt, crash, sizeof(crash) - 1);
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

/* Regression: erase_internal used to iterate columns up to rect.end_col
 * with no upper bound, and rows against state->rows rather than the
 * screen's own row count. Crafted sequences (an ESC 2 resetting state
 * followed by a CSI K with embedded C0 bytes) could push the rect out
 * of bounds, getcell() then returned NULL and the erase deref crashed. */
static int test_terminal_pane_erase_oob_rect_does_not_crash(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const char crash[] = "\x1b\x32\xc2\x9f\x1b[\x00\x14\x0b"
	                     "0K\x0b\x0b\x0a\x1b[\x00\x14\x0b"
	                     "0\x1bK";
	vterm_input_write(vt, crash, sizeof(crash) - 1);
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

/* Regression: TBC (CSI g) calls clear_col_tabstop(state, state->pos.col)
 * directly. Under crafted sequences pos.col can go out of range and the
 * helper used to index tabstops[col >> 3] without bounding col, reading
 * (or writing) the byte before the allocation when col was negative. */
static int test_terminal_pane_tbc_out_of_range_col_does_not_crash(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const char crash[] = "\x1b\x32\x1b[52;4X\xc2\x00\x9f\x1b[0g\x09\x00@P\x00\x00"
	                     "\x1b[\x00\x1b[\x00";
	vterm_input_write(vt, crash, sizeof(crash) - 1);
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

/* Regression: moverect_internal called memmove() on getcell() results
 * without guarding for NULL. Crafted sequences (DECSET + C1 bytes + scroll-
 * inducing text) drove the scroll path with rects that landed partially
 * outside the buffer, getcell() returned NULL, memmove crashed on its
 * nonnull-declared source arg. */
static int test_terminal_pane_scroll_oob_rect_does_not_crash(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const char crash[] = "\x1b?\x1b\x1b[4h\x1b\x1b\x32\xc2\x9f\x1b[\x00\xf5\xc2\x9f"
	                     "\xc2\x9f\xf4\x9f\x1b[\x00\xf5\xc2\x1b";
	vterm_input_write(vt, crash, sizeof(crash) - 1);
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

/* Regression: putglyph NULL-checked the leading cell of a wide glyph but
 * blindly dereferenced the continuation cell at pos.col+1. A width-2 glyph
 * landing at the rightmost column (or any col where pos.col+1 >= screen->cols)
 * crashed the unchecked deref. */
static int test_terminal_pane_wide_glyph_at_edge_does_not_crash(void) {
	VTerm *vt = vterm_new(24, 80);
	if (vt == NULL) {
		return 1;
	}
	vterm_set_utf8(vt, 1);
	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}
	const unsigned char crash[] = {
	        0x00, 0x6c, 0xca, 0x00, 0xff, 0xff, 0x27, 0x44, 0xff, 0xff, 0xff, 0xff, 0xff,
	        0x6c, 0x1b, 0x5b, 0x3f, 0x3a, 0x37, 0x6c, 0xf4, 0x00, 0x62, 0x42, 0xb3, 0xff,
	        0x00, 0x00, 0x35, 0x50, 0x00, 0x00, 0x20, 0x9e, 0x9d, 0x1b, 0x5b, 0x36, 0x39,
	        0x62, 0x1b, 0xff, 0xff, 0x63, 0x30, 0x79, 0x29, 0x5b, 0x63, 0x09, 0x0b, 0x6c,
	        0x2c, 0x00, 0x1b, 0x5b, 0x3f, 0x3a, 0x37, 0xff, 0xff, 0x4f, 0x30, 0x00, 0x0b,
	        0xf6, 0xe9, 0x9e, 0x9d, 0x1b, 0x5b, 0x36, 0x39, 0x62, 0xff, 0xff, 0x00,
	};
	vterm_input_write(vt, (const char *)crash, sizeof(crash));
	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
	}
	vterm_free(vt);
	return 0;
}

static int test_terminal_pane_scrollback_ring_captures_evicted_rows(void) {
	/* 3-row screen so a couple of LFs push lines into scrollback. */
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 20, 3);
	if (t == NULL) {
		return 1;
	}
	const char *input = "line1\r\nline2\r\nline3\r\nline4\r\nline5\r\n";
	vterm_input_write(t->vt, input, strlen(input));
	vterm_screen_flush_damage(t->screen);
	int failed = t->sb_size < 1;
	/* Pull most-recent scrollback row; expect "line1" (or "line2") depending
	 * on where the screen scroll lands relative to the cursor. Just verify
	 * something was captured and the row is non-empty. */
	if (!failed) {
		VTermScreenCell row[20];
		failed = !editorTerminalPaneGetLogRow(t, -1, row);
		if (!failed) {
			int has_text = 0;
			for (int i = 0; i < 20; i++) {
				if (row[i].chars[0] != 0 && row[i].chars[0] != ' ') {
					has_text = 1;
					break;
				}
			}
			failed = !has_text;
		}
	}
	editorTerminalPaneFree(t);
	return failed;
}

static int test_terminal_pane_scroll_by_clamps_to_history(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 20, 3);
	if (t == NULL) {
		return 1;
	}
	const char *input = "a\r\nb\r\nc\r\nd\r\ne\r\n";
	vterm_input_write(t->vt, input, strlen(input));
	vterm_screen_flush_damage(t->screen);
	int sb = t->sb_size;
	int failed = sb < 1;
	/* Way past the end clamps. */
	if (!failed) {
		(void)editorTerminalPaneScrollBy(t, 9999);
		failed = t->scroll_offset != sb;
	}
	/* Way past the front clamps to 0. */
	if (!failed) {
		(void)editorTerminalPaneScrollBy(t, -9999);
		failed = t->scroll_offset != 0;
	}
	editorTerminalPaneFree(t);
	return failed;
}

static int test_terminal_pane_selection_extract_returns_visible_text(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 20, 3);
	if (t == NULL) {
		return 1;
	}
	const char *input = "hello world";
	vterm_input_write(t->vt, input, strlen(input));
	vterm_screen_flush_damage(t->screen);
	/* Select cols 0..5 on live row 0 (i.e. "hello"). */
	editorTerminalPaneSelectionBegin(t, 0, 0);
	editorTerminalPaneSelectionUpdate(t, 0, 5);
	size_t len = 0;
	char *text = editorTerminalPaneSelectionExtract(t, &len);
	int failed = text == NULL || strncmp(text, "hello", 5) != 0;
	free(text);
	editorTerminalPaneFree(t);
	return failed;
}

static int test_terminal_pane_selection_contains_matches_bounds(void) {
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 20, 3);
	if (t == NULL) {
		return 1;
	}
	editorTerminalPaneSelectionBegin(t, 1, 2);
	editorTerminalPaneSelectionUpdate(t, 1, 5);
	int failed = 0;
	failed |= !editorTerminalPaneSelectionContains(t, 1, 2);
	failed |= !editorTerminalPaneSelectionContains(t, 1, 4);
	failed |= editorTerminalPaneSelectionContains(t, 1, 5); /* exclusive end */
	failed |= editorTerminalPaneSelectionContains(t, 0, 3);
	editorTerminalPaneSelectionClear(t);
	failed |= editorTerminalPaneSelectionContains(t, 1, 3);
	editorTerminalPaneFree(t);
	return failed ? 1 : 0;
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
        {"terminal_pane_pump_captures_child_output", test_terminal_pane_pump_captures_child_output},
        {"terminal_pane_pump_marks_exit", test_terminal_pane_pump_marks_exit},
        {"terminal_pane_resize_updates_grid", test_terminal_pane_resize_updates_grid},
        {"terminal_pane_resize_all_to_layout_updates_grids",
         test_terminal_pane_resize_all_to_layout_updates_grids},
        {"terminal_pane_mouse_drag_resizes_terminal_pane",
         test_terminal_pane_mouse_drag_resizes_terminal_pane},
        {"terminal_pane_mouse_drag_shrinks_terminal_pane",
         test_terminal_pane_mouse_drag_shrinks_terminal_pane},
        {"terminal_pane_open_split_replaces_sibling_kind",
         test_terminal_pane_open_split_replaces_sibling_kind},
        {"terminal_pane_open_vertical_split_replaces_sibling_kind",
         test_terminal_pane_open_vertical_split_replaces_sibling_kind},
        {"terminal_pane_close_exited_removes_leaf_and_restores_focus",
         test_terminal_pane_close_exited_removes_leaf_and_restores_focus},
        {"terminal_pane_send_key_writes_printable_byte",
         test_terminal_pane_send_key_writes_printable_byte},
        {"terminal_pane_send_key_handles_control", test_terminal_pane_send_key_handles_control},
        {"terminal_pane_mouse_tracking_disabled_by_default",
         test_terminal_pane_mouse_tracking_disabled_by_default},
        {"terminal_pane_mouse_tracking_enabled_via_decset",
         test_terminal_pane_mouse_tracking_enabled_via_decset},
        {"terminal_pane_cursor_props_follow_decset_sequences",
         test_terminal_pane_cursor_props_follow_decset_sequences},
        {"terminal_pane_write_forwards_to_child", test_terminal_pane_write_forwards_to_child},
        {"terminal_pane_csi_excess_args_does_not_crash",
         test_terminal_pane_csi_excess_args_does_not_crash},
        {"terminal_pane_csi_huge_arg_does_not_overflow",
         test_terminal_pane_csi_huge_arg_does_not_overflow},
        {"terminal_pane_dcs_embedded_esc_does_not_overread",
         test_terminal_pane_dcs_embedded_esc_does_not_overread},
        {"terminal_pane_erase_oob_rect_does_not_crash",
         test_terminal_pane_erase_oob_rect_does_not_crash},
        {"terminal_pane_tbc_out_of_range_col_does_not_crash",
         test_terminal_pane_tbc_out_of_range_col_does_not_crash},
        {"terminal_pane_scroll_oob_rect_does_not_crash",
         test_terminal_pane_scroll_oob_rect_does_not_crash},
        {"terminal_pane_wide_glyph_at_edge_does_not_crash",
         test_terminal_pane_wide_glyph_at_edge_does_not_crash},
        {"terminal_pane_scrollback_ring_captures_evicted_rows",
         test_terminal_pane_scrollback_ring_captures_evicted_rows},
        {"terminal_pane_scroll_by_clamps_to_history",
         test_terminal_pane_scroll_by_clamps_to_history},
        {"terminal_pane_selection_extract_returns_visible_text",
         test_terminal_pane_selection_extract_returns_visible_text},
        {"terminal_pane_selection_contains_matches_bounds",
         test_terminal_pane_selection_contains_matches_bounds},
};

const int g_terminal_pane_test_count =
        (int)(sizeof(g_terminal_pane_tests) / sizeof(g_terminal_pane_tests[0]));
