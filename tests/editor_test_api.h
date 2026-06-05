#ifndef TESTS_EDITOR_TEST_API_H
#define TESTS_EDITOR_TEST_API_H

void editorDocumentTestResetStats(void);
int editorDocumentTestFullRebuildCount(void);
int editorDocumentTestIncrementalUpdateCount(void);
int editorTextTreeTestFullRebuildCount(void);
int editorTextTreeTestIncrementalUpdateCount(void);
int editorRowCacheTestFullRebuildCount(void);
int editorRowCacheTestSpliceUpdateCount(void);
void editorActiveTextSourceBuildTestResetCount(void);
int editorActiveTextSourceBuildTestCount(void);
void editorActiveTextSourceDupTestResetCount(void);
int editorActiveTextSourceDupTestCount(void);
const char *editorInputSystemActiveId(void);
const char *editorVimModeLabel(void);

char *editorDrawerMovePathCompletionTest(const char *current, const char *anchor,
                                         int tab_iteration);

#endif
