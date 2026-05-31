#include "config/lsp_config.h"

#include "config/common.h"

#include <errno.h>
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

static enum lspConfigFileStatus lspConfigApplyFile(struct editorLspConfig *config,
                                                   int allow_install_command_override,
                                                   const char *path) {
	if (config == NULL) {
		return LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return LSP_CONFIG_FILE_MISSING;
		}
		return LSP_CONFIG_FILE_INVALID;
	}

	struct editorLspConfig *parsed = malloc(sizeof(*parsed));
	if (parsed == NULL) {
		(void)fclose(fp);
		return LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}
	*parsed = *config;

#define LSP_CONFIG_FREE_LOCAL()                                                                    \
	do {                                                                                       \
		free(parsed);                                                                      \
	} while (0)

	int in_lsp_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			LSP_CONFIG_FREE_LOCAL();
			(void)fclose(fp);
			return LSP_CONFIG_FILE_INVALID;
		}

		editorConfigStripInlineComment(line);
		editorConfigTrimRight(line);
		char *trimmed = editorConfigTrimLeft(line);
		if (trimmed[0] == '\0') {
			continue;
		}

		if (trimmed[0] == '[') {
			char *close = strchr(trimmed, ']');
			if (close == NULL) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}

			in_lsp_table = strcmp(table, "lsp") == 0;
			continue;
		}

		if (!in_lsp_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			LSP_CONFIG_FREE_LOCAL();
			(void)fclose(fp);
			return LSP_CONFIG_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		if (setting_name[0] == '\0') {
			LSP_CONFIG_FREE_LOCAL();
			(void)fclose(fp);
			return LSP_CONFIG_FILE_INVALID;
		}

		if (strcmp(setting_name, "enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->gopls_enabled = parsed_enabled;
			parsed->clangd_enabled = parsed_enabled;
			parsed->html_enabled = parsed_enabled;
			parsed->css_enabled = parsed_enabled;
			parsed->json_enabled = parsed_enabled;
			parsed->javascript_enabled = parsed_enabled;
			parsed->eslint_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "gopls_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->gopls_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "clangd_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->clangd_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "html_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->html_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "css_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->css_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "json_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->json_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "eslint_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->eslint_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "javascript_enabled") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->javascript_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "autocomplete") == 0) {
			int parsed_enabled = 0;
			if (!lspConfigParseBooleanValue(value, &parsed_enabled)) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->autocomplete_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "autocomplete_max_items") == 0) {
			char *endptr = NULL;
			long parsed_long = strtol(value, &endptr, 10);
			if (endptr == value || endptr == NULL || *endptr != '\0' ||
			    parsed_long < 1 || parsed_long > 1000) {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			parsed->autocomplete_max_items = (int)parsed_long;
			continue;
		}

		if (strcmp(setting_name, "gopls_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->gopls_command,
			                                  sizeof(parsed->gopls_command)) ||
			    parsed->gopls_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}

		if (strcmp(setting_name, "gopls_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(value, parsed->gopls_install_command,
			                                  sizeof(parsed->gopls_install_command)) ||
			    parsed->gopls_install_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "clangd_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->clangd_command,
			                                  sizeof(parsed->clangd_command)) ||
			    parsed->clangd_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "html_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->html_command,
			                                  sizeof(parsed->html_command)) ||
			    parsed->html_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "css_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->css_command,
			                                  sizeof(parsed->css_command)) ||
			    parsed->css_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "json_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->json_command,
			                                  sizeof(parsed->json_command)) ||
			    parsed->json_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "eslint_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->eslint_command,
			                                  sizeof(parsed->eslint_command)) ||
			    parsed->eslint_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "javascript_command") == 0) {
			if (!editorConfigParseQuotedValue(value, parsed->javascript_command,
			                                  sizeof(parsed->javascript_command)) ||
			    parsed->javascript_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "javascript_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(
			            value, parsed->javascript_install_command,
			            sizeof(parsed->javascript_install_command)) ||
			    parsed->javascript_install_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "vscode_langservers_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(
			            value, parsed->vscode_langservers_install_command,
			            sizeof(parsed->vscode_langservers_install_command)) ||
			    parsed->vscode_langservers_install_command[0] == '\0') {
				LSP_CONFIG_FREE_LOCAL();
				(void)fclose(fp);
				return LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
	}

	if (ferror(fp)) {
		LSP_CONFIG_FREE_LOCAL();
		(void)fclose(fp);
		return LSP_CONFIG_FILE_INVALID;
	}

	(void)fclose(fp);
	*config = *parsed;
	LSP_CONFIG_FREE_LOCAL();
#undef LSP_CONFIG_FREE_LOCAL
	return LSP_CONFIG_FILE_APPLIED;
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
