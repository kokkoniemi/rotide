#include "config/keymap.h"
#include "editor_test_api.h"
#include "input/input_system.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"

#include <string.h>

static int test_input_system_default_active_after_init(void) {
	ASSERT_TRUE(strcmp(editorInputSystemActiveId(), "cua") == 0);
	return 0;
}

static int test_input_system_lookup_and_activate_round_trip(void) {
	ASSERT_TRUE(editorInputSystemById("cua") == editorInputSystemActive());
	ASSERT_TRUE(editorInputSystemActivate("cua"));
	ASSERT_TRUE(strcmp(editorInputSystemActiveId(), "cua") == 0);
	return 0;
}

static int test_input_system_unknown_id_rejected(void) {
	const struct editorInputSystem *before = editorInputSystemActive();

	ASSERT_TRUE(!editorInputSystemActivate("missing"));
	ASSERT_TRUE(editorInputSystemActive() == before);
	ASSERT_TRUE(strcmp(editorInputSystemActiveId(), "cua") == 0);
	return 0;
}

static int test_input_system_cua_resolves_existing_action_names(void) {
	const struct editorInputSystem *system = editorInputSystemById("cua");
	int command_id = -1;

	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->resolve_command != NULL);
	ASSERT_TRUE(system->resolve_command("save", &command_id));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, command_id);
	ASSERT_TRUE(!system->resolve_command("vim_only_for_now", &command_id));
	return 0;
}

static int test_input_system_cua_bind_key_uses_keymap(void) {
	const struct editorInputSystem *system = editorInputSystemById("cua");
	enum editorAction action = EDITOR_ACTION_COUNT;

	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->bind_key != NULL);
	ASSERT_TRUE(system->bind_key(NULL, "save", CTRL_KEY('u')));
	ASSERT_TRUE(editorKeymapLookupAction(&E.keymap, CTRL_KEY('u'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!system->bind_key("normal", "save", CTRL_KEY('s')));
	return 0;
}

const struct editorTestCase g_input_system_tests[] = {
        {"input_system_default_active_after_init", test_input_system_default_active_after_init},
        {"input_system_lookup_and_activate_round_trip",
         test_input_system_lookup_and_activate_round_trip},
        {"input_system_unknown_id_rejected", test_input_system_unknown_id_rejected},
        {"input_system_cua_resolves_existing_action_names",
         test_input_system_cua_resolves_existing_action_names},
        {"input_system_cua_bind_key_uses_keymap", test_input_system_cua_bind_key_uses_keymap},
};

const int g_input_system_test_count =
        (int)(sizeof(g_input_system_tests) / sizeof(g_input_system_tests[0]));
