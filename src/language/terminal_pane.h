#ifndef ROTIDE_TERMINAL_PANE_H
#define ROTIDE_TERMINAL_PANE_H

#include <stddef.h>

#include "language/pty.h"

/*
 * Terminal pane state.
 *
 * Couples a PTY child (`editorPtyChild`) with a libvterm parser that
 * maintains a screen grid of cells. The editor reads bytes from the PTY's
 * master fd, feeds them into vterm via vterm_input_write, then renders the
 * vterm screen into the pane's rect at the next refresh.
 *
 * The libvterm types are forward-declared to keep `vterm.h` out of public
 * editor headers. Code that needs to read cells (Slice 3d render) includes
 * `vterm.h` directly along with this header.
 */
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
};

/*
 * Spawn `command` in a fresh PTY sized (cols, rows) and wire up a vterm
 * parser to interpret its output. Returns a heap-allocated terminal pane
 * on success (caller takes ownership; pass to editorTerminalPaneFree), or
 * NULL on failure (errno set).
 */
struct editorTerminalPane *editorTerminalPaneCreate(const char *command,
		int cols, int rows);

/*
 * Free a terminal pane. Terminates the child if it is still running and
 * releases vterm + transport resources. Safe to call with NULL.
 *
 * The signature matches `void (*)(void *)` so it can be stored in
 * `editorPane.kind_state_free`.
 */
void editorTerminalPaneFree(void *pane);

/*
 * Drain any pending output bytes from the PTY and feed them to vterm.
 * Non-blocking. Returns the number of bytes consumed; 0 if nothing was
 * available (or the child exited). Sets `terminal->exited` and
 * `terminal->exit_status` when the child has been reaped.
 */
int editorTerminalPanePump(struct editorTerminalPane *terminal);

/*
 * Resize both the vterm grid and the PTY. The child sees a SIGWINCH.
 * Returns 1 on success, 0 on failure.
 */
int editorTerminalPaneResize(struct editorTerminalPane *terminal,
		int cols, int rows);

/*
 * Write user-supplied bytes (typed keypresses) to the PTY's master fd.
 * Returns the number of bytes written (may be less than `len` on EAGAIN).
 */
int editorTerminalPaneWrite(struct editorTerminalPane *terminal,
		const char *bytes, size_t len);

#endif
