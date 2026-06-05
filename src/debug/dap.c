#include "debug/dap.h"

#include "config/dap_config.h"
#include "debug/dap_client.h"
#include "debug/dap_console.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "language/lsp_transport.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/file_io.h"
#include "support/json.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROTIDE_DAP_IO_TIMEOUT_MS 2500
#define ROTIDE_DAP_VARIABLE_PREVIEW_CHILDREN 6

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
	/* Correlates `variables` responses back to the scope they were requested
	 * for: when scopes arrive we fire one request per scope and remember each
	 * request seq -> scope index here. Rebuilt on every scopes response. */
	int pending_var_seq[ROTIDE_DAP_MAX_SCOPES];
	int pending_var_scope[ROTIDE_DAP_MAX_SCOPES];
	int pending_var_count;
	int pending_var_preview_seq[ROTIDE_DAP_MAX_VARIABLES];
	int pending_var_preview_index[ROTIDE_DAP_MAX_VARIABLES];
	int pending_var_preview_count;
	/* Scopes whose variables have already been received this stop, used as an
	 * in-order fallback when a response carries no `request_seq`. */
	unsigned long long var_scopes_received;
	/* Whether the once-per-session default collapse of register scopes ran. */
	int register_collapse_applied;
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
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
	g_dap_client.register_collapse_applied = 0;
	E.dap_running = 0;
	E.dap_stopped = 0;
	E.dap_thread_count = 0;
	E.dap_stack_frame_count = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
	E.drawer_dap_scope_collapsed = 0;
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

static int dapAppendJsonEscapedRaw(struct editorJsonString *sb, const char *text, size_t len) {
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

static int dapAppendJsonString(struct editorJsonString *sb, const char *text) {
	const char *safe = text != NULL ? text : "";
	return editorJsonStringAppend(sb, "\"") &&
	       dapAppendJsonEscapedRaw(sb, safe, strlen(safe)) && editorJsonStringAppend(sb, "\"");
}

static int dapJsonStringField(const char *json, const char *field, char *buf, size_t bufsize) {
	char *value = NULL;
	if (!editorJsonFindStringField(json, field, &value) || value == NULL) {
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

char *editorDapBuildInitializeRequestJson(int seq, const char *adapter_id) {
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"initialize\","
	            "\"arguments\":{\"clientID\":\"rotide\",\"clientName\":\"RotIDE\","
	            "\"adapterID\":",
	            seq) ||
	    !dapAppendJsonString(&sb, adapter_id) ||
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
	    !dapAppendJsonString(&sb, command) || !editorJsonStringAppend(&sb, "}")) {
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
	    !dapAppendJsonString(&sb, expr) || !editorJsonStringAppend(&sb, ",\"context\":") ||
	    !dapAppendJsonString(&sb, context != NULL ? context : "repl")) {
		free(sb.buf);
		return NULL;
	}
	/* frameId scopes the evaluation to a stack frame; omit it (global context)
	 * when there is no current frame. */
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
	/*
	 * gdb routes the debuggee's stdio to a real tty via the `--tty` startup flag,
	 * which keeps program output off the DAP stream (it instead reaches the
	 * Terminal tab's pts). gdb ignores the DAP `tty` launch argument, so the flag
	 * is the only way to get the split. Other adapters honour the launch argument
	 * and would choke on `--tty`, so it is appended only for gdb commands.
	 */
	if (tty_path != NULL && tty_path[0] != '\0' && base != NULL &&
	    strstr(base, "gdb") != NULL) {
		(void)snprintf(out, out_size, "%s --tty=%s", base, tty_path);
	} else {
		(void)snprintf(out, out_size, "%s", base != NULL ? base : "");
	}
}

static int dapAppendSubstitutedString(struct editorJsonString *sb, const char *value,
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

static int dapAppendLaunchFieldJson(struct editorJsonString *sb,
                                    const struct editorDapLaunchField *field,
                                    const char *workspace_root, const char *active_file) {
	if (!dapAppendJsonString(sb, field->key) || !editorJsonStringAppend(sb, ":")) {
		return 0;
	}
	switch (field->kind) {
		case EDITOR_DAP_LAUNCH_VALUE_STRING:
			return editorJsonStringAppend(sb, "\"") &&
			       dapAppendSubstitutedString(sb, field->string_value, workspace_root,
			                                  active_file) &&
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
				    !dapAppendSubstitutedString(sb, field->array_values[i],
				                                workspace_root, active_file) ||
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
		if (!dapAppendLaunchFieldJson(&sb, &config->fields[i], workspace_root,
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
			if (!dapAppendJsonString(&sb, config->env[i].key) ||
			    !editorJsonStringAppend(&sb, ":\"") ||
			    !dapAppendSubstitutedString(&sb, config->env[i].value, workspace_root,
			                                active_file) ||
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

static char *dapBuildSetBreakpointsRequestJson(int seq, const char *path) {
	/* Adapters resolve breakpoints against absolute debug-info paths; send an
	 * absolute source path even when the buffer was opened by a relative one. */
	char *absolute = editorPathAbsoluteDup(path);
	const char *source_path = absolute != NULL ? absolute : path;
	struct editorJsonString sb = {0};
	if (!editorJsonStringAppendf(
	            &sb,
	            "{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
	            "\"arguments\":{\"source\":{\"path\":",
	            seq) ||
	    !dapAppendJsonString(&sb, source_path) ||
	    !editorJsonStringAppend(&sb, "},\"breakpoints\":[")) {
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
		if (wrote && !editorJsonStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!editorJsonStringAppendf(&sb, "{\"line\":%d}", E.dap_breakpoints[i].line + 1)) {
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

static char *dapBuildVariablesRequestJson(int seq, int variables_reference, int start, int count) {
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

/* Reads an integer value for `quoted_key` at the top level of [start, end). */
static int dapObjectIntField(const char *start, const char *end, const char *quoted_key, int *out) {
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

/* Copies the (unescaped) string value for `quoted_key` at the top level of
 * [start, end) into `buf`. Returns 1 on success. */
static int dapObjectStringField(const char *start, const char *end, const char *quoted_key,
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

/* Locates the immediate child object stored under `quoted_key` within
 * [start, end). Writes the object's bounds and returns 1 on success. */
static int dapObjectChildObject(const char *start, const char *end, const char *quoted_key,
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

/* Locates the array stored under body.<quoted_key>, e.g. body."threads".
 * Writes the array bounds (`[` .. `]`) and returns 1 on success. */
static int dapFindBodyArray(const char *message, const char *quoted_key, const char **array_start,
                            const char **array_end) {
	const char *start = editorJsonSkipWs(message);
	if (start == NULL || start[0] != '{') {
		return 0;
	}
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (!dapObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"", &body_start,
	                          &body_end)) {
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

/* Reads body.<quoted_key> as an integer. Returns 1 on success. */
static int dapBodyIntField(const char *message, const char *quoted_key, int *out) {
	const char *start = editorJsonSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !dapObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"", &body_start,
	                          &body_end)) {
		return 0;
	}
	return dapObjectIntField(body_start, body_end, quoted_key, out);
}

/* Reads a top-level (non-body) integer field, e.g. a response's request_seq. */
static int dapTopLevelIntField(const char *message, const char *quoted_key, int *out) {
	const char *start = editorJsonSkipWs(message);
	if (start == NULL || start[0] != '{') {
		return 0;
	}
	return dapObjectIntField(start, editorJsonFindObjectEnd(start), quoted_key, out);
}

/* Case-insensitive substring test for "register", used to default-collapse the
 * register scope (which most adapters name "Registers" / "CPU Registers"). */
static int dapScopeNameLooksLikeRegisters(const char *name) {
	static const char needle[] = "register";
	size_t needle_len = sizeof(needle) - 1;
	for (const char *p = name; *p != '\0'; p++) {
		size_t i = 0;
		while (i < needle_len && p[i] != '\0' &&
		       (char)tolower((unsigned char)p[i]) == needle[i]) {
			i++;
		}
		if (i == needle_len) {
			return 1;
		}
	}
	return 0;
}

/* Copies the (unescaped) body.<quoted_key> string into `buf`. Returns 1 on success. */
static int dapBodyStringField(const char *message, const char *quoted_key, char *buf,
                              size_t bufsize) {
	const char *start = editorJsonSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start == NULL || start[0] != '{' ||
	    !dapObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"", &body_start,
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
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
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

/* Scope index that the variables currently being parsed belong to; set by
 * dapHandleVariablesResponse before walking the array. */
static int g_dap_collect_scope_index = 0;
static int g_dap_preview_parent_index = -1;
static int g_dap_preview_child_count = 0;

static int dapVariableScopeCanPreview(int scope_index) {
	if (scope_index < 0 || scope_index >= E.dap_scope_count) {
		return 0;
	}
	return !dapScopeNameLooksLikeRegisters(E.dap_scopes[scope_index].name);
}

static int dapVariablePreviewChildCount(const struct editorDapVariable *var) {
	int child_count =
	        var->indexed_variables > 0 ? var->indexed_variables : var->named_variables;
	if (child_count <= 0) {
		return 0;
	}
	return child_count < ROTIDE_DAP_VARIABLE_PREVIEW_CHILDREN
	               ? child_count
	               : ROTIDE_DAP_VARIABLE_PREVIEW_CHILDREN;
}

static void dapQueueVariablePreviewRequest(int variable_index) {
	if (g_dap_client.to_adapter_fd == -1 || variable_index < 0 ||
	    variable_index >= E.dap_variable_count ||
	    g_dap_client.pending_var_preview_count >= ROTIDE_DAP_MAX_VARIABLES) {
		return;
	}
	const struct editorDapVariable *var = &E.dap_variables[variable_index];
	if (var->variables_reference <= 0 || !dapVariableScopeCanPreview(var->scope_index)) {
		return;
	}
	int child_count = dapVariablePreviewChildCount(var);
	if (child_count <= 0) {
		return;
	}
	int seq = g_dap_client.next_seq++;
	g_dap_client.pending_var_preview_seq[g_dap_client.pending_var_preview_count] = seq;
	g_dap_client.pending_var_preview_index[g_dap_client.pending_var_preview_count] =
	        variable_index;
	g_dap_client.pending_var_preview_count++;
	(void)dapSendRequest(
	        dapBuildVariablesRequestJson(seq, var->variables_reference, 0, child_count));
}

static int dapPreviewAppendText(struct editorDapVariable *var, const char *text) {
	if (var == NULL || text == NULL || text[0] == '\0') {
		return 1;
	}
	size_t len = strlen(var->preview);
	size_t avail = sizeof(var->preview) - len;
	if (avail <= 1) {
		return 0;
	}
	int written = snprintf(var->preview + len, avail, "%s", text);
	return written >= 0 && (size_t)written < avail;
}

static int dapCollectVariablePreviewChild(const char *obj_start, const char *obj_end) {
	if (g_dap_preview_parent_index < 0 || g_dap_preview_parent_index >= E.dap_variable_count) {
		return 0;
	}
	struct editorDapVariable *parent = &E.dap_variables[g_dap_preview_parent_index];
	char name[ROTIDE_DAP_NAME_MAX] = "";
	char value[ROTIDE_DAP_VALUE_MAX] = "";
	(void)dapObjectStringField(obj_start, obj_end, "\"name\"", name, sizeof(name));
	(void)dapObjectStringField(obj_start, obj_end, "\"value\"", value, sizeof(value));
	if (value[0] == '\0' && name[0] == '\0') {
		return 1;
	}
	int array_like = parent->indexed_variables > 0 && parent->named_variables == 0;
	if (g_dap_preview_child_count > 0 && !dapPreviewAppendText(parent, ",")) {
		return 0;
	}
	if (!array_like && name[0] != '\0') {
		if (!dapPreviewAppendText(parent, name) || !dapPreviewAppendText(parent, "=")) {
			return 0;
		}
	}
	if (!dapPreviewAppendText(parent, value[0] != '\0' ? value : name)) {
		return 0;
	}
	g_dap_preview_child_count++;
	return 1;
}

static int dapResolveVariablePreviewParent(const char *message) {
	int request_seq = 0;
	if (!dapTopLevelIntField(message, "\"request_seq\"", &request_seq)) {
		return -1;
	}
	for (int i = 0; i < g_dap_client.pending_var_preview_count; i++) {
		if (g_dap_client.pending_var_preview_seq[i] == request_seq) {
			int parent_index = g_dap_client.pending_var_preview_index[i];
			for (int j = i; j + 1 < g_dap_client.pending_var_preview_count; j++) {
				g_dap_client.pending_var_preview_seq[j] =
				        g_dap_client.pending_var_preview_seq[j + 1];
				g_dap_client.pending_var_preview_index[j] =
				        g_dap_client.pending_var_preview_index[j + 1];
			}
			g_dap_client.pending_var_preview_count--;
			return parent_index;
		}
	}
	return -1;
}

static void dapHandleVariablePreviewResponse(const char *message, int parent_index) {
	if (parent_index < 0 || parent_index >= E.dap_variable_count) {
		return;
	}
	struct editorDapVariable *parent = &E.dap_variables[parent_index];
	parent->preview[0] = '\0';
	if (!dapPreviewAppendText(parent, "{")) {
		return;
	}
	g_dap_preview_parent_index = parent_index;
	g_dap_preview_child_count = 0;
	dapForEachBodyArrayElement(message, "\"variables\"", dapCollectVariablePreviewChild);
	g_dap_preview_parent_index = -1;
	if (g_dap_preview_child_count == 0) {
		parent->preview[0] = '\0';
		return;
	}
	int total_children =
	        parent->indexed_variables > 0 ? parent->indexed_variables : parent->named_variables;
	if (total_children > g_dap_preview_child_count) {
		(void)dapPreviewAppendText(parent, ",...");
	}
	(void)dapPreviewAppendText(parent, "}");
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
	(void)dapObjectStringField(obj_start, obj_end, "\"value\"", var->value, sizeof(var->value));
	(void)dapObjectStringField(obj_start, obj_end, "\"type\"", var->type, sizeof(var->type));
	(void)dapObjectIntField(obj_start, obj_end, "\"variablesReference\"",
	                        &var->variables_reference);
	(void)dapObjectIntField(obj_start, obj_end, "\"namedVariables\"", &var->named_variables);
	(void)dapObjectIntField(obj_start, obj_end, "\"indexedVariables\"",
	                        &var->indexed_variables);
	(void)dapObjectStringField(obj_start, obj_end, "\"memoryReference\"", var->memory_reference,
	                           sizeof(var->memory_reference));
	var->scope_index = g_dap_collect_scope_index;
	E.dap_variable_count++;
	dapQueueVariablePreviewRequest(E.dap_variable_count - 1);
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

/* First leaf whose active tab is an editor (i.e. shows source, not the debug
 * console or a terminal), preferring `preferred` when it qualifies. */
static struct editorPaneNode *dapFindSourceLeaf(struct editorPaneNode *node) {
	if (node == NULL) {
		return NULL;
	}
	if (node->is_split) {
		struct editorPaneNode *first = dapFindSourceLeaf(node->as.split.first);
		return first != NULL ? first : dapFindSourceLeaf(node->as.split.second);
	}
	return editorPaneActiveKind(node) == EDITOR_PANE_KIND_EDITOR ? node : NULL;
}

/* On a stop, reveal the top frame's line in a source pane: focus a source pane
 * (not the console), switch to the file, and center the viewport on the line.
 * The cursor lands on the line so the centered view stays put under
 * follow-cursor. */
/* A stopped frame is navigable only when its source is a regular file present
 * on disk. Frames in libc, inlined code, or units built without debug info
 * report a path that does not resolve locally (e.g. stepping into printf lands
 * in glibc source that is not installed) -- those must be skipped silently
 * rather than surfacing an "Unable to open file" error. */
static int dapFrameSourceIsOpenable(const struct editorDapStackFrame *frame) {
	struct stat st;
	return frame->path[0] != '\0' && stat(frame->path, &st) == 0 && S_ISREG(st.st_mode);
}

static void dapRevealStoppedFrame(const struct editorDapStackFrame *frame) {
	if (frame->line <= 0 || E.layout_root == NULL || !dapFrameSourceIsOpenable(frame)) {
		return;
	}
	struct editorPaneNode *src = E.focused_leaf;
	if (src == NULL || src->is_split || editorPaneActiveKind(src) != EDITOR_PANE_KIND_EDITOR) {
		src = dapFindSourceLeaf(E.layout_root);
	}
	if (src == NULL) {
		return;
	}
	if (src != E.focused_leaf) {
		(void)editorLayoutSetFocusedLeaf(src);
	}
	if (!editorTabOpenOrSwitchToFile(frame->path)) {
		return;
	}
	int line = frame->line - 1;
	if (line < 0) {
		line = 0;
	}
	if (line >= E.numrows) {
		line = E.numrows > 0 ? E.numrows - 1 : 0;
	}
	size_t offset = 0;
	if (editorBufferPosToOffset(line, 0, &offset)) {
		(void)editorSyncCursorFromOffset(offset);
	}
	editorViewportCenterCursor();
}

static void dapHandleStackTraceResponse(const char *message) {
	E.dap_stack_frame_count = 0;
	dapForEachBodyArrayElement(message, "\"stackFrames\"", dapCollectStackFrame);
	if (E.dap_stack_frame_count > 0) {
		dapRevealStoppedFrame(&E.dap_stack_frames[0]);
		(void)dapSendRequest(dapBuildIntArgRequestJson(
		        g_dap_client.next_seq++, "scopes", "frameId", E.dap_stack_frames[0].id));
	}
}

static void dapHandleScopesResponse(const char *message) {
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
	dapForEachBodyArrayElement(message, "\"scopes\"", dapCollectScope);

	/* Default the register scope(s) to collapsed once per session so the
	 * Variables view is not buried under the register dump; the user's own
	 * toggles afterward persist across steps. */
	if (!g_dap_client.register_collapse_applied) {
		for (int i = 0; i < E.dap_scope_count && i < 64; i++) {
			if (dapScopeNameLooksLikeRegisters(E.dap_scopes[i].name)) {
				E.drawer_dap_scope_collapsed |= 1ull << (unsigned int)i;
			}
		}
		g_dap_client.register_collapse_applied = 1;
	}

	for (int i = 0; i < E.dap_scope_count; i++) {
		if (E.dap_scopes[i].variables_reference <= 0) {
			continue;
		}
		int seq = g_dap_client.next_seq++;
		if (g_dap_client.pending_var_count < ROTIDE_DAP_MAX_SCOPES) {
			g_dap_client.pending_var_seq[g_dap_client.pending_var_count] = seq;
			g_dap_client.pending_var_scope[g_dap_client.pending_var_count] = i;
			g_dap_client.pending_var_count++;
		}
		(void)dapSendRequest(dapBuildVariablesRequestJson(
		        seq, E.dap_scopes[i].variables_reference, 0, 0));
	}
}

/* Determines which scope a `variables` response belongs to: prefer the
 * response's request_seq matched against the pending requests, else fall back
 * to the first scope that has not yet received its variables this stop. */
static int dapResolveVariablesScope(const char *message) {
	int request_seq = 0;
	if (dapTopLevelIntField(message, "\"request_seq\"", &request_seq)) {
		for (int i = 0; i < g_dap_client.pending_var_count; i++) {
			if (g_dap_client.pending_var_seq[i] == request_seq) {
				return g_dap_client.pending_var_scope[i];
			}
		}
		return -1;
	}
	for (int s = 0; s < E.dap_scope_count && s < 64; s++) {
		if ((g_dap_client.var_scopes_received & (1ull << (unsigned int)s)) == 0) {
			return s;
		}
	}
	return 0;
}

static void dapHandleVariablesResponse(const char *message) {
	int parent_index = dapResolveVariablePreviewParent(message);
	if (parent_index >= 0) {
		dapHandleVariablePreviewResponse(message, parent_index);
		return;
	}
	/* Variables share one flat list but each is tagged with its scope so the
	 * drawer can group them under Arguments / Locals / Registers. */
	int scope_index = dapResolveVariablesScope(message);
	if (scope_index < 0) {
		return;
	}
	g_dap_collect_scope_index = scope_index;
	dapForEachBodyArrayElement(message, "\"variables\"", dapCollectVariable);
	if (scope_index >= 0 && scope_index < 64) {
		g_dap_client.var_scopes_received |= 1ull << (unsigned int)scope_index;
	}
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
	const char *start = editorJsonSkipWs(message);
	const char *body_start = NULL;
	const char *body_end = NULL;
	if (start != NULL && start[0] == '{' &&
	    dapObjectChildObject(start, editorJsonFindObjectEnd(start), "\"body\"", &body_start,
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
	(void)dapSendRequest(editorDapBuildSimpleCommandRequestJson(g_dap_client.next_seq++,
	                                                            "configurationDone"));
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
			/* All adapter `output` events go to the Debug Console transcript.
			 * The debuggee's own stdout/stderr does not arrive here — it is
			 * routed to the Terminal tab's real tty via the adapter's --tty. */
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
				editorSetStatusMsg("DAP initialize failed%s%s",
				                   has_errmsg ? ": " : "",
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
			if (strcmp(command, "variables") == 0 &&
			    dapResolveVariablePreviewParent(message) >= 0) {
				return 1;
			}
			/* `configurationDone` answering `notStopped` is expected once the
			 * program is already running; don't nag. Surface other failures with
			 * the adapter's message in the status bar and the console output. */
			if (strcmp(command, "configurationDone") != 0) {
				editorSetStatusMsg("DAP %s failed%s%s", command,
				                   has_errmsg ? ": " : "",
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
	char tty_path[PATH_MAX] = "";
	if (!editorDapPrepareTerminalConsole(&launch_copy, tty_path, sizeof(tty_path))) {
		return 0;
	}

	/* gdb takes the debuggee tty via --tty so program output bypasses the DAP
	 * stream and lands in the Terminal tab. */
	char command[PATH_MAX + 64];
	editorDapBuildAdapterCommand(adapter->command, tty_path, command, sizeof(command));

	pid_t pid = 0;
	int to_fd = -1;
	int from_fd = -1;
	if (!editorLspSpawnProcess(command, &pid, &to_fd, &from_fd)) {
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
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
	g_dap_client.register_collapse_applied = 0;
	E.dap_selected_launch = launch_idx;
	E.dap_running = 1;
	E.dap_stopped = 0;
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';
	E.drawer_dap_scope_collapsed = 0;

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
	int frame_id =
	        (E.dap_stopped && E.dap_stack_frame_count > 0) ? E.dap_stack_frames[0].id : 0;
	dapAppendOutput("> ");
	dapAppendOutput(expr);
	dapAppendOutput("\n");
	if (!dapSendRequest(editorDapBuildEvaluateRequestJson(g_dap_client.next_seq++, expr,
	                                                      frame_id, "repl"))) {
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
		memset(bp, 0, sizeof(*bp));
		bp->kind = EDITOR_DAP_BREAKPOINT_LINE;
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
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
	g_dap_client.register_collapse_applied = 0;
	E.dap_running = 1;
	E.dap_stopped = 0;
	E.drawer_dap_scope_collapsed = 0;
}

void editorDapEndSessionForTest(void) {
	dapClientReset();
}
