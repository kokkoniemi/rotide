#ifndef EDITOR_FILE_SEARCH_H
#define EDITOR_FILE_SEARCH_H

#include "rotide.h"

int editorFileSearchEnter(void);
void editorFileSearchExit(int restore_previous_tab);
void editorFileSearchFree(void);
int editorFileSearchIsActive(void);
int editorFileSearchAppendByte(int c);
int editorFileSearchBackspace(void);
int editorFileSearchVisibleCount(void);
int editorFileSearchGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out);
void editorFileSearchClampViewport(int viewport_rows);
int editorFileSearchMoveSelectionBy(int delta, int viewport_rows);
int editorFileSearchSelectVisibleIndex(int visible_idx, int viewport_rows);
int editorFileSearchSelectedIsDirectory(void);
int editorFileSearchOpenSelectedFileInTab(void);
int editorFileSearchOpenSelectedFileInPreviewTab(void);
int editorFileSearchPreviewSelection(void);
int editorFileSearchHeaderCursorCol(int drawer_cols);
const char *editorFileSearchQuery(void);

#endif
