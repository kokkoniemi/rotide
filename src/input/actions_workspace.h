#ifndef ROTIDE_INPUT_ACTIONS_WORKSPACE_H
#define ROTIDE_INPUT_ACTIONS_WORKSPACE_H

#include "rotide.h"

typedef int (*editorJumpToPathLocationFn)(const char *path, int line, int character, int preview,
		int center);

void editorSetDrawerCollapseStatus(int collapsed);
void editorExpandDrawerForFocus(void);
void editorToggleDrawerFocus(void);
void editorOpenFileSearchDrawer(void);
void editorOpenProjectSearchDrawer(void);
void editorDrawerPromptCreateFile(void);
void editorDrawerPromptCreateFolder(void);
void editorDrawerPromptRename(void);
void editorDrawerPromptDelete(void);
int editorOpenSelectedGitDiff(void);
int editorJumpToSelectedLspDrawerLocation(int preview, editorJumpToPathLocationFn jump_fn);
int editorJumpToSelectedDapDrawerLocation(int preview, editorJumpToPathLocationFn jump_fn);
void editorDrawerPreviewSelectionAfterMove(editorJumpToPathLocationFn jump_fn);
int editorHandleDrawerSearchMappedAction(enum editorAction action, int *cursor_or_edit_out,
		void (*project_replace_from_search)(void));
int editorSwitchDrawerHeaderMode(enum editorDrawerMode mode);

#endif
