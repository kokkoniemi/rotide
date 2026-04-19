/* Included by syntax.c. Query caches, fallback query text, and load helpers live here. */

enum editorSyntaxCaptureRole {
	EDITOR_SYNTAX_CAPTURE_ROLE_NONE = 0,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_SCOPE,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_DEFINITION,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_REFERENCE,
	EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_CONTENT
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

struct editorSyntaxState {
	enum editorSyntaxLanguage language;
	struct editorSyntaxParsedTree host;
	struct editorSyntaxParsedTree javascript_injection;
	struct editorSyntaxParsedTree css_injection;
	struct editorSyntaxLocalsContext host_locals;
	struct editorSyntaxLocalsContext injection_javascript_locals;
	uint64_t host_locals_revision;
	uint64_t injection_javascript_locals_revision;
	int host_locals_valid;
	int injection_javascript_locals_valid;
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
	char **pattern_injection_languages;
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
static struct editorSyntaxQueryCacheEntry g_typescript_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_css_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_json_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_python_highlight_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_javascript_locals_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_typescript_locals_query_cache = {0};
static struct editorSyntaxQueryCacheEntry g_html_injection_query_cache = {0};

static struct {
	int enabled;
	uint32_t query_match_limit;
	uint64_t query_time_budget_ns;
	uint64_t parse_time_budget_ns;
} g_editor_syntax_budget_overrides = {0};

static const char editor_builtin_c_highlights_query[] =
		"(comment) @comment\n"
		"(string_literal) @string\n"
		"(char_literal) @string\n"
		"(number_literal) @number\n"
		"[(true) (false)] @constant\n"
		"(primitive_type) @type\n"
		"(type_identifier) @type\n"
		"(call_expression function: (identifier) @function)\n"
		"(function_declarator (identifier) @function)\n"
		"[(preproc_include) (preproc_def) (preproc_function_def) (preproc_call)] @preprocessor\n"
		"[\"if\" \"else\" \"switch\" \"case\" \"default\" \"while\" \"for\" \"do\"\n"
		" \"return\" \"break\" \"continue\" \"goto\" \"typedef\" \"struct\" \"enum\" \"union\"\n"
		" \"sizeof\" \"static\" \"extern\" \"inline\" \"const\" \"volatile\"] @keyword\n";

static const char editor_builtin_cpp_highlights_query[] =
		"(comment) @comment\n"
		"[(string_literal) (raw_string_literal)] @string\n"
		"(char_literal) @string\n"
		"(number_literal) @number\n"
		"[(true) (false)] @constant\n"
		"(null) @constant\n"
		"\"nullptr\" @constant\n"
		"(this) @constant\n"
		"(primitive_type) @type\n"
		"(type_identifier) @type\n"
		"(auto) @type\n"
		"(call_expression function: (identifier) @function)\n"
		"(function_declarator declarator: (identifier) @function)\n"
		"(function_declarator declarator: (field_identifier) @function)\n"
		"[(preproc_include) (preproc_def) (preproc_function_def) (preproc_call)] @preprocessor\n"
		"[\"if\" \"else\" \"switch\" \"case\" \"default\" \"while\" \"for\" \"do\"\n"
		" \"return\" \"break\" \"continue\" \"goto\" \"typedef\" \"struct\" \"enum\" \"union\"\n"
		" \"sizeof\" \"static\" \"extern\" \"inline\" \"const\" \"volatile\"\n"
		" \"class\" \"namespace\" \"template\" \"typename\" \"using\" \"virtual\"\n"
		" \"public\" \"private\" \"protected\" \"friend\" \"explicit\" \"override\"\n"
		" \"final\" \"new\" \"delete\" \"try\" \"catch\" \"throw\" \"noexcept\"\n"
		" \"constexpr\" \"constinit\" \"consteval\" \"mutable\" \"concept\" \"requires\"\n"
		" \"co_await\" \"co_return\" \"co_yield\"] @keyword\n";

static const char editor_builtin_go_highlights_query[] =
		"(call_expression function: (identifier) @function)\n"
		"(call_expression function: (selector_expression field: (field_identifier) @function.method))\n"
		"(function_declaration name: (identifier) @function)\n"
		"(method_declaration name: (field_identifier) @function.method)\n"
		"(type_identifier) @type\n"
		"(field_identifier) @property\n"
		"(identifier) @variable\n"
		"[(interpreted_string_literal) (raw_string_literal) (rune_literal)] @string\n"
		"[(int_literal) (float_literal) (imaginary_literal)] @number\n"
		"[(true) (false) (nil) (iota)] @constant.builtin\n"
		"(comment) @comment\n"
		"[\"break\" \"case\" \"chan\" \"const\" \"continue\" \"default\" \"defer\" \"else\"\n"
		" \"fallthrough\" \"for\" \"func\" \"go\" \"goto\" \"if\" \"import\" \"interface\"\n"
		" \"map\" \"package\" \"range\" \"return\" \"select\" \"struct\" \"switch\" \"type\"\n"
		" \"var\"] @keyword\n";

static const char editor_builtin_shell_highlights_query[] =
		"(comment) @comment\n"
		"[(string) (raw_string) (heredoc_body) (heredoc_start)] @string\n"
		"(command_name) @command\n"
		"(function_definition name: (word) @function)\n"
		"(variable_name) @variable\n"
		"(file_descriptor) @number\n"
		"[\"if\" \"then\" \"else\" \"elif\" \"fi\" \"for\" \"in\" \"do\" \"done\"\n"
		" \"while\" \"until\" \"case\" \"esac\" \"function\"] @keyword\n"
		"[\"$\" \"&&\" \">\" \">>\" \"<\" \"|\"] @operator\n";

static const char editor_builtin_html_highlights_query[] =
		"(tag_name) @tag\n"
		"(attribute_name) @attribute\n"
		"(attribute_value) @string\n"
		"(comment) @comment\n"
		"[(\"<\") (\">\") (\"</\") (\"/>\")] @punctuation.bracket\n";

static const char editor_builtin_javascript_highlights_query[] =
		"(identifier) @variable\n"
		"(property_identifier) @property\n"
		"(comment) @comment\n"
		"[(string) (template_string)] @string\n"
		"(number) @number\n"
		"[(function_declaration name: (identifier) @function)]\n"
		"[(\"function\") (\"return\") (\"if\") (\"else\") (\"for\") (\"while\")] @keyword\n";

static const char editor_builtin_typescript_highlights_query[] =
		"(identifier) @variable\n"
		"(property_identifier) @property\n"
		"(comment) @comment\n"
		"[(string) (template_string)] @string\n"
		"(number) @number\n"
		"(type_identifier) @type\n"
		"(predefined_type) @type\n"
		"[(function_declaration name: (identifier) @function)]\n"
		"[(\"function\") (\"return\") (\"if\") (\"else\") (\"for\") (\"while\")] @keyword\n"
		"[\"abstract\" \"declare\" \"enum\" \"export\" \"implements\" \"interface\"\n"
		" \"keyof\" \"namespace\" \"private\" \"protected\" \"public\" \"type\"\n"
		" \"readonly\" \"override\" \"satisfies\"] @keyword\n";

static const char editor_builtin_javascript_locals_query[] =
		"[(statement_block) (function_expression) (arrow_function)\n"
		" (function_declaration) (method_definition)] @local.scope\n"
		"(pattern/identifier) @local.definition\n"
		"(variable_declarator name: (identifier) @local.definition)\n"
		"(identifier) @local.reference\n";

static const char editor_builtin_typescript_locals_query[] =
		"[(statement_block) (function_expression) (arrow_function)\n"
		" (function_declaration) (method_definition)] @local.scope\n"
		"(pattern/identifier) @local.definition\n"
		"(variable_declarator name: (identifier) @local.definition)\n"
		"(required_parameter (identifier) @local.definition)\n"
		"(optional_parameter (identifier) @local.definition)\n"
		"(identifier) @local.reference\n";

static const char editor_builtin_css_highlights_query[] =
		"(comment) @comment\n"
		"(tag_name) @tag\n"
		"(property_name) @property\n"
		"(function_name) @function\n"
		"(string_value) @string\n"
		"[(integer_value) (float_value)] @number\n"
		"(attribute_name) @attribute\n"
		"(class_name) @property\n"
		"(id_name) @property\n"
		"(at_keyword) @keyword\n";

static const char editor_builtin_json_highlights_query[] =
		"(pair key: (_) @string)\n"
		"(string) @string\n"
		"(number) @number\n"
		"[(null) (true) (false)] @constant\n"
		"(comment) @comment\n";

static const char editor_builtin_python_highlights_query[] =
		"(comment) @comment\n"
		"(string) @string\n"
		"[(integer) (float)] @number\n"
		"[(true) (false) (none)] @constant\n"
		"(call function: (identifier) @function)\n"
		"(call function: (attribute attribute: (identifier) @function.method))\n"
		"(function_definition name: (identifier) @function)\n"
		"(class_definition name: (identifier) @type)\n"
		"(decorator) @function\n"
		"(attribute attribute: (identifier) @property)\n"
		"(type (identifier) @type)\n"
		"[\"and\" \"as\" \"assert\" \"async\" \"await\" \"break\" \"class\" \"continue\"\n"
		" \"def\" \"del\" \"elif\" \"else\" \"except\" \"finally\" \"for\" \"from\"\n"
		" \"global\" \"if\" \"import\" \"in\" \"is\" \"lambda\" \"nonlocal\" \"not\"\n"
		" \"or\" \"pass\" \"raise\" \"return\" \"try\" \"while\" \"with\" \"yield\"\n"
		" \"match\" \"case\"] @keyword\n";

static const char editor_builtin_html_injections_query[] =
		"((script_element (raw_text) @injection.content)\n"
		" (#set! injection.language \"javascript\"))\n"
		"((style_element (raw_text) @injection.content)\n"
		" (#set! injection.language \"css\"))\n";

static const char *const g_c_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/c/queries/highlights.scm"
};

static const char *const g_cpp_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/c/queries/highlights.scm",
	"vendor/tree_sitter/grammars/cpp/queries/highlights.scm"
};

static const char *const g_go_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/go/queries/highlights.scm"
};

static const char *const g_shell_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/bash/queries/highlights.scm"
};

static const char *const g_html_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/html/queries/highlights.scm"
};

static const char *const g_javascript_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/javascript/queries/highlights.scm",
	"vendor/tree_sitter/grammars/javascript/queries/highlights-jsx.scm",
	"vendor/tree_sitter/grammars/javascript/queries/highlights-params.scm"
};

static const char *const g_typescript_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/javascript/queries/highlights.scm",
	"vendor/tree_sitter/grammars/javascript/queries/highlights-jsx.scm",
	"vendor/tree_sitter/grammars/javascript/queries/highlights-params.scm",
	"vendor/tree_sitter/grammars/typescript/queries/highlights.scm"
};

static const char *const g_javascript_locals_query_paths[] = {
	"vendor/tree_sitter/grammars/javascript/queries/locals.scm"
};

static const char *const g_typescript_locals_query_paths[] = {
	"vendor/tree_sitter/grammars/javascript/queries/locals.scm",
	"vendor/tree_sitter/grammars/typescript/queries/locals.scm"
};

static const char *const g_css_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/css/queries/highlights.scm"
};

static const char *const g_json_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/json/queries/highlights.scm"
};

static const char *const g_python_highlight_query_paths[] = {
	"vendor/tree_sitter/grammars/python/queries/highlights.scm"
};

static const char *const g_html_injection_query_paths[] = {
	"vendor/tree_sitter/grammars/html/queries/injections.scm"
};

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

static char *editorSyntaxReadFileDup(const char *path, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (path == NULL) {
		return NULL;
	}

	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		return NULL;
	}

	size_t cap = 1024;
	size_t len = 0;
	char *buf = malloc(cap);
	if (buf == NULL) {
		fclose(fp);
		return NULL;
	}

	for (;;) {
		if (len + 1 >= cap) {
			size_t new_cap = cap * 2;
			if (new_cap <= cap) {
				free(buf);
				fclose(fp);
				return NULL;
			}
			char *grown = realloc(buf, new_cap);
			if (grown == NULL) {
				free(buf);
				fclose(fp);
				return NULL;
			}
			buf = grown;
			cap = new_cap;
		}

		size_t remaining = cap - len - 1;
		size_t nread = fread(buf + len, 1, remaining, fp);
		len += nread;
		if (nread < remaining) {
			if (ferror(fp)) {
				free(buf);
				fclose(fp);
				return NULL;
			}
			break;
		}
	}

	fclose(fp);
	buf[len] = '\0';
	if (len_out != NULL) {
		*len_out = len;
	}
	return buf;
}

static int editorSyntaxAppendBytes(char **buf, size_t *len, size_t *cap,
		const char *chunk, size_t chunk_len) {
	if (buf == NULL || len == NULL || cap == NULL || (chunk_len > 0 && chunk == NULL)) {
		return 0;
	}

	size_t needed = *len + chunk_len + 1;
	if (needed > *cap) {
		size_t new_cap = *cap == 0 ? 1024 : *cap;
		while (new_cap < needed) {
			size_t grown = new_cap * 2;
			if (grown <= new_cap) {
				return 0;
			}
			new_cap = grown;
		}
		char *grown_buf = realloc(*buf, new_cap);
		if (grown_buf == NULL) {
			return 0;
		}
		*buf = grown_buf;
		*cap = new_cap;
	}

	if (chunk_len > 0) {
		memcpy(*buf + *len, chunk, chunk_len);
		*len += chunk_len;
	}
	(*buf)[*len] = '\0';
	return 1;
}

static char *editorSyntaxReadFilesConcat(const char *const *paths, int path_count,
		size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (paths == NULL || path_count <= 0) {
		return NULL;
	}

	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;
	int loaded_any = 0;

	for (int i = 0; i < path_count; i++) {
		size_t file_len = 0;
		char *file = editorSyntaxReadFileDup(paths[i], &file_len);
		if (file == NULL) {
			continue;
		}

		if (!editorSyntaxAppendBytes(&buf, &len, &cap, file, file_len)) {
			free(file);
			free(buf);
			return NULL;
		}
		if (!editorSyntaxAppendBytes(&buf, &len, &cap, "\n", 1)) {
			free(file);
			free(buf);
			return NULL;
		}
		loaded_any = 1;
		free(file);
	}

	if (!loaded_any) {
		free(buf);
		return NULL;
	}
	if (len_out != NULL) {
		*len_out = len;
	}
	return buf;
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
		case EDITOR_SYNTAX_TYPESCRIPT:
			return tree_sitter_typescript();
		case EDITOR_SYNTAX_CSS:
			return tree_sitter_css();
		case EDITOR_SYNTAX_JSON:
			return tree_sitter_json();
		case EDITOR_SYNTAX_PYTHON:
			return tree_sitter_python();
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
			}
		}
	}

	*capture_roles_out = capture_roles;
	*capture_count_out = capture_count;
	return 1;
}

static int editorSyntaxPopulateInjectionPatternLanguages(TSQuery *query,
		char ***languages_out, uint32_t *pattern_count_out) {
	if (query == NULL || languages_out == NULL || pattern_count_out == NULL) {
		return 0;
	}
	*languages_out = NULL;
	*pattern_count_out = 0;

	uint32_t pattern_count = ts_query_pattern_count(query);
	char **languages = NULL;
	if (pattern_count > 0) {
		languages = calloc(pattern_count, sizeof(*languages));
		if (languages == NULL) {
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
							for (uint32_t j = 0; j < pattern_count; j++) {
								free(languages[j]);
							}
							free(languages);
							return 0;
						}
						memcpy(dup, value, value_len);
						dup[value_len] = '\0';
						free(languages[pattern_idx]);
						languages[pattern_idx] = dup;
					}
				}
			}
			i++;
		}
	}

	*languages_out = languages;
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
	if (cache->pattern_injection_languages != NULL) {
		for (uint32_t i = 0; i < cache->pattern_count; i++) {
			free(cache->pattern_injection_languages[i]);
		}
	}
	free(cache->pattern_injection_languages);
	cache->pattern_injection_languages = NULL;
	cache->capture_count = 0;
	cache->pattern_count = 0;
	cache->load_attempted = 0;
}

static int editorSyntaxEnsureQueryCache(struct editorSyntaxQueryCacheEntry *cache,
		enum editorSyntaxLanguage language,
		const char *const *query_paths,
		int query_path_count,
		const char *fallback_query,
		int want_capture_classes,
		int want_locals_roles,
		int want_injection_roles,
		int want_injection_languages) {
	if (cache == NULL || (query_paths == NULL && fallback_query == NULL)) {
		return 0;
	}
	if (cache->load_attempted) {
		return cache->query != NULL;
	}
	cache->load_attempted = 1;

	TSQuery *query = NULL;
	size_t query_len = 0;
	char *file_query = editorSyntaxReadFilesConcat(query_paths, query_path_count, &query_len);
	if (file_query != NULL) {
		(void)editorSyntaxCompileQuery(language, file_query, query_len, &query);
		free(file_query);
	}

	if (query == NULL && fallback_query != NULL) {
		(void)editorSyntaxCompileQuery(language, fallback_query, strlen(fallback_query), &query);
	}
	if (query == NULL) {
		return 0;
	}

	enum editorSyntaxHighlightClass *capture_classes = NULL;
	uint8_t *capture_roles = NULL;
	char **pattern_languages = NULL;
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

	if (want_injection_languages &&
			!editorSyntaxPopulateInjectionPatternLanguages(query, &pattern_languages,
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
			if (pattern_languages != NULL) {
				for (uint32_t i = 0; i < pattern_count; i++) {
					free(pattern_languages[i]);
				}
			}
			free(pattern_languages);
			ts_query_delete(query);
			return 0;
		}
	}

	cache->query = query;
	cache->capture_classes = capture_classes;
	cache->capture_roles = capture_roles;
	cache->pattern_injection_languages = pattern_languages;
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
					g_c_highlight_query_paths,
					(int)(sizeof(g_c_highlight_query_paths) / sizeof(g_c_highlight_query_paths[0])),
					editor_builtin_c_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_CPP:
			return editorSyntaxEnsureQueryCache(&g_cpp_highlight_query_cache, EDITOR_SYNTAX_CPP,
					g_cpp_highlight_query_paths,
					(int)(sizeof(g_cpp_highlight_query_paths) /
						sizeof(g_cpp_highlight_query_paths[0])),
					editor_builtin_cpp_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_GO:
			return editorSyntaxEnsureQueryCache(&g_go_highlight_query_cache, EDITOR_SYNTAX_GO,
					g_go_highlight_query_paths,
					(int)(sizeof(g_go_highlight_query_paths) /
						sizeof(g_go_highlight_query_paths[0])),
					editor_builtin_go_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_SHELL:
			return editorSyntaxEnsureQueryCache(&g_shell_highlight_query_cache,
					EDITOR_SYNTAX_SHELL,
					g_shell_highlight_query_paths,
					(int)(sizeof(g_shell_highlight_query_paths) /
						sizeof(g_shell_highlight_query_paths[0])),
					editor_builtin_shell_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_HTML:
			return editorSyntaxEnsureQueryCache(&g_html_highlight_query_cache, EDITOR_SYNTAX_HTML,
					g_html_highlight_query_paths,
					(int)(sizeof(g_html_highlight_query_paths) /
						sizeof(g_html_highlight_query_paths[0])),
					editor_builtin_html_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JAVASCRIPT:
			return editorSyntaxEnsureQueryCache(&g_javascript_highlight_query_cache,
					EDITOR_SYNTAX_JAVASCRIPT,
					g_javascript_highlight_query_paths,
					(int)(sizeof(g_javascript_highlight_query_paths) /
						sizeof(g_javascript_highlight_query_paths[0])),
					editor_builtin_javascript_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_TYPESCRIPT:
			return editorSyntaxEnsureQueryCache(&g_typescript_highlight_query_cache,
					EDITOR_SYNTAX_TYPESCRIPT,
					g_typescript_highlight_query_paths,
					(int)(sizeof(g_typescript_highlight_query_paths) /
						sizeof(g_typescript_highlight_query_paths[0])),
					editor_builtin_typescript_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_CSS:
			return editorSyntaxEnsureQueryCache(&g_css_highlight_query_cache,
					EDITOR_SYNTAX_CSS,
					g_css_highlight_query_paths,
					(int)(sizeof(g_css_highlight_query_paths) /
						sizeof(g_css_highlight_query_paths[0])),
					editor_builtin_css_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_JSON:
			return editorSyntaxEnsureQueryCache(&g_json_highlight_query_cache,
					EDITOR_SYNTAX_JSON,
					g_json_highlight_query_paths,
					(int)(sizeof(g_json_highlight_query_paths) /
						sizeof(g_json_highlight_query_paths[0])),
					editor_builtin_json_highlights_query,
					1, 0, 0, 0);
		case EDITOR_SYNTAX_PYTHON:
			return editorSyntaxEnsureQueryCache(&g_python_highlight_query_cache,
					EDITOR_SYNTAX_PYTHON,
					g_python_highlight_query_paths,
					(int)(sizeof(g_python_highlight_query_paths) /
						sizeof(g_python_highlight_query_paths[0])),
					editor_builtin_python_highlights_query,
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
					g_javascript_locals_query_paths,
					(int)(sizeof(g_javascript_locals_query_paths) /
						sizeof(g_javascript_locals_query_paths[0])),
					editor_builtin_javascript_locals_query,
					0, 1, 0, 0);
		case EDITOR_SYNTAX_TYPESCRIPT:
			return editorSyntaxEnsureQueryCache(&g_typescript_locals_query_cache,
					EDITOR_SYNTAX_TYPESCRIPT,
					g_typescript_locals_query_paths,
					(int)(sizeof(g_typescript_locals_query_paths) /
						sizeof(g_typescript_locals_query_paths[0])),
					editor_builtin_typescript_locals_query,
					0, 1, 0, 0);
		default:
			return 0;
	}
}

static int editorSyntaxEnsureHtmlInjectionQuery(void) {
	return editorSyntaxEnsureQueryCache(&g_html_injection_query_cache,
			EDITOR_SYNTAX_HTML,
			g_html_injection_query_paths,
			(int)(sizeof(g_html_injection_query_paths) /
				sizeof(g_html_injection_query_paths[0])),
			editor_builtin_html_injections_query,
			0, 0, 1, 1);
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
		case EDITOR_SYNTAX_TYPESCRIPT:
			return &g_typescript_highlight_query_cache;
		case EDITOR_SYNTAX_CSS:
			return &g_css_highlight_query_cache;
		case EDITOR_SYNTAX_JSON:
			return &g_json_highlight_query_cache;
		case EDITOR_SYNTAX_PYTHON:
			return &g_python_highlight_query_cache;
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
		&g_typescript_highlight_query_cache,
		&g_css_highlight_query_cache,
		&g_json_highlight_query_cache,
		&g_python_highlight_query_cache,
		&g_javascript_locals_query_cache,
		&g_typescript_locals_query_cache,
		&g_html_injection_query_cache
	};

	for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
		if (all[i]->query == query) {
			return all[i];
		}
	}
	return NULL;
}
