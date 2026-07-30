#ifndef ROTIDE_INPUT_DISPATCH_H
#define ROTIDE_INPUT_DISPATCH_H

#include "input/actions_debug.h"
#include "input/actions_edit.h"
#include "input/actions_file_tab.h"
#include "input/actions_language.h"
#include "input/actions_terminal.h"
#include "input/actions_workspace.h"
#include "input/mouse.h"
#include "input/prompt.h"
#include "input/text_pairs.h"

int editorDispatchProcessMappedAction(enum editorAction action, int *effects_out);
int editorDispatchGoToBufferPosition(const char *path, int cy, int cx);
int editorDispatchJumpToPathLocation(const char *path, int line, int character);
int editorDispatchJumplistBack(int count, int *effects_out);
int editorDispatchJumplistForward(int count, int *effects_out);
void editorDispatchHandleTextByte(int c, int *effects_out);
void editorDispatchResetInputState(void);
int editorDispatchOpenGitBlameDetailsAt(int row_idx, int anchor_col, int report_status);
void editorProcessKeypress(void);
int editorLspLocationMenuActivate(void);

/* Replace occurrences of `query` with `replacement` across the active buffer.
 * When `global` is zero, only the first match on each line is replaced (Vim
 * `:s` semantics); otherwise every match is replaced (`:s//g`). Returns the
 * number of replacements, or -1 on allocation failure. */
int editorDispatchSubstituteInBuffer(const char *query, const char *replacement, int global);

#endif
