/* Included by lsp.c. Shared protocol, URI, and parse helpers live here. */

static int editorLspStringEnsureCap(struct editorLspString *sb, size_t needed) {
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

static int editorLspStringAppendBytes(struct editorLspString *sb, const char *bytes, size_t len) {
	if (len == 0) {
		return 1;
	}

	size_t needed = 0;
	if (!editorSizeAdd(sb->len, len, &needed) || !editorSizeAdd(needed, 1, &needed)) {
		return 0;
	}
	if (!editorLspStringEnsureCap(sb, needed)) {
		return 0;
	}

	memcpy(sb->buf + sb->len, bytes, len);
	sb->len += len;
	sb->buf[sb->len] = '\0';
	return 1;
}

static int editorLspStringAppend(struct editorLspString *sb, const char *text) {
	return editorLspStringAppendBytes(sb, text, strlen(text));
}

static int editorLspStringAppendf(struct editorLspString *sb, const char *fmt, ...) {
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
	if (!editorLspStringEnsureCap(sb, total_needed)) {
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

static int editorLspStringAppendJsonEscaped(struct editorLspString *sb, const char *text,
		size_t len) {
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
					if (!editorLspStringAppendf(sb, "\\u%04x", (unsigned int)ch)) {
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

static char *editorLspBuildInitializeRequestJson(int request_id, const char *root_uri,
		int process_id) {
	if (root_uri == NULL || root_uri[0] == '\0') {
		return NULL;
	}

	struct editorLspString init = {0};
	int built = editorLspStringAppendf(&init,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
			"\"processId\":%d,\"rootUri\":",
			request_id, process_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&init, root_uri, strlen(root_uri));
	}
	if (built) {
		built = editorLspStringAppend(&init,
				",\"capabilities\":{\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]},"
				"\"workspace\":{\"applyEdit\":true},"
				"\"textDocument\":{\"codeAction\":{\"codeActionLiteralSupport\":{"
				"\"codeActionKind\":{\"valueSet\":[\"quickfix\",\"source.fixAll\","
				"\"source.fixAll.eslint\"]}}}}}}}");
	}
	if (!built) {
		free(init.buf);
		return NULL;
	}
	return init.buf;
}


static int editorLspBuildFileUri(const char *path, char **uri_out) {
	if (path == NULL || uri_out == NULL) {
		return 0;
	}
	*uri_out = NULL;

	char *absolute_path = editorPathAbsoluteDup(path);
	if (absolute_path == NULL) {
		return 0;
	}

	struct editorLspString sb = {0};
	if (!editorLspStringAppend(&sb, "file://")) {
		free(absolute_path);
		free(sb.buf);
		return 0;
	}

	for (const unsigned char *p = (const unsigned char *)absolute_path; *p != '\0'; p++) {
		unsigned char ch = *p;
		int unreserved = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
		if (unreserved) {
			if (!editorLspStringAppendBytes(&sb, (const char *)&ch, 1)) {
				free(absolute_path);
				free(sb.buf);
				return 0;
			}
		} else {
			if (!editorLspStringAppendf(&sb, "%%%02X", (unsigned int)ch)) {
				free(absolute_path);
				free(sb.buf);
				return 0;
			}
		}
	}

	free(absolute_path);
	*uri_out = sb.buf;
	return 1;
}

static int editorLspHexValue(char c) {
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

static char *editorLspDecodeFileUri(const char *uri) {
	if (uri == NULL || strncmp(uri, "file://", 7) != 0) {
		return NULL;
	}

	const char *rest = uri + 7;
	if (rest[0] != '/') {
		const char *slash = strchr(rest, '/');
		if (slash == NULL) {
			return NULL;
		}
		if ((size_t)(slash - rest) != strlen("localhost") ||
				strncasecmp(rest, "localhost", strlen("localhost")) != 0) {
			return NULL;
		}
		rest = slash;
	}

	size_t len = strlen(rest);
	char *path = malloc(len + 1);
	if (path == NULL) {
		return NULL;
	}

	size_t write_idx = 0;
	for (size_t i = 0; i < len; i++) {
		if (rest[i] == '%' && i + 2 < len) {
			int hi = editorLspHexValue(rest[i + 1]);
			int lo = editorLspHexValue(rest[i + 2]);
			if (hi >= 0 && lo >= 0) {
				path[write_idx++] = (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		path[write_idx++] = rest[i];
	}
	path[write_idx] = '\0';
	return path;
}


static int editorLspParseJsonString(const char *json, char **value_out, const char **after_out) {
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
					if (json[i + 1] == '\0' || json[i + 2] == '\0' || json[i + 3] == '\0' ||
							json[i + 4] == '\0') {
						free(sb.buf);
						return 0;
					}
					int h1 = editorLspHexValue(json[i + 1]);
					int h2 = editorLspHexValue(json[i + 2]);
					int h3 = editorLspHexValue(json[i + 3]);
					int h4 = editorLspHexValue(json[i + 4]);
					if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
						free(sb.buf);
						return 0;
					}
					unsigned int code = (unsigned int)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
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

static const char *editorLspSkipWs(const char *p) {
	while (p != NULL && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
		p++;
	}
	return p;
}

static int editorLspParseJsonInt(const char *json, int *value_out, const char **after_out) {
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

static int editorLspExtractResponseId(const char *json, int *id_out) {
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

static int editorLspResponseHasError(const char *json) {
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

static int editorLspFindStringField(const char *json, const char *field_name, char **value_out) {
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

static const char *editorLspStrstrBounded(const char *haystack, const char *needle,
		const char *limit) {
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

static const char *editorLspFindJsonObjectEnd(const char *object_start) {
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

static const char *editorLspFindJsonArrayEnd(const char *array_start) {
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

static int editorLspParsePositionFromKey(const char *range_json, const char *key_name,
		const char *limit, int *line_out, int *character_out) {
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

static int editorLspParsePositionFromStart(const char *range_json, const char *limit,
		int *line_out, int *character_out) {
	return editorLspParsePositionFromKey(range_json, "start", limit, line_out, character_out);
}

void editorLspFreeLocations(struct editorLspLocation *locations, int count) {
	if (locations == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(locations[i].path);
	}
	free(locations);
}

int editorLspProtocolCharacterToBufferColumn(int line, int protocol_character) {
	if (protocol_character < 0) {
		return 0;
	}
	if (!g_lsp_client.position_encoding_utf16) {
		return protocol_character;
	}

	char *line_text = NULL;
	size_t line_len = 0;
	if (!editorLspReadActiveLineText(line, &line_text, &line_len)) {
		return protocol_character;
	}
	int byte_column = editorLspUtf16UnitsToUtf8Column(line_text, line_len, protocol_character);
	free(line_text);
	return byte_column;
}

static int editorLspClientProtocolCharacterToBufferColumn(struct editorLspClient *client, int line,
		int protocol_character) {
	if (protocol_character < 0) {
		return 0;
	}
	if (client == NULL || !client->position_encoding_utf16) {
		return protocol_character;
	}

	char *line_text = NULL;
	size_t line_len = 0;
	if (!editorLspReadActiveLineText(line, &line_text, &line_len)) {
		return protocol_character;
	}
	int byte_column = editorLspUtf16UnitsToUtf8Column(line_text, line_len, protocol_character);
	free(line_text);
	return byte_column;
}

static int editorLspAppendLocation(struct editorLspLocation **locations, int *count, int *cap,
		const char *path, int line, int character) {
	if (locations == NULL || count == NULL || cap == NULL || path == NULL) {
		return 0;
	}
	if (*count >= *cap) {
		int new_cap = *cap > 0 ? *cap * 2 : 4;
		if (new_cap < *count + 1) {
			new_cap = *count + 1;
		}
		size_t bytes = 0;
		if (!editorSizeMul(sizeof(**locations), (size_t)new_cap, &bytes)) {
			return 0;
		}
		struct editorLspLocation *grown = realloc(*locations, bytes);
		if (grown == NULL) {
			return 0;
		}
		*locations = grown;
		*cap = new_cap;
	}

	char *path_dup = strdup(path);
	if (path_dup == NULL) {
		return 0;
	}
	(*locations)[*count].path = path_dup;
	(*locations)[*count].line = line;
	(*locations)[*count].character = character;
	(*count)++;
	return 1;
}

static int editorLspParseLocationObjects(const char *result_json, const char *uri_key,
		const char *range_key_primary, const char *range_key_fallback,
		struct editorLspLocation **locations_out, int *count_out) {
	struct editorLspLocation *locations = NULL;
	int count = 0;
	int cap = 0;

	char key_pattern[64];
	int key_written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", uri_key);
	if (key_written <= 0 || (size_t)key_written >= sizeof(key_pattern)) {
		return 0;
	}

	const char *scan = result_json;
	while (scan != NULL) {
		const char *object_start = strchr(scan, '{');
		if (object_start == NULL) {
			break;
		}
		const char *object_end = editorLspFindJsonObjectEnd(object_start);
		if (object_end == NULL) {
			editorLspFreeLocations(locations, count);
			return 0;
		}
		scan = object_end;

		const char *uri_key_pos = editorLspStrstrBounded(object_start, key_pattern, object_end);
		if (uri_key_pos == NULL) {
			continue;
		}

		const char *uri_colon = strchr(uri_key_pos, ':');
		if (uri_colon == NULL || uri_colon >= object_end) {
			continue;
		}
		const char *uri_value = editorLspSkipWs(uri_colon + 1);
		if (uri_value == NULL || uri_value[0] != '"') {
			continue;
		}

		char *uri = NULL;
		const char *after_uri = NULL;
		if (!editorLspParseJsonString(uri_value, &uri, &after_uri)) {
			continue;
		}
		(void)after_uri;

		char primary_pattern[64];
		int primary_written = snprintf(primary_pattern, sizeof(primary_pattern), "\"%s\"",
				range_key_primary);
		if (primary_written <= 0 || (size_t)primary_written >= sizeof(primary_pattern)) {
			free(uri);
			editorLspFreeLocations(locations, count);
			return 0;
		}

		const char *range_pos = editorLspStrstrBounded(object_start, primary_pattern, object_end);
		if (range_pos == NULL && range_key_fallback != NULL) {
			char fallback_pattern[64];
			int fallback_written = snprintf(fallback_pattern, sizeof(fallback_pattern), "\"%s\"",
					range_key_fallback);
			if (fallback_written <= 0 || (size_t)fallback_written >= sizeof(fallback_pattern)) {
				free(uri);
				editorLspFreeLocations(locations, count);
				return 0;
			}
			range_pos = editorLspStrstrBounded(object_start, fallback_pattern, object_end);
		}

		int line = -1;
		int character = -1;
		if (range_pos != NULL) {
			(void)editorLspParsePositionFromStart(range_pos, object_end, &line, &character);
		}

		if (line >= 0 && character >= 0) {
			char *path = editorLspDecodeFileUri(uri);
			if (path != NULL) {
				if (!editorLspAppendLocation(&locations, &count, &cap, path, line, character)) {
					free(path);
					free(uri);
					editorLspFreeLocations(locations, count);
					return 0;
				}
				free(path);
			}
		}

		free(uri);
	}

	*locations_out = locations;
	*count_out = count;
	return 1;
}

static int editorLspParseDefinitionLocations(const char *response_json,
		struct editorLspLocation **locations_out, int *count_out) {
	*locations_out = NULL;
	*count_out = 0;

	const char *result_key = strstr(response_json, "\"result\"");
	if (result_key == NULL) {
		return 0;
	}
	const char *result_colon = strchr(result_key, ':');
	if (result_colon == NULL) {
		return 0;
	}
	const char *result = editorLspSkipWs(result_colon + 1);
	if (result == NULL) {
		return 0;
	}
	if (strncmp(result, "null", 4) == 0) {
		return 1;
	}

	if (strstr(result, "\"targetUri\"") != NULL) {
		return editorLspParseLocationObjects(result, "targetUri", "targetSelectionRange",
				"targetRange", locations_out, count_out);
	}
	return editorLspParseLocationObjects(result, "uri", "range", NULL, locations_out, count_out);
}

static void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count) {
	if (diagnostics == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(diagnostics[i].message);
	}
	free(diagnostics);
}

static int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
		const struct editorLspDiagnostic *diagnostics, int count) {
	if (out_diagnostics == NULL || out_count == NULL) {
		return 0;
	}
	*out_diagnostics = NULL;
	*out_count = 0;
	if (diagnostics == NULL || count <= 0) {
		return 1;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(*diagnostics), (size_t)count, &bytes)) {
		return 0;
	}
	struct editorLspDiagnostic *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		copy[i].start_line = diagnostics[i].start_line;
		copy[i].start_character = diagnostics[i].start_character;
		copy[i].end_line = diagnostics[i].end_line;
		copy[i].end_character = diagnostics[i].end_character;
		copy[i].severity = diagnostics[i].severity;
		if (diagnostics[i].message != NULL) {
			copy[i].message = strdup(diagnostics[i].message);
			if (copy[i].message == NULL) {
				editorLspFreeDiagnostics(copy, count);
				return 0;
			}
		}
	}

	*out_diagnostics = copy;
	*out_count = count;
	return 1;
}

static int editorLspDiagnosticsErrorCount(const struct editorLspDiagnostic *diagnostics, int count) {
	int errors = 0;
	for (int i = 0; i < count; i++) {
		if (diagnostics[i].severity == 1) {
			errors++;
		}
	}
	return errors;
}

static int editorLspDiagnosticsWarningCount(const struct editorLspDiagnostic *diagnostics, int count) {
	int warnings = 0;
	for (int i = 0; i < count; i++) {
		if (diagnostics[i].severity == 2) {
			warnings++;
		}
	}
	return warnings;
}

static int editorLspPathMatches(const char *left, const char *right) {
	return left != NULL && right != NULL && editorPathsReferToSameFile(left, right);
}

static void editorLspUpdateDiagnosticFields(struct editorLspDiagnostic **diagnostics_in_out,
		int *count_in_out, int *error_count_out, int *warning_count_out,
		const struct editorLspDiagnostic *diagnostics, int count) {
	editorLspFreeDiagnostics(*diagnostics_in_out, *count_in_out);
	*diagnostics_in_out = NULL;
	*count_in_out = 0;
	*error_count_out = 0;
	*warning_count_out = 0;
	if (diagnostics == NULL || count <= 0) {
		return;
	}
	if (!editorLspCopyDiagnostics(diagnostics_in_out, count_in_out, diagnostics, count)) {
		return;
	}
	*error_count_out = editorLspDiagnosticsErrorCount(*diagnostics_in_out, *count_in_out);
	*warning_count_out = editorLspDiagnosticsWarningCount(*diagnostics_in_out, *count_in_out);
}

static void editorLspSetDiagnosticsForPath(const char *path,
		const struct editorLspDiagnostic *diagnostics, int count) {
	if (path == NULL || path[0] == '\0') {
		return;
	}

	int active_matches = editorLspPathMatches(path, E.filename);
	int old_count = active_matches ? E.lsp_diagnostic_count : 0;
	int old_errors = active_matches ? E.lsp_diagnostic_error_count : 0;
	int old_warnings = active_matches ? E.lsp_diagnostic_warning_count : 0;

	if (active_matches) {
		editorLspUpdateDiagnosticFields(&E.lsp_diagnostics, &E.lsp_diagnostic_count,
				&E.lsp_diagnostic_error_count, &E.lsp_diagnostic_warning_count,
				diagnostics, count);
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (!editorLspPathMatches(path, E.tabs[i].filename)) {
			continue;
		}
		editorLspUpdateDiagnosticFields(&E.tabs[i].lsp_diagnostics,
				&E.tabs[i].lsp_diagnostic_count, &E.tabs[i].lsp_diagnostic_error_count,
				&E.tabs[i].lsp_diagnostic_warning_count, diagnostics, count);
	}

	if (active_matches &&
			(old_count != E.lsp_diagnostic_count ||
			 old_errors != E.lsp_diagnostic_error_count ||
			 old_warnings != E.lsp_diagnostic_warning_count)) {
		if (E.lsp_diagnostic_count == 0) {
			editorSetStatusMsg("ESLint: diagnostics cleared");
		} else {
			editorSetStatusMsg("ESLint: %d error%s, %d warning%s",
					E.lsp_diagnostic_error_count,
					E.lsp_diagnostic_error_count == 1 ? "" : "s",
					E.lsp_diagnostic_warning_count,
					E.lsp_diagnostic_warning_count == 1 ? "" : "s");
		}
	}
}

void editorLspClearDiagnosticsForFile(const char *filename) {
	editorLspSetDiagnosticsForPath(filename, NULL, 0);
}

void editorLspGetDiagnosticSummaryForFile(const char *filename,
		struct editorLspDiagnosticSummary *summary_out) {
	if (summary_out == NULL) {
		return;
	}
	memset(summary_out, 0, sizeof(*summary_out));
	if (editorLspPathMatches(filename, E.filename)) {
		summary_out->count = E.lsp_diagnostic_count;
		summary_out->error_count = E.lsp_diagnostic_error_count;
		summary_out->warning_count = E.lsp_diagnostic_warning_count;
		return;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (!editorLspPathMatches(filename, E.tabs[i].filename)) {
			continue;
		}
		summary_out->count = E.tabs[i].lsp_diagnostic_count;
		summary_out->error_count = E.tabs[i].lsp_diagnostic_error_count;
		summary_out->warning_count = E.tabs[i].lsp_diagnostic_warning_count;
		return;
	}
}

static int editorLspParseDiagnosticsMessage(const char *message, char **path_out,
		struct editorLspDiagnostic **diagnostics_out, int *count_out) {
	if (path_out == NULL || diagnostics_out == NULL || count_out == NULL) {
		return 0;
	}
	*path_out = NULL;
	*diagnostics_out = NULL;
	*count_out = 0;

	char *method = NULL;
	if (!editorLspFindStringField(message, "method", &method)) {
		return 0;
	}
	int is_publish = strcmp(method, "textDocument/publishDiagnostics") == 0;
	free(method);
	if (!is_publish) {
		return 0;
	}

	const char *params_key = strstr(message, "\"params\"");
	if (params_key == NULL) {
		return 0;
	}
	const char *params_colon = strchr(params_key, ':');
	if (params_colon == NULL) {
		return 0;
	}
	const char *params_object = strchr(params_colon + 1, '{');
	if (params_object == NULL) {
		return 0;
	}
	const char *params_end = editorLspFindJsonObjectEnd(params_object);
	if (params_end == NULL) {
		return 0;
	}

	char *uri = NULL;
	if (!editorLspFindStringField(params_object, "uri", &uri) || uri == NULL) {
		free(uri);
		return 0;
	}
	char *path = editorLspDecodeFileUri(uri);
	free(uri);
	if (path == NULL) {
		return 0;
	}

	const char *diag_key = editorLspStrstrBounded(params_object, "\"diagnostics\"", params_end);
	if (diag_key == NULL) {
		free(path);
		return 0;
	}
	const char *diag_colon = strchr(diag_key, ':');
	if (diag_colon == NULL || diag_colon >= params_end) {
		free(path);
		return 0;
	}
	const char *diag_array = strchr(diag_colon + 1, '[');
	if (diag_array == NULL || diag_array >= params_end) {
		free(path);
		return 0;
	}
	const char *diag_array_end = editorLspFindJsonArrayEnd(diag_array);
	if (diag_array_end == NULL || diag_array_end > params_end) {
		free(path);
		return 0;
	}

	struct editorLspDiagnostic *diagnostics = NULL;
	int count = 0;
	int cap = 0;
	const char *scan = diag_array + 1;
	while (scan < diag_array_end) {
		const char *object_start = strchr(scan, '{');
		if (object_start == NULL || object_start >= diag_array_end) {
			break;
		}
		const char *object_end = editorLspFindJsonObjectEnd(object_start);
		if (object_end == NULL || object_end > diag_array_end) {
			editorLspFreeDiagnostics(diagnostics, count);
			free(path);
			return 0;
		}
		scan = object_end;

		const char *range_key = editorLspStrstrBounded(object_start, "\"range\"", object_end);
		if (range_key == NULL) {
			continue;
		}
		const char *range_colon = strchr(range_key, ':');
		if (range_colon == NULL || range_colon >= object_end) {
			continue;
		}
		const char *range_object = strchr(range_colon + 1, '{');
		if (range_object == NULL || range_object >= object_end) {
			continue;
		}
		const char *range_end = editorLspFindJsonObjectEnd(range_object);
		if (range_end == NULL || range_end > object_end) {
			continue;
		}

		int start_line = 0;
		int start_character = 0;
		int end_line = 0;
		int end_character = 0;
		if (!editorLspParsePositionFromKey(range_object, "start", range_end, &start_line,
					&start_character) ||
				!editorLspParsePositionFromKey(range_object, "end", range_end, &end_line,
						&end_character)) {
			continue;
		}

		int severity = 1;
		const char *severity_key =
				editorLspStrstrBounded(object_start, "\"severity\"", object_end);
		if (severity_key != NULL) {
			const char *severity_colon = strchr(severity_key, ':');
			int parsed_severity = 0;
			if (severity_colon != NULL &&
					editorLspParseJsonInt(severity_colon + 1, &parsed_severity, NULL)) {
				severity = parsed_severity;
			}
		}

		char *msg = NULL;
		if (!editorLspFindStringField(object_start, "message", &msg) || msg == NULL) {
			msg = strdup("");
			if (msg == NULL) {
				editorLspFreeDiagnostics(diagnostics, count);
				free(path);
				return 0;
			}
		}

		if (count >= cap) {
			int new_cap = cap > 0 ? cap * 2 : 4;
			size_t bytes = 0;
			if (!editorSizeMul(sizeof(*diagnostics), (size_t)new_cap, &bytes)) {
				free(msg);
				editorLspFreeDiagnostics(diagnostics, count);
				free(path);
				return 0;
			}
			struct editorLspDiagnostic *grown = realloc(diagnostics, bytes);
			if (grown == NULL) {
				free(msg);
				editorLspFreeDiagnostics(diagnostics, count);
				free(path);
				return 0;
			}
			diagnostics = grown;
			cap = new_cap;
		}
		diagnostics[count].start_line = start_line;
		diagnostics[count].start_character = start_character;
		diagnostics[count].end_line = end_line;
		diagnostics[count].end_character = end_character;
		diagnostics[count].severity = severity;
		diagnostics[count].message = msg;
		count++;
	}

	*path_out = path;
	*diagnostics_out = diagnostics;
	*count_out = count;
	return 1;
}

static void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count) {
	if (edits == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(edits[i].new_text);
	}
	free(edits);
}

static int editorLspPendingEditCompareDesc(const void *lhs, const void *rhs) {
	const struct editorLspPendingEdit *left = lhs;
	const struct editorLspPendingEdit *right = rhs;
	if (left->start_line != right->start_line) {
		return right->start_line - left->start_line;
	}
	return right->start_character - left->start_character;
}

static int editorLspApplyPendingEditsWithClient(struct editorLspClient *client,
		const struct editorLspPendingEdit *edits, int count) {
	if (edits == NULL || count <= 0) {
		return 0;
	}

	struct editorLspPendingEdit *sorted = calloc((size_t)count, sizeof(*sorted));
	if (sorted == NULL) {
		return -1;
	}
	for (int i = 0; i < count; i++) {
		sorted[i] = edits[i];
	}
	qsort(sorted, (size_t)count, sizeof(*sorted), editorLspPendingEditCompareDesc);

	for (int i = 0; i < count; i++) {
		int start_cx = editorLspClientProtocolCharacterToBufferColumn(client,
				sorted[i].start_line, sorted[i].start_character);
		int end_cx = editorLspClientProtocolCharacterToBufferColumn(client,
				sorted[i].end_line, sorted[i].end_character);
		size_t start_offset = 0;
		size_t end_offset = 0;
		if (!editorBufferPosToOffset(sorted[i].start_line, start_cx, &start_offset) ||
				!editorBufferPosToOffset(sorted[i].end_line, end_cx, &end_offset) ||
				end_offset < start_offset) {
			free(sorted);
			return -1;
		}

		size_t cursor_before = E.cursor_offset;
		size_t cursor_after = cursor_before;
		size_t new_len = sorted[i].new_text != NULL ? strlen(sorted[i].new_text) : 0;
		size_t old_len = end_offset - start_offset;
		if (cursor_before > start_offset) {
			if (cursor_before <= end_offset) {
				cursor_after = start_offset + new_len;
			} else {
				cursor_after = start_offset + new_len + (cursor_before - end_offset);
			}
		}

		struct editorDocumentEdit edit = {
			.kind = old_len > 0 ? EDITOR_EDIT_DELETE_TEXT : EDITOR_EDIT_INSERT_TEXT,
			.start_offset = start_offset,
			.old_len = old_len,
			.new_text = sorted[i].new_text != NULL ? sorted[i].new_text : "",
			.new_len = new_len,
			.before_cursor_offset = cursor_before,
			.after_cursor_offset = cursor_after,
			.before_dirty = E.dirty,
			.after_dirty = E.dirty + 1,
		};
		if (!editorApplyDocumentEdit(&edit)) {
			free(sorted);
			return -1;
		}
	}

	free(sorted);
	return count;
}

static int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count) {
	return editorLspApplyPendingEditsWithClient(&g_lsp_client, edits, count);
}

static int editorLspParseWorkspaceEditChanges(const char *edit_json, const char *target_path,
		struct editorLspPendingEdit **edits_out, int *count_out) {
	if (edits_out == NULL || count_out == NULL) {
		return 0;
	}
	*edits_out = NULL;
	*count_out = 0;

	const char *changes_key = strstr(edit_json, "\"changes\"");
	if (changes_key == NULL) {
		return 1;
	}
	const char *changes_colon = strchr(changes_key, ':');
	if (changes_colon == NULL) {
		return 0;
	}
	const char *changes_object = strchr(changes_colon + 1, '{');
	if (changes_object == NULL) {
		return 0;
	}
	const char *changes_end = editorLspFindJsonObjectEnd(changes_object);
	if (changes_end == NULL) {
		return 0;
	}

	const char *scan = changes_object + 1;
	while (scan < changes_end) {
		const char *key = strchr(scan, '"');
		if (key == NULL || key >= changes_end) {
			break;
		}
		char *uri = NULL;
		const char *after_key = NULL;
		if (!editorLspParseJsonString(key, &uri, &after_key) || uri == NULL) {
			return 0;
		}
		const char *colon = strchr(after_key, ':');
		if (colon == NULL || colon >= changes_end) {
			free(uri);
			return 0;
		}
		const char *array_start = strchr(colon + 1, '[');
		if (array_start == NULL || array_start >= changes_end) {
			free(uri);
			return 0;
		}
		const char *array_end = editorLspFindJsonArrayEnd(array_start);
		if (array_end == NULL || array_end > changes_end) {
			free(uri);
			return 0;
		}

		char *path = editorLspDecodeFileUri(uri);
		free(uri);
		if (path != NULL && editorLspPathMatches(path, target_path)) {
			struct editorLspPendingEdit *edits = NULL;
			int count = 0;
			int cap = 0;
			const char *item_scan = array_start + 1;
			while (item_scan < array_end) {
				const char *object_start = strchr(item_scan, '{');
				if (object_start == NULL || object_start >= array_end) {
					break;
				}
				const char *object_end = editorLspFindJsonObjectEnd(object_start);
				if (object_end == NULL || object_end > array_end) {
					editorLspFreePendingEdits(edits, count);
					free(path);
					return 0;
				}
				item_scan = object_end;

				const char *range_key =
						editorLspStrstrBounded(object_start, "\"range\"", object_end);
				const char *range_colon = range_key != NULL ? strchr(range_key, ':') : NULL;
				const char *range_object =
						range_colon != NULL ? strchr(range_colon + 1, '{') : NULL;
				const char *range_end =
						range_object != NULL ? editorLspFindJsonObjectEnd(range_object) : NULL;
				char *new_text = NULL;
				if (!editorLspFindStringField(object_start, "newText", &new_text) ||
						new_text == NULL) {
					new_text = strdup("");
				}
				if (new_text == NULL) {
					editorLspFreePendingEdits(edits, count);
					free(path);
					return 0;
				}

				int start_line = 0;
				int start_character = 0;
				int end_line = 0;
				int end_character = 0;
				if (range_end != NULL &&
						editorLspParsePositionFromKey(range_object, "start", range_end,
								&start_line, &start_character) &&
						editorLspParsePositionFromKey(range_object, "end", range_end,
								&end_line, &end_character)) {
					if (count >= cap) {
						int new_cap = cap > 0 ? cap * 2 : 4;
						size_t bytes = 0;
						if (!editorSizeMul(sizeof(*edits), (size_t)new_cap, &bytes)) {
							free(new_text);
							editorLspFreePendingEdits(edits, count);
							free(path);
							return 0;
						}
						struct editorLspPendingEdit *grown = realloc(edits, bytes);
						if (grown == NULL) {
							free(new_text);
							editorLspFreePendingEdits(edits, count);
							free(path);
							return 0;
						}
						edits = grown;
						cap = new_cap;
					}
					edits[count].start_line = start_line;
					edits[count].start_character = start_character;
					edits[count].end_line = end_line;
					edits[count].end_character = end_character;
					edits[count].new_text = new_text;
					count++;
				} else {
					free(new_text);
				}
			}
			free(path);
			*edits_out = edits;
			*count_out = count;
			return 1;
		}
		free(path);
		scan = array_end;
	}

	return 1;
}


static int editorLspRespondToRequest(struct editorLspClient *client, int request_id,
		const char *result_json) {
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(&payload,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":", request_id);
	if (built) {
		built = editorLspStringAppend(&payload, result_json != NULL ? result_json : "null");
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}");
	}
	if (!built) {
		free(payload.buf);
		return 0;
	}
	int sent = editorLspSendRawJsonToFd(client != NULL ? client->to_server_fd : -1, payload.buf);
	free(payload.buf);
	return sent;
}

static int editorLspProcessIncomingMessage(struct editorLspClient *client, const char *message) {
	if (message == NULL || message[0] == '\0') {
		return 1;
	}

	char *path = NULL;
	struct editorLspDiagnostic *diagnostics = NULL;
	int diagnostic_count = 0;
	if (editorLspParseDiagnosticsMessage(message, &path, &diagnostics, &diagnostic_count)) {
		editorLspSetDiagnosticsForPath(path, diagnostics, diagnostic_count);
		editorLspFreeDiagnostics(diagnostics, diagnostic_count);
		free(path);
		return 1;
	}

	char *method = NULL;
	if (!editorLspFindStringField(message, "method", &method) || method == NULL) {
		free(method);
		return 1;
	}

	int request_id = 0;
	int has_request_id = editorLspExtractResponseId(message, &request_id);
	if (strcmp(method, "workspace/configuration") == 0) {
		free(method);
		if (has_request_id) {
			return editorLspRespondToRequest(client, request_id, "[{}]");
		}
		return 1;
	}
	if (strcmp(method, "client/registerCapability") == 0) {
		free(method);
		if (has_request_id) {
			return editorLspRespondToRequest(client, request_id, "null");
		}
		return 1;
	}
	if (strcmp(method, "workspace/applyEdit") == 0) {
		free(method);
		if (!has_request_id) {
			return 1;
		}
		struct editorLspPendingEdit *edits = NULL;
		int count = 0;
		int parsed = editorLspParseWorkspaceEditChanges(message, E.filename, &edits, &count);
		int applied =
				parsed && count > 0 && editorLspApplyPendingEditsWithClient(client, edits, count) >= 0;
		editorLspFreePendingEdits(edits, count);
		return editorLspRespondToRequest(client, request_id, applied ? "{\"applied\":true}" :
				"{\"applied\":false}");
	}

	free(method);
	if (has_request_id) {
		return editorLspRespondToRequest(client, request_id, "null");
	}
	return 1;
}


static int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column) {
	if (text == NULL || byte_column <= 0) {
		return 0;
	}

	int text_len_int = 0;
	if (!editorSizeToInt(text_len, &text_len_int)) {
		text_len_int = INT_MAX;
	}
	int clamped = byte_column;
	if (clamped < 0) {
		clamped = 0;
	}
	if (clamped > text_len_int) {
		clamped = text_len_int;
	}
	while (clamped > 0 && clamped < text_len_int &&
			editorIsUtf8ContinuationByte((unsigned char)text[clamped])) {
		clamped--;
	}

	int utf16_units = 0;
	for (int idx = 0; idx < clamped;) {
		unsigned int cp = 0;
		int seq_len = editorUtf8DecodeCodepoint(text + idx, text_len_int - idx, &cp);
		if (seq_len <= 0) {
			seq_len = 1;
			cp = (unsigned char)text[idx];
		}
		utf16_units += (cp > 0xFFFFU) ? 2 : 1;
		idx += seq_len;
	}

	return utf16_units;
}

static int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units) {
	if (text == NULL || utf16_units <= 0) {
		return 0;
	}

	int text_len_int = 0;
	if (!editorSizeToInt(text_len, &text_len_int)) {
		text_len_int = INT_MAX;
	}
	int units = 0;
	int idx = 0;
	while (idx < text_len_int) {
		unsigned int cp = 0;
		int seq_len = editorUtf8DecodeCodepoint(text + idx, text_len_int - idx, &cp);
		if (seq_len <= 0) {
			seq_len = 1;
			cp = (unsigned char)text[idx];
		}

		int cp_units = (cp > 0xFFFFU) ? 2 : 1;
		if (units + cp_units > utf16_units) {
			break;
		}
		units += cp_units;
		idx += seq_len;
	}

	return idx;
}

static int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out) {
	if (text_out == NULL || len_out == NULL || line < 0) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	size_t line_start = 0;
	size_t line_end = 0;
	if (!editorBufferLineByteRange(line, &line_start, &line_end)) {
		return 0;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	*text_out = editorTextSourceDupRange(&source, line_start, line_end, len_out);
	if (*text_out == NULL) {
		return line_end == line_start;
	}
	return 1;
}

static int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column) {
	if (byte_column < 0) {
		byte_column = 0;
	}
	if (!g_lsp_client.position_encoding_utf16) {
		return byte_column;
	}
	char *line_text = NULL;
	size_t line_len = 0;
	if (!editorLspReadActiveLineText(line, &line_text, &line_len)) {
		return byte_column;
	}
	int protocol_character = editorLspUtf8ColumnToUtf16Units(line_text, line_len, byte_column);
	free(line_text);
	return protocol_character;
}

static int editorLspClientProtocolCharacterFromBufferColumn(struct editorLspClient *client, int line,
		int byte_column) {
	if (client == NULL || byte_column < 0) {
		byte_column = 0;
	}
	if (client == NULL || !client->position_encoding_utf16) {
		return byte_column;
	}
	char *line_text = NULL;
	size_t line_len = 0;
	if (!editorLspReadActiveLineText(line, &line_text, &line_len)) {
		return byte_column;
	}
	int protocol_character = editorLspUtf8ColumnToUtf16Units(line_text, line_len, byte_column);
	free(line_text);
	return protocol_character;
}


static int editorLspCopyLocations(struct editorLspLocation **out_locations, int *out_count,
		const struct editorLspLocation *locations, int count) {
	*out_locations = NULL;
	*out_count = 0;
	if (count <= 0) {
		return 1;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(struct editorLspLocation), (size_t)count, &bytes)) {
		return 0;
	}
	struct editorLspLocation *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		copy[i].line = locations[i].line;
		copy[i].character = locations[i].character;
		if (locations[i].path != NULL) {
			copy[i].path = strdup(locations[i].path);
			if (copy[i].path == NULL) {
				editorLspFreeLocations(copy, count);
				return 0;
			}
		}
	}

	*out_locations = copy;
	*out_count = count;
	return 1;
}


