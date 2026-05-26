#include "terminal/terminal_pane.h"

#include "editing/selection.h"
#include "rotide.h"
#include "text/utf8.h"
#include "vterm.h"
#include "vterm_keycodes.h"
#include "workspace/layout.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>

#define TERMINAL_SCROLLBACK_DEFAULT 10000
#define TERMINAL_SCROLLBACK_MAX 1000000

static int g_terminal_scrollback_lines = TERMINAL_SCROLLBACK_DEFAULT;

void editorTerminalPaneSetDefaultScrollbackLines(int lines) {
	if (lines < 0) {
		lines = 0;
	}
	if (lines > TERMINAL_SCROLLBACK_MAX) {
		lines = TERMINAL_SCROLLBACK_MAX;
	}
	g_terminal_scrollback_lines = lines;
}

int editorTerminalPaneGetDefaultScrollbackLines(void) {
	return g_terminal_scrollback_lines;
}

/* Ring helpers. Newest line is at sb_rows[(sb_head-1+sb_cap)%sb_cap]; back=1
 * returns that row, back=2 the next-older, up to back=sb_size. */
static const struct terminalScrollbackRow *
terminalPaneScrollbackAt(const struct editorTerminalPane *t, int back) {
	if (t == NULL || t->sb_rows == NULL || back < 1 || back > t->sb_size) {
		return NULL;
	}
	int idx = ((t->sb_head - back) % t->sb_cap + t->sb_cap) % t->sb_cap;
	return &t->sb_rows[idx];
}

static int terminalPaneClampDim(int v) {
	if (v < 1) {
		return 1;
	}
	if (v > 999) {
		return 999;
	}
	return v;
}

static void terminalPaneOutputCallback(const char *s, size_t len, void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || t->child.master_fd < 0 || s == NULL || len == 0) {
		return;
	}
	ssize_t written = write(t->child.master_fd, s, len);
	(void)written;
}

/* (Re)allocate row_dirty to hold `rows` entries, init all to dirty so the next
 * frame paints from scratch. Returns 1 on success, 0 on OOM (row_dirty kept
 * at its prior size; caller falls back to "everything dirty" via row_dirty==NULL). */
static int terminalPaneAllocRowDirty(struct editorTerminalPane *t, int rows) {
	if (t == NULL || rows <= 0) {
		return 1;
	}
	if (t->row_dirty != NULL && t->row_dirty_cap >= rows) {
		memset(t->row_dirty, 1, (size_t)rows);
		return 1;
	}
	unsigned char *grown = realloc(t->row_dirty, (size_t)rows);
	if (grown == NULL) {
		return 0;
	}
	memset(grown, 1, (size_t)rows);
	t->row_dirty = grown;
	t->row_dirty_cap = rows;
	return 1;
}

static void terminalPaneMarkAllRowsDirty(struct editorTerminalPane *t) {
	if (t == NULL || t->row_dirty == NULL) {
		return;
	}
	int n = t->rows < t->row_dirty_cap ? t->rows : t->row_dirty_cap;
	if (n > 0) {
		memset(t->row_dirty, 1, (size_t)n);
	}
}

static int terminalPaneDamage(VTermRect rect, void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || t->row_dirty == NULL) {
		return 1;
	}
	int r0 = rect.start_row < 0 ? 0 : rect.start_row;
	int r1 = rect.end_row;
	if (r1 > t->row_dirty_cap) {
		r1 = t->row_dirty_cap;
	}
	for (int r = r0; r < r1; r++) {
		t->row_dirty[r] = 1;
	}
	return 1;
}

static int terminalPaneMoverect(VTermRect dest, VTermRect src, void *user) {
	(void)dest;
	(void)src;
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	/* Internal scrolls touch large rectangles and libvterm follows up with
	 * damage events for the new content, but the simplest correct thing is
	 * to repaint every row this frame. */
	terminalPaneMarkAllRowsDirty(t);
	return 1;
}

static int terminalPaneResizeCb(int rows, int cols, void *user) {
	(void)rows;
	(void)cols;
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	terminalPaneMarkAllRowsDirty(t);
	return 1;
}

static int terminalPaneSetTermProp(VTermProp prop, VTermValue *val, void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || val == NULL) {
		return 0;
	}
	switch (prop) {
		case VTERM_PROP_CURSORVISIBLE:
			t->cursor_visible = val->boolean ? 1 : 0;
			return 1;
		case VTERM_PROP_CURSORBLINK:
			t->cursor_blink = val->boolean ? 1 : 0;
			return 1;
		case VTERM_PROP_CURSORSHAPE:
			t->cursor_shape = val->number;
			return 1;
		case VTERM_PROP_MOUSE:
			t->mouse_tracking = val->number;
			return 1;
		default:
			break;
	}
	return 0;
}

static int terminalPaneSbPushline(int cols, const VTermScreenCell *cells, void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || cols <= 0 || cells == NULL) {
		return 1;
	}
	if (t->sb_cap <= 0 || t->sb_rows == NULL) {
		return 1;
	}
	/* Resize the destination slot before mutating any user-visible state — if
	 * realloc fails we drop the row, and the user's selection / scroll_offset
	 * must not skew as if the row had landed. */
	struct terminalScrollbackRow *slot = &t->sb_rows[t->sb_head];
	VTermScreenCell *new_cells = realloc(slot->cells, (size_t)cols * sizeof(*new_cells));
	if (new_cells == NULL) {
		return 1;
	}
	memcpy(new_cells, cells, (size_t)cols * sizeof(*new_cells));
	slot->cells = new_cells;
	slot->cols = cols;
	t->sb_head = (t->sb_head + 1) % t->sb_cap;
	if (t->sb_size < t->sb_cap) {
		t->sb_size += 1;
	}
	/* Now that the row really entered scrollback, shift the selection up by
	 * one and bump scroll_offset so the user's view stays anchored to the
	 * same content. */
	if (t->sel_active) {
		t->sel_anchor_row -= 1;
		t->sel_cursor_row -= 1;
	}
	if (t->scroll_offset > 0 && t->scroll_offset < t->sb_cap) {
		t->scroll_offset += 1;
	}
	if (t->scroll_offset > t->sb_size) {
		t->scroll_offset = t->sb_size;
	}
	/* Live cells shifted up under the user's view (or selection rows shifted
	 * if scrolled), so the whole pane needs a fresh paint this frame. */
	terminalPaneMarkAllRowsDirty(t);
	return 1;
}

static int terminalPaneSbPopline(int cols, VTermScreenCell *cells, void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || cells == NULL || cols <= 0 || t->sb_size <= 0) {
		return 0;
	}
	int back_idx = ((t->sb_head - 1) % t->sb_cap + t->sb_cap) % t->sb_cap;
	struct terminalScrollbackRow *slot = &t->sb_rows[back_idx];
	if (slot->cells == NULL) {
		return 0;
	}
	int copy_cols = slot->cols < cols ? slot->cols : cols;
	memcpy(cells, slot->cells, (size_t)copy_cols * sizeof(*cells));
	for (int c = copy_cols; c < cols; c++) {
		memset(&cells[c], 0, sizeof(cells[c]));
	}
	/* Inverse of push: selection rows shift down. */
	if (t->sel_active) {
		t->sel_anchor_row += 1;
		t->sel_cursor_row += 1;
	}
	if (t->scroll_offset > 0) {
		t->scroll_offset -= 1;
	}
	t->sb_head = back_idx;
	t->sb_size -= 1;
	terminalPaneMarkAllRowsDirty(t);
	return 1;
}

static int terminalPaneSbClear(void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || t->sb_rows == NULL) {
		return 1;
	}
	for (int i = 0; i < t->sb_cap; i++) {
		free(t->sb_rows[i].cells);
		t->sb_rows[i].cells = NULL;
		t->sb_rows[i].cols = 0;
	}
	t->sb_size = 0;
	t->sb_head = 0;
	t->scroll_offset = 0;
	t->sel_active = 0;
	terminalPaneMarkAllRowsDirty(t);
	return 1;
}

static const VTermScreenCallbacks g_terminal_pane_screen_callbacks = {
        .damage = terminalPaneDamage,
        .moverect = terminalPaneMoverect,
        .settermprop = terminalPaneSetTermProp,
        .resize = terminalPaneResizeCb,
        .sb_pushline = terminalPaneSbPushline,
        .sb_popline = terminalPaneSbPopline,
        .sb_clear = terminalPaneSbClear,
};

struct editorTerminalPane *editorTerminalPaneCreate(const char *command, int cols, int rows) {
	if (command == NULL) {
		errno = EINVAL;
		return NULL;
	}
	cols = terminalPaneClampDim(cols);
	rows = terminalPaneClampDim(rows);

	struct editorTerminalPane *t = malloc(sizeof(*t));
	if (t == NULL) {
		return NULL;
	}
	memset(t, 0, sizeof(*t));
	editorPtyChildInit(&t->child);
	t->cols = cols;
	t->rows = rows;
	t->cursor_visible = 1;
	t->cursor_blink = 1;
	t->cursor_shape = VTERM_PROP_CURSORSHAPE_BLOCK;

	int sb_cap = g_terminal_scrollback_lines;
	if (sb_cap > 0) {
		t->sb_rows = calloc((size_t)sb_cap, sizeof(*t->sb_rows));
		if (t->sb_rows != NULL) {
			t->sb_cap = sb_cap;
		}
	}
	(void)terminalPaneAllocRowDirty(t, rows);

	t->vt = vterm_new(rows, cols);
	if (t->vt == NULL) {
		free(t);
		return NULL;
	}
	vterm_set_utf8(t->vt, 1);
	vterm_output_set_callback(t->vt, terminalPaneOutputCallback, t);
	t->screen = vterm_obtain_screen(t->vt);
	if (t->screen == NULL) {
		vterm_free(t->vt);
		free(t);
		return NULL;
	}
	vterm_screen_set_callbacks(t->screen, &g_terminal_pane_screen_callbacks, t);
	vterm_screen_reset(t->screen, 1);

	if (!editorPtySpawn(command, cols, rows, &t->child)) {
		int saved = errno;
		vterm_free(t->vt);
		free(t);
		errno = saved;
		return NULL;
	}

	return t;
}

void editorTerminalPaneFree(void *pane) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)pane;
	if (t == NULL) {
		return;
	}
	editorPtyClose(&t->child);
	if (t->vt != NULL) {
		vterm_free(t->vt);
		t->vt = NULL;
		t->screen = NULL;
	}
	if (t->sb_rows != NULL) {
		for (int i = 0; i < t->sb_cap; i++) {
			free(t->sb_rows[i].cells);
		}
		free(t->sb_rows);
		t->sb_rows = NULL;
	}
	free(t->render_row_scratch);
	t->render_row_scratch = NULL;
	free(t->row_dirty);
	t->row_dirty = NULL;
	free(t);
}

VTermScreenCell *editorTerminalPaneEnsureRenderRowScratch(struct editorTerminalPane *terminal,
                                                          int cells) {
	if (terminal == NULL || cells <= 0) {
		return NULL;
	}
	if (terminal->render_row_scratch_cap >= cells) {
		return terminal->render_row_scratch;
	}
	VTermScreenCell *grown =
	        realloc(terminal->render_row_scratch, (size_t)cells * sizeof(*grown));
	if (grown == NULL) {
		return NULL;
	}
	terminal->render_row_scratch = grown;
	terminal->render_row_scratch_cap = cells;
	return grown;
}

int editorTerminalPanePump(struct editorTerminalPane *terminal) {
	if (terminal == NULL || terminal->vt == NULL) {
		return 0;
	}
	int total = 0;
	if (terminal->child.master_fd >= 0) {
		/* Larger buffer amortizes the per-read syscall + vterm_input_write
		 * call overhead under flood. Drop the old short-read break — on a
		 * non-blocking fd the next read returns EAGAIN and we stop anyway,
		 * and bursts that perfectly fill a 4 KB chunk used to cost us the
		 * follow-up read. */
		char buf[32 * 1024];
		/* Per-pump cap so one runaway pane can't starve siblings or the
		 * input loop. After this many bytes we bail; the main loop will
		 * come back around and drain more on the next iteration. */
		const int per_pump_cap = 1024 * 1024;
		for (;;) {
			ssize_t n = read(terminal->child.master_fd, buf, sizeof(buf));
			if (n > 0) {
				vterm_input_write(terminal->vt, buf, (size_t)n);
				total += (int)n;
				if (total >= per_pump_cap) {
					break;
				}
				continue;
			}
			if (n == 0) {
				/* EOF: child closed slave side. */
				break;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			break;
		}
		vterm_screen_flush_damage(terminal->screen);
	}
	if (!terminal->exited && terminal->child.pid > 0) {
		int status = 0;
		int r = editorPtyTryReap(&terminal->child, &status);
		if (r == 1) {
			terminal->exited = 1;
			terminal->exit_status = status;
			/* Treat exit as activity so callers can close the pane promptly. */
			total += 1;
		}
	}
	return total;
}

int editorTerminalPaneResize(struct editorTerminalPane *terminal, int cols, int rows) {
	if (terminal == NULL || terminal->vt == NULL) {
		return 0;
	}
	cols = terminalPaneClampDim(cols);
	rows = terminalPaneClampDim(rows);
	vterm_set_size(terminal->vt, rows, cols);
	terminal->cols = cols;
	terminal->rows = rows;
	(void)terminalPaneAllocRowDirty(terminal, rows);
	if (terminal->child.master_fd >= 0) {
		(void)editorPtyResize(&terminal->child, cols, rows);
	}
	return 1;
}

int editorTerminalPaneWrite(struct editorTerminalPane *terminal, const char *bytes, size_t len) {
	if (terminal == NULL || terminal->child.master_fd < 0 || bytes == NULL || len == 0) {
		return 0;
	}
	ssize_t n = write(terminal->child.master_fd, bytes, len);
	if (n < 0) {
		return 0;
	}
	return (int)n;
}

static VTermModifier terminalPaneModifiersToVterm(int rotide_modifiers) {
	VTermModifier mod = VTERM_MOD_NONE;
	if (rotide_modifiers & EDITOR_MOUSE_MOD_SHIFT) {
		mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
	}
	if (rotide_modifiers & EDITOR_MOUSE_MOD_ALT) {
		mod = (VTermModifier)(mod | VTERM_MOD_ALT);
	}
	if (rotide_modifiers & EDITOR_MOUSE_MOD_CTRL) {
		mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
	}
	return mod;
}

int editorTerminalPaneSendMouseButton(struct editorTerminalPane *terminal, int button, int pressed,
                                      int row, int col, int rotide_modifiers) {
	if (terminal == NULL || terminal->vt == NULL || terminal->mouse_tracking <= 0) {
		return 0;
	}
	VTermModifier mod = terminalPaneModifiersToVterm(rotide_modifiers);
	vterm_mouse_move(terminal->vt, row, col, mod);
	vterm_mouse_button(terminal->vt, button, pressed != 0, mod);
	return 1;
}

int editorTerminalPaneSendMouseMove(struct editorTerminalPane *terminal, int row, int col,
                                    int rotide_modifiers) {
	if (terminal == NULL || terminal->vt == NULL || terminal->mouse_tracking <= 0) {
		return 0;
	}
	VTermModifier mod = terminalPaneModifiersToVterm(rotide_modifiers);
	vterm_mouse_move(terminal->vt, row, col, mod);
	return 1;
}

int editorTerminalPaneSendPasteStart(struct editorTerminalPane *terminal) {
	if (terminal == NULL || terminal->vt == NULL) {
		return 0;
	}
	vterm_keyboard_start_paste(terminal->vt);
	return 1;
}

int editorTerminalPaneSendPasteEnd(struct editorTerminalPane *terminal) {
	if (terminal == NULL || terminal->vt == NULL) {
		return 0;
	}
	vterm_keyboard_end_paste(terminal->vt);
	return 1;
}

int editorTerminalPaneSendKey(struct editorTerminalPane *terminal, int rotide_key) {
	if (terminal == NULL || terminal->vt == NULL || terminal->child.master_fd < 0) {
		return 0;
	}
	/* Any key returns the pane to live view and drops a stale selection.
	 * Mirrors how most terminals (iterm, tmux copy-mode) behave. */
	if (terminal->sel_active || terminal->scroll_offset != 0) {
		terminal->sel_active = 0;
		terminal->scroll_offset = 0;
		terminalPaneMarkAllRowsDirty(terminal);
	}
	/* Route printables through vterm. */
	if (rotide_key >= 0x20 && rotide_key < 0x7f) {
		vterm_keyboard_unichar(terminal->vt, (uint32_t)rotide_key, VTERM_MOD_NONE);
		return 1;
	}
	VTermKey vk = VTERM_KEY_NONE;
	VTermModifier mod = VTERM_MOD_NONE;
	switch (rotide_key) {
		case '\r':
			vk = VTERM_KEY_ENTER;
			break;
		case 27: /* esc */
			vk = VTERM_KEY_ESCAPE;
			break;
		case '\t':
			vk = VTERM_KEY_TAB;
			break;
		case BACKSPACE:
			vk = VTERM_KEY_BACKSPACE;
			break;
		case ARROW_UP:
			vk = VTERM_KEY_UP;
			break;
		case ARROW_DOWN:
			vk = VTERM_KEY_DOWN;
			break;
		case ARROW_LEFT:
			vk = VTERM_KEY_LEFT;
			break;
		case ARROW_RIGHT:
			vk = VTERM_KEY_RIGHT;
			break;
		case DEL_KEY:
			vk = VTERM_KEY_DEL;
			break;
		case HOME_KEY:
			vk = VTERM_KEY_HOME;
			break;
		case END_KEY:
			vk = VTERM_KEY_END;
			break;
		case PAGE_UP:
			vk = VTERM_KEY_PAGEUP;
			break;
		case PAGE_DOWN:
			vk = VTERM_KEY_PAGEDOWN;
			break;
		default:
			break;
	}
	if (vk != VTERM_KEY_NONE) {
		vterm_keyboard_key(terminal->vt, vk, mod);
		return 1;
	}
	/* Forward control bytes directly. */
	if (rotide_key > 0 && rotide_key < 0x20) {
		char b = (char)rotide_key;
		ssize_t written = write(terminal->child.master_fd, &b, 1);
		(void)written;
		return 1;
	}
	return 0;
}

int editorTerminalPaneScrollBy(struct editorTerminalPane *terminal, int lines) {
	if (terminal == NULL) {
		return 0;
	}
	int target = terminal->scroll_offset + lines;
	if (target < 0) {
		target = 0;
	}
	if (target > terminal->sb_size) {
		target = terminal->sb_size;
	}
	if (target == terminal->scroll_offset) {
		return 0;
	}
	terminal->scroll_offset = target;
	terminalPaneMarkAllRowsDirty(terminal);
	return 1;
}

int editorTerminalPaneScrollReset(struct editorTerminalPane *terminal) {
	if (terminal == NULL || terminal->scroll_offset == 0) {
		return 0;
	}
	terminal->scroll_offset = 0;
	terminalPaneMarkAllRowsDirty(terminal);
	return 1;
}

int editorTerminalPaneGetLogRow(const struct editorTerminalPane *terminal, int row,
                                VTermScreenCell *cells_out) {
	if (terminal == NULL || cells_out == NULL || terminal->cols <= 0) {
		return 0;
	}
	int cols = terminal->cols;
	if (row >= 0) {
		if (row >= terminal->rows || terminal->screen == NULL) {
			return 0;
		}
		for (int c = 0; c < cols; c++) {
			VTermPos pos = {.row = row, .col = c};
			VTermScreenCell cell;
			memset(&cell, 0, sizeof(cell));
			(void)vterm_screen_get_cell(terminal->screen, pos, &cell);
			cells_out[c] = cell;
		}
		return 1;
	}
	int back = -row;
	const struct terminalScrollbackRow *sb = terminalPaneScrollbackAt(terminal, back);
	if (sb == NULL || sb->cells == NULL) {
		return 0;
	}
	int copy = sb->cols < cols ? sb->cols : cols;
	memcpy(cells_out, sb->cells, (size_t)copy * sizeof(*cells_out));
	for (int c = copy; c < cols; c++) {
		memset(&cells_out[c], 0, sizeof(cells_out[c]));
	}
	return 1;
}

/* Sort (anchor, cursor) into (start, end) in reading order — top-to-bottom,
 * left-to-right. */
static void terminalPaneSelectionBounds(const struct editorTerminalPane *t, int *r0, int *c0,
                                        int *r1, int *c1) {
	int ar = t->sel_anchor_row;
	int ac = t->sel_anchor_col;
	int br = t->sel_cursor_row;
	int bc = t->sel_cursor_col;
	if (ar < br || (ar == br && ac <= bc)) {
		*r0 = ar;
		*c0 = ac;
		*r1 = br;
		*c1 = bc;
	} else {
		*r0 = br;
		*c0 = bc;
		*r1 = ar;
		*c1 = ac;
	}
}

void editorTerminalPaneSelectionBegin(struct editorTerminalPane *terminal, int row, int col) {
	if (terminal == NULL) {
		return;
	}
	terminal->sel_active = 1;
	terminal->sel_anchor_row = row;
	terminal->sel_anchor_col = col;
	terminal->sel_cursor_row = row;
	terminal->sel_cursor_col = col;
	terminalPaneMarkAllRowsDirty(terminal);
}

void editorTerminalPaneSelectionUpdate(struct editorTerminalPane *terminal, int row, int col) {
	if (terminal == NULL || !terminal->sel_active) {
		return;
	}
	if (terminal->sel_cursor_row == row && terminal->sel_cursor_col == col) {
		return;
	}
	terminal->sel_cursor_row = row;
	terminal->sel_cursor_col = col;
	terminalPaneMarkAllRowsDirty(terminal);
}

void editorTerminalPaneSelectionClear(struct editorTerminalPane *terminal) {
	if (terminal == NULL || !terminal->sel_active) {
		return;
	}
	terminal->sel_active = 0;
	terminalPaneMarkAllRowsDirty(terminal);
}

int editorTerminalPaneSelectionContains(const struct editorTerminalPane *terminal, int row,
                                        int col) {
	if (terminal == NULL || !terminal->sel_active) {
		return 0;
	}
	int r0;
	int c0;
	int r1;
	int c1;
	terminalPaneSelectionBounds(terminal, &r0, &c0, &r1, &c1);
	if (r0 == r1) {
		return row == r0 && col >= c0 && col < c1;
	}
	if (row < r0 || row > r1) {
		return 0;
	}
	if (row == r0) {
		return col >= c0;
	}
	if (row == r1) {
		return col < c1;
	}
	return 1;
}

char *editorTerminalPaneSelectionExtract(const struct editorTerminalPane *terminal,
                                         size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (terminal == NULL || !terminal->sel_active || terminal->cols <= 0) {
		return NULL;
	}
	int r0;
	int c0;
	int r1;
	int c1;
	terminalPaneSelectionBounds(terminal, &r0, &c0, &r1, &c1);
	int cols = terminal->cols;
	VTermScreenCell *row_cells = calloc((size_t)cols, sizeof(*row_cells));
	if (row_cells == NULL) {
		return NULL;
	}
	size_t cap = 256;
	size_t len = 0;
	char *buf = malloc(cap);
	if (buf == NULL) {
		free(row_cells);
		return NULL;
	}
	for (int row = r0; row <= r1; row++) {
		if (!editorTerminalPaneGetLogRow(terminal, row, row_cells)) {
			continue;
		}
		int col_start = (row == r0) ? c0 : 0;
		int col_end = (row == r1) ? c1 : cols;
		if (col_start < 0) {
			col_start = 0;
		}
		if (col_end > cols) {
			col_end = cols;
		}
		/* Trim trailing blanks of each row except when the user explicitly
		 * selected up to (and possibly past) a blank tail on a partial row. */
		int row_end = col_end;
		if (row != r1 || col_end == cols) {
			while (row_end > col_start) {
				const VTermScreenCell *cell = &row_cells[row_end - 1];
				if (cell->chars[0] != 0 && cell->chars[0] != ' ') {
					break;
				}
				if (cell->chars[0] == ' ') {
					row_end--;
					continue;
				}
				row_end--;
			}
		}
		for (int c = col_start; c < row_end; c++) {
			const VTermScreenCell *cell = &row_cells[c];
			if (cell->chars[0] == (uint32_t)-1) {
				/* Continuation half of a wide cell. */
				continue;
			}
			if (cell->chars[0] == 0) {
				if (len + 1 >= cap) {
					size_t ncap = cap * 2;
					char *nb = realloc(buf, ncap);
					if (nb == NULL) {
						goto oom;
					}
					buf = nb;
					cap = ncap;
				}
				buf[len++] = ' ';
				continue;
			}
			for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell->chars[i] != 0; i++) {
				char utf8[4];
				int n = editorUtf8EncodeCodepoint(cell->chars[i], utf8);
				if (n <= 0) {
					continue;
				}
				if (len + (size_t)n + 1 >= cap) {
					size_t ncap = cap * 2;
					while (len + (size_t)n + 1 >= ncap) {
						ncap *= 2;
					}
					char *nb = realloc(buf, ncap);
					if (nb == NULL) {
						goto oom;
					}
					buf = nb;
					cap = ncap;
				}
				memcpy(buf + len, utf8, (size_t)n);
				len += (size_t)n;
			}
		}
		if (row != r1) {
			if (len + 1 >= cap) {
				size_t ncap = cap * 2;
				char *nb = realloc(buf, ncap);
				if (nb == NULL) {
					goto oom;
				}
				buf = nb;
				cap = ncap;
			}
			buf[len++] = '\n';
		}
	}
	if (len + 1 >= cap) {
		char *nb = realloc(buf, len + 1);
		if (nb == NULL) {
			goto oom;
		}
		buf = nb;
	}
	buf[len] = '\0';
	free(row_cells);
	if (len_out != NULL) {
		*len_out = len;
	}
	return buf;

oom:
	free(buf);
	free(row_cells);
	return NULL;
}

int editorTerminalPaneCopySelection(struct editorTerminalPane *terminal) {
	if (terminal == NULL || !terminal->sel_active) {
		return 0;
	}
	size_t len = 0;
	char *text = editorTerminalPaneSelectionExtract(terminal, &len);
	if (text == NULL || len == 0) {
		free(text);
		return 0;
	}
	int ok = editorClipboardSet(text, len);
	free(text);
	return ok;
}

struct editorPaneNode *editorPaneNodeNewTerminalLeaf(const char *command, int cols, int rows) {
	struct editorTerminalPane *terminal = editorTerminalPaneCreate(command, cols, rows);
	if (terminal == NULL) {
		return NULL;
	}
	struct editorPaneNode *node = editorPaneNodeNewLeaf(EDITOR_PANE_KIND_TERMINAL);
	if (node == NULL) {
		int saved = errno;
		editorTerminalPaneFree(terminal);
		errno = saved;
		return NULL;
	}
	node->as.leaf.kind_state = terminal;
	node->as.leaf.kind_state_free = editorTerminalPaneFree;
	return node;
}

int editorTerminalPanePumpAll(struct editorPaneNode *root) {
	if (root == NULL) {
		return 0;
	}
	if (root->is_split) {
		return editorTerminalPanePumpAll(root->as.split.first) +
		       editorTerminalPanePumpAll(root->as.split.second);
	}
	if (root->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL && root->as.leaf.kind_state != NULL) {
		return editorTerminalPanePump(
		        (struct editorTerminalPane *)root->as.leaf.kind_state);
	}
	return 0;
}

int editorTerminalPaneCollectMasterFds(struct editorPaneNode *root, int *fds_out, int capacity) {
	if (root == NULL) {
		return 0;
	}
	if (root->is_split) {
		int first =
		        editorTerminalPaneCollectMasterFds(root->as.split.first, fds_out, capacity);
		int *next_out = NULL;
		int next_cap = 0;
		if (first < capacity && fds_out != NULL) {
			next_out = fds_out + first;
			next_cap = capacity - first;
		}
		int second = editorTerminalPaneCollectMasterFds(root->as.split.second, next_out,
		                                                next_cap);
		return first + second;
	}
	if (root->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL || root->as.leaf.kind_state == NULL) {
		return 0;
	}
	struct editorTerminalPane *t = (struct editorTerminalPane *)root->as.leaf.kind_state;
	if (t->child.master_fd < 0) {
		return 0;
	}
	if (fds_out != NULL && capacity > 0) {
		fds_out[0] = t->child.master_fd;
	}
	return 1;
}

static void terminalPaneResizeRecursive(struct editorPaneNode *node, struct editorRect rect,
                                        int border_size) {
	if (node == NULL) {
		return;
	}
	if (!node->is_split) {
		if (node->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
		    node->as.leaf.kind_state != NULL && rect.w > 0 && rect.h > 0) {
			(void)editorTerminalPaneResize(
			        (struct editorTerminalPane *)node->as.leaf.kind_state, rect.w,
			        rect.h);
		}
		return;
	}
	/* Keep split math aligned with layout renderer. */
	struct editorRect first_rect = rect;
	struct editorRect second_rect = rect;
	double ratio = node->as.split.ratio;
	if (ratio < 0.0) {
		ratio = 0.0;
	} else if (ratio > 1.0) {
		ratio = 1.0;
	}
	if (node->as.split.orientation == EDITOR_SPLIT_VERTICAL) {
		int available = rect.w - border_size;
		if (available < 0) {
			available = 0;
		}
		int first_w = (int)((double)available * ratio);
		if (first_w < 0) {
			first_w = 0;
		}
		if (first_w > available) {
			first_w = available;
		}
		first_rect.w = first_w;
		second_rect.x = rect.x + first_w + border_size;
		second_rect.w = available - first_w;
	} else {
		int available = rect.h - border_size;
		if (available < 0) {
			available = 0;
		}
		int first_h = (int)((double)available * ratio);
		if (first_h < 0) {
			first_h = 0;
		}
		if (first_h > available) {
			first_h = available;
		}
		first_rect.h = first_h;
		second_rect.y = rect.y + first_h + border_size;
		second_rect.h = available - first_h;
	}
	terminalPaneResizeRecursive(node->as.split.first, first_rect, border_size);
	terminalPaneResizeRecursive(node->as.split.second, second_rect, border_size);
}

void editorTerminalPaneResizeAllToLayout(struct editorPaneNode *root) {
	if (root == NULL) {
		return;
	}
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return;
	}
	terminalPaneResizeRecursive(root, viewport, ROTIDE_PANE_BORDER_SIZE);
}

int editorTerminalPaneTreeHasTerminal(const struct editorPaneNode *root) {
	if (root == NULL) {
		return 0;
	}
	if (root->is_split) {
		return editorTerminalPaneTreeHasTerminal(root->as.split.first) ||
		       editorTerminalPaneTreeHasTerminal(root->as.split.second);
	}
	return root->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL;
}

static struct editorPaneNode *terminalPaneFindFirstExitedLeaf(struct editorPaneNode *root) {
	if (root == NULL) {
		return NULL;
	}
	if (root->is_split) {
		struct editorPaneNode *found =
		        terminalPaneFindFirstExitedLeaf(root->as.split.first);
		if (found != NULL) {
			return found;
		}
		return terminalPaneFindFirstExitedLeaf(root->as.split.second);
	}
	if (root->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL || root->as.leaf.kind_state == NULL) {
		return NULL;
	}
	struct editorTerminalPane *terminal = (struct editorTerminalPane *)root->as.leaf.kind_state;
	return terminal->exited ? root : NULL;
}

int editorTerminalPaneCloseExited(struct editorPaneNode **root_ptr,
                                  struct editorPaneNode **focused_leaf_ptr,
                                  struct editorPaneNode **tracked_leaf_ptr) {
	if (root_ptr == NULL || *root_ptr == NULL) {
		return 0;
	}
	int closed = 0;
	for (;;) {
		struct editorPaneNode *exited_leaf = terminalPaneFindFirstExitedLeaf(*root_ptr);
		if (exited_leaf == NULL) {
			break;
		}
		if (tracked_leaf_ptr != NULL && *tracked_leaf_ptr == exited_leaf) {
			*tracked_leaf_ptr = NULL;
		}
		struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(root_ptr, exited_leaf);
		if (new_focus == NULL) {
			/* Single-root leaf cannot be removed. */
			break;
		}
		if (focused_leaf_ptr != NULL &&
		    (*focused_leaf_ptr == NULL || *focused_leaf_ptr == exited_leaf ||
		     !editorPaneNodeContainsLeaf(*root_ptr, *focused_leaf_ptr))) {
			*focused_leaf_ptr = new_focus;
		}
		closed++;
	}
	return closed;
}

struct editorPaneNode *editorTerminalPaneOpenSplit(const char *command, int orientation) {
	if (command == NULL) {
		errno = EINVAL;
		return NULL;
	}
	struct editorPaneNode *sibling =
	        editorLayoutSplitFocused((enum editorSplitOrientation)orientation, 0.5);
	if (sibling == NULL) {
		return NULL;
	}
	struct editorRect rect = {0};
	int cols = 80;
	int rows = 24;
	if (editorLayoutFocusedLeafRect(&rect) && rect.w > 0 && rect.h > 0) {
		cols = rect.w;
		rows = rect.h;
	}
	struct editorTerminalPane *terminal = editorTerminalPaneCreate(command, cols, rows);
	if (terminal == NULL) {
		return NULL;
	}
	sibling->as.leaf.kind = EDITOR_PANE_KIND_TERMINAL;
	sibling->as.leaf.kind_state = terminal;
	sibling->as.leaf.kind_state_free = editorTerminalPaneFree;
	return sibling;
}
