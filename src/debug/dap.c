#include "debug/dap.h"

#include "config/dap_config.h"
#include "debug/dap_breakpoints.h"
#include "debug/dap_client.h"
#include "debug/dap_console.h"
#include "debug/dap_control.h"
#include "debug/dap_inspection.h"
#include "debug/dap_output.h"
#include "debug/dap_protocol.h"
#include "debug/dap_session.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "render/viewport.h"
#include "rotide.h"
#include "support/file_io.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ROTIDE_DAP_IO_TIMEOUT_MS 2500
#define ROTIDE_DAP_VARIABLE_PREVIEW_CHILDREN 6

struct dapClient {
	struct editorDapSessionProcess process;
	struct editorDapSessionHandshake handshake;
	int next_seq;
	/* Thread reported by the most recent `stopped` event; the stack trace and
	 * variable queries that follow a stop are scoped to it. */
	int stopped_thread_id;
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
        .process = {.pid = 0, .to_adapter_fd = -1, .from_adapter_fd = -1},
        .handshake = {.initialized = 0,
                      .state = EDITOR_DAP_SESSION_IDLE,
                      .pending_launch_json = NULL},
        .next_seq = 1,
};

static void dapClientReset(void) {
	editorDapSessionHandshakeReset(&g_dap_client.handshake);
	editorDapSessionProcessReset(&g_dap_client.process);
	g_dap_client.next_seq = 1;
	g_dap_client.stopped_thread_id = 0;
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

static int dapSendRequest(char *json) {
	return editorDapClientSendRequest(g_dap_client.process.to_adapter_fd, json);
}

static void dapSendAllBreakpoints(void) {
	for (int i = 0; i < E.dap_breakpoint_count; i++) {
		if (!editorDapBreakpointsPathWasSeenBefore(i)) {
			(void)dapSendRequest(editorDapBreakpointsBuildSetRequestJson(
			        g_dap_client.next_seq++, E.dap_breakpoints[i].path));
		}
	}
}

static void dapClearInspectionState(void) {
	editorDapInspectionClearState();
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
}

static void dapQueueVariablePreviewRequest(int variable_index) {
	if (g_dap_client.process.to_adapter_fd == -1 || variable_index < 0 ||
	    variable_index >= E.dap_variable_count ||
	    g_dap_client.pending_var_preview_count >= ROTIDE_DAP_MAX_VARIABLES) {
		return;
	}
	const struct editorDapVariable *var = &E.dap_variables[variable_index];
	if (!editorDapInspectionVariableCanPreview(var)) {
		return;
	}
	int child_count = editorDapInspectionVariablePreviewChildCount(
	        var, ROTIDE_DAP_VARIABLE_PREVIEW_CHILDREN);
	if (child_count <= 0) {
		return;
	}
	int seq = g_dap_client.next_seq++;
	g_dap_client.pending_var_preview_seq[g_dap_client.pending_var_preview_count] = seq;
	g_dap_client.pending_var_preview_index[g_dap_client.pending_var_preview_count] =
	        variable_index;
	g_dap_client.pending_var_preview_count++;
	(void)dapSendRequest(
	        editorDapBuildVariablesRequestJson(seq, var->variables_reference, 0, child_count));
}

static int dapResolveVariablePreviewParent(const char *message) {
	int request_seq = 0;
	if (!editorDapJsonTopLevelIntField(message, "\"request_seq\"", &request_seq)) {
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
	(void)dapSendRequest(editorDapBuildIntArgRequestJson(g_dap_client.next_seq++, "stackTrace",
	                                                     "threadId", thread_id));
}

static void dapHandleThreadsResponse(const char *message) {
	editorDapInspectionApplyThreadsResponse(message);
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
	editorDapInspectionApplyStackTraceResponse(message);
	if (E.dap_stack_frame_count > 0) {
		dapRevealStoppedFrame(&E.dap_stack_frames[0]);
		(void)dapSendRequest(editorDapBuildIntArgRequestJson(
		        g_dap_client.next_seq++, "scopes", "frameId", E.dap_stack_frames[0].id));
	}
}

static void dapHandleScopesResponse(const char *message) {
	editorDapInspectionApplyScopesResponse(message);
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;

	/* Default the register scope(s) to collapsed once per session so the
	 * Variables view is not buried under the register dump; the user's own
	 * toggles afterward persist across steps. */
	if (!g_dap_client.register_collapse_applied) {
		for (int i = 0; i < E.dap_scope_count && i < 64; i++) {
			if (editorDapInspectionScopeNameLooksLikeRegisters(E.dap_scopes[i].name)) {
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
		(void)dapSendRequest(editorDapBuildVariablesRequestJson(
		        seq, E.dap_scopes[i].variables_reference, 0, 0));
	}
}

static int dapOnlyUnreceivedVariablesScope(void) {
	int found_scope = -1;
	for (int i = 0; i < g_dap_client.pending_var_count; i++) {
		int scope_index = g_dap_client.pending_var_scope[i];
		if (scope_index < 0 || scope_index >= 64) {
			return -1;
		}
		if ((g_dap_client.var_scopes_received & (1ull << (unsigned int)scope_index)) != 0) {
			continue;
		}
		if (found_scope >= 0) {
			return -1;
		}
		found_scope = scope_index;
	}
	return found_scope;
}

/* Determines which scope a `variables` response belongs to. DAP responses should
 * carry request_seq; tolerate its absence only when one pending scope remains. */
static int dapResolveVariablesScope(const char *message) {
	int request_seq = 0;
	if (editorDapJsonTopLevelIntField(message, "\"request_seq\"", &request_seq)) {
		for (int i = 0; i < g_dap_client.pending_var_count; i++) {
			if (g_dap_client.pending_var_seq[i] == request_seq) {
				return g_dap_client.pending_var_scope[i];
			}
		}
		return -1;
	}
	return dapOnlyUnreceivedVariablesScope();
}

static void dapHandleVariablesResponse(const char *message) {
	int parent_index = dapResolveVariablePreviewParent(message);
	if (parent_index >= 0) {
		editorDapInspectionApplyVariablePreviewResponse(message, parent_index);
		return;
	}
	/* Variables share one flat list but each is tagged with its scope so the
	 * drawer can group them under Arguments / Locals / Registers. */
	int scope_index = dapResolveVariablesScope(message);
	if (scope_index < 0) {
		return;
	}
	int first_variable_index = E.dap_variable_count;
	editorDapInspectionApplyVariablesResponse(message, scope_index, &first_variable_index);
	if (scope_index >= 0 && scope_index < 64) {
		g_dap_client.var_scopes_received |= 1ull << (unsigned int)scope_index;
	}
	for (int i = first_variable_index; i < E.dap_variable_count; i++) {
		dapQueueVariablePreviewRequest(i);
	}
}

/* A successful REPL evaluate: echo the result into the console output stream and
 * the status bar. Failures fall through the generic failed-response path. */
static void dapHandleEvaluateResponse(const char *message) {
	char result[ROTIDE_DAP_VALUE_MAX];
	if (!editorDapJsonBodyStringField(message, "\"result\"", result, sizeof(result))) {
		return;
	}
	editorDapOutputAppend("= ");
	editorDapOutputAppend(result);
	editorDapOutputAppend("\n");
	editorSetStatusMsg("DAP eval: %s", result);
}

/*
 * Best human-readable message from a failed response: the detailed
 * `body.error.format` if present, else the short top-level `message`. Returns 1
 * and fills `buf` on success, 0 if the response carries no message text.
 */
static int dapExtractErrorMessage(const char *message, char *buf, size_t bufsize) {
	const char *error_start = NULL;
	const char *error_end = NULL;
	if (editorDapJsonBodyChildObject(message, "\"error\"", &error_start, &error_end) &&
	    editorDapJsonObjectStringField(error_start, error_end, "\"format\"", buf, bufsize)) {
		return 1;
	}
	return editorDapJsonStringField(message, "message", buf, bufsize);
}

/*
 * Sends the queued `launch` request once `initialize` has been acknowledged.
 * Consumes the queued JSON whether sending succeeds or fails.
 */
static void dapFlushPendingLaunch(void) {
	char *launch_json = editorDapSessionHandshakeConsumeLaunch(&g_dap_client.handshake);
	if (launch_json == NULL) {
		return;
	}
	(void)dapSendRequest(launch_json);
}

/*
 * On the `initialized` event, register breakpoints and signal that
 * configuration is complete. Guarded so it only runs once per session.
 */
static void dapHandleInitializedEvent(void) {
	if (!editorDapSessionHandshakeMarkInitialized(&g_dap_client.handshake)) {
		return;
	}
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
	if (editorDapJsonStringField(message, "type", type, sizeof(type)) &&
	    strcmp(type, "event") == 0 &&
	    editorDapJsonStringField(message, "event", event, sizeof(event))) {
		if (strcmp(event, "initialized") == 0) {
			dapHandleInitializedEvent();
			return 1;
		}
		if (strcmp(event, "stopped") == 0) {
			E.dap_stopped = 1;
			g_dap_client.stopped_thread_id = 0;
			(void)editorDapJsonBodyIntField(message, "\"threadId\"",
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
			if (editorDapJsonBodyStringField(message, "\"output\"", output,
			                                 sizeof(output))) {
				editorDapOutputAppend(output);
			}
			return 1;
		}
		return 1;
	}

	if (editorDapJsonStringField(message, "type", type, sizeof(type)) &&
	    strcmp(type, "response") == 0 &&
	    editorDapJsonStringField(message, "command", command, sizeof(command))) {
		char errmsg[ROTIDE_DAP_VALUE_MAX];
		int has_errmsg = dapExtractErrorMessage(message, errmsg, sizeof(errmsg));
		if (strcmp(command, "initialize") == 0) {
			if (!editorDapJsonResponseSucceeded(message)) {
				editorSetStatusMsg("DAP initialize failed%s%s",
				                   has_errmsg ? ": " : "",
				                   has_errmsg ? errmsg : "");
				editorDapShutdown();
				return 1;
			}
			dapFlushPendingLaunch();
			return 1;
		}
		if (strcmp(command, "launch") == 0 && !editorDapJsonResponseSucceeded(message)) {
			editorSetStatusMsg("DAP launch failed%s%s", has_errmsg ? ": " : "",
			                   has_errmsg ? errmsg : "");
			editorDapShutdown();
			return 1;
		}
		if (!editorDapJsonResponseSucceeded(message)) {
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
				editorDapOutputAppend("[dap] ");
				editorDapOutputAppend(command);
				editorDapOutputAppend(has_errmsg ? " failed: " : " failed");
				if (has_errmsg) {
					editorDapOutputAppend(errmsg);
				}
				editorDapOutputAppend("\n");
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
	return g_dap_client.process.from_adapter_fd;
}

void editorDapPumpNotifications(void) {
	if (g_dap_client.process.from_adapter_fd == -1) {
		return;
	}
	for (;;) {
		struct pollfd pfd = {
		        .fd = g_dap_client.process.from_adapter_fd,
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
		char *message = editorDapClientReadFrame(g_dap_client.process.from_adapter_fd);
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

	if (!editorDapSessionProcessStart(&g_dap_client.process, command)) {
		editorSetStatusMsg("Could not start DAP adapter");
		editorDapShutdown();
		return 0;
	}
	g_dap_client.next_seq = 1;
	g_dap_client.pending_var_count = 0;
	g_dap_client.pending_var_preview_count = 0;
	g_dap_client.var_scopes_received = 0;
	g_dap_client.register_collapse_applied = 0;
	E.dap_selected_launch = launch_idx;
	E.dap_running = 1;
	E.dap_stopped = 0;
	editorDapOutputClear();
	E.drawer_dap_scope_collapsed = 0;

	const char *workspace_root = editorDrawerRootPath();
	if (workspace_root == NULL) {
		workspace_root = ".";
	}
	/*
	 * Build both requests up front so a build failure aborts cleanly, but send
	 * only `initialize` now; `launch` is queued for dapFlushPendingLaunch.
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
	editorDapSessionHandshakeAwaitInitialize(&g_dap_client.handshake, launch_json);
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

static int dapSendThreadControl(const char *command) {
	return editorDapControlSendThread(g_dap_client.process.to_adapter_fd,
	                                  &g_dap_client.next_seq, command,
	                                  g_dap_client.stopped_thread_id);
}

static int dapSendSimpleControl(const char *command) {
	return editorDapControlSendSimple(g_dap_client.process.to_adapter_fd,
	                                  &g_dap_client.next_seq, command);
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
	(void)dapSendSimpleControl("disconnect");
	editorDapShutdown();
	editorSetStatusMsg("DAP stopped");
	return 1;
}

int editorDapEvaluate(const char *expr) {
	if (expr == NULL || expr[0] == '\0') {
		return 0;
	}
	if (!E.dap_running || g_dap_client.process.to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	/* Scope the evaluation to the top frame when stopped; global otherwise. */
	int frame_id =
	        (E.dap_stopped && E.dap_stack_frame_count > 0) ? E.dap_stack_frames[0].id : 0;
	editorDapOutputAppend("> ");
	editorDapOutputAppend(expr);
	editorDapOutputAppend("\n");
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
	enum editorDapBreakpointToggleResult result =
	        editorDapBreakpointsTogglePathLine(E.filename, line);
	switch (result) {
		case EDITOR_DAP_BREAKPOINT_TOGGLE_SET:
			editorSetStatusMsg("Breakpoint set");
			break;
		case EDITOR_DAP_BREAKPOINT_TOGGLE_REMOVED:
			editorSetStatusMsg("Breakpoint removed");
			break;
		case EDITOR_DAP_BREAKPOINT_TOGGLE_TOO_MANY:
			editorSetStatusMsg("Too many DAP breakpoints");
			return 0;
		case EDITOR_DAP_BREAKPOINT_TOGGLE_INVALID:
			return 0;
	}
	if (E.dap_running) {
		(void)dapSendRequest(editorDapBreakpointsBuildSetRequestJson(
		        g_dap_client.next_seq++, E.filename));
	}
	return 1;
}

int editorDapToggleBreakpointAtCursor(void) {
	return editorDapToggleBreakpointAtLine(E.cy);
}

void editorDapShutdown(void) {
	editorDapSessionProcessShutdown(&g_dap_client.process);
	dapClientReset();
	editorDapConsoleCloseOwnedTerminalPane();
}

int editorDapSessionStateForTest(void) {
	return (int)g_dap_client.handshake.state;
}

void editorDapBeginSessionForTest(int to_adapter_fd, char *launch_json) {
	editorDapSessionProcessReset(&g_dap_client.process);
	g_dap_client.process.to_adapter_fd = to_adapter_fd;
	g_dap_client.next_seq = 2; /* initialize already "sent" as seq 1 */
	editorDapSessionHandshakeAwaitInitialize(&g_dap_client.handshake, launch_json);
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
