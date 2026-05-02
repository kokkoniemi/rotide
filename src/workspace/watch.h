#ifndef EDITOR_WATCH_H
#define EDITOR_WATCH_H

#include "rotide.h"

int editorWatchPoll(void);
int editorWatchPollNow(void);
void editorWatchRefreshActiveBaseline(void);
int editorWatchActiveHasDiskConflict(void);
void editorWatchTestReset(void);

#endif
