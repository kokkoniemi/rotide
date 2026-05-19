#ifndef LSP_FRAMING_H
#define LSP_FRAMING_H

#include <stddef.h>

/* Default polling timeout for inter-process LSP RPC. */
#define ROTIDE_LSP_IO_TIMEOUT_MS 2500

/* Upper bound on the header portion of an LSP frame ("Content-Length: N\r\n"
 * plus any optional header lines, terminated by a blank "\r\n"). Anything
 * larger is rejected with errno=EMSGSIZE so a peer cannot wedge the parser
 * by refusing to send the blank terminator. */
#define ROTIDE_LSP_MAX_HEADER_BYTES 8192

/* Upper bound on a single frame's payload, enforced after parsing
 * Content-Length but before allocating. Real clangd / gopls responses
 * top out in the low MiB range even on large projects; 64 MiB leaves
 * comfortable headroom while keeping a malformed or hostile peer from
 * coaxing the client into multi-gigabyte allocations. Rejected with
 * errno=EMSGSIZE. */
#define ROTIDE_LSP_MAX_PAYLOAD_BYTES ((size_t)(64 * 1024 * 1024))

/* Monotonic millisecond clock used by the read-with-deadline helpers. */
long long editorLspMonotonicMillis(void);

/* Writes the full buffer to `fd`, retrying on EINTR and short writes.
 * Returns 1 on success, 0 on error with errno set. */
int editorLspWriteAll(int fd, const char *buf, size_t len);

/* Reads exactly `len` bytes from `fd`, polling until the absolute monotonic
 * deadline (in milliseconds). Returns 1 on success, 0 on error
 * (ETIMEDOUT on deadline expiry, EPIPE on EOF). */
int editorLspReadWithDeadline(int fd, char *buf, size_t len, long long deadline_ms);

/* Parses CRLF-delimited LSP headers and writes the Content-Length value
 * to *length_out. Returns 1 on success, 0 if missing/malformed or if
 * the numeric value overflows size_t. */
int editorLspParseContentLength(const char *header, size_t *length_out);

/* Reads a single LSP frame (headers + payload) from `fd`. Returns a
 * malloc'd, NUL-terminated payload (caller frees) or NULL on error
 * with errno set. `timeout_ms` is wall-clock from the call site. */
char *editorLspReadFrame(int fd, int timeout_ms);

/* Frames the given JSON string with a Content-Length header and writes
 * both to `fd`. Returns 1 on success, 0 on error with errno set. */
int editorLspSendRawJsonToFd(int fd, const char *json);

#endif
