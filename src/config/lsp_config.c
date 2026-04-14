#include "config/lsp_config.h"

#include "config/common.h"

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

void editorLspConfigInitDefaults(int *gopls_enabled_out, int *clangd_enabled_out,
		int *html_enabled_out, int *css_enabled_out, int *json_enabled_out,
		int *javascript_enabled_out, int *eslint_enabled_out, char *gopls_command_out,
		size_t gopls_command_out_size,
		char *gopls_install_command_out, size_t gopls_install_command_out_size,
		char *clangd_command_out, size_t clangd_command_out_size, char *html_command_out,
		size_t html_command_out_size, char *css_command_out, size_t css_command_out_size,
		char *json_command_out, size_t json_command_out_size, char *javascript_command_out,
		size_t javascript_command_out_size, char *javascript_install_command_out,
		size_t javascript_install_command_out_size, char *eslint_command_out,
		size_t eslint_command_out_size, char *vscode_langservers_install_command_out,
		size_t vscode_langservers_install_command_out_size) {
	if (gopls_enabled_out != NULL) {
		*gopls_enabled_out = 1;
	}
	if (clangd_enabled_out != NULL) {
		*clangd_enabled_out = 1;
	}
	if (html_enabled_out != NULL) {
		*html_enabled_out = 1;
	}
	if (css_enabled_out != NULL) {
		*css_enabled_out = 1;
	}
	if (json_enabled_out != NULL) {
		*json_enabled_out = 1;
	}
	if (javascript_enabled_out != NULL) {
		*javascript_enabled_out = 1;
	}
	if (eslint_enabled_out != NULL) {
		*eslint_enabled_out = 1;
	}
	if (gopls_command_out != NULL && gopls_command_out_size != 0) {
		(void)snprintf(gopls_command_out, gopls_command_out_size, "%s", "gopls");
		gopls_command_out[gopls_command_out_size - 1] = '\0';
	}
	if (gopls_install_command_out != NULL && gopls_install_command_out_size != 0) {
		(void)snprintf(gopls_install_command_out, gopls_install_command_out_size, "%s",
				"go install golang.org/x/tools/gopls@latest");
		gopls_install_command_out[gopls_install_command_out_size - 1] = '\0';
	}
	if (clangd_command_out != NULL && clangd_command_out_size != 0) {
		(void)snprintf(clangd_command_out, clangd_command_out_size, "%s", "clangd");
		clangd_command_out[clangd_command_out_size - 1] = '\0';
	}
	if (html_command_out != NULL && html_command_out_size != 0) {
		(void)snprintf(html_command_out, html_command_out_size, "%s",
				"~/.local/bin/vscode-html-language-server --stdio");
		html_command_out[html_command_out_size - 1] = '\0';
	}
	if (css_command_out != NULL && css_command_out_size != 0) {
		(void)snprintf(css_command_out, css_command_out_size, "%s",
				"~/.local/bin/vscode-css-language-server --stdio");
		css_command_out[css_command_out_size - 1] = '\0';
	}
	if (json_command_out != NULL && json_command_out_size != 0) {
		(void)snprintf(json_command_out, json_command_out_size, "%s",
				"~/.local/bin/vscode-json-language-server --stdio");
		json_command_out[json_command_out_size - 1] = '\0';
	}
	if (javascript_command_out != NULL && javascript_command_out_size != 0) {
		(void)snprintf(javascript_command_out, javascript_command_out_size, "%s",
				"~/.local/bin/typescript-language-server --stdio");
		javascript_command_out[javascript_command_out_size - 1] = '\0';
	}
	if (javascript_install_command_out != NULL && javascript_install_command_out_size != 0) {
		(void)snprintf(javascript_install_command_out, javascript_install_command_out_size, "%s",
				"npm install --global --prefix ~/.local typescript typescript-language-server");
		javascript_install_command_out[javascript_install_command_out_size - 1] = '\0';
	}
	if (eslint_command_out != NULL && eslint_command_out_size != 0) {
		(void)snprintf(eslint_command_out, eslint_command_out_size, "%s",
				"~/.local/bin/vscode-eslint-language-server --stdio");
		eslint_command_out[eslint_command_out_size - 1] = '\0';
	}
	if (vscode_langservers_install_command_out != NULL &&
			vscode_langservers_install_command_out_size != 0) {
		(void)snprintf(vscode_langservers_install_command_out,
				vscode_langservers_install_command_out_size, "%s",
				"npm install --global --prefix ~/.local vscode-langservers-extracted");
		vscode_langservers_install_command_out[
				vscode_langservers_install_command_out_size - 1] = '\0';
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

static enum editorLspConfigFileStatus editorLspConfigApplyConfigFile(int *gopls_enabled_in_out,
		int *clangd_enabled_in_out, int *html_enabled_in_out, int *css_enabled_in_out,
		int *json_enabled_in_out, int *javascript_enabled_in_out, int *eslint_enabled_in_out,
		char *gopls_command_in_out, size_t gopls_command_in_out_size,
		char *gopls_install_command_in_out, size_t gopls_install_command_in_out_size,
		char *clangd_command_in_out, size_t clangd_command_in_out_size,
		char *html_command_in_out, size_t html_command_in_out_size,
		char *css_command_in_out, size_t css_command_in_out_size,
		char *json_command_in_out, size_t json_command_in_out_size,
		char *javascript_command_in_out, size_t javascript_command_in_out_size,
		char *javascript_install_command_in_out,
		size_t javascript_install_command_in_out_size,
		char *eslint_command_in_out, size_t eslint_command_in_out_size,
		char *vscode_langservers_install_command_in_out,
		size_t vscode_langservers_install_command_in_out_size,
		int allow_install_command_override, const char *path) {
	if (gopls_enabled_in_out == NULL || clangd_enabled_in_out == NULL ||
			html_enabled_in_out == NULL || css_enabled_in_out == NULL ||
			json_enabled_in_out == NULL || javascript_enabled_in_out == NULL ||
			eslint_enabled_in_out == NULL ||
			gopls_command_in_out == NULL ||
			gopls_command_in_out_size == 0 || gopls_install_command_in_out == NULL ||
			gopls_install_command_in_out_size == 0 || clangd_command_in_out == NULL ||
			clangd_command_in_out_size == 0 || html_command_in_out == NULL ||
			html_command_in_out_size == 0 || css_command_in_out == NULL ||
			css_command_in_out_size == 0 || json_command_in_out == NULL ||
			json_command_in_out_size == 0 || javascript_command_in_out == NULL ||
			javascript_command_in_out_size == 0 || javascript_install_command_in_out == NULL ||
			javascript_install_command_in_out_size == 0 || eslint_command_in_out == NULL ||
			eslint_command_in_out_size == 0 ||
			vscode_langservers_install_command_in_out == NULL ||
			vscode_langservers_install_command_in_out_size == 0) {
		return EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return EDITOR_LSP_CONFIG_FILE_MISSING;
		}
		return EDITOR_LSP_CONFIG_FILE_INVALID;
	}

	int gopls_enabled = *gopls_enabled_in_out;
	int clangd_enabled = *clangd_enabled_in_out;
	int html_enabled = *html_enabled_in_out;
	int css_enabled = *css_enabled_in_out;
	int json_enabled = *json_enabled_in_out;
	int javascript_enabled = *javascript_enabled_in_out;
	int eslint_enabled = *eslint_enabled_in_out;
	char *gopls_command = malloc(gopls_command_in_out_size);
	char *gopls_install_command = malloc(gopls_install_command_in_out_size);
	char *clangd_command = malloc(clangd_command_in_out_size);
	char *html_command = malloc(html_command_in_out_size);
	char *css_command = malloc(css_command_in_out_size);
	char *json_command = malloc(json_command_in_out_size);
	char *javascript_command = malloc(javascript_command_in_out_size);
	char *javascript_install_command = malloc(javascript_install_command_in_out_size);
	char *eslint_command = malloc(eslint_command_in_out_size);
	char *vscode_langservers_install_command =
			malloc(vscode_langservers_install_command_in_out_size);
	if (gopls_command == NULL || gopls_install_command == NULL || clangd_command == NULL ||
			html_command == NULL || css_command == NULL || json_command == NULL ||
			javascript_command == NULL || javascript_install_command == NULL ||
			eslint_command == NULL || vscode_langservers_install_command == NULL) {
		free(gopls_command);
		free(gopls_install_command);
		free(clangd_command);
		free(html_command);
		free(css_command);
		free(json_command);
		free(javascript_command);
		free(javascript_install_command);
		free(eslint_command);
		free(vscode_langservers_install_command);
		fclose(fp);
		return EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY;
	}
	(void)snprintf(gopls_command, gopls_command_in_out_size, "%s", gopls_command_in_out);
	gopls_command[gopls_command_in_out_size - 1] = '\0';
	(void)snprintf(gopls_install_command, gopls_install_command_in_out_size, "%s",
			gopls_install_command_in_out);
	gopls_install_command[gopls_install_command_in_out_size - 1] = '\0';
	(void)snprintf(clangd_command, clangd_command_in_out_size, "%s", clangd_command_in_out);
	clangd_command[clangd_command_in_out_size - 1] = '\0';
	(void)snprintf(html_command, html_command_in_out_size, "%s", html_command_in_out);
	html_command[html_command_in_out_size - 1] = '\0';
	(void)snprintf(css_command, css_command_in_out_size, "%s", css_command_in_out);
	css_command[css_command_in_out_size - 1] = '\0';
	(void)snprintf(json_command, json_command_in_out_size, "%s", json_command_in_out);
	json_command[json_command_in_out_size - 1] = '\0';
	(void)snprintf(javascript_command, javascript_command_in_out_size, "%s",
			javascript_command_in_out);
	javascript_command[javascript_command_in_out_size - 1] = '\0';
	(void)snprintf(javascript_install_command, javascript_install_command_in_out_size, "%s",
			javascript_install_command_in_out);
	javascript_install_command[javascript_install_command_in_out_size - 1] = '\0';
	(void)snprintf(eslint_command, eslint_command_in_out_size, "%s", eslint_command_in_out);
	eslint_command[eslint_command_in_out_size - 1] = '\0';
	(void)snprintf(vscode_langservers_install_command,
			vscode_langservers_install_command_in_out_size, "%s",
			vscode_langservers_install_command_in_out);
	vscode_langservers_install_command[
			vscode_langservers_install_command_in_out_size - 1] = '\0';

#define EDITOR_LSP_CONFIG_FREE_LOCAL() \
	do { \
		free(gopls_command); \
		free(gopls_install_command); \
		free(clangd_command); \
		free(html_command); \
		free(css_command); \
		free(json_command); \
		free(javascript_command); \
		free(javascript_install_command); \
		free(eslint_command); \
		free(vscode_langservers_install_command); \
	} while (0)

	int in_lsp_table = 0;
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			EDITOR_LSP_CONFIG_FREE_LOCAL();
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
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			*close = '\0';
			char *table = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
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
			EDITOR_LSP_CONFIG_FREE_LOCAL();
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		*eq = '\0';
		char *setting_name = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(setting_name);
		char *value = editorConfigTrimLeft(eq + 1);
		if (setting_name[0] == '\0') {
			EDITOR_LSP_CONFIG_FREE_LOCAL();
			fclose(fp);
			return EDITOR_LSP_CONFIG_FILE_INVALID;
		}

		if (strcmp(setting_name, "enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			gopls_enabled = parsed_enabled;
			clangd_enabled = parsed_enabled;
			html_enabled = parsed_enabled;
			css_enabled = parsed_enabled;
			json_enabled = parsed_enabled;
			javascript_enabled = parsed_enabled;
			eslint_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "gopls_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			gopls_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "clangd_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			clangd_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "html_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			html_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "css_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			css_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "json_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			json_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "eslint_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			eslint_enabled = parsed_enabled;
			continue;
		}
		if (strcmp(setting_name, "javascript_enabled") == 0) {
			int parsed_enabled = 0;
			if (!editorParseBooleanValue(value, &parsed_enabled)) {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			javascript_enabled = parsed_enabled;
			continue;
		}

		if (strcmp(setting_name, "gopls_command") == 0) {
			if (!editorConfigParseQuotedValue(value, gopls_command, gopls_command_in_out_size) ||
					gopls_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}

		if (strcmp(setting_name, "gopls_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(value, gopls_install_command,
						gopls_install_command_in_out_size) ||
					gopls_install_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "clangd_command") == 0) {
			if (!editorConfigParseQuotedValue(value, clangd_command, clangd_command_in_out_size) ||
					clangd_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "html_command") == 0) {
			if (!editorConfigParseQuotedValue(value, html_command, html_command_in_out_size) ||
					html_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "css_command") == 0) {
			if (!editorConfigParseQuotedValue(value, css_command, css_command_in_out_size) ||
					css_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "json_command") == 0) {
			if (!editorConfigParseQuotedValue(value, json_command, json_command_in_out_size) ||
					json_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "eslint_command") == 0) {
			if (!editorConfigParseQuotedValue(value, eslint_command, eslint_command_in_out_size) ||
					eslint_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "javascript_command") == 0) {
			if (!editorConfigParseQuotedValue(value, javascript_command,
						javascript_command_in_out_size) || javascript_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "javascript_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(value, javascript_install_command,
						javascript_install_command_in_out_size) ||
					javascript_install_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
		if (strcmp(setting_name, "vscode_langservers_install_command") == 0) {
			if (!allow_install_command_override) {
				continue;
			}
			if (!editorConfigParseQuotedValue(value, vscode_langservers_install_command,
						vscode_langservers_install_command_in_out_size) ||
					vscode_langservers_install_command[0] == '\0') {
				EDITOR_LSP_CONFIG_FREE_LOCAL();
				fclose(fp);
				return EDITOR_LSP_CONFIG_FILE_INVALID;
			}
			continue;
		}
	}

	if (ferror(fp)) {
		EDITOR_LSP_CONFIG_FREE_LOCAL();
		fclose(fp);
		return EDITOR_LSP_CONFIG_FILE_INVALID;
	}

	fclose(fp);
	*gopls_enabled_in_out = gopls_enabled;
	*clangd_enabled_in_out = clangd_enabled;
	*html_enabled_in_out = html_enabled;
	*css_enabled_in_out = css_enabled;
	*json_enabled_in_out = json_enabled;
	*javascript_enabled_in_out = javascript_enabled;
	*eslint_enabled_in_out = eslint_enabled;
	(void)snprintf(gopls_command_in_out, gopls_command_in_out_size, "%s", gopls_command);
	gopls_command_in_out[gopls_command_in_out_size - 1] = '\0';
	(void)snprintf(gopls_install_command_in_out, gopls_install_command_in_out_size, "%s",
			gopls_install_command);
	gopls_install_command_in_out[gopls_install_command_in_out_size - 1] = '\0';
	(void)snprintf(clangd_command_in_out, clangd_command_in_out_size, "%s", clangd_command);
	clangd_command_in_out[clangd_command_in_out_size - 1] = '\0';
	(void)snprintf(html_command_in_out, html_command_in_out_size, "%s", html_command);
	html_command_in_out[html_command_in_out_size - 1] = '\0';
	(void)snprintf(css_command_in_out, css_command_in_out_size, "%s", css_command);
	css_command_in_out[css_command_in_out_size - 1] = '\0';
	(void)snprintf(json_command_in_out, json_command_in_out_size, "%s", json_command);
	json_command_in_out[json_command_in_out_size - 1] = '\0';
	(void)snprintf(javascript_command_in_out, javascript_command_in_out_size, "%s",
			javascript_command);
	javascript_command_in_out[javascript_command_in_out_size - 1] = '\0';
	(void)snprintf(javascript_install_command_in_out, javascript_install_command_in_out_size, "%s",
			javascript_install_command);
	javascript_install_command_in_out[javascript_install_command_in_out_size - 1] = '\0';
	(void)snprintf(eslint_command_in_out, eslint_command_in_out_size, "%s", eslint_command);
	eslint_command_in_out[eslint_command_in_out_size - 1] = '\0';
	(void)snprintf(vscode_langservers_install_command_in_out,
			vscode_langservers_install_command_in_out_size, "%s",
			vscode_langservers_install_command);
	vscode_langservers_install_command_in_out[
			vscode_langservers_install_command_in_out_size - 1] = '\0';
	EDITOR_LSP_CONFIG_FREE_LOCAL();
#undef EDITOR_LSP_CONFIG_FREE_LOCAL
	return EDITOR_LSP_CONFIG_FILE_APPLIED;
}

enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(int *gopls_enabled_out,
		int *clangd_enabled_out, int *html_enabled_out, int *css_enabled_out,
		int *json_enabled_out, int *javascript_enabled_out, int *eslint_enabled_out,
		char *gopls_command_out, size_t gopls_command_out_size, char *gopls_install_command_out,
		size_t gopls_install_command_out_size, char *clangd_command_out,
		size_t clangd_command_out_size, char *html_command_out,
		size_t html_command_out_size, char *css_command_out, size_t css_command_out_size,
		char *json_command_out, size_t json_command_out_size, char *javascript_command_out,
		size_t javascript_command_out_size, char *javascript_install_command_out,
		size_t javascript_install_command_out_size, char *eslint_command_out,
		size_t eslint_command_out_size, char *vscode_langservers_install_command_out,
		size_t vscode_langservers_install_command_out_size, const char *global_path,
		const char *project_path) {
	if (gopls_enabled_out == NULL || clangd_enabled_out == NULL || html_enabled_out == NULL ||
			css_enabled_out == NULL || json_enabled_out == NULL ||
			javascript_enabled_out == NULL || eslint_enabled_out == NULL ||
			gopls_command_out == NULL || gopls_command_out_size == 0 ||
			gopls_install_command_out == NULL || gopls_install_command_out_size == 0 ||
			clangd_command_out == NULL || clangd_command_out_size == 0 ||
			html_command_out == NULL || html_command_out_size == 0 ||
			css_command_out == NULL || css_command_out_size == 0 ||
			json_command_out == NULL || json_command_out_size == 0 ||
			javascript_command_out == NULL || javascript_command_out_size == 0 ||
			javascript_install_command_out == NULL ||
			javascript_install_command_out_size == 0 ||
			eslint_command_out == NULL || eslint_command_out_size == 0 ||
			vscode_langservers_install_command_out == NULL ||
			vscode_langservers_install_command_out_size == 0) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	editorLspConfigInitDefaults(gopls_enabled_out, clangd_enabled_out, html_enabled_out,
			css_enabled_out, json_enabled_out, javascript_enabled_out, eslint_enabled_out,
			gopls_command_out, gopls_command_out_size, gopls_install_command_out,
			gopls_install_command_out_size, clangd_command_out, clangd_command_out_size,
			html_command_out, html_command_out_size, css_command_out, css_command_out_size,
			json_command_out, json_command_out_size, javascript_command_out,
			javascript_command_out_size, javascript_install_command_out,
			javascript_install_command_out_size, eslint_command_out, eslint_command_out_size,
			vscode_langservers_install_command_out,
			vscode_langservers_install_command_out_size);
	enum editorLspConfigLoadStatus status = EDITOR_LSP_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum editorLspConfigFileStatus global_status =
				editorLspConfigApplyConfigFile(gopls_enabled_out, clangd_enabled_out,
						html_enabled_out, css_enabled_out, json_enabled_out,
						javascript_enabled_out, eslint_enabled_out, gopls_command_out,
						gopls_command_out_size,
						gopls_install_command_out, gopls_install_command_out_size,
						clangd_command_out, clangd_command_out_size, html_command_out,
						html_command_out_size, css_command_out, css_command_out_size,
						json_command_out, json_command_out_size, javascript_command_out,
						javascript_command_out_size, javascript_install_command_out,
						javascript_install_command_out_size, eslint_command_out,
						eslint_command_out_size, vscode_langservers_install_command_out,
						vscode_langservers_install_command_out_size, 1, global_path);
		if (global_status == EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(gopls_enabled_out, clangd_enabled_out,
					html_enabled_out, css_enabled_out, json_enabled_out,
					javascript_enabled_out, eslint_enabled_out,
					gopls_command_out, gopls_command_out_size,
					gopls_install_command_out, gopls_install_command_out_size,
					clangd_command_out, clangd_command_out_size, html_command_out,
					html_command_out_size, css_command_out, css_command_out_size,
					json_command_out, json_command_out_size, javascript_command_out,
					javascript_command_out_size, javascript_install_command_out,
					javascript_install_command_out_size, eslint_command_out,
					eslint_command_out_size, vscode_langservers_install_command_out,
					vscode_langservers_install_command_out_size);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (global_status == EDITOR_LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(gopls_enabled_out, clangd_enabled_out,
					html_enabled_out, css_enabled_out, json_enabled_out,
					javascript_enabled_out, eslint_enabled_out,
					gopls_command_out, gopls_command_out_size,
					gopls_install_command_out, gopls_install_command_out_size,
					clangd_command_out, clangd_command_out_size, html_command_out,
					html_command_out_size, css_command_out, css_command_out_size,
					json_command_out, json_command_out_size, javascript_command_out,
					javascript_command_out_size, javascript_install_command_out,
					javascript_install_command_out_size, eslint_command_out,
					eslint_command_out_size, vscode_langservers_install_command_out,
					vscode_langservers_install_command_out_size);
			status = (enum editorLspConfigLoadStatus)(
					status | EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum editorLspConfigFileStatus project_status =
				editorLspConfigApplyConfigFile(gopls_enabled_out, clangd_enabled_out,
						html_enabled_out, css_enabled_out, json_enabled_out,
						javascript_enabled_out, eslint_enabled_out, gopls_command_out,
						gopls_command_out_size,
						gopls_install_command_out, gopls_install_command_out_size,
						clangd_command_out, clangd_command_out_size, html_command_out,
						html_command_out_size, css_command_out, css_command_out_size,
						json_command_out, json_command_out_size, javascript_command_out,
						javascript_command_out_size, javascript_install_command_out,
						javascript_install_command_out_size, eslint_command_out,
						eslint_command_out_size, vscode_langservers_install_command_out,
						vscode_langservers_install_command_out_size, 0, project_path);
		if (project_status == EDITOR_LSP_CONFIG_FILE_OUT_OF_MEMORY) {
			editorLspConfigInitDefaults(gopls_enabled_out, clangd_enabled_out,
					html_enabled_out, css_enabled_out, json_enabled_out,
					javascript_enabled_out, eslint_enabled_out,
					gopls_command_out, gopls_command_out_size,
					gopls_install_command_out, gopls_install_command_out_size,
					clangd_command_out, clangd_command_out_size, html_command_out,
					html_command_out_size, css_command_out, css_command_out_size,
					json_command_out, json_command_out_size, javascript_command_out,
					javascript_command_out_size, javascript_install_command_out,
					javascript_install_command_out_size, eslint_command_out,
					eslint_command_out_size, vscode_langservers_install_command_out,
					vscode_langservers_install_command_out_size);
			return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
		}
		if (project_status == EDITOR_LSP_CONFIG_FILE_INVALID) {
			editorLspConfigInitDefaults(gopls_enabled_out, clangd_enabled_out,
					html_enabled_out, css_enabled_out, json_enabled_out,
					javascript_enabled_out, eslint_enabled_out,
					gopls_command_out, gopls_command_out_size,
					gopls_install_command_out, gopls_install_command_out_size,
					clangd_command_out, clangd_command_out_size, html_command_out,
					html_command_out_size, css_command_out, css_command_out_size,
					json_command_out, json_command_out_size, javascript_command_out,
					javascript_command_out_size, javascript_install_command_out,
					javascript_install_command_out_size, eslint_command_out,
					eslint_command_out_size, vscode_langservers_install_command_out,
					vscode_langservers_install_command_out_size);
			status = (enum editorLspConfigLoadStatus)(
					status | EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT);
		}
	}

	return status;
}

enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(int *gopls_enabled_out,
		int *clangd_enabled_out, int *html_enabled_out, int *css_enabled_out,
		int *json_enabled_out, int *javascript_enabled_out, int *eslint_enabled_out,
		char *gopls_command_out, size_t gopls_command_out_size, char *gopls_install_command_out,
		size_t gopls_install_command_out_size, char *clangd_command_out,
		size_t clangd_command_out_size, char *html_command_out,
		size_t html_command_out_size, char *css_command_out, size_t css_command_out_size,
		char *json_command_out, size_t json_command_out_size, char *javascript_command_out,
		size_t javascript_command_out_size, char *javascript_install_command_out,
		size_t javascript_install_command_out_size, char *eslint_command_out,
		size_t eslint_command_out_size, char *vscode_langservers_install_command_out,
		size_t vscode_langservers_install_command_out_size) {
	if (gopls_enabled_out == NULL || clangd_enabled_out == NULL || html_enabled_out == NULL ||
			css_enabled_out == NULL || json_enabled_out == NULL ||
			javascript_enabled_out == NULL || eslint_enabled_out == NULL ||
			gopls_command_out == NULL || gopls_command_out_size == 0 ||
			gopls_install_command_out == NULL || gopls_install_command_out_size == 0 ||
			clangd_command_out == NULL || clangd_command_out_size == 0 ||
			html_command_out == NULL || html_command_out_size == 0 ||
			css_command_out == NULL || css_command_out_size == 0 ||
			json_command_out == NULL || json_command_out_size == 0 ||
			javascript_command_out == NULL || javascript_command_out_size == 0 ||
			javascript_install_command_out == NULL ||
			javascript_install_command_out_size == 0 ||
			eslint_command_out == NULL || eslint_command_out_size == 0 ||
			vscode_langservers_install_command_out == NULL ||
			vscode_langservers_install_command_out_size == 0) {
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return editorLspConfigLoadFromPaths(gopls_enabled_out, clangd_enabled_out,
				html_enabled_out, css_enabled_out, json_enabled_out, javascript_enabled_out,
				eslint_enabled_out,
				gopls_command_out, gopls_command_out_size,
				gopls_install_command_out, gopls_install_command_out_size,
				clangd_command_out, clangd_command_out_size, html_command_out,
				html_command_out_size, css_command_out, css_command_out_size,
				json_command_out, json_command_out_size, javascript_command_out,
				javascript_command_out_size, javascript_install_command_out,
				javascript_install_command_out_size, eslint_command_out,
				eslint_command_out_size, vscode_langservers_install_command_out,
				vscode_langservers_install_command_out_size, NULL, ".rotide.toml");
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorLspConfigInitDefaults(gopls_enabled_out, clangd_enabled_out,
				html_enabled_out, css_enabled_out, json_enabled_out, javascript_enabled_out,
				eslint_enabled_out,
				gopls_command_out, gopls_command_out_size,
				gopls_install_command_out, gopls_install_command_out_size,
				clangd_command_out, clangd_command_out_size, html_command_out,
				html_command_out_size, css_command_out, css_command_out_size,
				json_command_out, json_command_out_size, javascript_command_out,
				javascript_command_out_size, javascript_install_command_out,
				javascript_install_command_out_size, eslint_command_out,
				eslint_command_out_size, vscode_langservers_install_command_out,
				vscode_langservers_install_command_out_size);
		return EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(gopls_enabled_out, clangd_enabled_out,
					html_enabled_out, css_enabled_out, json_enabled_out,
					javascript_enabled_out, eslint_enabled_out,
					gopls_command_out, gopls_command_out_size,
					gopls_install_command_out, gopls_install_command_out_size,
					clangd_command_out, clangd_command_out_size, html_command_out,
					html_command_out_size, css_command_out, css_command_out_size,
					json_command_out, json_command_out_size, javascript_command_out,
					javascript_command_out_size, javascript_install_command_out,
					javascript_install_command_out_size, eslint_command_out,
					eslint_command_out_size, vscode_langservers_install_command_out,
					vscode_langservers_install_command_out_size, global_path, ".rotide.toml");
	free(global_path);
	return status;
}
