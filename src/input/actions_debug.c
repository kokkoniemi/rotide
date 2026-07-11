#include "input/actions_debug.h"

#include "debug/dap.h"
#include "debug/dap_console.h"
#include "editing/history.h"
#include "input/prompt.h"
#include "rotide.h"

#include <stdlib.h>

int editorHandleDebugMappedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_DAP_START:
			editorHistoryBreakGroup();
			(void)editorDapStartSelectedLaunch();
			return 1;
		case EDITOR_ACTION_DAP_STOP:
			editorHistoryBreakGroup();
			(void)editorDapStop();
			return 1;
		case EDITOR_ACTION_DAP_RESTART:
			editorHistoryBreakGroup();
			(void)editorDapRestart();
			return 1;
		case EDITOR_ACTION_DAP_EVALUATE: {
			editorHistoryBreakGroup();
			char *expr = editorPrompt("DAP eval: %s");
			if (expr != NULL) {
				(void)editorDapEvaluate(expr);
				free(expr);
			}
			return 1;
		}
		case EDITOR_ACTION_DAP_CONSOLE:
			editorHistoryBreakGroup();
			(void)editorDapConsoleToggle();
			return 1;
		case EDITOR_ACTION_DAP_CONTINUE:
			editorHistoryBreakGroup();
			(void)editorDapContinue();
			return 1;
		case EDITOR_ACTION_DAP_PAUSE:
			editorHistoryBreakGroup();
			(void)editorDapPause();
			return 1;
		case EDITOR_ACTION_DAP_STEP_OVER:
			editorHistoryBreakGroup();
			(void)editorDapStepOver();
			return 1;
		case EDITOR_ACTION_DAP_STEP_INTO:
			editorHistoryBreakGroup();
			(void)editorDapStepInto();
			return 1;
		case EDITOR_ACTION_DAP_STEP_OUT:
			editorHistoryBreakGroup();
			(void)editorDapStepOut();
			return 1;
		case EDITOR_ACTION_DAP_TOGGLE_BREAKPOINT:
			editorHistoryBreakGroup();
			(void)editorDapToggleBreakpointAtCursor();
			return 1;
		default:
			return 0;
	}
}
