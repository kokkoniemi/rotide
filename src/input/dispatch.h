#ifndef ROTIDE_INPUT_DISPATCH_H
#define ROTIDE_INPUT_DISPATCH_H

#include "input/actions_edit.h"
#include "input/actions_file_tab.h"
#include "input/actions_language.h"
#include "input/actions_terminal_debug.h"
#include "input/actions_workspace.h"
#include "input/mouse.h"
#include "input/prompt.h"
#include "input/text_pairs.h"

int editorDispatchProcessMappedAction(enum editorAction action, int *effects_out);
void editorDispatchHandleTextByte(int c, int *effects_out);
void editorProcessKeypress(void);
int editorLspLocationMenuActivate(void);

#endif
