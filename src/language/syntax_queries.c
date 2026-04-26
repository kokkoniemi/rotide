/* Included by syntax.c. Query caches, fallback query text, and load helpers live here. */

#include "language/syntax_query_data.h"

enum editorSyntaxCaptureRole {
	EDITOR_SYNTAX_CAPTURE_ROLE_NONE = 0,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_SCOPE,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_DEFINITION,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_REFERENCE,
	EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_CONTENT,
	EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_LANGUAGE
};

#define ROTIDE_SYNTAX_PERF_DEGRADED_PREDICATES_BYTES ((size_t)(512 * 1024))
#define ROTIDE_SYNTAX_PERF_DEGRADED_INJECTIONS_BYTES ((size_t)(2 * 1024 * 1024))
#define ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_NORMAL 8192U
#define ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_DEGRADED 4096U
#define ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_DEGRADED_INJECTIONS 2048U

#define ROTIDE_SYNTAX_QUERY_BUDGET_NS_NORMAL (8000000ULL)
#define ROTIDE_SYNTAX_QUERY_BUDGET_NS_DEGRADED (6000000ULL)
#define ROTIDE_SYNTAX_QUERY_BUDGET_NS_DEGRADED_INJECTIONS (5000000ULL)

#define ROTIDE_SYNTAX_PARSE_BUDGET_NS_NORMAL (50000000ULL)
#define ROTIDE_SYNTAX_PARSE_BUDGET_NS_DEGRADED (30000000ULL)
#define ROTIDE_SYNTAX_PARSE_BUDGET_NS_DEGRADED_INJECTIONS (20000000ULL)
struct editorSyntaxLocalMark {
	TSNode node;
	int is_local;
};

struct editorSyntaxLocalsContext {
	struct editorSyntaxLocalMark *marks;
	int count;
	int cap;
};

struct editorSyntaxParsedTree {
	enum editorSyntaxLanguage language;
	TSParser *parser;
	TSTree *tree;
	TSRange *included_ranges;
	uint32_t included_range_count;
	uint64_t revision;
};

struct editorSyntaxInjectedTree {
	struct editorSyntaxParsedTree parsed;
	struct editorSyntaxLocalsContext locals;
	uint64_t locals_revision;
	int locals_valid;
	int active;
	int depth;
};

struct editorSyntaxInjectionPatternMetadata {
	char *language;
	uint8_t combined;
	uint8_t include_children;
	uint8_t has_offset;
	uint32_t offset_capture_id;
	int32_t start_row_offset;
	int32_t start_column_offset;
	int32_t end_row_offset;
	int32_t end_column_offset;
};

#define ROTIDE_SYNTAX_MAX_INJECTION_TREES 16
#define ROTIDE_SYNTAX_MAX_INJECTION_DEPTH 3

struct editorSyntaxState {
	enum editorSyntaxLanguage language;
	struct editorSyntaxParsedTree host;
	struct editorSyntaxLocalsContext host_locals;
	struct editorSyntaxInjectedTree injections[ROTIDE_SYNTAX_MAX_INJECTION_TREES];
	int injection_count;
	uint64_t host_locals_revision;
	int host_locals_valid;
	int perf_disable_predicates;
	int perf_disable_injections;
	enum editorSyntaxPerformanceMode perf_mode;
	struct editorSyntaxByteRange *last_changed_ranges;
	int last_changed_range_count;
	int last_changed_range_cap;
	int budget_parse_exceeded;
	int budget_query_exceeded;
	size_t source_len;
	char *scratch_primary;
	size_t scratch_primary_cap;
	char *scratch_secondary;
	size_t scratch_secondary_cap;
};

struct editorSyntaxQueryCacheEntry {
	int load_attempted;
	TSQuery *query;
	enum editorSyntaxHighlightClass *capture_classes;
	uint8_t *capture_roles;
	struct editorSyntaxInjectionPatternMetadata *pattern_injection_metadata;
	uint32_t capture_count;
	uint32_t pattern_count;
	regex_t *compiled_regexes;
	uint8_t *compiled_regex_compiled;
	uint8_t *compiled_regex_failed;
	uint32_t string_count;
};

struct editorSyntaxScopeInfo {
	TSNode node;
	int parent_idx;
	char **definitions;
	int def_count;
	int def_cap;
};

struct editorSyntaxCaptureVec {
	struct editorSyntaxCapture *items;
	int count;
	int cap;
};

struct editorSyntaxRangeVec {
	TSRange *items;
	uint32_t count;
	uint32_t cap;
};

struct editorSyntaxBudgetConfig {
	uint32_t query_match_limit;
	uint64_t query_budget_ns;
	uint64_t parse_budget_ns;
};

struct editorSyntaxDeadlineContext {
	uint64_t deadline_ns;
	int exceeded;
};

struct editorSyntaxPredicateContext {
	struct editorSyntaxState *state;
	const struct editorTextSource *source;
	const struct editorSyntaxLocalsContext *locals;
};

static struct editorSyntaxQueryCacheEntry g_c_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_cpp_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_go_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_shell_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_html_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_javascript_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_jsdoc_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_typescript_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_css_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_json_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_python_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_php_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_rust_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_java_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_regex_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_csharp_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_haskell_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_ruby_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_ocaml_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_julia_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_scala_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_ejs_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_erb_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_javascript_locals_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_typescript_locals_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_html_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_javascript_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_typescript_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_php_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_cpp_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_haskell_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_julia_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_ejs_injection_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_erb_injection_query_cache = {0};

static struct {
	int enabled;
	uint32_t query_match_limit;
	uint64_t query_time_budget_ns;
	uint64_t parse_time_budget_ns;
} g_editor_syntax_budget_overrides = {0};

static int editorSyntaxStringEquals(const char *s, size_t len, const char *literal) {
	if (s == NULL || literal == NULL) {
		return 0;
	}
	size_t lit_len = strlen(literal);
	if (len != lit_len) {
		return 0;
	}
	return memcmp(s, literal, len) == 0;
}

static int editorSyntaxStringEqualsNoCase(const char *s, size_t len, const char *literal) {
	if (s == NULL || literal == NULL) {
		return 0;
	}
	size_t lit_len = strlen(literal);
	if (len != lit_len) {
		return 0;
	}
	return strncasecmp(s, literal, len) == 0;
}

static int editorSyntaxLengthFitsTreeSitter(size_t len) {
	return len <= UINT32_MAX;
}

static uint64_t editorSyntaxMonotonicNanos(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t editorSyntaxComputeDeadlineNs(uint64_t budget_ns) {
	if (budget_ns == 0) {
		return 0;
	}
	uint64_t now = editorSyntaxMonotonicNanos();
	if (now == 0 || budget_ns > UINT64_MAX - now) {
		return 0;
	}
	return now + budget_ns;
}

static bool editorSyntaxParseProgressCallback(TSParseState *state) {
	if (state == NULL || state->payload == NULL) {
		return false;
	}
	struct editorSyntaxDeadlineContext *deadline = state->payload;
	if (deadline->deadline_ns == 0) {
		return false;
	}
	uint64_t now = editorSyntaxMonotonicNanos();
	if (now != 0 && now >= deadline->deadline_ns) {
		deadline->exceeded = 1;
		return true;
	}
	return false;
}

static bool editorSyntaxQueryProgressCallback(TSQueryCursorState *state) {
	if (state == NULL || state->payload == NULL) {
		return false;
	}
	struct editorSyntaxDeadlineContext *deadline = state->payload;
	if (deadline->deadline_ns == 0) {
		return false;
	}
	uint64_t now = editorSyntaxMonotonicNanos();
	if (now != 0 && now >= deadline->deadline_ns) {
		deadline->exceeded = 1;
		return true;
	}
	return false;
}

static const char *editorTextSourceReadFromString(const struct editorTextSource *source,
		size_t byte_index, uint32_t *bytes_read) {
	if (bytes_read == NULL) {
		return NULL;
	}
	*bytes_read = 0;
	if (source == NULL || source->context == NULL || byte_index >= source->length) {
		return NULL;
	}

	size_t remaining = source->length - byte_index;
	if (remaining > UINT32_MAX) {
		remaining = UINT32_MAX;
	}
	*bytes_read = (uint32_t)remaining;
	return (const char *)source->context + byte_index;
}

void editorTextSourceInitString(struct editorTextSource *source, const char *text, size_t len) {
	if (source == NULL) {
		return;
	}
	source->read = editorTextSourceReadFromString;
	source->context = text;
	source->length = len;
}

size_t editorTextSourceLength(const struct editorTextSource *source) {
	if (source == NULL) {
		return 0;
	}
	return source->length;
}

int editorTextSourceCopyRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, char *dst) {
	if (source == NULL || source->read == NULL || dst == NULL || end_byte < start_byte ||
			end_byte > source->length) {
		return 0;
	}
	size_t offset = start_byte;
	size_t write_offset = 0;
	while (offset < end_byte) {
		uint32_t bytes_read = 0;
		const char *chunk = source->read(source, offset, &bytes_read);
		if (chunk == NULL || bytes_read == 0) {
			return 0;
		}

		size_t chunk_len = bytes_read;
		size_t remaining = end_byte - offset;
		if (chunk_len > remaining) {
			chunk_len = remaining;
		}
		memcpy(dst + write_offset, chunk, chunk_len);
		offset += chunk_len;
		write_offset += chunk_len;
	}
	return 1;
}

char *editorTextSourceDupRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (source == NULL || end_byte < start_byte || end_byte > source->length) {
		return NULL;
	}

	size_t len = end_byte - start_byte;
	char *dup = malloc(len + 1);
	if (dup == NULL) {
		if (len_out != NULL) {
			*len_out = len;
		}
		return NULL;
	}
	if (len > 0 && !editorTextSourceCopyRange(source, start_byte, end_byte, dup)) {
		free(dup);
		return NULL;
	}
	dup[len] = '\0';
	if (len_out != NULL) {
		*len_out = len;
	}
	return dup;
}

static const char *editorSyntaxSourceRead(void *payload, uint32_t byte_index,
		TSPoint position, uint32_t *bytes_read) {
	(void)position;
	if (bytes_read == NULL) {
		return NULL;
	}
	*bytes_read = 0;
	if (payload == NULL) {
		return NULL;
	}
	const struct editorTextSource *source = payload;
	if (source->read == NULL || (size_t)byte_index >= source->length) {
		return NULL;
	}
	return source->read(source, byte_index, bytes_read);
}

static struct editorSyntaxBudgetConfig editorSyntaxBudgetConfigForMode(
		enum editorSyntaxPerformanceMode mode) {
	struct editorSyntaxBudgetConfig config = {
		.query_match_limit = ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_NORMAL,
		.query_budget_ns = ROTIDE_SYNTAX_QUERY_BUDGET_NS_NORMAL,
		.parse_budget_ns = ROTIDE_SYNTAX_PARSE_BUDGET_NS_NORMAL
	};

	if (mode == EDITOR_SYNTAX_PERF_DEGRADED_PREDICATES) {
		config.query_match_limit = ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_DEGRADED;
		config.query_budget_ns = ROTIDE_SYNTAX_QUERY_BUDGET_NS_DEGRADED;
		config.parse_budget_ns = ROTIDE_SYNTAX_PARSE_BUDGET_NS_DEGRADED;
	} else if (mode == EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS) {
		config.query_match_limit = ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_DEGRADED_INJECTIONS;
		config.query_budget_ns = ROTIDE_SYNTAX_QUERY_BUDGET_NS_DEGRADED_INJECTIONS;
		config.parse_budget_ns = ROTIDE_SYNTAX_PARSE_BUDGET_NS_DEGRADED_INJECTIONS;
	}

	if (g_editor_syntax_budget_overrides.enabled) {
		config.query_match_limit = g_editor_syntax_budget_overrides.query_match_limit;
		config.query_budget_ns = g_editor_syntax_budget_overrides.query_time_budget_ns;
		config.parse_budget_ns = g_editor_syntax_budget_overrides.parse_time_budget_ns;
	}

	return config;
}

static int editorSyntaxCaptureNameHasPrefix(const char *name, size_t len, const char *prefix) {
	size_t prefix_len = strlen(prefix);
	if (name == NULL || prefix == NULL || len < prefix_len) {
		return 0;
	}
	return strncmp(name, prefix, prefix_len) == 0;
}

static enum editorSyntaxHighlightClass editorSyntaxClassFromCaptureName(const char *name,
		size_t len) {
	if (name == NULL || len == 0) {
		return EDITOR_SYNTAX_HL_NONE;
	}

	if (editorSyntaxCaptureNameHasPrefix(name, len, "comment")) {
		return EDITOR_SYNTAX_HL_COMMENT;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "keyword")) {
		return EDITOR_SYNTAX_HL_KEYWORD;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "type") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "constructor") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "tag")) {
		return EDITOR_SYNTAX_HL_TYPE;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "function") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "command") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "label")) {
		return EDITOR_SYNTAX_HL_FUNCTION;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "string") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "character")) {
		return EDITOR_SYNTAX_HL_STRING;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "number") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "float")) {
		return EDITOR_SYNTAX_HL_NUMBER;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "variable.builtin") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "constant") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "boolean") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "property")) {
		return EDITOR_SYNTAX_HL_CONSTANT;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "attribute") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "preproc") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "preprocessor")) {
		return EDITOR_SYNTAX_HL_PREPROCESSOR;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "operator")) {
		return EDITOR_SYNTAX_HL_OPERATOR;
	}
	if (editorSyntaxCaptureNameHasPrefix(name, len, "punctuation") ||
			editorSyntaxCaptureNameHasPrefix(name, len, "delimiter")) {
		return EDITOR_SYNTAX_HL_PUNCTUATION;
	}

	return EDITOR_SYNTAX_HL_NONE;
}

static const TSLanguage *editorSyntaxLanguageObject(enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_C:
			return tree_sitter_c();
		case EDITOR_SYNTAX_CPP:
			return tree_sitter_cpp();
		case EDITOR_SYNTAX_GO:
			return tree_sitter_go();
		case EDITOR_SYNTAX_SHELL:
			return tree_sitter_bash();
		case EDITOR_SYNTAX_HTML:
			return tree_sitter_html();
		case EDITOR_SYNTAX_JAVASCRIPT:
			return tree_sitter_javascript();
		case EDITOR_SYNTAX_JSDOC:
			return tree_sitter_jsdoc();
		case EDITOR_SYNTAX_TYPESCRIPT:
			return tree_sitter_typescript();
		case EDITOR_SYNTAX_CSS:
			return tree_sitter_css();
		case EDITOR_SYNTAX_JSON:
			return tree_sitter_json();
		case EDITOR_SYNTAX_PYTHON:
			return tree_sitter_python();
		case EDITOR_SYNTAX_PHP:
			return tree_sitter_php();
		case EDITOR_SYNTAX_RUST:
			return tree_sitter_rust();
		case EDITOR_SYNTAX_JAVA:
			return tree_sitter_java();
		case EDITOR_SYNTAX_REGEX:
			return tree_sitter_regex();
		case EDITOR_SYNTAX_CSHARP:
			return tree_sitter_c_sharp();
		case EDITOR_SYNTAX_HASKELL:
			return tree_sitter_haskell();
		case EDITOR_SYNTAX_RUBY:
			return tree_sitter_ruby();
		case EDITOR_SYNTAX_OCAML:
			return tree_sitter_ocaml();
		case EDITOR_SYNTAX_JULIA:
			return tree_sitter_julia();
		case EDITOR_SYNTAX_SCALA:
			return tree_sitter_scala();
		case EDITOR_SYNTAX_EJS:
		case EDITOR_SYNTAX_ERB:
			return tree_sitter_embedded_template();
		case EDITOR_SYNTAX_NONE:
		default:
			return NULL;
	}
}

static int editorSyntaxCompileQuery(enum editorSyntaxLanguage language,
		const char *query_source, size_t query_len, TSQuery **query_out) {
	const TSLanguage *ts_language = editorSyntaxLanguageObject(language);
	if (ts_language == NULL || query_source == NULL || query_out == NULL ||
			!editorSyntaxLengthFitsTreeSitter(query_len)) {
		return 0;
	}

	uint32_t error_offset = 0;
	TSQueryError error_type = TSQueryErrorNone;
	TSQuery *query = ts_query_new(ts_language, query_source, (uint32_t)query_len,
			&error_offset, &error_type);
	if (query == NULL) {
		(void)error_offset;
		(void)error_type;
		return 0;
	}

	*query_out = query;
	return 1;
}

static int editorSyntaxPopulateCaptureClasses(TSQuery *query,
		enum editorSyntaxHighlightClass **capture_classes_out, uint32_t *capture_count_out) {
	if (capture_classes_out == NULL || capture_count_out == NULL || query == NULL) {
		return 0;
	}
	*capture_classes_out = NULL;
	*capture_count_out = 0;

	uint32_t capture_count = ts_query_capture_count(query);
	enum editorSyntaxHighlightClass *capture_classes = NULL;
	if (capture_count > 0) {
		size_t bytes = (size_t)capture_count * sizeof(*capture_classes);
		capture_classes = malloc(bytes);
		if (capture_classes == NULL) {
			return 0;
		}
		for (uint32_t i = 0; i < capture_count; i++) {
			capture_classes[i] = EDITOR_SYNTAX_HL_NONE;
		}
		for (uint32_t i = 0; i < capture_count; i++) {
			uint32_t name_len = 0;
			const char *name = ts_query_capture_name_for_id(query, i, &name_len);
			capture_classes[i] = editorSyntaxClassFromCaptureName(name, name_len);
		}
	}

	*capture_classes_out = capture_classes;
	*capture_count_out = capture_count;
	return 1;
}

static int editorSyntaxPopulateLocalsCaptureRoles(TSQuery *query, uint8_t **capture_roles_out,
		uint32_t *capture_count_out) {
	if (query == NULL || capture_roles_out == NULL || capture_count_out == NULL) {
		return 0;
	}
	*capture_roles_out = NULL;
	*capture_count_out = 0;

	uint32_t capture_count = ts_query_capture_count(query);
	uint8_t *capture_roles = NULL;
	if (capture_count > 0) {
		capture_roles = calloc(capture_count, sizeof(*capture_roles));
		if (capture_roles == NULL) {
			return 0;
		}

		for (uint32_t i = 0; i < capture_count; i++) {
			uint32_t name_len = 0;
			const char *name = ts_query_capture_name_for_id(query, i, &name_len);
			if (editorSyntaxStringEquals(name, name_len, "local.scope")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_SCOPE;
			} else if (editorSyntaxStringEquals(name, name_len, "local.definition")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_DEFINITION;
			} else if (editorSyntaxStringEquals(name, name_len, "local.reference")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_REFERENCE;
			}
		}
	}

	*capture_roles_out = capture_roles;
	*capture_count_out = capture_count;
	return 1;
}

static int editorSyntaxPopulateInjectionCaptureRoles(TSQuery *query, uint8_t **capture_roles_out,
		uint32_t *capture_count_out) {
	if (query == NULL || capture_roles_out == NULL || capture_count_out == NULL) {
		return 0;
	}
	*capture_roles_out = NULL;
	*capture_count_out = 0;

	uint32_t capture_count = ts_query_capture_count(query);
	uint8_t *capture_roles = NULL;
	if (capture_count > 0) {
		capture_roles = calloc(capture_count, sizeof(*capture_roles));
		if (capture_roles == NULL) {
			return 0;
		}

		for (uint32_t i = 0; i < capture_count; i++) {
			uint32_t name_len = 0;
			const char *name = ts_query_capture_name_for_id(query, i, &name_len);
			if (editorSyntaxCaptureNameHasPrefix(name, name_len, "injection.content")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_CONTENT;
			} else if (editorSyntaxCaptureNameHasPrefix(name, name_len, "injection.language")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_LANGUAGE;
			}
		}
	}

	*capture_roles_out = capture_roles;
	*capture_count_out = capture_count;
	return 1;
}

static int editorSyntaxParsePredicateInt32(const char *value, uint32_t value_len,
		int32_t *out) {
	if (value == NULL || value_len == 0 || out == NULL) {
		return 0;
	}
	if (value_len >= 32) {
		return 0;
	}
	char buf[32];
	memcpy(buf, value, value_len);
	buf[value_len] = '\0';
	char *end = NULL;
	long parsed = strtol(buf, &end, 10);
	if (end == buf || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) {
		return 0;
	}
	*out = (int32_t)parsed;
	return 1;
}

static void editorSyntaxFreeInjectionPatternMetadata(
		struct editorSyntaxInjectionPatternMetadata *metadata,
		uint32_t pattern_count) {
	if (metadata == NULL) {
		return;
	}
	for (uint32_t i = 0; i < pattern_count; i++) {
		free(metadata[i].language);
	}
	free(metadata);
}

static int editorSyntaxPopulateInjectionPatternMetadata(TSQuery *query,
		struct editorSyntaxInjectionPatternMetadata **metadata_out,
		uint32_t *pattern_count_out) {
	if (query == NULL || metadata_out == NULL ||
			pattern_count_out == NULL) {
		return 0;
	}
	*metadata_out = NULL;
	*pattern_count_out = 0;

	uint32_t pattern_count = ts_query_pattern_count(query);
	struct editorSyntaxInjectionPatternMetadata *metadata = NULL;
	if (pattern_count > 0) {
		metadata = calloc(pattern_count, sizeof(*metadata));
		if (metadata == NULL) {
			return 0;
		}
	}

	for (uint32_t pattern_idx = 0; pattern_idx < pattern_count; pattern_idx++) {
		uint32_t step_count = 0;
		const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(query, pattern_idx,
				&step_count);
		if (steps == NULL || step_count == 0) {
			continue;
		}

		uint32_t i = 0;
		while (i < step_count) {
			uint32_t start = i;
			while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) {
				i++;
			}
			uint32_t end = i;
			if (end > start && steps[start].type == TSQueryPredicateStepTypeString) {
				uint32_t cmd_len = 0;
				const char *cmd = ts_query_string_value_for_id(query, steps[start].value_id,
						&cmd_len);
				if (editorSyntaxStringEquals(cmd, cmd_len, "set!") && end - start >= 3 &&
						steps[start + 1].type == TSQueryPredicateStepTypeString &&
						steps[start + 2].type == TSQueryPredicateStepTypeString) {
					uint32_t key_len = 0;
					const char *key = ts_query_string_value_for_id(query,
							steps[start + 1].value_id, &key_len);
					if (editorSyntaxStringEquals(key, key_len, "injection.language")) {
						uint32_t value_len = 0;
						const char *value = ts_query_string_value_for_id(query,
								steps[start + 2].value_id, &value_len);
						char *dup = malloc((size_t)value_len + 1);
						if (dup == NULL) {
							editorSyntaxFreeInjectionPatternMetadata(metadata, pattern_count);
							return 0;
						}
						memcpy(dup, value, value_len);
						dup[value_len] = '\0';
						free(metadata[pattern_idx].language);
						metadata[pattern_idx].language = dup;
					} else if (editorSyntaxStringEquals(key, key_len,
								"injection.combined")) {
						metadata[pattern_idx].combined = 1;
					} else if (editorSyntaxStringEquals(key, key_len,
								"injection.include-children")) {
						metadata[pattern_idx].include_children = 1;
					}
				} else if (editorSyntaxStringEquals(cmd, cmd_len, "set!") &&
						end - start >= 2 &&
						steps[start + 1].type == TSQueryPredicateStepTypeString) {
					uint32_t key_len = 0;
					const char *key = ts_query_string_value_for_id(query,
							steps[start + 1].value_id, &key_len);
					if (editorSyntaxStringEquals(key, key_len, "injection.combined")) {
						metadata[pattern_idx].combined = 1;
					} else if (editorSyntaxStringEquals(key, key_len,
								"injection.include-children")) {
						metadata[pattern_idx].include_children = 1;
					}
				} else if (editorSyntaxStringEquals(cmd, cmd_len, "offset!") &&
						end - start >= 6 &&
						steps[start + 1].type == TSQueryPredicateStepTypeCapture &&
						steps[start + 2].type == TSQueryPredicateStepTypeString &&
						steps[start + 3].type == TSQueryPredicateStepTypeString &&
						steps[start + 4].type == TSQueryPredicateStepTypeString &&
						steps[start + 5].type == TSQueryPredicateStepTypeString) {
					int32_t start_row = 0;
					int32_t start_col = 0;
					int32_t end_row = 0;
					int32_t end_col = 0;
					uint32_t value_len = 0;
					const char *value = ts_query_string_value_for_id(query,
							steps[start + 2].value_id, &value_len);
					if (!editorSyntaxParsePredicateInt32(value, value_len, &start_row)) {
						i++;
						continue;
					}
					value = ts_query_string_value_for_id(query,
							steps[start + 3].value_id, &value_len);
					if (!editorSyntaxParsePredicateInt32(value, value_len, &start_col)) {
						i++;
						continue;
					}
					value = ts_query_string_value_for_id(query,
							steps[start + 4].value_id, &value_len);
					if (!editorSyntaxParsePredicateInt32(value, value_len, &end_row)) {
						i++;
						continue;
					}
					value = ts_query_string_value_for_id(query,
							steps[start + 5].value_id, &value_len);
					if (!editorSyntaxParsePredicateInt32(value, value_len, &end_col)) {
						i++;
						continue;
					}
					metadata[pattern_idx].has_offset = 1;
					metadata[pattern_idx].offset_capture_id = steps[start + 1].value_id;
					metadata[pattern_idx].start_row_offset = start_row;
					metadata[pattern_idx].start_column_offset = start_col;
					metadata[pattern_idx].end_row_offset = end_row;
					metadata[pattern_idx].end_column_offset = end_col;
				}
			}
			i++;
		}
	}

	*metadata_out = metadata;
	*pattern_count_out = pattern_count;
	return 1;
}

static void editorSyntaxClearQueryCacheEntry(struct editorSyntaxQueryCacheEntry *cache) {
	if (cache == NULL) {
		return;
	}
	if (cache->compiled_regexes != NULL && cache->compiled_regex_compiled != NULL) {
		for (uint32_t i = 0; i < cache->string_count; i++) {
			if (cache->compiled_regex_compiled[i]) {
				regfree(&cache->compiled_regexes[i]);
			}
		}
	}
	free(cache->compiled_regexes);
	cache->compiled_regexes = NULL;
	free(cache->compiled_regex_compiled);
	cache->compiled_regex_compiled = NULL;
	free(cache->compiled_regex_failed);
	cache->compiled_regex_failed = NULL;
	cache->string_count = 0;

	if (cache->query != NULL) {
		ts_query_delete(cache->query);
		cache->query = NULL;
	}
	free(cache->capture_classes);
	cache->capture_classes = NULL;
	free(cache->capture_roles);
	cache->capture_roles = NULL;
	editorSyntaxFreeInjectionPatternMetadata(cache->pattern_injection_metadata,
			cache->pattern_count);
	cache->pattern_injection_metadata = NULL;
	cache->capture_count = 0;
	cache->pattern_count = 0;
	cache->load_attempted = 0;
}

static char *editorSyntaxConcatEmbeddedParts(
		const struct editorSyntaxEmbeddedQueryPart *parts, int part_count,
		size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (parts == NULL || part_count <= 0) {
		return NULL;
	}
	size_t total = 0;
	for (int i = 0; i < part_count; i++) {
		total += parts[i].len + 1;
	}
	char *buf = malloc(total);
	if (buf == NULL) {
		return NULL;
	}
	size_t off = 0;
	for (int i = 0; i < part_count; i++) {
		if (parts[i].len > 0) {
			memcpy(buf + off, parts[i].data, parts[i].len);
			off += parts[i].len;
		}
		buf[off++] = '\n';
	}
	if (len_out != NULL) {
		*len_out = off;
	}
	return buf;
}

static int editorSyntaxEnsureQueryCache(struct editorSyntaxQueryCacheEntry *cache,
		enum editorSyntaxLanguage language,
		const struct editorSyntaxEmbeddedQueryPart *embedded_parts,
		int embedded_part_count,
	int want_capture_classes,
	int want_locals_roles,
	int want_injection_roles,
	int want_injection_metadata) {
	if (cache == NULL || embedded_parts == NULL || embedded_part_count <= 0) {
		return 0;
	}
	if (cache->load_attempted) {
		return cache->query != NULL;
	}
	cache->load_attempted = 1;

	TSQuery *query = NULL;
	size_t embedded_len = 0;
	char *embedded_query = editorSyntaxConcatEmbeddedParts(
			embedded_parts, embedded_part_count, &embedded_len);
	if (embedded_query == NULL) {
		return 0;
	}
	(void)editorSyntaxCompileQuery(language, embedded_query, embedded_len, &query);
	free(embedded_query);
	if (query == NULL) {
		return 0;
	}

	enum editorSyntaxHighlightClass *capture_classes = NULL;
	uint8_t *capture_roles = NULL;
	struct editorSyntaxInjectionPatternMetadata *pattern_metadata = NULL;
	uint32_t capture_count = 0;
	uint32_t pattern_count = 0;
	uint32_t string_count = ts_query_string_count(query);
	regex_t *compiled_regexes = NULL;
	uint8_t *compiled_regex_compiled = NULL;
	uint8_t *compiled_regex_failed = NULL;

	if (want_capture_classes &&
			!editorSyntaxPopulateCaptureClasses(query, &capture_classes, &capture_count)) {
		ts_query_delete(query);
		return 0;
	}

	if (want_locals_roles &&
			!editorSyntaxPopulateLocalsCaptureRoles(query, &capture_roles, &capture_count)) {
		free(capture_classes);
		ts_query_delete(query);
		return 0;
	}

	if (want_injection_roles &&
			!editorSyntaxPopulateInjectionCaptureRoles(query, &capture_roles, &capture_count)) {
		free(capture_classes);
		ts_query_delete(query);
		return 0;
	}

	if (want_injection_metadata &&
			!editorSyntaxPopulateInjectionPatternMetadata(query, &pattern_metadata,
					&pattern_count)) {
		free(capture_classes);
		free(capture_roles);
		ts_query_delete(query);
		return 0;
	}

	if (string_count > 0) {
		size_t strings_bytes = (size_t)string_count;
		compiled_regexes = calloc(string_count, sizeof(*compiled_regexes));
		compiled_regex_compiled = calloc(strings_bytes, sizeof(*compiled_regex_compiled));
		compiled_regex_failed = calloc(strings_bytes, sizeof(*compiled_regex_failed));
		if (compiled_regexes == NULL || compiled_regex_compiled == NULL ||
				compiled_regex_failed == NULL) {
			free(compiled_regexes);
			free(compiled_regex_compiled);
			free(compiled_regex_failed);
			free(capture_classes);
			free(capture_roles);
			editorSyntaxFreeInjectionPatternMetadata(pattern_metadata, pattern_count);
			ts_query_delete(query);
			return 0;
		}
	}

	cache->query = query;
	cache->capture_classes = capture_classes;
	cache->capture_roles = capture_roles;
	cache->pattern_injection_metadata = pattern_metadata;
	cache->capture_count = capture_count;
	cache->pattern_count = pattern_count;
	cache->compiled_regexes = compiled_regexes;
	cache->compiled_regex_compiled = compiled_regex_compiled;
	cache->compiled_regex_failed = compiled_regex_failed;
	cache->string_count = string_count;
	return 1;
}

static int editorSyntaxEnsureHighlightQuery(enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_C:
			return editorSyntaxEnsureQueryCache(&g_c_highlight_query_cache, EDITOR_SYNTAX_C,
					editor_query_c_highlight_parts, EDITOR_QUERY_C_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_CPP:
			return editorSyntaxEnsureQueryCache(&g_cpp_highlight_query_cache, EDITOR_SYNTAX_CPP,
					editor_query_cpp_highlight_parts, EDITOR_QUERY_CPP_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_GO:
			return editorSyntaxEnsureQueryCache(&g_go_highlight_query_cache, EDITOR_SYNTAX_GO,
					editor_query_go_highlight_parts, EDITOR_QUERY_GO_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_SHELL:
			return editorSyntaxEnsureQueryCache(&g_shell_highlight_query_cache,
					EDITOR_SYNTAX_SHELL,
					editor_query_shell_highlight_parts, EDITOR_QUERY_SHELL_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_HTML:
			return editorSyntaxEnsureQueryCache(&g_html_highlight_query_cache, EDITOR_SYNTAX_HTML,
					editor_query_html_highlight_parts, EDITOR_QUERY_HTML_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JAVASCRIPT:
			return editorSyntaxEnsureQueryCache(&g_javascript_highlight_query_cache,
					EDITOR_SYNTAX_JAVASCRIPT,
					editor_query_javascript_highlight_parts, EDITOR_QUERY_JAVASCRIPT_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JSDOC:
			return editorSyntaxEnsureQueryCache(&g_jsdoc_highlight_query_cache,
					EDITOR_SYNTAX_JSDOC,
					editor_query_jsdoc_highlight_parts, EDITOR_QUERY_JSDOC_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_TYPESCRIPT:
			return editorSyntaxEnsureQueryCache(&g_typescript_highlight_query_cache,
					EDITOR_SYNTAX_TYPESCRIPT,
					editor_query_typescript_highlight_parts, EDITOR_QUERY_TYPESCRIPT_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_CSS:
			return editorSyntaxEnsureQueryCache(&g_css_highlight_query_cache,
					EDITOR_SYNTAX_CSS,
					editor_query_css_highlight_parts, EDITOR_QUERY_CSS_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JSON:
			return editorSyntaxEnsureQueryCache(&g_json_highlight_query_cache,
					EDITOR_SYNTAX_JSON,
					editor_query_json_highlight_parts, EDITOR_QUERY_JSON_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_PYTHON:
			return editorSyntaxEnsureQueryCache(&g_python_highlight_query_cache,
					EDITOR_SYNTAX_PYTHON,
					editor_query_python_highlight_parts, EDITOR_QUERY_PYTHON_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_PHP:
			return editorSyntaxEnsureQueryCache(&g_php_highlight_query_cache,
					EDITOR_SYNTAX_PHP,
					editor_query_php_highlight_parts, EDITOR_QUERY_PHP_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_RUST:
			return editorSyntaxEnsureQueryCache(&g_rust_highlight_query_cache,
					EDITOR_SYNTAX_RUST,
					editor_query_rust_highlight_parts, EDITOR_QUERY_RUST_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JAVA:
			return editorSyntaxEnsureQueryCache(&g_java_highlight_query_cache,
					EDITOR_SYNTAX_JAVA,
					editor_query_java_highlight_parts, EDITOR_QUERY_JAVA_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_REGEX:
			return editorSyntaxEnsureQueryCache(&g_regex_highlight_query_cache,
					EDITOR_SYNTAX_REGEX,
					editor_query_regex_highlight_parts, EDITOR_QUERY_REGEX_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_CSHARP:
			return editorSyntaxEnsureQueryCache(&g_csharp_highlight_query_cache,
					EDITOR_SYNTAX_CSHARP,
					editor_query_csharp_highlight_parts, EDITOR_QUERY_CSHARP_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_HASKELL:
			return editorSyntaxEnsureQueryCache(&g_haskell_highlight_query_cache,
					EDITOR_SYNTAX_HASKELL,
					editor_query_haskell_highlight_parts, EDITOR_QUERY_HASKELL_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_RUBY:
			return editorSyntaxEnsureQueryCache(&g_ruby_highlight_query_cache,
					EDITOR_SYNTAX_RUBY,
					editor_query_ruby_highlight_parts, EDITOR_QUERY_RUBY_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_OCAML:
			return editorSyntaxEnsureQueryCache(&g_ocaml_highlight_query_cache,
					EDITOR_SYNTAX_OCAML,
					editor_query_ocaml_highlight_parts, EDITOR_QUERY_OCAML_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JULIA:
			return editorSyntaxEnsureQueryCache(&g_julia_highlight_query_cache,
					EDITOR_SYNTAX_JULIA,
					editor_query_julia_highlight_parts, EDITOR_QUERY_JULIA_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_SCALA:
			return editorSyntaxEnsureQueryCache(&g_scala_highlight_query_cache,
					EDITOR_SYNTAX_SCALA,
					editor_query_scala_highlight_parts, EDITOR_QUERY_SCALA_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_EJS:
			return editorSyntaxEnsureQueryCache(&g_ejs_highlight_query_cache,
					EDITOR_SYNTAX_EJS,
					editor_query_ejs_highlight_parts, EDITOR_QUERY_EJS_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_ERB:
			return editorSyntaxEnsureQueryCache(&g_erb_highlight_query_cache,
					EDITOR_SYNTAX_ERB,
					editor_query_erb_highlight_parts, EDITOR_QUERY_ERB_HIGHLIGHT_PART_COUNT,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_NONE:
		default:
			return 0;
	}
}

static int editorSyntaxEnsureLocalsQuery(enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_JAVASCRIPT:
			return editorSyntaxEnsureQueryCache(&g_javascript_locals_query_cache,
					EDITOR_SYNTAX_JAVASCRIPT,
					editor_query_javascript_locals_parts, EDITOR_QUERY_JAVASCRIPT_LOCALS_PART_COUNT,
					0, 1, 0, 0);
		case EDITOR_SYNTAX_TYPESCRIPT:
			return editorSyntaxEnsureQueryCache(&g_typescript_locals_query_cache,
					EDITOR_SYNTAX_TYPESCRIPT,
					editor_query_typescript_locals_parts, EDITOR_QUERY_TYPESCRIPT_LOCALS_PART_COUNT,
					0, 1, 0, 0);
		default:
			return 0;
	}
}

static int editorSyntaxEnsureInjectionQuery(enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_HTML:
			return editorSyntaxEnsureQueryCache(&g_html_injection_query_cache,
					EDITOR_SYNTAX_HTML,
					editor_query_html_injection_parts, EDITOR_QUERY_HTML_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_JAVASCRIPT:
			return editorSyntaxEnsureQueryCache(&g_javascript_injection_query_cache,
					EDITOR_SYNTAX_JAVASCRIPT,
					editor_query_javascript_injection_parts, EDITOR_QUERY_JAVASCRIPT_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_TYPESCRIPT:
			return editorSyntaxEnsureQueryCache(&g_typescript_injection_query_cache,
					EDITOR_SYNTAX_TYPESCRIPT,
					editor_query_typescript_injection_parts, EDITOR_QUERY_TYPESCRIPT_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_PHP:
			return editorSyntaxEnsureQueryCache(&g_php_injection_query_cache,
					EDITOR_SYNTAX_PHP,
					editor_query_php_injection_parts, EDITOR_QUERY_PHP_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_CPP:
			return editorSyntaxEnsureQueryCache(&g_cpp_injection_query_cache,
					EDITOR_SYNTAX_CPP,
					editor_query_cpp_injection_parts, EDITOR_QUERY_CPP_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_HASKELL:
			return editorSyntaxEnsureQueryCache(&g_haskell_injection_query_cache,
					EDITOR_SYNTAX_HASKELL,
					editor_query_haskell_injection_parts, EDITOR_QUERY_HASKELL_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_JULIA:
			return editorSyntaxEnsureQueryCache(&g_julia_injection_query_cache,
					EDITOR_SYNTAX_JULIA,
					editor_query_julia_injection_parts, EDITOR_QUERY_JULIA_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_EJS:
			return editorSyntaxEnsureQueryCache(&g_ejs_injection_query_cache,
					EDITOR_SYNTAX_EJS,
					editor_query_ejs_injection_parts, EDITOR_QUERY_EJS_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		case EDITOR_SYNTAX_ERB:
			return editorSyntaxEnsureQueryCache(&g_erb_injection_query_cache,
					EDITOR_SYNTAX_ERB,
					editor_query_erb_injection_parts, EDITOR_QUERY_ERB_INJECTION_PART_COUNT,
					0, 0, 1, 1);
		default:
			return 0;
	}
}

static const struct editorSyntaxQueryCacheEntry *editorSyntaxInjectionQueryCacheForLanguage(
		enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_HTML:
			return &g_html_injection_query_cache;
		case EDITOR_SYNTAX_JAVASCRIPT:
			return &g_javascript_injection_query_cache;
		case EDITOR_SYNTAX_TYPESCRIPT:
			return &g_typescript_injection_query_cache;
		case EDITOR_SYNTAX_PHP:
			return &g_php_injection_query_cache;
		case EDITOR_SYNTAX_CPP:
			return &g_cpp_injection_query_cache;
		case EDITOR_SYNTAX_HASKELL:
			return &g_haskell_injection_query_cache;
		case EDITOR_SYNTAX_JULIA:
			return &g_julia_injection_query_cache;
		case EDITOR_SYNTAX_EJS:
			return &g_ejs_injection_query_cache;
		case EDITOR_SYNTAX_ERB:
			return &g_erb_injection_query_cache;
		default:
			return NULL;
	}
}

static const struct editorSyntaxQueryCacheEntry *editorSyntaxHighlightQueryCacheForLanguage(
		enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_C:
			return &g_c_highlight_query_cache;
		case EDITOR_SYNTAX_CPP:
			return &g_cpp_highlight_query_cache;
		case EDITOR_SYNTAX_GO:
			return &g_go_highlight_query_cache;
		case EDITOR_SYNTAX_SHELL:
			return &g_shell_highlight_query_cache;
		case EDITOR_SYNTAX_HTML:
			return &g_html_highlight_query_cache;
		case EDITOR_SYNTAX_JAVASCRIPT:
			return &g_javascript_highlight_query_cache;
		case EDITOR_SYNTAX_JSDOC:
			return &g_jsdoc_highlight_query_cache;
		case EDITOR_SYNTAX_TYPESCRIPT:
			return &g_typescript_highlight_query_cache;
		case EDITOR_SYNTAX_CSS:
			return &g_css_highlight_query_cache;
		case EDITOR_SYNTAX_JSON:
			return &g_json_highlight_query_cache;
		case EDITOR_SYNTAX_PYTHON:
			return &g_python_highlight_query_cache;
		case EDITOR_SYNTAX_PHP:
			return &g_php_highlight_query_cache;
		case EDITOR_SYNTAX_RUST:
			return &g_rust_highlight_query_cache;
		case EDITOR_SYNTAX_JAVA:
			return &g_java_highlight_query_cache;
		case EDITOR_SYNTAX_REGEX:
			return &g_regex_highlight_query_cache;
		case EDITOR_SYNTAX_CSHARP:
			return &g_csharp_highlight_query_cache;
		case EDITOR_SYNTAX_HASKELL:
			return &g_haskell_highlight_query_cache;
		case EDITOR_SYNTAX_RUBY:
			return &g_ruby_highlight_query_cache;
		case EDITOR_SYNTAX_OCAML:
			return &g_ocaml_highlight_query_cache;
		case EDITOR_SYNTAX_JULIA:
			return &g_julia_highlight_query_cache;
		case EDITOR_SYNTAX_SCALA:
			return &g_scala_highlight_query_cache;
		case EDITOR_SYNTAX_EJS:
			return &g_ejs_highlight_query_cache;
		case EDITOR_SYNTAX_ERB:
			return &g_erb_highlight_query_cache;
		case EDITOR_SYNTAX_NONE:
		default:
			return NULL;
	}
}

static const struct editorSyntaxQueryCacheEntry *editorSyntaxLocalsQueryCacheForLanguage(
		enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_JAVASCRIPT:
			return &g_javascript_locals_query_cache;
		case EDITOR_SYNTAX_TYPESCRIPT:
			return &g_typescript_locals_query_cache;
		default:
			return NULL;
	}
}

static struct editorSyntaxQueryCacheEntry *editorSyntaxQueryCacheEntryForQuery(const TSQuery *query) {
	if (query == NULL) {
		return NULL;
	}

	struct editorSyntaxQueryCacheEntry *all[] = {
		&g_c_highlight_query_cache,
		&g_cpp_highlight_query_cache,
		&g_go_highlight_query_cache,
		&g_shell_highlight_query_cache,
		&g_html_highlight_query_cache,
		&g_javascript_highlight_query_cache,
		&g_jsdoc_highlight_query_cache,
		&g_typescript_highlight_query_cache,
		&g_css_highlight_query_cache,
		&g_json_highlight_query_cache,
		&g_python_highlight_query_cache,
		&g_php_highlight_query_cache,
		&g_rust_highlight_query_cache,
		&g_java_highlight_query_cache,
		&g_regex_highlight_query_cache,
		&g_csharp_highlight_query_cache,
		&g_haskell_highlight_query_cache,
		&g_ruby_highlight_query_cache,
		&g_ocaml_highlight_query_cache,
		&g_julia_highlight_query_cache,
		&g_scala_highlight_query_cache,
		&g_ejs_highlight_query_cache,
		&g_erb_highlight_query_cache,
		&g_javascript_locals_query_cache,
		&g_typescript_locals_query_cache,
		&g_html_injection_query_cache,
		&g_javascript_injection_query_cache,
		&g_typescript_injection_query_cache,
		&g_php_injection_query_cache,
		&g_cpp_injection_query_cache,
		&g_haskell_injection_query_cache,
		&g_julia_injection_query_cache,
		&g_ejs_injection_query_cache,
		&g_erb_injection_query_cache
	};

	for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
		if (all[i]->query == query) {
			return all[i];
		}
	}
	return NULL;
}
