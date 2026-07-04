#include "language/syntax.h"
#include "rotide.h"
#include "seed.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-language test: after N random edits, captures from the incremental
 * parse path must equal captures from a fresh full parse on the same text. */

#define INC_MAX_TEXT_BYTES (1u << 20)
#define INC_MAX_CAPTURES 8192

struct refBuf {
	char *bytes;
	size_t len;
	size_t cap;
};

static int refBufInit(struct refBuf *b, const char *initial, size_t initial_len) {
	b->cap = initial_len + 1024;
	b->bytes = (char *)malloc(b->cap);
	if (b->bytes == NULL) {
		return -1;
	}
	memcpy(b->bytes, initial, initial_len);
	b->len = initial_len;
	return 0;
}

static void refBufFree(struct refBuf *b) {
	free(b->bytes);
	b->bytes = NULL;
	b->len = b->cap = 0;
}

static int refBufReplace(struct refBuf *b, size_t start, size_t old_len, const char *text,
                         size_t new_len) {
	if (start + old_len > b->len) {
		return -1;
	}
	size_t after = b->len - old_len + new_len;
	if (after + 1 > b->cap) {
		size_t nc = b->cap;
		while (nc < after + 1) {
			nc *= 2;
		}
		char *grown = (char *)realloc(b->bytes, nc);
		if (grown == NULL) {
			return -1;
		}
		b->bytes = grown;
		b->cap = nc;
	}
	memmove(b->bytes + start + new_len, b->bytes + start + old_len, b->len - (start + old_len));
	memcpy(b->bytes + start, text, new_len);
	b->len = after;
	return 0;
}

static void byteToPoint(const char *text, size_t len, size_t byte, struct editorSyntaxPoint *out) {
	uint32_t row = 0;
	uint32_t col = 0;
	for (size_t i = 0; i < byte && i < len; i++) {
		if (text[i] == '\n') {
			row++;
			col = 0;
		} else {
			col++;
		}
	}
	out->row = row;
	out->column = col;
}

static uint64_t g_rng;
static uint64_t rngNext(void) {
	uint64_t x = g_rng;
	if (x == 0) {
		x = 0x9E3779B97F4A7C15ULL;
	}
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	g_rng = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static int captureCompare(const void *a_in, const void *b_in) {
	const struct editorSyntaxCapture *a = (const struct editorSyntaxCapture *)a_in;
	const struct editorSyntaxCapture *b = (const struct editorSyntaxCapture *)b_in;
	if (a->start_byte != b->start_byte) {
		return a->start_byte < b->start_byte ? -1 : 1;
	}
	if (a->end_byte != b->end_byte) {
		return a->end_byte < b->end_byte ? -1 : 1;
	}
	if (a->highlight_class != b->highlight_class) {
		return (int)a->highlight_class - (int)b->highlight_class;
	}
	return 0;
}

static int collectAndSortCaptures(struct editorSyntaxState *state, const char *text, size_t len,
                                  struct editorSyntaxCapture *out, int max, int *count_out) {
	struct editorTextSource view = {0};
	editorTextSourceInitString(&view, text, len);
	if (!editorSyntaxStateCollectCapturesForRange(state, &view, 0, (uint32_t)len, out, max,
	                                              count_out)) {
		return 0;
	}
	if (*count_out > 1) {
		qsort(out, (size_t)*count_out, sizeof(*out), captureCompare);
	}
	return 1;
}

struct langCase {
	const char *suite_name;
	enum editorSyntaxLanguage language;
	const char *fixture_path;
	int edits;
};

static const struct langCase k_lang_cases[] = {
        {"c", EDITOR_SYNTAX_C, "tests/syntax/supported/c/highlight.c", 20},
        {"cpp", EDITOR_SYNTAX_CPP, "tests/syntax/supported/cpp/contract.cpp", 20},
        {"go", EDITOR_SYNTAX_GO, "tests/syntax/supported/go/highlight.go", 20},
        {"python", EDITOR_SYNTAX_PYTHON, "tests/syntax/supported/python/highlight.py", 20},
        {"javascript", EDITOR_SYNTAX_JAVASCRIPT, "tests/syntax/supported/javascript/highlight.js",
         20},
        {"typescript", EDITOR_SYNTAX_TYPESCRIPT, "tests/syntax/supported/typescript/highlight.ts",
         20},
        {"rust", EDITOR_SYNTAX_RUST, "tests/syntax/supported/rust/highlight.rs", 20},
        {"csharp", EDITOR_SYNTAX_CSHARP, "tests/syntax/supported/csharp/contract.cs", 20},
        {"json", EDITOR_SYNTAX_JSON, "tests/syntax/supported/json/activation.json", 20},
        {"bash", EDITOR_SYNTAX_SHELL, "tests/syntax/supported/bash/highlight.sh", 20},
        {"toml", EDITOR_SYNTAX_TOML, "tests/syntax/supported/toml/highlight.toml", 20},
        {"latex", EDITOR_SYNTAX_LATEX, "tests/syntax/supported/latex/highlight.tex", 20},
        {"scala", EDITOR_SYNTAX_SCALA, "tests/syntax/supported/scala/contract.scala", 20},
        {"ocaml", EDITOR_SYNTAX_OCAML, "tests/syntax/supported/ocaml/contract.ml", 20},
        {"ruby", EDITOR_SYNTAX_RUBY, "tests/syntax/supported/ruby/contract.rb", 20},
        {"julia", EDITOR_SYNTAX_JULIA, "tests/syntax/supported/julia/contract.jl", 20},
        {"haskell", EDITOR_SYNTAX_HASKELL, "tests/syntax/supported/haskell/contract.hs", 20},
        {"bibtex", EDITOR_SYNTAX_BIBTEX, "tests/syntax/supported/bibtex/contract.bib", 20},
        {"hcl", EDITOR_SYNTAX_HCL, "tests/syntax/supported/hcl/contract.hcl", 20},
        {"lua", EDITOR_SYNTAX_LUA, "tests/syntax/supported/lua/contract.lua", 20},
        {"glsl", EDITOR_SYNTAX_GLSL, "tests/syntax/supported/glsl/contract.glsl", 20},
};

#define K_LANG_CASE_COUNT ((int)(sizeof(k_lang_cases) / sizeof(k_lang_cases[0])))

static int runIncrementalEquivTest(const struct langCase *lc, uint64_t seed) {
	char *resolved = testResolveRepoPath(lc->fixture_path);
	if (resolved == NULL) {
		(void)fprintf(stderr, "%s: failed to resolve %s\n", lc->suite_name,
		              lc->fixture_path);
		return 1;
	}
	size_t initial_len = 0;
	char *initial = read_file_contents(resolved, &initial_len);
	free(resolved);
	if (initial == NULL) {
		(void)fprintf(stderr, "%s: read failed %s\n", lc->suite_name, lc->fixture_path);
		return 1;
	}
	if (initial_len > INC_MAX_TEXT_BYTES) {
		initial_len = INC_MAX_TEXT_BYTES;
		initial[initial_len] = '\0';
	}

	struct refBuf buf;
	if (refBufInit(&buf, initial, initial_len) != 0) {
		free(initial);
		return 1;
	}
	free(initial);

	struct editorSyntaxState *incremental = editorSyntaxStateCreate(lc->language);
	if (incremental == NULL) {
		(void)fprintf(stderr, "%s: state create failed\n", lc->suite_name);
		refBufFree(&buf);
		return 1;
	}
	struct editorTextSource view = {0};
	editorTextSourceInitString(&view, buf.bytes, buf.len);
	if (!editorSyntaxStateParseFull(incremental, &view)) {
		(void)fprintf(stderr, "%s: initial parse failed\n", lc->suite_name);
		editorSyntaxStateDestroy(incremental);
		refBufFree(&buf);
		return 1;
	}

	g_rng = seed;
	for (int i = 0; i < lc->edits; i++) {
		size_t cur = buf.len;
		if (cur == 0) {
			break;
		}
		size_t start = (size_t)(rngNext() % (cur + 1));
		size_t available = cur - start;
		unsigned kind = (unsigned)(rngNext() % 3);
		size_t old_len = 0;
		size_t new_len = 0;
		char ins[16];
		if (kind == 0) {
			new_len = 1 + (size_t)(rngNext() % 12);
		} else if (kind == 1) {
			old_len =
			        available == 0
			                ? 0
			                : 1 + (size_t)(rngNext() % (available < 8 ? available : 8));
		} else {
			old_len =
			        available == 0
			                ? 0
			                : 1 + (size_t)(rngNext() % (available < 6 ? available : 6));
			new_len = 1 + (size_t)(rngNext() % 12);
		}
		if (new_len > sizeof(ins)) {
			new_len = sizeof(ins);
		}
		for (size_t j = 0; j < new_len; j++) {
			ins[j] = (char)('a' + (rngNext() % 26));
		}
		if (old_len == 0 && new_len == 0) {
			continue;
		}

		struct editorSyntaxEdit edit;
		edit.start_byte = (uint32_t)start;
		edit.old_end_byte = (uint32_t)(start + old_len);
		edit.new_end_byte = (uint32_t)(start + new_len);
		byteToPoint(buf.bytes, buf.len, start, &edit.start_point);
		byteToPoint(buf.bytes, buf.len, start + old_len, &edit.old_end_point);

		if (refBufReplace(&buf, start, old_len, ins, new_len) != 0) {
			(void)fprintf(stderr, "%s: refBufReplace failed at op %d\n", lc->suite_name,
			              i);
			editorSyntaxStateDestroy(incremental);
			refBufFree(&buf);
			return 1;
		}
		byteToPoint(buf.bytes, buf.len, start + new_len, &edit.new_end_point);

		struct editorTextSource after_view = {0};
		editorTextSourceInitString(&after_view, buf.bytes, buf.len);
		if (!editorSyntaxStateApplyEditAndParse(incremental, &edit, &after_view)) {
			(void)fprintf(
			        stderr,
			        "%s: ApplyEditAndParse failed op#%d start=%zu old=%zu new=%zu "
			        "seed=0x%016llx\n",
			        lc->suite_name, i, start, old_len, new_len,
			        (unsigned long long)seed);
			editorSyntaxStateDestroy(incremental);
			refBufFree(&buf);
			return 1;
		}
	}

	struct editorSyntaxCapture *inc_caps =
	        (struct editorSyntaxCapture *)malloc(sizeof(*inc_caps) * INC_MAX_CAPTURES);
	struct editorSyntaxCapture *full_caps =
	        (struct editorSyntaxCapture *)malloc(sizeof(*full_caps) * INC_MAX_CAPTURES);
	if (inc_caps == NULL || full_caps == NULL) {
		free(inc_caps);
		free(full_caps);
		editorSyntaxStateDestroy(incremental);
		refBufFree(&buf);
		return 1;
	}
	int inc_count = 0;
	if (!collectAndSortCaptures(incremental, buf.bytes, buf.len, inc_caps, INC_MAX_CAPTURES,
	                            &inc_count)) {
		(void)fprintf(stderr, "%s: incremental capture collect failed\n", lc->suite_name);
		free(inc_caps);
		free(full_caps);
		editorSyntaxStateDestroy(incremental);
		refBufFree(&buf);
		return 1;
	}

	struct editorSyntaxState *full = editorSyntaxStateCreate(lc->language);
	if (full == NULL) {
		free(inc_caps);
		free(full_caps);
		editorSyntaxStateDestroy(incremental);
		refBufFree(&buf);
		return 1;
	}
	struct editorTextSource final_view = {0};
	editorTextSourceInitString(&final_view, buf.bytes, buf.len);
	if (!editorSyntaxStateParseFull(full, &final_view)) {
		(void)fprintf(stderr, "%s: full reparse failed\n", lc->suite_name);
		editorSyntaxStateDestroy(full);
		free(inc_caps);
		free(full_caps);
		editorSyntaxStateDestroy(incremental);
		refBufFree(&buf);
		return 1;
	}
	int full_count = 0;
	if (!collectAndSortCaptures(full, buf.bytes, buf.len, full_caps, INC_MAX_CAPTURES,
	                            &full_count)) {
		(void)fprintf(stderr, "%s: full capture collect failed\n", lc->suite_name);
		editorSyntaxStateDestroy(full);
		free(inc_caps);
		free(full_caps);
		editorSyntaxStateDestroy(incremental);
		refBufFree(&buf);
		return 1;
	}

	int ok = 1;
	if (inc_count != full_count) {
		(void)fprintf(stderr,
		              "%s: capture count mismatch incremental=%d full=%d seed=0x%016llx\n",
		              lc->suite_name, inc_count, full_count, (unsigned long long)seed);
		ok = 0;
	} else {
		for (int i = 0; i < inc_count; i++) {
			if (captureCompare(&inc_caps[i], &full_caps[i]) != 0) {
				(void)fprintf(
				        stderr,
				        "%s: capture #%d differs: inc=[%u..%u cls=%d] full=[%u..%u "
				        "cls=%d] seed=0x%016llx\n",
				        lc->suite_name, i, inc_caps[i].start_byte,
				        inc_caps[i].end_byte, (int)inc_caps[i].highlight_class,
				        full_caps[i].start_byte, full_caps[i].end_byte,
				        (int)full_caps[i].highlight_class,
				        (unsigned long long)seed);
				ok = 0;
				break;
			}
		}
	}

	editorSyntaxStateDestroy(full);
	editorSyntaxStateDestroy(incremental);
	free(inc_caps);
	free(full_caps);
	refBufFree(&buf);
	return ok ? 0 : 1;
}

#define INCR_EQUIV_TEST(lang, idx)                                                                 \
	static int test_syntax_incremental_equiv_##lang(void) {                                    \
		uint64_t base = rotide_test_seed();                                                \
		if (base == 0)                                                                     \
			base = 0x9E3779B97F4A7C15ULL;                                              \
		return runIncrementalEquivTest(&k_lang_cases[idx],                                 \
		                               base ^ ((uint64_t)0xA5A5A5A5ULL * (idx + 1)));      \
	}

INCR_EQUIV_TEST(c, 0)
INCR_EQUIV_TEST(cpp, 1)
INCR_EQUIV_TEST(go, 2)
INCR_EQUIV_TEST(python, 3)
INCR_EQUIV_TEST(javascript, 4)
INCR_EQUIV_TEST(typescript, 5)
INCR_EQUIV_TEST(rust, 6)
INCR_EQUIV_TEST(csharp, 7)
INCR_EQUIV_TEST(json, 8)
INCR_EQUIV_TEST(bash, 9)
INCR_EQUIV_TEST(toml, 10)
INCR_EQUIV_TEST(latex, 11)
INCR_EQUIV_TEST(scala, 12)
INCR_EQUIV_TEST(ocaml, 13)
INCR_EQUIV_TEST(ruby, 14)
INCR_EQUIV_TEST(julia, 15)
INCR_EQUIV_TEST(haskell, 16)
INCR_EQUIV_TEST(bibtex, 17)
INCR_EQUIV_TEST(hcl, 18)
INCR_EQUIV_TEST(lua, 19)
INCR_EQUIV_TEST(glsl, 20)

const struct editorTestCase g_syntax_incremental_equiv_tests[] = {
        {"syntax_incremental_equiv_c", test_syntax_incremental_equiv_c},
        {"syntax_incremental_equiv_cpp", test_syntax_incremental_equiv_cpp},
        {"syntax_incremental_equiv_go", test_syntax_incremental_equiv_go},
        {"syntax_incremental_equiv_python", test_syntax_incremental_equiv_python},
        {"syntax_incremental_equiv_javascript", test_syntax_incremental_equiv_javascript},
        {"syntax_incremental_equiv_typescript", test_syntax_incremental_equiv_typescript},
        {"syntax_incremental_equiv_rust", test_syntax_incremental_equiv_rust},
        {"syntax_incremental_equiv_csharp", test_syntax_incremental_equiv_csharp},
        {"syntax_incremental_equiv_json", test_syntax_incremental_equiv_json},
        {"syntax_incremental_equiv_bash", test_syntax_incremental_equiv_bash},
        {"syntax_incremental_equiv_toml", test_syntax_incremental_equiv_toml},
        {"syntax_incremental_equiv_latex", test_syntax_incremental_equiv_latex},
        {"syntax_incremental_equiv_scala", test_syntax_incremental_equiv_scala},
        {"syntax_incremental_equiv_ocaml", test_syntax_incremental_equiv_ocaml},
        {"syntax_incremental_equiv_ruby", test_syntax_incremental_equiv_ruby},
        {"syntax_incremental_equiv_julia", test_syntax_incremental_equiv_julia},
        {"syntax_incremental_equiv_haskell", test_syntax_incremental_equiv_haskell},
        {"syntax_incremental_equiv_bibtex", test_syntax_incremental_equiv_bibtex},
        {"syntax_incremental_equiv_hcl", test_syntax_incremental_equiv_hcl},
        {"syntax_incremental_equiv_lua", test_syntax_incremental_equiv_lua},
        {"syntax_incremental_equiv_glsl", test_syntax_incremental_equiv_glsl},
};

const int g_syntax_incremental_equiv_test_count =
        (int)(sizeof(g_syntax_incremental_equiv_tests) /
              sizeof(g_syntax_incremental_equiv_tests[0]));
