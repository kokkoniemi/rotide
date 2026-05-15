#ifndef ROTIDE_INPUT_ACTIONS_WORKSPACE_H
#define ROTIDE_INPUT_ACTIONS_WORKSPACE_H

#include "rotide.h"

void editorSetDrawerCollapseStatus(int collapsed);
void editorExpandDrawerForFocus(void);
void editorToggleDrawerFocus(void);
void editorOpenFileSearchDrawer(void);
void editorOpenProjectSearchDrawer(void);
int editorSwitchDrawerHeaderMode(enum editorDrawerMode mode);

#endif
