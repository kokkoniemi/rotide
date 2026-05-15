#ifndef EDITING_SYNTAX_RUNTIME_H
#define EDITING_SYNTAX_RUNTIME_H

#include "language/syntax.h"

#include <stddef.h>

int editorSyntaxApplyIncrementalEditActive(const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_len);
void editorSyntaxRuntimeReportStatusIfNeeded(void);

#endif
