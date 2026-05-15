#ifndef ROTIDE_INPUT_PROMPT_H
#define ROTIDE_INPUT_PROMPT_H

typedef void (*editorPromptCallback)(const char *query, int key);

char *editorPromptWithCallback(const char *prompt, int allow_empty,
		editorPromptCallback callback);
char *editorPrompt(const char *prompt);
int editorPromptYesNo(const char *prompt);

void editorExitOnInputShutdown(void);

#endif
