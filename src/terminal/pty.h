#ifndef ROTIDE_PTY_H
#define ROTIDE_PTY_H

#include <sys/types.h>

/*
 * Pseudo-terminal transport.
 *
 * A PTY is the only portable way to give a child process its own controlling
 * terminal that rotide can read/write over file descriptors. Used by the
 * terminal pane kind (Slice 3c+): the editor reads the child's stdout/stderr
 * from the master fd, writes keypresses to it, and forwards window-size
 * changes via ioctl(TIOCSWINSZ).
 *
 * The master fd is always set non-blocking so callers can poll it alongside
 * other fds (LSP/DAP transports) without dedicating a thread. The child runs
 * under /bin/sh -c so command strings can contain pipes, redirections, and
 * environment-variable expansion.
 */
struct editorPtyChild {
	int master_fd;
	pid_t pid;
	int width;
	int height;
};

void editorPtyChildInit(struct editorPtyChild *child);

/*
 * Spawn `command` in a fresh PTY sized (cols, rows). Returns 1 on success
 * with `out` populated; the master fd is non-blocking. Returns 0 on failure
 * (errno set; nothing is left allocated).
 */
int editorPtySpawn(const char *command, int cols, int rows, struct editorPtyChild *out);

/*
 * Push a new window size to the child via ioctl(TIOCSWINSZ). The child gets
 * SIGWINCH. Returns 1 on success, 0 on failure (errno set).
 */
int editorPtyResize(struct editorPtyChild *child, int cols, int rows);

/*
 * Non-blocking exit reap. Returns 1 if the child has exited and writes the
 * exit status (waitpid-style) to *status_out. Returns 0 if the child is
 * still running. Returns -1 on error (errno set).
 */
int editorPtyTryReap(struct editorPtyChild *child, int *status_out);

/*
 * Close the master fd and SIGTERM the child if it is still running. Safe to
 * call repeatedly; subsequent calls are no-ops.
 */
void editorPtyClose(struct editorPtyChild *child);

#endif
