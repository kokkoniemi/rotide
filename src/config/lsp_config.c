#include "config/lsp_config.h"

#include "config/internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum editorLspConfigFileStatus {
	EDITOR_LSP_CONFIG_FILE_APPLIED = 0,
	EDITOR_LSP_CONFIG_FILE_MISSING,
	EDITOR_LSP_CONFIG_FILE_INVALID,
	EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY
};

void editorLspConfigInitDefaults(int *enabled_out, char *command_out, size_t command_out_size,
		char *install_command_out, size_t install_command_out_size) {
	if (enabled_out != NULL) {
		*enabled_out = 1;
	}
	if (command_out != NULL && command_out_size != 0) {
		(void)snprintf(command_out, command_out_size, "%s", "gopls");
		command_out[command_out_size - 1] = '\0';
	}
	if (install_command_out != NULL && install_command_out_size != 0) {
		(void)snprintf(install_command_out, install_command_out_size, "%s",
				"go install golang.org/x/tools/gopls@latest");
		install_command_out[install_command_out_size - 1] = '\0';
	}
}

static int editorParseBooleanValue(const char *value, int *out) {
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

static enum editorLspConfigFileStatus editorLspConfigApplyConfigFile(int *enabled_in_out,
		char *command_in_out, size_t command_in_out_size, char *install_command_in_out,
		size_t install_command_in_out_size, int allow_install_command_override, const char *path) {
	if (enabled_in_out == NULL || command_in_out == NULL || command_in_out_size == 0 ||
			install_command_in_out == NULL || install_command_in_out_size == 0) {
		return EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_LSP_CONFIG_FILE_MISSING;
		}
		return EDITOR_LSP_CONFIG_FILE_INVALID;
	}

	int enabled = *enabled_in_out;
	char *command = malloc(command_in_out_size);
	char *install_command = malloc(install_command_in_out_size);
	if (command == NULL || install_command == NULL) {
		free(command);
		free(install_command);
		fclose(fp);
		return EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}
	(void)snprintf(command, command_in_out_size, "%s", command_in_out);
	command[command_in_out_size - 1] = '\0';
	(void)snprintf(install_command, install_command_in_out_size, "%s", install_command_in_out);
	install_command[install_command_in_out_size - 1] = '\0';

	int in_lsp_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			free(command);
			free(install_command);
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
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
				free(command);
				free(install_command);
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				free(command);
				free(install_command);
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}

			in_lsp_table = strcmp(table, "lsp") == 0;
			continue;
		}

		if (!in_lsp_table) {
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			free(command);
			free(install_command);
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		if (setting_name[0] == '\0') {
			free(command);
			free(install_command);
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		if (strcmp(setting_name, "enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				free(command);
				free(install_command);
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "gopls_command") == 0) {
			if (!editorConfigParseQuotedValue(value, command, command_in_out_size) ||
					command[0] == '\0') {
				free(command);
				free(install_command);
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}

		if (strcmp(setting_name, "gopls_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(value, install_command, install_command_in_out_size) ||
					install_command[0] == '\0') {
				free(command);
				free(install_command);
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
	}

	if (ferror(fp)) {
		free(command);
		free(install_command);
		fclose(fp);
		return EDITOR_LSP_CONFIG_FILE_INVALID;
	}

	fclose(fp);
	*enabled_in_out = enabled;
	(void)snprintf(command_in_out, command_in_out_size, "%s", command);
	command_in_out[command_in_out_size - 1] = '\0';
	(void)snprintf(install_command_in_out, install_command_in_out_size, "%s", install_command);
	install_command_in_out[install_command_in_out_size - 1] = '\0';
	free(command);
	free(install_command);
	return EDITOR_LSP_CONFIG_FILE_APPLIED;
}

enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(int *enabled_out,
		char *command_out, size_t command_out_size, char *install_command_out,
		size_t install_command_out_size, const char *global_path, const char *project_path) {
	if (enabled_out == NULL || command_out == NULL || command_out_size == 0 ||
			install_command_out == NULL || install_command_out_size == 0) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	editorLspConfigInitDefaults(enabled_out, command_out, command_out_size, install_command_out,
			install_command_out_size);
	enum editorLspConfigLoadStatus status = EDITOR_LSP_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum editorLspConfigFileStatus global_status =
				editorLspConfigApplyConfigFile(enabled_out, command_out, command_out_size,
						install_command_out, install_command_out_size, 1, global_path);
		if (global_status == EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size,
					install_command_out, install_command_out_size);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size,
					install_command_out, install_command_out_size);
			status = (enum editorLspConfigLoadStatus)(
					status | EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorLspConfigFileStatus project_status =
				editorLspConfigApplyConfigFile(enabled_out, command_out, command_out_size,
						install_command_out, install_command_out_size, 0, project_path);
		if (project_status == EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size,
					install_command_out, install_command_out_size);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(enabled_out, command_out, command_out_size,
					install_command_out, install_command_out_size);
			status = (enum editorLspConfigLoadStatus)(
					status | EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT);
		}
	}

	return status;
}

enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(int *enabled_out,
		char *command_out, size_t command_out_size, char *install_command_out,
		size_t install_command_out_size) {
	if (enabled_out == NULL || command_out == NULL || command_out_size == 0 ||
			install_command_out == NULL || install_command_out_size == 0) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorLspConfigLoadFromPaths(enabled_out, command_out, command_out_size,
				install_command_out, install_command_out_size, NULL, ".rotide.toml");
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorLspConfigInitDefaults(enabled_out, command_out, command_out_size,
				install_command_out, install_command_out_size);
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(enabled_out, command_out, command_out_size,
					install_command_out, install_command_out_size, global_path, ".rotide.toml");
	free(global_path);
	return status;
}
