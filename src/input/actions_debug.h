#ifndef ROTIDE_INPUT_ACTIONS_DEBUG_H
#define ROTIDE_INPUT_ACTIONS_DEBUG_H

#include "rotide.h"

/* Handle a DAP/debug mapped action. Returns 1 if `action` was a debug action
 * and was handled, 0 otherwise (so the caller can try other handlers). */
int editorHandleDebugMappedAction(enum editorAction action);

#endif
