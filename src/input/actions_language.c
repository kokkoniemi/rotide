#include "input/actions_language.h"

#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "input/prompt.h"
#include "language/lsp.h"
#include "language/lsp_protocol.h"
#include "language/syntax.h"
#include "render/viewport.h"
#include "rotide.h"
#include "text/document.h"
#include "workspace/task.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

enum actionsLanguageGoToDefinitionInstallFamily {
	ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE = 0,
	ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_GOPLS,
	ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_CLANGD,
	ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_JAVASCRIPT,
	ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS,
	ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_TEXLAB
};

int editorLanguageGoToSupported(enum editorSyntaxLanguage language) {
	if (editorLspFileSupportsDefinition(E.filename, language)) {
		return 1;
	}
	return language == EDITOR_SYNTAX_GO || language == EDITOR_SYNTAX_C ||
	       language == EDITOR_SYNTAX_HTML || language == EDITOR_SYNTAX_CSS ||
	       language == EDITOR_SYNTAX_JAVASCRIPT;
}

int editorLanguageGoToEnabled(void) {
	return editorLspFileEnabled(E.filename, E.syntax_language);
}

const char *editorLanguageGoToLabel(void) {
	const char *label = editorLspLanguageLabelForFile(E.filename, E.syntax_language);
	if (label != NULL) {
		return label;
	}
	if (E.syntax_language == EDITOR_SYNTAX_GO) {
		return "Go";
	}
	if (E.syntax_language == EDITOR_SYNTAX_C) {
		return "C/C++";
	}
	if (E.syntax_language == EDITOR_SYNTAX_HTML) {
		return "HTML";
	}
	if (E.syntax_language == EDITOR_SYNTAX_CSS) {
		return "CSS/SCSS";
	}
	return NULL;
}

const char *editorLanguageGoToServerName(void) {
	return editorLspServerNameForFile(E.filename, E.syntax_language);
}

const char *editorLanguageGoToCommand(void) {
	return editorLspCommandForFile(E.filename, E.syntax_language);
}

const char *editorLanguageGoToCommandSettingName(void) {
	return editorLspCommandSettingNameForFile(E.filename, E.syntax_language);
}

static enum actionsLanguageGoToDefinitionInstallFamily
actionsLanguageGoToInstallFamilyForLanguage(void) {
	const char *server_name = editorLanguageGoToServerName();
	if (server_name != NULL && strcmp(server_name, "gopls") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_GOPLS;
	}
	if (server_name != NULL && strcmp(server_name, "clangd") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_CLANGD;
	}
	if (server_name != NULL && strcmp(server_name, "typescript-language-server") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_JAVASCRIPT;
	}
	if (server_name != NULL && strcmp(server_name, "texlab") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_TEXLAB;
	}
	if (editorLspUsesSharedVscodeInstallPrompt(E.filename, E.syntax_language)) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS;
	}
	return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE;
}

static void actionsLanguagePromptInstallJavascriptServer(void) {
	if (!editorPromptYesNo("typescript-language-server not found. Install now? [y/N] %s")) {
		editorSetStatusMsg("typescript-language-server not installed");
		return;
	}
	if (!editorTaskStart("Task: Install typescript-language-server",
	                     E.lsp_config.javascript_install_command,
	                     "typescript-language-server installed. Retry Ctrl-O",
	                     "typescript-language-server install failed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("Unable to start typescript-language-server install");
		}
	}
}

void editorLanguagePromptInstallSharedVscodeServers(void) {
	if (!editorPromptYesNo("vscode-langservers-extracted not found. Install now? [y/N] %s")) {
		editorSetStatusMsg("vscode-langservers-extracted not installed");
		return;
	}
	if (!editorTaskStart("Task: Install vscode-langservers-extracted",
	                     E.lsp_config.vscode_langservers_install_command,
	                     "vscode-langservers-extracted installed. Retry Ctrl-O",
	                     "vscode-langservers-extracted install failed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("Unable to start vscode-langservers-extracted install");
		}
	}
}

const char *editorLanguageTexlabAssetFor(const char *sysname, const char *machine, int is_musl) {
	if (sysname == NULL || machine == NULL) {
		return NULL;
	}
	if (strcmp(sysname, "Linux") == 0) {
		if (strcmp(machine, "x86_64") == 0) {
			return is_musl ? "texlab-x86_64-alpine.tar.gz"
			               : "texlab-x86_64-linux.tar.gz";
		}
		if (strcmp(machine, "aarch64") == 0 || strcmp(machine, "arm64") == 0) {
			return "texlab-aarch64-linux.tar.gz";
		}
		if (strcmp(machine, "armv7l") == 0 || strcmp(machine, "armv7hf") == 0) {
			return "texlab-armv7hf-linux.tar.gz";
		}
		return NULL;
	}
	if (strcmp(sysname, "Darwin") == 0) {
		if (strcmp(machine, "x86_64") == 0) {
			return "texlab-x86_64-macos.tar.gz";
		}
		if (strcmp(machine, "arm64") == 0 || strcmp(machine, "aarch64") == 0) {
			return "texlab-aarch64-macos.tar.gz";
		}
		return NULL;
	}
	return NULL;
}

/* Resolve the prebuilt texlab asset for the live host, or NULL if none. */
static const char *actionsLanguageTexlabPrebuiltAsset(void) {
	struct utsname info;
	if (uname(&info) != 0) {
		return NULL;
	}
	int is_musl = access("/etc/alpine-release", F_OK) == 0;
	return editorLanguageTexlabAssetFor(info.sysname, info.machine, is_musl);
}

/*
 * Build the hardened download+extract command for a matched asset. The asset
 * name comes from our own fixed table (no user input), and we still guard
 * against truncation. TLS-only, fail-on-error curl; no checksum verification
 * (texlab publishes none) — see PLAN.md "Download integrity".
 */
static int actionsLanguageBuildTexlabInstallCommand(const char *asset, char *out, size_t out_size) {
	if (asset == NULL || out == NULL || out_size == 0) {
		return 0;
	}
	int written = snprintf(out, out_size,
	                       "mkdir -p ~/.local/bin && curl --proto '=https' --tlsv1.2 -fSL "
	                       "https://github.com/latex-lsp/texlab/releases/latest/download/%s "
	                       "| tar -xzf - -C ~/.local/bin texlab",
	                       asset);
	return written > 0 && (size_t)written < out_size;
}

static void actionsLanguagePromptInstallTexlabServer(void) {
	/* Explicit override wins: run it verbatim, like gopls. */
	if (E.lsp_config.texlab_install_command[0] != '\0') {
		if (!editorPromptYesNo("texlab not found. Install now? [y/N] %s")) {
			editorSetStatusMsg("texlab not installed");
			return;
		}
		if (!editorTaskStart("Task: Install texlab", E.lsp_config.texlab_install_command,
		                     "texlab installed. Retry Ctrl-O",
		                     "texlab install failed; see task log")) {
			if (E.statusmsg[0] == '\0') {
				editorSetStatusMsg("Unable to start texlab install");
			}
		}
		return;
	}

	const char *asset = actionsLanguageTexlabPrebuiltAsset();
	if (asset != NULL) {
		char command[512];
		if (!actionsLanguageBuildTexlabInstallCommand(asset, command, sizeof(command))) {
			editorSetStatusMsg("Unable to build texlab install command");
			return;
		}
		if (!editorPromptYesNo(
		            "texlab not found. Download prebuilt binary now? [y/N] %s")) {
			editorSetStatusMsg("texlab not installed");
			return;
		}
		if (!editorTaskStart("Task: Install texlab", command,
		                     "texlab installed. Retry Ctrl-O",
		                     "texlab install failed; see task log")) {
			if (E.statusmsg[0] == '\0') {
				editorSetStatusMsg("Unable to start texlab install");
			}
		}
		return;
	}

	/* No prebuilt binary for this platform: show a self-serve guide. */
	static const char message[] =
	        "texlab was not found on PATH.\n"
	        "\n"
	        "No prebuilt binary is available for this platform, so please install "
	        "texlab manually.\n"
	        "\n"
	        "Project page:\n"
	        "https://github.com/latex-lsp/texlab\n"
	        "\n"
	        "Prebuilt binaries and release notes:\n"
	        "https://github.com/latex-lsp/texlab/releases\n"
	        "\n"
	        "Distro packages (see Repology):\n"
	        "https://repology.org/project/texlab/versions\n"
	        "\n"
	        "After installing texlab:\n"
	        "- retry Ctrl-O or Ctrl + left click\n"
	        "- set [lsp].texlab_command in .rotide.toml if texlab is installed in a "
	        "custom location\n";
	if (!editorPromptYesNo("texlab not found. Show install instructions? [y/N] %s")) {
		editorSetStatusMsg("texlab not installed");
		return;
	}
	if (!editorTaskShowMessage("Task: Install texlab", message,
	                           "texlab not installed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("texlab not installed");
		}
	}
}

static void actionsLanguagePromptInstallGoplsServer(void) {
	if (!editorPromptYesNo("gopls not found. Install now? [y/N] %s")) {
		editorSetStatusMsg("gopls not installed");
		return;
	}
	if (!editorTaskStart("Task: Install gopls", E.lsp_config.gopls_install_command,
	                     "gopls installed. Retry Ctrl-O",
	                     "gopls install failed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("Unable to start gopls install");
		}
	}
}

static void actionsLanguagePromptInstallClangdServer(void) {
	static const char message[] =
	        "clangd was not found on PATH.\n"
	        "\n"
	        "Install instructions:\n"
	        "https://clangd.llvm.org/installation\n"
	        "\n"
	        "clangd usually needs a compile_commands.json compilation database "
	        "for C/C++ projects.\n"
	        "\n"
	        "Create compile_commands.json with CMake:\n"
	        "- cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n"
	        "- use build/compile_commands.json, or copy/symlink it into the "
	        "project root\n"
	        "\n"
	        "Create compile_commands.json with Bear:\n"
	        "- bear -- make\n"
	        "- or bear -- <your normal build command>\n"
	        "- this is often a good fit for pure C projects that already build "
	        "without CMake\n"
	        "\n"
	        "After installing clangd and setting up compile_commands.json:\n"
	        "- retry Ctrl-O or Ctrl + left click\n"
	        "- set [lsp].clangd_command in .rotide.toml if clangd is installed "
	        "in a custom location\n";
	if (!editorPromptYesNo("clangd not found. Show install instructions? [y/N] %s")) {
		editorSetStatusMsg("clangd not installed");
		return;
	}
	if (!editorTaskShowMessage("Task: Install clangd", message,
	                           "clangd not installed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("clangd not installed");
		}
	}
}

/*
 * Resolve a server name (canonical name or a language alias) to an install
 * family, or ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE if unrecognized. Pure;
 * shared by editorLanguageServerNameIsInstallable and
 * editorLanguageInstallServerByName.
 */
static enum actionsLanguageGoToDefinitionInstallFamily
actionsLanguageInstallFamilyForServerName(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE;
	}
	if (strcmp(name, "gopls") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_GOPLS;
	}
	if (strcmp(name, "clangd") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_CLANGD;
	}
	if (strcmp(name, "texlab") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_TEXLAB;
	}
	if (strcmp(name, "typescript-language-server") == 0 || strcmp(name, "javascript") == 0 ||
	    strcmp(name, "typescript") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_JAVASCRIPT;
	}
	if (strcmp(name, "vscode-langservers-extracted") == 0 || strcmp(name, "html") == 0 ||
	    strcmp(name, "css") == 0 || strcmp(name, "json") == 0 || strcmp(name, "eslint") == 0) {
		return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS;
	}
	return ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE;
}

static void
actionsLanguagePromptInstallFamily(enum actionsLanguageGoToDefinitionInstallFamily family) {
	switch (family) {
		case ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_GOPLS:
			actionsLanguagePromptInstallGoplsServer();
			return;
		case ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_CLANGD:
			actionsLanguagePromptInstallClangdServer();
			return;
		case ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_JAVASCRIPT:
			actionsLanguagePromptInstallJavascriptServer();
			return;
		case ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS:
			editorLanguagePromptInstallSharedVscodeServers();
			return;
		case ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_TEXLAB:
			actionsLanguagePromptInstallTexlabServer();
			return;
		default:
			return;
	}
}

int editorLanguageServerNameIsInstallable(const char *name) {
	return actionsLanguageInstallFamilyForServerName(name) !=
	       ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE;
}

int editorLanguageInstallServerByName(const char *name) {
	enum actionsLanguageGoToDefinitionInstallFamily family =
	        actionsLanguageInstallFamilyForServerName(name);
	if (family == ACTIONS_LANGUAGE_GOTO_DEF_INSTALL_NONE) {
		return 0;
	}
	actionsLanguagePromptInstallFamily(family);
	return 1;
}

void editorLanguageMaybePromptInstallServer(void) {
	if (editorLspLastStartupFailureReason() != EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
		return;
	}
	actionsLanguagePromptInstallFamily(actionsLanguageGoToInstallFamilyForLanguage());
}

/*
 * Shared failure path for actions that need a running LSP server: offer the
 * install prompt when the server binary is missing, otherwise surface a
 * generic message unless startup already left a more specific "LSP ..." one.
 */
void editorLanguageReportLspUnavailable(void) {
	if (editorLspLastStartupFailureReason() == EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
		editorLanguageMaybePromptInstallServer();
		return;
	}
	if (strncmp(E.statusmsg, "LSP ", strlen("LSP ")) != 0) {
		editorSetStatusMsg("LSP unavailable for this file");
	}
}

/* Buffer position (cy, byte cx) of a diagnostic's start, clamped to the buffer. */
static void actionsLanguageDiagnosticPos(const struct editorLspDiagnostic *diag, int *cy_out,
                                         int *cx_out) {
	struct editorLineView line = {0};
	int cy = diag->start_line;
	int cx = 0;

	if (cy < 0) {
		cy = 0;
	}
	if (cy >= E.numrows) {
		cy = E.numrows > 0 ? E.numrows - 1 : 0;
	}
	if (editorDocumentLineView(E.document, cy, &line)) {
		cx = editorLspUtf16UnitsToUtf8Column(line.data, (size_t)line.size,
		                                     diag->start_character);
		if (cx > line.size) {
			cx = line.size;
		}
		editorLineViewRelease(&line);
	}
	*cy_out = cy;
	*cx_out = cx;
}

static int actionsLanguageDiagnosticIsAfter(int cy, int cx, int cur_cy, int cur_cx) {
	return cy > cur_cy || (cy == cur_cy && cx > cur_cx);
}

/* Move the cursor to the next/previous diagnostic in the current buffer, wrapping
 * around. Diagnostics are already held in `E.lsp_diagnostics` for the drawer. */
static void actionsLanguageGotoDiagnostic(int forward) {
	int best = -1;
	int best_cy = 0;
	int best_cx = 0;
	int wrap = -1;
	int wrap_cy = 0;
	int wrap_cx = 0;
	size_t offset = 0;

	if (E.lsp_diagnostic_count <= 0 || E.lsp_diagnostics == NULL) {
		editorSetStatusMsg("No diagnostics");
		return;
	}
	for (int i = 0; i < E.lsp_diagnostic_count; i++) {
		int cy = 0;
		int cx = 0;
		actionsLanguageDiagnosticPos(&E.lsp_diagnostics[i], &cy, &cx);
		int relative = actionsLanguageDiagnosticIsAfter(cy, cx, E.cy, E.cx);
		if (forward ? relative : (!relative && !(cy == E.cy && cx == E.cx))) {
			if (best < 0 ||
			    (forward ? actionsLanguageDiagnosticIsAfter(best_cy, best_cx, cy, cx)
			             : actionsLanguageDiagnosticIsAfter(cy, cx, best_cy,
			                                                best_cx))) {
				best = i;
				best_cy = cy;
				best_cx = cx;
			}
		}
		/* Track the global extreme for wrap-around. */
		if (wrap < 0 ||
		    (forward ? actionsLanguageDiagnosticIsAfter(wrap_cy, wrap_cx, cy, cx)
		             : actionsLanguageDiagnosticIsAfter(cy, cx, wrap_cy, wrap_cx))) {
			wrap = i;
			wrap_cy = cy;
			wrap_cx = cx;
		}
	}
	if (best < 0) {
		best = wrap;
		best_cy = wrap_cy;
		best_cx = wrap_cx;
	}
	if (best < 0 || !editorBufferPosToOffset(best_cy, best_cx, &offset) ||
	    !editorSyncCursorFromOffset(offset)) {
		return;
	}
	editorViewportEnsureCursorVisible();
	if (E.lsp_diagnostics[best].message != NULL) {
		editorSetStatusMsg("%s", E.lsp_diagnostics[best].message);
	}
}

static int actionsLanguageLatexRequestReady(void) {
	if (E.tab_kind != EDITOR_TAB_FILE || (E.syntax_language != EDITOR_SYNTAX_LATEX &&
	                                      E.syntax_language != EDITOR_SYNTAX_BIBTEX)) {
		editorSetStatusMsg("LaTeX actions require a LaTeX or BibTeX file");
		return 0;
	}
	if (E.filename == NULL || E.filename[0] == '\0') {
		editorSetStatusMsg("Save this LaTeX buffer before using LaTeX actions");
		return 0;
	}
	if (!E.lsp_config.texlab_enabled) {
		editorSetStatusMsg("texlab is disabled in config");
		return 0;
	}
	if (E.lsp_config.texlab_command[0] == '\0') {
		editorSetStatusMsg("LSP disabled: [lsp].texlab_command is empty");
		return 0;
	}

	editorLspEnsureActiveDocumentTracked();
	if (!E.lsp_doc_open) {
		editorLanguageReportLspUnavailable();
		return 0;
	}
	return 1;
}

static void actionsLanguageLatexForwardSearch(void) {
	if (!actionsLanguageLatexRequestReady()) {
		return;
	}

	if (E.lsp_config.texlab_pdf_viewer[0] == '\0' &&
	    E.lsp_config.texlab_forward_search_command[0] == '\0') {
		editorSetStatusMsg("Forward search viewer not configured (set texlab_pdf_viewer)");
		return;
	}
	int result = editorLspRequestForwardSearch(E.filename, E.syntax_language, E.cy, E.cx);
	if (result <= 0) {
		editorSetStatusMsg("Forward search failed");
		return;
	}
	editorSetStatusMsg("Forward search sent to viewer");
}

static void actionsLanguageLatexBuild(void) {
	if (!actionsLanguageLatexRequestReady()) {
		return;
	}
	if (editorLspRequestBuild(E.filename, E.syntax_language) <= 0) {
		editorSetStatusMsg("LaTeX build request failed");
		return;
	}
	editorSetStatusMsg("LaTeX build started (diagnostics update when done)");
}

int editorHandleLanguageMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
                                     void (*pin_active_preview_for_edit)(void),
                                     editorLanguageActionFn goto_definition,
                                     editorLanguageActionFn goto_implementation,
                                     editorLanguageActionFn goto_references,
                                     editorLanguageActionFn hover,
                                     editorLanguageActionFn goto_symbol,
                                     editorLanguageActionFn apply_eslint_fixes, int *effects_io) {
	int effects = effects_io != NULL ? *effects_io : 0;

	switch (action) {
		case EDITOR_ACTION_GOTO_DEFINITION:
			editorHistoryBreakGroup();
			if (goto_definition != NULL) {
				goto_definition();
			}
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_GOTO_IMPLEMENTATION:
			editorHistoryBreakGroup();
			if (goto_implementation != NULL) {
				goto_implementation();
			}
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_GOTO_REFERENCES:
			editorHistoryBreakGroup();
			if (goto_references != NULL) {
				goto_references();
			}
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_HOVER:
			editorHistoryBreakGroup();
			if (hover != NULL) {
				hover();
			}
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_LATEX_FORWARD_SEARCH:
			actionsLanguageLatexForwardSearch();
			break;
		case EDITOR_ACTION_LATEX_BUILD:
			actionsLanguageLatexBuild();
			break;
		case EDITOR_ACTION_GOTO_SYMBOL:
			editorHistoryBreakGroup();
			if (goto_symbol != NULL) {
				goto_symbol();
			}
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_DIAGNOSTIC_NEXT:
			editorHistoryBreakGroup();
			actionsLanguageGotoDiagnostic(1);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_DIAGNOSTIC_PREV:
			editorHistoryBreakGroup();
			actionsLanguageGotoDiagnostic(0);
			effects |= cursor_or_edit_effect_bit;
			break;
		case EDITOR_ACTION_ESLINT_FIX:
			editorHistoryBreakGroup();
			if (pin_active_preview_for_edit != NULL) {
				pin_active_preview_for_edit();
			}
			if (apply_eslint_fixes != NULL) {
				apply_eslint_fixes();
			}
			effects |= cursor_or_edit_effect_bit;
			break;
		default:
			return 0;
	}

	if (effects_io != NULL) {
		*effects_io = effects;
	}
	return 1;
}
