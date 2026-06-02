#include "config/lsp_config.h"

#include "config/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum lspConfigFileStatus {
	LSP_CONFIG_FILE_APPLIED = 0,
	LSP_CONFIG_FILE_MISSING,
	LSP_CONFIG_FILE_INVALID,
	LSP_CONFIG_FILE_OUT_OF_MEMORY
};

void editorLspConfigInitDefaults(struct editorLspConfig *config) {
	if (config == NULL) {
		return;
	}

	config->autocomplete_enabled = 1;
	config->autocomplete_max_items = 100;
	config->gopls_enabled = 1;
	config->clangd_enabled = 1;
	config->html_enabled = 1;
	config->css_enabled = 1;
	config->json_enabled = 1;
	config->javascript_enabled = 1;
	config->eslint_enabled = 1;
	(void)snprintf(config->gopls_command, sizeof(config->gopls_command), "%s", "gopls");
	(void)snprintf(config->gopls_install_command, sizeof(config->gopls_install_command), "%s",
	               "go install golang.org/x/tools/gopls@latest");
	(void)snprintf(config->clangd_command, sizeof(config->clangd_command), "%s", "clangd");
	(void)snprintf(config->html_command, sizeof(config->html_command), "%s",
	               "~/.local/bin/vscode-html-language-server --stdio");
	(void)snprintf(config->css_command, sizeof(config->css_command), "%s",
	               "~/.local/bin/vscode-css-language-server --stdio");
	(void)snprintf(config->json_command, sizeof(config->json_command), "%s",
	               "~/.local/bin/vscode-json-language-server --stdio");
	(void)snprintf(config->javascript_command, sizeof(config->javascript_command), "%s",
	               "~/.local/bin/typescript-language-server --stdio");
	(void)snprintf(config->javascript_install_command,
	               sizeof(config->javascript_install_command), "%s",
	               "npm install --global --prefix ~/.local typescript "
	               "typescript-language-server");
	(void)snprintf(config->eslint_command, sizeof(config->eslint_command), "%s",
	               "~/.local/bin/vscode-eslint-language-server --stdio");
	(void)snprintf(config->vscode_langservers_install_command,
	               sizeof(config->vscode_langservers_install_command), "%s",
	               "npm install --global --prefix ~/.local vscode-langservers-extracted");
}

static int lspConfigParseBooleanValue(const char *value, int *out) {
	if (value == NULL || out == NULL) {
		return 0;
	}
	if (strcasecmp(value, "true") == 0) {
		*out = 1;
		return 1;
	}
	if (strcasecmp(value, "false") == 0) {
		*out = 0;
		return 1;
	}
	return 0;
}

struct lspConfigApplyContext {
	struct editorLspConfig *config;
	int allow_install_command_override;
};

static int lspConfigOnSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "lsp") == 0;
}

static int lspConfigParseCommandValue(char *value, char *dst, size_t dst_size) {
	return editorConfigParseQuotedValue(value, dst, dst_size) && dst[0] != '\0';
}

static int lspConfigOnEntry(void *ctx, const char *key, char *value) {
	struct lspConfigApplyContext *apply = ctx;
	struct editorLspConfig *config = apply->config;

	if (strcmp(key, "enabled") == 0) {
		int enabled = 0;
		if (!lspConfigParseBooleanValue(value, &enabled)) {
			return 0;
		}
		config->gopls_enabled = enabled;
		config->clangd_enabled = enabled;
		config->html_enabled = enabled;
		config->css_enabled = enabled;
		config->json_enabled = enabled;
		config->javascript_enabled = enabled;
		config->eslint_enabled = enabled;
		return 1;
	}
	if (strcmp(key, "gopls_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->gopls_enabled);
	}
	if (strcmp(key, "clangd_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->clangd_enabled);
	}
	if (strcmp(key, "html_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->html_enabled);
	}
	if (strcmp(key, "css_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->css_enabled);
	}
	if (strcmp(key, "json_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->json_enabled);
	}
	if (strcmp(key, "eslint_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->eslint_enabled);
	}
	if (strcmp(key, "javascript_enabled") == 0) {
		return lspConfigParseBooleanValue(value, &config->javascript_enabled);
	}
	if (strcmp(key, "autocomplete") == 0) {
		return lspConfigParseBooleanValue(value, &config->autocomplete_enabled);
	}
	if (strcmp(key, "autocomplete_max_items") == 0) {
		char *endptr = NULL;
		long parsed_long = strtol(value, &endptr, 10);
		if (endptr == value || endptr == NULL || *endptr != '\0' || parsed_long < 1 ||
		    parsed_long > 1000) {
			return 0;
		}
		config->autocomplete_max_items = (int)parsed_long;
		return 1;
	}
	if (strcmp(key, "gopls_command") == 0) {
		return lspConfigParseCommandValue(value, config->gopls_command,
		                                  sizeof(config->gopls_command));
	}
	if (strcmp(key, "gopls_install_command") == 0) {
		if (!apply->allow_install_command_override) {
			return 1;
		}
		return lspConfigParseCommandValue(value, config->gopls_install_command,
		                                  sizeof(config->gopls_install_command));
	}
	if (strcmp(key, "clangd_command") == 0) {
		return lspConfigParseCommandValue(value, config->clangd_command,
		                                  sizeof(config->clangd_command));
	}
	if (strcmp(key, "html_command") == 0) {
		return lspConfigParseCommandValue(value, config->html_command,
		                                  sizeof(config->html_command));
	}
	if (strcmp(key, "css_command") == 0) {
		return lspConfigParseCommandValue(value, config->css_command,
		                                  sizeof(config->css_command));
	}
	if (strcmp(key, "json_command") == 0) {
		return lspConfigParseCommandValue(value, config->json_command,
		                                  sizeof(config->json_command));
	}
	if (strcmp(key, "eslint_command") == 0) {
		return lspConfigParseCommandValue(value, config->eslint_command,
		                                  sizeof(config->eslint_command));
	}
	if (strcmp(key, "javascript_command") == 0) {
		return lspConfigParseCommandValue(value, config->javascript_command,
		                                  sizeof(config->javascript_command));
	}
	if (strcmp(key, "javascript_install_command") == 0) {
		if (!apply->allow_install_command_override) {
			return 1;
		}
		return lspConfigParseCommandValue(value, config->javascript_install_command,
		                                  sizeof(config->javascript_install_command));
	}
	if (strcmp(key, "vscode_langservers_install_command") == 0) {
		if (!apply->allow_install_command_override) {
			return 1;
		}
		return lspConfigParseCommandValue(
		        value, config->vscode_langservers_install_command,
		        sizeof(config->vscode_langservers_install_command));
	}
	return 1;
}

static enum lspConfigFileStatus lspConfigApplyFile(struct editorLspConfig *config,
                                                   int allow_install_command_override,
                                                   const char *path) {
	if (config == NULL) {
		return LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}

	struct editorLspConfig *parsed = malloc(sizeof(*parsed));
	if (parsed == NULL) {
		return LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}
	*parsed = *config;

	struct lspConfigApplyContext apply = {
	        .config = parsed,
	        .allow_install_command_override = allow_install_command_override,
	};
	struct editorConfigScanner scanner = {lspConfigOnSection, lspConfigOnEntry};

	enum lspConfigFileStatus result;
	switch (editorConfigScanFile(path, &scanner, &apply)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			result = LSP_CONFIG_FILE_MISSING;
			break;
		case EDITOR_CONFIG_SCAN_OK:
			*config = *parsed;
			result = LSP_CONFIG_FILE_APPLIED;
			break;
		default:
			result = LSP_CONFIG_FILE_INVALID;
			break;
	}

	free(parsed);
	return result;
}

enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(struct editorLspConfig *config,
                                                            const char *global_path,
                                                            const char *project_path) {
	if (config == NULL) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	editorLspConfigInitDefaults(config);
	enum editorLspConfigLoadStatus status = EDITOR_LSP_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum lspConfigFileStatus global_status = lspConfigApplyFile(config, 1, global_path);
		if (global_status == LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(config);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(config);
			status = (enum editorLspConfigLoadStatus)(
			        status | EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum lspConfigFileStatus project_status =
		        lspConfigApplyFile(config, 0, project_path);
		if (project_status == LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(config);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(config);
			status = (enum editorLspConfigLoadStatus)(
			        status | EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT);
		}
	}

	return status;
}

enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(struct editorLspConfig *config) {
	if (config == NULL) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorLspConfigLoadFromPaths(config, NULL, NULL);
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorLspConfigInitDefaults(config);
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	enum editorLspConfigLoadStatus status =
	        editorLspConfigLoadFromPaths(config, global_path, NULL);
	free(global_path);
	return status;
}
