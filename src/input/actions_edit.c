#include "input/actions_edit.h"

#include "editing/edit.h"
#include "editing/history.h"

int editorHandleEditMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
		editorEditActionFn clear_selection_mode, editorEditActionFn pin_active_preview_for_edit,
		editorEditActionFn clear_search_state, editorEditActionFn toggle_selection_mode,
		editorEditActionFn copy_selection, editorEditActionFn cut_selection,
		editorEditActionFn delete_selection, editorEditActionFn paste_clipboard,
		editorEditActionFn delete_char_action, editorEditActionFn backspace_action,
		editorEditActionFn move_line_up, editorEditActionFn move_line_down,
		editorEditActionFn toggle_comment_lines, int *effects_io) {
	int effects = effects_io != NULL ? *effects_io : 0;

	switch (action) {
	case EDITOR_ACTION_NEWLINE:
		if (clear_selection_mode != NULL) {
			clear_selection_mode();
		}
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
		{
			int dirty_before = E.dirty;
			editorInsertNewline();
			editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_TOGGLE_SELECTION:
		editorHistoryBreakGroup();
		if (toggle_selection_mode != NULL) {
			toggle_selection_mode();
		}
		break;
	case EDITOR_ACTION_COPY_SELECTION:
		editorHistoryBreakGroup();
		if (copy_selection != NULL) {
			copy_selection();
		}
		break;
	case EDITOR_ACTION_CUT_SELECTION:
		editorHistoryBreakGroup();
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (cut_selection != NULL) {
			cut_selection();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_DELETE_SELECTION:
		editorHistoryBreakGroup();
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (delete_selection != NULL) {
			delete_selection();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_PASTE:
		editorHistoryBreakGroup();
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (paste_clipboard != NULL) {
			paste_clipboard();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_UNDO:
		editorHistoryBreakGroup();
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (editorUndo() == 1) {
			if (clear_search_state != NULL) {
				clear_search_state();
			}
			effects |= cursor_or_edit_effect_bit;
		}
		break;
	case EDITOR_ACTION_REDO:
		editorHistoryBreakGroup();
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (editorRedo() == 1) {
			if (clear_search_state != NULL) {
				clear_search_state();
			}
			effects |= cursor_or_edit_effect_bit;
		}
		break;
	case EDITOR_ACTION_DELETE_CHAR:
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (delete_char_action != NULL) {
			delete_char_action();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_BACKSPACE:
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (backspace_action != NULL) {
			backspace_action();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_MOVE_LINE_UP:
		if (clear_selection_mode != NULL) {
			clear_selection_mode();
		}
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (move_line_up != NULL) {
			move_line_up();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_MOVE_LINE_DOWN:
		if (clear_selection_mode != NULL) {
			clear_selection_mode();
		}
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (move_line_down != NULL) {
			move_line_down();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_TOGGLE_COMMENT:
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (toggle_comment_lines != NULL) {
			toggle_comment_lines();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	default:
		return 0;
	}

	if (effects_io != NULL) {
		*effects_io = effects;
	}
	return 1;
}
