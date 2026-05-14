#include "language/terminal_pane.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rotide.h"
#include "vterm.h"
#include "vterm_keycodes.h"
#include "workspace/layout.h"

static int editorTerminalPaneClampDim(int v) {
	if (v < 1) {
		return 1;
	}
	if (v > 999) {
		return 999;
	}
	return v;
}

static void editorTerminalOutputCallback(const char *s, size_t len, void *user) {
	struct editorTerminalPane *t = (struct editorTerminalPane *)user;
	if (t == NULL || t->child.master_fd < 0 || s == NULL || len == 0) {
		return;
	}
	ssize_t written = write(t->child.master_fd, s, len);
	(void)written;
}

static int editorTerminalSetTermProp(VTermProp prop, VTermValue *val, void *user) {
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

static const VTermScreenCallbacks editor_terminal_screen_callbacks = {
	.settermprop = editorTerminalSetTermProp,
};

struct editorTerminalPane *editorTerminalPaneCreate(const char *command,
		int cols, int rows) {
	if (command == NULL) {
		errno = EINVAL;
		return NULL;
	}
	cols = editorTerminalPaneClampDim(cols);
	rows = editorTerminalPaneClampDim(rows);

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

	t->vt = vterm_new(rows, cols);
	if (t->vt == NULL) {
		free(t);
		return NULL;
	}
	vterm_set_utf8(t->vt, 1);
	vterm_output_set_callback(t->vt, editorTerminalOutputCallback, t);
	t->screen = vterm_obtain_screen(t->vt);
	if (t->screen == NULL) {
		vterm_free(t->vt);
		free(t);
		return NULL;
	}
	vterm_screen_set_callbacks(t->screen, &editor_terminal_screen_callbacks, t);
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
	free(t);
}

int editorTerminalPanePump(struct editorTerminalPane *terminal) {
	if (terminal == NULL || terminal->vt == NULL) {
		return 0;
	}
	int total = 0;
	if (terminal->child.master_fd >= 0) {
		char buf[4096];
		for (;;) {
			ssize_t n = read(terminal->child.master_fd, buf, sizeof(buf));
			if (n > 0) {
				vterm_input_write(terminal->vt, buf, (size_t)n);
				total += (int)n;
				if ((size_t)n < sizeof(buf)) {
					break;
				}
				continue;
			}
			if (n == 0) {
				/* EOF — child closed the slave; will be reaped below. */
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
			/* Count process exit as terminal activity so callers polling
			 * for screen changes wake promptly and can close the pane. */
			total += 1;
		}
	}
	return total;
}

int editorTerminalPaneResize(struct editorTerminalPane *terminal,
		int cols, int rows) {
	if (terminal == NULL || terminal->vt == NULL) {
		return 0;
	}
	cols = editorTerminalPaneClampDim(cols);
	rows = editorTerminalPaneClampDim(rows);
	vterm_set_size(terminal->vt, rows, cols);
	terminal->cols = cols;
	terminal->rows = rows;
	if (terminal->child.master_fd >= 0) {
		(void)editorPtyResize(&terminal->child, cols, rows);
	}
	return 1;
}

int editorTerminalPaneWrite(struct editorTerminalPane *terminal,
		const char *bytes, size_t len) {
	if (terminal == NULL || terminal->child.master_fd < 0 || bytes == NULL ||
			len == 0) {
		return 0;
	}
	ssize_t n = write(terminal->child.master_fd, bytes, len);
	if (n < 0) {
		return 0;
	}
	return (int)n;
}

static VTermModifier editorTerminalRotideModifiersToVterm(int rotide_modifiers) {
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

int editorTerminalPaneSendMouseButton(struct editorTerminalPane *terminal,
		int button, int pressed, int row, int col, int rotide_modifiers) {
	if (terminal == NULL || terminal->vt == NULL ||
			terminal->mouse_tracking <= 0) {
		return 0;
	}
	VTermModifier mod = editorTerminalRotideModifiersToVterm(rotide_modifiers);
	vterm_mouse_move(terminal->vt, row, col, mod);
	vterm_mouse_button(terminal->vt, button, pressed != 0, mod);
	return 1;
}

int editorTerminalPaneSendMouseMove(struct editorTerminalPane *terminal,
		int row, int col, int rotide_modifiers) {
	if (terminal == NULL || terminal->vt == NULL ||
			terminal->mouse_tracking <= 0) {
		return 0;
	}
	VTermModifier mod = editorTerminalRotideModifiersToVterm(rotide_modifiers);
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

int editorTerminalPaneSendKey(struct editorTerminalPane *terminal,
		int rotide_key) {
	if (terminal == NULL || terminal->vt == NULL ||
			terminal->child.master_fd < 0) {
		return 0;
	}
	/* Printable ASCII goes through vterm so it handles UTF-8 paste/keypress
	 * normalization. */
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
	/* Control characters (Ctrl+A .. Ctrl+Z and related) pass through as
	 * the literal control byte. */
	if (rotide_key > 0 && rotide_key < 0x20) {
		char b = (char)rotide_key;
		ssize_t written = write(terminal->child.master_fd, &b, 1);
		(void)written;
		return 1;
	}
	return 0;
}

struct editorPaneNode *editorPaneNodeNewTerminalLeaf(const char *command,
		int cols, int rows) {
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
	if (root->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
			root->as.leaf.kind_state != NULL) {
		return editorTerminalPanePump(
				(struct editorTerminalPane *)root->as.leaf.kind_state);
	}
	return 0;
}

static void editorTerminalPaneResizeRecursive(struct editorPaneNode *node,
		struct editorRect rect, int border_size) {
	if (node == NULL) {
		return;
	}
	if (!node->is_split) {
		if (node->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
				node->as.leaf.kind_state != NULL &&
				rect.w > 0 && rect.h > 0) {
			(void)editorTerminalPaneResize(
					(struct editorTerminalPane *)node->as.leaf.kind_state,
					rect.w, rect.h);
		}
		return;
	}
	/* Mirror editorLayoutSplitRects so the rects line up exactly with
	 * what the renderer uses. */
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
	editorTerminalPaneResizeRecursive(node->as.split.first, first_rect,
			border_size);
	editorTerminalPaneResizeRecursive(node->as.split.second, second_rect,
			border_size);
}

void editorTerminalPaneResizeAllToLayout(struct editorPaneNode *root) {
	if (root == NULL) {
		return;
	}
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return;
	}
	editorTerminalPaneResizeRecursive(root, viewport, ROTIDE_PANE_BORDER_SIZE);
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

static struct editorPaneNode *editorTerminalPaneFindFirstExitedLeaf(
		struct editorPaneNode *root) {
	if (root == NULL) {
		return NULL;
	}
	if (root->is_split) {
		struct editorPaneNode *found =
				editorTerminalPaneFindFirstExitedLeaf(root->as.split.first);
		if (found != NULL) {
			return found;
		}
		return editorTerminalPaneFindFirstExitedLeaf(root->as.split.second);
	}
	if (root->as.leaf.kind != EDITOR_PANE_KIND_TERMINAL ||
			root->as.leaf.kind_state == NULL) {
		return NULL;
	}
	struct editorTerminalPane *terminal =
			(struct editorTerminalPane *)root->as.leaf.kind_state;
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
		struct editorPaneNode *exited_leaf =
				editorTerminalPaneFindFirstExitedLeaf(*root_ptr);
		if (exited_leaf == NULL) {
			break;
		}
		if (tracked_leaf_ptr != NULL && *tracked_leaf_ptr == exited_leaf) {
			*tracked_leaf_ptr = NULL;
		}
		struct editorPaneNode *new_focus =
				editorPaneTreeCloseLeaf(root_ptr, exited_leaf);
		if (new_focus == NULL) {
			/* Root terminal leaf cannot be closed (single-leaf no-op). */
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

struct editorPaneNode *editorTerminalPaneOpenSplit(const char *command,
		int orientation) {
	if (command == NULL) {
		errno = EINVAL;
		return NULL;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(
			(enum editorSplitOrientation)orientation, 0.5);
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
	struct editorTerminalPane *terminal = editorTerminalPaneCreate(command,
			cols, rows);
	if (terminal == NULL) {
		return NULL;
	}
	sibling->as.leaf.kind = EDITOR_PANE_KIND_TERMINAL;
	sibling->as.leaf.kind_state = terminal;
	sibling->as.leaf.kind_state_free = editorTerminalPaneFree;
	return sibling;
}
