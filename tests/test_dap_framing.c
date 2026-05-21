/* Unit tests for the DAP framing parser (src/debug/dap_client.c).
 *
 * Locks in the same guarantees the LSP framing suite covers, since the
 * two parsers share a wire format but are duplicate code: malformed
 * Content-Length, overflow rejection, oversized payload rejection,
 * and multi-frame draining. */

#define _GNU_SOURCE

#include "debug/dap_client.h"
#include "test_case.h"
#include "test_helpers.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int memfd_with(const void *data, size_t size) {
	int fd = memfd_create("dap_framing_test", 0);
	if (fd < 0) {
		return -1;
	}
	if (size > 0) {
		ssize_t written = write(fd, data, size);
		if (written < 0 || (size_t)written != size) {
			close(fd);
			return -1;
		}
	}
	if (lseek(fd, 0, SEEK_SET) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int test_read_frame_valid(void) {
	const char *stream = "Content-Length: 2\r\n\r\n{}";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	char *payload = editorDapClientReadFrame(fd);
	ASSERT_TRUE(payload != NULL);
	ASSERT_EQ_STR("{}", payload);
	free(payload);
	close(fd);
	return 0;
}

static int test_read_frame_zero_length(void) {
	const char *stream = "Content-Length: 0\r\n\r\n";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	char *payload = editorDapClientReadFrame(fd);
	ASSERT_TRUE(payload != NULL);
	ASSERT_EQ_STR("", payload);
	free(payload);
	close(fd);
	return 0;
}

static int test_read_frame_two_back_to_back(void) {
	const char *stream = "Content-Length: 3\r\n\r\nabcContent-Length: 5\r\n\r\nhello";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);

	char *first = editorDapClientReadFrame(fd);
	ASSERT_TRUE(first != NULL);
	ASSERT_EQ_STR("abc", first);
	free(first);

	char *second = editorDapClientReadFrame(fd);
	ASSERT_TRUE(second != NULL);
	ASSERT_EQ_STR("hello", second);
	free(second);

	char *third = editorDapClientReadFrame(fd);
	ASSERT_TRUE(third == NULL);

	close(fd);
	return 0;
}

static int test_read_frame_truncated_payload(void) {
	const char *stream = "Content-Length: 10\r\n\r\nabc";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	char *payload = editorDapClientReadFrame(fd);
	ASSERT_TRUE(payload == NULL);
	close(fd);
	return 0;
}

static int test_read_frame_malformed_content_length(void) {
	const char *stream = "Content-Length: abc\r\n\r\n";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	errno = 0;
	char *payload = editorDapClientReadFrame(fd);
	ASSERT_TRUE(payload == NULL);
	ASSERT_EQ_INT(EPROTO, errno);
	close(fd);
	return 0;
}

static int test_read_frame_rejects_overflowing_content_length(void) {
	/* 21 nines: must be rejected at parse time rather than silently
	 * wrapping. */
	const char *stream = "Content-Length: 999999999999999999999\r\n\r\n";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	errno = 0;
	char *payload = editorDapClientReadFrame(fd);
	ASSERT_TRUE(payload == NULL);
	ASSERT_EQ_INT(EPROTO, errno);
	close(fd);
	return 0;
}

static int test_read_frame_rejects_oversized_payload(void) {
	/* 20-digit Content-Length that fits in size_t but exceeds the
	 * per-frame ceiling. Must reject before reaching malloc. */
	const char *stream = "Content-Length: 07766279631452241918\r\n\r\n";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	errno = 0;
	char *payload = editorDapClientReadFrame(fd);
	ASSERT_TRUE(payload == NULL);
	ASSERT_EQ_INT(EMSGSIZE, errno);
	close(fd);
	return 0;
}

const struct editorTestCase g_dap_framing_tests[] = {
        {"read_frame_valid", test_read_frame_valid},
        {"read_frame_zero_length", test_read_frame_zero_length},
        {"read_frame_two_back_to_back", test_read_frame_two_back_to_back},
        {"read_frame_truncated_payload", test_read_frame_truncated_payload},
        {"read_frame_malformed_content_length", test_read_frame_malformed_content_length},
        {"read_frame_rejects_overflowing_content_length",
         test_read_frame_rejects_overflowing_content_length},
        {"read_frame_rejects_oversized_payload", test_read_frame_rejects_oversized_payload},
};

const int g_dap_framing_test_count =
        (int)(sizeof(g_dap_framing_tests) / sizeof(g_dap_framing_tests[0]));
