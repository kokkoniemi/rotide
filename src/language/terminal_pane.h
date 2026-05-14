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
	int cursor_visible;
	int cursor_blink;
	int cursor_shape;
	/*
	 * VTERM_PROP_MOUSE_* (NONE/CLICK/DRAG/MOVE). Updated by the
	 * `settermprop` callback whenever the child writes DECSET 1000/1002/
	 * 1003. Used by the input dispatcher to decide whether to forward
	 * host mouse events into the pane.
	 */
	int mouse_tracking;
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

/*
 * Encode a rotide key code and send it to the PTY child. Handles ASCII
 * printables, control characters, and rotide's named keys (arrows, home,
 * end, page up/down, function keys, etc.) by translating to libvterm's
 * keyboard helpers, which emit the matching terminal byte sequence.
 * Returns 1 if any bytes were forwarded, 0 if the key produced none.
 */
int editorTerminalPaneSendKey(struct editorTerminalPane *terminal,
		int rotide_key);

/*
 * Forward a mouse event to the focused child. `button` follows libvterm
 * conventions: 1=left, 2=middle, 3=right, 4/5=wheel up/down. `pressed`
 * is 1 for press, 0 for release. `row`/`col` are 0-based positions within
 * the pane's grid. `modifiers` is a bitmask of rotide mouse modifiers
 * (EDITOR_MOUSE_MOD_*). The bytes the child sees come from vterm's
 * mouse encoder, which respects the child's chosen SGR/legacy mode.
 * Returns 1 if forwarded, 0 if the terminal isn't tracking mouse events.
 */
int editorTerminalPaneSendMouseButton(struct editorTerminalPane *terminal,
		int button, int pressed, int row, int col, int rotide_modifiers);
int editorTerminalPaneSendMouseMove(struct editorTerminalPane *terminal,
		int row, int col, int rotide_modifiers);

/*
 * Bracketed-paste forwarding. The caller emits start, then a sequence of
 * raw byte payloads, then end. When the child has bracketed paste enabled
 * (DECSET 2004), the markers are wrapped automatically; otherwise just
 * the raw bytes go through. Returns 1 on success.
 */
int editorTerminalPaneSendPasteStart(struct editorTerminalPane *terminal);
int editorTerminalPaneSendPasteEnd(struct editorTerminalPane *terminal);

/*
 * Convenience: build a leaf pane node of kind TERMINAL with a freshly
 * spawned PTY + vterm wired into its kind_state. Caller takes ownership of
 * the node; closing the pane via editorPaneNodeFree releases everything.
 * Returns NULL on failure (errno set; nothing leaked).
 */
struct editorPaneNode;
struct editorPaneNode *editorPaneNodeNewTerminalLeaf(const char *command,
		int cols, int rows);

/*
 * Walk the pane tree and pump every terminal pane's PTY into its vterm.
 * Called by the renderer (and the main-loop input poll) so child output
 * lands on the screen promptly. Returns the total number of bytes drained
 * across all terminal panes — callers use this to detect "something
 * changed, redraw" without scanning each pane manually.
 */
int editorTerminalPanePumpAll(struct editorPaneNode *root);

/*
 * Walk the pane tree and resize every terminal pane to match its current
 * rect in the given layout viewport. Called from the SIGWINCH path so
 * children see their windows shrink/grow promptly.
 */
void editorTerminalPaneResizeAllToLayout(struct editorPaneNode *root);

/*
 * Returns 1 if the tree contains at least one terminal-kind leaf. Used by
 * the renderer to decide whether to take the multi-pane path even with a
 * single leaf (single-pane fast path doesn't know how to draw terminals).
 */
int editorTerminalPaneTreeHasTerminal(const struct editorPaneNode *root);

/*
 * Close all terminal leaves whose child process has exited (`exited!=0`).
 * Returns the number of panes closed.
 *
 * The helper mutates the pane tree via editorPaneTreeCloseLeaf, updates
 * `*focused_leaf_ptr` when the focused leaf is removed, and clears
 * `*tracked_leaf_ptr` when that tracked leaf gets closed (used by DAP's
 * owned-terminal pointer).
 */
int editorTerminalPaneCloseExited(struct editorPaneNode **root_ptr,
		struct editorPaneNode **focused_leaf_ptr,
		struct editorPaneNode **tracked_leaf_ptr);

/*
 * Split the focused pane and replace the new sibling with a terminal pane
 * running `command`. The new terminal pane gains focus. `orientation` is
 * an `editorSplitOrientation` value cast to int (the parameter type is
 * int to avoid forcing layout.h on every caller). Returns the new leaf
 * on success, NULL on failure (errno set; the layout may be left with
 * an empty editor sibling if the split succeeded but the terminal spawn
 * failed).
 */
struct editorPaneNode *editorTerminalPaneOpenSplit(const char *command,
		int orientation);

#endif
