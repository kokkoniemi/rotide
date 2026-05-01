#ifndef EDITOR_PROJECT_SEARCH_H
#define EDITOR_PROJECT_SEARCH_H

#include "rotide.h"

int editorProjectSearchEnter(void);
void editorProjectSearchExit(int restore_previous_tab);
void editorProjectSearchFree(void);
int editorProjectSearchIsActive(void);
int editorProjectSearchAppendByte(int c);
int editorProjectSearchBackspace(void);
int editorProjectSearchVisibleCount(void);
int editorProjectSearchGetVisibleEntry(int visible_idx,
		struct editorDrawerEntryView *view_out);
void editorProjectSearchClampViewport(int viewport_rows);
int editorProjectSearchMoveSelectionBy(int delta, int viewport_rows);
int editorProjectSearchSelectVisibleIndex(int visible_idx, int viewport_rows);
int editorProjectSearchSelectedIsDirectory(void);
int editorProjectSearchOpenSelectedFileInTab(void);
int editorProjectSearchOpenSelectedFileInPreviewTab(void);
int editorProjectSearchPreviewSelection(void);
int editorProjectSearchHeaderCursorCol(int drawer_cols);
const char *editorProjectSearchQuery(void);

#endif
