#include "config/keymap.h"

#include "config/common.h"
#include "input/system_vim.h"
#include "rotide.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum keymapFileStatus { KEYMAP_FILE_APPLIED = 0, KEYMAP_FILE_MISSING, KEYMAP_FILE_INVALID };

struct keymapNamedKey {
	const char *name;
	int key;
};

#define KEYMAP_KEY_SPEC_MAX 64

static const struct keymapNamedKey g_keymap_named_keys[] = {
        {"home", HOME_KEY},       {"end", END_KEY}, {"page_up", PAGE_UP},
        {"page_down", PAGE_DOWN}, {"enter", '\r'},  {"esc", '\x1b'},
        {"backspace", BACKSPACE}, {"del", DEL_KEY}, {"left", ARROW_LEFT},
        {"right", ARROW_RIGHT},   {"up", ARROW_UP}, {"down", ARROW_DOWN},
};

static int keymapParseModifiedLetter(const char *spec, int *key_out) {
	const char *letter = NULL;
	int ctrl = 0;
	int alt = 0;

	if (strncmp(spec, "ctrl+alt+", 9) == 0) {
		ctrl = 1;
		alt = 1;
		letter = spec + 9;
	} else if (strncmp(spec, "ctrl+", 5) == 0) {
		ctrl = 1;
		letter = spec + 5;
	} else if (strncmp(spec, "alt+", 4) == 0) {
		alt = 1;
		letter = spec + 4;
	} else {
		return 0;
	}
	if (letter[0] < 'a' || letter[0] > 'z' || letter[1] != '\0') {
		return 0;
	}
	if (ctrl && alt) {
		*key_out = EDITOR_CTRL_ALT_LETTER_KEY(letter[0]);
	} else if (alt) {
		*key_out = EDITOR_ALT_LETTER_KEY(letter[0]);
	} else {
		*key_out = CTRL_KEY(letter[0]);
	}
	return 1;
}

static int keymapParseKeySpec(const char *spec, int *key_out) {
	char normalized[KEYMAP_KEY_SPEC_MAX];
	size_t len = strlen(spec);

	if (len == 0 || len >= sizeof(normalized)) {
		return 0;
	}
	for (size_t i = 0; i <= len; i++) {
		normalized[i] = (char)tolower((unsigned char)spec[i]);
	}
	if (strcmp(normalized, "space") == 0) {
		*key_out = ' ';
		return 1;
	}
	for (size_t i = 0; i < sizeof(g_keymap_named_keys) / sizeof(g_keymap_named_keys[0]); i++) {
		if (strcmp(normalized, g_keymap_named_keys[i].name) == 0) {
			*key_out = g_keymap_named_keys[i].key;
			return 1;
		}
	}
	return keymapParseModifiedLetter(normalized, key_out);
}

static int keymapVimParseKeySpec(const char *spec, int *key_out) {
	if (spec[0] != '\0' && spec[1] == '\0' && isprint((unsigned char)spec[0])) {
		*key_out = (unsigned char)spec[0];
		return 1;
	}
	return keymapParseKeySpec(spec, key_out);
}

static int keymapVimOnSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "keymap.vim") == 0;
}

static int keymapVimOnEntry(void *ctx, const char *key, char *value) {
	const char *dot = strchr(key, '.');
	char mode[16];
	char key_spec[KEYMAP_KEY_SPEC_MAX];
	int parsed_key = 0;
	size_t mode_len = 0;

	(void)ctx;
	if (dot == NULL) {
		return 0;
	}
	mode_len = (size_t)(dot - key);
	if (mode_len == 0 || mode_len >= sizeof(mode) || dot[1] == '\0') {
		return 0;
	}
	memcpy(mode, key, mode_len);
	mode[mode_len] = '\0';
	if (!editorConfigParseQuotedValue(value, key_spec, sizeof(key_spec)) ||
	    !keymapVimParseKeySpec(key_spec, &parsed_key)) {
		return 0;
	}
	return editorVimBindKey(mode, dot + 1, parsed_key);
}

static enum keymapFileStatus keymapApplyVimConfigFile(const char *path) {
	struct editorConfigScanner scanner = {keymapVimOnSection, keymapVimOnEntry};

	switch (editorConfigScanFile(path, &scanner, NULL)) {
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
	enum editorKeymapLoadStatus status = EDITOR_KEYMAP_LOAD_OK;

	editorVimKeymapResetDefaults();
	if (global_path != NULL && keymapApplyVimConfigFile(global_path) == KEYMAP_FILE_INVALID) {
		editorVimKeymapResetDefaults();
		status = EDITOR_KEYMAP_LOAD_INVALID_GLOBAL;
	}
	if (project_path != NULL && keymapApplyVimConfigFile(project_path) == KEYMAP_FILE_INVALID) {
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
