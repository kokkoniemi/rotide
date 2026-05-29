#include "parallel_runner.h"

#include "editor_state_snapshot.h"
#include "rotide.h"
#include "runner_support.h"
#include "seed.h"
#include "test_case.h"
#include "test_helpers.h"

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARTIFACT_ROOT "tests/artifacts"
#define ARTIFACT_LOGS ARTIFACT_ROOT "/logs"
#define ARTIFACT_CRASHES ARTIFACT_ROOT "/crashes"

static volatile sig_atomic_t g_crash_in_progress = 0;
static const char *g_crash_suite_name = "(unknown)";
static char g_crash_artifact_path[1024];
static char g_crash_marker_path[1024];
static unsigned long long g_crash_seed = 0;
static int g_crash_repeat = 1;
/* MAP_SHARED page so parent can read the in-flight test name on crash. */
static char *g_current_test_marker = NULL;
static const size_t k_marker_size = 256;

static int write_all_fd(int fd, const char *buf, size_t len) {
	while (len > 0) {
		ssize_t n = write(fd, buf, len);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static int signal_safe_strlen(const char *s) {
	int n = 0;
	while (s[n] != '\0') {
		n++;
	}
	return n;
}

static void signal_safe_u_to_str(unsigned long long v, char *out, int *out_len) {
	char tmp[24];
	int n = 0;
	if (v == 0) {
		tmp[n++] = '0';
	} else {
		while (v > 0 && n < (int)sizeof(tmp)) {
			tmp[n++] = (char)('0' + (v % 10));
			v /= 10;
		}
	}
	int i = 0;
	for (int j = n - 1; j >= 0; j--) {
		out[i++] = tmp[j];
	}
	*out_len = i;
}

static void crash_signal_handler(int signo, siginfo_t *info, void *ucontext) {
	(void)info;
	(void)ucontext;
	if (g_crash_in_progress) {
		_exit(128 + signo);
	}
	g_crash_in_progress = 1;

	int fd = open(g_crash_artifact_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd >= 0) {
		(void)write_all_fd(fd, "signal=", 7);
		char buf[32];
		int n = 0;
		signal_safe_u_to_str((unsigned long long)signo, buf, &n);
		(void)write_all_fd(fd, buf, (size_t)n);
		(void)write_all_fd(fd, "\nsuite=", 7);
		(void)write_all_fd(fd, g_crash_suite_name,
		                   (size_t)signal_safe_strlen(g_crash_suite_name));
		(void)write_all_fd(fd, "\ntest=", 6);
		if (g_current_test_marker != NULL && g_current_test_marker[0] != '\0') {
			(void)write_all_fd(fd, g_current_test_marker,
			                   (size_t)signal_safe_strlen(g_current_test_marker));
		} else {
			(void)write_all_fd(fd, "(unknown)", 9);
		}
		(void)write_all_fd(fd, "\nseed=0x", 8);
		unsigned long long s = g_crash_seed;
		char hex[17];
		for (int i = 15; i >= 0; i--) {
			unsigned nibble = (unsigned)(s & 0xf);
			hex[i] = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
			s >>= 4;
		}
		hex[16] = '\0';
		(void)write_all_fd(fd, hex, 16);
		(void)write_all_fd(fd, "\nrepeat=", 8);
		signal_safe_u_to_str((unsigned long long)g_crash_repeat, buf, &n);
		(void)write_all_fd(fd, buf, (size_t)n);
		(void)write_all_fd(fd, "\nbacktrace:\n", 12);
		void *frames[64];
		int nframes = backtrace(frames, 64);
		/* Best-effort; backtrace_symbols_fd isn't AS-safe but works for
		 * test SEGVs that aren't holding the malloc lock. */
		backtrace_symbols_fd(frames, nframes, fd);
		(void)close(fd);
	}

	/* Re-raise so the child terminates with WIFSIGNALED=true. SA_RESETHAND
	 * already restored the default action; _exit() here would mask it. */
	(void)raise(signo);
}

static int install_crash_handlers(void) {
	struct sigaction sa = {0};
	sa.sa_sigaction = crash_signal_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
		if (sigaction(signals[i], &sa, NULL) != 0) {
			return -1;
		}
	}
	return 0;
}

static int mkdir_p(const char *path, mode_t mode) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	char buf[512];
	size_t len = strlen(path);
	if (len >= sizeof(buf)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(buf, path, len + 1);
	for (size_t i = 1; i < len; i++) {
		if (buf[i] == '/') {
			buf[i] = '\0';
			if (mkdir(buf, mode) != 0 && errno != EEXIST) {
				return -1;
			}
			buf[i] = '/';
		}
	}
	if (mkdir(buf, mode) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}

void parallelEnsureArtifactDirs(const char *root) {
	if (root == NULL) {
		root = ARTIFACT_ROOT;
	}
	(void)mkdir_p(root, 0755);
	char buf[512];
	(void)snprintf(buf, sizeof(buf), "%s/logs", root);
	(void)mkdir_p(buf, 0755);
	(void)snprintf(buf, sizeof(buf), "%s/crashes", root);
	(void)mkdir_p(buf, 0755);
}

static char *slurp_file(const char *path, size_t *len_out) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}
	size_t cap = 4096;
	size_t len = 0;
	char *buf = malloc(cap);
	if (buf == NULL) {
		(void)close(fd);
		return NULL;
	}
	while (1) {
		if (len == cap) {
			size_t new_cap = cap * 2;
			char *grown = realloc(buf, new_cap);
			if (grown == NULL) {
				free(buf);
				(void)close(fd);
				return NULL;
			}
			buf = grown;
			cap = new_cap;
		}
		ssize_t n = read(fd, buf + len, cap - len);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			free(buf);
			(void)close(fd);
			return NULL;
		}
		if (n == 0) {
			break;
		}
		len += (size_t)n;
	}
	(void)close(fd);
	if (len_out != NULL) {
		*len_out = len;
	}
	if (len == cap) {
		char *grown = realloc(buf, cap + 1);
		if (grown != NULL) {
			buf = grown;
		}
	}
	buf[len] = '\0';
	return buf;
}

int parallelChildRunBatch(const struct testRunnerOptions *opts, const struct editorTestSuite *suite,
                          struct suiteBatch *batch) {
	g_crash_suite_name = suite->name;
	g_crash_seed = opts->seed;
	g_crash_repeat = opts->repeat;
	(void)snprintf(g_crash_marker_path, sizeof(g_crash_marker_path), "%s/.marker.%d",
	               ARTIFACT_ROOT, (int)getpid());
	char crash_dir[512];
	(void)snprintf(crash_dir, sizeof(crash_dir), "%s/%s", ARTIFACT_CRASHES, suite->name);
	(void)mkdir_p(crash_dir, 0755);
	(void)snprintf(g_crash_artifact_path, sizeof(g_crash_artifact_path), "%s/%s.crash",
	               crash_dir, "unknown");
	(void)install_crash_handlers();

	unsigned char *snapshot = NULL;
	if (opts->validate_reset) {
		snapshot = malloc(EDITOR_STATE_SNAPSHOT_SIZE);
		if (snapshot == NULL) {
			(void)fprintf(stderr, "child(%s): out of memory for snapshot\n",
			              suite->name);
			return EXIT_FAILURE;
		}
		reset_editor_state();
		rotideTestSnapshotEditor(snapshot);
	}

	int local_failed_unique = 0;
	for (int i = 0; i < batch->count; i++) {
		const struct editorTestCase *tc = &suite->tests[batch->test_indices[i]];
		if (g_current_test_marker != NULL) {
			size_t n = strlen(tc->name);
			if (n >= k_marker_size) {
				n = k_marker_size - 1;
			}
			memcpy(g_current_test_marker, tc->name, n);
			g_current_test_marker[n] = '\0';
		}
		(void)snprintf(g_crash_artifact_path, sizeof(g_crash_artifact_path),
		               "%s/%s/%s.crash", ARTIFACT_CRASHES, suite->name, tc->name);
		/* Hook for check_crash_handler.sh; no-op without the env var. */
		const char *crash_match = getenv("ROTIDE_TEST_CRASH");
		if (crash_match != NULL) {
			char want[256];
			(void)snprintf(want, sizeof(want), "%s/%s", suite->name, tc->name);
			if (strcmp(crash_match, want) == 0) {
				volatile int *bad = NULL;
				*bad = 1;
			}
		}
		int local_passed = 0;
		int local_failed = 0;
		for (int rep = 0; rep < opts->repeat; rep++) {
			rotide_test_seed_set(runnerSeedForRepeat(opts->seed, rep));
			batch->total_runs++;
			reset_editor_state();
			struct timespec run_start;
			struct timespec run_end;
			clock_gettime(CLOCK_MONOTONIC, &run_start);
			int failed = tc->run();
			clock_gettime(CLOCK_MONOTONIC, &run_end);
			batch->exec_seconds_total +=
			        (double)(run_end.tv_sec - run_start.tv_sec) +
			        (double)(run_end.tv_nsec - run_start.tv_nsec) / 1e9;
			reset_editor_state();
			if (opts->validate_reset) {
				size_t diff_at = 0;
				if (!rotideTestSnapshotMatchesEditor(snapshot, &diff_at)) {
					batch->reset_violations++;
					const unsigned char *live = (const unsigned char *)&E;
					(void)fprintf(
					        stderr,
					        "RESET-DRIFT after %s (repeat %d/%d): offset=%zu "
					        "snap=0x%02x live=0x%02x\n",
					        tc->name, rep + 1, opts->repeat, diff_at,
					        snapshot[diff_at], live[diff_at]);
				}
			}
			if (failed == 0) {
				batch->passed_runs++;
				local_passed = 1;
				printf("PASS %s", tc->name);
				if (opts->repeat > 1) {
					printf(" (%d/%d)", rep + 1, opts->repeat);
				}
				printf("\n");
			} else {
				local_failed = 1;
				printf("FAIL %s", tc->name);
				if (opts->repeat > 1) {
					printf(" (%d/%d)", rep + 1, opts->repeat);
				}
				printf(" seed=0x%016llx\n", (unsigned long long)opts->seed);
				if (opts->fail_fast) {
					local_failed_unique++;
					batch->failed_unique = local_failed_unique;
					batch->skipped_remaining = batch->count - i - 1;
					free(snapshot);
					return EXIT_FAILURE;
				}
			}
		}
		if (local_failed) {
			local_failed_unique++;
		}
		if (local_passed && local_failed) {
			batch->flakes++;
		}
	}
	batch->failed_unique = local_failed_unique;
	batch->property_ops = test_property_ops_total();
	batch->property_ops_seconds = test_property_ops_elapsed_seconds();
	free(snapshot);
	printf("__CHILD_SUMMARY total=%d passed=%d failed=%d drift=%d flakes=%d "
	       "property_ops=%lld property_ops_seconds=%.9f exec_seconds_total=%.9f\n",
	       batch->total_runs, batch->passed_runs, batch->failed_unique, batch->reset_violations,
	       batch->flakes, batch->property_ops, batch->property_ops_seconds,
	       batch->exec_seconds_total);
	return local_failed_unique > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* Fork-COW means the child's struct updates aren't visible to the parent;
 * counts cross the boundary via a __CHILD_SUMMARY trailer in the log. */
static void parse_child_summary(const char *out, size_t out_len, struct suiteBatch *batch) {
	if (out == NULL || out_len == 0) {
		return;
	}
	const char *p = out + out_len;
	const char *start = NULL;
	while (p > out && (*(p - 1) == '\n' || *(p - 1) == '\r')) {
		p--;
	}
	const char *end = p;
	while (p > out && *(p - 1) != '\n') {
		p--;
	}
	start = p;
	if (end - start < 8) {
		return;
	}
	int total = 0;
	int passed = 0;
	int failed = 0;
	int drift = 0;
	int flakes = 0;
	long long property_ops = 0;
	double property_ops_seconds = 0.0;
	double exec_seconds_total = 0.0;
	if (sscanf(start, // NOLINT(cert-err34-c)
	           "__CHILD_SUMMARY total=%d passed=%d failed=%d drift=%d flakes=%d "
	           "property_ops=%lld property_ops_seconds=%lf exec_seconds_total=%lf",
	           &total, &passed, &failed, &drift, &flakes, &property_ops, &property_ops_seconds,
	           &exec_seconds_total) == 8) {
		batch->total_runs = total;
		batch->passed_runs = passed;
		batch->failed_unique = failed;
		batch->reset_violations = drift;
		batch->flakes = flakes;
		batch->property_ops = property_ops;
		batch->property_ops_seconds = property_ops_seconds;
		batch->exec_seconds_total = exec_seconds_total;
	}
}

static char *strip_summary_trailer(char *out, size_t *out_len) {
	if (out == NULL || *out_len == 0) {
		return out;
	}
	const char *marker = "\n__CHILD_SUMMARY ";
	char *hit = memmem(out, *out_len, marker, strlen(marker));
	if (hit != NULL) {
		*out_len = (size_t)(hit - out) + 1;
		out[*out_len] = '\0';
	}
	return out;
}

static int reap_one_child(struct suiteBatch *batches, int batch_count,
                          const struct editorTestSuite *suites, pid_t *child_pids,
                          struct parallelRunResult *result_out) {
	(void)suites;
	int status = 0;
	pid_t pid = waitpid(-1, &status, 0);
	if (pid < 0) {
		return -1;
	}
	int batch_idx = -1;
	for (int i = 0; i < batch_count; i++) {
		if (child_pids[i] == pid) {
			batch_idx = i;
			break;
		}
	}
	if (batch_idx < 0) {
		return -1;
	}
	child_pids[batch_idx] = -1;

	struct suiteBatch *batch = &batches[batch_idx];
	const struct editorTestSuite *suite = &suites[batch->suite_idx];

	size_t out_len = 0;
	char *out = slurp_file(batch->log_path, &out_len);
	if (out != NULL) {
		parse_child_summary(out, out_len, batch);
		out = strip_summary_trailer(out, &out_len);
		batch->output = out;
		batch->output_len = out_len;
	}

	int marker_fd = open(batch->marker_path, O_RDONLY);
	if (marker_fd >= 0) {
		ssize_t n =
		        read(marker_fd, batch->crash_test_name, sizeof(batch->crash_test_name) - 1);
		if (n > 0) {
			batch->crash_test_name[n] = '\0';
		}
		(void)close(marker_fd);
	}
	(void)unlink(batch->marker_path);
	(void)unlink(batch->log_path);

	if (WIFSIGNALED(status)) {
		batch->crashed = 1;
		batch->crash_signal = WTERMSIG(status);
		(void)snprintf(batch->crash_artifact_path, sizeof(batch->crash_artifact_path),
		               "%s/%s/%s.crash", ARTIFACT_CRASHES, suite->name,
		               batch->crash_test_name[0] ? batch->crash_test_name : "unknown");
		result_out->crashes++;
		result_out->failed_unique++;
		return 0;
	}

	result_out->total_runs += batch->total_runs;
	result_out->passed_runs += batch->passed_runs;
	result_out->failed_unique += batch->failed_unique;
	result_out->reset_violations += batch->reset_violations;
	result_out->flakes += batch->flakes;
	result_out->property_ops += batch->property_ops;
	result_out->property_ops_seconds += batch->property_ops_seconds;
	result_out->exec_seconds_total += batch->exec_seconds_total;
	return 0;
}

static int spawn_child_for_batch(const struct testRunnerOptions *opts,
                                 const struct editorTestSuite *suites, struct suiteBatch *batch,
                                 pid_t *pid_out) {
	const struct editorTestSuite *suite = &suites[batch->suite_idx];
	(void)snprintf(batch->log_path, sizeof(batch->log_path), "%s/%s.log", ARTIFACT_LOGS,
	               suite->name);
	(void)snprintf(batch->marker_path, sizeof(batch->marker_path), "%s/.marker.%s",
	               ARTIFACT_ROOT, suite->name);

	int log_fd = open(batch->log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (log_fd < 0) {
		return -1;
	}
	int marker_fd = open(batch->marker_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (marker_fd < 0) {
		(void)close(log_fd);
		return -1;
	}
	if (ftruncate(marker_fd, (off_t)k_marker_size) != 0) {
		(void)close(marker_fd);
		(void)close(log_fd);
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		(void)close(marker_fd);
		(void)close(log_fd);
		return -1;
	}
	if (pid == 0) {
		void *map =
		        mmap(NULL, k_marker_size, PROT_READ | PROT_WRITE, MAP_SHARED, marker_fd, 0);
		(void)close(marker_fd);
		if (map != MAP_FAILED) {
			g_current_test_marker = (char *)map;
			memset(g_current_test_marker, 0, k_marker_size);
		}
		if (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
			_exit(EXIT_FAILURE);
		}
		(void)close(log_fd);
		int rc = parallelChildRunBatch(opts, suite, batch);
		(void)fflush(stdout);
		(void)fflush(stderr);
		_exit(rc);
	}

	(void)close(log_fd);
	(void)close(marker_fd);
	*pid_out = pid;
	return 0;
}

int parallelRunBatches(const struct testRunnerOptions *opts, const struct editorTestSuite *suites,
                       struct suiteBatch *batches, int batch_count,
                       struct parallelRunResult *result_out) {
	memset(result_out, 0, sizeof(*result_out));
	parallelEnsureArtifactDirs(ARTIFACT_ROOT);

	pid_t *child_pids =
	        calloc((size_t)(batch_count > 0 ? batch_count : 1), sizeof(*child_pids));
	if (child_pids == NULL) {
		return -ENOMEM;
	}
	for (int i = 0; i < batch_count; i++) {
		child_pids[i] = -1;
	}

	int max_jobs = opts->jobs > 0 ? opts->jobs : 1;
	int next_to_spawn = 0;
	int in_flight = 0;
	while (next_to_spawn < batch_count || in_flight > 0) {
		while (in_flight < max_jobs && next_to_spawn < batch_count) {
			pid_t pid = -1;
			if (spawn_child_for_batch(opts, suites, &batches[next_to_spawn], &pid) !=
			    0) {
				free(child_pids);
				return -EIO;
			}
			child_pids[next_to_spawn] = pid;
			next_to_spawn++;
			in_flight++;
		}
		if (in_flight > 0) {
			if (reap_one_child(batches, batch_count, suites, child_pids, result_out) !=
			    0) {
				free(child_pids);
				return -ECHILD;
			}
			in_flight--;
		}
	}

	free(child_pids);
	return (result_out->failed_unique > 0 || result_out->crashes > 0) ? 1 : 0;
}
