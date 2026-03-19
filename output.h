#ifndef OUTPUT_H
#define OUTPUT_H

#include "rotide.h"

void editorRefreshScreen(void);
void editorViewportSetMode(enum editorViewportMode mode);
void editorViewportScrollByRows(int delta_rows);
void editorViewportEnsureCursorVisible(void);

#endif
