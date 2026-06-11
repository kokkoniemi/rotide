#ifndef ROTIDE_INPUT_ACTIONS_LANGUAGE_H
#define ROTIDE_INPUT_ACTIONS_LANGUAGE_H

#include "rotide.h"

typedef void (*editorLanguageActionFn)(void);

int editorHandleLanguageMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
                                     void (*pin_active_preview_for_edit)(void),
                                     editorLanguageActionFn goto_definition,
                                     editorLanguageActionFn goto_implementation,
                                     editorLanguageActionFn goto_references,
                                     editorLanguageActionFn hover,
                                     editorLanguageActionFn goto_symbol,
                                     editorLanguageActionFn apply_eslint_fixes, int *effects_io);
int editorLanguageGoToSupported(enum editorSyntaxLanguage language);
int editorLanguageGoToEnabled(void);
const char *editorLanguageGoToLabel(void);
const char *editorLanguageGoToServerName(void);
const char *editorLanguageGoToCommand(void);
const char *editorLanguageGoToCommandSettingName(void);
void editorLanguageMaybePromptInstallServer(void);
void editorLanguagePromptInstallSharedVscodeServers(void);

#endif
