#include "language/dap.h"

#include "config/dap_config.h"
#include "editing/edit.h"
#include "language/lsp_protocol.h"
#include "language/lsp_transport.h"
#include "language/terminal_pane.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "workspace/drawer.h"
#include "workspace/layout.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROTIDE_DAP_MAX_HEADER_BYTES 8192
#define ROTIDE_DAP_IO_TIMEOUT_MS 2500

struct editorDapClient {
	pid_t pid;
	int to_adapter_fd;
	int from_adapter_fd;
	int next_seq;
	int initialized;
};

static struct editorDapClient g_dap_client = {
	.pid = 0,
	.to_adapter_fd = -1,
	.from_adapter_fd = -1,
	.next_seq = 1,
	.initialized = 0,
};

static void editorDapCloseOwnedTerminalPane(void);

static void editorDapClientReset(void) {
	g_dap_client.pid = 0;
	g_dap_client.to_adapter_fd = -1;
	g_dap_client.from_adapter_fd = -1;
	g_dap_client.next_seq = 1;
	g_dap_client.initialized = 0;
	E.dap_running = 0;
	E.dap_stopped = 0;
	E.dap_thread_count = 0;
	E.dap_stack_frame_count = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
}

static void editorDapAppendOutput(const char *text) {
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

static int editorDapWriteAll(int fd, const char *buf, size_t len) {
	while (len > 0) {
		ssize_t written = write(fd, buf, len);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (written == 0) {
			errno = EPIPE;
			return 0;
		}
		buf += (size_t)written;
		len -= (size_t)written;
	}
	return 1;
}

static int editorDapParseContentLength(const char *header, size_t *length_out) {
	const char *line = header;
	while (line != NULL && *line != '\0') {
		const char *line_end = strstr(line, "\r\n");
		if (line_end == NULL) {
			return 0;
		}
		if (line_end == line) {
			break;
		}
		if (strncasecmp(line, "Content-Length:", 15) == 0) {
			const char *value = line + 15;
			while (*value == ' ' || *value == '\t') {
				value++;
			}
			size_t parsed = 0;
			for (const char *p = value; p < line_end; p++) {
				if (!isdigit((unsigned char)*p)) {
					return 0;
				}
				parsed = parsed * 10u + (size_t)(*p - '0');
			}
			*length_out = parsed;
			return 1;
		}
		line = line_end + 2;
	}
	return 0;
}

static char *editorDapReadFrame(int fd) {
	char header[ROTIDE_DAP_MAX_HEADER_BYTES + 1];
	size_t header_len = 0;
	while (header_len < ROTIDE_DAP_MAX_HEADER_BYTES) {
		char ch = '\0';
		ssize_t nread = read(fd, &ch, 1);
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return NULL;
		}
		if (nread == 0) {
			errno = EPIPE;
			return NULL;
		}
		header[header_len++] = ch;
		header[header_len] = '\0';
		if (header_len >= 4 && memcmp(header + header_len - 4, "\r\n\r\n", 4) == 0) {
			break;
		}
	}
	if (header_len >= ROTIDE_DAP_MAX_HEADER_BYTES) {
		errno = EMSGSIZE;
		return NULL;
	}
	size_t payload_len = 0;
	if (!editorDapParseContentLength(header, &payload_len)) {
		errno = EPROTO;
		return NULL;
	}
	char *payload = malloc(payload_len + 1);
	if (payload == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	size_t total = 0;
	while (total < payload_len) {
		ssize_t nread = read(fd, payload + total, payload_len - total);
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			free(payload);
			return NULL;
		}
		if (nread == 0) {
			free(payload);
			errno = EPIPE;
			return NULL;
		}
		total += (size_t)nread;
	}
	payload[payload_len] = '\0';
	return payload;
}

static int editorDapSendRawJson(const char *json) {
	if (json == NULL || g_dap_client.to_adapter_fd == -1) {
		return 0;
	}
	size_t json_len = strlen(json);
	char header[64];
	int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json_len);
	if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
		return 0;
	}
	return editorDapWriteAll(g_dap_client.to_adapter_fd, header, (size_t)header_len) &&
			editorDapWriteAll(g_dap_client.to_adapter_fd, json, json_len);
}

static int editorDapSendRequest(char *json) {
	if (json == NULL) {
		return 0;
	}
	int ok = editorDapSendRawJson(json);
	free(json);
	return ok;
}

static int editorDapAppendJsonEscapedRaw(struct editorLspString *sb,
		const char *text, size_t len) {
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
				if (!editorLspStringAppendf(sb, "\\u%04x", (unsigned int)ch)) {
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

static int editorDapAppendJsonString(struct editorLspString *sb, const char *text) {
	const char *safe = text != NULL ? text : "";
	return editorLspStringAppend(sb, "\"") &&
			editorDapAppendJsonEscapedRaw(sb, safe, strlen(safe)) &&
			editorLspStringAppend(sb, "\"");
}

static int editorDapJsonStringField(const char *json, const char *field, char *buf, size_t bufsize) {
	char *value = NULL;
	if (!editorLspFindStringField(json, field, &value) || value == NULL) {
		return 0;
	}
	int ok = snprintf(buf, bufsize, "%s", value) >= 0 && strlen(value) < bufsize;
	free(value);
	return ok;
}

char *editorDapBuildInitializeRequestJson(int seq, const char *adapter_id) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(&sb,
				"{\"seq\":%d,\"type\":\"request\",\"command\":\"initialize\","
				"\"arguments\":{\"clientID\":\"rotide\",\"clientName\":\"RotIDE\","
				"\"adapterID\":", seq) ||
			!editorDapAppendJsonString(&sb, adapter_id) ||
			!editorLspStringAppend(&sb,
					",\"pathFormat\":\"path\",\"linesStartAt1\":true,"
					"\"columnsStartAt1\":true}}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

char *editorDapBuildSimpleCommandRequestJson(int seq, const char *command) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(&sb, "{\"seq\":%d,\"type\":\"request\",\"command\":",
				seq) ||
			!editorDapAppendJsonString(&sb, command) ||
			!editorLspStringAppend(&sb, "}")) {
		free(sb.buf);
		return NULL;
	}
	return sb.buf;
}

static int editorDapAppendSubstitutedString(struct editorLspString *sb, const char *value,
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
		} else if (strncmp(p, "${fileDirname}", 15) == 0) {
			replacement = file_dir != NULL ? file_dir : "";
			token_len = 15;
		} else if (strncmp(p, "${fileBasename}", 15) == 0) {
			replacement = file_base != NULL ? file_base : "";
			token_len = 15;
		} else if (strncmp(p, "${file}", 7) == 0) {
			replacement = active_file != NULL ? active_file : "";
			token_len = 7;
		}
		if (replacement != NULL) {
			if (!editorDapAppendJsonEscapedRaw(sb, replacement, strlen(replacement))) {
				free(file_dir);
				free(file_base);
				return 0;
			}
			p += token_len;
			continue;
		}
		if (!editorDapAppendJsonEscapedRaw(sb, p, 1)) {
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

static int editorDapAppendLaunchFieldJson(struct editorLspString *sb,
		const struct editorDapLaunchField *field, const char *workspace_root,
		const char *active_file) {
	if (!editorDapAppendJsonString(sb, field->key) ||
			!editorLspStringAppend(sb, ":")) {
		return 0;
	}
	switch (field->kind) {
	case EDITOR_DAP_LAUNCH_VALUE_STRING:
		return editorLspStringAppend(sb, "\"") &&
				editorDapAppendSubstitutedString(sb, field->string_value, workspace_root,
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
					!editorDapAppendSubstitutedString(sb, field->array_values[i],
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
	if (!editorLspStringAppendf(&sb,
				"{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\",\"arguments\":{",
				seq, config->request[0] != '\0' ? config->request : "launch")) {
		free(sb.buf);
		return NULL;
	}
	int wrote_any = 0;
	for (int i = 0; i < config->field_count; i++) {
		if (wrote_any && !editorLspStringAppend(&sb, ",")) {
			free(sb.buf);
			return NULL;
		}
		if (!editorDapAppendLaunchFieldJson(&sb, &config->fields[i], workspace_root,
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
			if (!editorDapAppendJsonString(&sb, config->env[i].key) ||
					!editorLspStringAppend(&sb, ":\"") ||
					!editorDapAppendSubstitutedString(&sb, config->env[i].value,
							workspace_root, active_file) ||
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

static char *editorDapBuildSetBreakpointsRequestJson(int seq, const char *path) {
	struct editorLspString sb = {0};
	if (!editorLspStringAppendf(&sb,
				"{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
				"\"arguments\":{\"source\":{\"path\":", seq) ||
			!editorDapAppendJsonString(&sb, path) ||
			!editorLspStringAppend(&sb, "},\"breakpoints\":[")) {
		free(sb.buf);
		return NULL;
	}
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

static void editorDapSendAllBreakpoints(void) {
	for (int i = 0; i < E.dap_breakpoint_count; i++) {
		int seen = 0;
		for (int j = 0; j < i; j++) {
			if (strcmp(E.dap_breakpoints[i].path, E.dap_breakpoints[j].path) == 0) {
				seen = 1;
				break;
			}
		}
		if (!seen) {
			(void)editorDapSendRequest(editorDapBuildSetBreakpointsRequestJson(
					g_dap_client.next_seq++, E.dap_breakpoints[i].path));
		}
	}
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
			g_dap_client.initialized = 1;
			editorDapSendAllBreakpoints();
			(void)editorDapSendRequest(editorDapBuildSimpleCommandRequestJson(
					g_dap_client.next_seq++, "configurationDone"));
			return 1;
		}
		if (strcmp(event, "stopped") == 0) {
			E.dap_stopped = 1;
			editorSetStatusMsg("DAP stopped");
			(void)editorDapSendRequest(editorDapBuildSimpleCommandRequestJson(
					g_dap_client.next_seq++, "threads"));
			return 1;
		}
		if (strcmp(event, "continued") == 0) {
			E.dap_stopped = 0;
			editorSetStatusMsg("DAP continued");
			return 1;
		}
		if (strcmp(event, "terminated") == 0 || strcmp(event, "exited") == 0) {
			E.dap_running = 0;
			E.dap_stopped = 0;
			editorDapCloseOwnedTerminalPane();
			editorSetStatusMsg("DAP session ended");
			return 1;
		}
		if (strcmp(event, "output") == 0) {
			char output[ROTIDE_DAP_VALUE_MAX];
			if (editorDapJsonStringField(message, "output", output, sizeof(output))) {
				editorDapAppendOutput(output);
			}
			return 1;
		}
		return 1;
	}

	if (editorDapJsonStringField(message, "command", command, sizeof(command))) {
		if (strcmp(command, "threads") == 0) {
			/* v1 keeps response parsing intentionally conservative; output/state still update. */
			return 1;
		}
	}
	return 1;
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
		char *message = editorDapReadFrame(g_dap_client.from_adapter_fd);
		if (message == NULL) {
			editorDapShutdown();
			editorSetStatusMsg("DAP adapter read failed");
			return;
		}
		(void)editorDapProcessIncomingMessage(message);
		free(message);
	}
}

int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config) {
	char console_value[ROTIDE_DAP_VALUE_MAX];
	if (!editorDapLaunchGetStringField(config, "console", console_value,
			sizeof(console_value))) {
		return 1;
	}
	if (strcmp(console_value, "terminal") != 0) {
		/* Other values (none, internalConsole) — leave as-is and strip
		 * the key so adapters don't reject an unknown enum. */
		editorDapLaunchRemoveField(config, "console");
		return 1;
	}
	struct editorPaneNode *terminal_leaf = editorTerminalPaneOpenSplit(
			"sleep infinity", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		editorSetStatusMsg("Could not open terminal pane for DAP console");
		editorDapLaunchRemoveField(config, "console");
		return 0;
	}
	struct editorTerminalPane *tp =
			(struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	const char *slave_path = NULL;
	if (tp != NULL && tp->child.master_fd >= 0) {
		slave_path = ptsname(tp->child.master_fd);
	}
	if (slave_path == NULL ||
			!editorDapLaunchSetStringField(config, "tty", slave_path)) {
		editorSetStatusMsg("Failed to resolve terminal pane tty");
		(void)editorPaneTreeCloseLeaf(&E.layout_root, terminal_leaf);
		if (E.focused_leaf != NULL) {
			(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
		}
		editorDapLaunchRemoveField(config, "console");
		return 0;
	}
	E.dap_terminal_leaf = terminal_leaf;
	editorDapLaunchRemoveField(config, "console");
	return 1;
}

int editorDapStartLaunch(int launch_idx) {
	if (launch_idx < 0 || launch_idx >= E.dap_launch_count) {
		editorSetStatusMsg("No DAP launch config selected");
		return 0;
	}
	const struct editorDapLaunchConfig *config = &E.dap_launches[launch_idx];
	if (strcmp(config->request, "launch") != 0) {
		editorSetStatusMsg("DAP request '%s' is not supported yet", config->request);
		return 0;
	}
	const struct editorDapAdapterConfig *adapter = editorDapAdapterById(config->adapter);
	if (adapter == NULL) {
		editorSetStatusMsg("DAP adapter '%s' is not configured", config->adapter);
		return 0;
	}

	editorDapShutdown();

	/* Work on a local copy so spawn-time mutations (e.g. setting `tty`
	 * after opening a terminal pane for console="terminal") don't pollute
	 * the persistent launch config in E.dap_launches. */
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
	E.dap_selected_launch = launch_idx;
	E.dap_running = 1;
	E.dap_stopped = 0;
	E.dap_output_len = 0;
	E.dap_output[0] = '\0';

	const char *workspace_root = editorDrawerRootPath();
	if (workspace_root == NULL) {
		workspace_root = ".";
	}
	if (!editorDapSendRequest(editorDapBuildInitializeRequestJson(g_dap_client.next_seq++,
				launch_copy.adapter)) ||
			!editorDapSendRequest(editorDapBuildLaunchRequestJson(g_dap_client.next_seq++,
					&launch_copy, workspace_root, E.filename))) {
		editorDapShutdown();
		editorSetStatusMsg("DAP launch request failed");
		return 0;
	}
	editorSetStatusMsg("DAP launched %s", launch_copy.name[0] != '\0' ? launch_copy.name : launch_copy.id);
	return 1;
}

int editorDapStartSelectedLaunch(void) {
	int launch_idx = E.dap_selected_launch;
	if (launch_idx < 0 && E.dap_launch_count > 0) {
		launch_idx = 0;
	}
	return editorDapStartLaunch(launch_idx);
}

static int editorDapSendControl(const char *command) {
	if (!E.dap_running || g_dap_client.to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	if (!editorDapSendRequest(editorDapBuildSimpleCommandRequestJson(g_dap_client.next_seq++,
				command))) {
		editorSetStatusMsg("DAP command failed");
		return 0;
	}
	return 1;
}

int editorDapContinue(void) {
	E.dap_stopped = 0;
	return editorDapSendControl("continue");
}

int editorDapPause(void) {
	return editorDapSendControl("pause");
}

int editorDapStepOver(void) {
	return editorDapSendControl("next");
}

int editorDapStepInto(void) {
	return editorDapSendControl("stepIn");
}

int editorDapStepOut(void) {
	return editorDapSendControl("stepOut");
}

int editorDapStop(void) {
	if (!E.dap_running) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	(void)editorDapSendControl("disconnect");
	editorDapShutdown();
	editorSetStatusMsg("DAP stopped");
	return 1;
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

int editorDapToggleBreakpointAtCursor(void) {
	if (E.filename == NULL || E.filename[0] == '\0') {
		editorSetStatusMsg("Save the file before setting a breakpoint");
		return 0;
	}
	int idx = editorDapHasBreakpoint(E.filename, E.cy);
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
		snprintf(bp->path, sizeof(bp->path), "%s", E.filename);
		bp->line = E.cy;
		editorSetStatusMsg("Breakpoint set");
	}
	if (E.dap_running) {
		(void)editorDapSendRequest(editorDapBuildSetBreakpointsRequestJson(
				g_dap_client.next_seq++, E.filename));
	}
	return 1;
}

static void editorDapCloseOwnedTerminalPane(void) {
	if (E.dap_terminal_leaf == NULL) {
		return;
	}
	struct editorPaneNode *leaf = E.dap_terminal_leaf;
	E.dap_terminal_leaf = NULL;
	if (E.layout_root == NULL ||
			!editorPaneNodeContainsLeaf(E.layout_root, leaf)) {
		return; /* User already closed it. */
	}
	struct editorPaneNode *new_focus =
			editorPaneTreeCloseLeaf(&E.layout_root, leaf);
	if (new_focus != NULL) {
		E.focused_leaf = new_focus;
		(void)editorPaneViewLoadIntoState(&new_focus->as.leaf.view);
	}
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
	editorDapClientReset();
	editorDapCloseOwnedTerminalPane();
}
