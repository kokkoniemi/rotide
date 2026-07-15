#include "terminal/pty.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void editorPtyChildInit(struct editorPtyChild *child) {
	if (child == NULL) {
		return;
	}
	child->master_fd = -1;
	child->slave_fd = -1;
	child->pid = -1;
	child->width = 0;
	child->height = 0;
}

int editorPtySpawn(const char *command, int cols, int rows, struct editorPtyChild *out) {
	if (command == NULL || out == NULL || cols <= 0 || rows <= 0) {
		errno = EINVAL;
		return 0;
	}
	editorPtyChildInit(out);

	struct winsize ws = {0};
	ws.ws_col = (unsigned short)cols;
	ws.ws_row = (unsigned short)rows;

	int master = -1;
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if (pid < 0) {
		return 0;
	}
	if (pid == 0) {
		/* Child: become its own session leader; forkpty already made the
		 * slave the controlling tty. Just exec the command via sh -c. */
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	int flags = fcntl(master, F_GETFL, 0);
	if (flags == -1 || fcntl(master, F_SETFL, flags | O_NONBLOCK) == -1) {
		int saved = errno;
		(void)kill(pid, SIGTERM);
		(void)waitpid(pid, NULL, 0);
		close(master);
		errno = saved;
		return 0;
	}
	(void)fcntl(master, F_SETFD, FD_CLOEXEC);

	out->master_fd = master;
	out->pid = pid;
	out->width = cols;
	out->height = rows;
	return 1;
}

int editorPtyOpenWithoutChild(int cols, int rows, struct editorPtyChild *out, char *slave_path,
                              size_t slave_path_size) {
	if (out == NULL || slave_path == NULL || slave_path_size == 0 || cols <= 0 || rows <= 0) {
		errno = EINVAL;
		return 0;
	}
	editorPtyChildInit(out);

	struct winsize ws = {0};
	ws.ws_col = (unsigned short)cols;
	ws.ws_row = (unsigned short)rows;

	/* openpty instead of posix_openpt+ptsname: Fil-C's musl ptsname traps on
	 * the TIOCGPTN ioctl, and openpty hands back the slave path anyway. */
	int master = -1;
	int slave_fd = -1;
	char slave[128] = {0};
	if (openpty(&master, &slave_fd, slave, NULL, &ws) != 0) {
		return 0;
	}
	if (strlen(slave) >= slave_path_size) {
		close(slave_fd);
		close(master);
		errno = ENAMETOOLONG;
		return 0;
	}
	memcpy(slave_path, slave, strlen(slave) + 1);

	int flags = fcntl(master, F_GETFL, 0);
	if (flags == -1 || fcntl(master, F_SETFL, flags | O_NONBLOCK) == -1) {
		int saved = errno;
		close(slave_fd);
		close(master);
		errno = saved;
		return 0;
	}
	(void)fcntl(master, F_SETFD, FD_CLOEXEC);
	(void)fcntl(slave_fd, F_SETFD, FD_CLOEXEC);

	out->master_fd = master;
	out->slave_fd = slave_fd;
	out->pid = -1;
	out->width = cols;
	out->height = rows;
	return 1;
}

int editorPtyResize(struct editorPtyChild *child, int cols, int rows) {
	if (child == NULL || child->master_fd < 0 || cols <= 0 || rows <= 0) {
		errno = EINVAL;
		return 0;
	}
	struct winsize ws = {0};
	ws.ws_col = (unsigned short)cols;
	ws.ws_row = (unsigned short)rows;
	if (ioctl(child->master_fd, TIOCSWINSZ, &ws) == -1) {
		return 0;
	}
	child->width = cols;
	child->height = rows;
	return 1;
}

int editorPtyTryReap(struct editorPtyChild *child, int *status_out) {
	if (child == NULL || child->pid <= 0) {
		errno = EINVAL;
		return -1;
	}
	int status = 0;
	pid_t r = waitpid(child->pid, &status, WNOHANG);
	if (r == 0) {
		return 0;
	}
	if (r < 0) {
		if (errno == ECHILD) {
			child->pid = -1;
			if (status_out != NULL) {
				*status_out = 0;
			}
			return 1;
		}
		return -1;
	}
	child->pid = -1;
	if (status_out != NULL) {
		*status_out = status;
	}
	return 1;
}

void editorPtyClose(struct editorPtyChild *child) {
	if (child == NULL) {
		return;
	}
	if (child->master_fd >= 0) {
		close(child->master_fd);
		child->master_fd = -1;
	}
	if (child->slave_fd >= 0) {
		close(child->slave_fd);
		child->slave_fd = -1;
	}
	if (child->pid > 0) {
		(void)kill(child->pid, SIGTERM);
		int reaped = 0;
		for (int i = 0; i < 50 && !reaped; i++) {
			pid_t r = waitpid(child->pid, NULL, WNOHANG);
			if (r != 0) {
				reaped = 1;
				break;
			}
			struct timespec ts = {0, 10 * 1000 * 1000};
			nanosleep(&ts, NULL);
		}
		if (!reaped) {
			(void)kill(child->pid, SIGKILL);
			(void)waitpid(child->pid, NULL, 0);
		}
		child->pid = -1;
	}
	child->width = 0;
	child->height = 0;
}
