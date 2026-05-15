#ifndef ROTIDE_DEBUG_DAP_CONSOLE_H
#define ROTIDE_DEBUG_DAP_CONSOLE_H

#include "config/dap_config.h"

void editorDapConsoleCloseOwnedTerminalPane(void);
int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config);

#endif
