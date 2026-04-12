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
		char *command_out, size_t command_out_size, char *install_command_out,
		size_t install_command_out_size,
		char *clangd_command_out, size_t clangd_command_out_size);
enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(int *gopls_enabled_out,
		int *clangd_enabled_out, char *command_out, size_t command_out_size,
		char *install_command_out, size_t install_command_out_size,
		char *clangd_command_out, size_t clangd_command_out_size, const char *global_path,
		const char *project_path);
enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(int *gopls_enabled_out,
		int *clangd_enabled_out, char *command_out, size_t command_out_size,
		char *install_command_out, size_t install_command_out_size,
		char *clangd_command_out, size_t clangd_command_out_size);

#endif
