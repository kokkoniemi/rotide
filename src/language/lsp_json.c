#include "language/lsp_json.h"

#include "support/size_utils.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lspJsonStringEnsureCap(struct editorLspString *sb, size_t needed) {
	if (needed <= sb->cap) {
		return 1;
	}

	size_t new_cap = sb->cap > 0 ? sb->cap : 128;
	while (new_cap < needed) {
		if (new_cap > SIZE_MAX / 2) {
			return 0;
		}
		new_cap *= 2;
	}

	char *grown = realloc(sb->buf, new_cap);
	if (grown == NULL) {
		return 0;
	}
	sb->buf = grown;
	sb->cap = new_cap;
	return 1;
}

int editorLspStringAppendBytes(struct editorLspString *sb, const char *bytes, size_t len) {
	if (len == 0) {
		return 1;
	}

	size_t needed = 0;
	if (!editorSizeAdd(sb->len, len, &needed) || !editorSizeAdd(needed, 1, &needed)) {
		return 0;
	}
	if (!lspJsonStringEnsureCap(sb, needed)) {
		return 0;
	}

	memcpy(sb->buf + sb->len, bytes, len);
	sb->len += len;
	sb->buf[sb->len] = '\0';
	return 1;
}

int editorLspStringAppend(struct editorLspString *sb, const char *text) {
	return editorLspStringAppendBytes(sb, text, strlen(text));
}

int editorLspStringAppendf(struct editorLspString *sb, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap_copy;
	va_copy(ap_copy, ap);
	int needed = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (needed < 0) {
		va_end(ap_copy);
		return 0;
	}

	size_t append_len = (size_t)needed;
	size_t total_needed = 0;
	if (!editorSizeAdd(sb->len, append_len, &total_needed) ||
	    !editorSizeAdd(total_needed, 1, &total_needed)) {
		va_end(ap_copy);
		return 0;
	}
	if (!lspJsonStringEnsureCap(sb, total_needed)) {
		va_end(ap_copy);
		return 0;
	}

	int written = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap_copy);
	va_end(ap_copy);
	if (written < 0 || (size_t)written != append_len) {
		return 0;
	}
	sb->len += append_len;
	return 1;
}

int editorLspStringAppendJsonEscaped(struct editorLspString *sb, const char *text, size_t len) {
	if (!editorLspStringAppendBytes(sb, "\"", 1)) {
		return 0;
	}

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)text[i];
		switch (ch) {
			case '"':
				if (!editorLspStringAppend(sb, "\\\"")) {
					return 0;
				}
				break;
			case '\\':
				if (!editorLspStringAppend(sb, "\\\\")) {
					return 0;
				}
				break;
			case '\b':
				if (!editorLspStringAppend(sb, "\\b")) {
					return 0;
				}
				break;
			case '\f':
				if (!editorLspStringAppend(sb, "\\f")) {
					return 0;
				}
				break;
			case '\n':
				if (!editorLspStringAppend(sb, "\\n")) {
					return 0;
				}
				break;
			case '\r':
				if (!editorLspStringAppend(sb, "\\r")) {
					return 0;
				}
				break;
			case '\t':
				if (!editorLspStringAppend(sb, "\\t")) {
					return 0;
				}
				break;
			default:
				if (ch < 0x20) {
					if (!editorLspStringAppendf(sb, "\\u%04x",
					                            (unsigned int)ch)) {
						return 0;
					}
				} else {
					if (!editorLspStringAppendBytes(sb, (const char *)&ch, 1)) {
						return 0;
					}
				}
				break;
		}
	}

	return editorLspStringAppendBytes(sb, "\"", 1);
}

static int lspJsonHexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

const char *editorLspSkipWs(const char *p) {
	while (p != NULL && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
		p++;
	}
	return p;
}

int editorLspParseJsonString(const char *json, char **value_out, const char **after_out) {
	if (json == NULL || value_out == NULL || json[0] != '"') {
		return 0;
	}

	struct editorLspString sb = {0};
	size_t i = 1;
	while (json[i] != '\0') {
		char ch = json[i];
		if (ch == '"') {
			i++;
			*value_out = sb.buf != NULL ? sb.buf : strdup("");
			if (*value_out == NULL) {
				free(sb.buf);
				return 0;
			}
			if (after_out != NULL) {
				*after_out = &json[i];
			}
			return 1;
		}
		if (ch == '\\') {
			i++;
			if (json[i] == '\0') {
				free(sb.buf);
				return 0;
			}
			char esc = json[i];
			switch (esc) {
				case '"':
				case '\\':
				case '/':
					if (!editorLspStringAppendBytes(&sb, &esc, 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'b':
					if (!editorLspStringAppendBytes(&sb, "\b", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'f':
					if (!editorLspStringAppendBytes(&sb, "\f", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'n':
					if (!editorLspStringAppendBytes(&sb, "\n", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'r':
					if (!editorLspStringAppendBytes(&sb, "\r", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 't':
					if (!editorLspStringAppendBytes(&sb, "\t", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'u': {
					if (json[i + 1] == '\0' || json[i + 2] == '\0' ||
					    json[i + 3] == '\0' || json[i + 4] == '\0') {
						free(sb.buf);
						return 0;
					}
					int h1 = lspJsonHexValue(json[i + 1]);
					int h2 = lspJsonHexValue(json[i + 2]);
					int h3 = lspJsonHexValue(json[i + 3]);
					int h4 = lspJsonHexValue(json[i + 4]);
					if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
						free(sb.buf);
						return 0;
					}
					unsigned int code = (unsigned int)((h1 << 12) | (h2 << 8) |
					                                   (h3 << 4) | h4);
					char out = code <= 0x7F ? (char)code : '?';
					if (!editorLspStringAppendBytes(&sb, &out, 1)) {
						free(sb.buf);
						return 0;
					}
					i += 4;
					break;
				}
				default:
					free(sb.buf);
					return 0;
			}
		} else {
			if (!editorLspStringAppendBytes(&sb, &ch, 1)) {
				free(sb.buf);
				return 0;
			}
		}
		i++;
	}

	free(sb.buf);
	return 0;
}

int editorLspParseJsonInt(const char *json, int *value_out, const char **after_out) {
	if (json == NULL || value_out == NULL) {
		return 0;
	}
	const char *p = editorLspSkipWs(json);
	if (p == NULL || (*p != '-' && !isdigit((unsigned char)*p))) {
		return 0;
	}

	int neg = 0;
	if (*p == '-') {
		neg = 1;
		p++;
	}
	if (!isdigit((unsigned char)*p)) {
		return 0;
	}

	long long value = 0;
	while (isdigit((unsigned char)*p)) {
		value = value * 10 + (*p - '0');
		if (value > INT_MAX) {
			return 0;
		}
		p++;
	}

	if (neg) {
		value = -value;
		if (value < INT_MIN) {
			return 0;
		}
	}

	*value_out = (int)value;
	if (after_out != NULL) {
		*after_out = p;
	}
	return 1;
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
	return editorLspParseJsonInt(colon + 1, id_out, NULL);
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
	const char *value = editorLspSkipWs(colon + 1);
	if (value == NULL) {
		return 0;
	}
	return strncmp(value, "null", 4) != 0;
}

int editorLspFindStringField(const char *json, const char *field_name, char **value_out) {
	if (json == NULL || field_name == NULL || value_out == NULL) {
		return 0;
	}
	*value_out = NULL;

	char key[128];
	int written = snprintf(key, sizeof(key), "\"%s\"", field_name);
	if (written <= 0 || (size_t)written >= sizeof(key)) {
		return 0;
	}

	const char *found = strstr(json, key);
	if (found == NULL) {
		return 0;
	}
	const char *colon = strchr(found, ':');
	if (colon == NULL) {
		return 0;
	}
	const char *value = editorLspSkipWs(colon + 1);
	if (value == NULL || value[0] != '"') {
		return 0;
	}
	return editorLspParseJsonString(value, value_out, NULL);
}

const char *editorLspFindJsonObjectEnd(const char *object_start) {
	if (object_start == NULL || object_start[0] != '{') {
		return NULL;
	}

	int depth = 0;
	int in_string = 0;
	int escaped = 0;
	for (const char *scan = object_start; scan[0] != '\0'; scan++) {
		char ch = scan[0];
		if (in_string) {
			if (escaped) {
				escaped = 0;
			} else if (ch == '\\') {
				escaped = 1;
			} else if (ch == '"') {
				in_string = 0;
			}
			continue;
		}

		if (ch == '"') {
			in_string = 1;
			continue;
		}
		if (ch == '{') {
			depth++;
			continue;
		}
		if (ch == '}') {
			depth--;
			if (depth == 0) {
				return scan + 1;
			}
			if (depth < 0) {
				return NULL;
			}
		}
	}

	return NULL;
}

const char *editorLspFindJsonArrayEnd(const char *array_start) {
	if (array_start == NULL || array_start[0] != '[') {
		return NULL;
	}

	int depth = 0;
	int in_string = 0;
	int escaped = 0;
	for (const char *scan = array_start; scan[0] != '\0'; scan++) {
		char ch = scan[0];
		if (in_string) {
			if (escaped) {
				escaped = 0;
			} else if (ch == '\\') {
				escaped = 1;
			} else if (ch == '"') {
				in_string = 0;
			}
			continue;
		}

		if (ch == '"') {
			in_string = 1;
			continue;
		}
		if (ch == '[') {
			depth++;
			continue;
		}
		if (ch == ']') {
			depth--;
			if (depth == 0) {
				return scan + 1;
			}
			if (depth < 0) {
				return NULL;
			}
		}
	}

	return NULL;
}

const char *editorLspStrstrBounded(const char *haystack, const char *needle, const char *limit) {
	if (haystack == NULL || needle == NULL) {
		return NULL;
	}
	const char *found = strstr(haystack, needle);
	if (found == NULL) {
		return NULL;
	}
	if (limit != NULL && found >= limit) {
		return NULL;
	}
	return found;
}

const char *editorLspFindTopLevelKey(const char *object_start, const char *object_end,
                                     const char *quoted_key) {
	if (object_start == NULL || object_end == NULL || quoted_key == NULL ||
	    object_start >= object_end || object_start[0] != '{') {
		return NULL;
	}
	size_t key_len = strlen(quoted_key);
	int depth = 0;
	const char *p = object_start + 1;
	while (p < object_end) {
		char ch = *p;
		if (ch == '"') {
			if (depth == 0 && (size_t)(object_end - p) >= key_len &&
			    memcmp(p, quoted_key, key_len) == 0) {
				const char *after = p + key_len;
				while (after < object_end && (*after == ' ' || *after == '\t' ||
				                              *after == '\n' || *after == '\r')) {
					after++;
				}
				if (after < object_end && *after == ':') {
					return p;
				}
			}
			p++;
			while (p < object_end && *p != '"') {
				if (*p == '\\' && p + 1 < object_end) {
					p += 2;
					continue;
				}
				p++;
			}
			if (p < object_end) {
				p++;
			}
			continue;
		}
		if (ch == '{' || ch == '[') {
			depth++;
		} else if (ch == '}' || ch == ']') {
			if (depth > 0) {
				depth--;
			}
		}
		p++;
	}
	return NULL;
}

int editorLspParsePositionFromKey(const char *range_json, const char *key_name, const char *limit,
                                  int *line_out, int *character_out) {
	char key_pattern[32];
	int written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key_name);
	if (written <= 0 || (size_t)written >= sizeof(key_pattern)) {
		return 0;
	}
	const char *start = editorLspStrstrBounded(range_json, key_pattern, limit);
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
	const char *start_end = editorLspFindJsonObjectEnd(start_object);
	if (start_end == NULL || (limit != NULL && start_end > limit)) {
		return 0;
	}

	const char *line_key = editorLspStrstrBounded(start_object, "\"line\"", start_end);
	if (line_key == NULL) {
		return 0;
	}
	const char *line_colon = strchr(line_key, ':');
	if (line_colon == NULL || line_colon >= start_end) {
		return 0;
	}
	int line = 0;
	if (!editorLspParseJsonInt(line_colon + 1, &line, NULL) || line < 0) {
		return 0;
	}

	const char *char_key = editorLspStrstrBounded(start_object, "\"character\"", start_end);
	if (char_key == NULL) {
		return 0;
	}
	const char *char_colon = strchr(char_key, ':');
	if (char_colon == NULL || char_colon >= start_end) {
		return 0;
	}
	int character = 0;
	if (!editorLspParseJsonInt(char_colon + 1, &character, NULL) || character < 0) {
		return 0;
	}

	*line_out = line;
	*character_out = character;
	return 1;
}
