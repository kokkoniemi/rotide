#include "debug/dap.h"

#include "config/dap_config.h"
#include "debug/dap_client.h"
#include "debug/dap_console.h"
#include "editing/edit.h"
#include "language/lsp_json.h"
#include "language/lsp_transport.h"
#include "rotide.h"
#include "support/file_io.h"
#include "workspace/drawer.h"

#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROTIDE_DAP_IO_TIMEOUT_MS 2500

/*
 * DAP launch handshake state. Ordering is load-bearing: `launch` must follow
 * the `initialize` *response*, and breakpoints + `configurationDone` must follow
 * the `initialized` *event*. Adapters may start the debuggee on `launch`, so
 * sending it before configuration loses breakpoints and the program runs to
 * exit.
 */
enum dapSessionState {
	DAP_SESSION_IDLE = 0,
	DAP_SESSION_AWAIT_INITIALIZE_RESPONSE,
	DAP_SESSION_AWAIT_INITIALIZED_EVENT,
	DAP_SESSION_RUNNING,
};

struct dapClient {
	pid_t pid;
	int to_adapter_fd;
	int from_adapter_fd;
	int next_seq;
	int initialized;
	enum dapSessionState state;
	/* Thread reported by the most recent `stopped` event; the stack trace and
	 * variable queries that follow a stop are scoped to it. */
	int stopped_thread_id;
	/* `launch` request JSON, built at start time but not sent until the
	 * `initialize` response arrives. Owned here; freed when sent or on reset. */
	char *pending_launch_json;
};

static struct dapClient g_dap_client = {
        .pid = 0,
        .to_adapter_fd = -1,
        .from_adapter_fd = -1,
        .next_seq = 1,
        .initialized = 0,
        .state = DAP_SESSION_IDLE,
        .pending_launch_json = NULL,
};

static void dapClientReset(void) {
	free(g_dap_client.pending_launch_json);
	g_dap_client.pid = 0;
	g_dap_client.to_adapter_fd = -1;
	g_dap_client.from_adapter_fd = -1;
	g_dap_client.next_seq = 1;
	g_dap_client.initialized = 0;
	g_dap_client.state = DAP_SESSION_IDLE;
	g_dap_client.stopped_thread_id = 0;
	g_dap_client.pending_launch_json = NULL;
	E.dap_running = 0;
	E.dap_stopped = 0;
	E.dap_thread_count = 0;
	E.dap_stack_frame_count = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
}

static void dapAppendOutput(const char *text) {
	if (text == NULL || text[0] == '\0') {
		return;
	}
	size_t len = strlen(text);
	if (len >= sizeof(E.dap_output)) {
		text += len - (sizeof(E.dap_output) - 1);
		len = strlen(text);
		E.dap_output_len = 0;
	}
	if (E.dap_output_len + len >= sizeof(E.dap_output)) {
		size_t remove = E.dap_output_len + len - (sizeof(E.dap_output) - 1);
		memmove(E.dap_output, E.dap_output + remove, E.dap_output_len - remove);
		E.dap_output_len -= remove;
	}
	memcpy(E.dap_output + E.dap_output_len, text, len);
	E.dap_output_len += len;
	E.dap_output[E.dap_output_len] = '\0';
}

static int dapSendRequest(char *json) {
	return editorDapClientSendRequest(g_dap_client.to_adapter_fd, json);
}

static int dapAppendJsonEscapedRaw(struct editorLspString *sb, const char *text, size_t len) {
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
				} else if (!editorLspStringAppendf(sb, "%c", ch)) {
					return 0;
				}
				break;
		}
	}
	return 1;
}

static int dapAppendJsonString(struct editorLspString *sb, const char *text) {
	const char *safe = text != NULL ? text : "";
	return editorLspStringAppend(sb, "\"") && dapAppendJsonEscapedRaw(sb, safe, strlen(safe)) &&
	       editorLspStringAppend(sb, "\"");
}

static int dapJsonStringField(const char *json, const char *field, char *buf, size_t bufsize) {
	char *value = NULL;
	if (!editorLspFindStringField(json, field, &value) || value == NULL) {
		return 0;
	}
	int ok = snprintf(buf, bufsize, "%s", value) >= 0 && strlen(value) < bufsize;
	free(value);
	return ok;
}

/*
 * Reads a response's top-level `success` boolean. DAP requires it on every
 * response; treat its absence as success so a terse adapter isn't misread as a
 * failure. Returns 1 unless `success` is explicitly `false`.
 */
static int dapJsonResponseSucceeded(const char *json) {
	const char *start = editorLspSkipWs(json);
	if (start == NULL || *start != '{') {
		return 1;
	}
	const char *obj_end = editorLspFindJsonObjectEnd(start);
	const char *key = editorLspFindTopLevelKey(start, obj_end, "\"success\"");
	if (key == NULL) {
		return 1;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL) {
		return 1;
	}
	return strncmp(editorLspSkipWs(colon + 1), "false", 5) != 0;
}

char *editorDapBuildInitializeRequestJson(int seq, const char *adapter_id) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"initialize\","
	            "\"arguments\":{\"clientID\":\"rotide\",\"clientName\":\"RotIDE\","
	            "\"adapterID\":",
	            seq) ||
	    !dapAppendJsonString(&sb, adapter_id) ||
	    !editorLspStringAppend(&sb, ",\"pathFormat\":\"path\",\"linesStartAt1\":true,"
	                                "\"columnsStartAt1\":true}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildSimpleCommandRequestJson(int seq, const char *command) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(&sb, "{\"seq\":%d,\"type\":\"request\",\"command\":", seq) ||
	    !dapAppendJsonString(&sb, command) || !editorLspStringAppend(&sb, "}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildEvaluateRequestJson(int seq, const char *expr, int frame_id,
                                        const char *context) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(
	            &sb, "{\"seq\":%d,\"type\":\"request\",\"command\":\"evaluate\",\"arguments\":{"
	                 "\"expression\":",
	            seq) ||
	    !dapAppendJsonString(&sb, expr) || !editorLspStringAppend(&sb, ",\"context\":") ||
	    !dapAppendJsonString(&sb, context != NULL ? context : "repl")) {
		free(sb.buf);
		return NULL;
	}
	/* frameId scopes the evaluation to a stack frame; omit it (global context)
	 * when there is no current frame. */
	if (frame_id > 0 && !editorLspStringAppendf(&sb, ",\"frameId\":%d", frame_id)) {
		free(sb.buf);
		return NULL;
	}
	if (!editorLspStringAppend(&sb, "}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

static int dapAppendSubstitutedString(struct editorLspString *sb, const char *value,
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
			if (!dapAppendJsonEscapedRaw(sb, replacement, strlen(replacement))) {
				free(file_dir);
				free(file_base);
				return 0;
			}
			p += token_len;
			continue;
		}
		if (!dapAppendJsonEscapedRaw(sb, p, 1)) {
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

static int dapAppendLaunchFieldJson(struct editorLspString *sb,
                                    const struct editorDapLaunchField *field,
                                    const char *workspace_root, const char *active_file) {
	if (!dapAppendJsonString(sb, field->key) || !editorLspStringAppend(sb, ":")) {
		return 0;
	}
	switch (field->kind) {
		case EDITOR_DAP_LAUNCH_VALUE_STRING:
			return editorLspStringAppend(sb, "\"") &&
			       dapAppendSubstitutedString(sb, field->string_value, workspace_root,
			                                  active_file) &&
			       editorLspStringAppend(sb, "\"");
		case EDITOR_DAP_LAUNCH_VALUE_BOOL:
			return editorLspStringAppend(sb, field->bool_value ? "true" : "false");
		case EDITOR_DAP_LAUNCH_VALUE_INT:
			return editorLspStringAppendf(sb, "%d", field->int_value);
		case EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY:
			if (!editorLspStringAppend(sb, "[")) {
				return 0;
			}
			for (int i = 0; i < field->array_count; i++) {
				if (i > 0 && !editorLspStringAppend(sb, ",")) {
					return 0;
				}
				if (!editorLspStringAppend(sb, "\"") ||
				    !dapAppendSubstitutedString(sb, field->array_values[i],
				                                workspace_root, active_file) ||
				    !editorLspStringAppend(sb, "\"")) {
					return 0;
				}
			}
			return editorLspStringAppend(sb, "]");
	}
	return 0;
}

char *editorDapBuildLaunchRequestJson(int seq, const struct editorDapLaunchConfig *config,
                                      const char *workspace_root, const char *active_file) {
	if (config == NULL) {
		return NULL;
	}
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(
	            &sb, "{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\",\"arguments\":{", seq,
	            config->request[0] != '\0' ? config->request : "launch")) {
		free(sb.buf);
		return NULL;
	}
	int wrote_any = 0;
	for (int i = 0; i < config->field_count; i++) {
		if (wrote_any && !editorLspStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!dapAppendLaunchFieldJson(&sb, &config->fields[i], workspace_root,
		                              active_file)) {
			free(sb.buf);
			return NULL;
		}
		wrote_any = 1;
	}
	if (config->env_count > 0) {
		if (wrote_any && !editorLspStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!editorLspStringAppend(&sb, "\"env\":{")) {
			free(sb.buf);
			return NULL;
		}
		for (int i = 0; i < config->env_count; i++) {
			if (i > 0 && !editorLspStringAppend(&sb, ",")) {
				free(sb.buf);
				return NULL;
			}
			if (!dapAppendJsonString(&sb, config->env[i].key) ||
			    !editorLspStringAppend(&sb, ":\"") ||
			    !dapAppendSubstitutedString(&sb, config->env[i].value, workspace_root,
			                                active_file) ||
			    !editorLspStringAppend(&sb, "\"")) {
				free(sb.buf);
				return NULL;
			}
		}
		if (!editorLspStringAppend(&sb, "}")) {
			free(sb.buf);
			return NULL;
		}
	}
	if (!editorLspStringAppend(&sb, "}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

static char *dapBuildSetBreakpointsRequestJson(int seq, const char *path) {
	/* Adapters resolve breakpoints against absolute debug-info paths; send an
	 * absolute source path even when the buffer was opened by a relative one. */
	char *absolute = editorPathAbsoluteDup(path);
	const char *source_path = absolute != NULL ? absolute : path;
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
	            "\"arguments\":{\"source\":{\"path\":",
	            seq) ||
	    !dapAppendJsonString(&sb, source_path) ||
	    !editorLspStringAppend(&sb, "},\"breakpoints\":[")) {
		free(absolute);
		free(sb.buf);
		return NULL;
	}
	free(absolute);
	int wrote = 0;
	for (int i = 0; i < E.dap_breakpoint_count; i++) {
		if (strcmp(E.dap_breakpoints[i].path, path) != 0) {
			continue;
		}
		if (wrote && !editorLspStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!editorLspStringAppendf(&sb, "{\"line\":%d}", E.dap_breakpoints[i].line + 1)) {
			free(sb.buf);
			return NULL;
		}
		wrote = 1;
	}
	if (!editorLspStringAppend(&sb, "]}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

static void dapSendAllBreakpoints(void) {
	for (int i = 0; i < E.dap_breakpoint_count; i++) {
		int seen = 0;
		for (int j = 0; j < i; j++) {
			if (strcmp(E.dap_breakpoints[i].path, E.dap_breakpoints[j].path) == 0) {
				seen = 1;
				break;
			}
		}
		if (!seen) {
			(void)dapSendRequest(dapBuildSetBreakpointsRequestJson(
			        g_dap_client.next_seq++, E.dap_breakpoints[i].path));
		}
	}
}

static char *dapBuildIntArgRequestJson(int seq, const char *command, const char *arg_key,
                                       int arg_value) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(&sb,
	                            "{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\","
	                            "\"arguments\":{\"%s\":%d}}",
	                            seq, command, arg_key, arg_value)) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

/* Reads an integer value for `quoted_key` at the top level of [start, end). */
static int dapObjectIntField(const char *start, const char *end, const char *quoted_key,
                             int *out) {
	const char *key = editorLspFindTopLevelKey(start, end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= end) {
		return 0;
	}
	return editorLspParseJsonInt(editorLspSkipWs(colon + 1), out, NULL);
}

/* Copies the (unescaped) string value for `quoted_key` at the top level of
 * [start, end) into `buf`. Returns 1 on success. */
static int dapObjectStringField(const char *start, const char *end, const char *quoted_key,
                                char *buf, size_t bufsize) {
	const char *key = editorLspFindTopLevelKey(start, end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= end) {
		return 0;
	}
	char *value = NULL;
	if (!editorLspParseJsonString(editorLspSkipWs(colon + 1), &value, NULL) || value == NULL) {
		return 0;
	}
	int ok = strlen(value) < bufsize;
	if (ok) {
		memcpy(buf, value, strlen(value) + 1);
	}
	free(value);
	return ok;
}

/* Locates the immediate child object stored under `quoted_key` within
 * [start, end). Writes the object's bounds and returns 1 on success. */
static int dapObjectChildObject(const char *start, const char *end, const char *quoted_key,
                                const char **child_start, const char **child_end) {
	const char *key = editorLspFindTopLevelKey(start, end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= end) {
		return 0;
	}
	const char *child = editorLspSkipWs(colon + 1);
	if (child == NULL || child[0] != '{') {
		return 0;
	}
	const char *child_obj_end = editorLspFindJsonObjectEnd(child);
	if (child_obj_end == NULL) {
		return 0;
	}
	*child_start = child;
	*child_end = child_obj_end;
	return 1;
}

/* Locates the array stored under body.<quoted_key>, e.g. body."threads".
 * Writes the array bounds (`[` .. `]`) and returns 1 on success. */
static int dapFindBodyArray(const char *message, const char *quoted_key, const char **array_start,
                            const char **array_end) {
	const char *start = editorLspSkipWs(message);
	if (start == NULL || start[0] != '{') {
		return 0;
	}
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (!dapObjectChildObject(start, editorLspFindJsonObjectEnd(start), "\"body\"", &body_start,
	                          &body_end)) {
		return 0;
	}
	const char *key = editorLspFindTopLevelKey(body_start, body_end, quoted_key);
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL || colon >= body_end) {
		return 0;
	}
	const char *arr = editorLspSkipWs(colon + 1);
	if (arr == NULL || arr[0] != '[') {
		return 0;
	}
	const char *arr_end = editorLspFindJsonArrayEnd(arr);
	if (arr_end == NULL) {
		return 0;
	}
	*array_start = arr;
	*array_end = arr_end;
	return 1;
}

/* Reads body.<quoted_key> as an integer. Returns 1 on success. */
static int dapBodyIntField(const char *message, const char *quoted_key, int *out) {
	const char *start = editorLspSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !dapObjectChildObject(start, editorLspFindJsonObjectEnd(start), "\"body\"", &body_start,
	                          &body_end)) {
		return 0;
	}
	return dapObjectIntField(body_start, body_end, quoted_key, out);
}

/* Copies the (unescaped) body.<quoted_key> string into `buf`. Returns 1 on success. */
static int dapBodyStringField(const char *message, const char *quoted_key, char *buf,
                              size_t bufsize) {
	const char *start = editorLspSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !dapObjectChildObject(start, editorLspFindJsonObjectEnd(start), "\"body\"", &body_start,
	                          &body_end)) {
		return 0;
	}
	return dapObjectStringField(body_start, body_end, quoted_key, buf, bufsize);
}

static void dapClearInspectionState(void) {
	E.dap_thread_count = 0;
	E.dap_stack_frame_count = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
}

/*
 * Iterates the objects of a body array via dapFindBodyArray. For each `{...}`
 * element the callback receives the object bounds; it returns 1 to keep the
 * element. Stops when the callback declines (array full) or the array ends.
 */
typedef int (*dapArrayElementFn)(const char *obj_start, const char *obj_end);

static void dapForEachBodyArrayElement(const char *message, const char *quoted_key,
                                       dapArrayElementFn on_element) {
	const char *array_start = NULL;
	const char *array_end = NULL;
	if (!dapFindBodyArray(message, quoted_key, &array_start, &array_end)) {
		return;
	}
	const char *scan = array_start + 1;
	while (scan < array_end) {
		const char *obj_start = strchr(scan, '{');
		if (obj_start == NULL || obj_start >= array_end) {
			break;
		}
		const char *obj_end = editorLspFindJsonObjectEnd(obj_start);
		if (obj_end == NULL || obj_end > array_end) {
			break;
		}
		if (!on_element(obj_start, obj_end)) {
			break;
		}
		scan = obj_end + 1;
	}
}

static int dapCollectThread(const char *obj_start, const char *obj_end) {
	if (E.dap_thread_count >= ROTIDE_DAP_MAX_THREADS) {
		return 0;
	}
	int id = 0;
	if (!dapObjectIntField(obj_start, obj_end, "\"id\"", &id)) {
		return 1; /* skip malformed element, keep scanning */
	}
	struct editorDapThread *thread = &E.dap_threads[E.dap_thread_count];
	memset(thread, 0, sizeof(*thread));
	thread->id = id;
	(void)dapObjectStringField(obj_start, obj_end, "\"name\"", thread->name,
	                           sizeof(thread->name));
	E.dap_thread_count++;
	return 1;
}

static int dapCollectStackFrame(const char *obj_start, const char *obj_end) {
	if (E.dap_stack_frame_count >= ROTIDE_DAP_MAX_STACK_FRAMES) {
		return 0;
	}
	int id = 0;
	if (!dapObjectIntField(obj_start, obj_end, "\"id\"", &id)) {
		return 1;
	}
	struct editorDapStackFrame *frame = &E.dap_stack_frames[E.dap_stack_frame_count];
	memset(frame, 0, sizeof(*frame));
	frame->id = id;
	(void)dapObjectStringField(obj_start, obj_end, "\"name\"", frame->name,
	                           sizeof(frame->name));
	(void)dapObjectIntField(obj_start, obj_end, "\"line\"", &frame->line);
	(void)dapObjectIntField(obj_start, obj_end, "\"column\"", &frame->column);
	const char *source_start = NULL;
	const char *source_end = NULL;
	if (dapObjectChildObject(obj_start, obj_end, "\"source\"", &source_start, &source_end)) {
		(void)dapObjectStringField(source_start, source_end, "\"path\"", frame->path,
		                           sizeof(frame->path));
	}
	E.dap_stack_frame_count++;
	return 1;
}

static int dapCollectScope(const char *obj_start, const char *obj_end) {
	if (E.dap_scope_count >= ROTIDE_DAP_MAX_SCOPES) {
		return 0;
	}
	struct editorDapScope *scope = &E.dap_scopes[E.dap_scope_count];
	memset(scope, 0, sizeof(*scope));
	(void)dapObjectStringField(obj_start, obj_end, "\"name\"", scope->name,
	                           sizeof(scope->name));
	(void)dapObjectIntField(obj_start, obj_end, "\"variablesReference\"",
	                        &scope->variables_reference);
	E.dap_scope_count++;
	return 1;
}

static int dapCollectVariable(const char *obj_start, const char *obj_end) {
	if (E.dap_variable_count >= ROTIDE_DAP_MAX_VARIABLES) {
		return 0;
	}
	struct editorDapVariable *var = &E.dap_variables[E.dap_variable_count];
	memset(var, 0, sizeof(*var));
	if (!dapObjectStringField(obj_start, obj_end, "\"name\"", var->name, sizeof(var->name))) {
		return 1;
	}
	(void)dapObjectStringField(obj_start, obj_end, "\"value\"", var->value,
	                           sizeof(var->value));
	(void)dapObjectIntField(obj_start, obj_end, "\"variablesReference\"",
	                        &var->variables_reference);
	E.dap_variable_count++;
	return 1;
}

/*
 * After a stop, the variable/stack views are rebuilt by chaining requests:
 * stackTrace (for the stopped thread) -> scopes (for the top frame) ->
 * variables (for each scope). Each response handler parses its payload and
 * issues the next request.
 */
static void dapRequestStackTrace(void) {
	int thread_id = g_dap_client.stopped_thread_id;
	if (thread_id == 0 && E.dap_thread_count > 0) {
		thread_id = E.dap_threads[0].id;
	}
	if (thread_id == 0) {
		return;
	}
	(void)dapSendRequest(dapBuildIntArgRequestJson(g_dap_client.next_seq++, "stackTrace",
	                                               "threadId", thread_id));
}

static void dapHandleThreadsResponse(const char *message) {
	E.dap_thread_count = 0;
	dapForEachBodyArrayElement(message, "\"threads\"", dapCollectThread);
	dapRequestStackTrace();
}

static void dapHandleStackTraceResponse(const char *message) {
	E.dap_stack_frame_count = 0;
	dapForEachBodyArrayElement(message, "\"stackFrames\"", dapCollectStackFrame);
	if (E.dap_stack_frame_count > 0) {
		(void)dapSendRequest(dapBuildIntArgRequestJson(g_dap_client.next_seq++, "scopes",
		                                               "frameId",
		                                               E.dap_stack_frames[0].id));
	}
}

static void dapHandleScopesResponse(const char *message) {
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
	dapForEachBodyArrayElement(message, "\"scopes\"", dapCollectScope);
	for (int i = 0; i < E.dap_scope_count; i++) {
		if (E.dap_scopes[i].variables_reference <= 0) {
			continue;
		}
		(void)dapSendRequest(dapBuildIntArgRequestJson(g_dap_client.next_seq++, "variables",
		                                               "variablesReference",
		                                               E.dap_scopes[i].variables_reference));
	}
}

static void dapHandleVariablesResponse(const char *message) {
	/* Variables from every scope share one flat list (matching the drawer);
	 * responses append in arrival order. */
	dapForEachBodyArrayElement(message, "\"variables\"", dapCollectVariable);
}

/* A successful REPL evaluate: echo the result into the console output stream and
 * the status bar. Failures fall through the generic failed-response path. */
static void dapHandleEvaluateResponse(const char *message) {
	char result[ROTIDE_DAP_VALUE_MAX];
	if (!dapBodyStringField(message, "\"result\"", result, sizeof(result))) {
		return;
	}
	dapAppendOutput("= ");
	dapAppendOutput(result);
	dapAppendOutput("\n");
	editorSetStatusMsg("DAP eval: %s", result);
}

/*
 * Best human-readable message from a failed response: the detailed
 * `body.error.format` if present, else the short top-level `message`. Returns 1
 * and fills `buf` on success, 0 if the response carries no message text.
 */
static int dapExtractErrorMessage(const char *message, char *buf, size_t bufsize) {
	const char *start = editorLspSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start != NULL && start[0] == '{' &&
	    dapObjectChildObject(start, editorLspFindJsonObjectEnd(start), "\"body\"", &body_start,
	                         &body_end)) {
		const char *error_start = NULL;
		const char *error_end = NULL;
		if (dapObjectChildObject(body_start, body_end, "\"error\"", &error_start,
		                         &error_end) &&
		    dapObjectStringField(error_start, error_end, "\"format\"", buf, bufsize)) {
			return 1;
		}
	}
	return dapJsonStringField(message, "message", buf, bufsize);
}

/*
 * Sends the queued `launch` request once `initialize` has been acknowledged.
 * Consumes pending_launch_json (sent or freed).
 */
static void dapFlushPendingLaunch(void) {
	char *launch_json = g_dap_client.pending_launch_json;
	g_dap_client.pending_launch_json = NULL;
	if (launch_json == NULL) {
		return;
	}
	g_dap_client.state = DAP_SESSION_AWAIT_INITIALIZED_EVENT;
	(void)dapSendRequest(launch_json);
}

/*
 * On the `initialized` event, register breakpoints and signal that
 * configuration is complete. Guarded so it only runs once per session.
 */
static void dapHandleInitializedEvent(void) {
	if (g_dap_client.initialized) {
		return;
	}
	g_dap_client.initialized = 1;
	g_dap_client.state = DAP_SESSION_RUNNING;
	dapSendAllBreakpoints();
	(void)dapSendRequest(
	        editorDapBuildSimpleCommandRequestJson(g_dap_client.next_seq++, "configurationDone"));
}

int editorDapProcessIncomingMessage(const char *message) {
	if (message == NULL) {
		return 0;
	}
	char type[32];
	char event[64];
	char command[64];
	if (dapJsonStringField(message, "type", type, sizeof(type)) && strcmp(type, "event") == 0 &&
	    dapJsonStringField(message, "event", event, sizeof(event))) {
		if (strcmp(event, "initialized") == 0) {
			dapHandleInitializedEvent();
			return 1;
		}
		if (strcmp(event, "stopped") == 0) {
			E.dap_stopped = 1;
			g_dap_client.stopped_thread_id = 0;
			(void)dapBodyIntField(message, "\"threadId\"",
			                      &g_dap_client.stopped_thread_id);
			dapClearInspectionState();
			editorSetStatusMsg("DAP stopped");
			(void)dapSendRequest(editorDapBuildSimpleCommandRequestJson(
			        g_dap_client.next_seq++, "threads"));
			return 1;
		}
		if (strcmp(event, "continued") == 0) {
			E.dap_stopped = 0;
			dapClearInspectionState();
			editorSetStatusMsg("DAP continued");
			return 1;
		}
		if (strcmp(event, "terminated") == 0 || strcmp(event, "exited") == 0) {
			E.dap_running = 0;
			E.dap_stopped = 0;
			dapClearInspectionState();
			editorDapConsoleCloseOwnedTerminalPane();
			editorSetStatusMsg("DAP session ended");
			return 1;
		}
		if (strcmp(event, "output") == 0) {
			char output[ROTIDE_DAP_VALUE_MAX];
			if (dapBodyStringField(message, "\"output\"", output, sizeof(output))) {
				dapAppendOutput(output);
			}
			return 1;
		}
		return 1;
	}

	if (dapJsonStringField(message, "type", type, sizeof(type)) &&
	    strcmp(type, "response") == 0 &&
	    dapJsonStringField(message, "command", command, sizeof(command))) {
		char errmsg[ROTIDE_DAP_VALUE_MAX];
		int has_errmsg = dapExtractErrorMessage(message, errmsg, sizeof(errmsg));
		if (strcmp(command, "initialize") == 0) {
			if (!dapJsonResponseSucceeded(message)) {
				editorSetStatusMsg("DAP initialize failed%s%s", has_errmsg ? ": " : "",
				                   has_errmsg ? errmsg : "");
				editorDapShutdown();
				return 1;
			}
			dapFlushPendingLaunch();
			return 1;
		}
		if (strcmp(command, "launch") == 0 && !dapJsonResponseSucceeded(message)) {
			editorSetStatusMsg("DAP launch failed%s%s", has_errmsg ? ": " : "",
			                   has_errmsg ? errmsg : "");
			editorDapShutdown();
			return 1;
		}
		if (!dapJsonResponseSucceeded(message)) {
			/* `configurationDone` answering `notStopped` is expected once the
			 * program is already running; don't nag. Surface other failures with
			 * the adapter's message in the status bar and the console output. */
			if (strcmp(command, "configurationDone") != 0) {
				editorSetStatusMsg("DAP %s failed%s%s", command, has_errmsg ? ": " : "",
				                   has_errmsg ? errmsg : "");
				dapAppendOutput("[dap] ");
				dapAppendOutput(command);
				dapAppendOutput(has_errmsg ? " failed: " : " failed");
				if (has_errmsg) {
					dapAppendOutput(errmsg);
				}
				dapAppendOutput("\n");
			}
			return 1;
		}
		if (strcmp(command, "threads") == 0) {
			dapHandleThreadsResponse(message);
		} else if (strcmp(command, "stackTrace") == 0) {
			dapHandleStackTraceResponse(message);
		} else if (strcmp(command, "scopes") == 0) {
			dapHandleScopesResponse(message);
		} else if (strcmp(command, "variables") == 0) {
			dapHandleVariablesResponse(message);
		} else if (strcmp(command, "evaluate") == 0) {
			dapHandleEvaluateResponse(message);
		}
		return 1;
	}
	return 1;
}

int editorDapAdapterReadFd(void) {
	return g_dap_client.from_adapter_fd;
}

void editorDapPumpNotifications(void) {
	if (g_dap_client.from_adapter_fd == -1) {
		return;
	}
	for (;;) {
		struct pollfd pfd = {
		        .fd = g_dap_client.from_adapter_fd,
		        .events = POLLIN,
		        .revents = 0,
		};
		int polled = poll(&pfd, 1, 0);
		if (polled <= 0) {
			return;
		}
		if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			editorDapShutdown();
			editorSetStatusMsg("DAP adapter exited");
			return;
		}
		char *message = editorDapClientReadFrame(g_dap_client.from_adapter_fd);
		if (message == NULL) {
			editorDapShutdown();
			editorSetStatusMsg("DAP adapter read failed");
			return;
		}
		(void)editorDapProcessIncomingMessage(message);
		free(message);
	}
}

int editorDapStartLaunch(int launch_idx) {
	if (launch_idx < 0 || launch_idx >= E.dap_launch_count) {
		editorSetStatusMsg("No DAP launch config selected");
		return 0;
	}
	const struct editorDapLaunchConfig *config = &E.dap_launches[launch_idx];
	if (strcmp(config->request, "launch") != 0) {
		editorSetStatusMsg("DAP: only 'launch' is supported yet (config requests '%s')",
		                   config->request);
		return 0;
	}
	const struct editorDapAdapterConfig *adapter = editorDapAdapterById(config->adapter);
	if (adapter == NULL) {
		editorSetStatusMsg("DAP adapter '%s' is not configured", config->adapter);
		return 0;
	}

	editorDapShutdown();

	/* Keep launch config immutable while applying spawn-time fields (e.g. tty). */
	struct editorDapLaunchConfig launch_copy = *config;
	if (!editorDapPrepareTerminalConsole(&launch_copy)) {
		return 0;
	}

	pid_t pid = 0;
	int to_fd = -1;
	int from_fd = -1;
	if (!editorLspSpawnProcess(adapter->command, &pid, &to_fd, &from_fd)) {
		editorSetStatusMsg("Could not start DAP adapter");
		editorDapShutdown();
		return 0;
	}
	g_dap_client.pid = pid;
	g_dap_client.to_adapter_fd = to_fd;
	g_dap_client.from_adapter_fd = from_fd;
	g_dap_client.next_seq = 1;
	g_dap_client.initialized = 0;
	g_dap_client.state = DAP_SESSION_AWAIT_INITIALIZE_RESPONSE;
	E.dap_selected_launch = launch_idx;
	E.dap_running = 1;
	E.dap_stopped = 0;
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';

	const char *workspace_root = editorDrawerRootPath();
	if (workspace_root == NULL) {
		workspace_root = ".";
	}
	/*
	 * Build both requests up front so a build failure aborts cleanly, but send
	 * only `initialize` now; `launch` is queued for dapFlushPendingLaunch (see
	 * the handshake-ordering note on enum dapSessionState).
	 */
	char *init_json =
	        editorDapBuildInitializeRequestJson(g_dap_client.next_seq++, launch_copy.adapter);
	char *launch_json = editorDapBuildLaunchRequestJson(g_dap_client.next_seq++, &launch_copy,
	                                                    workspace_root, E.filename);
	if (init_json == NULL || launch_json == NULL) {
		free(init_json);
		free(launch_json);
		editorDapShutdown();
		editorSetStatusMsg("DAP launch request failed");
		return 0;
	}
	g_dap_client.pending_launch_json = launch_json;
	if (!dapSendRequest(init_json)) {
		editorDapShutdown();
		editorSetStatusMsg("DAP launch request failed");
		return 0;
	}
	editorSetStatusMsg("DAP launched %s",
	                   launch_copy.name[0] != '\0' ? launch_copy.name : launch_copy.id);
	return 1;
}

int editorDapStartSelectedLaunch(void) {
	int launch_idx = E.dap_selected_launch;
	if (launch_idx < 0 && E.dap_launch_count > 0) {
		launch_idx = 0;
	}
	return editorDapStartLaunch(launch_idx);
}

static int dapSendControl(const char *command) {
	if (!E.dap_running || g_dap_client.to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	if (!dapSendRequest(
	            editorDapBuildSimpleCommandRequestJson(g_dap_client.next_seq++, command))) {
		editorSetStatusMsg("DAP command failed");
		return 0;
	}
	return 1;
}

/* Best-effort current thread for execution-control requests: the thread of the
 * most recent stop, else the first known thread, else the main thread (1). */
static int dapCurrentThreadId(void) {
	if (g_dap_client.stopped_thread_id > 0) {
		return g_dap_client.stopped_thread_id;
	}
	if (E.dap_thread_count > 0 && E.dap_threads[0].id > 0) {
		return E.dap_threads[0].id;
	}
	return 1;
}

/* continue/next/stepIn/stepOut/pause are thread-scoped; adapters require a
 * threadId and silently no-op (or error) without one. */
static int dapSendThreadControl(const char *command) {
	if (!E.dap_running || g_dap_client.to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	if (!dapSendRequest(dapBuildIntArgRequestJson(g_dap_client.next_seq++, command, "threadId",
	                                              dapCurrentThreadId()))) {
		editorSetStatusMsg("DAP command failed");
		return 0;
	}
	return 1;
}

int editorDapContinue(void) {
	E.dap_stopped = 0;
	return dapSendThreadControl("continue");
}

int editorDapPause(void) {
	return dapSendThreadControl("pause");
}

int editorDapStepOver(void) {
	return dapSendThreadControl("next");
}

int editorDapStepInto(void) {
	return dapSendThreadControl("stepIn");
}

int editorDapStepOut(void) {
	return dapSendThreadControl("stepOut");
}

int editorDapStop(void) {
	if (!E.dap_running) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	(void)dapSendControl("disconnect");
	editorDapShutdown();
	editorSetStatusMsg("DAP stopped");
	return 1;
}

int editorDapEvaluate(const char *expr) {
	if (expr == NULL || expr[0] == '\0') {
		return 0;
	}
	if (!E.dap_running || g_dap_client.to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	/* Scope the evaluation to the top frame when stopped; global otherwise. */
	int frame_id = (E.dap_stopped && E.dap_stack_frame_count > 0) ? E.dap_stack_frames[0].id : 0;
	dapAppendOutput("> ");
	dapAppendOutput(expr);
	dapAppendOutput("\n");
	if (!dapSendRequest(
	            editorDapBuildEvaluateRequestJson(g_dap_client.next_seq++, expr, frame_id,
	                                              "repl"))) {
		editorSetStatusMsg("DAP evaluate failed");
		return 0;
	}
	return 1;
}

int editorDapRestart(void) {
	/* Tear down the current session (if any) and relaunch the selected config.
	 * editorDapStartLaunch already shuts down any prior session, so a stop here
	 * is only to give a clean "stopped" state if no launch is selected. */
	int launch_idx = E.dap_selected_launch;
	if (launch_idx < 0 && E.dap_launch_count > 0) {
		launch_idx = 0;
	}
	if (launch_idx < 0) {
		if (E.dap_running) {
			(void)editorDapStop();
		}
		editorSetStatusMsg("No DAP launch config selected");
		return 0;
	}
	return editorDapStartLaunch(launch_idx);
}

int editorDapHasBreakpoint(const char *path, int line) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	for (int i = 0; i < E.dap_breakpoint_count; i++) {
		if (E.dap_breakpoints[i].line == line &&
		    strcmp(E.dap_breakpoints[i].path, path) == 0) {
			return i;
		}
	}
	return -1;
}

int editorDapIsStoppedLine(const char *path, int line) {
	if (!E.dap_stopped || E.dap_stack_frame_count <= 0 || path == NULL || path[0] == '\0') {
		return 0;
	}
	const struct editorDapStackFrame *frame = &E.dap_stack_frames[0];
	/* Cheap line check first; the path comparison may hit the filesystem. */
	if (frame->line - 1 != line || frame->path[0] == '\0') {
		return 0;
	}
	return editorPathsReferToSameFile(frame->path, path);
}

int editorDapToggleBreakpointAtLine(int line) {
	if (E.filename == NULL || E.filename[0] == '\0') {
		editorSetStatusMsg("Save the file before setting a breakpoint");
		return 0;
	}
	if (line < 0) {
		return 0;
	}
	int idx = editorDapHasBreakpoint(E.filename, line);
	if (idx >= 0) {
		for (int i = idx; i + 1 < E.dap_breakpoint_count; i++) {
			E.dap_breakpoints[i] = E.dap_breakpoints[i + 1];
		}
		E.dap_breakpoint_count--;
		editorSetStatusMsg("Breakpoint removed");
	} else {
		if (E.dap_breakpoint_count >= ROTIDE_DAP_MAX_BREAKPOINTS ||
		    strlen(E.filename) >= PATH_MAX) {
			editorSetStatusMsg("Too many DAP breakpoints");
			return 0;
		}
		struct editorDapBreakpoint *bp = &E.dap_breakpoints[E.dap_breakpoint_count++];
		(void)snprintf(bp->path, sizeof(bp->path), "%s", E.filename);
		bp->line = line;
		editorSetStatusMsg("Breakpoint set");
	}
	if (E.dap_running) {
		(void)dapSendRequest(
		        dapBuildSetBreakpointsRequestJson(g_dap_client.next_seq++, E.filename));
	}
	return 1;
}

int editorDapToggleBreakpointAtCursor(void) {
	return editorDapToggleBreakpointAtLine(E.cy);
}

void editorDapShutdown(void) {
	if (g_dap_client.to_adapter_fd != -1) {
		close(g_dap_client.to_adapter_fd);
	}
	if (g_dap_client.from_adapter_fd != -1) {
		close(g_dap_client.from_adapter_fd);
	}
	if (g_dap_client.pid > 0) {
		int status = 0;
		pid_t waited = waitpid(g_dap_client.pid, &status, WNOHANG);
		if (waited == 0) {
			(void)kill(g_dap_client.pid, SIGTERM);
			(void)waitpid(g_dap_client.pid, &status, 0);
		}
	}
	dapClientReset();
	editorDapConsoleCloseOwnedTerminalPane();
}

int editorDapSessionStateForTest(void) {
	return (int)g_dap_client.state;
}

void editorDapBeginSessionForTest(int to_adapter_fd, char *launch_json) {
	free(g_dap_client.pending_launch_json);
	g_dap_client.pid = 0;
	g_dap_client.to_adapter_fd = to_adapter_fd;
	g_dap_client.from_adapter_fd = -1;
	g_dap_client.next_seq = 2; /* initialize already "sent" as seq 1 */
	g_dap_client.initialized = 0;
	g_dap_client.state = DAP_SESSION_AWAIT_INITIALIZE_RESPONSE;
	g_dap_client.pending_launch_json = launch_json;
	E.dap_running = 1;
	E.dap_stopped = 0;
}

void editorDapEndSessionForTest(void) {
	dapClientReset();
}
