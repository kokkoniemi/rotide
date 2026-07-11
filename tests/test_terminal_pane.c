#include "input/actions_workspace.h"
#include "input/input_system.h"
#include "input/mouse.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "test_case.h"
#include "test_support.h"
#include "vterm.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

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
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
	if (t == NULL) {
		return 1;
	}
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
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
	if (t == NULL) {
		return 1;
	}
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
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
	if (t == NULL) {
		return 1;
	}
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
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 2", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	int failed = E.focused_leaf != terminal_leaf ||
	             editorPaneActiveKind(terminal_leaf) != EDITOR_PANE_KIND_TERMINAL ||
	             editorTerminalPaneForPane(terminal_leaf) == NULL ||
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
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 2", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	int failed = E.focused_leaf != terminal_leaf ||
	             editorPaneActiveKind(terminal_leaf) != EDITOR_PANE_KIND_TERMINAL ||
	             editorTerminalPaneForPane(terminal_leaf) == NULL ||
	             editorPaneTreeLeafCount(E.layout_root) != 2 ||
	             !editorPaneNodeContainsLeaf(E.layout_root, original);
	return failed;
}

static int test_terminal_pane_close_exited_removes_leaf_and_restores_focus(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *original = E.focused_leaf;
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("true", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
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
	/* An exited TERMINAL tab is closed; its now-empty split pane collapses and
	 * focus returns to the original editor pane. */
	int closed = editorTerminalPaneCloseExitedTabs();
	int failed = closed != 1 || editorPaneTreeLeafCount(E.layout_root) != 1 ||
	             E.focused_leaf != original ||
	             !editorPaneNodeContainsLeaf(E.layout_root, original);
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

/* Regression: with focus in the drawer, a left-click on a terminal pane's tab
 * strip must move focus to that terminal. Previously the terminal mouse handler
 * swallowed the tab-strip click as terminal input and left primary focus in the
 * drawer, so the user had to click a non-terminal tab first. */
static int test_terminal_pane_click_tab_from_drawer_focuses_terminal(void) {
	if (E.layout_root == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	if (!editorTabsInit()) {
		return 1;
	}
	/* Upper editor pane + lower terminal pane. */
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	editorTerminalPaneResizeAllToLayout(E.layout_root);

	struct editorRect viewport = {0};
	struct editorRect rect = {0};
	if (!editorLayoutEditorViewport(&viewport) ||
	    !editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                  terminal_leaf, &rect)) {
		return 1;
	}

	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	/* SGR left-press on the terminal's tab strip: the strip sits one row above the
	 * content rect (0-based rect.y - 1, i.e. 1-based rect.y) at the pane's left. */
	char click[32];
	int n = snprintf(click, sizeof(click), "\x1b[<0;%d;%dM", rect.x + 1, rect.y);
	if (n <= 0 || n >= (int)sizeof(click)) {
		return 1;
	}
	(void)editor_process_keypress_with_input_silent(click, (size_t)n);

	return E.primary_focus != EDITOR_PRIMARY_FOCUS_TEXT || E.focused_leaf != terminal_leaf;
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

static int test_terminal_pane_hydrate_placeholders_spawns_pty(void) {
	struct editorPaneNode *prev_root = E.layout_root;
	struct editorPaneNode *prev_focus = E.focused_leaf;
	int prev_cols = E.window_cols;
	int prev_rows = E.window_rows;
	struct editorPaneNode *root = editorLayoutDeserialize("(h 0.5 leaf term)");
	if (root == NULL) {
		return 1;
	}
	E.layout_root = root;
	E.focused_leaf = editorPaneNodeFirstLeaf(root);
	E.window_cols = 80;
	E.window_rows = 24;

	int failures = editorTerminalPaneHydratePlaceholders(root, "sleep 2");
	struct editorPaneNode *term_leaf = root->as.split.second;
	/* Hydration restores a `term` placeholder as an editor leaf hosting a
	 * TERMINAL tab. */
	int failed = failures != 0 ||
	             editorPaneActiveKind(term_leaf) != EDITOR_PANE_KIND_TERMINAL ||
	             editorTerminalPaneForPane(term_leaf) == NULL;
	if (!failed) {
		(void)editorTerminalPanePumpAll(root);
	}

	editorPaneNodeFree(root);
	E.layout_root = prev_root;
	E.focused_leaf = prev_focus;
	E.window_cols = prev_cols;
	E.window_rows = prev_rows;
	return failed;
}

static int test_terminal_pane_hydrate_placeholders_skips_already_hydrated(void) {
	struct editorPaneNode *prev_root = E.layout_root;
	struct editorPaneNode *prev_focus = E.focused_leaf;
	int prev_cols = E.window_cols;
	int prev_rows = E.window_rows;
	struct editorPaneNode *root = editorLayoutDeserialize("(h 0.5 leaf term)");
	if (root == NULL) {
		return 1;
	}
	E.layout_root = root;
	E.focused_leaf = editorPaneNodeFirstLeaf(root);
	E.window_cols = 80;
	E.window_rows = 24;

	int failures = editorTerminalPaneHydratePlaceholders(root, "sleep 2");
	struct editorPaneNode *term_leaf = root->as.split.second;
	struct editorTerminalPane *first_term = editorTerminalPaneForPane(term_leaf);
	int failed = failures != 0 || first_term == NULL;
	if (!failed) {
		/* A second pass is a no-op: the leaf is now an editor leaf, not a
		 * `term` placeholder, so the terminal tab is left untouched. */
		failures = editorTerminalPaneHydratePlaceholders(root, "sleep 2");
		failed = failures != 0 || editorTerminalPaneForPane(term_leaf) != first_term;
	}

	editorPaneNodeFree(root);
	E.layout_root = prev_root;
	E.focused_leaf = prev_focus;
	E.window_cols = prev_cols;
	E.window_rows = prev_rows;
	return failed;
}

static int test_terminal_pane_open_split_creates_labeled_terminal_tab(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 80;
	E.window_rows = 24;
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *sibling =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_HORIZONTAL);
	if (sibling == NULL) {
		return 1;
	}
	int term_idx = sibling->as.leaf.view.active_tab_idx;
	/* The hosting leaf stays an editor leaf; the terminal lives on the tab,
	 * which is labeled "Terminal" in the strip. */
	int failed = sibling->as.leaf.kind != EDITOR_PANE_KIND_EDITOR ||
	             sibling->as.leaf.view.pane_tab_count != 1 ||
	             editorTabKindAt(term_idx) != EDITOR_PANE_KIND_TERMINAL ||
	             strcmp(editorTabDisplayNameAt(term_idx), "Terminal") != 0;
	return failed;
}

/* After the frame-level reconcile, the terminal's libvterm grid and stored
 * dimensions must match the exact bordered leaf rect the renderer draws into,
 * so the painted slice and the PTY/vterm size cannot drift. */
static int test_terminal_pane_resize_all_matches_layout_rect(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
	if (t == NULL) {
		return 1;
	}
	editorTerminalPaneResizeAllToLayout(E.layout_root);

	struct editorRect viewport = {0};
	struct editorRect rect = {0};
	if (!editorLayoutEditorViewport(&viewport) ||
	    !editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                  terminal_leaf, &rect)) {
		return 1;
	}
	int vrows = 0;
	int vcols = 0;
	vterm_get_size(t->vt, &vrows, &vcols);
	int failed = t->cols != rect.w || t->rows != rect.h || vcols != rect.w || vrows != rect.h;
	return failed;
}

/* editorPaneMoveTab does not resize a moved terminal; the frame reconcile must.
 * Move a terminal from a small pane into a larger neighbor and confirm its grid
 * fills the larger target rect after the reconcile. */
static int test_terminal_pane_move_to_neighbor_matches_target_rect(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	if (!editorTabsInit()) {
		return 1;
	}
	/* Left = original editor pane, right = new terminal pane (focused). */
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep 5", EDITOR_SPLIT_VERTICAL);
	if (terminal_leaf == NULL || !E.layout_root->is_split) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(terminal_leaf);
	if (t == NULL) {
		return 1;
	}
	/* Make the terminal's pane the small one so the target (left) is larger. */
	E.layout_root->as.split.ratio = 0.75;
	editorTerminalPaneResizeAllToLayout(E.layout_root);
	int small_cols = t->cols;

	if (!editorActionMoveActiveTabToNeighborPane(EDITOR_FOCUS_LEFT)) {
		return 1;
	}
	editorTerminalPaneResizeAllToLayout(E.layout_root);

	/* The right pane emptied and the tree collapsed to the single left pane,
	 * which now hosts the terminal as its active tab. */
	struct editorTerminalPane *moved = editorTerminalPaneForPane(E.focused_leaf);
	if (moved != t) {
		return 1;
	}
	struct editorRect viewport = {0};
	struct editorRect rect = {0};
	if (!editorLayoutEditorViewport(&viewport) ||
	    !editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                  E.focused_leaf, &rect)) {
		return 1;
	}
	int failed = t->cols != rect.w || t->rows != rect.h || t->cols <= small_cols;
	return failed;
}

/* Splitting a pane whose active tab is a terminal must not mirror the single-
 * host terminal into the sibling: it stays in the source pane, and the sibling
 * is left empty for the caller to seed. */
static int test_terminal_pane_split_keeps_terminal_single_host(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorTerminalPane *t = editorTerminalPaneCreate("sleep 5", 40, 8);
	if (t == NULL) {
		return 1;
	}
	int term_idx = editorTabCreateWidget(EDITOR_PANE_KIND_TERMINAL, t, editorTerminalPaneFree);
	if (term_idx < 0) {
		editorTerminalPaneFree(t);
		return 1;
	}
	struct editorPaneNode *source = E.focused_leaf;
	if (!editorPaneViewAddTab(&source->as.leaf.view, term_idx) ||
	    !editorTabSwitchToIndex(term_idx)) {
		return 1;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	if (sibling == NULL || sibling->is_split) {
		return 1;
	}
	int failed = editorPaneViewHasTab(&sibling->as.leaf.view, term_idx) ||
	             sibling->as.leaf.view.pane_tab_count != 0 ||
	             sibling->as.leaf.view.active_tab_idx != -1 ||
	             !editorPaneViewHasTab(&source->as.leaf.view, term_idx);
	return failed;
}

/* A new terminal tab is inserted immediately after the active tab in the focused
 * pane and activated, without adding a pane (leaf count unchanged). */
static int test_terminal_new_tab_inserts_beside_active(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *pane = E.focused_leaf;
	int tabs_before = E.tab_count;
	int members_before = pane->as.leaf.view.pane_tab_count;
	int leaves_before = editorPaneTreeLeafCount(E.layout_root);
	int editor_tab = E.active_tab;

	int new_idx = editorTabNewTerminalBesideActive("sleep 5");
	if (new_idx < 0) {
		return 1;
	}
	int failed = E.tab_count != tabs_before + 1 ||
	             pane->as.leaf.view.pane_tab_count != members_before + 1 ||
	             editorPaneTreeLeafCount(E.layout_root) != leaves_before ||
	             E.active_tab != new_idx ||
	             editorTabKindAt(new_idx) != EDITOR_PANE_KIND_TERMINAL ||
	             editorTerminalPaneForPane(pane) == NULL ||
	             /* Order: [editor_tab, new terminal]. */
	             pane->as.leaf.view.pane_tabs[members_before - 1] != editor_tab ||
	             pane->as.leaf.view.pane_tabs[members_before] != new_idx;
	return failed;
}

/* With a following tab present, the new terminal lands between the active tab
 * and the one after it, not at the end. */
static int test_terminal_new_tab_order_with_following_tab(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	if (!editorTabsInit()) {
		return 1;
	}
	struct editorPaneNode *pane = E.focused_leaf;
	int t0 = E.active_tab;
	if (!editorTabNewEmpty()) {
		return 1;
	}
	int t1 = E.active_tab;
	if (!editorTabSwitchToIndex(t0)) {
		return 1;
	}
	int term = editorTabNewTerminalBesideActive("sleep 5");
	if (term < 0) {
		return 1;
	}
	int failed = pane->as.leaf.view.pane_tab_count != 3 ||
	             pane->as.leaf.view.pane_tabs[0] != t0 ||
	             pane->as.leaf.view.pane_tabs[1] != term ||
	             pane->as.leaf.view.pane_tabs[2] != t1 || E.active_tab != term;
	return failed;
}

/* A NULL command is rejected and leaves tab count and active tab unchanged. */
static int test_terminal_new_tab_null_command_leaves_state(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	if (!editorTabsInit()) {
		return 1;
	}
	int tabs_before = E.tab_count;
	int active_before = E.active_tab;
	int members_before = E.focused_leaf->as.leaf.view.pane_tab_count;
	if (editorTabNewTerminalBesideActive(NULL) >= 0) {
		return 1;
	}
	return E.tab_count != tabs_before || E.active_tab != active_before ||
	       E.focused_leaf->as.leaf.view.pane_tab_count != members_before;
}

/* Create a vertical split with a terminal running `command` as the focused
 * (right) pane, under the given input system. Returns the terminal leaf, or NULL
 * on failure; *term_out receives the pane. */
static struct editorPaneNode *setup_focused_terminal(const char *command, const char *system_id,
                                                     struct editorTerminalPane **term_out) {
	E.window_cols = 120;
	E.window_rows = 40;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	if (!editorTabsInit() || !editorInputSystemActivate(system_id)) {
		return NULL;
	}
	struct editorPaneNode *leaf = editorTerminalPaneOpenSplit(command, EDITOR_SPLIT_VERTICAL);
	if (leaf == NULL) {
		return NULL;
	}
	struct editorTerminalPane *t = editorTerminalPaneForPane(leaf);
	if (t == NULL) {
		return NULL;
	}
	if (term_out != NULL) {
		*term_out = t;
	}
	return leaf;
}

static void feed_keys(const char *s) {
	(void)editor_process_keypress_with_input_silent(s, strlen(s));
}

/* Job/Insert mode forwards ordinary bytes (including Space) to the PTY: a `cat`
 * child echoes them back, and the leader is not recognized. */
static int test_terminal_input_vim_insert_forwards_bytes(void) {
	struct editorTerminalPane *t = NULL;
	struct editorPaneNode *leaf = setup_focused_terminal("cat", "vim", &t);
	if (leaf == NULL) {
		return 1;
	}
	feed_keys(" e");
	int shown = wait_for_text_in_screen(t, " e", 2000);
	int failed = !shown || t->input_mode != EDITOR_TERMINAL_INPUT_INSERT ||
	             E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER;
	return failed;
}

/* Esc is never a mode trigger in Job/Insert: Esc-prefixed keys (arrow-up/down,
 * which the terminal sends as ESC [ A / ESC [ B) stay in Insert and do not steal
 * pane focus — they are forwarded to the child. (A lone Esc cannot be fed through
 * the pipe harness: with no following byte it reads as EOF; the real terminal
 * disambiguates it by a timeout. The forwarding path is identical either way.) */
static int test_terminal_input_vim_esc_prefixed_stays_insert(void) {
	struct editorTerminalPane *t = NULL;
	struct editorPaneNode *leaf = setup_focused_terminal("sleep 5", "vim", &t);
	if (leaf == NULL) {
		return 1;
	}
	feed_keys("\x1b[A\x1b[B"); /* Up, Down: Esc-prefixed */
	return t->input_mode != EDITOR_TERMINAL_INPUT_INSERT || E.focused_leaf != leaf;
}

/* Ctrl-W N enters Terminal Normal mode; i returns to Job/Insert. */
static int test_terminal_input_vim_ctrl_w_n_toggles_mode(void) {
	struct editorTerminalPane *t = NULL;
	if (setup_focused_terminal("sleep 5", "vim", &t) == NULL) {
		return 1;
	}
	feed_keys("\x17N");
	if (t->input_mode != EDITOR_TERMINAL_INPUT_NORMAL) {
		return 1;
	}
	feed_keys("i");
	return t->input_mode != EDITOR_TERMINAL_INPUT_INSERT;
}

/* Ctrl-W t opens a new terminal tab beside the current one, from Job/Insert mode
 * and without leaving it or adding a pane. */
static int test_terminal_input_vim_ctrl_w_t_opens_terminal_tab(void) {
	struct editorTerminalPane *t = NULL;
	struct editorPaneNode *leaf = setup_focused_terminal("sleep 5", "vim", &t);
	if (leaf == NULL) {
		return 1;
	}
	int tabs_before = E.tab_count;
	int leaves_before = editorPaneTreeLeafCount(E.layout_root);
	feed_keys("\x17t");
	struct editorTerminalPane *active = editorTerminalPaneForPane(E.focused_leaf);
	int failed = E.tab_count != tabs_before + 1 ||
	             editorPaneTreeLeafCount(E.layout_root) != leaves_before || active == NULL ||
	             active == t || t->input_mode != EDITOR_TERMINAL_INPUT_INSERT;
	return failed;
}

/* Ctrl-W t opens a terminal tab from a non-terminal (editor) pane too, since it
 * resolves through the shared Vim window map. */
static int test_terminal_input_vim_ctrl_w_t_from_editor_pane(void) {
	if (E.layout_root == NULL || E.focused_leaf == NULL) {
		return 1;
	}
	E.window_cols = 120;
	E.window_rows = 40;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	if (!editorTabsInit() || !editorInputSystemActivate("vim")) {
		return 1;
	}
	int tabs_before = E.tab_count;
	int leaves_before = editorPaneTreeLeafCount(E.layout_root);
	feed_keys("\x17t");
	int failed = E.tab_count != tabs_before + 1 ||
	             editorPaneTreeLeafCount(E.layout_root) != leaves_before ||
	             editorTerminalPaneForPane(E.focused_leaf) == NULL;
	return failed;
}

/* Ctrl-W h switches panes directly from Job/Insert mode (no mode change). */
static int test_terminal_input_vim_ctrl_w_switches_pane(void) {
	struct editorTerminalPane *t = NULL;
	struct editorPaneNode *leaf = setup_focused_terminal("sleep 5", "vim", &t);
	if (leaf == NULL || E.focused_leaf != leaf) {
		return 1;
	}
	feed_keys("\x17h");
	int failed = E.focused_leaf == leaf || t->input_mode != EDITOR_TERMINAL_INPUT_INSERT;
	return failed;
}

/* In Terminal Normal mode, <leader>g opens the git drawer (a leader sequence
 * resolving through the shared Vim map); the key is not sent to the child. */
static int test_terminal_input_vim_normal_leader_resolves(void) {
	struct editorTerminalPane *t = NULL;
	if (setup_focused_terminal("sleep 5", "vim", &t) == NULL) {
		return 1;
	}
	feed_keys("\x17N");
	if (t->input_mode != EDITOR_TERMINAL_INPUT_NORMAL) {
		return 1;
	}
	feed_keys(" g");
	return E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER;
}

/* Two terminal tabs keep independent modes; refocusing a terminal clears any
 * in-flight Ctrl-W wait. */
static int test_terminal_input_modes_are_per_tab(void) {
	struct editorTerminalPane *a = NULL;
	struct editorPaneNode *leaf_a = setup_focused_terminal("sleep 5", "vim", &a);
	if (leaf_a == NULL) {
		return 1;
	}
	int a_idx = E.active_tab;
	/* Put A into Normal mode. */
	feed_keys("\x17N");
	if (a->input_mode != EDITOR_TERMINAL_INPUT_NORMAL) {
		return 1;
	}
	/* Second terminal tab in the same pane. */
	struct editorTerminalPane *b = editorTerminalPaneCreate("sleep 5", 40, 8);
	if (b == NULL) {
		return 1;
	}
	int b_idx = editorTabCreateWidget(EDITOR_PANE_KIND_TERMINAL, b, editorTerminalPaneFree);
	if (b_idx < 0) {
		editorTerminalPaneFree(b);
		return 1;
	}
	if (!editorPaneViewAddTab(&E.focused_leaf->as.leaf.view, b_idx) ||
	    !editorTabSwitchToIndex(b_idx)) {
		return 1;
	}
	if (b->input_mode != EDITOR_TERMINAL_INPUT_INSERT ||
	    a->input_mode != EDITOR_TERMINAL_INPUT_NORMAL) {
		return 1;
	}
	/* Arm a Ctrl-W wait on B, then leave and return: the wait must be cleared. */
	feed_keys("\x17");
	if (!b->pending_ctrl_w) {
		return 1;
	}
	(void)editorTabSwitchToIndex(a_idx);
	(void)editorTabSwitchToIndex(b_idx);
	return b->pending_ctrl_w != 0;
}

/* CUA has no modes: Ctrl-Alt-A arms the one-command prefix, and a bare Ctrl-W is
 * forwarded to the child rather than treated as a window prefix. */
static int test_terminal_input_cua_prefix_and_ctrl_w_literal(void) {
	struct editorTerminalPane *t = NULL;
	struct editorPaneNode *leaf = setup_focused_terminal("sleep 5", "cua", &t);
	if (leaf == NULL) {
		return 1;
	}
	/* Bare Ctrl-W does not switch/close a pane under CUA. */
	feed_keys("\x17");
	if (E.focused_leaf != leaf) {
		return 1;
	}
	/* Ctrl-Alt-A (ESC + Ctrl-A) arms the one-command escape. */
	feed_keys("\x1b\x01");
	int failed = !E.terminal_prefix_armed;
	E.terminal_prefix_armed = 0;
	return failed;
}

const struct editorTestCase g_terminal_pane_tests[] = {
        {"terminal_input_vim_insert_forwards_bytes", test_terminal_input_vim_insert_forwards_bytes},
        {"terminal_input_vim_esc_prefixed_stays_insert",
         test_terminal_input_vim_esc_prefixed_stays_insert},
        {"terminal_input_vim_ctrl_w_n_toggles_mode", test_terminal_input_vim_ctrl_w_n_toggles_mode},
        {"terminal_input_vim_ctrl_w_switches_pane", test_terminal_input_vim_ctrl_w_switches_pane},
        {"terminal_input_vim_ctrl_w_t_opens_terminal_tab",
         test_terminal_input_vim_ctrl_w_t_opens_terminal_tab},
        {"terminal_input_vim_ctrl_w_t_from_editor_pane",
         test_terminal_input_vim_ctrl_w_t_from_editor_pane},
        {"terminal_input_vim_normal_leader_resolves",
         test_terminal_input_vim_normal_leader_resolves},
        {"terminal_input_modes_are_per_tab", test_terminal_input_modes_are_per_tab},
        {"terminal_input_cua_prefix_and_ctrl_w_literal",
         test_terminal_input_cua_prefix_and_ctrl_w_literal},
        {"terminal_new_tab_inserts_beside_active", test_terminal_new_tab_inserts_beside_active},
        {"terminal_new_tab_order_with_following_tab",
         test_terminal_new_tab_order_with_following_tab},
        {"terminal_new_tab_null_command_leaves_state",
         test_terminal_new_tab_null_command_leaves_state},
        {"terminal_pane_resize_all_matches_layout_rect",
         test_terminal_pane_resize_all_matches_layout_rect},
        {"terminal_pane_move_to_neighbor_matches_target_rect",
         test_terminal_pane_move_to_neighbor_matches_target_rect},
        {"terminal_pane_split_keeps_terminal_single_host",
         test_terminal_pane_split_keeps_terminal_single_host},
        {"terminal_pane_open_split_creates_labeled_terminal_tab",
         test_terminal_pane_open_split_creates_labeled_terminal_tab},
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
        {"terminal_pane_click_tab_from_drawer_focuses_terminal",
         test_terminal_pane_click_tab_from_drawer_focuses_terminal},
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
        {"terminal_pane_hydrate_placeholders_spawns_pty",
         test_terminal_pane_hydrate_placeholders_spawns_pty},
        {"terminal_pane_hydrate_placeholders_skips_already_hydrated",
         test_terminal_pane_hydrate_placeholders_skips_already_hydrated},
};

const int g_terminal_pane_test_count =
        (int)(sizeof(g_terminal_pane_tests) / sizeof(g_terminal_pane_tests[0]));
