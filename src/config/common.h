#ifndef ROTIDE_CONFIG_COMMON_H
#define ROTIDE_CONFIG_COMMON_H

#include <stddef.h>
#include <stdio.h>

char *editorConfigTrimLeft(char *s);
void editorConfigTrimRight(char *s);
void editorConfigStripInlineComment(char *line);
int editorConfigParseQuotedValue(const char *value, char *buf, size_t bufsize);
char *editorConfigBuildGlobalConfigPath(void);
int editorConfigPathIsGlobalConfig(const char *path);

enum editorConfigScanStatus {
	EDITOR_CONFIG_SCAN_OK = 0,
	EDITOR_CONFIG_SCAN_MISSING,
	EDITOR_CONFIG_SCAN_MALFORMED,
};

/*
 * Drives config parsing through caller callbacks. `on_section` opts into a
 * `[table]`'s entries by returning non-zero (and is called once with "" for the
 * region before the first header); those `key = value` lines then go to
 * `on_entry`, which returns zero to reject one and abort the scan. `value` is
 * mutable and points into a reused line buffer. Either callback may be NULL.
 * Returns MISSING only when the file is absent; any other open/read error, bad
 * line, or rejected entry is MALFORMED.
 */
struct editorConfigScanner {
	int (*on_section)(void *ctx, const char *table);
	int (*on_entry)(void *ctx, const char *key, char *value);
};

enum editorConfigScanStatus
editorConfigScanFile(const char *path, const struct editorConfigScanner *scanner, void *ctx);

/* As above, but scans an already-open stream the caller owns (never returns
 * MISSING). Lets callers with a FILE * — e.g. a fuzz harness — reuse the scan. */
enum editorConfigScanStatus
editorConfigScanStream(FILE *fp, const struct editorConfigScanner *scanner, void *ctx);

enum editorConfigBootstrapStatus {
	EDITOR_CONFIG_BOOTSTRAP_OK = 0,
	EDITOR_CONFIG_BOOTSTRAP_CREATED,
	EDITOR_CONFIG_BOOTSTRAP_FAILED,
};

enum editorConfigBootstrapStatus editorConfigEnsureGlobalConfig(void);

#endif
