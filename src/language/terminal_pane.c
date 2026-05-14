#include "language/terminal_pane.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vterm.h"

static int editorTerminalPaneClampDim(int v) {
	if (v < 1) {
		return 1;
	}
	if (v > 999) {
		return 999;
	}
	return v;
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
