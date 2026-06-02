#include "config/keymap.h"

#include "config/common.h"
#include "rotide.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct keymapActionName {
	const char *name;
	enum editorAction action;
};

struct keymapNamedKey {
	const char *name;
	int key;
};

enum keymapFileStatus {
	KEYMAP_FILE_APPLIED = 0,
	KEYMAP_FILE_MISSING,
	KEYMAP_FILE_INVALID,
	KEYMAP_FILE_OUT_OF_MEMORY
};

static const struct keymapActionName g_keymap_action_names[] = {
        {"quit", EDITOR_ACTION_QUIT},
        {"save", EDITOR_ACTION_SAVE},
        {"new_tab", EDITOR_ACTION_NEW_TAB},
        {"close_tab", EDITOR_ACTION_CLOSE_TAB},
        {"next_tab", EDITOR_ACTION_NEXT_TAB},
        {"prev_tab", EDITOR_ACTION_PREV_TAB},
        {"focus_drawer", EDITOR_ACTION_FOCUS_DRAWER},
        {"toggle_drawer", EDITOR_ACTION_TOGGLE_DRAWER},
        {"main_menu", EDITOR_ACTION_MAIN_MENU},
        {"context_menu", EDITOR_ACTION_CONTEXT_MENU},
        {"resize_drawer_narrow", EDITOR_ACTION_RESIZE_DRAWER_NARROW},
        {"resize_drawer_widen", EDITOR_ACTION_RESIZE_DRAWER_WIDEN},
        {"toggle_line_wrap", EDITOR_ACTION_TOGGLE_LINE_WRAP},
        {"toggle_line_numbers", EDITOR_ACTION_TOGGLE_LINE_NUMBERS},
        {"toggle_current_line_highlight", EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT},
        {"find_file", EDITOR_ACTION_FIND_FILE},
        {"project_search", EDITOR_ACTION_PROJECT_SEARCH},
        {"find", EDITOR_ACTION_FIND},
        {"goto_line", EDITOR_ACTION_GOTO_LINE},
        {"goto_matching_bracket", EDITOR_ACTION_GOTO_MATCHING_BRACKET},
        {"goto_definition", EDITOR_ACTION_GOTO_DEFINITION},
        {"goto_implementation", EDITOR_ACTION_GOTO_IMPLEMENTATION},
        {"goto_symbol", EDITOR_ACTION_GOTO_SYMBOL},
        {"eslint_fix", EDITOR_ACTION_ESLINT_FIX},
        {"toggle_selection", EDITOR_ACTION_TOGGLE_SELECTION},
        {"select_all", EDITOR_ACTION_SELECT_ALL},
        {"select_left", EDITOR_ACTION_SELECT_LEFT},
        {"select_right", EDITOR_ACTION_SELECT_RIGHT},
        {"select_up", EDITOR_ACTION_SELECT_UP},
        {"select_down", EDITOR_ACTION_SELECT_DOWN},
        {"select_word_left", EDITOR_ACTION_SELECT_WORD_LEFT},
        {"select_word_right", EDITOR_ACTION_SELECT_WORD_RIGHT},
        {"select_home", EDITOR_ACTION_SELECT_HOME},
        {"select_end", EDITOR_ACTION_SELECT_END},
        {"copy_selection", EDITOR_ACTION_COPY_SELECTION},
        {"cut_selection", EDITOR_ACTION_CUT_SELECTION},
        {"delete_selection", EDITOR_ACTION_DELETE_SELECTION},
        {"paste", EDITOR_ACTION_PASTE},
        {"undo", EDITOR_ACTION_UNDO},
        {"redo", EDITOR_ACTION_REDO},
        {"move_home", EDITOR_ACTION_MOVE_HOME},
        {"move_end", EDITOR_ACTION_MOVE_END},
        {"move_word_left", EDITOR_ACTION_MOVE_WORD_LEFT},
        {"move_word_right", EDITOR_ACTION_MOVE_WORD_RIGHT},
        {"page_up", EDITOR_ACTION_PAGE_UP},
        {"page_down", EDITOR_ACTION_PAGE_DOWN},
        {"scroll_left", EDITOR_ACTION_SCROLL_LEFT},
        {"scroll_right", EDITOR_ACTION_SCROLL_RIGHT},
        {"scroll_up", EDITOR_ACTION_SCROLL_UP},
        {"scroll_down", EDITOR_ACTION_SCROLL_DOWN},
        {"move_up", EDITOR_ACTION_MOVE_UP},
        {"move_down", EDITOR_ACTION_MOVE_DOWN},
        {"move_left", EDITOR_ACTION_MOVE_LEFT},
        {"move_right", EDITOR_ACTION_MOVE_RIGHT},
        {"newline", EDITOR_ACTION_NEWLINE},
        {"escape", EDITOR_ACTION_ESCAPE},
        {"redraw", EDITOR_ACTION_REDRAW},
        {"delete_char", EDITOR_ACTION_DELETE_CHAR},
        {"backspace", EDITOR_ACTION_BACKSPACE},
        {"move_line_up", EDITOR_ACTION_MOVE_LINE_UP},
        {"move_line_down", EDITOR_ACTION_MOVE_LINE_DOWN},
        {"toggle_comment", EDITOR_ACTION_TOGGLE_COMMENT},
        {"column_select_up", EDITOR_ACTION_COLUMN_SELECT_UP},
        {"column_select_down", EDITOR_ACTION_COLUMN_SELECT_DOWN},
        {"column_select_left", EDITOR_ACTION_COLUMN_SELECT_LEFT},
        {"column_select_right", EDITOR_ACTION_COLUMN_SELECT_RIGHT},
        {"find_replace", EDITOR_ACTION_FIND_REPLACE},
        {"drawer_create_file", EDITOR_ACTION_DRAWER_CREATE_FILE},
        {"drawer_create_folder", EDITOR_ACTION_DRAWER_CREATE_FOLDER},
        {"drawer_rename", EDITOR_ACTION_DRAWER_RENAME},
        {"drawer_delete", EDITOR_ACTION_DRAWER_DELETE},
        {"git_drawer", EDITOR_ACTION_GIT_DRAWER},
        {"lsp_drawer", EDITOR_ACTION_LSP_DRAWER},
        {"dap_drawer", EDITOR_ACTION_DAP_DRAWER},
        {"dap_start", EDITOR_ACTION_DAP_START},
        {"dap_stop", EDITOR_ACTION_DAP_STOP},
        {"dap_continue", EDITOR_ACTION_DAP_CONTINUE},
        {"dap_pause", EDITOR_ACTION_DAP_PAUSE},
        {"dap_step_over", EDITOR_ACTION_DAP_STEP_OVER},
        {"dap_step_into", EDITOR_ACTION_DAP_STEP_INTO},
        {"dap_step_out", EDITOR_ACTION_DAP_STEP_OUT},
        {"dap_toggle_breakpoint", EDITOR_ACTION_DAP_TOGGLE_BREAKPOINT},
        {"split_horizontal", EDITOR_ACTION_SPLIT_HORIZONTAL},
        {"split_vertical", EDITOR_ACTION_SPLIT_VERTICAL},
        {"close_pane", EDITOR_ACTION_CLOSE_PANE},
        {"focus_left_pane", EDITOR_ACTION_FOCUS_LEFT_PANE},
        {"focus_right_pane", EDITOR_ACTION_FOCUS_RIGHT_PANE},
        {"focus_up_pane", EDITOR_ACTION_FOCUS_UP_PANE},
        {"focus_down_pane", EDITOR_ACTION_FOCUS_DOWN_PANE},
        {"move_tab_left_pane", EDITOR_ACTION_MOVE_TAB_LEFT_PANE},
        {"move_tab_right_pane", EDITOR_ACTION_MOVE_TAB_RIGHT_PANE},
        {"move_tab_up_pane", EDITOR_ACTION_MOVE_TAB_UP_PANE},
        {"move_tab_down_pane", EDITOR_ACTION_MOVE_TAB_DOWN_PANE},
        {"pane_grow", EDITOR_ACTION_PANE_GROW},
        {"pane_shrink", EDITOR_ACTION_PANE_SHRINK},
        {"terminal_open", EDITOR_ACTION_TERMINAL_OPEN},
        {"terminal_open_vertical", EDITOR_ACTION_TERMINAL_OPEN_VERTICAL},
        {"terminal_prefix", EDITOR_ACTION_TERMINAL_PREFIX},
        {"open_settings", EDITOR_ACTION_OPEN_SETTINGS},
};

static const struct keymapNamedKey g_keymap_named_keys[] = {
        {"left", ARROW_LEFT}, {"right", ARROW_RIGHT},   {"up", ARROW_UP},
        {"down", ARROW_DOWN}, {"home", HOME_KEY},       {"end", END_KEY},
        {"page_up", PAGE_UP}, {"page_down", PAGE_DOWN}, {"enter", '\r'},
        {"esc", '\x1b'},      {"backspace", BACKSPACE}, {"del", DEL_KEY},
};

static int keymapHasBindingForKey(const struct editorKeymap *keymap, int key) {
	for (size_t i = 0; i < keymap->len; i++) {
		if (keymap->bindings[i].key == key) {
			return 1;
		}
	}
	return 0;
}

static int keymapBindingUsesReservedTerminalInput(enum editorAction action, int key) {
	/*
	 * Rotide binds terminal key codes, not physical keyboard keys, so some
	 * key combinations alias the same input byte unless an extended protocol is used.
	 * Reserve those shared bytes for their built-in meanings under the baseline
	 * terminal model, so config cannot steal Tab, Enter, Escape, or Ctrl-H backspace.
	 */
	switch (key) {
		case '\t':
			return 1;
		case '\r':
			return action != EDITOR_ACTION_NEWLINE;
		case '\x1b':
			return action != EDITOR_ACTION_ESCAPE;
		case CTRL_KEY('h'):
			return action != EDITOR_ACTION_BACKSPACE;
		default:
			return 0;
	}
}

static void keymapRemoveActionBindings(struct editorKeymap *keymap, enum editorAction action) {
	size_t write_idx = 0;
	for (size_t read_idx = 0; read_idx < keymap->len; read_idx++) {
		if (keymap->bindings[read_idx].action == action) {
			continue;
		}
		keymap->bindings[write_idx] = keymap->bindings[read_idx];
		write_idx++;
	}
	keymap->len = write_idx;
}

static int keymapAppendBinding(struct editorKeymap *keymap, int key, enum editorAction action) {
	if (keymap->len >= ROTIDE_KEYMAP_MAX_BINDINGS) {
		return 0;
	}
	if (keymapHasBindingForKey(keymap, key)) {
		return 0;
	}

	keymap->bindings[keymap->len].key = key;
	keymap->bindings[keymap->len].action = action;
	keymap->len++;
	return 1;
}

static int keymapSetActionBinding(struct editorKeymap *keymap, enum editorAction action, int key) {
	struct editorKeymap updated = *keymap;

	if (keymapBindingUsesReservedTerminalInput(action, key)) {
		return 0;
	}

	keymapRemoveActionBindings(&updated, action);
	if (!keymapAppendBinding(&updated, key, action)) {
		return 0;
	}

	*keymap = updated;
	return 1;
}

static int keymapResolveActionName(const char *name, enum editorAction *action_out) {
	for (size_t i = 0; i < sizeof(g_keymap_action_names) / sizeof(g_keymap_action_names[0]);
	     i++) {
		if (strcmp(g_keymap_action_names[i].name, name) == 0) {
			*action_out = g_keymap_action_names[i].action;
			return 1;
		}
	}
	return 0;
}

static int keymapParseCtrlKeySpec(const char *spec, int *key_out) {
	if (strncmp(spec, "ctrl+", 5) != 0 || spec[5] == '\0' || spec[6] != '\0') {
		return 0;
	}

	unsigned char ch = (unsigned char)spec[5];
	if (isalpha(ch)) {
		ch = (unsigned char)tolower(ch);
		if (ch < 'a' || ch > 'z') {
			return 0;
		}
		*key_out = CTRL_KEY((int)ch);
		return 1;
	}

	switch (ch) {
		case '@':
		case '[':
		case '\\':
		case ']':
		case '^':
		case '_':
			*key_out = CTRL_KEY((int)ch);
			return 1;
		default:
			return 0;
	}
}

enum keymapModifierFlags {
	KEYMAP_MOD_NONE = 0,
	KEYMAP_MOD_CTRL = 1 << 0,
	KEYMAP_MOD_ALT = 1 << 1,
	KEYMAP_MOD_SHIFT = 1 << 2
};

static int keymapParseLetterToken(const char *token, char *letter_out) {
	if (token[0] == '\0' || token[1] != '\0') {
		return 0;
	}
	unsigned char ch = (unsigned char)token[0];
	if (!isalpha(ch)) {
		return 0;
	}
	*letter_out = (char)tolower(ch);
	return 1;
}

static int keymapParseArrowToken(const char *token, int *arrow_out) {
	if (strcmp(token, "left") == 0) {
		*arrow_out = ARROW_LEFT;
		return 1;
	}
	if (strcmp(token, "right") == 0) {
		*arrow_out = ARROW_RIGHT;
		return 1;
	}
	if (strcmp(token, "up") == 0) {
		*arrow_out = ARROW_UP;
		return 1;
	}
	if (strcmp(token, "down") == 0) {
		*arrow_out = ARROW_DOWN;
		return 1;
	}
	return 0;
}

static int keymapArrowWithModifiers(int arrow, int modifiers, int *key_out) {
	static const struct {
		int modifiers;
		int left;
		int right;
		int down;
		int up;
	} arrow_keys[] = {
	        {KEYMAP_MOD_SHIFT, SHIFT_ARROW_LEFT, SHIFT_ARROW_RIGHT, SHIFT_ARROW_DOWN,
	         SHIFT_ARROW_UP},
	        {KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, CTRL_SHIFT_ARROW_LEFT, CTRL_SHIFT_ARROW_RIGHT,
	         CTRL_SHIFT_ARROW_DOWN, CTRL_SHIFT_ARROW_UP},
	        {KEYMAP_MOD_ALT, ALT_ARROW_LEFT, ALT_ARROW_RIGHT, ALT_ARROW_DOWN, ALT_ARROW_UP},
	        {KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, ALT_SHIFT_ARROW_LEFT, ALT_SHIFT_ARROW_RIGHT,
	         ALT_SHIFT_ARROW_DOWN, ALT_SHIFT_ARROW_UP},
	        {KEYMAP_MOD_CTRL, CTRL_ARROW_LEFT, CTRL_ARROW_RIGHT, CTRL_ARROW_DOWN,
	         CTRL_ARROW_UP},
	        {KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT, CTRL_ALT_ARROW_LEFT, CTRL_ALT_ARROW_RIGHT,
	         CTRL_ALT_ARROW_DOWN, CTRL_ALT_ARROW_UP},
	        {KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT | KEYMAP_MOD_ALT, CTRL_SHIFT_ALT_ARROW_LEFT,
	         CTRL_SHIFT_ALT_ARROW_RIGHT, CTRL_SHIFT_ALT_ARROW_DOWN, CTRL_SHIFT_ALT_ARROW_UP},
	};
	for (size_t i = 0; i < sizeof(arrow_keys) / sizeof(arrow_keys[0]); i++) {
		if (arrow_keys[i].modifiers != modifiers) {
			continue;
		}
		switch (arrow) {
			case ARROW_LEFT:
				*key_out = arrow_keys[i].left;
				return 1;
			case ARROW_RIGHT:
				*key_out = arrow_keys[i].right;
				return 1;
			case ARROW_DOWN:
				*key_out = arrow_keys[i].down;
				return 1;
			case ARROW_UP:
				*key_out = arrow_keys[i].up;
				return 1;
			default:
				return 0;
		}
	}
	return 0;
}

static int keymapParseKeySpec(const char *spec, int *key_out) {
	if (keymapParseCtrlKeySpec(spec, key_out)) {
		return 1;
	}

	char normalized[64];
	size_t spec_len = strlen(spec);
	if (spec_len == 0 || spec_len >= sizeof(normalized)) {
		return 0;
	}
	for (size_t i = 0; i < spec_len; i++) {
		normalized[i] = (char)tolower((unsigned char)spec[i]);
	}
	normalized[spec_len] = '\0';

	int modifiers = KEYMAP_MOD_NONE;
	char *key_token = NULL;
	char *cursor = normalized;
	while (1) {
		char *sep = strchr(cursor, '+');
		if (sep != NULL) {
			*sep = '\0';
		}
		if (cursor[0] == '\0') {
			return 0;
		}

		if (strcmp(cursor, "ctrl") == 0) {
			if (modifiers & KEYMAP_MOD_CTRL) {
				return 0;
			}
			modifiers |= KEYMAP_MOD_CTRL;
		} else if (strcmp(cursor, "alt") == 0) {
			if (modifiers & KEYMAP_MOD_ALT) {
				return 0;
			}
			modifiers |= KEYMAP_MOD_ALT;
		} else if (strcmp(cursor, "shift") == 0) {
			if (modifiers & KEYMAP_MOD_SHIFT) {
				return 0;
			}
			modifiers |= KEYMAP_MOD_SHIFT;
		} else {
			if (key_token != NULL) {
				return 0;
			}
			key_token = cursor;
		}

		if (sep == NULL) {
			break;
		}
		cursor = sep + 1;
	}

	if (key_token == NULL) {
		return 0;
	}

	if (modifiers == KEYMAP_MOD_NONE) {
		for (size_t i = 0; i < sizeof(g_keymap_named_keys) / sizeof(g_keymap_named_keys[0]);
		     i++) {
			if (strcmp(g_keymap_named_keys[i].name, key_token) == 0) {
				*key_out = g_keymap_named_keys[i].key;
				return 1;
			}
		}
		return 0;
	}

	if (modifiers == KEYMAP_MOD_SHIFT) {
		if (strcmp(key_token, "home") == 0) {
			*key_out = SHIFT_HOME_KEY;
			return 1;
		}
		if (strcmp(key_token, "end") == 0) {
			*key_out = SHIFT_END_KEY;
			return 1;
		}
	}

	char letter = '\0';
	if (keymapParseLetterToken(key_token, &letter)) {
		if (modifiers == KEYMAP_MOD_CTRL) {
			*key_out = CTRL_KEY((int)letter);
			return 1;
		}
		if (modifiers == KEYMAP_MOD_ALT) {
			*key_out = EDITOR_ALT_LETTER_KEY(letter);
			return 1;
		}
		if (modifiers == (KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT)) {
			*key_out = EDITOR_CTRL_ALT_LETTER_KEY(letter);
			return 1;
		}
		return 0;
	}

	int arrow = 0;
	if (keymapParseArrowToken(key_token, &arrow)) {
		return keymapArrowWithModifiers(arrow, modifiers, key_out);
	}

	return 0;
}

static int keymapConfigOnSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "keymap") == 0;
}

static int keymapConfigOnEntry(void *ctx, const char *key, char *value) {
	struct editorKeymap *keymap = ctx;

	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!keymapResolveActionName(key, &action)) {
		return 0;
	}

	char key_spec[64];
	if (!editorConfigParseQuotedValue(value, key_spec, sizeof(key_spec))) {
		return 0;
	}

	int parsed_key = 0;
	if (!keymapParseKeySpec(key_spec, &parsed_key)) {
		return 0;
	}

	return keymapSetActionBinding(keymap, action, parsed_key);
}

static enum keymapFileStatus keymapApplyConfigFile(struct editorKeymap *keymap, const char *path) {
	struct editorKeymap updated = *keymap;
	struct editorConfigScanner scanner = {
	        .on_section = keymapConfigOnSection,
	        .on_entry = keymapConfigOnEntry,
	};

	switch (editorConfigScanFile(path, &scanner, &updated)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return KEYMAP_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			*keymap = updated;
			return KEYMAP_FILE_APPLIED;
		case EDITOR_CONFIG_SCAN_MALFORMED:
		default:
			return KEYMAP_FILE_INVALID;
	}
}

static int keymapFormatKey(int key, char *buf, size_t bufsize) {
	if (bufsize == 0) {
		return 0;
	}

	if (key >= 1 && key <= 26) {
		return snprintf(buf, bufsize, "Ctrl-%c", 'A' + key - 1) > 0;
	}
	if (EDITOR_IS_ALT_LETTER_KEY(key)) {
		return snprintf(buf, bufsize, "Alt-%c",
		                'A' + (int)(EDITOR_ALT_LETTER_FROM_KEY(key) - 'a')) > 0;
	}
	if (EDITOR_IS_CTRL_ALT_LETTER_KEY(key)) {
		return snprintf(buf, bufsize, "Ctrl-Alt-%c",
		                'A' + (int)(EDITOR_CTRL_ALT_LETTER_FROM_KEY(key) - 'a')) > 0;
	}

	switch (key) {
		case ALT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Alt-Left") > 0;
		case ALT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Alt-Right") > 0;
		case ALT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Alt-Down") > 0;
		case ALT_ARROW_UP:
			return snprintf(buf, bufsize, "Alt-Up") > 0;
		case ALT_SHIFT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Alt-Shift-Left") > 0;
		case ALT_SHIFT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Alt-Shift-Right") > 0;
		case ALT_SHIFT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Alt-Shift-Down") > 0;
		case ALT_SHIFT_ARROW_UP:
			return snprintf(buf, bufsize, "Alt-Shift-Up") > 0;
		case CTRL_ARROW_LEFT:
			return snprintf(buf, bufsize, "Ctrl-Left") > 0;
		case CTRL_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Ctrl-Right") > 0;
		case CTRL_ARROW_DOWN:
			return snprintf(buf, bufsize, "Ctrl-Down") > 0;
		case CTRL_ARROW_UP:
			return snprintf(buf, bufsize, "Ctrl-Up") > 0;
		case SHIFT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Shift-Left") > 0;
		case SHIFT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Shift-Right") > 0;
		case SHIFT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Shift-Down") > 0;
		case SHIFT_ARROW_UP:
			return snprintf(buf, bufsize, "Shift-Up") > 0;
		case CTRL_SHIFT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Ctrl-Shift-Left") > 0;
		case CTRL_SHIFT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Ctrl-Shift-Right") > 0;
		case CTRL_SHIFT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Ctrl-Shift-Down") > 0;
		case CTRL_SHIFT_ARROW_UP:
			return snprintf(buf, bufsize, "Ctrl-Shift-Up") > 0;
		case CTRL_ALT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Ctrl-Alt-Left") > 0;
		case CTRL_ALT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Ctrl-Alt-Right") > 0;
		case CTRL_ALT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Ctrl-Alt-Down") > 0;
		case CTRL_ALT_ARROW_UP:
			return snprintf(buf, bufsize, "Ctrl-Alt-Up") > 0;
		case CTRL_SHIFT_ALT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Ctrl-Shift-Alt-Left") > 0;
		case CTRL_SHIFT_ALT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Ctrl-Shift-Alt-Right") > 0;
		case CTRL_SHIFT_ALT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Ctrl-Shift-Alt-Down") > 0;
		case CTRL_SHIFT_ALT_ARROW_UP:
			return snprintf(buf, bufsize, "Ctrl-Shift-Alt-Up") > 0;
		case ARROW_LEFT:
			return snprintf(buf, bufsize, "Left") > 0;
		case ARROW_RIGHT:
			return snprintf(buf, bufsize, "Right") > 0;
		case ARROW_UP:
			return snprintf(buf, bufsize, "Up") > 0;
		case ARROW_DOWN:
			return snprintf(buf, bufsize, "Down") > 0;
		case HOME_KEY:
			return snprintf(buf, bufsize, "Home") > 0;
		case END_KEY:
			return snprintf(buf, bufsize, "End") > 0;
		case SHIFT_HOME_KEY:
			return snprintf(buf, bufsize, "Shift-Home") > 0;
		case SHIFT_END_KEY:
			return snprintf(buf, bufsize, "Shift-End") > 0;
		case PAGE_UP:
			return snprintf(buf, bufsize, "PageUp") > 0;
		case PAGE_DOWN:
			return snprintf(buf, bufsize, "PageDown") > 0;
		case DEL_KEY:
			return snprintf(buf, bufsize, "Del") > 0;
		case BACKSPACE:
			return snprintf(buf, bufsize, "Backspace") > 0;
		case '\r':
			return snprintf(buf, bufsize, "Enter") > 0;
		case '\x1b':
			return snprintf(buf, bufsize, "Esc") > 0;
		default:
			break;
	}

	if (key >= CHAR_MIN && key <= CHAR_MAX && isprint((unsigned char)key)) {
		return snprintf(buf, bufsize, "%c", key) > 0;
	}

	return snprintf(buf, bufsize, "Key%d", key) > 0;
}

void editorKeymapInitDefaults(struct editorKeymap *keymap) {
	keymap->len = 0;
	(void)keymapAppendBinding(keymap, CTRL_KEY('q'), EDITOR_ACTION_QUIT);
	(void)keymapAppendBinding(keymap, CTRL_KEY('s'), EDITOR_ACTION_SAVE);
	(void)keymapAppendBinding(keymap, CTRL_KEY('n'), EDITOR_ACTION_NEW_TAB);
	(void)keymapAppendBinding(keymap, CTRL_KEY('w'), EDITOR_ACTION_CLOSE_TAB);
	(void)keymapAppendBinding(keymap, ALT_ARROW_RIGHT, EDITOR_ACTION_NEXT_TAB);
	(void)keymapAppendBinding(keymap, ALT_ARROW_LEFT, EDITOR_ACTION_PREV_TAB);
	(void)keymapAppendBinding(keymap, CTRL_SHIFT_ALT_ARROW_LEFT,
	                          EDITOR_ACTION_MOVE_TAB_LEFT_PANE);
	(void)keymapAppendBinding(keymap, CTRL_SHIFT_ALT_ARROW_RIGHT,
	                          EDITOR_ACTION_MOVE_TAB_RIGHT_PANE);
	(void)keymapAppendBinding(keymap, CTRL_SHIFT_ALT_ARROW_UP, EDITOR_ACTION_MOVE_TAB_UP_PANE);
	(void)keymapAppendBinding(keymap, CTRL_SHIFT_ALT_ARROW_DOWN,
	                          EDITOR_ACTION_MOVE_TAB_DOWN_PANE);
	(void)keymapAppendBinding(keymap, CTRL_KEY('e'), EDITOR_ACTION_FOCUS_DRAWER);
	(void)keymapAppendBinding(keymap, CTRL_KEY('b'), EDITOR_ACTION_TOGGLE_DRAWER);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('m'), EDITOR_ACTION_MAIN_MENU);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('m'),
	                          EDITOR_ACTION_CONTEXT_MENU);
	(void)keymapAppendBinding(keymap, ALT_SHIFT_ARROW_LEFT, EDITOR_ACTION_COLUMN_SELECT_LEFT);
	(void)keymapAppendBinding(keymap, ALT_SHIFT_ARROW_RIGHT, EDITOR_ACTION_COLUMN_SELECT_RIGHT);
	(void)keymapAppendBinding(keymap, ALT_SHIFT_ARROW_UP, EDITOR_ACTION_COLUMN_SELECT_UP);
	(void)keymapAppendBinding(keymap, ALT_SHIFT_ARROW_DOWN, EDITOR_ACTION_COLUMN_SELECT_DOWN);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('z'),
	                          EDITOR_ACTION_TOGGLE_LINE_WRAP);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('n'),
	                          EDITOR_ACTION_TOGGLE_LINE_NUMBERS);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('h'),
	                          EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT);
	(void)keymapAppendBinding(keymap, CTRL_KEY('p'), EDITOR_ACTION_FIND_FILE);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('f'),
	                          EDITOR_ACTION_PROJECT_SEARCH);
	(void)keymapAppendBinding(keymap, CTRL_KEY('f'), EDITOR_ACTION_FIND);
	(void)keymapAppendBinding(keymap, CTRL_KEY('g'), EDITOR_ACTION_GOTO_LINE);
	(void)keymapAppendBinding(keymap, CTRL_KEY(']'), EDITOR_ACTION_GOTO_MATCHING_BRACKET);
	(void)keymapAppendBinding(keymap, CTRL_KEY('o'), EDITOR_ACTION_GOTO_DEFINITION);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('i'),
	                          EDITOR_ACTION_GOTO_IMPLEMENTATION);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('s'), EDITOR_ACTION_GOTO_SYMBOL);
	(void)keymapAppendBinding(keymap, CTRL_KEY('a'), EDITOR_ACTION_SELECT_ALL);
	(void)keymapAppendBinding(keymap, SHIFT_ARROW_LEFT, EDITOR_ACTION_SELECT_LEFT);
	(void)keymapAppendBinding(keymap, SHIFT_ARROW_RIGHT, EDITOR_ACTION_SELECT_RIGHT);
	(void)keymapAppendBinding(keymap, SHIFT_ARROW_UP, EDITOR_ACTION_SELECT_UP);
	(void)keymapAppendBinding(keymap, SHIFT_ARROW_DOWN, EDITOR_ACTION_SELECT_DOWN);
	(void)keymapAppendBinding(keymap, CTRL_SHIFT_ARROW_LEFT, EDITOR_ACTION_SELECT_WORD_LEFT);
	(void)keymapAppendBinding(keymap, CTRL_SHIFT_ARROW_RIGHT, EDITOR_ACTION_SELECT_WORD_RIGHT);
	(void)keymapAppendBinding(keymap, SHIFT_HOME_KEY, EDITOR_ACTION_SELECT_HOME);
	(void)keymapAppendBinding(keymap, SHIFT_END_KEY, EDITOR_ACTION_SELECT_END);
	(void)keymapAppendBinding(keymap, CTRL_KEY('c'), EDITOR_ACTION_COPY_SELECTION);
	(void)keymapAppendBinding(keymap, CTRL_KEY('x'), EDITOR_ACTION_CUT_SELECTION);
	(void)keymapAppendBinding(keymap, CTRL_KEY('d'), EDITOR_ACTION_DELETE_SELECTION);
	(void)keymapAppendBinding(keymap, CTRL_KEY('v'), EDITOR_ACTION_PASTE);
	(void)keymapAppendBinding(keymap, CTRL_KEY('z'), EDITOR_ACTION_UNDO);
	(void)keymapAppendBinding(keymap, CTRL_KEY('y'), EDITOR_ACTION_REDO);
	(void)keymapAppendBinding(keymap, HOME_KEY, EDITOR_ACTION_MOVE_HOME);
	(void)keymapAppendBinding(keymap, END_KEY, EDITOR_ACTION_MOVE_END);
	(void)keymapAppendBinding(keymap, CTRL_ARROW_LEFT, EDITOR_ACTION_MOVE_WORD_LEFT);
	(void)keymapAppendBinding(keymap, CTRL_ARROW_RIGHT, EDITOR_ACTION_MOVE_WORD_RIGHT);
	(void)keymapAppendBinding(keymap, PAGE_UP, EDITOR_ACTION_PAGE_UP);
	(void)keymapAppendBinding(keymap, PAGE_DOWN, EDITOR_ACTION_PAGE_DOWN);
	(void)keymapAppendBinding(keymap, CTRL_ARROW_UP, EDITOR_ACTION_SCROLL_UP);
	(void)keymapAppendBinding(keymap, CTRL_ARROW_DOWN, EDITOR_ACTION_SCROLL_DOWN);
	(void)keymapAppendBinding(keymap, ARROW_UP, EDITOR_ACTION_MOVE_UP);
	(void)keymapAppendBinding(keymap, ARROW_DOWN, EDITOR_ACTION_MOVE_DOWN);
	(void)keymapAppendBinding(keymap, ARROW_LEFT, EDITOR_ACTION_MOVE_LEFT);
	(void)keymapAppendBinding(keymap, ARROW_RIGHT, EDITOR_ACTION_MOVE_RIGHT);
	(void)keymapAppendBinding(keymap, '\r', EDITOR_ACTION_NEWLINE);
	(void)keymapAppendBinding(keymap, '\x1b', EDITOR_ACTION_ESCAPE);
	(void)keymapAppendBinding(keymap, CTRL_KEY('l'), EDITOR_ACTION_REDRAW);
	(void)keymapAppendBinding(keymap, DEL_KEY, EDITOR_ACTION_DELETE_CHAR);
	(void)keymapAppendBinding(keymap, BACKSPACE, EDITOR_ACTION_BACKSPACE);
	(void)keymapAppendBinding(keymap, CTRL_KEY('h'), EDITOR_ACTION_BACKSPACE);
	(void)keymapAppendBinding(keymap, ALT_ARROW_UP, EDITOR_ACTION_MOVE_LINE_UP);
	(void)keymapAppendBinding(keymap, ALT_ARROW_DOWN, EDITOR_ACTION_MOVE_LINE_DOWN);
	(void)keymapAppendBinding(keymap, EDITOR_ALT_LETTER_KEY('c'), EDITOR_ACTION_TOGGLE_COMMENT);
	(void)keymapAppendBinding(keymap, CTRL_KEY('r'), EDITOR_ACTION_FIND_REPLACE);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('n'),
	                          EDITOR_ACTION_DRAWER_CREATE_FILE);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('d'),
	                          EDITOR_ACTION_DRAWER_CREATE_FOLDER);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('r'),
	                          EDITOR_ACTION_DRAWER_RENAME);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('k'),
	                          EDITOR_ACTION_DRAWER_DELETE);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('g'),
	                          EDITOR_ACTION_GIT_DRAWER);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('l'),
	                          EDITOR_ACTION_LSP_DRAWER);
	(void)keymapAppendBinding(keymap, EDITOR_CTRL_ALT_LETTER_KEY('b'),
	                          EDITOR_ACTION_DAP_DRAWER);
}

int editorKeymapLookupAction(const struct editorKeymap *keymap, int key,
                             enum editorAction *action_out) {
	for (size_t i = 0; i < keymap->len; i++) {
		if (keymap->bindings[i].key == key) {
			*action_out = keymap->bindings[i].action;
			return 1;
		}
	}
	return 0;
}

int editorKeymapFormatBinding(const struct editorKeymap *keymap, enum editorAction action,
                              char *buf, size_t bufsize) {
	for (size_t i = 0; i < keymap->len; i++) {
		if (keymap->bindings[i].action == action) {
			return keymapFormatKey(keymap->bindings[i].key, buf, bufsize);
		}
	}

	if (bufsize != 0) {
		buf[0] = '\0';
	}
	return 0;
}

void editorKeymapBuildHelpStatus(const struct editorKeymap *keymap, char *buf, size_t bufsize) {
	char save[24];
	char quit[24];
	char new_tab[24];
	char close_tab[24];
	char next_tab[24];
	char prev_tab[24];
	char focus_drawer[24];
	char find_file[24];
	char project_search[24];
	char find[24];
	char go_to[24];
	char select[24];
	char copy[24];
	char cut[24];
	char delete_sel[24];
	char paste[24];
	char undo[24];
	char redo[24];

	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_SAVE, save, sizeof(save))) {
		(void)snprintf(save, sizeof(save), "Save");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_QUIT, quit, sizeof(quit))) {
		(void)snprintf(quit, sizeof(quit), "Quit");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_NEW_TAB, new_tab, sizeof(new_tab))) {
		(void)snprintf(new_tab, sizeof(new_tab), "NewTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_CLOSE_TAB, close_tab,
	                               sizeof(close_tab))) {
		(void)snprintf(close_tab, sizeof(close_tab), "CloseTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_NEXT_TAB, next_tab,
	                               sizeof(next_tab))) {
		(void)snprintf(next_tab, sizeof(next_tab), "NextTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_PREV_TAB, prev_tab,
	                               sizeof(prev_tab))) {
		(void)snprintf(prev_tab, sizeof(prev_tab), "PrevTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_FOCUS_DRAWER, focus_drawer,
	                               sizeof(focus_drawer))) {
		(void)snprintf(focus_drawer, sizeof(focus_drawer), "Drawer");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_FIND_FILE, find_file,
	                               sizeof(find_file))) {
		(void)snprintf(find_file, sizeof(find_file), "File");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_PROJECT_SEARCH, project_search,
	                               sizeof(project_search))) {
		(void)snprintf(project_search, sizeof(project_search), "Text");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_FIND, find, sizeof(find))) {
		(void)snprintf(find, sizeof(find), "Find");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_GOTO_LINE, go_to, sizeof(go_to))) {
		(void)snprintf(go_to, sizeof(go_to), "Goto");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_TOGGLE_SELECTION, select,
	                               sizeof(select))) {
		(void)snprintf(select, sizeof(select), "Select");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_COPY_SELECTION, copy, sizeof(copy))) {
		(void)snprintf(copy, sizeof(copy), "Copy");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_CUT_SELECTION, cut, sizeof(cut))) {
		(void)snprintf(cut, sizeof(cut), "Cut");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_DELETE_SELECTION, delete_sel,
	                               sizeof(delete_sel))) {
		(void)snprintf(delete_sel, sizeof(delete_sel), "Del");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_PASTE, paste, sizeof(paste))) {
		(void)snprintf(paste, sizeof(paste), "Paste");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_UNDO, undo, sizeof(undo))) {
		(void)snprintf(undo, sizeof(undo), "Undo");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_REDO, redo, sizeof(redo))) {
		(void)snprintf(redo, sizeof(redo), "Redo");
	}

	(void)snprintf(
	        buf, bufsize,
	        "Help: %s save; %s quit; %s new; %s close; %s/%s tabs; %s drawer; %s file; %s "
	        "text; %s find; %s goto",
	        save, quit, new_tab, close_tab, prev_tab, next_tab, focus_drawer, find_file,
	        project_search, find, go_to);
}

enum editorKeymapLoadStatus editorKeymapLoadFromPaths(struct editorKeymap *keymap,
                                                      const char *global_path,
                                                      const char *project_path) {
	if (keymap == NULL) {
		return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
	}

	editorKeymapInitDefaults(keymap);
	enum editorKeymapLoadStatus status = EDITOR_KEYMAP_LOAD_OK;

	if (global_path != NULL) {
		enum keymapFileStatus global_status = keymapApplyConfigFile(keymap, global_path);
		if (global_status == KEYMAP_FILE_OUT_OF_MEMORY) {
			editorKeymapInitDefaults(keymap);
			return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == KEYMAP_FILE_INVALID) {
			editorKeymapInitDefaults(keymap);
			status = EDITOR_KEYMAP_LOAD_INVALID_GLOBAL;
		}
	}

	if (project_path != NULL) {
		enum keymapFileStatus project_status = keymapApplyConfigFile(keymap, project_path);
		if (project_status == KEYMAP_FILE_OUT_OF_MEMORY) {
			editorKeymapInitDefaults(keymap);
			return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == KEYMAP_FILE_INVALID) {
			editorKeymapInitDefaults(keymap);
			return EDITOR_KEYMAP_LOAD_INVALID_PROJECT;
		}
	}

	return status;
}

enum editorKeymapLoadStatus editorKeymapLoadConfigured(struct editorKeymap *keymap) {
	if (keymap == NULL) {
		return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorKeymapLoadFromPaths(keymap, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorKeymapInitDefaults(keymap);
		return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
	}

	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(keymap, global_path, NULL);
	free(global_path);
	return status;
}
