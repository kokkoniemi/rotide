#ifndef CONFIG_COMMON_H
#define CONFIG_COMMON_H

#include <stddef.h>

char *editorConfigTrimLeft(char *s);
void editorConfigTrimRight(char *s);
void editorConfigStripInlineComment(char *line);
int editorConfigParseQuotedValue(const char *value, char *buf, size_t bufsize);
char *editorConfigBuildGlobalConfigPath(void);

#endif
