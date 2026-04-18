/* Included by lsp.c. Shared process, framing, and transport helpers live here. */

static long long editorLspMonotonicMillis(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}


static int editorLspWriteAll(int fd, const char *buf, size_t len) {
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

static int editorLspReadWithDeadline(int fd, char *buf, size_t len, long long deadline_ms) {
	size_t total = 0;
	while (total < len) {
		long long now = editorLspMonotonicMillis();
		int wait_ms = 0;
		if (deadline_ms > 0) {
			if (now >= deadline_ms) {
				errno = ETIMEDOUT;
				return 0;
			}
			long long remaining = deadline_ms - now;
			wait_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
		}

		struct pollfd pfd = {
			.fd = fd,
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
			errno = ETIMEDOUT;
			return 0;
		}
		if ((pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
			errno = EPIPE;
			return 0;
		}

		ssize_t nread = read(fd, buf + total, len - total);
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (nread == 0) {
			errno = EPIPE;
			return 0;
		}
		total += (size_t)nread;
	}
	return 1;
}

static int editorLspParseContentLength(const char *header, size_t *length_out) {
	if (header == NULL || length_out == NULL) {
		return 0;
	}

	const char *line = header;
	while (*line != '\0') {
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

			unsigned long parsed = 0;
			for (const char *p = value; p < line_end; p++) {
				if (!isdigit((unsigned char)*p)) {
					return 0;
				}
				parsed = parsed * 10UL + (unsigned long)(*p - '0');
				if (parsed > SIZE_MAX) {
					return 0;
				}
			}
			*length_out = (size_t)parsed;
			return 1;
		}
		line = line_end + 2;
	}

	return 0;
}

static char *editorLspReadFrame(int fd, int timeout_ms) {
	long long now = editorLspMonotonicMillis();
	long long deadline_ms = now + timeout_ms;
	char header[ROTIDE_LSP_MAX_HEADER_BYTES + 1];
	size_t header_len = 0;

	while (header_len < ROTIDE_LSP_MAX_HEADER_BYTES) {
		if (!editorLspReadWithDeadline(fd, &header[header_len], 1, deadline_ms)) {
			return NULL;
		}
		header_len++;
		header[header_len] = '\0';
		if (header_len >= 4 &&
				header[header_len - 4] == '\r' &&
				header[header_len - 3] == '\n' &&
				header[header_len - 2] == '\r' &&
				header[header_len - 1] == '\n') {
			break;
		}
	}

	if (header_len >= ROTIDE_LSP_MAX_HEADER_BYTES) {
		errno = EMSGSIZE;
		return NULL;
	}

	size_t payload_len = 0;
	if (!editorLspParseContentLength(header, &payload_len)) {
		errno = EPROTO;
		return NULL;
	}

	size_t alloc_len = 0;
	if (!editorSizeAdd(payload_len, 1, &alloc_len)) {
		errno = EOVERFLOW;
		return NULL;
	}
	char *payload = malloc(alloc_len);
	if (payload == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	if (!editorLspReadWithDeadline(fd, payload, payload_len, deadline_ms)) {
		free(payload);
		return NULL;
	}
	payload[payload_len] = '\0';
	return payload;
}

static int editorLspSendRawJsonToFd(int fd, const char *json) {
	if (json == NULL || fd == -1) {
		return 0;
	}

	size_t json_len = strlen(json);
	char header[64];
	int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json_len);
	if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
		errno = EOVERFLOW;
		return 0;
	}

	if (!editorLspWriteAll(fd, header, (size_t)header_len) ||
			!editorLspWriteAll(fd, json, json_len)) {
		return 0;
	}
	return 1;
}

static int editorLspSendRawJson(const char *json) {
	return editorLspSendRawJsonToFd(g_lsp_client.to_server_fd, json);
}


static int editorLspProcessAlive(struct editorLspClient *client) {
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


static int editorLspTryDrainIncoming(struct editorLspClient *client, int timeout_ms) {
	if (g_lsp_mock.enabled || client == NULL || client->from_server_fd == -1) {
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

		char *message = editorLspReadFrame(client->from_server_fd, ROTIDE_LSP_IO_TIMEOUT_MS);
		if (message == NULL) {
			return 0;
		}
		int processed = editorLspProcessIncomingMessage(client, message);
		free(message);
		if (!processed) {
			return 0;
		}
		wait_ms = 0;
	}
}

static int editorLspTryGetProcessExitCode(struct editorLspClient *client, int *exit_code_out) {
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

static int editorLspTryGetProcessExitCodeWithWait(struct editorLspClient *client, int timeout_ms,
		int *exit_code_out) {
	long long deadline_ms = editorLspMonotonicMillis() + (long long)timeout_ms;

	for (;;) {
		if (editorLspTryGetProcessExitCode(client, exit_code_out)) {
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


static void editorLspClientResetState(struct editorLspClient *client) {
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
}

static void editorLspClientCleanup(struct editorLspClient *client, int graceful_shutdown) {
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
		struct editorLspString shutdown = {0};
		if (editorLspStringAppendf(&shutdown,
					"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\",\"params\":null}",
					shutdown_id)) {
			(void)editorLspSendRawJsonToFd(client->to_server_fd, shutdown.buf);
			char *response = editorLspReadFrame(client->from_server_fd, 500);
			free(response);
		}
		free(shutdown.buf);
		(void)editorLspSendRawJsonToFd(client->to_server_fd,
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

static int editorLspSpawnProcess(const char *command, pid_t *pid_out, int *to_server_fd_out,
		int *from_server_fd_out) {
	int stdin_pipe[2] = {-1, -1};
	int stdout_pipe[2] = {-1, -1};
	if (pipe(stdin_pipe) == -1) {
		return 0;
	}
	if (pipe(stdout_pipe) == -1) {
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

static int editorLspWorkspaceRootsMatch(const char *left, const char *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}
	return editorPathsReferToSameFile(left, right);
}


static int editorLspWaitForResponseId(struct editorLspClient *client, int request_id, int timeout_ms,
		char **response_out, int *timed_out_out) {
	if (response_out == NULL) {
		return 0;
	}
	*response_out = NULL;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	for (;;) {
		char *response =
				editorLspReadFrame(client != NULL ? client->from_server_fd : -1, timeout_ms);
		if (response == NULL) {
			if (timed_out_out != NULL && errno == ETIMEDOUT) {
				*timed_out_out = 1;
			}
			return 0;
		}

		int id = 0;
		if (editorLspExtractResponseId(response, &id) && id == request_id) {
			*response_out = response;
			return 1;
		}
		(void)editorLspProcessIncomingMessage(client, response);
		free(response);
	}
}


