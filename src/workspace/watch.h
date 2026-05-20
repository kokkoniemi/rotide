#ifndef ROTIDE_WORKSPACE_WATCH_H
#define ROTIDE_WORKSPACE_WATCH_H

#include "rotide.h"

int editorWatchPoll(void);
int editorWatchPollNow(void);
void editorWatchRefreshActiveBaseline(void);
int editorWatchActiveHasDiskConflict(void);
void editorWatchTestReset(void);

#endif
