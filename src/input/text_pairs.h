#ifndef ROTIDE_INPUT_TEXT_PAIRS_H
#define ROTIDE_INPUT_TEXT_PAIRS_H

int editorByteShouldInsertAsText(int c);
int editorTrySkipOverClosingPair(int c);
int editorTryAutoClosePair(int c);
int editorJumpToMatchingBracket(void);
int editorBracketMatchComputeForCursor(int out_rows[2], int out_cols[2]);

#endif
