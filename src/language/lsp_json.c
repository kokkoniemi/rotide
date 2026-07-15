#include "language/lsp_json.h"

#include "support/json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int editorLspStringAppend(struct editorJsonString *sb, const char *text) {
	return editorJsonStringAppend(sb, text);
}

int editorLspStringAppendBytes(struct editorJsonString *sb, const char *bytes, size_t len) {
	return editorJsonStringAppendBytes(sb, bytes, len);
}

int editorLspStringAppendf(struct editorJsonString *sb, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int ok = editorJsonStringAppendVf(sb, fmt, ap);
	va_end(ap);
	return ok;
}

int editorLspStringAppendJsonEscaped(struct editorJsonString *sb, const char *text, size_t len) {
	return editorJsonStringAppendJsonEscaped(sb, text, len);
}

const char *editorLspSkipWs(const char *p) {
	return editorJsonSkipWs(p);
}

int editorLspParseJsonString(const char *json, char **value_out, const char **after_out) {
	return editorJsonParseString(json, value_out, after_out);
}

int editorLspParseJsonInt(const char *json, int *value_out, const char **after_out) {
	return editorJsonParseInt(json, value_out, after_out);
}

int editorLspExtractResponseId(const char *json, int *id_out) {
	if (json == NULL || id_out == NULL) {
		return 0;
	}
	const char *key = strstr(json, "\"id\"");
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL) {
		return 0;
	}
	return editorJsonParseInt(colon + 1, id_out, NULL);
}

int editorLspResponseHasError(const char *json) {
	const char *key = strstr(json, "\"error\"");
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL) {
		return 0;
	}
	const char *value = editorJsonSkipWs(colon + 1);
	if (value == NULL) {
		return 0;
	}
	return strncmp(value, "null", 4) != 0;
}

int editorLspFindStringField(const char *json, const char *field_name, char **value_out) {
	return editorJsonFindStringField(json, field_name, value_out);
}

const char *editorLspFindJsonObjectEnd(const char *object_start) {
	return editorJsonFindObjectEnd(object_start);
}

const char *editorLspFindJsonArrayEnd(const char *array_start) {
	return editorJsonFindArrayEnd(array_start);
}

const char *editorLspStrstrBounded(const char *haystack, const char *needle, const char *limit) {
	return editorJsonStrstrBounded(haystack, needle, limit);
}

const char *editorLspFindTopLevelKey(const char *object_start, const char *object_end,
                                     const char *quoted_key) {
	return editorJsonFindTopLevelKey(object_start, object_end, quoted_key);
}

int editorLspParseStringFieldFromObject(const char *object_start, const char *object_end,
                                        const char *quoted_key, char **value_out) {
	if (value_out == NULL) {
		return 0;
	}
	*value_out = NULL;
	const char *key = editorLspFindTopLevelKey(object_start, object_end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= object_end) {
		return 0;
	}
	const char *value = editorLspSkipWs(colon + 1);
	if (value == NULL || value >= object_end || value[0] != '"') {
		return 0;
	}
	return editorLspParseJsonString(value, value_out, NULL);
}

int editorLspParsePositionFromKey(const char *range_json, const char *key_name, const char *limit,
                                  int *line_out, int *character_out) {
	char key_pattern[32];
	int written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key_name);
	if (written <= 0 || (size_t)written >= sizeof(key_pattern)) {
		return 0;
	}
	const char *start = editorJsonStrstrBounded(range_json, key_pattern, limit);
	if (start == NULL) {
		return 0;
	}

	const char *start_colon = strchr(start, ':');
	if (start_colon == NULL || (limit != NULL && start_colon >= limit)) {
		return 0;
	}
	const char *start_object = strchr(start_colon + 1, '{');
	if (start_object == NULL || (limit != NULL && start_object >= limit)) {
		return 0;
	}
	const char *start_end = editorJsonFindObjectEnd(start_object);
	if (start_end == NULL || (limit != NULL && start_end > limit)) {
		return 0;
	}

	const char *line_key = editorJsonStrstrBounded(start_object, "\"line\"", start_end);
	if (line_key == NULL) {
		return 0;
	}
	const char *line_colon = strchr(line_key, ':');
	if (line_colon == NULL || line_colon >= start_end) {
		return 0;
	}
	int line = 0;
	if (!editorJsonParseInt(line_colon + 1, &line, NULL) || line < 0) {
		return 0;
	}

	const char *char_key = editorJsonStrstrBounded(start_object, "\"character\"", start_end);
	if (char_key == NULL) {
		return 0;
	}
	const char *char_colon = strchr(char_key, ':');
	if (char_colon == NULL || char_colon >= start_end) {
		return 0;
	}
	int character = 0;
	if (!editorJsonParseInt(char_colon + 1, &character, NULL) || character < 0) {
		return 0;
	}

	*line_out = line;
	*character_out = character;
	return 1;
}
