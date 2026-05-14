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
	(void)write(t->child.master_fd, s, len);
}

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
		(void)write(terminal->child.master_fd, &b, 1);
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

void editorTerminalPanePumpAll(struct editorPaneNode *root) {
	if (root == NULL) {
		return;
	}
	if (root->is_split) {
		editorTerminalPanePumpAll(root->as.split.first);
		editorTerminalPanePumpAll(root->as.split.second);
		return;
	}
	if (root->as.leaf.kind == EDITOR_PANE_KIND_TERMINAL &&
			root->as.leaf.kind_state != NULL) {
		(void)editorTerminalPanePump(
				(struct editorTerminalPane *)root->as.leaf.kind_state);
	}
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
