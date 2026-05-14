#ifndef ROTIDE_TERMINAL_PANE_H
#define ROTIDE_TERMINAL_PANE_H

#include <stddef.h>

#include "language/pty.h"

/* Terminal pane state: PTY child + libvterm screen model. */
struct VTerm;
struct VTermScreen;

struct editorTerminalPane {
	struct editorPtyChild child;
	struct VTerm *vt;
	struct VTermScreen *screen;
	int cols;
	int rows;
	int exited;
	int exit_status;
	int cursor_visible;
	int cursor_blink;
	int cursor_shape;
	/* VTERM_PROP_MOUSE_* value from child DECSET state. */
	int mouse_tracking;
};

/* Spawn command in PTY + vterm. Caller owns returned pane. */
struct editorTerminalPane *editorTerminalPaneCreate(const char *command,
		int cols, int rows);

/* Release pane resources and terminate child if needed. Safe on NULL. */
void editorTerminalPaneFree(void *pane);

/* Non-blocking PTY drain into vterm; updates exited/exit_status on reap. */
int editorTerminalPanePump(struct editorTerminalPane *terminal);

/* Resize vterm grid and PTY (SIGWINCH to child). */
int editorTerminalPaneResize(struct editorTerminalPane *terminal,
		int cols, int rows);

/* Write raw bytes to PTY master. */
int editorTerminalPaneWrite(struct editorTerminalPane *terminal,
		const char *bytes, size_t len);

/* Encode a rotide key and forward to PTY/vterm. */
int editorTerminalPaneSendKey(struct editorTerminalPane *terminal,
		int rotide_key);

/* Forward mouse events in pane-local coordinates. */
int editorTerminalPaneSendMouseButton(struct editorTerminalPane *terminal,
		int button, int pressed, int row, int col, int rotide_modifiers);
int editorTerminalPaneSendMouseMove(struct editorTerminalPane *terminal,
		int row, int col, int rotide_modifiers);

/* Bracketed-paste wrappers for terminal input forwarding. */
int editorTerminalPaneSendPasteStart(struct editorTerminalPane *terminal);
int editorTerminalPaneSendPasteEnd(struct editorTerminalPane *terminal);

/* Build a TERMINAL leaf node with owned terminal pane state. */
struct editorPaneNode;
struct editorPaneNode *editorPaneNodeNewTerminalLeaf(const char *command,
		int cols, int rows);

/* Pump all terminal leaves; returns total bytes/activity count. */
int editorTerminalPanePumpAll(struct editorPaneNode *root);

/* Resize all terminal leaves to current layout rects. */
void editorTerminalPaneResizeAllToLayout(struct editorPaneNode *root);

/* Returns 1 when pane tree contains at least one terminal leaf. */
int editorTerminalPaneTreeHasTerminal(const struct editorPaneNode *root);

/* Close terminal leaves with exited child; updates focus/tracked leaf refs. */
int editorTerminalPaneCloseExited(struct editorPaneNode **root_ptr,
		struct editorPaneNode **focused_leaf_ptr,
		struct editorPaneNode **tracked_leaf_ptr);

/* Split focused pane, replace new sibling with terminal pane, and focus it. */
struct editorPaneNode *editorTerminalPaneOpenSplit(const char *command,
		int orientation);

#endif
