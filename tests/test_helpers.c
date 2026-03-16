#include "test_helpers.h"

#include "buffer.h"
#include "input.h"
#include "output.h"
#include "terminal.h"

void clear_editor_state(void) {
	for (int i = 0; i < E.numrows; i++) {
		free(E.rows[i].chars);
		free(E.rows[i].render);
	}
	free(E.rows);
	E.rows = NULL;
	E.numrows = 0;

	free(E.filename);
	E.filename = NULL;
}

void reset_editor_state(void) {
	clear_editor_state();
	memset(&E, 0, sizeof(E));
	E.window_rows = 8;
	E.window_cols = 40;
}

void add_row(const char *s) {
	editorInsertRow(E.numrows, (char *)s, strlen(s));
}

void add_row_bytes(const char *s, size_t len) {
	editorInsertRow(E.numrows, (char *)s, len);
}

int write_all(int fd, const char *buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written <= 0) {
			return -1;
		}
		total += (size_t)written;
	}
	return 0;
}

int setup_stdin_bytes(const char *data, size_t len, int *saved_stdin) {
	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return -1;
	}
	if (write_all(pipefd[1], data, len) == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (close(pipefd[1]) == -1) {
		close(pipefd[0]);
		return -1;
	}

	*saved_stdin = dup(STDIN_FILENO);
	if (*saved_stdin == -1) {
		close(pipefd[0]);
		return -1;
	}
	if (dup2(pipefd[0], STDIN_FILENO) == -1) {
		close(pipefd[0]);
		close(*saved_stdin);
		return -1;
	}
	if (close(pipefd[0]) == -1) {
		close(*saved_stdin);
		return -1;
	}

	return 0;
}

int restore_stdin(int saved_stdin) {
	int ret = dup2(saved_stdin, STDIN_FILENO);
	int close_ret = close(saved_stdin);
	if (ret == -1 || close_ret == -1) {
		return -1;
	}
	return 0;
}

int redirect_stdout_to_devnull(int *saved_stdout) {
	int devnull_fd = open("/dev/null", O_WRONLY);
	if (devnull_fd == -1) {
		return -1;
	}

	*saved_stdout = dup(STDOUT_FILENO);
	if (*saved_stdout == -1) {
		close(devnull_fd);
		return -1;
	}
	if (dup2(devnull_fd, STDOUT_FILENO) == -1) {
		close(devnull_fd);
		close(*saved_stdout);
		return -1;
	}
	if (close(devnull_fd) == -1) {
		close(*saved_stdout);
		return -1;
	}

	return 0;
}

int restore_stdout(int saved_stdout) {
	int ret = dup2(saved_stdout, STDOUT_FILENO);
	int close_ret = close(saved_stdout);
	if (ret == -1 || close_ret == -1) {
		return -1;
	}
	return 0;
}

int start_stdout_capture(struct stdoutCapture *capture) {
	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return -1;
	}

	capture->saved_stdout = dup(STDOUT_FILENO);
	if (capture->saved_stdout == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		close(capture->saved_stdout);
		return -1;
	}
	if (close(pipefd[1]) == -1) {
		close(pipefd[0]);
		close(capture->saved_stdout);
		return -1;
	}

	capture->read_fd = pipefd[0];
	return 0;
}

char *read_all_fd(int fd, size_t *len_out) {
	size_t cap = 256;
	size_t len = 0;
	char *buf = malloc(cap + 1);
	if (buf == NULL) {
		return NULL;
	}

	for (;;) {
		if (len == cap) {
			size_t new_cap = cap * 2;
			char *new_buf = realloc(buf, new_cap + 1);
			if (new_buf == NULL) {
				free(buf);
				return NULL;
			}
			buf = new_buf;
			cap = new_cap;
		}

		ssize_t nread = read(fd, buf + len, cap - len);
		if (nread == 0) {
			break;
		}
		if (nread < 0) {
			if (errno == EINTR) {
				continue;
			}
			free(buf);
			return NULL;
		}
		len += (size_t)nread;
	}

	buf[len] = '\0';
	*len_out = len;
	return buf;
}

char *stop_stdout_capture(struct stdoutCapture *capture, size_t *len_out) {
	if (dup2(capture->saved_stdout, STDOUT_FILENO) == -1) {
		close(capture->saved_stdout);
		close(capture->read_fd);
		return NULL;
	}
	if (close(capture->saved_stdout) == -1) {
		close(capture->read_fd);
		return NULL;
	}

	char *output = read_all_fd(capture->read_fd, len_out);
	close(capture->read_fd);
	return output;
}

char *read_file_contents(const char *path, size_t *len_out) {
	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		return NULL;
	}
	char *contents = read_all_fd(fd, len_out);
	close(fd);
	return contents;
}

int editor_read_key_with_input(const char *input, size_t len, int *key_out) {
	int saved_stdin;
	if (setup_stdin_bytes(input, len, &saved_stdin) == -1) {
		return -1;
	}

	*key_out = editorReadKey();

	if (restore_stdin(saved_stdin) == -1) {
		return -1;
	}
	return 0;
}

int editor_process_keypress_with_input(const char *input, size_t len) {
	int saved_stdin;
	if (setup_stdin_bytes(input, len, &saved_stdin) == -1) {
		return -1;
	}

	editorProcessKeypress();

	if (restore_stdin(saved_stdin) == -1) {
		return -1;
	}
	return 0;
}

char *editor_prompt_with_input(const char *input, size_t len, char *prompt) {
	int saved_stdin;
	int saved_stdout;
	if (setup_stdin_bytes(input, len, &saved_stdin) == -1) {
		return NULL;
	}
	if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
		restore_stdin(saved_stdin);
		return NULL;
	}

	char *result = editorPrompt(prompt);

	restore_stdout(saved_stdout);
	restore_stdin(saved_stdin);
	return result;
}

char *refresh_screen_and_capture(size_t *len_out) {
	struct stdoutCapture capture;
	if (start_stdout_capture(&capture) == -1) {
		return NULL;
	}

	editorRefreshScreen();

	return stop_stdout_capture(&capture, len_out);
}
