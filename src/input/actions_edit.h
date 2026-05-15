#ifndef ROTIDE_INPUT_ACTIONS_EDIT_H
#define ROTIDE_INPUT_ACTIONS_EDIT_H

#include "rotide.h"

typedef void (*editorEditActionFn)(void);
typedef void (*editorEditMoveLineFn)(int direction);

int editorHandleEditMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
		editorEditActionFn clear_selection_mode, editorEditActionFn pin_active_preview_for_edit,
		editorEditActionFn clear_search_state, editorEditActionFn toggle_selection_mode,
		editorEditActionFn copy_selection, editorEditActionFn cut_selection,
		editorEditActionFn delete_selection, editorEditActionFn paste_clipboard,
		editorEditActionFn delete_char_action, editorEditActionFn backspace_action,
		editorEditActionFn move_line_up, editorEditActionFn move_line_down,
		editorEditActionFn toggle_comment_lines, int *effects_io);
void editorEditToggleSelectionMode(editorEditActionFn clear_selection_mode,
		editorEditActionFn align_cursor_with_row_end);
void editorEditCopySelection(editorEditActionFn clear_selection_mode);
void editorEditCutSelection(editorEditActionFn clear_selection_mode);
void editorEditDeleteSelection(editorEditActionFn clear_selection_mode);
void editorEditPasteClipboard(editorEditActionFn clear_selection_mode);
void editorEditToggleCommentLines(editorEditActionFn clear_selection_mode,
		editorEditActionFn pin_active_preview_for_edit);
void editorEditMoveCurrentLine(int direction);

#endif
