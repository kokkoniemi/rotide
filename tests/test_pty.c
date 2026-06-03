#include "terminal/pty.h"
#include "test_case.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int pty_read_until(int fd, char *buf, size_t cap, const char *needle, int timeout_ms) {
	size_t len = 0;
	int waited_ms = 0;
	while (len + 1 < cap && waited_ms < timeout_ms) {
		ssize_t n = read(fd, buf + len, cap - 1 - len);
		if (n > 0) {
			len += (size_t)n;
			buf[len] = '\0';
			if (needle == NULL || strstr(buf, needle) != NULL) {
				return (int)len;
			}
			continue;
		}
		if (n == 0) {
			break;
		}
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			return -1;
		}
		struct pollfd pfd = {.fd = fd, .events = POLLIN};
		int step = 20;
		int pr = poll(&pfd, 1, step);
		waited_ms += step;
		(void)pr;
	}
	return (int)len;
}

static int test_pty_spawn_rejects_invalid_args(void) {
	struct editorPtyChild child;
	editorPtyChildInit(&child);
	if (editorPtySpawn(NULL, 80, 24, &child) != 0) {
		return 1;
	}
	if (editorPtySpawn("true", 0, 24, &child) != 0) {
		return 1;
	}
	if (editorPtySpawn("true", 80, 0, &child) != 0) {
		return 1;
	}
	if (editorPtySpawn("true", 80, 24, NULL) != 0) {
		return 1;
	}
	return 0;
}

static int test_pty_spawn_echo_round_trip(void) {
	struct editorPtyChild child;
	if (!editorPtySpawn("printf 'rotide-pty-marker\\n'", 40, 8, &child)) {
		return 1;
	}
	if (child.master_fd < 0 || child.pid <= 0) {
		editorPtyClose(&child);
		return 1;
	}
	int flags = fcntl(child.master_fd, F_GETFL, 0);
	if (flags == -1 || (flags & O_NONBLOCK) == 0) {
		editorPtyClose(&child);
		return 1;
	}
	char buf[1024];
	int n = pty_read_until(child.master_fd, buf, sizeof(buf), "rotide-pty-marker", 2000);
	int found = n > 0 && strstr(buf, "rotide-pty-marker") != NULL;
	editorPtyClose(&child);
	return found ? 0 : 1;
}

static int test_pty_resize_updates_dimensions(void) {
	struct editorPtyChild child;
	if (!editorPtySpawn("sleep 5", 40, 8, &child)) {
		return 1;
	}
	if (child.width != 40 || child.height != 8) {
		editorPtyClose(&child);
		return 1;
	}
	if (!editorPtyResize(&child, 100, 30)) {
		editorPtyClose(&child);
		return 1;
	}
	int failed = child.width != 100 || child.height != 30;
	editorPtyClose(&child);
	return failed;
}

static int test_pty_try_reap_detects_exit(void) {
	struct editorPtyChild child;
	if (!editorPtySpawn("true", 40, 8, &child)) {
		return 1;
	}
	int status = -1;
	int waited_ms = 0;
	int reaped = 0;
	while (waited_ms < 2000) {
		int r = editorPtyTryReap(&child, &status);
		if (r == 1) {
			reaped = 1;
			break;
		}
		if (r < 0) {
			editorPtyClose(&child);
			return 1;
		}
		struct timespec ts = {0, 20 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited_ms += 20;
	}
	editorPtyClose(&child);
	if (!reaped) {
		return 1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		return 1;
	}
	return 0;
}

static int test_pty_close_is_idempotent(void) {
	struct editorPtyChild child;
	editorPtyChildInit(&child);
	editorPtyClose(&child);
	if (!editorPtySpawn("sleep 5", 40, 8, &child)) {
		return 1;
	}
	editorPtyClose(&child);
	editorPtyClose(&child);
	if (child.master_fd != -1 || child.pid != -1) {
		return 1;
	}
	return 0;
}

static int test_pty_open_without_child_round_trip(void) {
	struct editorPtyChild child;
	char slave_path[256];
	if (!editorPtyOpenWithoutChild(40, 8, &child, slave_path, sizeof(slave_path))) {
		return 1;
	}
	/* No owned process; master is a usable non-blocking fd, slave path resolved. */
	if (child.pid != -1 || child.master_fd < 0 || child.width != 40 || child.height != 8) {
		editorPtyClose(&child);
		return 1;
	}
	int flags = fcntl(child.master_fd, F_GETFL, 0);
	if (flags == -1 || (flags & O_NONBLOCK) == 0 || slave_path[0] != '/') {
		editorPtyClose(&child);
		return 1;
	}
	/* The adapter opens the slave by path; what it writes surfaces on the master. */
	int slave = open(slave_path, O_RDWR | O_NOCTTY);
	if (slave < 0) {
		editorPtyClose(&child);
		return 1;
	}
	ssize_t w = write(slave, "ping\n", 5);
	char buf[64];
	int n = (w == 5) ? pty_read_until(child.master_fd, buf, sizeof(buf), "ping", 2000) : -1;
	int ok = n > 0 && strstr(buf, "ping") != NULL;
	close(slave);
	editorPtyClose(&child);
	/* Closing a childless PTY must not block or fail. */
	return (ok && child.master_fd == -1 && child.pid == -1) ? 0 : 1;
}

static int test_pty_open_without_child_rejects_invalid_args(void) {
	struct editorPtyChild child;
	char slave_path[256];
	if (editorPtyOpenWithoutChild(0, 8, &child, slave_path, sizeof(slave_path)) != 0) {
		return 1;
	}
	if (editorPtyOpenWithoutChild(40, 0, &child, slave_path, sizeof(slave_path)) != 0) {
		return 1;
	}
	if (editorPtyOpenWithoutChild(40, 8, NULL, slave_path, sizeof(slave_path)) != 0) {
		return 1;
	}
	if (editorPtyOpenWithoutChild(40, 8, &child, NULL, sizeof(slave_path)) != 0) {
		return 1;
	}
	if (editorPtyOpenWithoutChild(40, 8, &child, slave_path, 0) != 0) {
		return 1;
	}
	return 0;
}

const struct editorTestCase g_pty_tests[] = {
        {"pty_spawn_rejects_invalid_args", test_pty_spawn_rejects_invalid_args},
        {"pty_spawn_echo_round_trip", test_pty_spawn_echo_round_trip},
        {"pty_resize_updates_dimensions", test_pty_resize_updates_dimensions},
        {"pty_try_reap_detects_exit", test_pty_try_reap_detects_exit},
        {"pty_close_is_idempotent", test_pty_close_is_idempotent},
        {"pty_open_without_child_round_trip", test_pty_open_without_child_round_trip},
        {"pty_open_without_child_rejects_invalid_args",
         test_pty_open_without_child_rejects_invalid_args},
};

const int g_pty_test_count = (int)(sizeof(g_pty_tests) / sizeof(g_pty_tests[0]));
