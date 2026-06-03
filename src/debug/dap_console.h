#ifndef ROTIDE_DEBUG_DAP_CONSOLE_H
#define ROTIDE_DEBUG_DAP_CONSOLE_H

#include "config/dap_config.h"

struct editorPaneNode;

/*
 * Per-tab state for a DEBUG_CONSOLE tab. The transcript itself stays global
 * (E.dap_output); this payload owns only the scrollback offset and the inline
 * REPL input line. Mirrors how a TERMINAL tab owns its editorTerminalPane.
 */
struct editorDapConsolePane {
	int scroll;      /* line offset from the bottom of the transcript */
	char input[256]; /* inline REPL input, evaluated on Enter */
	int input_len;
};

void editorDapConsoleCloseOwnedTerminalPane(void);
/*
 * Opens the Debug Console panel for a launch. When the config requests
 * `console = "terminal"` the panel also hosts a TERMINAL tab for the debuggee's
 * tty and the slave device path is written to `tty_out` (empty otherwise) so the
 * caller can hand it to the adapter (e.g. gdb's --tty).
 */
int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config, char *tty_out,
                                    size_t tty_out_size);

/*
 * Debug Console panel: a bottom-split pane whose tabs are a DEBUG_CONSOLE tab
 * (a scrollable view of the DAP output transcript, E.dap_output, plus an inline
 * REPL) and, when launched with console="terminal", a TERMINAL tab for the
 * debuggee tty. editorDapConsoleToggle opens the panel as a bottom split or
 * focuses its console tab if already open.
 */
int editorDapConsoleToggle(void);

/* The DEBUG_CONSOLE tab payload of `pane`'s active tab, or NULL. */
struct editorDapConsolePane *editorDapConsoleForPane(const struct editorPaneNode *pane);
/* Scroll `console`'s transcript view by `delta` lines (positive = older). */
void editorDapConsoleScroll(struct editorDapConsolePane *console, int delta);

#endif
