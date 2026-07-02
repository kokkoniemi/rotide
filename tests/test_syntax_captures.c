#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

struct expectedSyntaxCapture {
	uint32_t start_byte;
	uint32_t end_byte;
	enum editorSyntaxHighlightClass highlight_class;
};

static uint64_t syntax_capture_digest(const struct editorSyntaxCapture *captures, int count) {
	uint64_t digest = UINT64_C(1469598103934665603);
	for (int i = 0; i < count; i++) {
		digest ^= captures[i].start_byte;
		digest *= UINT64_C(1099511628211);
		digest ^= captures[i].end_byte;
		digest *= UINT64_C(1099511628211);
		digest ^= (uint64_t)captures[i].highlight_class;
		digest *= UINT64_C(1099511628211);
	}
	return digest;
}

static int assert_syntax_capture_digest(enum editorSyntaxLanguage language, const char *source,
                                        int expected_count, uint64_t expected_digest) {
	struct editorTextSource source_view = {0};
	size_t source_len = strlen(source);
	if (source_len > UINT32_MAX) {
		return 1;
	}
	editorTextSourceInitString(&source_view, source, source_len);

	struct editorSyntaxState *state = editorSyntaxStateCreate(language);
	if (state == NULL || !editorSyntaxStateParseFull(state, &source_view)) {
		editorSyntaxStateDestroy(state);
		return 1;
	}

	struct editorSyntaxCapture captures[512];
	int count = 0;
	int ok = editorSyntaxStateCollectCapturesForRange(
	        state, &source_view, 0, (uint32_t)source_len, captures,
	        (int)(sizeof(captures) / sizeof(captures[0])), &count);
	uint64_t digest = syntax_capture_digest(captures, count);
	editorSyntaxStateDestroy(state);
	if (!ok || count != expected_count || digest != expected_digest) {
		(void)fprintf(stderr,
		              "capture digest: expected count=%d hash=%016llx, got count=%d "
		              "hash=%016llx\n",
		              expected_count, (unsigned long long)expected_digest, count,
		              (unsigned long long)digest);
		return 1;
	}
	return 0;
}

static int assert_syntax_capture_contract(enum editorSyntaxLanguage language, const char *source,
                                          const struct expectedSyntaxCapture *expected,
                                          int expected_count) {
	struct editorTextSource source_view = {0};
	size_t source_len = strlen(source);
	if (source_len > UINT32_MAX) {
		return 1;
	}
	editorTextSourceInitString(&source_view, source, source_len);

	struct editorSyntaxState *state = editorSyntaxStateCreate(language);
	if (state == NULL || !editorSyntaxStateParseFull(state, &source_view)) {
		editorSyntaxStateDestroy(state);
		return 1;
	}

	struct editorSyntaxCapture actual[128];
	int actual_count = 0;
	int ok = editorSyntaxStateCollectCapturesForRange(
	        state, &source_view, 0, (uint32_t)source_len, actual,
	        (int)(sizeof(actual) / sizeof(actual[0])), &actual_count);
	if (!ok || actual_count != expected_count) {
		(void)fprintf(stderr, "capture count: expected %d, got %d\n", expected_count,
		              actual_count);
		for (int i = 0; i < actual_count; i++) {
			(void)fprintf(stderr, "  {%u, %u, %d}\n", actual[i].start_byte,
			              actual[i].end_byte, actual[i].highlight_class);
		}
		editorSyntaxStateDestroy(state);
		return 1;
	}

	for (int i = 0; i < expected_count; i++) {
		if (actual[i].start_byte != expected[i].start_byte ||
		    actual[i].end_byte != expected[i].end_byte ||
		    actual[i].highlight_class != expected[i].highlight_class) {
			(void)fprintf(stderr,
			              "capture %d: expected {%u, %u, %d}, got {%u, %u, %d}\n", i,
			              expected[i].start_byte, expected[i].end_byte,
			              expected[i].highlight_class, actual[i].start_byte,
			              actual[i].end_byte, actual[i].highlight_class);
			editorSyntaxStateDestroy(state);
			return 1;
		}
	}

	editorSyntaxStateDestroy(state);
	return 0;
}

static char *build_dense_c_expression_source(int terms, size_t *len_out) {
	if (terms <= 0 || len_out == NULL) {
		return NULL;
	}

	const char *prefix = "int value = ";
	const char *suffix = ";\n";
	size_t cap = strlen(prefix) + strlen(suffix) + (size_t)terms * 16 + 1;
	char *source = malloc(cap);
	if (source == NULL) {
		return NULL;
	}

	size_t used = 0;
	int written = snprintf(source + used, cap - used, "%s", prefix);
	if (written < 0 || (size_t)written >= cap - used) {
		free(source);
		return NULL;
	}
	used += (size_t)written;
	for (int i = 0; i < terms; i++) {
		written = snprintf(source + used, cap - used, "%s%d", i == 0 ? "" : " + ", i);
		if (written < 0 || (size_t)written >= cap - used) {
			free(source);
			return NULL;
		}
		used += (size_t)written;
	}
	written = snprintf(source + used, cap - used, "%s", suffix);
	if (written < 0 || (size_t)written >= cap - used) {
		free(source);
		return NULL;
	}
	used += (size_t)written;
	*len_out = used;
	return source;
}

static int test_editor_syntax_latex_capture_contract(void) {
	size_t source_len = 0;
	char *source =
	        read_file_contents("tests/syntax/supported/latex/highlight.tex", &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len == strlen(source));
	const struct expectedSyntaxCapture expected[] = {
	        {0, 9, EDITOR_SYNTAX_HL_COMMENT},      {10, 24, EDITOR_SYNTAX_HL_KEYWORD},
	        {24, 33, EDITOR_SYNTAX_HL_STRING},     {25, 32, EDITOR_SYNTAX_HL_STRING},
	        {34, 45, EDITOR_SYNTAX_HL_KEYWORD},    {45, 54, EDITOR_SYNTAX_HL_STRING},
	        {46, 53, EDITOR_SYNTAX_HL_STRING},     {55, 63, EDITOR_SYNTAX_HL_KEYWORD},
	        {71, 77, EDITOR_SYNTAX_HL_FUNCTION},   {77, 88, EDITOR_SYNTAX_HL_CONSTANT},
	        {78, 87, EDITOR_SYNTAX_HL_CONSTANT},   {93, 97, EDITOR_SYNTAX_HL_FUNCTION},
	        {97, 108, EDITOR_SYNTAX_HL_CONSTANT},  {98, 107, EDITOR_SYNTAX_HL_CONSTANT},
	        {113, 118, EDITOR_SYNTAX_HL_FUNCTION}, {118, 127, EDITOR_SYNTAX_HL_CONSTANT},
	        {129, 135, EDITOR_SYNTAX_HL_KEYWORD},  {135, 145, EDITOR_SYNTAX_HL_TYPE},
	        {154, 155, EDITOR_SYNTAX_HL_OPERATOR}, {155, 156, EDITOR_SYNTAX_HL_VARIABLE},
	        {157, 161, EDITOR_SYNTAX_HL_KEYWORD},  {161, 171, EDITOR_SYNTAX_HL_TYPE},
	};

	int result = assert_syntax_capture_contract(EDITOR_SYNTAX_LATEX, source, expected,
	                                            (int)(sizeof(expected) / sizeof(expected[0])));
	free(source);
	ASSERT_EQ_INT(0, result);

	source = read_file_contents("tests/syntax/supported/latex/incomplete.tex", &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len == strlen(source));
	const struct expectedSyntaxCapture incomplete_expected[] = {
	        {0, 12, EDITOR_SYNTAX_HL_COMMENT},
	        {38, 39, EDITOR_SYNTAX_HL_OPERATOR},
	        {40, 41, EDITOR_SYNTAX_HL_OPERATOR},
	        {41, 42, EDITOR_SYNTAX_HL_VARIABLE},
	};
	result = assert_syntax_capture_contract(
	        EDITOR_SYNTAX_LATEX, source, incomplete_expected,
	        (int)(sizeof(incomplete_expected) / sizeof(incomplete_expected[0])));
	free(source);
	ASSERT_EQ_INT(0, result);

	source = read_file_contents("tests/syntax/supported/latex/contract.tex", &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len == strlen(source));
	const struct expectedSyntaxCapture contract_expected[] = {
	        {0, 5, EDITOR_SYNTAX_HL_KEYWORD},      {16, 24, EDITOR_SYNTAX_HL_KEYWORD},
	        {37, 48, EDITOR_SYNTAX_HL_KEYWORD},    {58, 72, EDITOR_SYNTAX_HL_KEYWORD},
	        {80, 90, EDITOR_SYNTAX_HL_KEYWORD},    {123, 140, EDITOR_SYNTAX_HL_STRING},
	        {156, 160, EDITOR_SYNTAX_HL_STRING},   {161, 166, EDITOR_SYNTAX_HL_STRING},
	        {176, 185, EDITOR_SYNTAX_HL_STRING},   {187, 194, EDITOR_SYNTAX_HL_STRING},
	        {202, 221, EDITOR_SYNTAX_HL_STRING},   {247, 249, EDITOR_SYNTAX_HL_NUMBER},
	        {251, 260, EDITOR_SYNTAX_HL_FUNCTION}, {271, 290, EDITOR_SYNTAX_HL_CONSTANT},
	        {291, 305, EDITOR_SYNTAX_HL_FUNCTION}, {305, 316, EDITOR_SYNTAX_HL_CONSTANT},
	        {306, 315, EDITOR_SYNTAX_HL_CONSTANT}, {316, 326, EDITOR_SYNTAX_HL_CONSTANT},
	        {317, 325, EDITOR_SYNTAX_HL_CONSTANT}, {327, 332, EDITOR_SYNTAX_HL_FUNCTION},
	        {350, 351, EDITOR_SYNTAX_HL_OPERATOR}, {352, 353, EDITOR_SYNTAX_HL_OPERATOR},
	        {353, 354, EDITOR_SYNTAX_HL_VARIABLE}, {354, 355, EDITOR_SYNTAX_HL_OPERATOR},
	        {355, 356, EDITOR_SYNTAX_HL_VARIABLE}, {356, 357, EDITOR_SYNTAX_HL_OPERATOR},
	        {363, 379, EDITOR_SYNTAX_HL_OPERATOR},
	};
	result = assert_syntax_capture_contract(
	        EDITOR_SYNTAX_LATEX, source, contract_expected,
	        (int)(sizeof(contract_expected) / sizeof(contract_expected[0])));
	free(source);
	ASSERT_EQ_INT(0, result);
	return 0;
}

static int test_editor_syntax_csharp_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/csharp/highlight.cs", 16, UINT64_C(0x031420054c17a1a3)},
	        {"tests/syntax/supported/csharp/contract.cs", 134, UINT64_C(0x68040afd67d15581)},
	        {"tests/syntax/supported/csharp/incomplete.cs", 9, UINT64_C(0x7670410eec51c76f)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_CSHARP, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_bash_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/bash/highlight.sh", 15, UINT64_C(0x024863e33145cfa3)},
	        {"tests/syntax/supported/bash/contract.sh", 106, UINT64_C(0xc37e39637070ebd3)},
	        {"tests/syntax/supported/bash/incomplete.sh", 5, UINT64_C(0x4df0a55a4342f4bc)},
	        {"tests/syntax/supported/bash/incomplete_heredoc.sh", 4,
	         UINT64_C(0xcad64e051c9653df)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_SHELL, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_rust_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/rust/highlight.rs", 11, UINT64_C(0x63b72cba09d7bc1e)},
	        {"tests/syntax/supported/rust/contract.rs", 263, UINT64_C(0x4fd9284e3655394c)},
	        {"tests/syntax/supported/rust/incomplete.rs", 22, UINT64_C(0x45bdecc487ba6fe8)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_RUST, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_php_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/php/highlight.php", 7, UINT64_C(0x2b18dafb41aaaece)},
	        {"tests/syntax/supported/php/contract.php", 126, UINT64_C(0xe24f76d9cbbb138b)},
	        {"tests/syntax/supported/php/incomplete.php", 5, UINT64_C(0x89ad798c15bc4147)},
	        {"tests/syntax/supported/php/incomplete_heredoc.php", 7,
	         UINT64_C(0x7d46f8f62ea83e9c)},
	        {"tests/syntax/supported/php/injections.php", 17, UINT64_C(0x38ab528b451ffc1c)},
	        {"tests/syntax/supported/php/injections_nowdoc.php", 15,
	         UINT64_C(0xc9ee70243c5d6c5c)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_PHP, source, cases[i].count,
		                                          cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_typescript_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/typescript/highlight.ts", 14,
	         UINT64_C(0x30742120ea77b1d1)},
	        {"tests/syntax/supported/typescript/contract.ts", 254,
	         UINT64_C(0x59d5198804e3967b)},
	        {"tests/syntax/supported/typescript/injections.ts", 52,
	         UINT64_C(0xbdb5dc0a99859efc)},
	        {"tests/syntax/supported/typescript/incomplete.ts", 19,
	         UINT64_C(0xf678890b67145ff4)},
	        {"tests/syntax/supported/typescript/jsdoc.ts", 25, UINT64_C(0x747be4fe22e2dd3a)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_TYPESCRIPT, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_tsx_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/tsx/highlight.tsx", 83, UINT64_C(0x644bd99932e13279)},
	        {"tests/syntax/supported/tsx/contract.tsx", 131, UINT64_C(0xe2b9393382bfeaae)},
	        {"tests/syntax/supported/tsx/incomplete.tsx", 22, UINT64_C(0x012bffd4582b89a8)},
	        {"tests/syntax/supported/tsx/jsdoc.tsx", 42, UINT64_C(0x301b8694dc0867c4)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_TSX, source, cases[i].count,
		                                          cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_scala_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/scala/highlight.scala", 5, UINT64_C(0x174f0c59d7fdae03)},
	        {"tests/syntax/supported/scala/contract.scala", 106, UINT64_C(0xaf8327b1a7136aea)},
	        {"tests/syntax/supported/scala/incomplete.scala", 5, UINT64_C(0x8033c4ebab4d4af5)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_SCALA, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_ocaml_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/ocaml/highlight.ml", 5, UINT64_C(0xe85d212258641a5f)},
	        {"tests/syntax/supported/ocaml/contract.ml", 89, UINT64_C(0x441a6f1521040203)},
	        {"tests/syntax/supported/ocaml/incomplete.ml", 11, UINT64_C(0x71fe69c680e45d97)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_OCAML, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_ruby_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/ruby/highlight.rb", 7, UINT64_C(0x960f0d0bddba30bf)},
	        {"tests/syntax/supported/ruby/contract.rb", 157, UINT64_C(0x2ecd54afd1bb3392)},
	        {"tests/syntax/supported/ruby/incomplete.rb", 11, UINT64_C(0x767be8b5415e2bef)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_RUBY, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_julia_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/julia/highlight.jl", 8, UINT64_C(0x723201961f1aaf6c)},
	        {"tests/syntax/supported/julia/contract.jl", 112, UINT64_C(0x2a1f00793d8efba5)},
	        {"tests/syntax/supported/julia/incomplete.jl", 13, UINT64_C(0x401667f1e49d89e1)},
	        {"tests/syntax/supported/julia/injections.jl", 22, UINT64_C(0x3a5aebae41096873)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_JULIA, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_haskell_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/haskell/highlight.hs", 5, UINT64_C(0x7642df1950c37a7b)},
	        {"tests/syntax/supported/haskell/contract.hs", 71, UINT64_C(0xe373197bc3d137b7)},
	        {"tests/syntax/supported/haskell/incomplete.hs", 2, UINT64_C(0x973da76b2f693144)},
	        {"tests/syntax/supported/haskell/injections.hs", 32, UINT64_C(0xecd55c7e7e34e560)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_HASKELL, source,
		                                          cases[i].count, cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_cpp_capture_contract(void) {
	static const struct {
		const char *path;
		int count;
		uint64_t digest;
	} cases[] = {
	        {"tests/syntax/supported/cpp/highlight.cpp", 19, UINT64_C(0xc11fcb91b55faeee)},
	        {"tests/syntax/supported/cpp/contract.cpp", 138, UINT64_C(0xcad26b41eaddcaaa)},
	        {"tests/syntax/supported/cpp/incomplete.cpp", 5, UINT64_C(0xea35068451de7aed)},
	        {"tests/syntax/supported/cpp/incomplete_raw.cpp", 5, UINT64_C(0xea35068451de7aed)},
	        {"tests/syntax/supported/cpp/injections.cpp", 16, UINT64_C(0x25e932e7ba654c0e)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t source_len = 0;
		char *source = read_file_contents(cases[i].path, &source_len);
		ASSERT_TRUE(source != NULL);
		ASSERT_TRUE(source_len == strlen(source));
		int result = assert_syntax_capture_digest(EDITOR_SYNTAX_CPP, source, cases[i].count,
		                                          cases[i].digest);
		free(source);
		ASSERT_EQ_INT(0, result);
	}
	return 0;
}

static int test_editor_syntax_query_budget_match_limit_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("const value = document + window;\n", 512, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 1, 0, 2000000000ULL);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	(void)editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget);

	struct editorSyntaxCapture captures[1024];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(
	        state, &source_view, 0, (uint32_t)source_len, captures,
	        (int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(0, parse_budget);
	ASSERT_EQ_INT(1, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_query_compile_failure_records_diagnostics(void) {
	const char *broken_query = "((identifier) @name";
	editorSyntaxTestResetLastQueryCompileError();

	ASSERT_EQ_INT(0, editorSyntaxTestCompileQueryForDiagnostics(EDITOR_SYNTAX_C, broken_query));

	struct editorSyntaxQueryCompileError error = {0};
	ASSERT_TRUE(editorSyntaxCopyLastQueryCompileError(&error));
	ASSERT_EQ_INT(1, error.has_error);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, error.language);
	ASSERT_TRUE(error.error_type != 0);
	ASSERT_TRUE(error.error_offset <= strlen(broken_query));
	ASSERT_TRUE(strstr(error.context, "identifier") != NULL);

	struct editorSyntaxQueryCompileError drained = {0};
	ASSERT_TRUE(editorSyntaxDrainLastQueryCompileError(&drained));
	ASSERT_EQ_INT(error.language, drained.language);
	ASSERT_EQ_INT(error.error_type, drained.error_type);
	ASSERT_EQ_INT(error.error_offset, drained.error_offset);
	ASSERT_EQ_STR(error.context, drained.context);
	ASSERT_TRUE(!editorSyntaxDrainLastQueryCompileError(NULL));

	editorSyntaxTestResetLastQueryCompileError();
	return 0;
}

static int test_editor_syntax_capture_rules_are_longest_match_first(void) {
	int rule_count = editorSyntaxTestCaptureRuleCount();
	ASSERT_TRUE(rule_count > 0);

	size_t previous_len = (size_t)-1;
	for (int i = 0; i < rule_count; i++) {
		const char *prefix = NULL;
		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		ASSERT_TRUE(editorSyntaxTestCaptureRuleAt(i, &prefix, &highlight_class));
		ASSERT_TRUE(prefix != NULL);
		ASSERT_TRUE(prefix[0] != '\0');
		size_t prefix_len = strlen(prefix);
		ASSERT_TRUE(prefix_len <= previous_len);
		ASSERT_TRUE(highlight_class > EDITOR_SYNTAX_HL_NONE);
		ASSERT_TRUE(highlight_class < EDITOR_SYNTAX_HL_CLASS_COUNT);
		ASSERT_EQ_INT(highlight_class, editorSyntaxTestClassFromCaptureName(prefix));

		char nested_capture[128];
		int written = snprintf(nested_capture, sizeof(nested_capture), "%s.extra", prefix);
		ASSERT_TRUE(written > 0);
		ASSERT_TRUE((size_t)written < sizeof(nested_capture));
		ASSERT_EQ_INT(highlight_class,
		              editorSyntaxTestClassFromCaptureName(nested_capture));

		previous_len = prefix_len;
	}

	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_PARAMETER,
	              editorSyntaxTestClassFromCaptureName("variable.parameter"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_PROPERTY,
	              editorSyntaxTestClassFromCaptureName("variable.member"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_VARIABLE, editorSyntaxTestClassFromCaptureName("variable"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_MODULE, editorSyntaxTestClassFromCaptureName("namespace"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_PROPERTY, editorSyntaxTestClassFromCaptureName("property"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_CONSTANT, editorSyntaxTestClassFromCaptureName("constant"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_NONE, editorSyntaxTestClassFromCaptureName("none"));
	return 0;
}

static int test_editor_syntax_collect_c_captures_without_injection_query_is_graceful(void) {
	const char *source = "int value = 1;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxCapture captures[32];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(
	        state, &source_view, 0, (uint32_t)strlen(source), captures,
	        (int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(capture_count > 0);
	ASSERT_TRUE(!editorSyntaxStateConsumeQueryUnavailableEvent(state, NULL, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_capture_truncation_reports_event_and_keeps_span_limit(void) {
	size_t source_len = 0;
	char *source = build_dense_c_expression_source(300, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);

	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), source));
	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorRowSyntaxSpan spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(0, spans, (int)(sizeof(spans) / sizeof(spans[0])),
	                                       &span_count));
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, span_count);

	for (int i = 0; i < span_count; i++) {
		ASSERT_TRUE(spans[i].end_render_idx > spans[i].start_render_idx);
		ASSERT_TRUE(spans[i].highlight_class != EDITOR_SYNTAX_HL_NONE);
	}

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(E.syntax_state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, event.detail);

	E.window_rows = 4;
	E.window_cols = 120;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter syntax spans truncated") != NULL);

	span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(0, spans, (int)(sizeof(spans) / sizeof(spans[0])),
	                                       &span_count));
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, span_count);

	ASSERT_TRUE(unlink(path) == 0);
	free(source);
	return 0;
}

static int test_editor_syntax_parse_tree_errors_report_event_and_status(void) {
	const char *broken_source = "int main( {\n";
	struct editorTextSource broken_view = {0};
	editorTextSourceInitString(&broken_view, broken_source, strlen(broken_source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &broken_view));

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(1, event.detail);
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));

	const char *valid_source = "int main(void) { return 0; }\n";
	struct editorTextSource valid_view = {0};
	editorTextSourceInitString(&valid_view, valid_source, strlen(valid_source));
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &valid_view));
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));

	ASSERT_TRUE(editorSyntaxStateParseFull(state, &broken_view));
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR, event.kind);
	editorSyntaxStateDestroy(state);

	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), broken_source));
	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter parse tree has errors") != NULL);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_injection_depth_limit_reports_event(void) {
	editorSyntaxTestSetMaxInjectionDepth(3);

	const char *source = "const nested = cpp`const char *page = "
	                     "R\"html(<script>const tooDeep = /abc/;</script>)html\";`;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_REGEX, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(4, event.detail);
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));
	editorSyntaxStateDestroy(state);

	char path[] = "/tmp/rotide-test-syntax-injection-depth-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(
	        path, 3, "tests/syntax/supported/javascript/injection_depth.js"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter injection depth limit reached") != NULL);

	ASSERT_TRUE(unlink(path) == 0);
	editorSyntaxTestResetMaxInjectionDepth();
	return 0;
}

static int test_editor_syntax_injection_slot_limit_reports_event(void) {
	const char *source = "const a0 = c`x`;\n"
	                     "const a1 = cpp`x`;\n"
	                     "const a2 = go`x`;\n"
	                     "const a3 = bash`x`;\n"
	                     "const a4 = html`x`;\n"
	                     "const a5 = javascript`x`;\n"
	                     "const a6 = jsdoc`x`;\n"
	                     "const a7 = typescript`x`;\n"
	                     "const a8 = css`x`;\n"
	                     "const a9 = json`x`;\n"
	                     "const a10 = python`x`;\n"
	                     "const a11 = php`x`;\n"
	                     "const a12 = rust`x`;\n"
	                     "const a13 = java`x`;\n"
	                     "const a14 = regex`x`;\n"
	                     "const a15 = csharp`x`;\n"
	                     "const a16 = haskell`x`;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HASKELL, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(16, event.detail);
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_unknown_injection_target_is_graceful(void) {
	const char *source = "const value = notalanguage`body`;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));
	ASSERT_TRUE(!editorSyntaxStateConsumeQueryUnavailableEvent(state, NULL, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_parse_budget_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("function item(){ return 1; }\n", 120000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 2000000000ULL, 1);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(1, parse_budget);
	ASSERT_EQ_INT(0, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

const struct editorTestCase g_syntax_captures_tests[] = {
        {"editor_syntax_bash_capture_contract", test_editor_syntax_bash_capture_contract},
        {"editor_syntax_rust_capture_contract", test_editor_syntax_rust_capture_contract},
        {"editor_syntax_php_capture_contract", test_editor_syntax_php_capture_contract},
        {"editor_syntax_typescript_capture_contract",
         test_editor_syntax_typescript_capture_contract},
        {"editor_syntax_tsx_capture_contract", test_editor_syntax_tsx_capture_contract},
        {"editor_syntax_csharp_capture_contract", test_editor_syntax_csharp_capture_contract},
        {"editor_syntax_ocaml_capture_contract", test_editor_syntax_ocaml_capture_contract},
        {"editor_syntax_ruby_capture_contract", test_editor_syntax_ruby_capture_contract},
        {"editor_syntax_julia_capture_contract", test_editor_syntax_julia_capture_contract},
        {"editor_syntax_haskell_capture_contract", test_editor_syntax_haskell_capture_contract},
        {"editor_syntax_cpp_capture_contract", test_editor_syntax_cpp_capture_contract},
        {"editor_syntax_scala_capture_contract", test_editor_syntax_scala_capture_contract},
        {"editor_syntax_latex_capture_contract", test_editor_syntax_latex_capture_contract},
        {"editor_syntax_query_budget_match_limit_is_graceful",
         test_editor_syntax_query_budget_match_limit_is_graceful},
        {"editor_syntax_query_compile_failure_records_diagnostics",
         test_editor_syntax_query_compile_failure_records_diagnostics},
        {"editor_syntax_capture_rules_are_longest_match_first",
         test_editor_syntax_capture_rules_are_longest_match_first},
        {"editor_syntax_collect_c_captures_without_injection_query_is_graceful",
         test_editor_syntax_collect_c_captures_without_injection_query_is_graceful},
        {"editor_syntax_capture_truncation_reports_event_and_keeps_span_limit",
         test_editor_syntax_capture_truncation_reports_event_and_keeps_span_limit},
        {"editor_syntax_parse_tree_errors_report_event_and_status",
         test_editor_syntax_parse_tree_errors_report_event_and_status},
        {"editor_syntax_injection_depth_limit_reports_event",
         test_editor_syntax_injection_depth_limit_reports_event},
        {"editor_syntax_injection_slot_limit_reports_event",
         test_editor_syntax_injection_slot_limit_reports_event},
        {"editor_syntax_unknown_injection_target_is_graceful",
         test_editor_syntax_unknown_injection_target_is_graceful},
        {"editor_syntax_parse_budget_is_graceful", test_editor_syntax_parse_budget_is_graceful},
};

const int g_syntax_captures_test_count =
        (int)(sizeof(g_syntax_captures_tests) / sizeof(g_syntax_captures_tests[0]));
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "rotide.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
