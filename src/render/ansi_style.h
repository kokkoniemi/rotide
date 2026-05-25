#ifndef ROTIDE_RENDER_ANSI_STYLE_H
#define ROTIDE_RENDER_ANSI_STYLE_H

#include "render/write_buf.h"
#include "rotide.h"

int editorThemeColorEquals(struct editorThemeColor a, struct editorThemeColor b);
int editorThemeColorIsDefault(struct editorThemeColor color);
int editorAppendThemeColor(struct writeBuf *wb, struct editorThemeColor color, int bg);
int editorAppendThemeForeground(struct writeBuf *wb, struct editorThemeColor color);
int editorAppendThemeBackground(struct writeBuf *wb, struct editorThemeColor color);
int editorAppendThemeBaseForeground(struct writeBuf *wb);
int editorAppendThemeBaseStyle(struct writeBuf *wb);
int editorAppendThemeReset(struct writeBuf *wb);
int editorAppendThemeStyle(struct writeBuf *wb, enum editorThemeStyleRole role);
int editorAppendThemeForegroundRole(struct writeBuf *wb, enum editorThemeUiRole role);
int editorAppendThemeBackgroundRole(struct writeBuf *wb, enum editorThemeUiRole role);
int editorAppendThemeCursorColor(struct writeBuf *wb);

/* idx >= 16 returns the fg/bg fallback — callers must handle 256-color
 * indices themselves. */
struct editorThemeColor editorThemeResolveAnsi(unsigned idx, int is_fg);

#endif
