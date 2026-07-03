#ifndef ROTIDE_WORKSPACE_DRAWER_H
#define ROTIDE_WORKSPACE_DRAWER_H

#include "rotide.h"

int editorDrawerInitForStartup(int argc, char *argv[], int restored_session);
void editorDrawerShutdown(void);
int editorDrawerIsCollapsed(void);
int editorDrawerSetCollapsed(int collapsed);
int editorDrawerToggleCollapsed(void);
int editorDrawerMainMenuToggle(void);
int editorDrawerGitToggle(void);
int editorDrawerLspToggle(void);
int editorDrawerDapToggle(void);
int editorDrawerSelectedMenuAction(enum editorAction *action_out);
int editorDrawerSelectedGitEntry(int *entry_idx_out);
int editorDrawerGitSelectedFile(int *entry_idx_out, int *staged_group_out);
int editorDrawerGitSelectedGroup(int *staged_group_out, int *item_count_out);
int editorDrawerGitEntryInSelectedGroup(int entry_idx);
int editorDrawerGitSelectedAction(enum editorAction *action_out);
int editorDrawerSelectedLspLocation(const char **path_out, int *line_out, int *character_out);
int editorDrawerSelectedDapLaunch(int *launch_idx_out);
int editorDrawerSelectedDapDefault(int *default_idx_out);
int editorDrawerSelectedDapLocation(const char **path_out, int *line_out, int *character_out);
int editorDrawerCollapsedToggleWidthForCols(int total_cols);
int editorDrawerWidthForCols(int total_cols);
int editorDrawerSeparatorWidthForCols(int total_cols);
int editorDrawerTextStartColForCols(int total_cols);
int editorDrawerTextViewportCols(int total_cols);
int editorLineNumberGutterColsForCols(int total_cols);
int editorTextBodyStartColForCols(int total_cols);
int editorTextBodyViewportCols(int total_cols);
int editorDrawerSetWidthForCols(int width, int total_cols);
int editorDrawerResizeByDeltaForCols(int delta, int total_cols);
int editorDrawerVisibleCount(void);
int editorDrawerVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out);
void editorDrawerClampViewport(int viewport_rows);
int editorDrawerMoveSelectionBy(int delta, int viewport_rows);
int editorDrawerScrollBy(int delta, int viewport_rows);
int editorDrawerExpandSelection(int viewport_rows);
int editorDrawerCollapseSelection(int viewport_rows);
int editorDrawerToggleSelectionExpanded(int viewport_rows);
int editorDrawerSelectVisibleIndex(int visible_idx, int viewport_rows);
int editorDrawerSelectedIsDirectory(void);
int editorDrawerSelectedIsRoot(void);
const char *editorDrawerSelectedPath(void);
int editorDrawerCreateFileAtSelection(const char *name, int viewport_rows);
int editorDrawerCreateFolderAtSelection(const char *name, int viewport_rows);
int editorDrawerRenameSelection(const char *new_name, int viewport_rows);
int editorDrawerMoveSelectionToDir(const char *dest_dir_path, int viewport_rows);
int editorDrawerDeleteSelection(int viewport_rows);
int editorDrawerOpenSelectedFileInTab(void);
int editorDrawerOpenSelectedFileInPreviewTab(void);
int editorDrawerRevealPath(const char *path, int viewport_rows);
const char *editorDrawerRootPath(void);

#endif
