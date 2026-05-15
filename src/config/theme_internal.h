#ifndef CONFIG_THEME_INTERNAL_H
#define CONFIG_THEME_INTERNAL_H

#include "rotide.h"

/*
 * Internal contracts shared between theme_builtin.c and theme_parse.c.
 * Outside src/config/, consumers reach the theme system via
 * theme_config.h. The helpers below are implementation details — the
 * built-in tables module owns them and the parser module borrows them
 * to populate themes loaded from disk.
 */
void editorThemeSetName(struct editorTheme *theme, const char *name);

#endif
