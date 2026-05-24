#ifndef ROTIDE_TERMINAL_TERMINAL_PANE_H
#define ROTIDE_TERMINAL_TERMINAL_PANE_H

#include "terminal/pty.h"
#include "vterm.h"

#include <stddef.h>

/* Terminal pane state: PTY child + libvterm screen model. */

/* One captured row of scrollback. cells is malloc'd, length == cols. */
struct terminalScrollbackRow {
	int cols;
	VTermScreenCell *cells;
};

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

	/* Scrollback ring. sb_rows is a ring buffer of length sb_cap; sb_size
	 * is the number of valid rows, and sb_head is the next-write index. The
	 * Nth-most-recent line (N=1..sb_size) lives at sb_rows[(sb_head-N+sb_cap)%sb_cap]. */
	struct terminalScrollbackRow *sb_rows;
	int sb_cap;
	int sb_size;
	int sb_head;
	/* Number of scrollback rows currently shown above the live screen.
	 * 0 = live (default). When >0, rows 0..scroll_offset-1 of the rendered
	 * pane come from scrollback. Clamped to sb_size. */
	int scroll_offset;

	/* Selection. anchor/cursor rows are in "log coords": values >= 0 index
	 * live screen rows; negative values index scrollback (-1 = most recent
	 * scrollback line). Both coords shift up by one on each sb_pushline so
	 * the selection stays anchored to content. */
	int sel_active;
	int sel_anchor_row;
	int sel_anchor_col;
	int sel_cursor_row;
	int sel_cursor_col;
};

/* Spawn command in PTY + vterm. Caller owns returned pane. */
struct editorTerminalPane *editorTerminalPaneCreate(const char *command, int cols, int rows);

/* Release pane resources and terminate child if needed. Safe on NULL. */
void editorTerminalPaneFree(void *pane);

/* Non-blocking PTY drain into vterm; updates exited/exit_status on reap. */
int editorTerminalPanePump(struct editorTerminalPane *terminal);

/* Resize vterm grid and PTY (SIGWINCH to child). */
int editorTerminalPaneResize(struct editorTerminalPane *terminal, int cols, int rows);

/* Write raw bytes to PTY master. */
int editorTerminalPaneWrite(struct editorTerminalPane *terminal, const char *bytes, size_t len);

/* Encode a rotide key and forward to PTY/vterm. */
int editorTerminalPaneSendKey(struct editorTerminalPane *terminal, int rotide_key);

/* Forward mouse events in pane-local coordinates. */
int editorTerminalPaneSendMouseButton(struct editorTerminalPane *terminal, int button, int pressed,
                                      int row, int col, int rotide_modifiers);
int editorTerminalPaneSendMouseMove(struct editorTerminalPane *terminal, int row, int col,
                                    int rotide_modifiers);

/* Bracketed-paste wrappers for terminal input forwarding. */
int editorTerminalPaneSendPasteStart(struct editorTerminalPane *terminal);
int editorTerminalPaneSendPasteEnd(struct editorTerminalPane *terminal);

/* Scroll the pane viewport by `lines` (positive = back into scrollback,
 * negative = forward toward live). Clamped to [0, sb_size]. Returns 1 if
 * scroll_offset changed. */
int editorTerminalPaneScrollBy(struct editorTerminalPane *terminal, int lines);

/* Jump back to live view (scroll_offset = 0). Returns 1 if it changed. */
int editorTerminalPaneScrollReset(struct editorTerminalPane *terminal);

/* Read a scrollback or live row into cells_out. row is a log-row coordinate:
 * >= 0 = live row, < 0 = scrollback (-1 = most recent). cells_out must hold
 * at least pane->cols cells. Returns 1 on success, 0 if out of range. */
int editorTerminalPaneGetLogRow(const struct editorTerminalPane *terminal, int row,
                                VTermScreenCell *cells_out);

/* Selection in log-row coords (see struct field comments). */
void editorTerminalPaneSelectionBegin(struct editorTerminalPane *terminal, int row, int col);
void editorTerminalPaneSelectionUpdate(struct editorTerminalPane *terminal, int row, int col);
void editorTerminalPaneSelectionClear(struct editorTerminalPane *terminal);

/* Returns 1 if (row,col) falls inside the active selection, else 0. */
int editorTerminalPaneSelectionContains(const struct editorTerminalPane *terminal, int row, int col);

/* Extract the selection as a malloc'd UTF-8 string (NUL-terminated). Caller
 * frees. Returns NULL with *len_out=0 if no selection. */
char *editorTerminalPaneSelectionExtract(const struct editorTerminalPane *terminal,
                                         size_t *len_out);

/* Copy the active selection to the clipboard (which also fans out to OSC 52
 * via the registered external sink). Returns 1 on success, 0 if nothing to
 * copy or on allocation failure. */
int editorTerminalPaneCopySelection(struct editorTerminalPane *terminal);

/* Set the global default scrollback cap for new panes (lines). */
void editorTerminalPaneSetDefaultScrollbackLines(int lines);
int editorTerminalPaneGetDefaultScrollbackLines(void);

/* Build a TERMINAL leaf node with owned terminal pane state. */
struct editorPaneNode;
struct editorPaneNode *editorPaneNodeNewTerminalLeaf(const char *command, int cols, int rows);

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
struct editorPaneNode *editorTerminalPaneOpenSplit(const char *command, int orientation);

#endif
