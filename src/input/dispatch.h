#ifndef INPUT_H
#define INPUT_H

#include "rotide.h"

char *editorPrompt(const char *prompt);
int editorPromptYesNo(const char *prompt);
void editorProcessKeypress(void);

#endif
