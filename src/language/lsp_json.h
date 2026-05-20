#ifndef LSP_JSON_H
#define LSP_JSON_H

#include <stddef.h>

struct editorLspString {
	char *buf;
	size_t len;
	size_t cap;
};

int editorLspStringAppend(struct editorLspString *sb, const char *text);
int editorLspStringAppendBytes(struct editorLspString *sb, const char *bytes, size_t len);
int editorLspStringAppendf(struct editorLspString *sb, const char *fmt, ...)
        __attribute__((format(printf, 2, 3)));
int editorLspStringAppendJsonEscaped(struct editorLspString *sb, const char *text, size_t len);

const char *editorLspSkipWs(const char *p);
int editorLspParseJsonString(const char *json, char **value_out, const char **after_out);
int editorLspParseJsonInt(const char *json, int *value_out, const char **after_out);
int editorLspExtractResponseId(const char *json, int *id_out);
int editorLspResponseHasError(const char *json);
int editorLspFindStringField(const char *json, const char *field_name, char **value_out);
const char *editorLspFindJsonObjectEnd(const char *object_start);
const char *editorLspFindJsonArrayEnd(const char *array_start);

/*
 * editorLspStrstrBounded: like strstr but stops scanning at `limit` (exclusive).
 * Returns NULL if the needle is not found before `limit` or either input is
 * NULL. `limit` may be NULL to disable the bound.
 */
const char *editorLspStrstrBounded(const char *haystack, const char *needle, const char *limit);

/*
 * editorLspFindTopLevelKey: find a quoted key (e.g. "\"name\"") at the
 * immediate top level of a JSON object that starts at `object_start` (must
 * point at '{') and ends just before `object_end` (the matching '}'). Skips
 * nested objects, arrays, and strings so a same-named key inside a child
 * object is not matched. Returns a pointer to the opening quote of the
 * matched key, or NULL.
 */
const char *editorLspFindTopLevelKey(const char *object_start, const char *object_end,
                                     const char *quoted_key);

/*
 * editorLspParsePositionFromKey: parse an LSP Position object found under
 * `key_name` (e.g. "start") inside a Range-like JSON object at `range_json`.
 * Writes line/character on success. `limit` bounds the search. Returns 1
 * on success, 0 on absence or malformed input.
 */
int editorLspParsePositionFromKey(const char *range_json, const char *key_name, const char *limit,
                                  int *line_out, int *character_out);

#endif
