#include "debug/dap_protocol.h"

#include "debug/dap.h"
#include "support/file_io.h"
#include "support/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int dapProtocolAppendJsonEscapedRaw(struct editorJsonString *sb, const char *text,
                                           size_t len) {
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
				} else if (!editorJsonStringAppendf(sb, "%c", ch)) {
					return 0;
				}
				break;
		}
	}
	return 1;
}

static int dapProtocolAppendJsonString(struct editorJsonString *sb, const char *text) {
	const char *safe = text != NULL ? text : "";
	return editorJsonStringAppend(sb, "\"") &&
	       dapProtocolAppendJsonEscapedRaw(sb, safe, strlen(safe)) &&
	       editorJsonStringAppend(sb, "\"");
}

char *editorDapBuildInitializeRequestJson(int seq, const char *adapter_id) {
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"initialize\","
	            "\"arguments\":{\"clientID\":\"rotide\",\"clientName\":\"RotIDE\","
	            "\"adapterID\":",
	            seq) ||
	    !dapProtocolAppendJsonString(&sb, adapter_id) ||
	    !editorJsonStringAppend(&sb, ",\"pathFormat\":\"path\",\"linesStartAt1\":true,"
	                                 "\"columnsStartAt1\":true,"
	                                 "\"supportsVariableType\":true,"
	                                 "\"supportsMemoryReferences\":true,"
	                                 "\"supportsVariablePaging\":true}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildSimpleCommandRequestJson(int seq, const char *command) {
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(&sb, "{\"seq\":%d,\"type\":\"request\",\"command\":", seq) ||
	    !dapProtocolAppendJsonString(&sb, command) || !editorJsonStringAppend(&sb, "}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildEvaluateRequestJson(int seq, const char *expr, int frame_id,
                                        const char *context) {
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"evaluate\",\"arguments\":{"
	            "\"expression\":",
	            seq) ||
	    !dapProtocolAppendJsonString(&sb, expr) ||
	    !editorJsonStringAppend(&sb, ",\"context\":") ||
	    !dapProtocolAppendJsonString(&sb, context != NULL ? context : "repl")) {
		free(sb.buf);
		return NULL;
	}
	if (frame_id > 0 && !editorJsonStringAppendf(&sb, ",\"frameId\":%d", frame_id)) {
		free(sb.buf);
		return NULL;
	}
	if (!editorJsonStringAppend(&sb, "}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

void editorDapBuildAdapterCommand(const char *base, const char *tty_path, char *out,
                                  size_t out_size) {
	if (out == NULL || out_size == 0) {
		return;
	}
	if (tty_path != NULL && tty_path[0] != '\0' && base != NULL &&
	    strstr(base, "gdb") != NULL) {
		(void)snprintf(out, out_size, "%s --tty=%s", base, tty_path);
	} else {
		(void)snprintf(out, out_size, "%s", base != NULL ? base : "");
	}
}

static int dapProtocolAppendSubstitutedString(struct editorJsonString *sb, const char *value,
                                              const char *workspace_root, const char *active_file) {
	char *file_dir = active_file != NULL ? editorPathDirnameDup(active_file) : NULL;
	char *file_base = active_file != NULL ? editorPathBasenameDup(active_file) : NULL;
	const char *p = value != NULL ? value : "";
	while (*p != '\0') {
		const char *replacement = NULL;
		size_t token_len = 0;
		if (strncmp(p, "${workspaceFolder}", 18) == 0) {
			replacement = workspace_root != NULL ? workspace_root : "";
			token_len = 18;
		} else if (strncmp(p, "${fileDirname}", 14) == 0) {
			replacement = file_dir != NULL ? file_dir : "";
			token_len = 14;
		} else if (strncmp(p, "${fileBasename}", 15) == 0) {
			replacement = file_base != NULL ? file_base : "";
			token_len = 15;
		} else if (strncmp(p, "${file}", 7) == 0) {
			replacement = active_file != NULL ? active_file : "";
			token_len = 7;
		}
		if (replacement != NULL) {
			if (!dapProtocolAppendJsonEscapedRaw(sb, replacement,
			                                     strlen(replacement))) {
				free(file_dir);
				free(file_base);
				return 0;
			}
			p += token_len;
			continue;
		}
		if (!dapProtocolAppendJsonEscapedRaw(sb, p, 1)) {
			free(file_dir);
			free(file_base);
			return 0;
		}
		p++;
	}
	free(file_dir);
	free(file_base);
	return 1;
}

static int dapProtocolAppendLaunchFieldJson(struct editorJsonString *sb,
                                            const struct editorDapLaunchField *field,
                                            const char *workspace_root, const char *active_file) {
	if (!dapProtocolAppendJsonString(sb, field->key) || !editorJsonStringAppend(sb, ":")) {
		return 0;
	}
	switch (field->kind) {
		case EDITOR_DAP_LAUNCH_VALUE_STRING:
			return editorJsonStringAppend(sb, "\"") &&
			       dapProtocolAppendSubstitutedString(sb, field->string_value,
			                                          workspace_root, active_file) &&
			       editorJsonStringAppend(sb, "\"");
		case EDITOR_DAP_LAUNCH_VALUE_BOOL:
			return editorJsonStringAppend(sb, field->bool_value ? "true" : "false");
		case EDITOR_DAP_LAUNCH_VALUE_INT:
			return editorJsonStringAppendf(sb, "%d", field->int_value);
		case EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY:
			if (!editorJsonStringAppend(sb, "[")) {
				return 0;
			}
			for (int i = 0; i < field->array_count; i++) {
				if (i > 0 && !editorJsonStringAppend(sb, ",")) {
					return 0;
				}
				if (!editorJsonStringAppend(sb, "\"") ||
				    !dapProtocolAppendSubstitutedString(sb, field->array_values[i],
				                                        workspace_root,
				                                        active_file) ||
				    !editorJsonStringAppend(sb, "\"")) {
					return 0;
				}
			}
			return editorJsonStringAppend(sb, "]");
	}
	return 0;
}

char *editorDapBuildLaunchRequestJson(int seq, const struct editorDapLaunchConfig *config,
                                      const char *workspace_root, const char *active_file) {
	if (config == NULL) {
		return NULL;
	}
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(
	            &sb, "{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\",\"arguments\":{", seq,
	            config->request[0] != '\0' ? config->request : "launch")) {
		free(sb.buf);
		return NULL;
	}
	int wrote_any = 0;
	for (int i = 0; i < config->field_count; i++) {
		if (wrote_any && !editorJsonStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!dapProtocolAppendLaunchFieldJson(&sb, &config->fields[i], workspace_root,
		                                      active_file)) {
			free(sb.buf);
			return NULL;
		}
		wrote_any = 1;
	}
	if (config->env_count > 0) {
		if (wrote_any && !editorJsonStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!editorJsonStringAppend(&sb, "\"env\":{")) {
			free(sb.buf);
			return NULL;
		}
		for (int i = 0; i < config->env_count; i++) {
			if (i > 0 && !editorJsonStringAppend(&sb, ",")) {
				free(sb.buf);
				return NULL;
			}
			if (!dapProtocolAppendJsonString(&sb, config->env[i].key) ||
			    !editorJsonStringAppend(&sb, ":\"") ||
			    !dapProtocolAppendSubstitutedString(&sb, config->env[i].value,
			                                        workspace_root, active_file) ||
			    !editorJsonStringAppend(&sb, "\"")) {
				free(sb.buf);
				return NULL;
			}
		}
		if (!editorJsonStringAppend(&sb, "}")) {
			free(sb.buf);
			return NULL;
		}
	}
	if (!editorJsonStringAppend(&sb, "}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildSetBreakpointsRequestJson(int seq, const char *path,
                                              const struct editorDapBreakpoint *breakpoints,
                                              int breakpoint_count) {
	char *absolute = editorPathAbsoluteDup(path);
	const char *source_path = absolute != NULL ? absolute : path;
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
	            "\"arguments\":{\"source\":{\"path\":",
	            seq) ||
	    !dapProtocolAppendJsonString(&sb, source_path) ||
	    !editorJsonStringAppend(&sb, "},\"breakpoints\":[")) {
		free(absolute);
		free(sb.buf);
		return NULL;
	}
	free(absolute);
	int wrote = 0;
	for (int i = 0; i < breakpoint_count; i++) {
		if (strcmp(breakpoints[i].path, path) != 0) {
			continue;
		}
		if (wrote && !editorJsonStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!editorJsonStringAppendf(&sb, "{\"line\":%d}", breakpoints[i].line + 1)) {
			free(sb.buf);
			return NULL;
		}
		wrote = 1;
	}
	if (!editorJsonStringAppend(&sb, "]}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildIntArgRequestJson(int seq, const char *command, const char *arg_key,
                                      int arg_value) {
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(&sb,
	                             "{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\","
	                             "\"arguments\":{\"%s\":%d}}",
	                             seq, command, arg_key, arg_value)) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildVariablesRequestJson(int seq, int variables_reference, int start, int count) {
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(&sb,
	                             "{\"seq\":%d,\"type\":\"request\",\"command\":\"variables\","
	                             "\"arguments\":{\"variablesReference\":%d",
	                             seq, variables_reference)) {
		free(sb.buf);
		return NULL;
	}
	if (start > 0 && !editorJsonStringAppendf(&sb, ",\"start\":%d", start)) {
		free(sb.buf);
		return NULL;
	}
	if (count > 0 && !editorJsonStringAppendf(&sb, ",\"count\":%d", count)) {
		free(sb.buf);
		return NULL;
	}
	if (!editorJsonStringAppend(&sb, "}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

int editorDapJsonStringField(const char *json, const char *field, char *buf, size_t bufsize) {
	char *value = NULL;
	if (!editorJsonFindStringField(json, field, &value) || value == NULL) {
		return 0;
	}
	int ok = snprintf(buf, bufsize, "%s", value) >= 0 && strlen(value) < bufsize;
	free(value);
	return ok;
}

int editorDapJsonResponseSucceeded(const char *json) {
	const char *start = editorJsonSkipWs(json);
	if (start == NULL || *start != '{') {
		return 1;
	}
	const char *obj_end = editorJsonFindObjectEnd(start);
	const char *key = editorJsonFindTopLevelKey(start, obj_end, "\"success\"");
	if (key == NULL) {
		return 1;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL) {
		return 1;
	}
	return strncmp(editorJsonSkipWs(colon + 1), "false", 5) != 0;
}

int editorDapJsonObjectIntField(const char *start, const char *end, const char *quoted_key,
                                int *out) {
	const char *key = editorJsonFindTopLevelKey(start, end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= end) {
		return 0;
	}
	return editorJsonParseInt(editorJsonSkipWs(colon + 1), out, NULL);
}

int editorDapJsonObjectStringField(const char *start, const char *end, const char *quoted_key,
                                   char *buf, size_t bufsize) {
	const char *key = editorJsonFindTopLevelKey(start, end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= end) {
		return 0;
	}
	char *value = NULL;
	if (!editorJsonParseString(editorJsonSkipWs(colon + 1), &value, NULL) || value == NULL) {
		return 0;
	}
	int ok = strlen(value) < bufsize;
	if (ok) {
		memcpy(buf, value, strlen(value) + 1);
	}
	free(value);
	return ok;
}

int editorDapJsonObjectChildObject(const char *start, const char *end, const char *quoted_key,
                                   const char **child_start, const char **child_end) {
	const char *key = editorJsonFindTopLevelKey(start, end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= end) {
		return 0;
	}
	const char *child = editorJsonSkipWs(colon + 1);
	if (child == NULL || child[0] != '{') {
		return 0;
	}
	const char *child_obj_end = editorJsonFindObjectEnd(child);
	if (child_obj_end == NULL) {
		return 0;
	}
	*child_start = child;
	*child_end = child_obj_end;
	return 1;
}

int editorDapJsonBodyChildObject(const char *message, const char *quoted_key,
                                 const char **child_start, const char **child_end) {
	const char *start = editorJsonSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !editorDapJsonObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"",
	                                    &body_start, &body_end)) {
		return 0;
	}
	return editorDapJsonObjectChildObject(body_start, body_end, quoted_key, child_start,
	                                      child_end);
}

static int dapProtocolFindBodyArray(const char *message, const char *quoted_key,
                                    const char **array_start, const char **array_end) {
	const char *start = editorJsonSkipWs(message);
	if (start == NULL || start[0] != '{') {
		return 0;
	}
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (!editorDapJsonObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"",
	                                    &body_start, &body_end)) {
		return 0;
	}
	const char *key = editorJsonFindTopLevelKey(body_start, body_end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= body_end) {
		return 0;
	}
	const char *arr = editorJsonSkipWs(colon + 1);
	if (arr == NULL || arr[0] != '[') {
		return 0;
	}
	const char *arr_end = editorJsonFindArrayEnd(arr);
	if (arr_end == NULL) {
		return 0;
	}
	*array_start = arr;
	*array_end = arr_end;
	return 1;
}

int editorDapJsonBodyIntField(const char *message, const char *quoted_key, int *out) {
	const char *start = editorJsonSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !editorDapJsonObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"",
	                                    &body_start, &body_end)) {
		return 0;
	}
	return editorDapJsonObjectIntField(body_start, body_end, quoted_key, out);
}

int editorDapJsonTopLevelIntField(const char *message, const char *quoted_key, int *out) {
	const char *start = editorJsonSkipWs(message);
	if (start == NULL || start[0] != '{') {
		return 0;
	}
	return editorDapJsonObjectIntField(start, editorJsonFindObjectEnd(start), quoted_key, out);
}

int editorDapJsonBodyStringField(const char *message, const char *quoted_key, char *buf,
                                 size_t bufsize) {
	const char *start = editorJsonSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !editorDapJsonObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"",
	                                    &body_start, &body_end)) {
		return 0;
	}
	return editorDapJsonObjectStringField(body_start, body_end, quoted_key, buf, bufsize);
}

void editorDapJsonForEachBodyArrayElement(const char *message, const char *quoted_key,
                                          editorDapJsonArrayElementFn on_element) {
	const char *array_start = NULL;
	const char *array_end = NULL;
	if (!dapProtocolFindBodyArray(message, quoted_key, &array_start, &array_end)) {
		return;
	}
	const char *scan = array_start + 1;
	while (scan < array_end) {
		const char *obj_start = strchr(scan, '{');
		if (obj_start == NULL || obj_start >= array_end) {
			break;
		}
		const char *obj_end = editorJsonFindObjectEnd(obj_start);
		if (obj_end == NULL || obj_end > array_end) {
			break;
		}
		if (!on_element(obj_start, obj_end)) {
			break;
		}
		scan = obj_end + 1;
	}
}
