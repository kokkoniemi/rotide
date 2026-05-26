#include "language/lsp.h"

#include "editing/edit.h"
#include "editing/text_source.h"
#include "language/lsp_framing.h"
#include "language/lsp_json.h"
#include "language/lsp_mock.h"
#include "language/lsp_protocol.h"
#include "language/lsp_registry.h"
#include "language/lsp_responses.h"
#include "language/lsp_transport.h"
#include "language/syntax.h"
#include "rotide.h"
#include "support/file_io.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static enum editorLspStartupFailureReason g_lsp_last_startup_failure_reason =
        EDITOR_LSP_STARTUP_FAILURE_NONE;

static void lspResetTrackedDocumentsForServerKind(enum editorLspServerKind server_kind);
static int lspFilenameHasHtmlExtension(const char *filename);
static int lspFilenameHasCssExtension(const char *filename);
static int lspFilenameHasJsonExtension(const char *filename);
static int lspFilenameHasJavascriptExtension(const char *filename);
static const char *lspCommandForServerKind(enum editorLspServerKind server_kind);
static const char *lspServerNameForServerKind(enum editorLspServerKind server_kind);
static const char *lspCommandSettingNameForServerKind(enum editorLspServerKind server_kind);
static const char *lspLanguageLabelForServerKind(enum editorLspServerKind server_kind);
static int lspServerKindSupportsDefinition(enum editorLspServerKind server_kind);
static char *lspBuildWorkspaceRootPathForFile(const char *filename,
                                              enum editorLspServerKind server_kind);

static void lspResetTrackedDocumentsForServerKind(enum editorLspServerKind server_kind) {
	int is_eslint = server_kind == EDITOR_LSP_SERVER_ESLINT;
	if (is_eslint) {
		E.lsp_eslint_doc_open = 0;
		E.lsp_eslint_doc_version = 0;
	} else {
		E.lsp_doc_open = 0;
		E.lsp_doc_version = 0;
	}
	if (E.tabs == NULL) {
		return;
	}
	for (int i = 0; i < E.tab_count; i++) {
		struct editorBuffer *tab = editorTabBufferHandleAtMutable(i);
		if (tab == NULL) {
			continue;
		}
		if (is_eslint) {
			tab->lsp_eslint_doc_open = 0;
			tab->lsp_eslint_doc_version = 0;
		} else {
			tab->lsp_doc_open = 0;
			tab->lsp_doc_version = 0;
		}
	}
}

void editorLspResetTrackedDocuments(void) {
	lspResetTrackedDocumentsForServerKind(EDITOR_LSP_SERVER_GOPLS);
	lspResetTrackedDocumentsForServerKind(EDITOR_LSP_SERVER_ESLINT);
}

static int lspFilenameHasCppExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".cc") == 0 || strcmp(dot, ".cpp") == 0 || strcmp(dot, ".cxx") == 0 ||
	       strcmp(dot, ".c++") == 0 || strcmp(dot, ".hh") == 0 || strcmp(dot, ".hpp") == 0 ||
	       strcmp(dot, ".hxx") == 0;
}

static int lspFilenameHasHtmlExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0 || strcmp(dot, ".xhtml") == 0;
}

static int lspFilenameHasCssExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".css") == 0 || strcmp(dot, ".scss") == 0;
}

static int lspFilenameHasJsonExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".json") == 0;
}

static int lspFilenameHasJavascriptExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".js") == 0 || strcmp(dot, ".mjs") == 0 || strcmp(dot, ".cjs") == 0 ||
	       strcmp(dot, ".jsx") == 0;
}

enum editorLspServerKind editorLspServerKindForFile(const char *filename,
                                                    enum editorSyntaxLanguage language) {
	if (lspFilenameHasHtmlExtension(filename)) {
		return EDITOR_LSP_SERVER_HTML;
	}
	if (lspFilenameHasCssExtension(filename)) {
		return EDITOR_LSP_SERVER_CSS;
	}
	if (lspFilenameHasJsonExtension(filename)) {
		return EDITOR_LSP_SERVER_JSON;
	}
	if (lspFilenameHasJavascriptExtension(filename)) {
		return EDITOR_LSP_SERVER_JAVASCRIPT;
	}
	switch (language) {
		case EDITOR_SYNTAX_GO:
			return EDITOR_LSP_SERVER_GOPLS;
		case EDITOR_SYNTAX_C:
		case EDITOR_SYNTAX_CPP:
			return EDITOR_LSP_SERVER_CLANGD;
		case EDITOR_SYNTAX_HTML:
			return EDITOR_LSP_SERVER_HTML;
		case EDITOR_SYNTAX_CSS:
			return EDITOR_LSP_SERVER_CSS;
		case EDITOR_SYNTAX_JAVASCRIPT:
			return EDITOR_LSP_SERVER_JAVASCRIPT;
		default:
			return EDITOR_LSP_SERVER_NONE;
	}
}

static const char *lspCommandForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return E.lsp_gopls_command;
		case EDITOR_LSP_SERVER_CLANGD:
			return E.lsp_clangd_command;
		case EDITOR_LSP_SERVER_HTML:
			return E.lsp_html_command;
		case EDITOR_LSP_SERVER_CSS:
			return E.lsp_css_command;
		case EDITOR_LSP_SERVER_JSON:
			return E.lsp_json_command;
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return E.lsp_javascript_command;
		case EDITOR_LSP_SERVER_ESLINT:
			return E.lsp_eslint_command;
		default:
			return NULL;
	}
}

static const char *lspServerNameForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "gopls";
		case EDITOR_LSP_SERVER_CLANGD:
			return "clangd";
		case EDITOR_LSP_SERVER_HTML:
			return "vscode-html-language-server";
		case EDITOR_LSP_SERVER_CSS:
			return "vscode-css-language-server";
		case EDITOR_LSP_SERVER_JSON:
			return "vscode-json-language-server";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "typescript-language-server";
		case EDITOR_LSP_SERVER_ESLINT:
			return "vscode-eslint-language-server";
		default:
			return "LSP";
	}
}

static const char *lspCommandSettingNameForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "gopls_command";
		case EDITOR_LSP_SERVER_CLANGD:
			return "clangd_command";
		case EDITOR_LSP_SERVER_HTML:
			return "html_command";
		case EDITOR_LSP_SERVER_CSS:
			return "css_command";
		case EDITOR_LSP_SERVER_JSON:
			return "json_command";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "javascript_command";
		case EDITOR_LSP_SERVER_ESLINT:
			return "eslint_command";
		default:
			return NULL;
	}
}

static const char *lspLanguageLabelForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "Go";
		case EDITOR_LSP_SERVER_CLANGD:
			return "C/C++";
		case EDITOR_LSP_SERVER_HTML:
			return "HTML";
		case EDITOR_LSP_SERVER_CSS:
			return "CSS/SCSS";
		case EDITOR_LSP_SERVER_JSON:
			return "JSON";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "JavaScript";
		case EDITOR_LSP_SERVER_ESLINT:
			return "JavaScript";
		default:
			return NULL;
	}
}

static int lspServerKindEnabled(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return E.lsp_gopls_enabled;
		case EDITOR_LSP_SERVER_CLANGD:
			return E.lsp_clangd_enabled;
		case EDITOR_LSP_SERVER_HTML:
			return E.lsp_html_enabled;
		case EDITOR_LSP_SERVER_CSS:
			return E.lsp_css_enabled;
		case EDITOR_LSP_SERVER_JSON:
			return E.lsp_json_enabled;
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return E.lsp_javascript_enabled;
		case EDITOR_LSP_SERVER_ESLINT:
			return E.lsp_eslint_enabled;
		default:
			return 0;
	}
}

int editorLspFileEnabled(const char *filename, enum editorSyntaxLanguage language) {
	return lspServerKindEnabled(editorLspServerKindForFile(filename, language));
}

int editorLspFileUsesEslint(const char *filename, enum editorSyntaxLanguage language) {
	return lspFilenameHasJavascriptExtension(filename) || language == EDITOR_SYNTAX_JAVASCRIPT;
}

int editorLspEslintEnabledForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspFileUsesEslint(filename, language) && E.lsp_eslint_enabled;
}

static int lspServerKindSupportsDefinition(enum editorLspServerKind server_kind) {
	return server_kind == EDITOR_LSP_SERVER_GOPLS || server_kind == EDITOR_LSP_SERVER_CLANGD ||
	       server_kind == EDITOR_LSP_SERVER_HTML || server_kind == EDITOR_LSP_SERVER_CSS ||
	       server_kind == EDITOR_LSP_SERVER_JSON || server_kind == EDITOR_LSP_SERVER_JAVASCRIPT;
}

int editorLspFileSupportsDefinition(const char *filename, enum editorSyntaxLanguage language) {
	return lspServerKindSupportsDefinition(editorLspServerKindForFile(filename, language));
}

const char *editorLspLanguageIdForFile(const char *filename, enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_GO:
			return "go";
		case EDITOR_SYNTAX_C:
			return lspFilenameHasCppExtension(filename) ? "cpp" : "c";
		case EDITOR_SYNTAX_CPP:
			return "cpp";
		case EDITOR_SYNTAX_HTML:
			return "html";
		case EDITOR_SYNTAX_CSS:
			return lspFilenameHasCssExtension(filename) &&
			                       strrchr(filename, '.') != NULL &&
			                       strcmp(strrchr(filename, '.'), ".scss") == 0
			               ? "scss"
			               : "css";
		case EDITOR_SYNTAX_JAVASCRIPT:
			return lspFilenameHasJavascriptExtension(filename) &&
			                       strrchr(filename, '.') != NULL &&
			                       strcmp(strrchr(filename, '.'), ".jsx") == 0
			               ? "javascriptreact"
			               : "javascript";
		default:
			break;
	}
	if (lspFilenameHasHtmlExtension(filename)) {
		return "html";
	}
	if (lspFilenameHasCssExtension(filename)) {
		return strrchr(filename, '.') != NULL &&
		                       strcmp(strrchr(filename, '.'), ".scss") == 0
		               ? "scss"
		               : "css";
	}
	if (lspFilenameHasJsonExtension(filename)) {
		return "json";
	}
	if (lspFilenameHasJavascriptExtension(filename)) {
		return strrchr(filename, '.') != NULL && strcmp(strrchr(filename, '.'), ".jsx") == 0
		               ? "javascriptreact"
		               : "javascript";
	}
	return NULL;
}

const char *editorLspLanguageLabelForFile(const char *filename,
                                          enum editorSyntaxLanguage language) {
	return lspLanguageLabelForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspServerNameForFile(const char *filename, enum editorSyntaxLanguage language) {
	return lspServerNameForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspCommandForFile(const char *filename, enum editorSyntaxLanguage language) {
	return lspCommandForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspCommandSettingNameForFile(const char *filename,
                                               enum editorSyntaxLanguage language) {
	return lspCommandSettingNameForServerKind(editorLspServerKindForFile(filename, language));
}

int editorLspUsesSharedVscodeInstallPrompt(const char *filename,
                                           enum editorSyntaxLanguage language) {
	enum editorLspServerKind server_kind = editorLspServerKindForFile(filename, language);
	return server_kind == EDITOR_LSP_SERVER_HTML || server_kind == EDITOR_LSP_SERVER_CSS ||
	       server_kind == EDITOR_LSP_SERVER_JSON || server_kind == EDITOR_LSP_SERVER_ESLINT;
}

static void lspSetStartupFailureStatus(struct editorLspClient *client, int timed_out) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
	const char *command = lspCommandForServerKind(client != NULL ? client->server_kind
	                                                             : EDITOR_LSP_SERVER_NONE);
	if (command == NULL || command[0] == '\0') {
		command = "language server";
	}
	if (timed_out) {
		editorSetStatusMsg("LSP startup failed: initialize timed out");
		return;
	}

	int saved_errno = errno;
	if (saved_errno == EPIPE) {
		int exit_code = 0;
		if (editorLspTryGetProcessExitCodeWithWait(client, 100, &exit_code) &&
		    exit_code == 127) {
			g_lsp_last_startup_failure_reason =
			        EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND;
			editorSetStatusMsg("LSP startup failed: command not found (%s)", command);
			return;
		}
		editorSetStatusMsg("LSP startup failed: server exited during initialize");
		return;
	}
	if (saved_errno == EPROTO) {
		editorSetStatusMsg("LSP startup failed: invalid initialize response");
		return;
	}
	if (saved_errno == ENOMEM) {
		editorSetStatusMsg("LSP startup failed: out of memory");
		return;
	}

	editorSetStatusMsg("LSP startup failed: initialize I/O error");
}

static char *lspBuildWorkspaceRootPathForFile(const char *filename,
                                              enum editorLspServerKind server_kind) {
	static const char *const gopls_markers[] = {"go.work", "go.mod", ".rotide.toml", ".git"};
	static const char *const clangd_markers[] = {"compile_commands.json", "compile_flags.txt",
	                                             ".clangd", ".rotide.toml", ".git"};
	static const char *const html_markers[] = {"package.json", ".rotide.toml", ".git"};
	static const char *const javascript_markers[] = {"tsconfig.json", "jsconfig.json",
	                                                 "package.json", ".rotide.toml", ".git"};

	char *file_path = editorPathAbsoluteDup(filename);
	char *file_dir = NULL;
	char *workspace_root = NULL;

	if (file_path != NULL) {
		file_dir = editorPathDirnameDup(file_path);
		if (file_dir == NULL) {
			free(file_path);
			return NULL;
		}

		const char *const *markers = NULL;
		size_t marker_count = 0;
		if (server_kind == EDITOR_LSP_SERVER_GOPLS) {
			markers = gopls_markers;
			marker_count = sizeof(gopls_markers) / sizeof(gopls_markers[0]);
		} else if (server_kind == EDITOR_LSP_SERVER_CLANGD) {
			markers = clangd_markers;
			marker_count = sizeof(clangd_markers) / sizeof(clangd_markers[0]);
		} else if (server_kind == EDITOR_LSP_SERVER_HTML ||
		           server_kind == EDITOR_LSP_SERVER_CSS ||
		           server_kind == EDITOR_LSP_SERVER_JSON ||
		           server_kind == EDITOR_LSP_SERVER_ESLINT) {
			markers = html_markers;
			marker_count = sizeof(html_markers) / sizeof(html_markers[0]);
		} else if (server_kind == EDITOR_LSP_SERVER_JAVASCRIPT) {
			markers = javascript_markers;
			marker_count = sizeof(javascript_markers) / sizeof(javascript_markers[0]);
		}

		if (markers != NULL) {
			workspace_root =
			        editorPathFindMarkerUpward(file_dir, markers, marker_count);
		}
		if (workspace_root == NULL) {
			workspace_root = strdup(file_dir);
		}
		free(file_dir);
		free(file_path);
		return workspace_root;
	}

	if (E.drawer_root_path != NULL && E.drawer_root_path[0] != '\0') {
		return editorPathAbsoluteDup(E.drawer_root_path);
	}

	return editorPathCwdDup();
}

static int lspEnsureRunningReal(const char *filename, enum editorLspServerKind server_kind) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (!lspServerKindEnabled(server_kind)) {
		return 0;
	}
	const char *command = lspCommandForServerKind(server_kind);
	if (command == NULL || command[0] == '\0') {
		const char *setting_name = lspCommandSettingNameForServerKind(server_kind);
		if (setting_name != NULL) {
			editorSetStatusMsg("LSP disabled: [lsp].%s is empty", setting_name);
		}
		return 0;
	}

	char *workspace_root_path = lspBuildWorkspaceRootPathForFile(filename, server_kind);
	if (workspace_root_path == NULL) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: workspace root unavailable");
		return 0;
	}

	struct editorLspClient *client =
	        editorLspRegistryAcquireClient(server_kind, workspace_root_path);
	if (client == NULL) {
		free(workspace_root_path);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: out of memory");
		return 0;
	}
	editorLspRegistrySetActiveClient(server_kind, client);

	if (client->disabled_for_position_encoding_server_kind == server_kind) {
		free(workspace_root_path);
		return 0;
	}

	if (client->initialized && client->server_kind == server_kind &&
	    editorLspProcessAlive(client) &&
	    editorLspWorkspaceRootsMatch(client->workspace_root_path, workspace_root_path)) {
		free(workspace_root_path);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
		return 1;
	}

	lspResetTrackedDocumentsForServerKind(server_kind);
	editorLspClientCleanup(client, 0);

	pid_t pid = 0;
	int to_server_fd = -1;
	int from_server_fd = -1;
	if (!editorLspSpawnProcess(command, &pid, &to_server_fd, &from_server_fd)) {
		free(workspace_root_path);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: unable to launch %s", command);
		return 0;
	}

	editorLspClientResetState(client);
	client->pid = pid;
	client->to_server_fd = to_server_fd;
	client->from_server_fd = from_server_fd;
	client->server_kind = server_kind;
	client->next_request_id = 1;
	client->workspace_root_path = workspace_root_path;

	char *root_uri = NULL;
	if (client->workspace_root_path != NULL) {
		(void)editorLspBuildFileUri(client->workspace_root_path, &root_uri);
	}
	if (root_uri == NULL) {
		editorSetStatusMsg("LSP startup failed: workspace root unavailable");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	int request_id = client->next_request_id++;
	char *init = editorLspBuildInitializeRequestJson(request_id, root_uri, (int)getpid());
	free(root_uri);
	if (init == NULL) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: out of memory");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, init)) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialize write failed");
		free(init);
		editorLspClientCleanup(client, 0);
		return 0;
	}
	free(init);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
	                                &timed_out)) {
		lspSetStartupFailureStatus(client, timed_out);
		editorLspClientCleanup(client, 0);
		return 0;
	}
	if (editorLspResponseHasError(response)) {
		free(response);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialize returned error");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	char *position_encoding = NULL;
	int have_encoding =
	        editorLspFindStringField(response, "positionEncoding", &position_encoding);
	int completion_supported = 0;
	char *trigger_chars = NULL;
	(void)editorLspParseCompletionProviderInResponse(response, &completion_supported,
	                                                 &trigger_chars);
	free(response);
	client->completion_supported = completion_supported;
	free(client->completion_trigger_chars);
	client->completion_trigger_chars = trigger_chars;
	if (!have_encoding || position_encoding == NULL) {
		/* LSP default is UTF-16 when the field is omitted. */
		client->position_encoding_utf16 = 1;
	} else if (strcmp(position_encoding, "utf-8") == 0) {
		client->position_encoding_utf16 = 0;
	} else if (strcmp(position_encoding, "utf-16") == 0) {
		client->position_encoding_utf16 = 1;
	} else {
		free(position_encoding);
		client->disabled_for_position_encoding_server_kind = server_kind;
		editorLspClientCleanup(client, 0);
		editorSetStatusMsg("LSP disabled: unsupported position encoding");
		return 0;
	}
	free(position_encoding);

	if (!editorLspSendRawJsonToFd(
	            client->to_server_fd,
	            "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}")) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialized notification failed");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	client->initialized = 1;
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	return 1;
}

int editorLspEnsureRunningForFile(const char *filename, enum editorSyntaxLanguage language) {
	enum editorLspServerKind server_kind = editorLspServerKindForFile(filename, language);
	if (server_kind == EDITOR_LSP_SERVER_NONE) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		char *workspace_root_path = lspBuildWorkspaceRootPathForFile(filename, server_kind);
		if (workspace_root_path == NULL) {
			return 0;
		}
		if (!lspServerKindEnabled(server_kind)) {
			free(workspace_root_path);
			return 0;
		}
		if (!g_lsp_mock.primary_server_alive ||
		    g_lsp_mock.primary_server_kind != server_kind ||
		    !editorLspWorkspaceRootsMatch(g_lsp_mock.primary_workspace_root_path,
		                                  workspace_root_path)) {
			if (g_lsp_mock.primary_server_kind != EDITOR_LSP_SERVER_NONE &&
			    (!g_lsp_mock.primary_server_alive ||
			     g_lsp_mock.primary_server_kind != server_kind ||
			     !editorLspWorkspaceRootsMatch(g_lsp_mock.primary_workspace_root_path,
			                                   workspace_root_path))) {
				lspResetTrackedDocumentsForServerKind(server_kind);
			}
			g_lsp_mock.primary_server_alive = 1;
			g_lsp_mock.primary_server_kind = server_kind;
			free(g_lsp_mock.primary_workspace_root_path);
			g_lsp_mock.primary_workspace_root_path = workspace_root_path;
			g_lsp_mock.stats.start_count++;
		} else {
			free(workspace_root_path);
		}
		return 1;
	}
	return lspEnsureRunningReal(filename, server_kind);
}

int editorLspEnsureRunningEslintForFile(const char *filename, enum editorSyntaxLanguage language) {
	if (!editorLspEslintEnabledForFile(filename, language)) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		char *workspace_root_path =
		        lspBuildWorkspaceRootPathForFile(filename, EDITOR_LSP_SERVER_ESLINT);
		if (workspace_root_path == NULL) {
			return 0;
		}
		if (!g_lsp_mock.eslint_server_alive ||
		    !editorLspWorkspaceRootsMatch(g_lsp_mock.eslint_workspace_root_path,
		                                  workspace_root_path)) {
			if (g_lsp_mock.eslint_server_alive &&
			    !editorLspWorkspaceRootsMatch(g_lsp_mock.eslint_workspace_root_path,
			                                  workspace_root_path)) {
				lspResetTrackedDocumentsForServerKind(EDITOR_LSP_SERVER_ESLINT);
			}
			g_lsp_mock.eslint_server_alive = 1;
			free(g_lsp_mock.eslint_workspace_root_path);
			g_lsp_mock.eslint_workspace_root_path = workspace_root_path;
			g_lsp_mock.stats.start_count++;
		} else {
			free(workspace_root_path);
		}
		return 1;
	}
	return lspEnsureRunningReal(filename, EDITOR_LSP_SERVER_ESLINT);
}

struct editorLspClient *editorLspEnsureClientForFile(const char *filename,
                                                     enum editorSyntaxLanguage language) {
	if (!editorLspEnsureRunningForFile(filename, language)) {
		return NULL;
	}
	return editorLspPrimaryClient();
}

struct editorLspClient *editorLspEnsureEslintClientForFile(const char *filename,
                                                           enum editorSyntaxLanguage language) {
	if (!editorLspEnsureRunningEslintForFile(filename, language)) {
		return NULL;
	}
	return editorLspEslintClient();
}

void editorLspShutdown(void) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (g_lsp_mock.enabled) {
		if (g_lsp_mock.primary_server_alive) {
			g_lsp_mock.stats.shutdown_count++;
			g_lsp_mock.primary_server_alive = 0;
			g_lsp_mock.primary_server_kind = EDITOR_LSP_SERVER_NONE;
		}
		if (g_lsp_mock.eslint_server_alive) {
			g_lsp_mock.stats.shutdown_count++;
			g_lsp_mock.eslint_server_alive = 0;
		}
		free(g_lsp_mock.primary_workspace_root_path);
		g_lsp_mock.primary_workspace_root_path = NULL;
		free(g_lsp_mock.eslint_workspace_root_path);
		g_lsp_mock.eslint_workspace_root_path = NULL;
		return;
	}
	editorLspRegistryShutdownAll(1);
}

static void lspPumpClientCallback(struct editorLspClient *client, void *ctx) {
	(void)ctx;
	(void)editorLspTryDrainIncoming(client, 0);
}

void editorLspPumpNotifications(void) {
	if (g_lsp_mock.enabled) {
		if (g_lsp_mock.diagnostic_path != NULL) {
			editorLspSetDiagnosticsForPath(g_lsp_mock.diagnostic_path,
			                               g_lsp_mock.diagnostics,
			                               g_lsp_mock.diagnostic_count);
		}
		return;
	}
	editorLspRegistryForEachClient(lspPumpClientCallback, NULL);
}

enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void) {
	return g_lsp_last_startup_failure_reason;
}

void editorLspClearStartupFailureReason(void) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
}

void editorLspEnsureActiveDocumentTracked(void) {
	if (E.tab_kind != EDITOR_TAB_FILE || E.filename == NULL || E.filename[0] == '\0' ||
	    E.document == NULL) {
		return;
	}
	if (E.lsp_doc_open && E.lsp_eslint_doc_open) {
		return;
	}
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return;
	}
	size_t text_len = 0;
	char *text = editorTextSourceDupRange(&source, 0, source.length, &text_len);
	if (text == NULL) {
		return;
	}
	if (!E.lsp_doc_open) {
		(void)editorLspEnsureDocumentOpen(E.filename, E.syntax_language, &E.lsp_doc_open,
		                                  &E.lsp_doc_version, text, text_len);
	}
	if (!E.lsp_eslint_doc_open) {
		(void)editorLspEnsureEslintDocumentOpen(E.filename, E.syntax_language,
		                                        &E.lsp_eslint_doc_open,
		                                        &E.lsp_eslint_doc_version, text, text_len);
	}
	free(text);
}
