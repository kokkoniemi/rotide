#include "config/input_config.h"

#include "config/common.h"
#include "input/input_system.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { INPUT_CONFIG_SYSTEM_MAX = 32 };

enum inputConfigFileStatus {
	INPUT_CONFIG_FILE_APPLIED = 0,
	INPUT_CONFIG_FILE_MISSING,
	INPUT_CONFIG_FILE_INVALID
};

static int inputConfigCopySystem(char *system_out, size_t system_size, const char *system) {
	if (system_out == NULL || system_size == 0 || system == NULL) {
		return 0;
	}
	int written = snprintf(system_out, system_size, "%s", system);
	return written >= 0 && (size_t)written < system_size;
}

void editorInputConfigInitDefaults(char *system_out, size_t system_size) {
	(void)inputConfigCopySystem(system_out, system_size, "vim");
}

static int inputConfigOnSection(void *ctx, const char *table) {
	(void)ctx;
	return strcmp(table, "input") == 0;
}

static int inputConfigOnEntry(void *ctx, const char *key, char *value) {
	char *system = ctx;
	if (strcmp(key, "system") != 0) {
		return 1;
	}
	char parsed[INPUT_CONFIG_SYSTEM_MAX];
	if (!editorConfigParseQuotedValue(value, parsed, sizeof(parsed))) {
		return 0;
	}
	if (editorInputSystemById(parsed) == NULL) {
		return 0;
	}
	return inputConfigCopySystem(system, INPUT_CONFIG_SYSTEM_MAX, parsed);
}

static enum inputConfigFileStatus inputConfigApplyFile(char *system, const char *path) {
	char updated[INPUT_CONFIG_SYSTEM_MAX];
	if (!inputConfigCopySystem(updated, sizeof(updated), system)) {
		return INPUT_CONFIG_FILE_INVALID;
	}
	struct editorConfigScanner scanner = {inputConfigOnSection, inputConfigOnEntry};

	switch (editorConfigScanFile(path, &scanner, updated)) {
		case EDITOR_CONFIG_SCAN_MISSING:
			return INPUT_CONFIG_FILE_MISSING;
		case EDITOR_CONFIG_SCAN_OK:
			(void)inputConfigCopySystem(system, INPUT_CONFIG_SYSTEM_MAX, updated);
			return INPUT_CONFIG_FILE_APPLIED;
		default:
			return INPUT_CONFIG_FILE_INVALID;
	}
}

enum editorInputConfigLoadStatus editorInputConfigLoadFromPaths(char *system_out,
                                                                size_t system_size,
                                                                const char *global_path,
                                                                const char *project_path) {
	if (system_out == NULL || system_size == 0) {
		return EDITOR_INPUT_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	char system[INPUT_CONFIG_SYSTEM_MAX];
	editorInputConfigInitDefaults(system, sizeof(system));
	enum editorInputConfigLoadStatus status = EDITOR_INPUT_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		enum inputConfigFileStatus global_status =
		        inputConfigApplyFile(system, global_path);
		if (global_status == INPUT_CONFIG_FILE_INVALID) {
			editorInputConfigInitDefaults(system, sizeof(system));
			status = (enum editorInputConfigLoadStatus)(
			        status | EDITOR_INPUT_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		enum inputConfigFileStatus project_status =
		        inputConfigApplyFile(system, project_path);
		if (project_status == INPUT_CONFIG_FILE_INVALID) {
			editorInputConfigInitDefaults(system, sizeof(system));
			status = (enum editorInputConfigLoadStatus)(
			        status | EDITOR_INPUT_CONFIG_LOAD_INVALID_PROJECT);
		}
	}

	if (!inputConfigCopySystem(system_out, system_size, system)) {
		editorInputConfigInitDefaults(system_out, system_size);
		return EDITOR_INPUT_CONFIG_LOAD_OUT_OF_MEMORY;
	}
	return status;
}

enum editorInputConfigLoadStatus editorInputConfigLoadConfigured(char *system_out,
                                                                 size_t system_size) {
	if (system_out == NULL || system_size == 0) {
		return EDITOR_INPUT_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	char project_path[PATH_MAX];
	if (!editorConfigBuildProjectConfigPath(NULL, project_path, sizeof(project_path))) {
		editorInputConfigInitDefaults(system_out, system_size);
		return EDITOR_INPUT_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	char *global_path = editorConfigBuildGlobalConfigPath();
	enum editorInputConfigLoadStatus status =
	        editorInputConfigLoadFromPaths(system_out, system_size, global_path, project_path);
	free(global_path);
	return status;
}
