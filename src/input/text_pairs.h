#ifndef ROTIDE_INPUT_TEXT_PAIRS_H
#define ROTIDE_INPUT_TEXT_PAIRS_H

int editorByteShouldInsertAsText(int c);
int editorTrySkipOverClosingPair(int c);
int editorTryAutoClosePair(int c);
int editorJumpToMatchingBracket(void);

#endif
