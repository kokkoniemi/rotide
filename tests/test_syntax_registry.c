#include "test_case.h"
#include "test_helpers.h"

#include <string.h>

#include "language/languages.h"
#include "language/syntax.h"
#include "rotide.h"
#include "tree_sitter/api.h"

static int test_editor_syntax_registry_has_entries(void) {
	int count = editorSyntaxLanguageDefCount();
	ASSERT_TRUE(count > 0);
	ASSERT_TRUE(count <= EDITOR_SYNTAX_LANGUAGE_COUNT);
	return 0;
}

static int test_editor_syntax_registry_factories_succeed(void) {
	int count = editorSyntaxLanguageDefCount();
	for (int i = 0; i < count; i++) {
		const struct editorSyntaxLanguageDef *def = editorSyntaxLanguageDefAt(i);
		ASSERT_TRUE(def != NULL);
		ASSERT_TRUE(def->id != EDITOR_SYNTAX_NONE);
		ASSERT_TRUE((int)def->id > 0 && (int)def->id < EDITOR_SYNTAX_LANGUAGE_COUNT);
		ASSERT_TRUE(def->name != NULL && def->name[0] != '\0');
		ASSERT_TRUE(def->ts_factory != NULL);

		const TSLanguage *ts_language = def->ts_factory();
		ASSERT_TRUE(ts_language != NULL);

		TSParser *parser = ts_parser_new();
		ASSERT_TRUE(parser != NULL);
		bool set_ok = ts_parser_set_language(parser, ts_language);
		ts_parser_delete(parser);
		ASSERT_TRUE(set_ok);
	}
	return 0;
}

static int test_editor_syntax_registry_lookup_by_id_round_trips(void) {
	int count = editorSyntaxLanguageDefCount();
	for (int i = 0; i < count; i++) {
		const struct editorSyntaxLanguageDef *def = editorSyntaxLanguageDefAt(i);
		ASSERT_TRUE(def != NULL);
		const struct editorSyntaxLanguageDef *looked_up =
				editorSyntaxLookupLanguage(def->id);
		ASSERT_TRUE(looked_up == def);
	}
	ASSERT_TRUE(editorSyntaxLookupLanguage(EDITOR_SYNTAX_NONE) == NULL);
	return 0;
}

static int test_editor_syntax_registry_compiles_every_query(void) {
	editorSyntaxReleaseSharedResources();
	editorSyntaxTestResetLastQueryCompileError();

	int count = editorSyntaxLanguageDefCount();
	for (int i = 0; i < count; i++) {
		const struct editorSyntaxLanguageDef *def = editorSyntaxLanguageDefAt(i);
		ASSERT_TRUE(def != NULL);
		if (def->highlight_parts == NULL) {
			continue;
		}
		ASSERT_TRUE(def->highlight_part_count > 0);
		struct editorSyntaxState *state = editorSyntaxStateCreate(def->id);
		ASSERT_TRUE(state != NULL);

		const char *probe = "";
		struct editorTextSource source = {0};
		editorTextSourceInitString(&source, probe, 0);
		(void)editorSyntaxStateParseFull(state, &source);

		struct editorSyntaxCapture captures[4];
		int capture_count = 0;
		(void)editorSyntaxStateCollectCapturesForRange(state, &source, 0, 0,
				captures, (int)(sizeof(captures) / sizeof(captures[0])),
				&capture_count);

		struct editorSyntaxQueryCompileError err = {0};
		ASSERT_TRUE(!editorSyntaxCopyLastQueryCompileError(&err));

		editorSyntaxStateDestroy(state);
	}
	return 0;
}

static int test_editor_syntax_registry_compiles_every_locals_query(void) {
	editorSyntaxReleaseSharedResources();
	editorSyntaxTestResetLastQueryCompileError();

	int count = editorSyntaxLanguageDefCount();
	int languages_with_locals = 0;
	for (int i = 0; i < count; i++) {
		const struct editorSyntaxLanguageDef *def = editorSyntaxLanguageDefAt(i);
		ASSERT_TRUE(def != NULL);
		if (def->locals_parts == NULL) {
			continue;
		}
		ASSERT_TRUE(def->locals_part_count > 0);
		languages_with_locals++;
		struct editorSyntaxState *state = editorSyntaxStateCreate(def->id);
		ASSERT_TRUE(state != NULL);

		const char *probe = "a";
		struct editorTextSource source = {0};
		editorTextSourceInitString(&source, probe, 1);
		(void)editorSyntaxStateParseFull(state, &source);

		struct editorSyntaxCapture captures[4];
		int capture_count = 0;
		(void)editorSyntaxStateCollectCapturesForRange(state, &source, 0, 1,
				captures, (int)(sizeof(captures) / sizeof(captures[0])),
				&capture_count);

		struct editorSyntaxQueryCompileError err = {0};
		ASSERT_TRUE(!editorSyntaxCopyLastQueryCompileError(&err));

		editorSyntaxStateDestroy(state);
	}
	ASSERT_TRUE(languages_with_locals >= 2);
	return 0;
}

static int test_editor_syntax_registry_lookup_by_extension(void) {
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByExtension(".c");
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, (int)def->id);

	def = editorSyntaxLookupLanguageByExtension(".tsx");
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_TYPESCRIPT, (int)def->id);

	ASSERT_TRUE(editorSyntaxLookupLanguageByExtension(".bogus") == NULL);
	return 0;
}

static int test_editor_syntax_registry_lookup_by_basename(void) {
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByBasename("Rakefile");
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUBY, (int)def->id);

	def = editorSyntaxLookupLanguageByBasename("go.mod");
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, (int)def->id);

	ASSERT_TRUE(editorSyntaxLookupLanguageByBasename("noresult") == NULL);
	return 0;
}

static int test_editor_syntax_registry_lookup_by_shebang(void) {
	const char *python3 = "python3";
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByShebangToken(python3, strlen(python3));
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, (int)def->id);

	const char *bash = "bash";
	def = editorSyntaxLookupLanguageByShebangToken(bash, strlen(bash));
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, (int)def->id);

	const char *unknown = "perl";
	ASSERT_TRUE(editorSyntaxLookupLanguageByShebangToken(unknown, strlen(unknown)) == NULL);
	return 0;
}

static int test_editor_syntax_registry_lookup_by_injection_name(void) {
	const char *jsx = "jsx";
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByInjectionName(jsx, strlen(jsx));
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, (int)def->id);

	const char *aeson = "aesonQQ";
	def = editorSyntaxLookupLanguageByInjectionName(aeson, strlen(aeson));
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JSON, (int)def->id);

	const char *quoted = "\"hamlet\"";
	def = editorSyntaxLookupLanguageByInjectionName(quoted, strlen(quoted));
	ASSERT_TRUE(def != NULL);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, (int)def->id);

	const char *unknown = "klingon";
	ASSERT_TRUE(editorSyntaxLookupLanguageByInjectionName(unknown, strlen(unknown)) == NULL);
	return 0;
}

const struct editorTestCase g_syntax_registry_tests[] = {
	{"editor_syntax_registry_has_entries", test_editor_syntax_registry_has_entries},
	{"editor_syntax_registry_factories_succeed", test_editor_syntax_registry_factories_succeed},
	{"editor_syntax_registry_lookup_by_id_round_trips", test_editor_syntax_registry_lookup_by_id_round_trips},
	{"editor_syntax_registry_compiles_every_query", test_editor_syntax_registry_compiles_every_query},
	{"editor_syntax_registry_compiles_every_locals_query", test_editor_syntax_registry_compiles_every_locals_query},
	{"editor_syntax_registry_lookup_by_extension", test_editor_syntax_registry_lookup_by_extension},
	{"editor_syntax_registry_lookup_by_basename", test_editor_syntax_registry_lookup_by_basename},
	{"editor_syntax_registry_lookup_by_shebang", test_editor_syntax_registry_lookup_by_shebang},
	{"editor_syntax_registry_lookup_by_injection_name", test_editor_syntax_registry_lookup_by_injection_name},
};

const int g_syntax_registry_test_count =
		(int)(sizeof(g_syntax_registry_tests) / sizeof(g_syntax_registry_tests[0]));
