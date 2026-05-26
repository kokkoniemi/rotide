/* Unit tests for the LSP framing parser (src/language/lsp_framing.c).
 *
 * The fuzz harness explores untrusted inputs broadly; this suite pins
 * the protocol guarantees that should fail loudly in normal regression
 * runs: header shape, malformed Content-Length, overflow rejection,
 * oversized payload rejection, and multi-frame draining. */

#include "language/lsp_framing.h"
#include "test_case.h"
#include "test_helpers.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

static int memfd_with(const void *data, size_t size) {
	int fd = memfd_create("lsp_framing_test", 0);
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

static int test_parse_content_length_valid(void) {
	size_t len = 0;
	ASSERT_EQ_INT(1, editorLspParseContentLength("Content-Length: 42\r\n\r\n", &len));
	ASSERT_EQ_INT(42, (long)len);
	return 0;
}

static int test_parse_content_length_zero(void) {
	size_t len = 999;
	ASSERT_EQ_INT(1, editorLspParseContentLength("Content-Length: 0\r\n\r\n", &len));
	ASSERT_EQ_INT(0, (long)len);
	return 0;
}

static int test_parse_content_length_case_insensitive(void) {
	size_t len = 0;
	ASSERT_EQ_INT(1, editorLspParseContentLength("content-length: 7\r\n\r\n", &len));
	ASSERT_EQ_INT(7, (long)len);
	return 0;
}

static int test_parse_content_length_skips_other_headers(void) {
	const char *header = "Content-Type: application/vscode-jsonrpc\r\n"
	                     "Content-Length: 12\r\n\r\n";
	size_t len = 0;
	ASSERT_EQ_INT(1, editorLspParseContentLength(header, &len));
	ASSERT_EQ_INT(12, (long)len);
	return 0;
}

static int test_parse_content_length_rejects_non_digits(void) {
	size_t len = 0;
	ASSERT_EQ_INT(0, editorLspParseContentLength("Content-Length: 1a\r\n\r\n", &len));
	return 0;
}

static int test_parse_content_length_rejects_negative(void) {
	size_t len = 0;
	ASSERT_EQ_INT(0, editorLspParseContentLength("Content-Length: -1\r\n\r\n", &len));
	return 0;
}

static int test_parse_content_length_rejects_overflow(void) {
	/* 21 nines: comfortably above 2^64 - 1, must be rejected without
	 * silently wrapping. */
	size_t len = 999;
	ASSERT_EQ_INT(0, editorLspParseContentLength(
	                         "Content-Length: 999999999999999999999\r\n\r\n", &len));
	return 0;
}

static int test_parse_content_length_missing_returns_zero(void) {
	size_t len = 0;
	ASSERT_EQ_INT(0, editorLspParseContentLength("Content-Type: x\r\n\r\n", &len));
	return 0;
}

static int test_read_frame_valid(void) {
	const char *stream = "Content-Length: 2\r\n\r\n{}";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	char *payload = editorLspReadFrame(fd, 200);
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
	char *payload = editorLspReadFrame(fd, 200);
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

	char *first = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(first != NULL);
	ASSERT_EQ_STR("abc", first);
	free(first);

	char *second = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(second != NULL);
	ASSERT_EQ_STR("hello", second);
	free(second);

	char *third = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(third == NULL);

	close(fd);
	return 0;
}

static int test_read_frame_truncated_payload(void) {
	const char *stream = "Content-Length: 10\r\n\r\nabc";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	char *payload = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(payload == NULL);
	close(fd);
	return 0;
}

static int test_read_frame_malformed_header(void) {
	const char *stream = "Content-Length: abc\r\n\r\n";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	errno = 0;
	char *payload = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(payload == NULL);
	ASSERT_EQ_INT(EPROTO, errno);
	close(fd);
	return 0;
}

static int test_read_frame_rejects_oversized_payload(void) {
	/* A 20-digit Content-Length can fit in size_t while still exceeding any
	 * reasonable frame size. Reject it with EMSGSIZE before malloc. */
	const char *stream = "Content-Length: 07766279631452241918\r\n\r\n";
	int fd = memfd_with(stream, strlen(stream));
	ASSERT_TRUE(fd >= 0);
	errno = 0;
	char *payload = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(payload == NULL);
	ASSERT_EQ_INT(EMSGSIZE, errno);
	close(fd);
	return 0;
}

static int test_read_frame_header_too_long(void) {
	/* ROTIDE_LSP_MAX_HEADER_BYTES bytes of "X-Filler: AAAA..." with no
	 * blank-line terminator must be rejected with EMSGSIZE. */
	char *padding = malloc(ROTIDE_LSP_MAX_HEADER_BYTES + 1);
	ASSERT_TRUE(padding != NULL);
	memset(padding, 'A', ROTIDE_LSP_MAX_HEADER_BYTES);
	padding[ROTIDE_LSP_MAX_HEADER_BYTES] = '\0';
	int fd = memfd_with(padding, ROTIDE_LSP_MAX_HEADER_BYTES);
	free(padding);
	ASSERT_TRUE(fd >= 0);
	errno = 0;
	char *payload = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(payload == NULL);
	ASSERT_EQ_INT(EMSGSIZE, errno);
	close(fd);
	return 0;
}

static int test_send_raw_json_round_trip(void) {
	int fd = memfd_create("lsp_framing_send", 0);
	ASSERT_TRUE(fd >= 0);

	const char *body = "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}";
	ASSERT_EQ_INT(1, editorLspSendRawJsonToFd(fd, body));

	ASSERT_EQ_INT(0, (int)lseek(fd, 0, SEEK_SET));
	char *payload = editorLspReadFrame(fd, 200);
	ASSERT_TRUE(payload != NULL);
	ASSERT_EQ_STR(body, payload);
	free(payload);
	close(fd);
	return 0;
}

const struct editorTestCase g_lsp_framing_tests[] = {
        {"parse_content_length_valid", test_parse_content_length_valid},
        {"parse_content_length_zero", test_parse_content_length_zero},
        {"parse_content_length_case_insensitive", test_parse_content_length_case_insensitive},
        {"parse_content_length_skips_other_headers", test_parse_content_length_skips_other_headers},
        {"parse_content_length_rejects_non_digits", test_parse_content_length_rejects_non_digits},
        {"parse_content_length_rejects_negative", test_parse_content_length_rejects_negative},
        {"parse_content_length_rejects_overflow", test_parse_content_length_rejects_overflow},
        {"parse_content_length_missing_returns_zero",
         test_parse_content_length_missing_returns_zero},
        {"read_frame_valid", test_read_frame_valid},
        {"read_frame_zero_length", test_read_frame_zero_length},
        {"read_frame_two_back_to_back", test_read_frame_two_back_to_back},
        {"read_frame_truncated_payload", test_read_frame_truncated_payload},
        {"read_frame_malformed_header", test_read_frame_malformed_header},
        {"read_frame_rejects_oversized_payload", test_read_frame_rejects_oversized_payload},
        {"read_frame_header_too_long", test_read_frame_header_too_long},
        {"send_raw_json_round_trip", test_send_raw_json_round_trip},
};

const int g_lsp_framing_test_count =
        (int)(sizeof(g_lsp_framing_tests) / sizeof(g_lsp_framing_tests[0]));
