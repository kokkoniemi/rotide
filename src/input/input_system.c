#include "input/input_system.h"

#include <string.h>

static const struct editorInputSystem *const g_inputSystem_systems[] = {
        &editorCuaInputSystem,
        &editorVimInputSystem,
};

static const struct editorInputSystem *g_inputSystem_active = &editorVimInputSystem;

const struct editorInputSystem *editorInputSystemById(const char *id) {
	if (id == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(g_inputSystem_systems) / sizeof(g_inputSystem_systems[0]);
	     i++) {
		const struct editorInputSystem *system = g_inputSystem_systems[i];
		if (system != NULL && system->id != NULL && strcmp(system->id, id) == 0) {
			return system;
		}
	}
	return NULL;
}

const struct editorInputSystem *editorInputSystemActive(void) {
	return g_inputSystem_active;
}

int editorInputSystemActivate(const char *id) {
	const struct editorInputSystem *system = editorInputSystemById(id);
	if (system == NULL) {
		return 0;
	}
	if (system == g_inputSystem_active) {
		return 1;
	}
	if (g_inputSystem_active != NULL && g_inputSystem_active->on_deactivate != NULL) {
		g_inputSystem_active->on_deactivate();
	}
	if (system->on_activate != NULL && !system->on_activate()) {
		return 0;
	}
	g_inputSystem_active = system;
	return 1;
}

const char *editorInputSystemActiveId(void) {
	if (g_inputSystem_active == NULL) {
		return NULL;
	}
	return g_inputSystem_active->id;
}
