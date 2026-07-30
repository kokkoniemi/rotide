#include "language/lsp_transport.h"

#include "language/autocomplete.h"
#include "language/lsp.h"
#include "language/lsp_framing.h"
#include "language/lsp_json.h"
#include "language/lsp_protocol.h"
#include "language/lsp_responses.h"
#include "support/file_io.h"
#include "support/json.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int editorLspProcessAlive(struct editorLspClient *client) {
	if (client == NULL || client->pid <= 0) {
		return 0;
	}

	int status = 0;
	pid_t waited = waitpid(client->pid, &status, WNOHANG);
	if (waited == 0) {
		return 1;
	}
	if (waited == -1 && errno == EINTR) {
		return 1;
	}
	return 0;
}

int editorLspTryDrainIncoming(struct editorLspClient *client, int timeout_ms) {
	if (editorLspMockEnabled() || client == NULL || client->from_server_fd == -1) {
		return 1;
	}

	int wait_ms = timeout_ms;
	for (;;) {
		struct pollfd pfd = {
		        .fd = client->from_server_fd,
		        .events = POLLIN,
		        .revents = 0,
		};
		int polled = poll(&pfd, 1, wait_ms);
		if (polled == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (polled == 0) {
			return 1;
		}
		if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			return 0;
		}

		char *message =
		        editorLspReadFrame(client->from_server_fd, ROTIDE_LSP_IO_TIMEOUT_MS);
		if (message == NULL) {
			return 0;
		}
		int processed = 1;
		int message_id = 0;
		if (client->completion_pending.request_id != 0 &&
		    editorLspExtractResponseId(message, &message_id) &&
		    message_id == client->completion_pending.request_id) {
			struct editorLspCompletionPending pending = client->completion_pending;
			memset(&client->completion_pending, 0, sizeof(client->completion_pending));
			struct editorLspCompletionItem *items = NULL;
			int count = 0;
			if (!editorLspResponseHasError(message)) {
				(void)editorLspParseCompletionResponse(message, &items, &count);
			}
			struct editorAutocompleteResponseSink response = {
			        .request_id = pending.request_id,
			        .document_version = pending.document_version,
			        .request_cy = pending.cy,
			        .request_cx = pending.cx,
			        .prefix_start_cx = pending.prefix_start_cx,
			        .prefix = pending.prefix,
			        .filename = pending.filename,
			        .items = items,
			        .count = count,
			};
			editorAutocompleteHandleCompletionResponse(&response);
			free(pending.prefix);
			free(pending.filename);
		} else {
			processed = editorLspProcessIncomingMessage(client, message);
		}
		free(message);
		if (!processed) {
			return 0;
		}
		wait_ms = 0;
	}
}

static int lspTransportTryGetProcessExitCode(struct editorLspClient *client, int *exit_code_out) {
	if (client == NULL || exit_code_out == NULL || client->pid <= 0) {
		return 0;
	}

	int status = 0;
	pid_t waited = waitpid(client->pid, &status, WNOHANG);
	if (waited <= 0) {
		return 0;
	}
	if (WIFEXITED(status)) {
		*exit_code_out = WEXITSTATUS(status);
		return 1;
	}
	if (WIFSIGNALED(status)) {
		*exit_code_out = 128 + WTERMSIG(status);
		return 1;
	}
	return 0;
}

int editorLspTryGetProcessExitCodeWithWait(struct editorLspClient *client, int timeout_ms,
                                           int *exit_code_out) {
	long long deadline_ms = editorLspMonotonicMillis() + (long long)timeout_ms;

	for (;;) {
		if (lspTransportTryGetProcessExitCode(client, exit_code_out)) {
			return 1;
		}
		if (client == NULL || client->pid <= 0) {
			return 0;
		}
		if (editorLspMonotonicMillis() >= deadline_ms) {
			return 0;
		}
		struct timespec sleep_time = {
		        .tv_sec = 0,
		        .tv_nsec = 1000000L,
		};
		(void)nanosleep(&sleep_time, NULL);
	}
}

void editorLspCompletionPendingClear(struct editorLspCompletionPending *pending) {
	if (pending == NULL) {
		return;
	}
	free(pending->prefix);
	free(pending->filename);
	pending->prefix = NULL;
	pending->filename = NULL;
	pending->request_id = 0;
	pending->document_version = 0;
	pending->cy = 0;
	pending->cx = 0;
	pending->prefix_start_cx = 0;
}

void editorLspClientResetState(struct editorLspClient *client) {
	if (client == NULL) {
		return;
	}
	client->pid = 0;
	client->to_server_fd = -1;
	client->from_server_fd = -1;
	client->initialized = 0;
	client->server_kind = EDITOR_LSP_SERVER_NONE;
	client->next_request_id = 1;
	client->position_encoding_utf16 = 0;
	client->workspace_root_path = NULL;
	client->completion_supported = 0;
	free(client->completion_trigger_chars);
	client->completion_trigger_chars = NULL;
	editorLspCompletionPendingClear(&client->completion_pending);
}

void editorLspClientCleanup(struct editorLspClient *client, int graceful_shutdown) {
	if (client == NULL) {
		return;
	}
	if (client->pid <= 0 && client->to_server_fd == -1 && client->from_server_fd == -1) {
		editorLspClientResetState(client);
		return;
	}

	if (graceful_shutdown && client->initialized && client->to_server_fd != -1 &&
	    client->from_server_fd != -1) {
		int shutdown_id = client->next_request_id++;
		struct editorJsonString shutdown = {0};
		if (editorLspStringAppendf(&shutdown,
		                           "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\","
		                           "\"params\":null}",
		                           shutdown_id)) {
			(void)editorLspSendRawJsonToFd(client->to_server_fd, shutdown.buf);
			char *response = editorLspReadFrame(client->from_server_fd, 500);
			free(response);
		}
		free(shutdown.buf);
		(void)editorLspSendRawJsonToFd(
		        client->to_server_fd,
		        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}");
	}

	if (client->to_server_fd != -1) {
		close(client->to_server_fd);
	}
	if (client->from_server_fd != -1) {
		close(client->from_server_fd);
	}

	if (client->pid > 0) {
		int status = 0;
		pid_t waited = waitpid(client->pid, &status, WNOHANG);
		if (waited == 0) {
			(void)kill(client->pid, SIGTERM);
			(void)waitpid(client->pid, &status, 0);
		}
	}

	free(client->workspace_root_path);
	editorLspClientResetState(client);
}

/* CLOEXEC so other forked children never inherit the server's pipe ends;
 * otherwise a crashed rotide leaves the server without stdin EOF, lingering. */
static int lspTransportPipeCloexec(int fds[2]) {
	if (pipe2(fds, O_CLOEXEC) == 0) {
		return 0;
	}
	if (pipe(fds) == -1) {
		return -1;
	}
	(void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	(void)fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	return 0;
}

int editorLspSpawnProcess(const char *command, pid_t *pid_out, int *to_server_fd_out,
                          int *from_server_fd_out) {
	int stdin_pipe[2] = {-1, -1};
	int stdout_pipe[2] = {-1, -1};
	if (lspTransportPipeCloexec(stdin_pipe) == -1) {
		return 0;
	}
	if (lspTransportPipeCloexec(stdout_pipe) == -1) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return 0;
	}

	pid_t pid = fork();
	if (pid == -1) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return 0;
	}

	if (pid == 0) {
		(void)dup2(stdin_pipe[0], STDIN_FILENO);
		(void)dup2(stdout_pipe[1], STDOUT_FILENO);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull != -1) {
			(void)dup2(devnull, STDERR_FILENO);
			close(devnull);
		}

		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);

		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	*pid_out = pid;
	*to_server_fd_out = stdin_pipe[1];
	*from_server_fd_out = stdout_pipe[0];
	return 1;
}

int editorLspWorkspaceRootsMatch(const char *left, const char *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}
	return editorPathsReferToSameFile(left, right);
}

int editorLspWaitForResponseId(struct editorLspClient *client, int request_id, int timeout_ms,
                               char **response_out, int *timed_out_out) {
	if (response_out == NULL) {
		return 0;
	}
	*response_out = NULL;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	for (;;) {
		char *response = editorLspReadFrame(client != NULL ? client->from_server_fd : -1,
		                                    timeout_ms);
		if (response == NULL) {
			if (timed_out_out != NULL && errno == ETIMEDOUT) {
				*timed_out_out = 1;
			}
			return 0;
		}

		int id = 0;
		int has_id = editorLspExtractResponseId(response, &id);
		if (has_id && id == request_id) {
			*response_out = response;
			return 1;
		}
		if (has_id && client != NULL && client->completion_pending.request_id != 0 &&
		    id == client->completion_pending.request_id) {
			struct editorLspCompletionPending pending = client->completion_pending;
			memset(&client->completion_pending, 0, sizeof(client->completion_pending));
			struct editorLspCompletionItem *items = NULL;
			int count = 0;
			if (!editorLspResponseHasError(response)) {
				(void)editorLspParseCompletionResponse(response, &items, &count);
			}
			struct editorAutocompleteResponseSink completion_response = {
			        .request_id = pending.request_id,
			        .document_version = pending.document_version,
			        .request_cy = pending.cy,
			        .request_cx = pending.cx,
			        .prefix_start_cx = pending.prefix_start_cx,
			        .prefix = pending.prefix,
			        .filename = pending.filename,
			        .items = items,
			        .count = count,
			};
			editorAutocompleteHandleCompletionResponse(&completion_response);
			free(pending.prefix);
			free(pending.filename);
		} else {
			(void)editorLspProcessIncomingMessage(client, response);
		}
		free(response);
	}
}
