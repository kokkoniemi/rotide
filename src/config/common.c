#include "config/common.h"

#include "config/default_config_data.h"
#include "support/alloc.h"
#include "support/size_utils.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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

enum { CONFIG_SCAN_LINE_MAX = 1024 };

enum editorConfigScanStatus
editorConfigScanStream(FILE *fp, const struct editorConfigScanner *scanner, void *ctx) {
	int in_selected_table = scanner->on_section != NULL && scanner->on_section(ctx, "");
	char line[CONFIG_SCAN_LINE_MAX];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			return EDITOR_CONFIG_SCAN_MALFORMED;
		}

		editorConfigStripInlineComment(line);
		editorConfigTrimRight(line);
		char *trimmed = editorConfigTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}

		if (trimmed[0] == '[') {
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				return EDITOR_CONFIG_SCAN_MALFORMED;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				return EDITOR_CONFIG_SCAN_MALFORMED;
			}
			in_selected_table =
			        scanner->on_section != NULL && scanner->on_section(ctx, table);
			continue;
		}

		if (!in_selected_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			return EDITOR_CONFIG_SCAN_MALFORMED;
		}
		*eq = '\0';
		char *key = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(key);
		char *value = editorConfigTrimLeft(eq + 1);
		if (key[0] == '\0') {
			return EDITOR_CONFIG_SCAN_MALFORMED;
		}
		if (scanner->on_entry != NULL && !scanner->on_entry(ctx, key, value)) {
			return EDITOR_CONFIG_SCAN_MALFORMED;
		}
	}

	return ferror(fp) ? EDITOR_CONFIG_SCAN_MALFORMED : EDITOR_CONFIG_SCAN_OK;
}

enum editorConfigScanStatus
editorConfigScanFile(const char *path, const struct editorConfigScanner *scanner, void *ctx) {
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		return errno == ENOENT ? EDITOR_CONFIG_SCAN_MISSING : EDITOR_CONFIG_SCAN_MALFORMED;
	}

	enum editorConfigScanStatus status = editorConfigScanStream(fp, scanner, ctx);
	(void)fclose(fp);
	return status;
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

int editorConfigPathIsGlobalConfig(const char *path) {
	if (path == NULL || path[0] == '\0') {
		return 0;
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		return 0;
	}

	struct stat path_stat;
	struct stat global_stat;
	int matches = 0;
	if (stat(path, &path_stat) == 0 && stat(global_path, &global_stat) == 0) {
		matches = path_stat.st_dev == global_stat.st_dev &&
		          path_stat.st_ino == global_stat.st_ino;
	} else {
		matches = strcmp(path, global_path) == 0;
	}
	free(global_path);
	return matches;
}

static int commonEnsureDir(const char *path) {
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

static char *commonBuildGlobalConfigDir(void) {
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
	char *dir = commonBuildGlobalConfigDir();
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

	if (!commonEnsureDir(dir)) {
		goto done;
	}

	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd == -1) {
		if (errno == EEXIST) {
			status = EDITOR_CONFIG_BOOTSTRAP_OK;
		}
		goto done;
	}

	const unsigned char *buf = EDITOR_CONFIG_DEFAULT_GLOBAL_CONTENT;
	size_t remaining = EDITOR_CONFIG_DEFAULT_GLOBAL_CONTENT_SIZE;
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
