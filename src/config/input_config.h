#ifndef ROTIDE_CONFIG_INPUT_CONFIG_H
#define ROTIDE_CONFIG_INPUT_CONFIG_H

#include <stddef.h>

enum editorInputConfigLoadStatus {
	EDITOR_INPUT_CONFIG_LOAD_OK = 0,
	EDITOR_INPUT_CONFIG_LOAD_INVALID_GLOBAL = 1 << 0,
	EDITOR_INPUT_CONFIG_LOAD_INVALID_PROJECT = 1 << 1,
	EDITOR_INPUT_CONFIG_LOAD_OUT_OF_MEMORY = 1 << 2
};

void editorInputConfigInitDefaults(char *system_out, size_t system_size);
enum editorInputConfigLoadStatus editorInputConfigLoadFromPaths(char *system_out,
                                                                size_t system_size,
                                                                const char *global_path,
                                                                const char *project_path);
enum editorInputConfigLoadStatus editorInputConfigLoadConfigured(char *system_out,
                                                                 size_t system_size);

#endif
