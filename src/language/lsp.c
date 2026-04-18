#include "language/lsp_internal.h"

static struct editorLspClient g_lsp_client = {
	.pid = 0,
	.to_server_fd = -1,
	.from_server_fd = -1,
	.initialized = 0,
	.server_kind = EDITOR_LSP_SERVER_NONE,
	.disabled_for_position_encoding_server_kind = EDITOR_LSP_SERVER_NONE,
	.next_request_id = 1,
	.position_encoding_utf16 = 0,
	.workspace_root_path = NULL,
};
static struct editorLspClient g_lsp_eslint_client = {
	.pid = 0,
	.to_server_fd = -1,
	.from_server_fd = -1,
	.initialized = 0,
	.server_kind = EDITOR_LSP_SERVER_NONE,
	.disabled_for_position_encoding_server_kind = EDITOR_LSP_SERVER_NONE,
	.next_request_id = 1,
	.position_encoding_utf16 = 0,
	.workspace_root_path = NULL,
};
static struct editorLspMockState g_lsp_mock = {0};
static enum editorLspStartupFailureReason g_lsp_last_startup_failure_reason =
		EDITOR_LSP_STARTUP_FAILURE_NONE;

static int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column);
static int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units);
static int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out);
static int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column);
static int editorLspClientProtocolCharacterFromBufferColumn(struct editorLspClient *client, int line,
		int byte_column);
static int editorLspClientProtocolCharacterToBufferColumn(struct editorLspClient *client, int line,
		int protocol_character);
static int editorLspStringAppendf(struct editorLspString *sb, const char *fmt, ...);
static void editorLspResetTrackedDocumentsForServerKind(enum editorLspServerKind server_kind);
static int editorLspFilenameHasHtmlExtension(const char *filename);
static int editorLspFilenameHasCssExtension(const char *filename);
static int editorLspFilenameHasJsonExtension(const char *filename);
static int editorLspFilenameHasJavascriptExtension(const char *filename);
static enum editorLspServerKind editorLspServerKindForFile(const char *filename,
		enum editorSyntaxLanguage language);
static const char *editorLspCommandForServerKind(enum editorLspServerKind server_kind);
static const char *editorLspServerNameForServerKind(enum editorLspServerKind server_kind);
static const char *editorLspCommandSettingNameForServerKind(
		enum editorLspServerKind server_kind);
static const char *editorLspLanguageLabelForServerKind(enum editorLspServerKind server_kind);
static int editorLspServerKindSupportsDefinition(enum editorLspServerKind server_kind);
static const char *editorLspLanguageIdForFile(const char *filename,
		enum editorSyntaxLanguage language);
static char *editorLspBuildWorkspaceRootPathForFile(const char *filename,
		enum editorLspServerKind server_kind);
static int editorLspWorkspaceRootsMatch(const char *left, const char *right);
static int editorLspProcessIncomingMessage(struct editorLspClient *client, const char *message);
static int editorLspTryDrainIncoming(struct editorLspClient *client, int timeout_ms);
static void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count);
static int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
		const struct editorLspDiagnostic *diagnostics, int count);
static void editorLspSetDiagnosticsForPath(const char *path,
		const struct editorLspDiagnostic *diagnostics, int count);
static int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count);
static void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count);
static int editorLspExtractResponseId(const char *json, int *id_out);


#include "language/lsp_transport.c"

#include "language/lsp_protocol.c"

static void editorLspResetTrackedDocumentsForServerKind(enum editorLspServerKind server_kind) {
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
		if (is_eslint) {
			E.tabs[i].lsp_eslint_doc_open = 0;
			E.tabs[i].lsp_eslint_doc_version = 0;
		} else {
			E.tabs[i].lsp_doc_open = 0;
			E.tabs[i].lsp_doc_version = 0;
		}
	}
}

static int editorLspFilenameHasCppExtension(const char *filename) {
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

static int editorLspFilenameHasHtmlExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0 || strcmp(dot, ".xhtml") == 0;
}

static int editorLspFilenameHasCssExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".css") == 0 || strcmp(dot, ".scss") == 0;
}

static int editorLspFilenameHasJsonExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".json") == 0;
}

static int editorLspFilenameHasJavascriptExtension(const char *filename) {
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

static enum editorLspServerKind editorLspServerKindForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	if (editorLspFilenameHasHtmlExtension(filename)) {
		return EDITOR_LSP_SERVER_HTML;
	}
	if (editorLspFilenameHasCssExtension(filename)) {
		return EDITOR_LSP_SERVER_CSS;
	}
	if (editorLspFilenameHasJsonExtension(filename)) {
		return EDITOR_LSP_SERVER_JSON;
	}
	if (editorLspFilenameHasJavascriptExtension(filename)) {
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

static const char *editorLspCommandForServerKind(enum editorLspServerKind server_kind) {
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

static const char *editorLspServerNameForServerKind(enum editorLspServerKind server_kind) {
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

static const char *editorLspCommandSettingNameForServerKind(
		enum editorLspServerKind server_kind) {
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

static const char *editorLspLanguageLabelForServerKind(enum editorLspServerKind server_kind) {
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

static int editorLspServerKindEnabled(enum editorLspServerKind server_kind) {
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
	return editorLspServerKindEnabled(editorLspServerKindForFile(filename, language));
}

int editorLspFileUsesEslint(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspFilenameHasJavascriptExtension(filename) || language == EDITOR_SYNTAX_JAVASCRIPT;
}

int editorLspEslintEnabledForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspFileUsesEslint(filename, language) && E.lsp_eslint_enabled;
}

static int editorLspServerKindSupportsDefinition(enum editorLspServerKind server_kind) {
	return server_kind == EDITOR_LSP_SERVER_GOPLS || server_kind == EDITOR_LSP_SERVER_CLANGD ||
			server_kind == EDITOR_LSP_SERVER_HTML || server_kind == EDITOR_LSP_SERVER_CSS ||
			server_kind == EDITOR_LSP_SERVER_JSON ||
			server_kind == EDITOR_LSP_SERVER_JAVASCRIPT;
}

int editorLspFileSupportsDefinition(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspServerKindSupportsDefinition(editorLspServerKindForFile(filename, language));
}

static const char *editorLspLanguageIdForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_GO:
			return "go";
		case EDITOR_SYNTAX_C:
			return editorLspFilenameHasCppExtension(filename) ? "cpp" : "c";
		case EDITOR_SYNTAX_CPP:
			return "cpp";
		case EDITOR_SYNTAX_HTML:
			return "html";
		case EDITOR_SYNTAX_CSS:
			return editorLspFilenameHasCssExtension(filename) &&
						 strrchr(filename, '.') != NULL &&
						 strcmp(strrchr(filename, '.'), ".scss") == 0 ?
					"scss" :
					"css";
		case EDITOR_SYNTAX_JAVASCRIPT:
			return editorLspFilenameHasJavascriptExtension(filename) &&
						 strrchr(filename, '.') != NULL &&
						 strcmp(strrchr(filename, '.'), ".jsx") == 0 ?
					"javascriptreact" :
					"javascript";
		default:
			break;
	}
	if (editorLspFilenameHasHtmlExtension(filename)) {
		return "html";
	}
	if (editorLspFilenameHasCssExtension(filename)) {
		return strrchr(filename, '.') != NULL && strcmp(strrchr(filename, '.'), ".scss") == 0 ?
				"scss" :
				"css";
	}
	if (editorLspFilenameHasJsonExtension(filename)) {
		return "json";
	}
	if (editorLspFilenameHasJavascriptExtension(filename)) {
		return strrchr(filename, '.') != NULL && strcmp(strrchr(filename, '.'), ".jsx") == 0 ?
				"javascriptreact" :
				"javascript";
	}
	return NULL;
}

const char *editorLspLanguageLabelForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	return editorLspLanguageLabelForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspServerNameForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspServerNameForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspCommandForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspCommandForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspCommandSettingNameForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	return editorLspCommandSettingNameForServerKind(
			editorLspServerKindForFile(filename, language));
}

int editorLspUsesSharedVscodeInstallPrompt(const char *filename,
		enum editorSyntaxLanguage language) {
	enum editorLspServerKind server_kind = editorLspServerKindForFile(filename, language);
	return server_kind == EDITOR_LSP_SERVER_HTML || server_kind == EDITOR_LSP_SERVER_CSS ||
			server_kind == EDITOR_LSP_SERVER_JSON || server_kind == EDITOR_LSP_SERVER_ESLINT;
}


static void editorLspSetStartupFailureStatus(struct editorLspClient *client, int timed_out) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
	const char *command = editorLspCommandForServerKind(client != NULL ? client->server_kind :
			EDITOR_LSP_SERVER_NONE);
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
		if (editorLspTryGetProcessExitCodeWithWait(client, 100, &exit_code) && exit_code == 127) {
			g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND;
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


static char *editorLspBuildWorkspaceRootPathForFile(const char *filename,
		enum editorLspServerKind server_kind) {
	static const char *const gopls_markers[] = {
		"go.work",
		"go.mod",
		".rotide.toml",
		".git"
	};
	static const char *const clangd_markers[] = {
		"compile_commands.json",
		"compile_flags.txt",
		".clangd",
		".rotide.toml",
		".git"
	};
	static const char *const html_markers[] = {
		"package.json",
		".rotide.toml",
		".git"
	};
	static const char *const javascript_markers[] = {
		"tsconfig.json",
		"jsconfig.json",
		"package.json",
		".rotide.toml",
		".git"
	};

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
			workspace_root = editorPathFindMarkerUpward(file_dir, markers, marker_count);
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

	return editorPathGetCwd();
}


static int editorLspEnsureRunningReal(struct editorLspClient *client, const char *filename,
		enum editorLspServerKind server_kind) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (client == NULL) {
		return 0;
	}
	if (!editorLspServerKindEnabled(server_kind)) {
		return 0;
	}
	const char *command = editorLspCommandForServerKind(server_kind);
	if (command == NULL || command[0] == '\0') {
		const char *setting_name = editorLspCommandSettingNameForServerKind(server_kind);
		if (setting_name != NULL) {
			editorSetStatusMsg("LSP disabled: [lsp].%s is empty", setting_name);
		}
		return 0;
	}
	if (client->disabled_for_position_encoding_server_kind == server_kind) {
		return 0;
	}

	char *workspace_root_path = editorLspBuildWorkspaceRootPathForFile(filename, server_kind);
	if (workspace_root_path == NULL) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: workspace root unavailable");
		return 0;
	}

	if (client->initialized && client->server_kind == server_kind &&
			editorLspProcessAlive(client) &&
			editorLspWorkspaceRootsMatch(client->workspace_root_path, workspace_root_path)) {
		free(workspace_root_path);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
		return 1;
	}
	if (client->server_kind != EDITOR_LSP_SERVER_NONE &&
			(client->server_kind != server_kind || !editorLspProcessAlive(client) ||
			 !editorLspWorkspaceRootsMatch(client->workspace_root_path, workspace_root_path))) {
		editorLspResetTrackedDocumentsForServerKind(server_kind);
	}

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
		editorLspSetStartupFailureStatus(client, timed_out);
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
	int have_encoding = editorLspFindStringField(response, "positionEncoding", &position_encoding);
	free(response);
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

	if (!editorLspSendRawJsonToFd(client->to_server_fd,
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

static int editorLspEnsureRunningForFile(const char *filename, enum editorSyntaxLanguage language) {
	enum editorLspServerKind server_kind = editorLspServerKindForFile(filename, language);
	if (server_kind == EDITOR_LSP_SERVER_NONE) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		char *workspace_root_path = editorLspBuildWorkspaceRootPathForFile(filename, server_kind);
		if (workspace_root_path == NULL) {
			return 0;
		}
		if (!editorLspServerKindEnabled(server_kind)) {
			free(workspace_root_path);
			return 0;
		}
		if (!g_lsp_mock.primary_server_alive || g_lsp_mock.primary_server_kind != server_kind ||
				!editorLspWorkspaceRootsMatch(g_lsp_mock.primary_workspace_root_path,
						workspace_root_path)) {
			if (g_lsp_mock.primary_server_kind != EDITOR_LSP_SERVER_NONE &&
					(!g_lsp_mock.primary_server_alive ||
					 g_lsp_mock.primary_server_kind != server_kind ||
					 !editorLspWorkspaceRootsMatch(g_lsp_mock.primary_workspace_root_path,
							 workspace_root_path))) {
				editorLspResetTrackedDocumentsForServerKind(server_kind);
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
	return editorLspEnsureRunningReal(&g_lsp_client, filename, server_kind);
}

static int editorLspEnsureRunningEslintForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	if (!editorLspEslintEnabledForFile(filename, language)) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		char *workspace_root_path =
				editorLspBuildWorkspaceRootPathForFile(filename, EDITOR_LSP_SERVER_ESLINT);
		if (workspace_root_path == NULL) {
			return 0;
		}
		if (!g_lsp_mock.eslint_server_alive ||
				!editorLspWorkspaceRootsMatch(g_lsp_mock.eslint_workspace_root_path,
						workspace_root_path)) {
			if (g_lsp_mock.eslint_server_alive &&
					!editorLspWorkspaceRootsMatch(g_lsp_mock.eslint_workspace_root_path,
							workspace_root_path)) {
				editorLspResetTrackedDocumentsForServerKind(EDITOR_LSP_SERVER_ESLINT);
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
	return editorLspEnsureRunningReal(&g_lsp_eslint_client, filename, EDITOR_LSP_SERVER_ESLINT);
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
	editorLspClientCleanup(&g_lsp_client, 1);
	editorLspClientCleanup(&g_lsp_eslint_client, 1);
}

void editorLspPumpNotifications(void) {
	if (g_lsp_mock.enabled) {
		if (g_lsp_mock.diagnostic_path != NULL) {
			editorLspSetDiagnosticsForPath(g_lsp_mock.diagnostic_path, g_lsp_mock.diagnostics,
					g_lsp_mock.diagnostic_count);
		}
		return;
	}
	(void)editorLspTryDrainIncoming(&g_lsp_client, 0);
	(void)editorLspTryDrainIncoming(&g_lsp_eslint_client, 0);
}

static int editorLspIsTrackedLanguage(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' || !editorLspFileEnabled(filename, language) ||
			doc_open_in_out == NULL || doc_version_in_out == NULL ||
			editorLspServerKindForFile(filename, language) == EDITOR_LSP_SERVER_NONE) {
		return 0;
	}
	return 1;
}

static int editorLspIsTrackedEslintLanguage(const char *filename,
		enum editorSyntaxLanguage language, int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' ||
			!editorLspEslintEnabledForFile(filename, language) ||
			doc_open_in_out == NULL || doc_version_in_out == NULL) {
		return 0;
	}
	return 1;
}

int editorLspEnsureDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunningForFile(filename, language)) {
		return 0;
	}

	const char *language_id = editorLspLanguageIdForFile(filename, language);
	if (language_id == NULL) {
		return 0;
	}

	int version = *doc_version_in_out > 0 ? *doc_version_in_out : 1;
	if (g_lsp_mock.enabled) {
		(void)snprintf(g_lsp_mock.last_did_open_language_id,
				sizeof(g_lsp_mock.last_did_open_language_id), "%s", language_id);
		g_lsp_mock.last_did_open_language_id[
				sizeof(g_lsp_mock.last_did_open_language_id) - 1] = '\0';
		g_lsp_mock.stats.did_open_count++;
		*doc_open_in_out = 1;
		*doc_version_in_out = version;
		return 1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	if (full_text_len > 0 && full_text == NULL) {
		free(uri);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
				",\"languageId\":\"%s\",\"version\":%d,\"text\":", language_id, version);
	}
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload,
				full_text != NULL ? full_text : "", full_text_len);
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJson(payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_client, 0);
		return 0;
	}

	*doc_open_in_out = 1;
	*doc_version_in_out = version;
	return 1;
}

int editorLspNotifyDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}

	if (!editorLspEnsureDocumentOpen(filename, language, doc_open_in_out, doc_version_in_out,
				full_text, full_text_len)) {
		return 0;
	}

	int next_version = *doc_version_in_out + 1;
	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_change_count++;
		g_lsp_mock.last_change.had_range = edit != NULL;
		g_lsp_mock.last_change.start_line = edit != NULL ? (int)edit->start_point.row : 0;
		g_lsp_mock.last_change.start_character = edit != NULL ? (int)edit->start_point.column : 0;
		g_lsp_mock.last_change.end_line = edit != NULL ? (int)edit->old_end_point.row : 0;
		g_lsp_mock.last_change.end_character = edit != NULL ? (int)edit->old_end_point.column : 0;
		g_lsp_mock.last_change.version = next_version;
		const char *mock_text = inserted_text;
		size_t mock_text_len = inserted_text_len;
		if (edit == NULL) {
			mock_text = full_text;
			mock_text_len = full_text_len;
			if (mock_text == NULL) {
				struct editorTextSource source = {0};
				if (!editorBuildActiveTextSource(&source)) {
					return 0;
				}
				mock_text = editorTextSourceDupRange(&source, 0, source.length, &mock_text_len);
				if (mock_text == NULL && mock_text_len > 0) {
					return 0;
				}
			}
		}
		size_t copy_len = mock_text_len;
		if (copy_len >= sizeof(g_lsp_mock.last_change.text)) {
			copy_len = sizeof(g_lsp_mock.last_change.text) - 1;
		}
		if (copy_len > 0 && mock_text != NULL) {
			memcpy(g_lsp_mock.last_change.text, mock_text, copy_len);
		}
		g_lsp_mock.last_change.text[copy_len] = '\0';
		if (edit == NULL && mock_text != full_text) {
			free((char *)mock_text);
		}
		*doc_version_in_out = next_version;
		return 1;
	}

	int send_range = edit != NULL;
	if (send_range && g_lsp_client.position_encoding_utf16 &&
			(edit->start_point.row != edit->old_end_point.row ||
			 edit->start_point.column != edit->old_end_point.column)) {
		/*
		 * UTF-16 range conversion for deletes would require pre-edit text.
		 * Fall back to whole-document sync for those edits.
		 */
		send_range = 0;
	}

	char *owned_full_text = NULL;
	const char *change_text = NULL;
	size_t change_text_len = 0;
	if (send_range) {
		if (inserted_text_len > 0 && inserted_text == NULL) {
			return 0;
		}
		change_text = inserted_text != NULL ? inserted_text : "";
		change_text_len = inserted_text_len;
	} else {
		change_text = full_text;
		change_text_len = full_text_len;
		if (change_text == NULL) {
			struct editorTextSource source = {0};
			if (!editorBuildActiveTextSource(&source)) {
				return 0;
			}
			owned_full_text = editorTextSourceDupRange(&source, 0, source.length, &change_text_len);
			if (owned_full_text == NULL && change_text_len > 0) {
				return 0;
			}
			change_text = owned_full_text != NULL ? owned_full_text : "";
		}
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		free(owned_full_text);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload, ",\"version\":%d},\"contentChanges\":[{",
				next_version);
	}

	if (built && send_range) {
		unsigned int start_character = edit->start_point.column;
		unsigned int end_character = edit->old_end_point.column;
		if (g_lsp_client.position_encoding_utf16) {
			start_character = (unsigned int)editorLspProtocolCharacterFromBufferColumn(
					(int)edit->start_point.row, (int)edit->start_point.column);
			end_character = (unsigned int)editorLspProtocolCharacterFromBufferColumn(
					(int)edit->old_end_point.row, (int)edit->old_end_point.column);
		}

		built = editorLspStringAppendf(&payload,
				"\"range\":{\"start\":{\"line\":%u,\"character\":%u},"
				"\"end\":{\"line\":%u,\"character\":%u}},",
				edit->start_point.row, start_character,
				edit->old_end_point.row, end_character);
	}

	if (built) {
		built = editorLspStringAppend(&payload, "\"text\":");
	}
	if (built) {
		if (change_text_len > 0 && change_text == NULL) {
			built = 0;
		} else {
			built = editorLspStringAppendJsonEscaped(&payload,
					change_text != NULL ? change_text : "", change_text_len);
		}
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}]}}");
	}
	free(uri);
	free(owned_full_text);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJson(payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_client, 0);
		return 0;
	}

	*doc_version_in_out = next_version;
	return 1;
}

int editorLspNotifyDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (!*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunningForFile(filename, language)) {
		return 0;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_save_count++;
		return 1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJson(payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_client, 0);
		return 0;
	}
	return 1;
}

void editorLspNotifyDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out) ||
			!*doc_open_in_out) {
		return;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_close_count++;
		*doc_open_in_out = 0;
		*doc_version_in_out = 0;
		editorLspClearDiagnosticsForFile(filename);
		return;
	}

	if (editorLspEnsureRunningForFile(filename, language)) {
		char *uri = NULL;
		if (editorLspBuildFileUri(filename, &uri)) {
			struct editorLspString payload = {0};
			int built = editorLspStringAppend(&payload,
					"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
					"\"textDocument\":{\"uri\":");
			if (built) {
				built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
			}
			if (built) {
				built = editorLspStringAppend(&payload, "}}}");
			}
			if (built && !editorLspSendRawJson(payload.buf)) {
				editorLspClientCleanup(&g_lsp_client, 0);
			}
			free(payload.buf);
			free(uri);
		}
	}

	*doc_open_in_out = 0;
	*doc_version_in_out = 0;
	editorLspClearDiagnosticsForFile(filename);
}

int editorLspEnsureEslintDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunningEslintForFile(filename, language)) {
		return 0;
	}

	const char *language_id = editorLspLanguageIdForFile(filename, language);
	if (language_id == NULL) {
		return 0;
	}

	int version = *doc_version_in_out > 0 ? *doc_version_in_out : 1;
	if (g_lsp_mock.enabled) {
		(void)snprintf(g_lsp_mock.last_did_open_language_id,
				sizeof(g_lsp_mock.last_did_open_language_id), "%s", language_id);
		g_lsp_mock.last_did_open_language_id[
				sizeof(g_lsp_mock.last_did_open_language_id) - 1] = '\0';
		g_lsp_mock.stats.did_open_count++;
		*doc_open_in_out = 1;
		*doc_version_in_out = version;
		return 1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}
	if (full_text_len > 0 && full_text == NULL) {
		free(uri);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
				",\"languageId\":\"%s\",\"version\":%d,\"text\":", language_id, version);
	}
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload,
				full_text != NULL ? full_text : "", full_text_len);
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return 0;
	}

	*doc_open_in_out = 1;
	*doc_version_in_out = version;
	return 1;
}

int editorLspNotifyEslintDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}

	if (!editorLspEnsureEslintDocumentOpen(filename, language, doc_open_in_out, doc_version_in_out,
				full_text, full_text_len)) {
		return 0;
	}

	int next_version = *doc_version_in_out + 1;
	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_change_count++;
		g_lsp_mock.last_change.had_range = edit != NULL;
		g_lsp_mock.last_change.start_line = edit != NULL ? (int)edit->start_point.row : 0;
		g_lsp_mock.last_change.start_character = edit != NULL ? (int)edit->start_point.column : 0;
		g_lsp_mock.last_change.end_line = edit != NULL ? (int)edit->old_end_point.row : 0;
		g_lsp_mock.last_change.end_character = edit != NULL ? (int)edit->old_end_point.column : 0;
		g_lsp_mock.last_change.version = next_version;
		*doc_version_in_out = next_version;
		return 1;
	}

	int send_range = edit != NULL;
	if (send_range && g_lsp_eslint_client.position_encoding_utf16 &&
			(edit->start_point.row != edit->old_end_point.row ||
			 edit->start_point.column != edit->old_end_point.column)) {
		send_range = 0;
	}

	char *owned_full_text = NULL;
	const char *change_text = NULL;
	size_t change_text_len = 0;
	if (send_range) {
		if (inserted_text_len > 0 && inserted_text == NULL) {
			return 0;
		}
		change_text = inserted_text != NULL ? inserted_text : "";
		change_text_len = inserted_text_len;
	} else {
		change_text = full_text;
		change_text_len = full_text_len;
		if (change_text == NULL) {
			struct editorTextSource source = {0};
			if (!editorBuildActiveTextSource(&source)) {
				return 0;
			}
			owned_full_text = editorTextSourceDupRange(&source, 0, source.length, &change_text_len);
			if (owned_full_text == NULL && change_text_len > 0) {
				return 0;
			}
			change_text = owned_full_text != NULL ? owned_full_text : "";
		}
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		free(owned_full_text);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload, ",\"version\":%d},\"contentChanges\":[{",
				next_version);
	}
	if (built && send_range) {
		unsigned int start_character = edit->start_point.column;
		unsigned int end_character = edit->old_end_point.column;
		if (g_lsp_eslint_client.position_encoding_utf16) {
			start_character = (unsigned int)editorLspClientProtocolCharacterFromBufferColumn(
					&g_lsp_eslint_client, (int)edit->start_point.row, (int)edit->start_point.column);
			end_character = (unsigned int)editorLspClientProtocolCharacterFromBufferColumn(
					&g_lsp_eslint_client, (int)edit->old_end_point.row,
					(int)edit->old_end_point.column);
		}
		built = editorLspStringAppendf(&payload,
				"\"range\":{\"start\":{\"line\":%u,\"character\":%u},"
				"\"end\":{\"line\":%u,\"character\":%u}},",
				edit->start_point.row, start_character,
				edit->old_end_point.row, end_character);
	}
	if (built) {
		built = editorLspStringAppend(&payload, "\"text\":");
	}
	if (built) {
		if (change_text_len > 0 && change_text == NULL) {
			built = 0;
		} else {
			built = editorLspStringAppendJsonEscaped(&payload,
					change_text != NULL ? change_text : "", change_text_len);
		}
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}]}}");
	}
	free(uri);
	free(owned_full_text);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return 0;
	}

	*doc_version_in_out = next_version;
	return 1;
}

int editorLspNotifyEslintDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (!*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunningEslintForFile(filename, language)) {
		return 0;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_save_count++;
		return 1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return 0;
	}
	return 1;
}

void editorLspNotifyEslintDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out) ||
			!*doc_open_in_out) {
		return;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_close_count++;
		*doc_open_in_out = 0;
		*doc_version_in_out = 0;
		editorLspClearDiagnosticsForFile(filename);
		return;
	}

	if (editorLspEnsureRunningEslintForFile(filename, language)) {
		char *uri = NULL;
		if (editorLspBuildFileUri(filename, &uri)) {
			struct editorLspString payload = {0};
			int built = editorLspStringAppend(&payload,
					"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
					"\"textDocument\":{\"uri\":");
			if (built) {
				built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
			}
			if (built) {
				built = editorLspStringAppend(&payload, "}}}");
			}
			if (built &&
					!editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf)) {
				editorLspClientCleanup(&g_lsp_eslint_client, 0);
			}
			free(payload.buf);
			free(uri);
		}
	}

	*doc_open_in_out = 0;
	*doc_version_in_out = 0;
	editorLspClearDiagnosticsForFile(filename);
}


int editorLspRequestDefinition(const char *filename, enum editorSyntaxLanguage language, int line,
		int character, struct editorLspLocation **locations_out, int *count_out,
		int *timed_out_out) {
	if (locations_out == NULL || count_out == NULL) {
		return -1;
	}
	*locations_out = NULL;
	*count_out = 0;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	if (!editorLspFileEnabled(filename, language)) {
		return 0;
	}
	if (filename == NULL || filename[0] == '\0' || line < 0 || character < 0 ||
			!editorLspFileSupportsDefinition(filename, language)) {
		return -1;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningForFile(filename, language)) {
			return -1;
		}
		g_lsp_mock.stats.definition_count++;
		if (g_lsp_mock.definition_result_code == -2) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		if (g_lsp_mock.definition_result_code < 0) {
			return -1;
		}
		if (!editorLspCopyLocations(locations_out, count_out, g_lsp_mock.definition_locations,
					g_lsp_mock.definition_location_count)) {
			return -1;
		}
		return 1;
	}

	if (!editorLspEnsureRunningForFile(filename, language)) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int protocol_character = editorLspProtocolCharacterFromBufferColumn(line, character);

	int request_id = g_lsp_client.next_request_id++;
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(&payload,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/definition\",\"params\":{"
			"\"textDocument\":{\"uri\":",
			request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
				"},\"position\":{\"line\":%d,\"character\":%d}}}", line, protocol_character);
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJson(payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(&g_lsp_client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(&g_lsp_client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
				&timed_out)) {
		editorLspClientCleanup(&g_lsp_client, 0);
		if (timed_out) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		return -1;
	}

	if (editorLspResponseHasError(response)) {
		free(response);
		return -1;
	}

	struct editorLspLocation *locations = NULL;
	int count = 0;
	if (!editorLspParseDefinitionLocations(response, &locations, &count)) {
		free(response);
		return -1;
	}
	free(response);

	*locations_out = locations;
	*count_out = count;
	return 1;
}

int editorLspRequestCodeActionFixes(const char *filename, enum editorSyntaxLanguage language) {
	if (filename == NULL || filename[0] == '\0' ||
			!editorLspEslintEnabledForFile(filename, language)) {
		return 0;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningEslintForFile(filename, language)) {
			return -1;
		}
		g_lsp_mock.stats.code_action_count++;
		if (g_lsp_mock.code_action_result_code <= 0) {
			return g_lsp_mock.code_action_result_code;
		}
		return editorLspApplyPendingEdits(g_lsp_mock.code_action_edits,
				g_lsp_mock.code_action_edit_count) >= 0 ?
				g_lsp_mock.code_action_edit_count :
				-1;
	}

	size_t full_text_len = 0;
	char *full_text = NULL;
	if (!E.lsp_eslint_doc_open) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			return -1;
		}
	}
	int ready = editorLspEnsureEslintDocumentOpen(filename, language, &E.lsp_eslint_doc_open,
			&E.lsp_eslint_doc_version, full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
	if (!ready) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int request_id = g_lsp_eslint_client.next_request_id++;
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(&payload,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/codeAction\",\"params\":{"
			"\"textDocument\":{\"uri\":",
			request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(&payload,
				"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
				"\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[");
	}
	for (int i = 0; built && i < E.lsp_diagnostic_count; i++) {
		if (i > 0) {
			built = editorLspStringAppend(&payload, ",");
		}
		int start_character = editorLspClientProtocolCharacterFromBufferColumn(
				&g_lsp_eslint_client, E.lsp_diagnostics[i].start_line,
				E.lsp_diagnostics[i].start_character);
		int end_character = editorLspClientProtocolCharacterFromBufferColumn(
				&g_lsp_eslint_client, E.lsp_diagnostics[i].end_line,
				E.lsp_diagnostics[i].end_character);
		built = editorLspStringAppendf(&payload,
				"{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
				"\"end\":{\"line\":%d,\"character\":%d}},\"severity\":%d,\"message\":",
				E.lsp_diagnostics[i].start_line, start_character,
				E.lsp_diagnostics[i].end_line, end_character, E.lsp_diagnostics[i].severity);
		if (built) {
			built = editorLspStringAppendJsonEscaped(&payload,
					E.lsp_diagnostics[i].message != NULL ? E.lsp_diagnostics[i].message : "",
					E.lsp_diagnostics[i].message != NULL ? strlen(E.lsp_diagnostics[i].message) : 0);
		}
		if (built) {
			built = editorLspStringAppend(&payload, "}");
		}
	}
	if (built) {
		built = editorLspStringAppend(&payload,
				"],\"only\":[\"source.fixAll.eslint\"]}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(&g_lsp_eslint_client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS,
				&response, &timed_out)) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return timed_out ? -2 : -1;
	}
	if (editorLspResponseHasError(response)) {
		free(response);
		return -1;
	}

	const char *result_key = strstr(response, "\"result\"");
	const char *result_colon = result_key != NULL ? strchr(result_key, ':') : NULL;
	const char *result = result_colon != NULL ? editorLspSkipWs(result_colon + 1) : NULL;
	if (result == NULL || strncmp(result, "null", 4) == 0) {
		free(response);
		return 0;
	}

	const char *scan = result;
	struct editorLspPendingEdit *edits = NULL;
	int count = 0;
	while (scan != NULL) {
		const char *edit_key = strstr(scan, "\"edit\"");
		if (edit_key == NULL) {
			break;
		}
		const char *edit_colon = strchr(edit_key, ':');
		const char *edit_object = edit_colon != NULL ? strchr(edit_colon + 1, '{') : NULL;
		const char *edit_end = edit_object != NULL ? editorLspFindJsonObjectEnd(edit_object) : NULL;
		if (edit_end == NULL) {
			break;
		}
		if (editorLspParseWorkspaceEditChanges(edit_object, filename, &edits, &count) && count > 0) {
			break;
		}
		editorLspFreePendingEdits(edits, count);
		edits = NULL;
		count = 0;
		scan = edit_end;
	}
	free(response);
	if (count <= 0) {
		editorLspFreePendingEdits(edits, count);
		return 0;
	}

	int applied = editorLspApplyPendingEditsWithClient(&g_lsp_eslint_client, edits, count);
	editorLspFreePendingEdits(edits, count);
	return applied >= 0 ? applied : -1;
}

enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void) {
	return g_lsp_last_startup_failure_reason;
}

void editorLspTestSetMockEnabled(int enabled) {
	g_lsp_mock.enabled = enabled ? 1 : 0;
	if (!g_lsp_mock.enabled) {
		g_lsp_mock.primary_server_alive = 0;
		g_lsp_mock.primary_server_kind = EDITOR_LSP_SERVER_NONE;
		g_lsp_mock.eslint_server_alive = 0;
		free(g_lsp_mock.primary_workspace_root_path);
		g_lsp_mock.primary_workspace_root_path = NULL;
		free(g_lsp_mock.eslint_workspace_root_path);
		g_lsp_mock.eslint_workspace_root_path = NULL;
		g_lsp_mock.last_did_open_language_id[0] = '\0';
	}
}

void editorLspTestSetMockServerAlive(int alive) {
	g_lsp_mock.primary_server_alive = alive ? 1 : 0;
}

void editorLspTestResetMock(void) {
	editorLspFreeLocations(g_lsp_mock.definition_locations, g_lsp_mock.definition_location_count);
	editorLspFreeDiagnostics(g_lsp_mock.diagnostics, g_lsp_mock.diagnostic_count);
	editorLspFreePendingEdits(g_lsp_mock.code_action_edits, g_lsp_mock.code_action_edit_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = 1;
	g_lsp_mock.diagnostics = NULL;
	g_lsp_mock.diagnostic_count = 0;
	free(g_lsp_mock.diagnostic_path);
	g_lsp_mock.diagnostic_path = NULL;
	g_lsp_mock.code_action_result_code = 0;
	g_lsp_mock.code_action_edits = NULL;
	g_lsp_mock.code_action_edit_count = 0;
	g_lsp_mock.primary_server_alive = 0;
	g_lsp_mock.primary_server_kind = EDITOR_LSP_SERVER_NONE;
	g_lsp_mock.eslint_server_alive = 0;
	free(g_lsp_mock.primary_workspace_root_path);
	g_lsp_mock.primary_workspace_root_path = NULL;
	free(g_lsp_mock.eslint_workspace_root_path);
	g_lsp_mock.eslint_workspace_root_path = NULL;
	memset(&g_lsp_mock.stats, 0, sizeof(g_lsp_mock.stats));
	memset(&g_lsp_mock.last_change, 0, sizeof(g_lsp_mock.last_change));
	g_lsp_mock.last_did_open_language_id[0] = '\0';
	g_lsp_mock.enabled = 0;
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
}

void editorLspTestGetStats(struct editorLspTestStats *out) {
	if (out == NULL) {
		return;
	}
	*out = g_lsp_mock.stats;
}

void editorLspTestGetLastChange(struct editorLspTestLastChange *out) {
	if (out == NULL) {
		return;
	}
	*out = g_lsp_mock.last_change;
}

void editorLspTestGetLastDidOpenLanguageId(char *out, size_t out_size) {
	if (out == NULL || out_size == 0) {
		return;
	}
	(void)snprintf(out, out_size, "%s", g_lsp_mock.last_did_open_language_id);
	out[out_size - 1] = '\0';
}

void editorLspTestSetMockDefinitionResponse(int result_code,
		const struct editorLspLocation *locations, int count) {
	editorLspFreeLocations(g_lsp_mock.definition_locations, g_lsp_mock.definition_location_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = result_code;

	if (locations == NULL || count <= 0) {
		return;
	}
	(void)editorLspCopyLocations(&g_lsp_mock.definition_locations,
			&g_lsp_mock.definition_location_count, locations, count);
}

void editorLspTestSetMockDiagnostics(const char *path, const struct editorLspDiagnostic *diagnostics,
		int count) {
	editorLspFreeDiagnostics(g_lsp_mock.diagnostics, g_lsp_mock.diagnostic_count);
	g_lsp_mock.diagnostics = NULL;
	g_lsp_mock.diagnostic_count = 0;
	free(g_lsp_mock.diagnostic_path);
	g_lsp_mock.diagnostic_path = path != NULL ? strdup(path) : NULL;
	(void)editorLspCopyDiagnostics(&g_lsp_mock.diagnostics, &g_lsp_mock.diagnostic_count,
			diagnostics, count);
}

void editorLspTestSetMockCodeActionResult(int result_code,
		const struct editorLspDiagnostic *edits, int count) {
	editorLspFreePendingEdits(g_lsp_mock.code_action_edits, g_lsp_mock.code_action_edit_count);
	g_lsp_mock.code_action_edits = NULL;
	g_lsp_mock.code_action_edit_count = 0;
	g_lsp_mock.code_action_result_code = result_code;
	if (edits == NULL || count <= 0) {
		return;
	}
	g_lsp_mock.code_action_edits = calloc((size_t)count, sizeof(*g_lsp_mock.code_action_edits));
	if (g_lsp_mock.code_action_edits == NULL) {
		g_lsp_mock.code_action_result_code = -1;
		return;
	}
	g_lsp_mock.code_action_edit_count = count;
	for (int i = 0; i < count; i++) {
		g_lsp_mock.code_action_edits[i].start_line = edits[i].start_line;
		g_lsp_mock.code_action_edits[i].start_character = edits[i].start_character;
		g_lsp_mock.code_action_edits[i].end_line = edits[i].end_line;
		g_lsp_mock.code_action_edits[i].end_character = edits[i].end_character;
		g_lsp_mock.code_action_edits[i].new_text =
				edits[i].message != NULL ? strdup(edits[i].message) : strdup("");
		if (g_lsp_mock.code_action_edits[i].new_text == NULL) {
			editorLspFreePendingEdits(g_lsp_mock.code_action_edits,
					g_lsp_mock.code_action_edit_count);
			g_lsp_mock.code_action_edits = NULL;
			g_lsp_mock.code_action_edit_count = 0;
			g_lsp_mock.code_action_result_code = -1;
			return;
		}
	}
}

int editorLspTestParseDefinitionResponse(const char *response_json,
		struct editorLspLocation **locations_out, int *count_out) {
	if (response_json == NULL) {
		return 0;
	}
	return editorLspParseDefinitionLocations(response_json, locations_out, count_out);
}

char *editorLspTestBuildInitializeRequestJson(int request_id, const char *root_uri,
		int process_id) {
	return editorLspBuildInitializeRequestJson(request_id, root_uri, process_id);
}
