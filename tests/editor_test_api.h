#ifndef TESTS_EDITOR_TEST_API_H
#define TESTS_EDITOR_TEST_API_H

#include <stddef.h>

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
long editorGitBlameTestLoadCount(void);
int editorGitBlameTestIncrementalLookup(const char *incremental, int one_based_line,
                                        char *author_out, size_t author_size, char *filename_out,
                                        size_t filename_size, int *unique_commits_out);
int editorGitTestParseStatus(const char *data, size_t len, int *ahead_out, int *behind_out);
const char *editorVimModeLabel(void);
void editorVimRegistersClear(void);
void editorVimMarksClear(void);
char *vimSystemExCompletionTest(const char *current, int tab_iteration);

char *editorDrawerMovePathCompletionTest(const char *current, const char *anchor,
                                         int tab_iteration);
const char *editorDrawerNerdIconForFilenameTest(const char *filename);

#endif
