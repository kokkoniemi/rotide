#ifndef ROTIDE_INPUT_ACTIONS_EDIT_H
#define ROTIDE_INPUT_ACTIONS_EDIT_H

#include "rotide.h"

typedef void (*editorEditActionFn)(void);

int editorHandleEditMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
		editorEditActionFn clear_selection_mode, editorEditActionFn pin_active_preview_for_edit,
		editorEditActionFn clear_search_state, editorEditActionFn toggle_selection_mode,
		editorEditActionFn copy_selection, editorEditActionFn cut_selection,
		editorEditActionFn delete_selection, editorEditActionFn paste_clipboard,
		editorEditActionFn delete_char_action, editorEditActionFn backspace_action,
		editorEditActionFn move_line_up, editorEditActionFn move_line_down,
		editorEditActionFn toggle_comment_lines, int *effects_io);

#endif
