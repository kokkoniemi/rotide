#ifndef ROTIDE_SUPPORT_JSON_H
#define ROTIDE_SUPPORT_JSON_H

#include <stdarg.h>
#include <stddef.h>

struct editorJsonString {
	char *buf;
	size_t len;
	size_t cap;
};

int editorJsonStringAppend(struct editorJsonString *sb, const char *text);
int editorJsonStringAppendBytes(struct editorJsonString *sb, const char *bytes, size_t len);
int editorJsonStringAppendVf(struct editorJsonString *sb, const char *fmt, va_list ap);
int editorJsonStringAppendf(struct editorJsonString *sb, const char *fmt, ...)
        __attribute__((format(printf, 2, 3)));
int editorJsonStringAppendJsonEscaped(struct editorJsonString *sb, const char *text, size_t len);

const char *editorJsonSkipWs(const char *p);
int editorJsonParseString(const char *json, char **value_out, const char **after_out);
int editorJsonParseInt(const char *json, int *value_out, const char **after_out);
int editorJsonFindStringField(const char *json, const char *field_name, char **value_out);
const char *editorJsonFindObjectEnd(const char *object_start);
const char *editorJsonFindArrayEnd(const char *array_start);
const char *editorJsonStrstrBounded(const char *haystack, const char *needle, const char *limit);
const char *editorJsonFindTopLevelKey(const char *object_start, const char *object_end,
                                      const char *quoted_key);

#endif
