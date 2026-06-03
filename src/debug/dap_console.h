#ifndef ROTIDE_DEBUG_DAP_CONSOLE_H
#define ROTIDE_DEBUG_DAP_CONSOLE_H

#include "config/dap_config.h"

void editorDapConsoleCloseOwnedTerminalPane(void);
int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config);

/*
 * Debug Console pane: a scrollable view of the DAP output transcript
 * (E.dap_output). editorDapConsoleToggle opens it as a bottom split, focuses it
 * if already open, or closes it if it is the focused pane. editorDapConsoleScroll
 * shifts the view by `delta` lines (positive scrolls toward older output).
 */
int editorDapConsoleToggle(void);
void editorDapConsoleScroll(int delta);

#endif
