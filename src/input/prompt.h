#ifndef ROTIDE_INPUT_PROMPT_H
#define ROTIDE_INPUT_PROMPT_H

typedef void (*editorPromptCallback)(const char *query, int key);
typedef char *(*editorPromptCompleteFn)(const char *current, const char *anchor, void *ctx,
                                        int tab_iteration);

char *editorPromptWithCallback(const char *prompt, int allow_empty, editorPromptCallback callback);
char *editorPromptWithCompletion(const char *prompt, int allow_empty,
                                 editorPromptCompleteFn complete_fn, void *complete_ctx);
char *editorPrompt(const char *prompt);
int editorPromptYesNo(const char *prompt);

void editorExitOnInputShutdown(void);

#endif
