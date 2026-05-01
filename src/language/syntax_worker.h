#ifndef ROTIDE_LANGUAGE_SYNTAX_WORKER_H
#define ROTIDE_LANGUAGE_SYNTAX_WORKER_H

#include "language/syntax.h"
#include "rotide.h"

#include <stddef.h>
#include <stdint.h>

struct editorSyntaxWorkerJob {
	enum editorSyntaxLanguage language;
	uint64_t revision;
	uint64_t generation;
	int first_row;
	int row_count;
	char *text;
	size_t text_len;
};

struct editorSyntaxWorkerResult {
	enum editorSyntaxLanguage language;
	uint64_t revision;
	uint64_t generation;
	int first_row;
	int row_count;
	int parsed;
	struct editorSyntaxState *state;
	int *span_counts;
	struct editorRowSyntaxSpan *spans;
};

int editorSyntaxBackgroundStart(void);
void editorSyntaxBackgroundStop(void);
int editorSyntaxBackgroundEnabled(void);
void editorSyntaxBackgroundSetEnabledForTests(int enabled);
int editorSyntaxBackgroundPoll(void);
int editorSyntaxBackgroundFlushForTests(void);

int editorSyntaxWorkerSchedule(struct editorSyntaxWorkerJob *job);
struct editorSyntaxWorkerResult *editorSyntaxWorkerTakeResult(void);
int editorSyntaxWorkerHasWork(void);
void editorSyntaxWorkerWaitForIdle(void);
void editorSyntaxWorkerResultDestroy(struct editorSyntaxWorkerResult *result);

#endif
