#ifndef ROTIDE_INPUT_ACTIONS_FILE_TAB_H
#define ROTIDE_INPUT_ACTIONS_FILE_TAB_H

#include "rotide.h"

void editorActionQuit(void);
void editorActionQuitForce(void);
void editorActionCloseTab(void);
void editorOpenSettings(void);
int editorHandleFileTabMappedAction(enum editorAction action);
void editorFileTabActionsAfterKeypress(int mapped_action, enum editorAction action);

#endif
