#include "language/lsp_registry.h"

#include <stdlib.h>
#include <string.h>

#define ROTIDE_LSP_REGISTRY_MAX_CLIENTS 16

struct editorLspRegistryState {
	int initialized;
	struct editorLspClient fallback_primary;
	struct editorLspClient fallback_eslint;
	struct editorLspClient *primary_active;
	struct editorLspClient *eslint_active;
	struct editorLspClient *clients[ROTIDE_LSP_REGISTRY_MAX_CLIENTS];
	int client_count;
};

static struct editorLspRegistryState g_lsp_registry = {0};

static int editorLspRegistryServerKindIsEslint(enum editorLspServerKind server_kind) {
	return server_kind == EDITOR_LSP_SERVER_ESLINT;
}

static void editorLspRegistryResetClientStruct(struct editorLspClient *client) {
	if (client == NULL) {
		return;
	}
	memset(client, 0, sizeof(*client));
	client->to_server_fd = -1;
	client->from_server_fd = -1;
	client->server_kind = EDITOR_LSP_SERVER_NONE;
	client->disabled_for_position_encoding_server_kind = EDITOR_LSP_SERVER_NONE;
	client->next_request_id = 1;
}

static void editorLspRegistryInitIfNeeded(void) {
	if (g_lsp_registry.initialized) {
		return;
	}
	editorLspRegistryResetClientStruct(&g_lsp_registry.fallback_primary);
	editorLspRegistryResetClientStruct(&g_lsp_registry.fallback_eslint);
	g_lsp_registry.primary_active = &g_lsp_registry.fallback_primary;
	g_lsp_registry.eslint_active = &g_lsp_registry.fallback_eslint;
	g_lsp_registry.initialized = 1;
}

struct editorLspClient *editorLspPrimaryClient(void) {
	editorLspRegistryInitIfNeeded();
	return g_lsp_registry.primary_active != NULL ? g_lsp_registry.primary_active :
			&g_lsp_registry.fallback_primary;
}

struct editorLspClient *editorLspEslintClient(void) {
	editorLspRegistryInitIfNeeded();
	return g_lsp_registry.eslint_active != NULL ? g_lsp_registry.eslint_active :
			&g_lsp_registry.fallback_eslint;
}

struct editorLspClient *editorLspRegistryFindClient(enum editorLspServerKind server_kind,
		const char *workspace_root_path) {
	editorLspRegistryInitIfNeeded();
	if (workspace_root_path == NULL || workspace_root_path[0] == '\0') {
		return NULL;
	}
	for (int i = 0; i < g_lsp_registry.client_count; i++) {
		struct editorLspClient *client = g_lsp_registry.clients[i];
		if (client == NULL || client->server_kind != server_kind) {
			continue;
		}
		if (editorLspWorkspaceRootsMatch(client->workspace_root_path, workspace_root_path)) {
			return client;
		}
	}
	return NULL;
}

static struct editorLspClient *editorLspRegistrySelectEvictionCandidate(void) {
	for (int i = 0; i < g_lsp_registry.client_count; i++) {
		struct editorLspClient *candidate = g_lsp_registry.clients[i];
		if (candidate == NULL) {
			continue;
		}
		int active = candidate == g_lsp_registry.primary_active ||
				candidate == g_lsp_registry.eslint_active;
		if (!active && (candidate->server_kind == EDITOR_LSP_SERVER_NONE ||
					!editorLspProcessAlive(candidate))) {
			return candidate;
		}
	}
	for (int i = 0; i < g_lsp_registry.client_count; i++) {
		struct editorLspClient *candidate = g_lsp_registry.clients[i];
		if (candidate == NULL) {
			continue;
		}
		if (candidate != g_lsp_registry.primary_active &&
				candidate != g_lsp_registry.eslint_active) {
			return candidate;
		}
	}
	return g_lsp_registry.client_count > 0 ? g_lsp_registry.clients[0] : NULL;
}

struct editorLspClient *editorLspRegistryAcquireClient(enum editorLspServerKind server_kind,
		const char *workspace_root_path) {
	struct editorLspClient *existing =
			editorLspRegistryFindClient(server_kind, workspace_root_path);
	if (existing != NULL) {
		return existing;
	}
	editorLspRegistryInitIfNeeded();
	if (g_lsp_registry.client_count < ROTIDE_LSP_REGISTRY_MAX_CLIENTS) {
		struct editorLspClient *client = calloc(1, sizeof(*client));
		if (client == NULL) {
			return NULL;
		}
		editorLspRegistryResetClientStruct(client);
		g_lsp_registry.clients[g_lsp_registry.client_count++] = client;
		return client;
	}

	struct editorLspClient *evict = editorLspRegistrySelectEvictionCandidate();
	if (evict == NULL) {
		return NULL;
	}
	editorLspClientCleanup(evict, 0);
	return evict;
}

void editorLspRegistrySetActiveClient(enum editorLspServerKind server_kind,
		struct editorLspClient *client) {
	editorLspRegistryInitIfNeeded();
	if (editorLspRegistryServerKindIsEslint(server_kind)) {
		g_lsp_registry.eslint_active = client != NULL ? client : &g_lsp_registry.fallback_eslint;
		return;
	}
	g_lsp_registry.primary_active = client != NULL ? client : &g_lsp_registry.fallback_primary;
}

void editorLspRegistryForEachClient(void (*callback)(struct editorLspClient *client, void *ctx),
		void *ctx) {
	editorLspRegistryInitIfNeeded();
	if (callback == NULL) {
		return;
	}
	for (int i = 0; i < g_lsp_registry.client_count; i++) {
		struct editorLspClient *client = g_lsp_registry.clients[i];
		if (client == NULL) {
			continue;
		}
		callback(client, ctx);
	}
}

void editorLspRegistryShutdownAll(int graceful_shutdown) {
	editorLspRegistryInitIfNeeded();
	for (int i = 0; i < g_lsp_registry.client_count; i++) {
		struct editorLspClient *client = g_lsp_registry.clients[i];
		if (client == NULL) {
			continue;
		}
		editorLspClientCleanup(client, graceful_shutdown);
	}
	editorLspClientCleanup(&g_lsp_registry.fallback_primary, 0);
	editorLspClientCleanup(&g_lsp_registry.fallback_eslint, 0);
	g_lsp_registry.primary_active = &g_lsp_registry.fallback_primary;
	g_lsp_registry.eslint_active = &g_lsp_registry.fallback_eslint;
}

void editorLspRegistryReset(void) {
	editorLspRegistryInitIfNeeded();
	editorLspRegistryShutdownAll(0);
	for (int i = 0; i < g_lsp_registry.client_count; i++) {
		free(g_lsp_registry.clients[i]);
		g_lsp_registry.clients[i] = NULL;
	}
	g_lsp_registry.client_count = 0;
}
