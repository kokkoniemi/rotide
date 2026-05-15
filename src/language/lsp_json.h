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

#endif
