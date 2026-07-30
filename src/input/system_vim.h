#ifndef ROTIDE_INPUT_SYSTEM_VIM_H
#define ROTIDE_INPUT_SYSTEM_VIM_H

#include <stddef.h>

enum editorInputKeyEffect {
	EDITOR_INPUT_KEY_EFFECT_NONE = 0,
	EDITOR_INPUT_KEY_EFFECT_VIEWPORT_SCROLL = 1 << 0,
	EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT = 1 << 1
};

void editorVimInitialize(void);
void editorVimReset(void);
void editorVimBeginSelection(size_t anchor_offset);
void editorVimBeginLineSelection(size_t anchor_offset);
void editorVimBeginColumnSelection(void);
void editorVimCancelSelection(void);
int editorVimHandleKey(int c, int *effects_out);
int editorVimOpenExCommandLine(int *effects_out);
int editorVimKeySequencePending(void);
int editorVimResolveCommand(const char *name, int *command_id_out);
int editorVimBindKey(const char *mode, const char *name, int key);
void editorVimStatusSegment(char *buf, size_t bufsize);
int editorVimStatusColor(void);
int editorVimCursorStyle(void);
int editorVimIsInsertMode(void);
void editorVimKeymapResetDefaults(void);
int editorVimLeaderKey(void);
int editorVimLeaderAction(int c, int *action_out);
int editorVimCtrlWAction(int c, int *action_out);

#endif
