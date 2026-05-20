#include "config/dap_config.h"

#include "config/common.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum dapConfigFileKind { DAP_CONFIG_FILE_GLOBAL = 0, DAP_CONFIG_FILE_PROJECT };

enum dapConfigTableKind {
	DAP_CONFIG_TABLE_NONE = 0,
	DAP_CONFIG_TABLE_ADAPTERS,
	DAP_CONFIG_TABLE_DEFAULT,
	DAP_CONFIG_TABLE_DEFAULT_ENV,
	DAP_CONFIG_TABLE_LAUNCH,
	DAP_CONFIG_TABLE_LAUNCH_ENV
};

struct dapConfigTable {
	enum dapConfigTableKind kind;
	int config_idx;
};

static void dapConfigLaunchFieldDropArrayValues(struct editorDapLaunchField *field) {
	free(field->array_values);
	field->array_values = NULL;
	field->array_count = 0;
}

void editorDapLaunchFieldClear(struct editorDapLaunchField *field) {
	if (field == NULL) {
		return;
	}
	free(field->array_values);
	memset(field, 0, sizeof(*field));
}

void editorDapLaunchConfigClear(struct editorDapLaunchConfig *config) {
	if (config == NULL) {
		return;
	}
	for (int i = 0; i < ROTIDE_DAP_MAX_FIELDS; i++) {
		free(config->fields[i].array_values);
		config->fields[i].array_values = NULL;
	}
	memset(config, 0, sizeof(*config));
}

void editorDapLaunchConfigsClear(struct editorDapLaunchConfig *configs, int count) {
	if (configs == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		editorDapLaunchConfigClear(&configs[i]);
	}
}

static int dapConfigParseBoolValue(const char *value, int *out) {
	if (strcmp(value, "true") == 0) {
		*out = 1;
		return 1;
	}
	if (strcmp(value, "false") == 0) {
		*out = 0;
		return 1;
	}
	return 0;
}

static int dapConfigIdValid(const char *id) {
	if (id == NULL || id[0] == '\0') {
		return 0;
	}
	for (const char *p = id; *p != '\0'; p++) {
		if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.') {
			return 0;
		}
	}
	return 1;
}

static int dapConfigCopyString(char *dst, size_t dst_size, const char *src) {
	if (dst == NULL || dst_size == 0 || src == NULL) {
		return 0;
	}
	size_t len = strlen(src);
	if (len >= dst_size) {
		return 0;
	}
	memcpy(dst, src, len + 1);
	return 1;
}

static struct editorDapLaunchConfig *dapConfigEnsureLaunch(struct editorDapLaunchConfig *configs,
                                                           int *count, const char *id) {
	if (configs == NULL || count == NULL || id == NULL || !dapConfigIdValid(id)) {
		return NULL;
	}
	for (int i = 0; i < *count; i++) {
		if (strcmp(configs[i].id, id) == 0) {
			return &configs[i];
		}
	}
	if (*count >= ROTIDE_DAP_MAX_CONFIGS) {
		return NULL;
	}
	struct editorDapLaunchConfig *config = &configs[*count];
	memset(config, 0, sizeof(*config));
	if (!dapConfigCopyString(config->id, sizeof(config->id), id)) {
		return NULL;
	}
	if (!dapConfigCopyString(config->request, sizeof(config->request), "launch")) {
		return NULL;
	}
	(*count)++;
	return config;
}

static struct editorDapLaunchField *dapConfigEnsureLaunchField(struct editorDapLaunchConfig *config,
                                                               const char *key) {
	if (config == NULL || key == NULL || key[0] == '\0') {
		return NULL;
	}
	for (int i = 0; i < config->field_count; i++) {
		if (strcmp(config->fields[i].key, key) == 0) {
			return &config->fields[i];
		}
	}
	if (config->field_count >= ROTIDE_DAP_MAX_FIELDS) {
		return NULL;
	}
	struct editorDapLaunchField *field = &config->fields[config->field_count];
	memset(field, 0, sizeof(*field));
	if (!dapConfigCopyString(field->key, sizeof(field->key), key)) {
		return NULL;
	}
	config->field_count++;
	return field;
}

int editorDapLaunchGetStringField(const struct editorDapLaunchConfig *config, const char *key,
                                  char *out, size_t out_size) {
	if (config == NULL || key == NULL || out == NULL || out_size == 0) {
		return 0;
	}
	for (int i = 0; i < config->field_count; i++) {
		if (strcmp(config->fields[i].key, key) != 0) {
			continue;
		}
		if (config->fields[i].kind != EDITOR_DAP_LAUNCH_VALUE_STRING) {
			return 0;
		}
		return dapConfigCopyString(out, out_size, config->fields[i].string_value);
	}
	return 0;
}

int editorDapLaunchSetStringField(struct editorDapLaunchConfig *config, const char *key,
                                  const char *value) {
	if (value == NULL) {
		return 0;
	}
	struct editorDapLaunchField *field = dapConfigEnsureLaunchField(config, key);
	if (field == NULL) {
		return 0;
	}
	dapConfigLaunchFieldDropArrayValues(field);
	field->kind = EDITOR_DAP_LAUNCH_VALUE_STRING;
	return dapConfigCopyString(field->string_value, sizeof(field->string_value), value);
}

void editorDapLaunchRemoveField(struct editorDapLaunchConfig *config, const char *key) {
	if (config == NULL || key == NULL) {
		return;
	}
	for (int i = 0; i < config->field_count; i++) {
		if (strcmp(config->fields[i].key, key) != 0) {
			continue;
		}
		/* Free before the shift; the slot-to-slot copies transfer ownership
		 * of array_values pointers down, and the trailing memset NULLs the
		 * vacated tail. */
		free(config->fields[i].array_values);
		config->fields[i].array_values = NULL;
		for (int j = i; j < config->field_count - 1; j++) {
			config->fields[j] = config->fields[j + 1];
		}
		config->field_count--;
		memset(&config->fields[config->field_count], 0, sizeof(config->fields[0]));
		return;
	}
}

static struct editorDapEnvVar *dapConfigEnsureEnv(struct editorDapLaunchConfig *config,
                                                  const char *key) {
	if (config == NULL || key == NULL || key[0] == '\0') {
		return NULL;
	}
	for (int i = 0; i < config->env_count; i++) {
		if (strcmp(config->env[i].key, key) == 0) {
			return &config->env[i];
		}
	}
	if (config->env_count >= ROTIDE_DAP_MAX_ENV) {
		return NULL;
	}
	struct editorDapEnvVar *env = &config->env[config->env_count];
	memset(env, 0, sizeof(*env));
	if (!dapConfigCopyString(env->key, sizeof(env->key), key)) {
		return NULL;
	}
	config->env_count++;
	return env;
}

static int
dapConfigParseStringArray(const char *value,
                          char out[ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS][ROTIDE_DAP_VALUE_MAX],
                          int *count_out) {
	const char *p = editorConfigTrimLeft((char *)value);
	if (*p != '[') {
		return 0;
	}
	p++;
	int count = 0;
	for (;;) {
		p = editorConfigTrimLeft((char *)p);
		if (*p == ']') {
			p++;
			p = editorConfigTrimLeft((char *)p);
			if (*p != '\0') {
				return 0;
			}
			*count_out = count;
			return 1;
		}
		if (*p != '"' || count >= ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS) {
			return 0;
		}
		char *tmp = strdup(p);
		if (tmp == NULL) {
			return 0;
		}
		char *end_quote = tmp + 1;
		while (*end_quote != '\0') {
			if (*end_quote == '"' && end_quote[-1] != '\\') {
				end_quote[1] = '\0';
				break;
			}
			end_quote++;
		}
		if (*end_quote != '"') {
			free(tmp);
			return 0;
		}
		if (!editorConfigParseQuotedValue(tmp, out[count], ROTIDE_DAP_VALUE_MAX)) {
			free(tmp);
			return 0;
		}
		size_t consumed = (size_t)(end_quote - tmp) + 1;
		free(tmp);
		p += consumed;
		count++;
		p = editorConfigTrimLeft((char *)p);
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p == ']') {
			continue;
		}
		return 0;
	}
}

static int dapConfigSetLaunchField(struct editorDapLaunchConfig *config, const char *key,
                                   const char *value) {
	struct editorDapLaunchField *field = dapConfigEnsureLaunchField(config, key);
	if (field == NULL) {
		return 0;
	}

	char string_value[ROTIDE_DAP_VALUE_MAX];
	if (editorConfigParseQuotedValue(value, string_value, sizeof(string_value))) {
		dapConfigLaunchFieldDropArrayValues(field);
		field->kind = EDITOR_DAP_LAUNCH_VALUE_STRING;
		return dapConfigCopyString(field->string_value, sizeof(field->string_value),
		                           string_value);
	}

	int bool_value = 0;
	if (dapConfigParseBoolValue(value, &bool_value)) {
		dapConfigLaunchFieldDropArrayValues(field);
		field->kind = EDITOR_DAP_LAUNCH_VALUE_BOOL;
		field->bool_value = bool_value;
		return 1;
	}

	char *endptr = NULL;
	long parsed = strtol(value, &endptr, 10);
	if (endptr != value && endptr != NULL && *endptr == '\0' && parsed >= -2147483647L &&
	    parsed <= 2147483647L) {
		dapConfigLaunchFieldDropArrayValues(field);
		field->kind = EDITOR_DAP_LAUNCH_VALUE_INT;
		field->int_value = (int)parsed;
		return 1;
	}

	editorDapStringArrayItem *arr = calloc(ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS, sizeof(*arr));
	if (arr == NULL) {
		return 0;
	}
	int array_count = 0;
	if (dapConfigParseStringArray(value, arr, &array_count)) {
		free(field->array_values);
		field->array_values = arr;
		field->kind = EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY;
		field->array_count = array_count;
		return 1;
	}
	free(arr);

	return 0;
}

static int dapConfigApplyLaunchSetting(struct editorDapLaunchConfig *config, const char *key,
                                       const char *value) {
	if (strcmp(key, "name") == 0) {
		char parsed[ROTIDE_DAP_NAME_MAX];
		return editorConfigParseQuotedValue(value, parsed, sizeof(parsed)) &&
		       dapConfigCopyString(config->name, sizeof(config->name), parsed);
	}
	if (strcmp(key, "adapter") == 0) {
		char parsed[ROTIDE_DAP_ID_MAX];
		return editorConfigParseQuotedValue(value, parsed, sizeof(parsed)) &&
		       dapConfigIdValid(parsed) &&
		       dapConfigCopyString(config->adapter, sizeof(config->adapter), parsed);
	}
	if (strcmp(key, "request") == 0) {
		char parsed[32];
		return editorConfigParseQuotedValue(value, parsed, sizeof(parsed)) &&
		       dapConfigCopyString(config->request, sizeof(config->request), parsed);
	}
	return dapConfigSetLaunchField(config, key, value);
}

static int dapConfigParseTable(const char *table, enum dapConfigFileKind file_kind,
                               struct dapConfigTable *table_out) {
	memset(table_out, 0, sizeof(*table_out));
	if (strcmp(table, "dap.adapters") == 0) {
		table_out->kind = file_kind == DAP_CONFIG_FILE_GLOBAL ? DAP_CONFIG_TABLE_ADAPTERS
		                                                      : DAP_CONFIG_TABLE_NONE;
		table_out->config_idx = -1;
		return 1;
	}

	const char *default_prefix = "dap.defaults.";
	const char *launch_prefix = "dap.launch.";
	const char *id = NULL;
	int is_default = 0;
	if (strncmp(table, default_prefix, strlen(default_prefix)) == 0) {
		if (file_kind != DAP_CONFIG_FILE_GLOBAL) {
			table_out->kind = DAP_CONFIG_TABLE_NONE;
			table_out->config_idx = -1;
			return 1;
		}
		id = table + strlen(default_prefix);
		is_default = 1;
	} else if (strncmp(table, launch_prefix, strlen(launch_prefix)) == 0) {
		if (file_kind != DAP_CONFIG_FILE_PROJECT) {
			table_out->kind = DAP_CONFIG_TABLE_NONE;
			table_out->config_idx = -1;
			return 1;
		}
		id = table + strlen(launch_prefix);
	} else {
		table_out->kind = DAP_CONFIG_TABLE_NONE;
		table_out->config_idx = -1;
		return 1;
	}

	int env_table = 0;
	char id_buf[ROTIDE_DAP_ID_MAX];
	const char *env_suffix = ".env";
	size_t id_len = strlen(id);
	size_t suffix_len = strlen(env_suffix);
	if (id_len > suffix_len && strcmp(id + id_len - suffix_len, env_suffix) == 0) {
		env_table = 1;
		id_len -= suffix_len;
	}
	if (id_len == 0 || id_len >= sizeof(id_buf)) {
		return 0;
	}
	memcpy(id_buf, id, id_len);
	id_buf[id_len] = '\0';
	if (!dapConfigIdValid(id_buf)) {
		return 0;
	}

	struct editorDapLaunchConfig *configs = is_default ? E.dap_defaults : E.dap_launches;
	int *count = is_default ? &E.dap_default_count : &E.dap_launch_count;
	struct editorDapLaunchConfig *config = dapConfigEnsureLaunch(configs, count, id_buf);
	if (config == NULL) {
		return 0;
	}
	table_out->config_idx = (int)(config - configs);
	if (is_default) {
		table_out->kind =
		        env_table ? DAP_CONFIG_TABLE_DEFAULT_ENV : DAP_CONFIG_TABLE_DEFAULT;
	} else {
		table_out->kind = env_table ? DAP_CONFIG_TABLE_LAUNCH_ENV : DAP_CONFIG_TABLE_LAUNCH;
	}
	return 1;
}

static int dapConfigApplyFile(const char *path, enum dapConfigFileKind file_kind,
                              int *missing_out) {
	if (missing_out != NULL) {
		*missing_out = 0;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			if (missing_out != NULL) {
				*missing_out = 1;
			}
			return 1;
		}
		return 0;
	}

	struct dapConfigTable table = {0};
	table.kind = DAP_CONFIG_TABLE_NONE;
	table.config_idx = -1;
	char line[2048];
	while (fgets(line, sizeof(line), fp) != NULL) {
		size_t line_len = strlen(line);
		if (line_len == sizeof(line) - 1 && line[line_len - 1] != '\n') {
			fclose(fp);
			return 0;
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
				fclose(fp);
				return 0;
			}
			*close = '\0';
			char *table_name = editorConfigTrimLeft(trimmed + 1);
			editorConfigTrimRight(table_name);
			char *tail = editorConfigTrimLeft(close + 1);
			if (tail[0] != '\0' ||
			    !dapConfigParseTable(table_name, file_kind, &table)) {
				fclose(fp);
				return 0;
			}
			continue;
		}

		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			fclose(fp);
			return 0;
		}
		*eq = '\0';
		char *key = editorConfigTrimLeft(trimmed);
		editorConfigTrimRight(key);
		char *value = editorConfigTrimLeft(eq + 1);
		if (key[0] == '\0') {
			fclose(fp);
			return 0;
		}

		if (table.kind == DAP_CONFIG_TABLE_NONE) {
			continue;
		}

		if (table.kind == DAP_CONFIG_TABLE_ADAPTERS) {
			if (E.dap_adapter_count >= ROTIDE_DAP_MAX_ADAPTERS ||
			    !dapConfigIdValid(key)) {
				fclose(fp);
				return 0;
			}
			char command[PATH_MAX];
			if (!editorConfigParseQuotedValue(value, command, sizeof(command)) ||
			    command[0] == '\0') {
				fclose(fp);
				return 0;
			}
			struct editorDapAdapterConfig *adapter =
			        &E.dap_adapters[E.dap_adapter_count++];
			memset(adapter, 0, sizeof(*adapter));
			if (!dapConfigCopyString(adapter->id, sizeof(adapter->id), key) ||
			    !dapConfigCopyString(adapter->command, sizeof(adapter->command),
			                         command)) {
				fclose(fp);
				return 0;
			}
			continue;
		}

		struct editorDapLaunchConfig *configs =
		        table.kind == DAP_CONFIG_TABLE_DEFAULT ||
		                        table.kind == DAP_CONFIG_TABLE_DEFAULT_ENV
		                ? E.dap_defaults
		                : E.dap_launches;
		struct editorDapLaunchConfig *config = &configs[table.config_idx];
		if (table.kind == DAP_CONFIG_TABLE_DEFAULT_ENV ||
		    table.kind == DAP_CONFIG_TABLE_LAUNCH_ENV) {
			struct editorDapEnvVar *env = dapConfigEnsureEnv(config, key);
			char parsed[ROTIDE_DAP_VALUE_MAX];
			if (env == NULL ||
			    !editorConfigParseQuotedValue(value, parsed, sizeof(parsed)) ||
			    !dapConfigCopyString(env->value, sizeof(env->value), parsed)) {
				fclose(fp);
				return 0;
			}
			continue;
		}

		if (!dapConfigApplyLaunchSetting(config, key, value)) {
			fclose(fp);
			return 0;
		}
	}

	int failed = ferror(fp);
	fclose(fp);
	return !failed;
}

static int dapConfigAdapterIdExists(const char *id) {
	if (id == NULL || id[0] == '\0') {
		return 0;
	}
	for (int i = 0; i < E.dap_adapter_count; i++) {
		if (strcmp(E.dap_adapters[i].id, id) == 0) {
			return 1;
		}
	}
	return 0;
}

static int dapConfigLaunchesValid(const struct editorDapLaunchConfig *configs, int count) {
	for (int i = 0; i < count; i++) {
		const struct editorDapLaunchConfig *config = &configs[i];
		if (!dapConfigAdapterIdExists(config->adapter)) {
			return 0;
		}
		if (strcmp(config->request, "launch") != 0) {
			return 0;
		}
	}
	return 1;
}

void editorDapConfigInitDefaults(void) {
	memset(E.dap_adapters, 0, sizeof(E.dap_adapters));
	E.dap_adapter_count = 0;
	editorDapLaunchConfigsClear(E.dap_defaults, ROTIDE_DAP_MAX_CONFIGS);
	E.dap_default_count = 0;
	editorDapLaunchConfigsClear(E.dap_launches, ROTIDE_DAP_MAX_CONFIGS);
	E.dap_launch_count = 0;
	E.dap_project_config_exists = 0;
	E.dap_project_config_invalid = 0;
	E.dap_project_config_path[0] = '\0';
	E.dap_selected_launch = -1;
}

enum editorDapConfigLoadStatus editorDapConfigLoadFromPaths(const char *global_path,
                                                            const char *project_path) {
	editorDapConfigInitDefaults();
	enum editorDapConfigLoadStatus status = EDITOR_DAP_CONFIG_LOAD_OK;

	if (global_path != NULL) {
		int missing = 0;
		if (!dapConfigApplyFile(global_path, DAP_CONFIG_FILE_GLOBAL, &missing) ||
		    !dapConfigLaunchesValid(E.dap_defaults, E.dap_default_count)) {
			memset(E.dap_adapters, 0, sizeof(E.dap_adapters));
			E.dap_adapter_count = 0;
			editorDapLaunchConfigsClear(E.dap_defaults, ROTIDE_DAP_MAX_CONFIGS);
			E.dap_default_count = 0;
			status = (enum editorDapConfigLoadStatus)(
			        status | EDITOR_DAP_CONFIG_LOAD_INVALID_GLOBAL);
		}
	}

	if (project_path != NULL) {
		int missing = 0;
		if (!dapConfigApplyFile(project_path, DAP_CONFIG_FILE_PROJECT, &missing) ||
		    !dapConfigLaunchesValid(E.dap_launches, E.dap_launch_count)) {
			editorDapLaunchConfigsClear(E.dap_launches, ROTIDE_DAP_MAX_CONFIGS);
			E.dap_launch_count = 0;
			E.dap_project_config_invalid = 1;
			status = (enum editorDapConfigLoadStatus)(
			        status | EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT);
		}
		E.dap_project_config_exists = !missing;
		(void)dapConfigCopyString(E.dap_project_config_path,
		                          sizeof(E.dap_project_config_path), project_path);
	}

	return status;
}

enum editorDapConfigLoadStatus editorDapConfigLoadConfiguredGlobal(void) {
	char *global_path = editorConfigBuildGlobalConfigPath();
	if (global_path == NULL) {
		editorDapConfigInitDefaults();
		return EDITOR_DAP_CONFIG_LOAD_OUT_OF_MEMORY;
	}
	enum editorDapConfigLoadStatus status = editorDapConfigLoadFromPaths(global_path, NULL);
	free(global_path);
	return status;
}

int editorDapBuildProjectConfigPath(const char *project_root, char *buf, size_t bufsize) {
	if (buf == NULL || bufsize == 0) {
		return 0;
	}
	char *root = NULL;
	if (project_root != NULL && project_root[0] != '\0') {
		root = strdup(project_root);
	} else {
		root = editorPathGetCwd();
	}
	if (root == NULL) {
		return 0;
	}
	int written = snprintf(buf, bufsize, "%s/.rotide.toml", root);
	free(root);
	return written >= 0 && (size_t)written < bufsize;
}

enum editorDapConfigLoadStatus editorDapConfigReloadProject(const char *project_root) {
	char project_path[PATH_MAX];
	if (!editorDapBuildProjectConfigPath(project_root, project_path, sizeof(project_path))) {
		return EDITOR_DAP_CONFIG_LOAD_OUT_OF_MEMORY;
	}

	struct editorDapAdapterConfig *adapters = malloc(sizeof(E.dap_adapters));
	struct editorDapLaunchConfig *defaults = malloc(sizeof(E.dap_defaults));
	if (adapters == NULL || defaults == NULL) {
		free(adapters);
		free(defaults);
		return EDITOR_DAP_CONFIG_LOAD_OUT_OF_MEMORY;
	}
	int adapter_count = E.dap_adapter_count;
	int default_count = E.dap_default_count;
	memcpy(adapters, E.dap_adapters, sizeof(E.dap_adapters));
	memcpy(defaults, E.dap_defaults, sizeof(E.dap_defaults));

	editorDapLaunchConfigsClear(E.dap_launches, ROTIDE_DAP_MAX_CONFIGS);
	E.dap_launch_count = 0;
	E.dap_project_config_exists = 0;
	E.dap_project_config_invalid = 0;
	(void)dapConfigCopyString(E.dap_project_config_path, sizeof(E.dap_project_config_path),
	                          project_path);

	int missing = 0;
	enum editorDapConfigLoadStatus status = EDITOR_DAP_CONFIG_LOAD_OK;
	if (!dapConfigApplyFile(project_path, DAP_CONFIG_FILE_PROJECT, &missing)) {
		E.dap_project_config_invalid = 1;
		status = EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT;
	}
	E.dap_project_config_exists = !missing;
	memcpy(E.dap_adapters, adapters, sizeof(E.dap_adapters));
	memcpy(E.dap_defaults, defaults, sizeof(E.dap_defaults));
	E.dap_adapter_count = adapter_count;
	E.dap_default_count = default_count;
	free(adapters);
	free(defaults);
	if (status == EDITOR_DAP_CONFIG_LOAD_OK &&
	    !dapConfigLaunchesValid(E.dap_launches, E.dap_launch_count)) {
		editorDapLaunchConfigsClear(E.dap_launches, ROTIDE_DAP_MAX_CONFIGS);
		E.dap_launch_count = 0;
		E.dap_project_config_invalid = 1;
		status = EDITOR_DAP_CONFIG_LOAD_INVALID_PROJECT;
	}
	if (E.dap_selected_launch >= E.dap_launch_count) {
		E.dap_selected_launch = E.dap_launch_count > 0 ? 0 : -1;
	}
	return status;
}

const struct editorDapAdapterConfig *editorDapAdapterById(const char *id) {
	if (id == NULL || id[0] == '\0') {
		return NULL;
	}
	for (int i = 0; i < E.dap_adapter_count; i++) {
		if (strcmp(E.dap_adapters[i].id, id) == 0) {
			return &E.dap_adapters[i];
		}
	}
	return NULL;
}

static void dapConfigWriteTomlString(FILE *fp, const char *value) {
	fputc('"', fp);
	for (const char *p = value != NULL ? value : ""; *p != '\0'; p++) {
		if (*p == '"' || *p == '\\') {
			fputc('\\', fp);
		}
		fputc(*p, fp);
	}
	fputc('"', fp);
}

static void dapConfigWriteLaunchField(FILE *fp, const struct editorDapLaunchField *field) {
	fprintf(fp, "%s = ", field->key);
	switch (field->kind) {
		case EDITOR_DAP_LAUNCH_VALUE_STRING:
			dapConfigWriteTomlString(fp, field->string_value);
			break;
		case EDITOR_DAP_LAUNCH_VALUE_BOOL:
			fprintf(fp, "%s", field->bool_value ? "true" : "false");
			break;
		case EDITOR_DAP_LAUNCH_VALUE_INT:
			fprintf(fp, "%d", field->int_value);
			break;
		case EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY:
			fputc('[', fp);
			for (int i = 0; i < field->array_count; i++) {
				if (i > 0) {
					fprintf(fp, ", ");
				}
				dapConfigWriteTomlString(fp, field->array_values[i]);
			}
			fputc(']', fp);
			break;
	}
	fputc('\n', fp);
}

int editorDapCreateProjectLaunchFromDefault(int default_idx, const char *project_root) {
	if (default_idx < 0 || default_idx >= E.dap_default_count) {
		editorSetStatusMsg("Select a DAP default");
		return 0;
	}
	char project_path[PATH_MAX];
	if (!editorDapBuildProjectConfigPath(project_root, project_path, sizeof(project_path))) {
		editorSetAllocFailureStatus();
		return 0;
	}

	struct stat st;
	int exists = stat(project_path, &st) == 0;
	FILE *fp = fopen(project_path, "a");
	if (fp == NULL) {
		editorSetStatusMsg("Could not write .rotide.toml: %s", strerror(errno));
		return 0;
	}

	const struct editorDapLaunchConfig *config = &E.dap_defaults[default_idx];
	if (exists && st.st_size > 0) {
		fputc('\n', fp);
	}
	fprintf(fp, "[dap.launch.%s]\n", config->id);
	fprintf(fp, "name = ");
	dapConfigWriteTomlString(fp, config->name[0] != '\0' ? config->name : config->id);
	fputc('\n', fp);
	fprintf(fp, "adapter = ");
	dapConfigWriteTomlString(fp, config->adapter);
	fputc('\n', fp);
	fprintf(fp, "request = ");
	dapConfigWriteTomlString(fp, config->request[0] != '\0' ? config->request : "launch");
	fputc('\n', fp);
	for (int i = 0; i < config->field_count; i++) {
		dapConfigWriteLaunchField(fp, &config->fields[i]);
	}
	if (config->env_count > 0) {
		fprintf(fp, "\n[dap.launch.%s.env]\n", config->id);
		for (int i = 0; i < config->env_count; i++) {
			fprintf(fp, "%s = ", config->env[i].key);
			dapConfigWriteTomlString(fp, config->env[i].value);
			fputc('\n', fp);
		}
	}
	if (fclose(fp) != 0) {
		editorSetStatusMsg("Could not write .rotide.toml: %s", strerror(errno));
		return 0;
	}

	(void)editorDapConfigReloadProject(project_root);
	(void)editorTabOpenOrSwitchToFile(project_path);
	editorSetStatusMsg("Created DAP config in .rotide.toml");
	return 1;
}
