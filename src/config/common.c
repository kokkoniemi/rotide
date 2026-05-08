#include "config/common.h"

#include "support/alloc.h"
#include "support/size_utils.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static const char EDITOR_CONFIG_DEFAULT_GLOBAL_CONTENT[] =
		"# Rotide global config (auto-created on first launch).\n"
		"# Edit values to customize. See `config.toml.example` in the source\n"
		"# repository for a complete reference of available options.\n"
		"\n"
		"[editor]\n"
		"cursor_style = \"bar\"\n"
		"cursor_blink = true\n"
		"line_wrap = false\n"
		"line_numbers = true\n"
		"current_line_highlight = true\n"
		"\n"
		"[theme]\n"
		"name = \"terminal\"\n";

static int editorConfigEnsureDir(const char *path) {
	if (mkdir(path, 0700) == 0) {
		return 1;
	}
	if (errno != EEXIST) {
		return 0;
	}
	struct stat st;
	if (stat(path, &st) == -1) {
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static char *editorConfigBuildGlobalConfigDir(void) {
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return NULL;
	}
	static const char suffix[] = "/.rotide";
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

enum editorConfigBootstrapStatus editorConfigEnsureGlobalConfig(void) {
	char *dir = editorConfigBuildGlobalConfigDir();
	if (dir == NULL) {
		return EDITOR_CONFIG_BOOTSTRAP_FAILED;
	}
	char *path = editorConfigBuildGlobalConfigPath();
	if (path == NULL) {
		free(dir);
		return EDITOR_CONFIG_BOOTSTRAP_FAILED;
	}

	enum editorConfigBootstrapStatus status = EDITOR_CONFIG_BOOTSTRAP_FAILED;
	struct stat st;
	if (stat(path, &st) == 0) {
		status = S_ISREG(st.st_mode) ? EDITOR_CONFIG_BOOTSTRAP_OK
					     : EDITOR_CONFIG_BOOTSTRAP_FAILED;
		goto done;
	}
	if (errno != ENOENT) {
		goto done;
	}

	if (!editorConfigEnsureDir(dir)) {
		goto done;
	}

	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd == -1) {
		if (errno == EEXIST) {
			status = EDITOR_CONFIG_BOOTSTRAP_OK;
		}
		goto done;
	}

	const char *buf = EDITOR_CONFIG_DEFAULT_GLOBAL_CONTENT;
	size_t remaining = sizeof(EDITOR_CONFIG_DEFAULT_GLOBAL_CONTENT) - 1;
	while (remaining > 0) {
		ssize_t n = write(fd, buf, remaining);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(fd);
			(void)unlink(path);
			goto done;
		}
		buf += (size_t)n;
		remaining -= (size_t)n;
	}
	if (close(fd) != 0) {
		(void)unlink(path);
		goto done;
	}
	status = EDITOR_CONFIG_BOOTSTRAP_CREATED;

done:
	free(dir);
	free(path);
	return status;
}
