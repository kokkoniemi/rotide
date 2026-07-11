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

/*
 * Install a language server by canonical name (or a language alias, e.g.
 * "html"/"css"/"json"/"eslint" -> vscode-langservers-extracted, "javascript" ->
 * typescript-language-server). Starts the same confirm+install/guide flow as the
 * automatic "server not found" prompt. Returns 1 if the name was recognized
 * (flow started or user declined), 0 if the name is not an installable server.
 */
int editorLanguageInstallServerByName(const char *name);
/* Pure recognition check for editorLanguageInstallServerByName; no side effects. */
int editorLanguageServerNameIsInstallable(const char *name);

/*
 * Pure OS/arch -> texlab prebuilt-asset mapping. Returns the release asset file
 * name for the given `uname` sysname/machine (and musl flag for the Linux
 * alpine build), or NULL if no prebuilt binary exists for that platform.
 * Exposed for unit testing without touching the live host.
 */
const char *editorLanguageTexlabAssetFor(const char *sysname, const char *machine, int is_musl);

#endif
