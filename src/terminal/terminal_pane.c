#include "terminal/terminal_pane.h"

#include "editing/selection.h"
#include "rotide.h"
#include "terminal/pty.h"
#include "text/utf8.h"
#include "vterm.h"
#include "vterm_keycodes.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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
	return 1;
}

/* The renderer composites every terminal slice from VTermScreen each frame, so
 * no damage/moverect/resize callback is needed: those existed only to drive the
 * removed per-row dirty tracking. Scrollback and term-prop callbacks remain
 * because libvterm requires them for screen/scrollback semantics. */
static const VTermScreenCallbacks g_terminal_pane_screen_callbacks = {
        .settermprop = terminalPaneSetTermProp,
        .sb_pushline = terminalPaneSbPushline,
        .sb_popline = terminalPaneSbPopline,
        .sb_clear = terminalPaneSbClear,
};

/* Allocates and initializes a terminal pane (vterm + scrollback + dirty rows)
 * with no PTY attached yet. The caller attaches a PTY via editorPtySpawn or
 * editorPtyOpenWithoutChild. On failure returns NULL with errno set. */
static struct editorTerminalPane *terminalPaneAlloc(int cols, int rows) {
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

	t->vt = vterm_new(rows, cols);
	if (t->vt == NULL) {
		editorTerminalPaneFree(t);
		return NULL;
	}
	vterm_set_utf8(t->vt, 1);
	vterm_output_set_callback(t->vt, terminalPaneOutputCallback, t);
	t->screen = vterm_obtain_screen(t->vt);
	if (t->screen == NULL) {
		editorTerminalPaneFree(t);
		return NULL;
	}
	vterm_screen_set_callbacks(t->screen, &g_terminal_pane_screen_callbacks, t);
	vterm_screen_reset(t->screen, 1);
	return t;
}

struct editorTerminalPane *editorTerminalPaneCreate(const char *command, int cols, int rows) {
	if (command == NULL) {
		errno = EINVAL;
		return NULL;
	}
	struct editorTerminalPane *t = terminalPaneAlloc(cols, rows);
	if (t == NULL) {
		return NULL;
	}
	if (!editorPtySpawn(command, t->cols, t->rows, &t->child)) {
		int saved = errno;
		editorTerminalPaneFree(t);
		errno = saved;
		return NULL;
	}
	return t;
}

struct editorTerminalPane *editorTerminalPaneCreateDetached(int cols, int rows, char *slave_path,
                                                            size_t slave_path_size) {
	struct editorTerminalPane *t = terminalPaneAlloc(cols, rows);
	if (t == NULL) {
		return NULL;
	}
	if (!editorPtyOpenWithoutChild(t->cols, t->rows, &t->child, slave_path, slave_path_size)) {
		int saved = errno;
		editorTerminalPaneFree(t);
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
	/* Idempotent: skip the libvterm reflow and the child SIGWINCH when the grid
	 * already matches. This lets the frame-level reconcile in screenDrawRows()
	 * call us every frame cheaply. */
	if (cols == terminal->cols && rows == terminal->rows) {
		return 1;
	}
	vterm_set_size(terminal->vt, rows, cols);
	terminal->cols = cols;
	terminal->rows = rows;
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
	return 1;
}

int editorTerminalPaneScrollReset(struct editorTerminalPane *terminal) {
	if (terminal == NULL || terminal->scroll_offset == 0) {
		return 0;
	}
	terminal->scroll_offset = 0;
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
}

void editorTerminalPaneSelectionClear(struct editorTerminalPane *terminal) {
	if (terminal == NULL || !terminal->sel_active) {
		return;
	}
	terminal->sel_active = 0;
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

int editorTerminalPanePumpAll(struct editorPaneNode *root) {
	(void)root;
	int n = 0;
	for (int i = 0; i < E.tab_count; i++) {
		if (editorTabKindAt(i) != EDITOR_PANE_KIND_TERMINAL) {
			continue;
		}
		struct editorTerminalPane *t = (struct editorTerminalPane *)editorTabPayloadAt(i);
		if (t != NULL) {
			n += editorTerminalPanePump(t);
		}
	}
	return n;
}

/* Append one fd if valid; returns 1 if the fd exists (regardless of capacity). */
static int terminalPaneAppendFd(struct editorTerminalPane *t, int *fds_out, int capacity) {
	if (t == NULL || t->child.master_fd < 0) {
		return 0;
	}
	if (fds_out != NULL && capacity > 0) {
		fds_out[0] = t->child.master_fd;
	}
	return 1;
}

int editorTerminalPaneCollectMasterFds(struct editorPaneNode *root, int *fds_out, int capacity) {
	(void)root;
	int count = 0;
	for (int i = 0; i < E.tab_count; i++) {
		if (editorTabKindAt(i) != EDITOR_PANE_KIND_TERMINAL) {
			continue;
		}
		int *next_out = NULL;
		int next_cap = 0;
		if (count < capacity && fds_out != NULL) {
			next_out = fds_out + count;
			next_cap = capacity - count;
		}
		count += terminalPaneAppendFd((struct editorTerminalPane *)editorTabPayloadAt(i),
		                              next_out, next_cap);
	}
	return count;
}

void editorTerminalPaneResizeAllToLayout(struct editorPaneNode *root) {
	if (root == NULL) {
		return;
	}
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return;
	}
	/* Consume the exact leaf rectangles the renderer draws into rather than
	 * re-deriving split math here, so the libvterm grid and the painted slice
	 * cannot drift. editorTerminalPaneResize() is idempotent, so this is a cheap
	 * no-op for every terminal whose rect is unchanged. */
	struct editorLeafLayout layout = {0};
	if (!editorLayoutComputeBorderedInto(root, viewport, ROTIDE_PANE_BORDER_SIZE, &layout)) {
		return;
	}
	for (int i = 0; i < layout.count; i++) {
		struct editorTerminalPane *tab_term =
		        editorTerminalPaneForPane(layout.rects[i].node);
		struct editorRect rect = layout.rects[i].rect;
		/* The active TERMINAL tab sizes to the full content rect; its tab strip
		 * lives in the border row above. */
		if (tab_term != NULL && rect.w > 0 && rect.h > 0) {
			(void)editorTerminalPaneResize(tab_term, rect.w, rect.h);
		}
	}
	editorLeafLayoutFree(&layout);
}

int editorTerminalPaneTreeHasTerminal(const struct editorPaneNode *root) {
	(void)root;
	for (int i = 0; i < E.tab_count; i++) {
		if (editorTabKindAt(i) == EDITOR_PANE_KIND_TERMINAL) {
			return 1;
		}
	}
	return 0;
}

int editorTerminalPaneCloseExitedTabs(void) {
	int closed = 0;
	for (;;) {
		/* In a single-pane layout an exited terminal tab cannot collapse a pane;
		 * closing it would just replace it with an empty editor and hide the
		 * "[exited]" status. Leave it for the user to close (mirrors how the old
		 * sole terminal leaf could not be auto-closed). */
		if (editorPaneTreeLeafCount(E.layout_root) <= 1) {
			break;
		}
		int exited_idx = -1;
		for (int i = 0; i < E.tab_count; i++) {
			if (editorTabKindAt(i) != EDITOR_PANE_KIND_TERMINAL) {
				continue;
			}
			struct editorTerminalPane *t =
			        (struct editorTerminalPane *)editorTabPayloadAt(i);
			if (t != NULL && t->exited) {
				exited_idx = i;
				break;
			}
		}
		if (exited_idx < 0 || !editorTabCloseAt(exited_idx)) {
			break;
		}
		closed++;
	}
	return closed;
}

struct editorTerminalPane *editorTerminalPaneForPane(const struct editorPaneNode *pane) {
	if (pane == NULL || pane->is_split ||
	    editorPaneActiveKind(pane) != EDITOR_PANE_KIND_TERMINAL) {
		return NULL;
	}
	return (struct editorTerminalPane *)editorTabPayloadAt(pane->as.leaf.view.active_tab_idx);
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
	/* The terminal is a TERMINAL tab in the freshly split (editor) pane, not a
	 * terminal leaf kind. */
	if (editorTabAdoptInPane(sibling, EDITOR_PANE_KIND_TERMINAL, terminal,
	                         editorTerminalPaneFree) < 0) {
		editorTerminalPaneFree(terminal);
		return NULL;
	}
	return sibling;
}

static int terminalPaneHydrateRecursive(struct editorPaneNode *root, struct editorPaneNode *node,
                                        struct editorRect viewport, const char *command) {
	if (node == NULL) {
		return 0;
	}
	if (node->is_split) {
		return terminalPaneHydrateRecursive(root, node->as.split.first, viewport, command) +
		       terminalPaneHydrateRecursive(root, node->as.split.second, viewport, command);
	}
	/* `term` deserializes to a kind == TERMINAL placeholder leaf; everything else
	 * (including already-hydrated editor leaves) is left alone. */
	if (node->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL) {
		return 0;
	}
	struct editorRect rect = {0};
	int cols = 80;
	int rows = 24;
	if (editorLayoutLeafRectBordered(root, viewport, ROTIDE_PANE_BORDER_SIZE, node, &rect) &&
	    rect.w > 0 && rect.h > 0) {
		cols = rect.w;
		rows = rect.h;
	}
	struct editorTerminalPane *terminal = editorTerminalPaneCreate(command, cols, rows);
	if (terminal == NULL) {
		node->as.leaf.kind = EDITOR_PANE_KIND_EDITOR;
		return 1;
	}
	/* Restore as an editor leaf hosting a TERMINAL tab (the live model), not a
	 * terminal leaf kind. The file-open loop may have parked tabs in this view
	 * while the leaf was a placeholder; clear them first. */
	node->as.leaf.kind = EDITOR_PANE_KIND_EDITOR;
	node->as.leaf.view.active_tab_idx = -1;
	node->as.leaf.view.pane_tab_count = 0;
	node->as.leaf.view.preview_tab_idx = -1;
	int tab_idx =
	        editorTabCreateWidget(EDITOR_PANE_KIND_TERMINAL, terminal, editorTerminalPaneFree);
	if (tab_idx < 0 || !editorPaneViewActivateTab(&node->as.leaf.view, tab_idx)) {
		if (tab_idx < 0) {
			editorTerminalPaneFree(terminal);
		}
		return 1;
	}
	return 0;
}

int editorTerminalPaneHydratePlaceholders(struct editorPaneNode *root, const char *command) {
	if (root == NULL || command == NULL) {
		return 0;
	}
	struct editorRect viewport = {0};
	if (!editorLayoutEditorViewport(&viewport)) {
		viewport.x = 0;
		viewport.y = 0;
		viewport.w = 0;
		viewport.h = 0;
	}
	return terminalPaneHydrateRecursive(root, root, viewport, command);
}
