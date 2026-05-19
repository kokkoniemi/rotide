#ifndef ROTIDE_DEBUG_DAP_CLIENT_H
#define ROTIDE_DEBUG_DAP_CLIENT_H

#include <stddef.h>

/* Upper bound on a single frame's payload, enforced after parsing
 * Content-Length but before allocating. Mirrors the LSP framing cap:
 * real debug adapters send tens of KB at most; 64 MiB is comfortable
 * headroom while preventing a hostile or buggy adapter from coaxing
 * the client into multi-gigabyte allocations. */
#define ROTIDE_DAP_MAX_PAYLOAD_BYTES ((size_t)(64 * 1024 * 1024))

char *editorDapClientReadFrame(int from_adapter_fd);
int editorDapClientSendRequest(int to_adapter_fd, char *json);

#endif
