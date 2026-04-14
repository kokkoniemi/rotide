#ifndef CONFIG_LSP_CONFIG_H
#define CONFIG_LSP_CONFIG_H

#include <stddef.h>

enum editorLspConfigLoadStatus {
	EDITOR_LSP_CONFIG_LOAD_OK = 0,
	EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY = 1 << 2
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
		size_t vscode_langservers_install_command_out_size);
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
		const char *project_path);
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
		size_t vscode_langservers_install_command_out_size);

#endif
