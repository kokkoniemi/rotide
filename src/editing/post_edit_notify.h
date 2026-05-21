#ifndef ROTIDE_EDITING_POST_EDIT_NOTIFY_H
#define ROTIDE_EDITING_POST_EDIT_NOTIFY_H

#include "language/syntax.h"

#include <stddef.h>

void editorNotifyPostEditLanguage(int syntax_track, const struct editorSyntaxEdit *syntax_edit,
                                  const char *inserted_text, size_t inserted_len);

#endif
