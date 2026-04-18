#ifndef LSP_INTERNAL_H
#define LSP_INTERNAL_H

#include "language/lsp.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "text/utf8.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ROTIDE_LSP_IO_TIMEOUT_MS 2500
#define ROTIDE_LSP_MAX_HEADER_BYTES 8192

enum editorLspServerKind {
	EDITOR_LSP_SERVER_NONE = 0,
	EDITOR_LSP_SERVER_GOPLS,
	EDITOR_LSP_SERVER_CLANGD,
	EDITOR_LSP_SERVER_HTML,
	EDITOR_LSP_SERVER_CSS,
	EDITOR_LSP_SERVER_JSON,
	EDITOR_LSP_SERVER_JAVASCRIPT,
	EDITOR_LSP_SERVER_ESLINT
};

struct editorLspClient {
	pid_t pid;
	int to_server_fd;
	int from_server_fd;
	int initialized;
	enum editorLspServerKind server_kind;
	enum editorLspServerKind disabled_for_position_encoding_server_kind;
	int next_request_id;
	int position_encoding_utf16;
	char *workspace_root_path;
};

struct editorLspString {
	char *buf;
	size_t len;
	size_t cap;
};

struct editorLspPendingEdit {
	int start_line;
	int start_character;
	int end_line;
	int end_character;
	char *new_text;
};

struct editorLspMockState {
	int enabled;
	int primary_server_alive;
	enum editorLspServerKind primary_server_kind;
	char *primary_workspace_root_path;
	int eslint_server_alive;
	char *eslint_workspace_root_path;
	char last_did_open_language_id[32];
	struct editorLspTestStats stats;
	struct editorLspTestLastChange last_change;
	int definition_result_code;
	struct editorLspLocation *definition_locations;
	int definition_location_count;
	struct editorLspDiagnostic *diagnostics;
	int diagnostic_count;
	char *diagnostic_path;
	int code_action_result_code;
	struct editorLspPendingEdit *code_action_edits;
	int code_action_edit_count;
};

#endif
