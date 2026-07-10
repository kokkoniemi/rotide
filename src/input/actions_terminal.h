#ifndef ROTIDE_INPUT_ACTIONS_TERMINAL_H
#define ROTIDE_INPUT_ACTIONS_TERMINAL_H

#include "rotide.h"

/* Handle a terminal mapped action (open split, prefix, mode toggles). Returns 1
 * if `action` was a terminal action and was handled, 0 otherwise (so the caller
 * can try other handlers). */
int editorHandleTerminalMappedAction(enum editorAction action);

#endif
