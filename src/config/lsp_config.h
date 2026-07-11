#ifndef ROTIDE_CONFIG_LSP_CONFIG_H
#define ROTIDE_CONFIG_LSP_CONFIG_H

#include <limits.h>

enum editorLspConfigLoadStatus {
	EDITOR_LSP_CONFIG_LOAD_OK = 0,
	EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_LSP_CONFIG_LOAD_OUT_OF_MEMORY = 1 << 2
};

struct editorLspConfig {
	int gopls_enabled;
	int clangd_enabled;
	int html_enabled;
	int css_enabled;
	int json_enabled;
	int javascript_enabled;
	int eslint_enabled;
	int texlab_enabled;
	char gopls_command[PATH_MAX];
	char gopls_install_command[PATH_MAX];
	char clangd_command[PATH_MAX];
	char html_command[PATH_MAX];
	char css_command[PATH_MAX];
	char json_command[PATH_MAX];
	char javascript_command[PATH_MAX];
	char javascript_install_command[PATH_MAX];
	char eslint_command[PATH_MAX];
	char texlab_command[PATH_MAX];
	char texlab_install_command[PATH_MAX];
	char vscode_langservers_install_command[PATH_MAX];
	int autocomplete_enabled;
	int autocomplete_max_items;
};

void editorLspConfigInitDefaults(struct editorLspConfig *config);
enum editorLspConfigLoadStatus editorLspConfigLoadFromPaths(struct editorLspConfig *config,
                                                            const char *global_path,
                                                            const char *project_path);
enum editorLspConfigLoadStatus editorLspConfigLoadConfigured(struct editorLspConfig *config);

#endif
