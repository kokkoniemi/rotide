#include "config/keymap.h"

#include "config/common.h"
#include "input/input_system.h"
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
	const char *display;
	int key;
};

struct keymapArrowKey {
	const char *name;
	const char *display;
	int key;
};

enum keymapModifierFlags {
	KEYMAP_MOD_NONE = 0,
	KEYMAP_MOD_CTRL = 1 << 0,
	KEYMAP_MOD_ALT = 1 << 1,
	KEYMAP_MOD_SHIFT = 1 << 2
};

struct keymapArrowModifierKeys {
	int modifiers;
	const char *display_prefix;
	int left;
	int right;
	int down;
	int up;
};

struct keymapModifiedNamedKey {
	int modifiers;
	const char *name;
	const char *display;
	int key;
};

struct keymapHelpStatusEntry {
	enum editorAction action;
	const char *fallback;
};

struct keymapConfigContext {
	struct editorKeymap *keymap;
	const char *table;
};

enum keymapFileStatus {
	KEYMAP_FILE_APPLIED = 0,
	KEYMAP_FILE_MISSING,
	KEYMAP_FILE_INVALID,
	KEYMAP_FILE_OUT_OF_MEMORY
};

#define KEYMAP_KEY_SPEC_MAX 64
#define KEYMAP_HELP_STATUS_ITEM_MAX 24

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
        {"goto_references", EDITOR_ACTION_GOTO_REFERENCES},
        {"goto_symbol", EDITOR_ACTION_GOTO_SYMBOL},
        {"diagnostic_next", EDITOR_ACTION_DIAGNOSTIC_NEXT},
        {"diagnostic_prev", EDITOR_ACTION_DIAGNOSTIC_PREV},
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
        {"dap_restart", EDITOR_ACTION_DAP_RESTART},
        {"dap_evaluate", EDITOR_ACTION_DAP_EVALUATE},
        {"dap_console", EDITOR_ACTION_DAP_CONSOLE},
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
        {"home", "Home", HOME_KEY},
        {"end", "End", END_KEY},
        {"page_up", "PageUp", PAGE_UP},
        {"page_down", "PageDown", PAGE_DOWN},
        {"enter", "Enter", '\r'},
        {"esc", "Esc", '\x1b'},
        {"backspace", "Backspace", BACKSPACE},
        {"del", "Del", DEL_KEY},
};

static const struct keymapArrowKey g_keymap_arrow_keys[] = {
        {"left", "Left", ARROW_LEFT},
        {"right", "Right", ARROW_RIGHT},
        {"up", "Up", ARROW_UP},
        {"down", "Down", ARROW_DOWN},
};

static const struct keymapArrowModifierKeys g_keymap_arrow_modifier_keys[] = {
        {KEYMAP_MOD_SHIFT, "Shift", SHIFT_ARROW_LEFT, SHIFT_ARROW_RIGHT, SHIFT_ARROW_DOWN,
         SHIFT_ARROW_UP},
        {KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, "Ctrl-Shift", CTRL_SHIFT_ARROW_LEFT,
         CTRL_SHIFT_ARROW_RIGHT, CTRL_SHIFT_ARROW_DOWN, CTRL_SHIFT_ARROW_UP},
        {KEYMAP_MOD_ALT, "Alt", ALT_ARROW_LEFT, ALT_ARROW_RIGHT, ALT_ARROW_DOWN, ALT_ARROW_UP},
        {KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, "Alt-Shift", ALT_SHIFT_ARROW_LEFT,
         ALT_SHIFT_ARROW_RIGHT, ALT_SHIFT_ARROW_DOWN, ALT_SHIFT_ARROW_UP},
        {KEYMAP_MOD_CTRL, "Ctrl", CTRL_ARROW_LEFT, CTRL_ARROW_RIGHT, CTRL_ARROW_DOWN,
         CTRL_ARROW_UP},
        {KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT, "Ctrl-Alt", CTRL_ALT_ARROW_LEFT, CTRL_ALT_ARROW_RIGHT,
         CTRL_ALT_ARROW_DOWN, CTRL_ALT_ARROW_UP},
        {KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT | KEYMAP_MOD_ALT, "Ctrl-Shift-Alt",
         CTRL_SHIFT_ALT_ARROW_LEFT, CTRL_SHIFT_ALT_ARROW_RIGHT, CTRL_SHIFT_ALT_ARROW_DOWN,
         CTRL_SHIFT_ALT_ARROW_UP},
};

static const struct keymapModifiedNamedKey g_keymap_modified_named_keys[] = {
        {KEYMAP_MOD_SHIFT, "home", "Shift-Home", SHIFT_HOME_KEY},
        {KEYMAP_MOD_SHIFT, "end", "Shift-End", SHIFT_END_KEY},
};

static const struct keymapHelpStatusEntry g_keymap_help_status_entries[] = {
        {EDITOR_ACTION_SAVE, "Save"},           {EDITOR_ACTION_QUIT, "Quit"},
        {EDITOR_ACTION_NEW_TAB, "NewTab"},      {EDITOR_ACTION_CLOSE_TAB, "CloseTab"},
        {EDITOR_ACTION_PREV_TAB, "PrevTab"},    {EDITOR_ACTION_NEXT_TAB, "NextTab"},
        {EDITOR_ACTION_FOCUS_DRAWER, "Drawer"}, {EDITOR_ACTION_FIND_FILE, "File"},
        {EDITOR_ACTION_PROJECT_SEARCH, "Text"}, {EDITOR_ACTION_FIND, "Find"},
        {EDITOR_ACTION_GOTO_LINE, "Goto"},
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

int editorKeymapResolveActionName(const char *name, enum editorAction *action_out) {
	return keymapResolveActionName(name, action_out);
}

int editorKeymapBindAction(struct editorKeymap *keymap, enum editorAction action, int key) {
	return keymapSetActionBinding(keymap, action, key);
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
	for (size_t i = 0; i < sizeof(g_keymap_arrow_keys) / sizeof(g_keymap_arrow_keys[0]); i++) {
		if (strcmp(g_keymap_arrow_keys[i].name, token) == 0) {
			*arrow_out = g_keymap_arrow_keys[i].key;
			return 1;
		}
	}
	return 0;
}

static int keymapModifiedArrowKey(const struct keymapArrowModifierKeys *entry, int arrow,
                                  int *key_out) {
	switch (arrow) {
		case ARROW_LEFT:
			*key_out = entry->left;
			return 1;
		case ARROW_RIGHT:
			*key_out = entry->right;
			return 1;
		case ARROW_DOWN:
			*key_out = entry->down;
			return 1;
		case ARROW_UP:
			*key_out = entry->up;
			return 1;
		default:
			return 0;
	}
}

static int keymapArrowWithModifiers(int arrow, int modifiers, int *key_out) {
	for (size_t i = 0;
	     i < sizeof(g_keymap_arrow_modifier_keys) / sizeof(g_keymap_arrow_modifier_keys[0]);
	     i++) {
		if (g_keymap_arrow_modifier_keys[i].modifiers == modifiers) {
			return keymapModifiedArrowKey(&g_keymap_arrow_modifier_keys[i], arrow,
			                              key_out);
		}
	}
	return 0;
}

static int keymapParseNamedKeyToken(const char *token, int *key_out) {
	if (keymapParseArrowToken(token, key_out)) {
		return 1;
	}
	for (size_t i = 0; i < sizeof(g_keymap_named_keys) / sizeof(g_keymap_named_keys[0]); i++) {
		if (strcmp(g_keymap_named_keys[i].name, token) == 0) {
			*key_out = g_keymap_named_keys[i].key;
			return 1;
		}
	}
	return 0;
}

static int keymapNamedKeyWithModifiers(const char *token, int modifiers, int *key_out) {
	for (size_t i = 0;
	     i < sizeof(g_keymap_modified_named_keys) / sizeof(g_keymap_modified_named_keys[0]);
	     i++) {
		if (g_keymap_modified_named_keys[i].modifiers == modifiers &&
		    strcmp(g_keymap_modified_named_keys[i].name, token) == 0) {
			*key_out = g_keymap_modified_named_keys[i].key;
			return 1;
		}
	}
	return 0;
}

static int keymapParseKeySpec(const char *spec, int *key_out) {
	if (keymapParseCtrlKeySpec(spec, key_out)) {
		return 1;
	}

	char normalized[KEYMAP_KEY_SPEC_MAX];
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
		return keymapParseNamedKeyToken(key_token, key_out);
	}

	if (keymapNamedKeyWithModifiers(key_token, modifiers, key_out)) {
		return 1;
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
	struct keymapConfigContext *config = ctx;
	return config != NULL && config->table != NULL && strcmp(table, config->table) == 0;
}

static int keymapConfigOnEntry(void *ctx, const char *key, char *value) {
	struct keymapConfigContext *config = ctx;
	struct editorKeymap *keymap = config->keymap;

	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!keymapResolveActionName(key, &action)) {
		return 0;
	}

	char key_spec[KEYMAP_KEY_SPEC_MAX];
	if (!editorConfigParseQuotedValue(value, key_spec, sizeof(key_spec))) {
		return 0;
	}

	int parsed_key = 0;
	if (!keymapParseKeySpec(key_spec, &parsed_key)) {
		return 0;
	}

	return keymapSetActionBinding(keymap, action, parsed_key);
}

static enum keymapFileStatus keymapApplyConfigTable(struct editorKeymap *keymap, const char *path,
                                                    const char *table) {
	struct editorKeymap updated = *keymap;
	struct keymapConfigContext ctx = {
	        .keymap = &updated,
	        .table = table,
	};
	struct editorConfigScanner scanner = {keymapConfigOnSection, keymapConfigOnEntry};

	switch (editorConfigScanFile(path, &scanner, &ctx)) {
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

/* CUA bindings live in [keymap.cua]. There is no bare [keymap] alias: the CUA
 * and Vim systems are configured symmetrically under [keymap.cua] / [keymap.vim]. */
static enum keymapFileStatus keymapApplyConfigFile(struct editorKeymap *keymap, const char *path) {
	return keymapApplyConfigTable(keymap, path, "keymap.cua");
}

struct keymapVimContext {
	const struct editorInputSystem *system;
};

static int keymapVimOnSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "keymap.vim") == 0;
}

/* Vim bindings are usually single, case-sensitive printable characters (`h`,
 * `V`, `$`, `;`), which keymapParseKeySpec rejects and case-folds. Accept those
 * verbatim and fall back to the shared parser for named keys (`esc`, `ctrl+c`). */
static int keymapVimParseKeySpec(const char *spec, int *key_out) {
	if (spec[0] != '\0' && spec[1] == '\0') {
		unsigned char ch = (unsigned char)spec[0];
		if (isprint(ch)) {
			*key_out = (int)ch;
			return 1;
		}
	}
	if (strcmp(spec, "space") == 0) {
		*key_out = ' ';
		return 1;
	}
	return keymapParseKeySpec(spec, key_out);
}

static int keymapVimOnEntry(void *ctx, const char *key, char *value) {
	struct keymapVimContext *config = ctx;
	const char *dot = strchr(key, '.');
	char mode[16];
	char key_spec[KEYMAP_KEY_SPEC_MAX];
	int parsed_key = 0;
	size_t mode_len = 0;

	if (dot == NULL) {
		return 0;
	}
	mode_len = (size_t)(dot - key);
	if (mode_len == 0 || mode_len >= sizeof(mode)) {
		return 0;
	}
	memcpy(mode, key, mode_len);
	mode[mode_len] = '\0';
	if (dot[1] == '\0') {
		return 0;
	}
	if (!editorConfigParseQuotedValue(value, key_spec, sizeof(key_spec))) {
		return 0;
	}
	if (!keymapVimParseKeySpec(key_spec, &parsed_key)) {
		return 0;
	}
	if (config->system == NULL || config->system->bind_key == NULL) {
		return 0;
	}
	return config->system->bind_key(mode, dot + 1, parsed_key);
}

static enum keymapFileStatus keymapApplyVimConfigFile(const struct editorInputSystem *system,
                                                      const char *path) {
	struct keymapVimContext ctx = {.system = system};
	struct editorConfigScanner scanner = {keymapVimOnSection, keymapVimOnEntry};

	switch (editorConfigScanFile(path, &scanner, &ctx)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return KEYMAP_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			return KEYMAP_FILE_APPLIED;
		case EDITOR_CONFIG_SCAN_MALFORMED:
		default:
			return KEYMAP_FILE_INVALID;
	}
}

enum editorKeymapLoadStatus editorKeymapLoadVimBindings(const char *global_path,
                                                        const char *project_path) {
	const struct editorInputSystem *system = editorInputSystemActive();
	enum editorKeymapLoadStatus status = EDITOR_KEYMAP_LOAD_OK;

	if (system == NULL || system->id == NULL || strcmp(system->id, "vim") != 0) {
		return EDITOR_KEYMAP_LOAD_OK;
	}
	editorVimKeymapResetDefaults();

	if (global_path != NULL &&
	    keymapApplyVimConfigFile(system, global_path) == KEYMAP_FILE_INVALID) {
		editorVimKeymapResetDefaults();
		status = EDITOR_KEYMAP_LOAD_INVALID_GLOBAL;
	}
	if (project_path != NULL &&
	    keymapApplyVimConfigFile(system, project_path) == KEYMAP_FILE_INVALID) {
		editorVimKeymapResetDefaults();
		return EDITOR_KEYMAP_LOAD_INVALID_PROJECT;
	}
	return status;
}

enum editorKeymapLoadStatus editorKeymapLoadVimBindingsConfigured(void) {
	char project_path[PATH_MAX];
	char *global_path = NULL;
	enum editorKeymapLoadStatus status = EDITOR_KEYMAP_LOAD_OK;

	if (!editorConfigBuildProjectConfigPath(NULL, project_path, sizeof(project_path))) {
		return EDITOR_KEYMAP_LOAD_OK;
	}
	global_path = editorConfigBuildGlobalConfigPath();
	status = editorKeymapLoadVimBindings(global_path, project_path);
	free(global_path);
	return status;
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

	for (size_t i = 0;
	     i < sizeof(g_keymap_arrow_modifier_keys) / sizeof(g_keymap_arrow_modifier_keys[0]);
	     i++) {
		const struct keymapArrowModifierKeys *modifier = &g_keymap_arrow_modifier_keys[i];
		for (size_t j = 0; j < sizeof(g_keymap_arrow_keys) / sizeof(g_keymap_arrow_keys[0]);
		     j++) {
			int modified_key = 0;
			if (keymapModifiedArrowKey(modifier, g_keymap_arrow_keys[j].key,
			                           &modified_key) &&
			    modified_key == key) {
				return snprintf(buf, bufsize, "%s-%s", modifier->display_prefix,
				                g_keymap_arrow_keys[j].display) > 0;
			}
		}
	}
	for (size_t i = 0; i < sizeof(g_keymap_arrow_keys) / sizeof(g_keymap_arrow_keys[0]); i++) {
		if (g_keymap_arrow_keys[i].key == key) {
			return snprintf(buf, bufsize, "%s", g_keymap_arrow_keys[i].display) > 0;
		}
	}
	for (size_t i = 0; i < sizeof(g_keymap_named_keys) / sizeof(g_keymap_named_keys[0]); i++) {
		if (g_keymap_named_keys[i].key == key) {
			return snprintf(buf, bufsize, "%s", g_keymap_named_keys[i].display) > 0;
		}
	}
	for (size_t i = 0;
	     i < sizeof(g_keymap_modified_named_keys) / sizeof(g_keymap_modified_named_keys[0]);
	     i++) {
		if (g_keymap_modified_named_keys[i].key == key) {
			return snprintf(buf, bufsize, "%s",
			                g_keymap_modified_named_keys[i].display) > 0;
		}
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

static void keymapFormatHelpStatusEntry(const struct editorKeymap *keymap,
                                        const struct keymapHelpStatusEntry *entry, char *buf,
                                        size_t bufsize) {
	if (!editorKeymapFormatBinding(keymap, entry->action, buf, bufsize)) {
		(void)snprintf(buf, bufsize, "%s", entry->fallback);
	}
}

void editorKeymapBuildHelpStatus(const struct editorKeymap *keymap, char *buf, size_t bufsize) {
	char slots[sizeof(g_keymap_help_status_entries) / sizeof(g_keymap_help_status_entries[0])]
	          [KEYMAP_HELP_STATUS_ITEM_MAX];
	for (size_t i = 0;
	     i < sizeof(g_keymap_help_status_entries) / sizeof(g_keymap_help_status_entries[0]);
	     i++) {
		keymapFormatHelpStatusEntry(keymap, &g_keymap_help_status_entries[i], slots[i],
		                            sizeof(slots[i]));
	}

	(void)snprintf(
	        buf, bufsize,
	        "Help: %s save; %s quit; %s new; %s close; %s/%s tabs; %s drawer; %s file; %s "
	        "text; %s find; %s goto",
	        slots[0], slots[1], slots[2], slots[3], slots[4], slots[5], slots[6], slots[7],
	        slots[8], slots[9], slots[10]);
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

	char project_path[PATH_MAX];
	if (!editorConfigBuildProjectConfigPath(NULL, project_path, sizeof(project_path))) {
		editorKeymapInitDefaults(keymap);
		return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	enum editorKeymapLoadStatus status =
	        editorKeymapLoadFromPaths(keymap, global_path, project_path);
	free(global_path);
	return status;
}
