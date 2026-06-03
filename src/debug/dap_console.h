#ifndef ROTIDE_DEBUG_DAP_CONSOLE_H
#define ROTIDE_DEBUG_DAP_CONSOLE_H

#include "config/dap_config.h"

void editorDapConsoleCloseOwnedTerminalPane(void);
/*
 * Opens the Debug Console panel for a launch. When the config requests
 * `console = "terminal"` the panel owns a childless-PTY terminal for the
 * debuggee's tty and the slave device path is written to `tty_out` (empty
 * otherwise) so the caller can hand it to the adapter (e.g. gdb's --tty).
 */
int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config, char *tty_out,
                                    size_t tty_out_size);

/*
 * Debug Console pane: a scrollable view of the DAP output transcript
 * (E.dap_output). editorDapConsoleToggle opens it as a bottom split, focuses it
 * if already open, or closes it if it is the focused pane. editorDapConsoleScroll
 * shifts the view by `delta` lines (positive scrolls toward older output).
 */
int editorDapConsoleToggle(void);
void editorDapConsoleScroll(int delta);

#endif
