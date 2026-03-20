#include "keymap.h"

#include "alloc.h"
#include "size_utils.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

struct editorActionName {
	const char *name;
	enum editorAction action;
};

struct editorNamedKey {
	const char *name;
	int key;
};

enum editorKeymapFileStatus {
	EDITOR_KEYMAP_FILE_APPLIED = 0,
	EDITOR_KEYMAP_FILE_MISSING,
	EDITOR_KEYMAP_FILE_INVALID,
	EDITOR_KEYMAP_FILE_OUT_OF_MEMORY
};

enum editorCursorStyleFileStatus {
	EDITOR_CURSOR_STYLE_FILE_APPLIED = 0,
	EDITOR_CURSOR_STYLE_FILE_MISSING,
	EDITOR_CURSOR_STYLE_FILE_INVALID,
	EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY
};

enum editorSyntaxThemeFileStatus {
	EDITOR_SYNTAX_THEME_FILE_APPLIED = 0,
	EDITOR_SYNTAX_THEME_FILE_MISSING,
	EDITOR_SYNTAX_THEME_FILE_OUT_OF_MEMORY
};

enum editorLspConfigFileStatus {
	EDITOR_LSP_CONFIG_FILE_APPLIED = 0,
	EDITOR_LSP_CONFIG_FILE_MISSING,
	EDITOR_LSP_CONFIG_FILE_INVALID,
	EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY
};

static const struct editorActionName editor_action_names[] = {
	{"quit", EDITOR_ACTION_QUIT},
	{"save", EDITOR_ACTION_SAVE},
	{"new_tab", EDITOR_ACTION_NEW_TAB},
	{"close_tab", EDITOR_ACTION_CLOSE_TAB},
	{"next_tab", EDITOR_ACTION_NEXT_TAB},
	{"prev_tab", EDITOR_ACTION_PREV_TAB},
	{"focus_drawer", EDITOR_ACTION_FOCUS_DRAWER},
	{"resize_drawer_narrow", EDITOR_ACTION_RESIZE_DRAWER_NARROW},
	{"resize_drawer_widen", EDITOR_ACTION_RESIZE_DRAWER_WIDEN},
	{"find", EDITOR_ACTION_FIND},
	{"goto_line", EDITOR_ACTION_GOTO_LINE},
	{"goto_definition", EDITOR_ACTION_GOTO_DEFINITION},
	{"toggle_selection", EDITOR_ACTION_TOGGLE_SELECTION},
	{"copy_selection", EDITOR_ACTION_COPY_SELECTION},
	{"cut_selection", EDITOR_ACTION_CUT_SELECTION},
	{"delete_selection", EDITOR_ACTION_DELETE_SELECTION},
	{"paste", EDITOR_ACTION_PASTE},
	{"undo", EDITOR_ACTION_UNDO},
	{"redo", EDITOR_ACTION_REDO},
	{"move_home", EDITOR_ACTION_MOVE_HOME},
	{"move_end", EDITOR_ACTION_MOVE_END},
	{"page_up", EDITOR_ACTION_PAGE_UP},
	{"page_down", EDITOR_ACTION_PAGE_DOWN},
	{"scroll_left", EDITOR_ACTION_SCROLL_LEFT},
	{"scroll_right", EDITOR_ACTION_SCROLL_RIGHT},
	{"move_up", EDITOR_ACTION_MOVE_UP},
	{"move_down", EDITOR_ACTION_MOVE_DOWN},
	{"move_left", EDITOR_ACTION_MOVE_LEFT},
	{"move_right", EDITOR_ACTION_MOVE_RIGHT},
	{"newline", EDITOR_ACTION_NEWLINE},
	{"escape", EDITOR_ACTION_ESCAPE},
	{"redraw", EDITOR_ACTION_REDRAW},
	{"delete_char", EDITOR_ACTION_DELETE_CHAR},
	{"backspace", EDITOR_ACTION_BACKSPACE},
};

static const struct editorNamedKey editor_named_keys[] = {
	{"left", ARROW_LEFT},
	{"right", ARROW_RIGHT},
	{"up", ARROW_UP},
	{"down", ARROW_DOWN},
	{"home", HOME_KEY},
	{"end", END_KEY},
	{"page_up", PAGE_UP},
	{"page_down", PAGE_DOWN},
	{"enter", '\r'},
	{"esc", '\x1b'},
	{"backspace", BACKSPACE},
	{"del", DEL_KEY},
};

static char *editorTrimLeft(char *s) {
	while (*s != '\0' && isspace((unsigned char)*s)) {
		s++;
	}
	return s;
}

static void editorTrimRight(char *s) {
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1])) {
		len--;
	}
	s[len] = '\0';
}

static void editorStripInlineComment(char *line) {
	int in_quote = 0;

	for (size_t i = 0; line[i] != '\0'; i++) {
		if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
			in_quote = !in_quote;
			continue;
		}
		if (!in_quote && line[i] == '#') {
			line[i] = '\0';
			break;
		}
	}
}

static int editorKeymapHasBindingForKey(const struct editorKeymap *keymap, int key) {
	for (size_t i = 0; i < keymap->len; i++) {
		if (keymap->bindings[i].key == key) {
			return 1;
		}
	}
	return 0;
}

static void editorKeymapRemoveActionBindings(struct editorKeymap *keymap, enum editorAction action) {
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

static int editorKeymapAppendBinding(struct editorKeymap *keymap, int key, enum editorAction action) {
	if (keymap->len >= ROTIDE_KEYMAP_MAX_BINDINGS) {
		return 0;
	}
	if (editorKeymapHasBindingForKey(keymap, key)) {
		return 0;
	}

	keymap->bindings[keymap->len].key = key;
	keymap->bindings[keymap->len].action = action;
	keymap->len++;
	return 1;
}

static int editorKeymapSetActionBinding(struct editorKeymap *keymap, enum editorAction action, int key) {
	struct editorKeymap updated = *keymap;

	editorKeymapRemoveActionBindings(&updated, action);
	if (!editorKeymapAppendBinding(&updated, key, action)) {
		return 0;
	}

	*keymap = updated;
	return 1;
}

static int editorKeymapResolveActionName(const char *name, enum editorAction *action_out) {
	for (size_t i = 0; i < sizeof(editor_action_names) / sizeof(editor_action_names[0]); i++) {
		if (strcmp(editor_action_names[i].name, name) == 0) {
			*action_out = editor_action_names[i].action;
			return 1;
		}
	}
	return 0;
}

static int editorKeymapParseCtrlKeySpec(const char *spec, int *key_out) {
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

enum editorKeymapModifierFlags {
	EDITOR_KEYMAP_MOD_NONE = 0,
	EDITOR_KEYMAP_MOD_CTRL = 1 << 0,
	EDITOR_KEYMAP_MOD_ALT = 1 << 1,
	EDITOR_KEYMAP_MOD_SHIFT = 1 << 2
};

static int editorKeymapParseLetterToken(const char *token, char *letter_out) {
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

static int editorKeymapParseArrowToken(const char *token, int *arrow_out) {
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

static int editorKeymapArrowWithModifiers(int arrow, int modifiers, int *key_out) {
	switch (modifiers) {
		case EDITOR_KEYMAP_MOD_ALT:
			switch (arrow) {
				case ARROW_LEFT:
					*key_out = ALT_ARROW_LEFT;
					return 1;
				case ARROW_RIGHT:
					*key_out = ALT_ARROW_RIGHT;
					return 1;
				case ARROW_DOWN:
					*key_out = ALT_ARROW_DOWN;
					return 1;
				case ARROW_UP:
					*key_out = ALT_ARROW_UP;
					return 1;
				default:
					return 0;
			}
		case EDITOR_KEYMAP_MOD_ALT | EDITOR_KEYMAP_MOD_SHIFT:
			switch (arrow) {
				case ARROW_LEFT:
					*key_out = ALT_SHIFT_ARROW_LEFT;
					return 1;
				case ARROW_RIGHT:
					*key_out = ALT_SHIFT_ARROW_RIGHT;
					return 1;
				case ARROW_DOWN:
					*key_out = ALT_SHIFT_ARROW_DOWN;
					return 1;
				case ARROW_UP:
					*key_out = ALT_SHIFT_ARROW_UP;
					return 1;
				default:
					return 0;
			}
		case EDITOR_KEYMAP_MOD_CTRL:
			switch (arrow) {
				case ARROW_LEFT:
					*key_out = CTRL_ARROW_LEFT;
					return 1;
				case ARROW_RIGHT:
					*key_out = CTRL_ARROW_RIGHT;
					return 1;
				case ARROW_DOWN:
					*key_out = CTRL_ARROW_DOWN;
					return 1;
				case ARROW_UP:
					*key_out = CTRL_ARROW_UP;
					return 1;
				default:
					return 0;
			}
		case EDITOR_KEYMAP_MOD_CTRL | EDITOR_KEYMAP_MOD_ALT:
			switch (arrow) {
				case ARROW_LEFT:
					*key_out = CTRL_ALT_ARROW_LEFT;
					return 1;
				case ARROW_RIGHT:
					*key_out = CTRL_ALT_ARROW_RIGHT;
					return 1;
				case ARROW_DOWN:
					*key_out = CTRL_ALT_ARROW_DOWN;
					return 1;
				case ARROW_UP:
					*key_out = CTRL_ALT_ARROW_UP;
					return 1;
				default:
					return 0;
			}
		default:
			return 0;
	}
}

static int editorKeymapParseKeySpec(const char *spec, int *key_out) {
	if (editorKeymapParseCtrlKeySpec(spec, key_out)) {
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

	int modifiers = EDITOR_KEYMAP_MOD_NONE;
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
			if (modifiers & EDITOR_KEYMAP_MOD_CTRL) {
				return 0;
			}
			modifiers |= EDITOR_KEYMAP_MOD_CTRL;
		} else if (strcmp(cursor, "alt") == 0) {
			if (modifiers & EDITOR_KEYMAP_MOD_ALT) {
				return 0;
			}
			modifiers |= EDITOR_KEYMAP_MOD_ALT;
		} else if (strcmp(cursor, "shift") == 0) {
			if (modifiers & EDITOR_KEYMAP_MOD_SHIFT) {
				return 0;
			}
			modifiers |= EDITOR_KEYMAP_MOD_SHIFT;
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

	if (modifiers == EDITOR_KEYMAP_MOD_NONE) {
		for (size_t i = 0; i < sizeof(editor_named_keys) / sizeof(editor_named_keys[0]); i++) {
			if (strcmp(editor_named_keys[i].name, key_token) == 0) {
				*key_out = editor_named_keys[i].key;
				return 1;
			}
		}
		return 0;
	}

	char letter = '\0';
	if (editorKeymapParseLetterToken(key_token, &letter)) {
		if (modifiers == EDITOR_KEYMAP_MOD_CTRL) {
			*key_out = CTRL_KEY((int)letter);
			return 1;
		}
		if (modifiers == EDITOR_KEYMAP_MOD_ALT) {
			*key_out = EDITOR_ALT_LETTER_KEY(letter);
			return 1;
		}
		if (modifiers == (EDITOR_KEYMAP_MOD_CTRL | EDITOR_KEYMAP_MOD_ALT)) {
			*key_out = EDITOR_CTRL_ALT_LETTER_KEY(letter);
			return 1;
		}
		return 0;
	}

	int arrow = 0;
	if (editorKeymapParseArrowToken(key_token, &arrow)) {
		return editorKeymapArrowWithModifiers(arrow, modifiers, key_out);
	}

	return 0;
}

static int editorKeymapParseQuotedValue(const char *value, char *buf, size_t bufsize) {
	if (bufsize == 0 || value[0] != '"') {
		return 0;
	}

	size_t write_idx = 0;
	size_t i = 1;
	while (value[i] != '\0') {
		char ch = value[i];
		if (ch == '"') {
			i++;
			break;
		}

		if (ch == '\\') {
			i++;
			if (value[i] == '\0') {
				return 0;
			}
			ch = value[i];
			if (ch != '"' && ch != '\\') {
				return 0;
			}
		}

		if (write_idx + 1 >= bufsize) {
			return 0;
		}
		buf[write_idx++] = ch;
		i++;
	}

	if (value[i - 1] != '"') {
		return 0;
	}

	buf[write_idx] = '\0';
	const char *tail = editorTrimLeft((char *)&value[i]);
	return tail[0] == '\0';
}

static enum editorKeymapFileStatus editorKeymapApplyConfigFile(struct editorKeymap *keymap,
		const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_KEYMAP_FILE_MISSING;
		}
		return EDITOR_KEYMAP_FILE_INVALID;
	}

	struct editorKeymap updated = *keymap;
	int in_keymap_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}

		editorStripInlineComment(line);
		editorTrimRight(line);
		char *trimmed = editorTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}

		if (trimmed[0] == '[') {
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				fclose(fp);
				return EDITOR_KEYMAP_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorTrimLeft(trimmed + 1);
			editorTrimRight(table);
			char *tail = editorTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_KEYMAP_FILE_INVALID;
			}

			in_keymap_table = strcmp(table, "keymap") == 0;
			continue;
		}

		if (!in_keymap_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}

		*eq = '\0';
		char *action_name = editorTrimLeft(trimmed);
		editorTrimRight(action_name);
		char *value = editorTrimLeft(eq + 1);
		if (action_name[0] == '\0') {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}

		enum editorAction action = EDITOR_ACTION_COUNT;
		if (!editorKeymapResolveActionName(action_name, &action)) {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}

		char key_spec[64];
		if (!editorKeymapParseQuotedValue(value, key_spec, sizeof(key_spec))) {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}

		int key = 0;
		if (!editorKeymapParseKeySpec(key_spec, &key)) {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}

		if (!editorKeymapSetActionBinding(&updated, action, key)) {
			fclose(fp);
			return EDITOR_KEYMAP_FILE_INVALID;
		}
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_KEYMAP_FILE_INVALID;
	}

	fclose(fp);
	*keymap = updated;
	return EDITOR_KEYMAP_FILE_APPLIED;
}

static int editorParseCursorStyleValue(const char *value, enum editorCursorStyle *style_out) {
	if (strcasecmp(value, "block") == 0) {
		*style_out = EDITOR_CURSOR_STYLE_BLOCK;
		return 1;
	}
	if (strcasecmp(value, "bar") == 0) {
		*style_out = EDITOR_CURSOR_STYLE_BAR;
		return 1;
	}
	if (strcasecmp(value, "underline") == 0) {
		*style_out = EDITOR_CURSOR_STYLE_UNDERLINE;
		return 1;
	}
	return 0;
}

static enum editorCursorStyleFileStatus editorCursorStyleApplyConfigFile(
		enum editorCursorStyle *style_in_out, const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_CURSOR_STYLE_FILE_MISSING;
		}
		return EDITOR_CURSOR_STYLE_FILE_INVALID;
	}

	enum editorCursorStyle updated = *style_in_out;
	int in_editor_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}

		editorStripInlineComment(line);
		editorTrimRight(line);
		char *trimmed = editorTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}

		if (trimmed[0] == '[') {
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				fclose(fp);
				return EDITOR_CURSOR_STYLE_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorTrimLeft(trimmed + 1);
			editorTrimRight(table);
			char *tail = editorTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_CURSOR_STYLE_FILE_INVALID;
			}

			in_editor_table = strcmp(table, "editor") == 0;
			continue;
		}

		if (!in_editor_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorTrimLeft(trimmed);
		editorTrimRight(setting_name);
		char *value = editorTrimLeft(eq + 1);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}
		if (strcmp(setting_name, "cursor_style") != 0) {
			continue;
		}

		char cursor_style_value[32];
		if (!editorKeymapParseQuotedValue(value, cursor_style_value, sizeof(cursor_style_value))) {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}

		enum editorCursorStyle parsed = EDITOR_CURSOR_STYLE_BAR;
		if (!editorParseCursorStyleValue(cursor_style_value, &parsed)) {
			fclose(fp);
			return EDITOR_CURSOR_STYLE_FILE_INVALID;
		}
		updated = parsed;
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_CURSOR_STYLE_FILE_INVALID;
	}

	fclose(fp);
	*style_in_out = updated;
	return EDITOR_CURSOR_STYLE_FILE_APPLIED;
}

void editorSyntaxThemeInitDefaults(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]) {
	if (theme_out == NULL) {
		return;
	}

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme_out[i] = EDITOR_THEME_COLOR_DEFAULT;
	}

	theme_out[EDITOR_SYNTAX_HL_COMMENT] = EDITOR_THEME_COLOR_BRIGHT_BLACK;
	theme_out[EDITOR_SYNTAX_HL_KEYWORD] = EDITOR_THEME_COLOR_BRIGHT_BLUE;
	theme_out[EDITOR_SYNTAX_HL_TYPE] = EDITOR_THEME_COLOR_BRIGHT_CYAN;
	theme_out[EDITOR_SYNTAX_HL_FUNCTION] = EDITOR_THEME_COLOR_BRIGHT_YELLOW;
	theme_out[EDITOR_SYNTAX_HL_STRING] = EDITOR_THEME_COLOR_GREEN;
	theme_out[EDITOR_SYNTAX_HL_NUMBER] = EDITOR_THEME_COLOR_MAGENTA;
	theme_out[EDITOR_SYNTAX_HL_CONSTANT] = EDITOR_THEME_COLOR_BRIGHT_MAGENTA;
	theme_out[EDITOR_SYNTAX_HL_PREPROCESSOR] = EDITOR_THEME_COLOR_BRIGHT_RED;
	theme_out[EDITOR_SYNTAX_HL_OPERATOR] = EDITOR_THEME_COLOR_BRIGHT_WHITE;
	theme_out[EDITOR_SYNTAX_HL_PUNCTUATION] = EDITOR_THEME_COLOR_DEFAULT;
}

static int editorNormalizeThemeToken(const char *token, char *out, size_t out_size) {
	if (token == NULL || out == NULL || out_size == 0) {
		return 0;
	}

	size_t write_idx = 0;
	for (size_t i = 0; token[i] != '\0'; i++) {
		unsigned char ch = (unsigned char)token[i];
		if (isspace(ch)) {
			continue;
		}
		if (ch == '-') {
			ch = '_';
		}
		if (write_idx + 1 >= out_size) {
			return 0;
		}
		out[write_idx++] = (char)tolower(ch);
	}

	if (write_idx == 0) {
		return 0;
	}
	out[write_idx] = '\0';
	return 1;
}

static int editorParseSyntaxHighlightClassName(const char *name,
		enum editorSyntaxHighlightClass *class_out) {
	char normalized[64];
	if (!editorNormalizeThemeToken(name, normalized, sizeof(normalized))) {
		return 0;
	}

	if (strcmp(normalized, "comment") == 0) {
		*class_out = EDITOR_SYNTAX_HL_COMMENT;
		return 1;
	}
	if (strcmp(normalized, "keyword") == 0) {
		*class_out = EDITOR_SYNTAX_HL_KEYWORD;
		return 1;
	}
	if (strcmp(normalized, "type") == 0) {
		*class_out = EDITOR_SYNTAX_HL_TYPE;
		return 1;
	}
	if (strcmp(normalized, "function") == 0) {
		*class_out = EDITOR_SYNTAX_HL_FUNCTION;
		return 1;
	}
	if (strcmp(normalized, "string") == 0) {
		*class_out = EDITOR_SYNTAX_HL_STRING;
		return 1;
	}
	if (strcmp(normalized, "number") == 0) {
		*class_out = EDITOR_SYNTAX_HL_NUMBER;
		return 1;
	}
	if (strcmp(normalized, "constant") == 0) {
		*class_out = EDITOR_SYNTAX_HL_CONSTANT;
		return 1;
	}
	if (strcmp(normalized, "preprocessor") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PREPROCESSOR;
		return 1;
	}
	if (strcmp(normalized, "operator") == 0) {
		*class_out = EDITOR_SYNTAX_HL_OPERATOR;
		return 1;
	}
	if (strcmp(normalized, "punctuation") == 0) {
		*class_out = EDITOR_SYNTAX_HL_PUNCTUATION;
		return 1;
	}

	return 0;
}

static int editorParseThemeColorValue(const char *value, enum editorThemeColor *color_out) {
	char normalized[64];
	if (!editorNormalizeThemeToken(value, normalized, sizeof(normalized))) {
		return 0;
	}

	if (strcmp(normalized, "default") == 0) {
		*color_out = EDITOR_THEME_COLOR_DEFAULT;
		return 1;
	}
	if (strcmp(normalized, "black") == 0) {
		*color_out = EDITOR_THEME_COLOR_BLACK;
		return 1;
	}
	if (strcmp(normalized, "red") == 0) {
		*color_out = EDITOR_THEME_COLOR_RED;
		return 1;
	}
	if (strcmp(normalized, "green") == 0) {
		*color_out = EDITOR_THEME_COLOR_GREEN;
		return 1;
	}
	if (strcmp(normalized, "yellow") == 0) {
		*color_out = EDITOR_THEME_COLOR_YELLOW;
		return 1;
	}
	if (strcmp(normalized, "blue") == 0) {
		*color_out = EDITOR_THEME_COLOR_BLUE;
		return 1;
	}
	if (strcmp(normalized, "magenta") == 0) {
		*color_out = EDITOR_THEME_COLOR_MAGENTA;
		return 1;
	}
	if (strcmp(normalized, "cyan") == 0) {
		*color_out = EDITOR_THEME_COLOR_CYAN;
		return 1;
	}
	if (strcmp(normalized, "white") == 0) {
		*color_out = EDITOR_THEME_COLOR_WHITE;
		return 1;
	}
	if (strcmp(normalized, "bright_black") == 0 || strcmp(normalized, "gray") == 0 ||
			strcmp(normalized, "grey") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_BLACK;
		return 1;
	}
	if (strcmp(normalized, "bright_red") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_RED;
		return 1;
	}
	if (strcmp(normalized, "bright_green") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_GREEN;
		return 1;
	}
	if (strcmp(normalized, "bright_yellow") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_YELLOW;
		return 1;
	}
	if (strcmp(normalized, "bright_blue") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_BLUE;
		return 1;
	}
	if (strcmp(normalized, "bright_magenta") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_MAGENTA;
		return 1;
	}
	if (strcmp(normalized, "bright_cyan") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_CYAN;
		return 1;
	}
	if (strcmp(normalized, "bright_white") == 0) {
		*color_out = EDITOR_THEME_COLOR_BRIGHT_WHITE;
		return 1;
	}

	return 0;
}

static enum editorSyntaxThemeFileStatus editorSyntaxThemeApplyConfigFile(
		enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT], const char *path,
		int *had_invalid_out) {
	if (had_invalid_out != NULL) {
		*had_invalid_out = 0;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_SYNTAX_THEME_FILE_MISSING;
		}
		if (had_invalid_out != NULL) {
			*had_invalid_out = 1;
		}
		return EDITOR_SYNTAX_THEME_FILE_APPLIED;
	}

	enum editorThemeColor updated[EDITOR_SYNTAX_HL_CLASS_COUNT];
	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		updated[i] = theme[i];
	}

	int in_theme_syntax_table = 0;
	int had_invalid = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			had_invalid = 1;
			int ch = 0;
			while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
				;
			}
			continue;
		}

		editorStripInlineComment(line);
		editorTrimRight(line);
		char *trimmed = editorTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}

		if (trimmed[0] == '[') {
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				had_invalid = 1;
				continue;
			}
			*close = '\0';
			char *table = editorTrimLeft(trimmed + 1);
			editorTrimRight(table);
			char *tail = editorTrimLeft(close + 1);
			if (tail[0] != '\0') {
				had_invalid = 1;
				continue;
			}

			in_theme_syntax_table = strcmp(table, "theme.syntax") == 0;
			continue;
		}

		if (!in_theme_syntax_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			had_invalid = 1;
			continue;
		}

		*eq = '\0';
		char *class_name = editorTrimLeft(trimmed);
		editorTrimRight(class_name);
		char *value = editorTrimLeft(eq + 1);
		if (class_name[0] == '\0') {
			had_invalid = 1;
			continue;
		}

		char color_name[64];
		if (!editorKeymapParseQuotedValue(value, color_name, sizeof(color_name))) {
			had_invalid = 1;
			continue;
		}

		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		enum editorThemeColor color = EDITOR_THEME_COLOR_DEFAULT;
		if (!editorParseSyntaxHighlightClassName(class_name, &highlight_class) ||
				highlight_class == EDITOR_SYNTAX_HL_NONE ||
				!editorParseThemeColorValue(color_name, &color)) {
			had_invalid = 1;
			continue;
		}

		updated[highlight_class] = color;
	}

	if (ferror(fp)) {
		had_invalid = 1;
	}

	fclose(fp);
	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme[i] = updated[i];
	}
	if (had_invalid_out != NULL) {
		*had_invalid_out = had_invalid;
	}
	return EDITOR_SYNTAX_THEME_FILE_APPLIED;
}

static int editorKeymapFormatKey(int key, char *buf, size_t bufsize) {
	if (bufsize == 0) {
		return 0;
	}

	if (key == CTRL_KEY(']')) {
		return snprintf(buf, bufsize, "Ctrl-]") > 0;
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
		case CTRL_ALT_ARROW_LEFT:
			return snprintf(buf, bufsize, "Ctrl-Alt-Left") > 0;
		case CTRL_ALT_ARROW_RIGHT:
			return snprintf(buf, bufsize, "Ctrl-Alt-Right") > 0;
		case CTRL_ALT_ARROW_DOWN:
			return snprintf(buf, bufsize, "Ctrl-Alt-Down") > 0;
		case CTRL_ALT_ARROW_UP:
			return snprintf(buf, bufsize, "Ctrl-Alt-Up") > 0;
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

static char *editorBuildGlobalConfigPath(void) {
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return NULL;
	}

	static const char suffix[] = "/.rotide/config.toml";
	size_t total_len = 0;
	if (!editorSizeAdd(strlen(home), sizeof(suffix), &total_len)) {
		return NULL;
	}

	char *path = editorMalloc(total_len);
	if (path == NULL) {
		return NULL;
	}

	int written = snprintf(path, total_len, "%s%s", home, suffix);
	if (written < 0 || (size_t)written >= total_len) {
		free(path);
		return NULL;
	}
	return path;
}

void editorKeymapInitDefaults(struct editorKeymap *keymap) {
	keymap->len = 0;
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('q'), EDITOR_ACTION_QUIT);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('s'), EDITOR_ACTION_SAVE);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('n'), EDITOR_ACTION_NEW_TAB);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('w'), EDITOR_ACTION_CLOSE_TAB);
	(void)editorKeymapAppendBinding(keymap, ALT_ARROW_RIGHT, EDITOR_ACTION_NEXT_TAB);
	(void)editorKeymapAppendBinding(keymap, ALT_ARROW_LEFT, EDITOR_ACTION_PREV_TAB);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('e'), EDITOR_ACTION_FOCUS_DRAWER);
	(void)editorKeymapAppendBinding(keymap, ALT_SHIFT_ARROW_LEFT,
			EDITOR_ACTION_RESIZE_DRAWER_NARROW);
	(void)editorKeymapAppendBinding(keymap, ALT_SHIFT_ARROW_RIGHT,
			EDITOR_ACTION_RESIZE_DRAWER_WIDEN);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('f'), EDITOR_ACTION_FIND);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('g'), EDITOR_ACTION_GOTO_LINE);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY(']'), EDITOR_ACTION_GOTO_DEFINITION);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('b'), EDITOR_ACTION_TOGGLE_SELECTION);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('c'), EDITOR_ACTION_COPY_SELECTION);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('x'), EDITOR_ACTION_CUT_SELECTION);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('d'), EDITOR_ACTION_DELETE_SELECTION);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('v'), EDITOR_ACTION_PASTE);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('z'), EDITOR_ACTION_UNDO);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('y'), EDITOR_ACTION_REDO);
	(void)editorKeymapAppendBinding(keymap, HOME_KEY, EDITOR_ACTION_MOVE_HOME);
	(void)editorKeymapAppendBinding(keymap, END_KEY, EDITOR_ACTION_MOVE_END);
	(void)editorKeymapAppendBinding(keymap, PAGE_UP, EDITOR_ACTION_PAGE_UP);
	(void)editorKeymapAppendBinding(keymap, PAGE_DOWN, EDITOR_ACTION_PAGE_DOWN);
	(void)editorKeymapAppendBinding(keymap, CTRL_ARROW_LEFT, EDITOR_ACTION_SCROLL_LEFT);
	(void)editorKeymapAppendBinding(keymap, CTRL_ARROW_RIGHT, EDITOR_ACTION_SCROLL_RIGHT);
	(void)editorKeymapAppendBinding(keymap, ARROW_UP, EDITOR_ACTION_MOVE_UP);
	(void)editorKeymapAppendBinding(keymap, ARROW_DOWN, EDITOR_ACTION_MOVE_DOWN);
	(void)editorKeymapAppendBinding(keymap, ARROW_LEFT, EDITOR_ACTION_MOVE_LEFT);
	(void)editorKeymapAppendBinding(keymap, ARROW_RIGHT, EDITOR_ACTION_MOVE_RIGHT);
	(void)editorKeymapAppendBinding(keymap, '\r', EDITOR_ACTION_NEWLINE);
	(void)editorKeymapAppendBinding(keymap, '\x1b', EDITOR_ACTION_ESCAPE);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('l'), EDITOR_ACTION_REDRAW);
	(void)editorKeymapAppendBinding(keymap, DEL_KEY, EDITOR_ACTION_DELETE_CHAR);
	(void)editorKeymapAppendBinding(keymap, BACKSPACE, EDITOR_ACTION_BACKSPACE);
	(void)editorKeymapAppendBinding(keymap, CTRL_KEY('h'), EDITOR_ACTION_BACKSPACE);
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
			return editorKeymapFormatKey(keymap->bindings[i].key, buf, bufsize);
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
		snprintf(save, sizeof(save), "Save");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_QUIT, quit, sizeof(quit))) {
		snprintf(quit, sizeof(quit), "Quit");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_NEW_TAB, new_tab, sizeof(new_tab))) {
		snprintf(new_tab, sizeof(new_tab), "NewTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_CLOSE_TAB, close_tab, sizeof(close_tab))) {
		snprintf(close_tab, sizeof(close_tab), "CloseTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_NEXT_TAB, next_tab, sizeof(next_tab))) {
		snprintf(next_tab, sizeof(next_tab), "NextTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_PREV_TAB, prev_tab, sizeof(prev_tab))) {
		snprintf(prev_tab, sizeof(prev_tab), "PrevTab");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_FOCUS_DRAWER, focus_drawer,
				sizeof(focus_drawer))) {
		snprintf(focus_drawer, sizeof(focus_drawer), "Drawer");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_FIND, find, sizeof(find))) {
		snprintf(find, sizeof(find), "Find");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_GOTO_LINE, go_to, sizeof(go_to))) {
		snprintf(go_to, sizeof(go_to), "Goto");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_TOGGLE_SELECTION, select, sizeof(select))) {
		snprintf(select, sizeof(select), "Select");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_COPY_SELECTION, copy, sizeof(copy))) {
		snprintf(copy, sizeof(copy), "Copy");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_CUT_SELECTION, cut, sizeof(cut))) {
		snprintf(cut, sizeof(cut), "Cut");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_DELETE_SELECTION, delete_sel,
				sizeof(delete_sel))) {
		snprintf(delete_sel, sizeof(delete_sel), "Del");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_PASTE, paste, sizeof(paste))) {
		snprintf(paste, sizeof(paste), "Paste");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_UNDO, undo, sizeof(undo))) {
		snprintf(undo, sizeof(undo), "Undo");
	}
	if (!editorKeymapFormatBinding(keymap, EDITOR_ACTION_REDO, redo, sizeof(redo))) {
		snprintf(redo, sizeof(redo), "Redo");
	}

	snprintf(buf, bufsize,
			"Help: %s save; %s quit; %s new; %s close; %s/%s tabs; %s drawer; %s find; %s goto",
			save, quit, new_tab, close_tab, prev_tab, next_tab, focus_drawer, find, go_to);
}

enum editorKeymapLoadStatus editorKeymapLoadFromPaths(struct editorKeymap *keymap,
		const char *global_path, const char *project_path) {
	if (keymap == NULL) {
		return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
	}

	editorKeymapInitDefaults(keymap);
	enum editorKeymapLoadStatus status = EDITOR_KEYMAP_LOAD_OK;

	if (global_path != NULL) {
		enum editorKeymapFileStatus global_status = editorKeymapApplyConfigFile(keymap, global_path);
		if (global_status == EDITOR_KEYMAP_FILE_OUT_OF_MEMORY) {
			editorKeymapInitDefaults(keymap);
			return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_KEYMAP_FILE_INVALID) {
			editorKeymapInitDefaults(keymap);
			status = EDITOR_KEYMAP_LOAD_INVALID_GLOBAL;
		}
	}

	if (project_path != NULL) {
		enum editorKeymapFileStatus project_status = editorKeymapApplyConfigFile(keymap, project_path);
		if (project_status == EDITOR_KEYMAP_FILE_OUT_OF_MEMORY) {
			editorKeymapInitDefaults(keymap);
			return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_KEYMAP_FILE_INVALID) {
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
		return editorKeymapLoadFromPaths(keymap, NULL, ".rotide.toml");
	}

	char *global_path = editorBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorKeymapInitDefaults(keymap);
		return EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY;
	}

	enum editorKeymapLoadStatus status =
			editorKeymapLoadFromPaths(keymap, global_path, ".rotide.toml");
	free(global_path);
	return status;
}

enum editorCursorStyleLoadStatus editorCursorStyleLoadFromPaths(enum editorCursorStyle *style_out,
		const char *global_path, const char *project_path) {
	if (style_out == NULL) {
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status = EDITOR_CURSOR_STYLE_LOAD_OK;

	if (global_path != NULL) {
		enum editorCursorStyleFileStatus global_status =
				editorCursorStyleApplyConfigFile(&style, global_path);
		if (global_status == EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY) {
			*style_out = EDITOR_CURSOR_STYLE_BAR;
			return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_CURSOR_STYLE_FILE_INVALID) {
			style = EDITOR_CURSOR_STYLE_BAR;
			status = (enum editorCursorStyleLoadStatus)(
					status | EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorCursorStyleFileStatus project_status =
				editorCursorStyleApplyConfigFile(&style, project_path);
		if (project_status == EDITOR_CURSOR_STYLE_FILE_OUT_OF_MEMORY) {
			*style_out = EDITOR_CURSOR_STYLE_BAR;
			return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_CURSOR_STYLE_FILE_INVALID) {
			style = EDITOR_CURSOR_STYLE_BAR;
			status = (enum editorCursorStyleLoadStatus)(
					status | EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT);
		}
	}

	*style_out = style;
	return status;
}

enum editorCursorStyleLoadStatus editorCursorStyleLoadConfigured(enum editorCursorStyle *style_out) {
	if (style_out == NULL) {
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorCursorStyleLoadFromPaths(style_out, NULL, ".rotide.toml");
	}

	char *global_path = editorBuildGlobalConfigPath();
	if (global_path == NULL) {
		*style_out = EDITOR_CURSOR_STYLE_BAR;
		return EDITOR_CURSOR_STYLE_LOAD_OUT_OF_MEMORY;
	}

	enum editorCursorStyleLoadStatus status =
			editorCursorStyleLoadFromPaths(style_out, global_path, ".rotide.toml");
	free(global_path);
	return status;
}

enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadFromPaths(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT],
		const char *global_path, const char *project_path) {
	if (theme_out == NULL) {
		return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
	}

	enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	editorSyntaxThemeInitDefaults(theme);
	enum editorSyntaxThemeLoadStatus status = EDITOR_SYNTAX_THEME_LOAD_OK;

	if (global_path != NULL) {
		int global_invalid = 0;
		enum editorSyntaxThemeFileStatus global_status =
				editorSyntaxThemeApplyConfigFile(theme, global_path, &global_invalid);
		if (global_status == EDITOR_SYNTAX_THEME_FILE_OUT_OF_MEMORY) {
			editorSyntaxThemeInitDefaults(theme_out);
			return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
		}
		if (global_invalid) {
			status = (enum editorSyntaxThemeLoadStatus)(
					status | EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		int project_invalid = 0;
		enum editorSyntaxThemeFileStatus project_status =
				editorSyntaxThemeApplyConfigFile(theme, project_path, &project_invalid);
		if (project_status == EDITOR_SYNTAX_THEME_FILE_OUT_OF_MEMORY) {
			editorSyntaxThemeInitDefaults(theme_out);
			return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
		}
		if (project_invalid) {
			status = (enum editorSyntaxThemeLoadStatus)(
					status | EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT);
		}
	}

	for (int i = 0; i < EDITOR_SYNTAX_HL_CLASS_COUNT; i++) {
		theme_out[i] = theme[i];
	}
	return status;
}

enum editorSyntaxThemeLoadStatus editorSyntaxThemeLoadConfigured(
		enum editorThemeColor theme_out[EDITOR_SYNTAX_HL_CLASS_COUNT]) {
	if (theme_out == NULL) {
		return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorSyntaxThemeLoadFromPaths(theme_out, NULL, ".rotide.toml");
	}

	char *global_path = editorBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorSyntaxThemeInitDefaults(theme_out);
		return EDITOR_SYNTAX_THEME_LOAD_OUT_OF_MEMORY;
	}

	enum editorSyntaxThemeLoadStatus status =
			editorSyntaxThemeLoadFromPaths(theme_out, global_path, ".rotide.toml");
	free(global_path);
	return status;
}

void editorLspConfigInitDefaults(int *enabled_out, char *command_out, size_t command_out_size) {
	if (enabled_out != NULL) {
		*enabled_out = 1;
	}
	if (command_out != NULL && command_out_size != 0) {
		(void)snprintf(command_out, command_out_size, "%s", "gopls");
		command_out[command_out_size - 1] = '\0';
	}
}

static int editorParseBooleanValue(const char *value, int *out) {
	if (value == NULL || out == NULL) {
		return 0;
	}
	if (strcasecmp(value, "true") == 0) {
		*out = 1;
		return 1;
	}
	if (strcasecmp(value, "false") == 0) {
		*out = 0;
		return 1;
	}
	return 0;
}

static enum editorLspConfigFileStatus editorLspConfigApplyConfigFile(int *enabled_in_out,
		char *command_in_out, size_t command_in_out_size, const char *path) {
	if (enabled_in_out == NULL || command_in_out == NULL || command_in_out_size == 0) {
		return EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_LSP_CONFIG_FILE_MISSING;
		}
		return EDITOR_LSP_CONFIG_FILE_INVALID;
	}

	int enabled = *enabled_in_out;
	char command[PATH_MAX];
	(void)snprintf(command, sizeof(command), "%s", command_in_out);

	int in_lsp_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		editorStripInlineComment(line);
		editorTrimRight(line);
		char *trimmed = editorTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}

		if (trimmed[0] == '[') {
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorTrimLeft(trimmed + 1);
			editorTrimRight(table);
			char *tail = editorTrimLeft(close + 1);
			if (tail[0] != '\0') {
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}

			in_lsp_table = strcmp(table, "lsp") == 0;
			continue;
		}

		if (!in_lsp_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorTrimLeft(trimmed);
		editorTrimRight(setting_name);
		char *value = editorTrimLeft(eq + 1);
		if (setting_name[0] == '\0') {
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		if (strcmp(setting_name, "enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "gopls_command") == 0) {
			if (!editorKeymapParseQuotedValue(value, command, sizeof(command)) ||
					command[0] == '\0') {
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
	}

	if (ferror(fp)) {
		fclose(fp);
		return EDITOR_LSP_CONFIG_FILE_INVALID;
	}

	fclose(fp);
	*enabled_in_out = enabled;
	(void)snprintf(command_in_out, command_in_out_size, "%s", command);
	command_in_out[command_in_out_size - 1] = '\0';
	return EDITOR_LSP_CONFIG_FILE_APPLIED;
}

enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(int *enabled_out,
		char *command_out, size_t command_out_size, const char *global_path,
		const char *project_path) {
	if (enabled_out == NULL || command_out == NULL || command_out_size == 0) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	editorLspConfigInitDefaults(enabled_out, command_out, command_out_size);
	enum editorLspConfigLoadStatus status = EDITOR_LSP_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum editorLspConfigFileStatus global_status =
				editorLspConfigApplyConfigFile(enabled_out, command_out, command_out_size,
						global_path);
		if (global_status == EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size);
			status = (enum editorLspConfigLoadStatus)(
					status | EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorLspConfigFileStatus project_status =
				editorLspConfigApplyConfigFile(enabled_out, command_out, command_out_size,
						project_path);
		if (project_status == EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size);
			status = (enum editorLspConfigLoadStatus)(
					status | EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT);
		}
	}

	return status;
}

enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(int *enabled_out,
		char *command_out, size_t command_out_size) {
	if (enabled_out == NULL || command_out == NULL || command_out_size == 0) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorLspConfigLoadFromPaths(enabled_out, command_out, command_out_size, NULL,
				".rotide.toml");
	}

	char *global_path = editorBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorLspConfigInitDefaults(enabled_out, command_out, command_out_size);
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(enabled_out, command_out, command_out_size,
					global_path, ".rotide.toml");
	free(global_path);
	return status;
}
