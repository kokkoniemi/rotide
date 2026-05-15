/* Language detection from filename + shebang.
 *
 * This module turns a filename (and optionally the file's first line) into an
 * editorSyntaxLanguage by consulting the lookup tables in languages.c. It
 * holds no state and depends only on the language registry.
 */
#include "language/syntax.h"

#include "language/languages.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

static int editorSyntaxTokenFromLine(const char *line, size_t line_len, size_t *idx,
		const char **token_out, size_t *token_len_out) {
	if (line == NULL || idx == NULL || token_out == NULL || token_len_out == NULL) {
		return 0;
	}

	size_t i = *idx;
	while (i < line_len && isspace((unsigned char)line[i])) {
		i++;
	}
	if (i >= line_len) {
		*idx = i;
		return 0;
	}

	size_t start = i;
	while (i < line_len && !isspace((unsigned char)line[i])) {
		i++;
	}

	*idx = i;
	*token_out = &line[start];
	*token_len_out = i - start;
	return 1;
}

static enum editorSyntaxLanguage editorSyntaxLanguageFromShebangBase(const char *base,
		size_t base_len) {
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByShebangToken(base, base_len);
	return def != NULL ? def->id : EDITOR_SYNTAX_NONE;
}

static enum editorSyntaxLanguage editorSyntaxDetectLanguageFromShebang(const char *first_line) {
	if (first_line == NULL || first_line[0] != '#' || first_line[1] != '!') {
		return EDITOR_SYNTAX_NONE;
	}

	size_t line_len = strlen(first_line);
	size_t idx = 2;
	const char *token = NULL;
	size_t token_len = 0;
	if (!editorSyntaxTokenFromLine(first_line, line_len, &idx, &token, &token_len)) {
		return EDITOR_SYNTAX_NONE;
	}

	const char *base = token;
	for (size_t i = 0; i < token_len; i++) {
		if (token[i] == '/') {
			base = &token[i + 1];
		}
	}
	size_t base_len = token_len - (size_t)(base - token);

	enum editorSyntaxLanguage lang = editorSyntaxLanguageFromShebangBase(base, base_len);
	if (lang != EDITOR_SYNTAX_NONE) {
		return lang;
	}

	if (base_len == 3 && strncasecmp(base, "env", 3) == 0) {
		for (;;) {
			if (!editorSyntaxTokenFromLine(first_line, line_len, &idx, &token, &token_len)) {
				break;
			}
			if (token_len > 0 && token[0] == '-') {
				continue;
			}

			base = token;
			for (size_t i = 0; i < token_len; i++) {
				if (token[i] == '/') {
					base = &token[i + 1];
				}
			}
			base_len = token_len - (size_t)(base - token);
			lang = editorSyntaxLanguageFromShebangBase(base, base_len);
			if (lang != EDITOR_SYNTAX_NONE) {
				return lang;
			}
			break;
		}
	}

	return EDITOR_SYNTAX_NONE;
}

static int editorSyntaxFilenameIsExtensionless(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 1;
	}

	const char *base = strrchr(filename, '/');
	if (base != NULL) {
		base++;
	} else {
		base = filename;
	}
	if (base[0] == '\0') {
		return 0;
	}
	if (strchr(base, '.') == NULL) {
		return 1;
	}
	if (base[0] == '.' && strchr(base + 1, '.') == NULL) {
		return 1;
	}
	return 0;
}

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilename(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return EDITOR_SYNTAX_NONE;
	}

	const char *base = strrchr(filename, '/');
	if (base != NULL) {
		base++;
	} else {
		base = filename;
	}

	for (const char *dot = strchr(base, '.'); dot != NULL; dot = strchr(dot + 1, '.')) {
		const struct editorSyntaxLanguageDef *def =
				editorSyntaxLookupLanguageByExtension(dot);
		if (def != NULL) {
			return def->id;
		}
	}

	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByBasename(base);
	if (def != NULL) {
		return def->id;
	}

	return EDITOR_SYNTAX_NONE;
}

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilenameAndFirstLine(
		const char *filename, const char *first_line) {
	enum editorSyntaxLanguage from_filename = editorSyntaxDetectLanguageFromFilename(filename);
	if (from_filename != EDITOR_SYNTAX_NONE) {
		return from_filename;
	}
	if (!editorSyntaxFilenameIsExtensionless(filename)) {
		return EDITOR_SYNTAX_NONE;
	}

	return editorSyntaxDetectLanguageFromShebang(first_line);
}
