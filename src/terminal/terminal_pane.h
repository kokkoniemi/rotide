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

	/* Pane-owned scratch reused by the renderer to read one row of cells per
	 * draw, so refreshes don't malloc per drawn row. */
	VTermScreenCell *render_row_scratch;
	int render_row_scratch_cap;

	/* Per-row dirty bits for the live screen. 0 means the cells are
	 * unchanged since the last frame, so the renderer may skip the emit and
	 * leave the terminal's previous output in place. */
	unsigned char *row_dirty;
	int row_dirty_cap;
};

/* Spawn command in PTY + vterm. Caller owns returned pane. */
struct editorTerminalPane *editorTerminalPaneCreate(const char *command, int cols, int rows);

/* Create a pane around a childless PTY (vterm + master fd, pid = -1) and write
 * the slave device path to `slave_path`. For hosting an external process's tty
 * (e.g. a debuggee) without forking a placeholder. Caller owns the pane. */
struct editorTerminalPane *editorTerminalPaneCreateDetached(int cols, int rows, char *slave_path,
                                                            size_t slave_path_size);

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

int editorTerminalPaneSelectionContains(const struct editorTerminalPane *terminal, int row,
                                        int col);

/* Extract the selection as a malloc'd UTF-8 string (NUL-terminated). Caller
 * frees. Returns NULL with *len_out=0 if no selection. */
char *editorTerminalPaneSelectionExtract(const struct editorTerminalPane *terminal,
                                         size_t *len_out);

/* Copy the active selection to the clipboard (which also fans out to OSC 52
 * via the registered external sink). Returns 1 on success, 0 if nothing to
 * copy or on allocation failure. */
int editorTerminalPaneCopySelection(struct editorTerminalPane *terminal);

void editorTerminalPaneSetDefaultScrollbackLines(int lines);
int editorTerminalPaneGetDefaultScrollbackLines(void);

/* Ensures pane->render_row_scratch holds at least `cells` slots, growing it
 * if needed. Returns the scratch pointer, or NULL on allocation failure. */
VTermScreenCell *editorTerminalPaneEnsureRenderRowScratch(struct editorTerminalPane *terminal,
                                                          int cells);

struct editorPaneNode;

/* The active terminal of `pane`: the payload of its active tab when that tab is
 * a TERMINAL tab, else NULL. */
struct editorTerminalPane *editorTerminalPaneForPane(const struct editorPaneNode *pane);

/* Force a full repaint of `terminal` on the next frame. Needed when a terminal
 * tab becomes visible again: its rows are otherwise "clean" and the partial-
 * repaint path would leave whatever the previously-active tab painted. */
void editorTerminalPaneMarkDirty(struct editorTerminalPane *terminal);

/* Pump every live terminal (the TERMINAL tabs in E.tabs); returns total
 * bytes/activity count. `root` is unused, kept for call-site symmetry. */
int editorTerminalPanePumpAll(struct editorPaneNode *root);

/* Close TERMINAL tabs whose child has exited. Returns the number closed. */
int editorTerminalPaneCloseExitedTabs(void);

/* Append every terminal pane's master_fd (only those >= 0) into fds_out[],
 * writing at most `capacity` entries. Returns the number of fds that would
 * exist regardless of capacity — caller can detect truncation by comparing
 * against `capacity`. */
int editorTerminalPaneCollectMasterFds(struct editorPaneNode *root, int *fds_out, int capacity);

/* Resize each pane's active TERMINAL tab to its current layout rect. */
void editorTerminalPaneResizeAllToLayout(struct editorPaneNode *root);

/* Returns 1 when at least one TERMINAL tab exists. */
int editorTerminalPaneTreeHasTerminal(const struct editorPaneNode *root);

/* Split focused pane and host a terminal as a TERMINAL tab in the new pane. */
struct editorPaneNode *editorTerminalPaneOpenSplit(const char *command, int orientation);

/* Walk `root` and, for every kind == EDITOR_PANE_KIND_TERMINAL placeholder leaf
 * (produced by deserializing a `term` token), spawn a PTY with `command` and
 * convert the leaf to an editor leaf hosting a TERMINAL tab. Failed spawns demote
 * the leaf back to EDITOR_PANE_KIND_EDITOR. Returns the number of failed spawns. */
int editorTerminalPaneHydratePlaceholders(struct editorPaneNode *root, const char *command);

#endif
