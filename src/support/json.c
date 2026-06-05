#include "support/json.h"

#include "support/size_utils.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int jsonStringEnsureCap(struct editorJsonString *sb, size_t needed) {
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

int editorJsonStringAppendBytes(struct editorJsonString *sb, const char *bytes, size_t len) {
	if (len == 0) {
		return 1;
	}

	size_t needed = 0;
	if (!editorSizeAdd(sb->len, len, &needed) || !editorSizeAdd(needed, 1, &needed)) {
		return 0;
	}
	if (!jsonStringEnsureCap(sb, needed)) {
		return 0;
	}

	memcpy(sb->buf + sb->len, bytes, len);
	sb->len += len;
	sb->buf[sb->len] = '\0';
	return 1;
}

int editorJsonStringAppend(struct editorJsonString *sb, const char *text) {
	return editorJsonStringAppendBytes(sb, text, strlen(text));
}

int editorJsonStringAppendVf(struct editorJsonString *sb, const char *fmt, va_list ap) {
	va_list ap_copy;
	va_copy(ap_copy, ap);
	int needed = vsnprintf(NULL, 0, fmt, ap);
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
	if (!jsonStringEnsureCap(sb, total_needed)) {
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

int editorJsonStringAppendf(struct editorJsonString *sb, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int ok = editorJsonStringAppendVf(sb, fmt, ap);
	va_end(ap);
	return ok;
}

int editorJsonStringAppendJsonEscaped(struct editorJsonString *sb, const char *text, size_t len) {
	if (!editorJsonStringAppendBytes(sb, "\"", 1)) {
		return 0;
	}

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)text[i];
		switch (ch) {
			case '"':
				if (!editorJsonStringAppend(sb, "\\\"")) {
					return 0;
				}
				break;
			case '\\':
				if (!editorJsonStringAppend(sb, "\\\\")) {
					return 0;
				}
				break;
			case '\b':
				if (!editorJsonStringAppend(sb, "\\b")) {
					return 0;
				}
				break;
			case '\f':
				if (!editorJsonStringAppend(sb, "\\f")) {
					return 0;
				}
				break;
			case '\n':
				if (!editorJsonStringAppend(sb, "\\n")) {
					return 0;
				}
				break;
			case '\r':
				if (!editorJsonStringAppend(sb, "\\r")) {
					return 0;
				}
				break;
			case '\t':
				if (!editorJsonStringAppend(sb, "\\t")) {
					return 0;
				}
				break;
			default:
				if (ch < 0x20) {
					if (!editorJsonStringAppendf(sb, "\\u%04x",
					                             (unsigned int)ch)) {
						return 0;
					}
				} else if (!editorJsonStringAppendBytes(sb, (const char *)&ch, 1)) {
					return 0;
				}
				break;
		}
	}

	return editorJsonStringAppendBytes(sb, "\"", 1);
}

static int jsonHexValue(char c) {
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

const char *editorJsonSkipWs(const char *p) {
	while (p != NULL && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
		p++;
	}
	return p;
}

int editorJsonParseString(const char *json, char **value_out, const char **after_out) {
	if (json == NULL || value_out == NULL || json[0] != '"') {
		return 0;
	}

	struct editorJsonString sb = {0};
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
					if (!editorJsonStringAppendBytes(&sb, &esc, 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'b':
					if (!editorJsonStringAppendBytes(&sb, "\b", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'f':
					if (!editorJsonStringAppendBytes(&sb, "\f", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'n':
					if (!editorJsonStringAppendBytes(&sb, "\n", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'r':
					if (!editorJsonStringAppendBytes(&sb, "\r", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 't':
					if (!editorJsonStringAppendBytes(&sb, "\t", 1)) {
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
					int h1 = jsonHexValue(json[i + 1]);
					int h2 = jsonHexValue(json[i + 2]);
					int h3 = jsonHexValue(json[i + 3]);
					int h4 = jsonHexValue(json[i + 4]);
					if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
						free(sb.buf);
						return 0;
					}
					unsigned int code = (unsigned int)((h1 << 12) | (h2 << 8) |
					                                   (h3 << 4) | h4);
					char out = code <= 0x7F ? (char)code : '?';
					if (!editorJsonStringAppendBytes(&sb, &out, 1)) {
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
		} else if (!editorJsonStringAppendBytes(&sb, &ch, 1)) {
			free(sb.buf);
			return 0;
		}
		i++;
	}

	free(sb.buf);
	return 0;
}

int editorJsonParseInt(const char *json, int *value_out, const char **after_out) {
	if (json == NULL || value_out == NULL) {
		return 0;
	}
	const char *p = editorJsonSkipWs(json);
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

int editorJsonFindStringField(const char *json, const char *field_name, char **value_out) {
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
	const char *value = editorJsonSkipWs(colon + 1);
	if (value == NULL || value[0] != '"') {
		return 0;
	}
	return editorJsonParseString(value, value_out, NULL);
}

const char *editorJsonFindObjectEnd(const char *object_start) {
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

const char *editorJsonFindArrayEnd(const char *array_start) {
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

const char *editorJsonStrstrBounded(const char *haystack, const char *needle, const char *limit) {
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

const char *editorJsonFindTopLevelKey(const char *object_start, const char *object_end,
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
