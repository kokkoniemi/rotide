#include "config/common.h"

#include "support/alloc.h"
#include "support/size_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *editorConfigTrimLeft(char *s) {
	while (*s != '\0' && isspace((unsigned char)*s)) {
		s++;
	}
	return s;
}

void editorConfigTrimRight(char *s) {
	size_t len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1])) {
		len--;
	}
	s[len] = '\0';
}

void editorConfigStripInlineComment(char *line) {
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

int editorConfigParseQuotedValue(const char *value, char *buf, size_t bufsize) {
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
	const char *tail = editorConfigTrimLeft((char *)&value[i]);
	return tail[0] == '\0';
}

char *editorConfigBuildGlobalConfigPath(void) {
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
