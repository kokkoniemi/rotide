#include "input/actions_language.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "input/prompt.h"
#include "language/lsp.h"
#include "workspace/task.h"

#include <string.h>

enum editorGoToDefinitionInstallFamily {
	EDITOR_GOTO_DEF_INSTALL_NONE = 0,
	EDITOR_GOTO_DEF_INSTALL_GOPLS,
	EDITOR_GOTO_DEF_INSTALL_CLANGD,
	EDITOR_GOTO_DEF_INSTALL_JAVASCRIPT,
	EDITOR_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS
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

static enum editorGoToDefinitionInstallFamily
editorLanguageGoToInstallFamilyForLanguage(void) {
	const char *server_name = editorLanguageGoToServerName();
	if (server_name != NULL && strcmp(server_name, "gopls") == 0) {
		return EDITOR_GOTO_DEF_INSTALL_GOPLS;
	}
	if (server_name != NULL && strcmp(server_name, "clangd") == 0) {
		return EDITOR_GOTO_DEF_INSTALL_CLANGD;
	}
	if (server_name != NULL && strcmp(server_name, "typescript-language-server") == 0) {
		return EDITOR_GOTO_DEF_INSTALL_JAVASCRIPT;
	}
	if (editorLspUsesSharedVscodeInstallPrompt(E.filename, E.syntax_language)) {
		return EDITOR_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS;
	}
	return EDITOR_GOTO_DEF_INSTALL_NONE;
}

static void editorLanguagePromptInstallJavascriptServer(void) {
	if (!editorPromptYesNo("typescript-language-server not found. Install now? [y/N] %s")) {
		editorSetStatusMsg("typescript-language-server not installed");
		return;
	}
	if (!editorTaskStart("Task: Install typescript-language-server",
				E.lsp_javascript_install_command,
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
				E.lsp_vscode_langservers_install_command,
				"vscode-langservers-extracted installed. Retry Ctrl-O",
				"vscode-langservers-extracted install failed; see task log")) {
		if (E.statusmsg[0] == '\0') {
			editorSetStatusMsg("Unable to start vscode-langservers-extracted install");
		}
	}
}

void editorLanguageMaybePromptInstallServer(void) {
	if (editorLspLastStartupFailureReason() != EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND) {
		return;
	}
	switch (editorLanguageGoToInstallFamilyForLanguage()) {
		case EDITOR_GOTO_DEF_INSTALL_GOPLS:
			if (!editorPromptYesNo("gopls not found. Install now? [y/N] %s")) {
				editorSetStatusMsg("gopls not installed");
				return;
			}
			if (!editorTaskStart("Task: Install gopls", E.lsp_gopls_install_command,
						"gopls installed. Retry Ctrl-O",
						"gopls install failed; see task log")) {
				if (E.statusmsg[0] == '\0') {
					editorSetStatusMsg("Unable to start gopls install");
				}
			}
			return;
		case EDITOR_GOTO_DEF_INSTALL_CLANGD: {
			static const char message[] =
					"clangd was not found on PATH.\n"
					"\n"
					"Install instructions:\n"
					"https://clangd.llvm.org/installation\n"
					"\n"
					"clangd usually needs a compile_commands.json compilation database for C/C++ projects.\n"
					"\n"
					"Create compile_commands.json with CMake:\n"
					"- cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n"
					"- use build/compile_commands.json, or copy/symlink it into the project root\n"
					"\n"
					"Create compile_commands.json with Bear:\n"
					"- bear -- make\n"
					"- or bear -- <your normal build command>\n"
					"- this is often a good fit for pure C projects that already build without CMake\n"
					"\n"
					"After installing clangd and setting up compile_commands.json:\n"
					"- retry Ctrl-O or Ctrl + left click\n"
					"- set [lsp].clangd_command in .rotide.toml if clangd is installed in a custom location\n";
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
			return;
		}
		case EDITOR_GOTO_DEF_INSTALL_JAVASCRIPT:
			editorLanguagePromptInstallJavascriptServer();
			return;
		case EDITOR_GOTO_DEF_INSTALL_VSCODE_LANGSERVERS:
			editorLanguagePromptInstallSharedVscodeServers();
			return;
		default:
			return;
	}
}

int editorHandleLanguageMappedAction(enum editorAction action, int cursor_or_edit_effect_bit,
		void (*pin_active_preview_for_edit)(void), editorLanguageActionFn goto_definition,
		editorLanguageActionFn goto_implementation, editorLanguageActionFn goto_symbol,
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
	case EDITOR_ACTION_GOTO_SYMBOL:
		editorHistoryBreakGroup();
		if (goto_symbol != NULL) {
			goto_symbol();
		}
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
