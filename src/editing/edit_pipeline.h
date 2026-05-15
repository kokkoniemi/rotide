#ifndef EDITING_EDIT_PIPELINE_H
#define EDITING_EDIT_PIPELINE_H

#include "rotide.h"

#include <stddef.h>

/* Canonical edit descriptor for text mutations.
 * All normal editing, undo, redo, syntax updates, LSP notifications, cursor
 * synchronization, and dirty-state transitions route through this shape.
 */
struct editorDocumentEdit {
	enum editorEditKind kind;
	size_t start_offset;
	size_t old_len;
	const char *new_text;
	size_t new_len;
	size_t before_cursor_offset;
	size_t after_cursor_offset;
	int before_dirty;
	int after_dirty;
};

/* The single active-buffer mutation path. */
int editorApplyDocumentEdit(const struct editorDocumentEdit *edit);

#endif
