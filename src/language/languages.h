#ifndef ROTIDE_LANGUAGES_H
#define ROTIDE_LANGUAGES_H

#include "rotide.h"

#include <stddef.h>

#include "tree_sitter/api.h"

struct editorSyntaxEmbeddedQueryPart {
	const unsigned char *data;
	size_t len;
};

struct editorSyntaxLanguageDef {
	enum editorSyntaxLanguage id;
	const char *name;
	const TSLanguage *(*ts_factory)(void);

	const struct editorSyntaxEmbeddedQueryPart *highlight_parts;
	int highlight_part_count;
	const struct editorSyntaxEmbeddedQueryPart *locals_parts;
	int locals_part_count;
	const struct editorSyntaxEmbeddedQueryPart *injection_parts;
	int injection_part_count;

	const char *const *extensions;
	const char *const *basenames;
	int (*shebang_matches)(const char *token, size_t len);
	const char *const *injection_aliases;
};

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguage(enum editorSyntaxLanguage id);
const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByName(const char *name, size_t len);
const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByExtension(const char *ext);
const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByBasename(const char *base);
const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByShebangToken(
		const char *token, size_t len);
const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByInjectionName(
		const char *name, size_t len);

int editorSyntaxLanguageDefCount(void);
const struct editorSyntaxLanguageDef *editorSyntaxLanguageDefAt(int idx);

#endif
