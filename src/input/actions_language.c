#include "input/actions_language.h"

#include "editing/history.h"

int editorHandleLanguageMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
		void (*pin_active_preview_for_edit)(void), editorLanguageActionFn goto_definition,
		editorLanguageActionFn goto_implementation, editorLanguageActionFn goto_symbol,
		editorLanguageActionFn apply_eslint_fixes, int *effects_io) {
	int effects = effects_io != NULL ? *effects_io : 0;

	switch (action) {
	case EDITOR_ACTION_GOTO_DEFINITION:
		editorHistoryBreakGroup();
		if (goto_definition != NULL) {
			goto_definition();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_GOTO_IMPLEMENTATION:
		editorHistoryBreakGroup();
		if (goto_implementation != NULL) {
			goto_implementation();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_GOTO_SYMBOL:
		editorHistoryBreakGroup();
		if (goto_symbol != NULL) {
			goto_symbol();
		}
		effects |= cursor_or_edit_effect_bit;
		break;
	case EDITOR_ACTION_ESLINT_FIX:
		editorHistoryBreakGroup();
		if (pin_active_preview_for_edit != NULL) {
			pin_active_preview_for_edit();
		}
		if (apply_eslint_fixes != NULL) {
			apply_eslint_fixes();
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
