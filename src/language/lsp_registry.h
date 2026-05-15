#ifndef LSP_REGISTRY_H
#define LSP_REGISTRY_H

#include "language/lsp_transport.h"

struct editorLspClient *editorLspRegistryFindClient(enum editorLspServerKind server_kind,
		const char *workspace_root_path);
struct editorLspClient *editorLspRegistryAcquireClient(enum editorLspServerKind server_kind,
		const char *workspace_root_path);
void editorLspRegistrySetActiveClient(enum editorLspServerKind server_kind,
		struct editorLspClient *client);
void editorLspRegistryForEachClient(void (*callback)(struct editorLspClient *client, void *ctx),
		void *ctx);
void editorLspRegistryShutdownAll(int graceful_shutdown);
void editorLspRegistryReset(void);

#endif
