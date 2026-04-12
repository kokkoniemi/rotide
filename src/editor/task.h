#ifndef EDITOR_TASK_H
#define EDITOR_TASK_H

int editorTaskStart(const char *title, const char *command,
		const char *success_status, const char *failure_status);
int editorTaskPoll(void);
int editorTaskIsRunning(void);
int editorTaskRunningTabIndex(void);
int editorTaskTerminate(void);

#endif
