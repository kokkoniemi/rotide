#ifndef SYNTAX_H
#define SYNTAX_H

#include "rotide.h"

#include <stddef.h>
#include <stdint.h>

struct editorSyntaxState;

struct editorSyntaxPoint {
	uint32_t row;
	uint32_t column;
};

struct editorSyntaxEdit {
	uint32_t start_byte;
	uint32_t old_end_byte;
	uint32_t new_end_byte;
	struct editorSyntaxPoint start_point;
	struct editorSyntaxPoint old_end_point;
	struct editorSyntaxPoint new_end_point;
};

struct editorSyntaxCapture {
	uint32_t start_byte;
	uint32_t end_byte;
	enum editorSyntaxHighlightClass highlight_class;
};

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilename(const char *filename);

struct editorSyntaxState *editorSyntaxStateCreate(enum editorSyntaxLanguage language);
void editorSyntaxStateDestroy(struct editorSyntaxState *state);

int editorSyntaxStateParseFull(struct editorSyntaxState *state, const char *source, size_t len);
int editorSyntaxStateApplyEditAndParse(struct editorSyntaxState *state,
		const struct editorSyntaxEdit *edit,
		const char *source,
		size_t len);

int editorSyntaxStateHasTree(const struct editorSyntaxState *state);
const char *editorSyntaxStateRootType(const struct editorSyntaxState *state);
enum editorSyntaxLanguage editorSyntaxStateLanguage(const struct editorSyntaxState *state);
int editorSyntaxStateCollectCapturesForRange(const struct editorSyntaxState *state,
		uint32_t start_byte, uint32_t end_byte,
		struct editorSyntaxCapture *captures, int max_captures, int *count_out);
void editorSyntaxReleaseSharedResources(void);

#endif
