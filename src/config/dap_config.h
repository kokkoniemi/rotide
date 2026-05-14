#ifndef CONFIG_DAP_CONFIG_H
#define CONFIG_DAP_CONFIG_H

#include "rotide.h"

enum editorDapConfigLoadStatus {
	EDITOR_DAP_CONFIG_LOAD_OK = 0,
	EDITOR_DAP_CONFIG_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_DAP_CONFIG_LOAD_OUT_OF_MEMORY = 1 << 2
};

void editorDapConfigInitDefaults(void);
enum editorDapConfigLoadStatus editorDapConfigLoadFromPaths(
		const char *global_path, const char *project_path);
enum editorDapConfigLoadStatus editorDapConfigLoadConfiguredGlobal(void);
enum editorDapConfigLoadStatus editorDapConfigReloadProject(const char *project_root);
int editorDapBuildProjectConfigPath(const char *project_root, char *buf, size_t bufsize);
const struct editorDapAdapterConfig *editorDapAdapterById(const char *id);
int editorDapCreateProjectLaunchFromDefault(int default_idx, const char *project_root);

#endif
