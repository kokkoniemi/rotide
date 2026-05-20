#ifndef ROTIDE_LANGUAGE_LSP_TRANSPORT_H
#define ROTIDE_LANGUAGE_LSP_TRANSPORT_H

#include "language/lsp_framing.h"
#include "rotide.h"

#include <sys/types.h>

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

struct editorLspCompletionPending {
	int request_id;
	int document_version;
	int cy;
	int cx;
	int prefix_start_cx;
	char *prefix;
	char *filename;
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
	int completion_supported;
	char *completion_trigger_chars;
	struct editorLspCompletionPending completion_pending;
};

struct editorLspClient *editorLspPrimaryClient(void);
struct editorLspClient *editorLspEslintClient(void);

int editorLspMockEnabled(void);

void editorLspClientCleanup(struct editorLspClient *client, int graceful_shutdown);
void editorLspClientResetState(struct editorLspClient *client);
void editorLspCompletionPendingClear(struct editorLspCompletionPending *pending);
int editorLspProcessAlive(struct editorLspClient *client);
int editorLspSpawnProcess(const char *command, pid_t *pid_out, int *to_server_fd_out,
                          int *from_server_fd_out);
int editorLspTryDrainIncoming(struct editorLspClient *client, int timeout_ms);
int editorLspTryGetProcessExitCodeWithWait(struct editorLspClient *client, int timeout_ms,
                                           int *exit_code_out);
int editorLspWaitForResponseId(struct editorLspClient *client, int request_id, int timeout_ms,
                               char **response_out, int *timed_out_out);
int editorLspWorkspaceRootsMatch(const char *left, const char *right);

#endif
