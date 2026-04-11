#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "rotide.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(expr) \
	do { \
		if (!(expr)) { \
			fprintf(stderr, "Assertion failed in %s:%d: %s\\n", __func__, __LINE__, #expr); \
			return 1; \
		} \
	} while (0)

#define ASSERT_EQ_INT(expected, actual) \
	do { \
		long expected_val = (long)(expected); \
		long actual_val = (long)(actual); \
		if (expected_val != actual_val) { \
			fprintf(stderr, "Assertion failed in %s:%d: expected %ld, got %ld\\n", \
					__func__, __LINE__, expected_val, actual_val); \
			return 1; \
		} \
	} while (0)

#define ASSERT_EQ_STR(expected, actual) \
	do { \
		if (strcmp((expected), (actual)) != 0) { \
			fprintf(stderr, "Assertion failed in %s:%d: expected \\\"%s\\\", got \\\"%s\\\"\\n", \
					__func__, __LINE__, (expected), (actual)); \
			return 1; \
		} \
	} while (0)

#define ASSERT_MEM_EQ(expected, actual, nbytes) \
	do { \
		if (memcmp((expected), (actual), (nbytes)) != 0) { \
			fprintf(stderr, "Assertion failed in %s:%d: memory mismatch\\n", \
					__func__, __LINE__); \
			return 1; \
		} \
	} while (0)

struct stdoutCapture {
	int saved_stdout;
	int read_fd;
};

void clear_editor_state(void);
void reset_editor_state(void);
void add_row(const char *s);
void add_row_bytes(const char *s, size_t len);

int write_all(int fd, const char *buf, size_t len);
int setup_stdin_bytes(const char *data, size_t len, int *saved_stdin);
int restore_stdin(int saved_stdin);
int redirect_stdout_to_devnull(int *saved_stdout);
int restore_stdout(int saved_stdout);

int start_stdout_capture(struct stdoutCapture *capture);
char *read_all_fd(int fd, size_t *len_out);
char *stop_stdout_capture(struct stdoutCapture *capture, size_t *len_out);
char *read_file_contents(const char *path, size_t *len_out);
void testHelpersInitPaths(const char *startup_cwd);
char *testResolveRepoPath(const char *relative_path);
int copyTestFixtureToPath(const char *fixture_relative_path, const char *target_path);

int editor_read_key_with_input(const char *input, size_t len, int *key_out);
int editor_process_keypress_with_input(const char *input, size_t len);
char *editor_prompt_with_input(const char *input, size_t len, const char *prompt);
char *refresh_screen_and_capture(size_t *len_out);

#endif
