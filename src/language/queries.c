/* Translation unit for tree-sitter query loading and the per-language compiled-query
 * caches. Paired with syntax.c (parser/edit/orchestration) via syntax_internal.h.
 */

#include "language/syntax_internal.h"

#include "language/languages.h"

#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct editorSyntaxQueryCacheEntry
		g_query_caches[EDITOR_SYNTAX_LANGUAGE_COUNT]
		              [EDITOR_SYNTAX_QUERY_CACHE_KIND_COUNT] = {0};

static struct editorSyntaxQueryCacheEntry *editorSyntaxQueryCacheSlot(
		enum editorSyntaxLanguage language,
		enum editorSyntaxQueryCacheKind kind) {
	if ((int)language <= 0 || (int)language >= EDITOR_SYNTAX_LANGUAGE_COUNT) {
		return NULL;
	}
	if ((int)kind < 0 || (int)kind >= EDITOR_SYNTAX_QUERY_CACHE_KIND_COUNT) {
		return NULL;
	}
	return &g_query_caches[(int)language][(int)kind];
}

static struct {
	int enabled;
	uint32_t query_match_limit;
	uint64_t query_time_budget_ns;
	uint64_t parse_time_budget_ns;
} g_editor_syntax_budget_overrides = {0};

static struct editorSyntaxQueryCompileError g_last_query_compile_error = {0};
static unsigned int g_last_query_compile_error_generation = 0;
static unsigned int g_drained_query_compile_error_generation = 0;
static uint64_t g_query_unavailable_reported[ROTIDE_SYNTAX_QUERY_KIND_COUNT] = {0};

int editorSyntaxStringEquals(const char *s, size_t len, const char *literal) {
	if (s == NULL || literal == NULL) {
		return 0;
	}
	size_t lit_len = strlen(literal);
	if (len != lit_len) {
		return 0;
	}
	return memcmp(s, literal, len) == 0;
}

int editorSyntaxLengthFitsTreeSitter(size_t len) {
	return len <= UINT32_MAX;
}

static uint64_t editorSyntaxMonotonicNanos(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t editorSyntaxComputeDeadlineNs(uint64_t budget_ns) {
	if (budget_ns == 0) {
		return 0;
	}
	uint64_t now = editorSyntaxMonotonicNanos();
	if (now == 0 || budget_ns > UINT64_MAX - now) {
		return 0;
	}
	return now + budget_ns;
}

bool editorSyntaxParseProgressCallback(TSParseState *state) {
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

bool editorSyntaxQueryProgressCallback(TSQueryCursorState *state) {
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

const char *editorSyntaxSourceRead(void *payload, uint32_t byte_index,
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

struct editorSyntaxBudgetConfig editorSyntaxBudgetConfigForMode(
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

struct editorSyntaxCaptureRule {
	const char *prefix;
	enum editorSyntaxHighlightClass highlight_class;
};

static const struct editorSyntaxCaptureRule g_capture_rules[] = {
	{"keyword.conditional.ternary", EDITOR_SYNTAX_HL_KEYWORD},
	{"reference.implementation", EDITOR_SYNTAX_HL_TYPE},
	{"function.method.builtin", EDITOR_SYNTAX_HL_FUNCTION},
	{"definition.enum_variant", EDITOR_SYNTAX_HL_CONSTANT},
	{"reference.enum_variant", EDITOR_SYNTAX_HL_CONSTANT},
	{"punctuation.delimiter", EDITOR_SYNTAX_HL_PUNCTUATION},
	{"comment.documentation", EDITOR_SYNTAX_HL_COMMENT},
	{"string.documentation", EDITOR_SYNTAX_HL_STRING},
	{"definition.interface", EDITOR_SYNTAX_HL_TYPE},
	{"reference.interface", EDITOR_SYNTAX_HL_TYPE},
	{"punctuation.special", EDITOR_SYNTAX_HL_PUNCTUATION},
	{"punctuation.bracket", EDITOR_SYNTAX_HL_PUNCTUATION},
	{"property.definition", EDITOR_SYNTAX_HL_PROPERTY},
	{"keyword.conditional", EDITOR_SYNTAX_HL_KEYWORD},
	{"definition.constant", EDITOR_SYNTAX_HL_CONSTANT},
	{"definition.function", EDITOR_SYNTAX_HL_FUNCTION},
	{"definition.operator", EDITOR_SYNTAX_HL_OPERATOR},
	{"definition.property", EDITOR_SYNTAX_HL_PROPERTY},
	{"definition.variable", EDITOR_SYNTAX_HL_VARIABLE},
	{"variable.parameter", EDITOR_SYNTAX_HL_PARAMETER},
	{"string.special.key", EDITOR_SYNTAX_HL_STRING},
	{"constant.character", EDITOR_SYNTAX_HL_CONSTANT},
	{"keyword.exception", EDITOR_SYNTAX_HL_KEYWORD},
	{"keyword.directive", EDITOR_SYNTAX_HL_KEYWORD},
	{"definition.method", EDITOR_SYNTAX_HL_FUNCTION},
	{"definition.module", EDITOR_SYNTAX_HL_MODULE},
	{"definition.object", EDITOR_SYNTAX_HL_TYPE},
	{"variable.builtin", EDITOR_SYNTAX_HL_CONSTANT},
	{"reference.module", EDITOR_SYNTAX_HL_MODULE},
	{"keyword.operator", EDITOR_SYNTAX_HL_KEYWORD},
	{"keyword.modifier", EDITOR_SYNTAX_HL_KEYWORD},
	{"keyword.function", EDITOR_SYNTAX_HL_KEYWORD},
	{"function.builtin", EDITOR_SYNTAX_HL_FUNCTION},
	{"function.special", EDITOR_SYNTAX_HL_FUNCTION},
	{"definition.class", EDITOR_SYNTAX_HL_TYPE},
	{"definition.field", EDITOR_SYNTAX_HL_PROPERTY},
	{"definition.macro", EDITOR_SYNTAX_HL_FUNCTION},
	{"constant.builtin", EDITOR_SYNTAX_HL_CONSTANT},
	{"variable.member", EDITOR_SYNTAX_HL_PROPERTY},
	{"type.definition", EDITOR_SYNTAX_HL_TYPE},
	{"reference.field", EDITOR_SYNTAX_HL_PROPERTY},
	{"reference.class", EDITOR_SYNTAX_HL_TYPE},
	{"definition.type", EDITOR_SYNTAX_HL_TYPE},
	{"function.method", EDITOR_SYNTAX_HL_FUNCTION},
	{"type.qualifier", EDITOR_SYNTAX_HL_TYPE},
	{"string.special", EDITOR_SYNTAX_HL_STRING},
	{"reference.type", EDITOR_SYNTAX_HL_TYPE},
	{"reference.call", EDITOR_SYNTAX_HL_FUNCTION},
	{"module.builtin", EDITOR_SYNTAX_HL_MODULE},
	{"keyword.import", EDITOR_SYNTAX_HL_KEYWORD},
	{"keyword.repeat", EDITOR_SYNTAX_HL_KEYWORD},
	{"keyword.return", EDITOR_SYNTAX_HL_KEYWORD},
	{"function.macro", EDITOR_SYNTAX_HL_FUNCTION},
	{"string.symbol", EDITOR_SYNTAX_HL_STRING},
	{"string.escape", EDITOR_SYNTAX_HL_STRING},
	{"keyword.debug", EDITOR_SYNTAX_HL_KEYWORD},
	{"function.call", EDITOR_SYNTAX_HL_FUNCTION},
	{"type.builtin", EDITOR_SYNTAX_HL_TYPE},
	{"string.regex", EDITOR_SYNTAX_HL_STRING},
	{"storageclass", EDITOR_SYNTAX_HL_PREPROCESSOR},
	{"preprocessor", EDITOR_SYNTAX_HL_PREPROCESSOR},
	{"number.float", EDITOR_SYNTAX_HL_NUMBER},
	{"keyword.type", EDITOR_SYNTAX_HL_KEYWORD},
	{"punctuation", EDITOR_SYNTAX_HL_PUNCTUATION},
	{"method.call", EDITOR_SYNTAX_HL_FUNCTION},
	{"conditional", EDITOR_SYNTAX_HL_KEYWORD},
	{"constructor", EDITOR_SYNTAX_HL_TYPE},
	{"parameter", EDITOR_SYNTAX_HL_PARAMETER},
	{"namespace", EDITOR_SYNTAX_HL_MODULE},
	{"exception", EDITOR_SYNTAX_HL_KEYWORD},
	{"delimiter", EDITOR_SYNTAX_HL_PUNCTUATION},
	{"character", EDITOR_SYNTAX_HL_STRING},
	{"attribute", EDITOR_SYNTAX_HL_PREPROCESSOR},
	{"variable", EDITOR_SYNTAX_HL_VARIABLE},
	{"property", EDITOR_SYNTAX_HL_PROPERTY},
	{"operator", EDITOR_SYNTAX_HL_OPERATOR},
	{"function", EDITOR_SYNTAX_HL_FUNCTION},
	{"constant", EDITOR_SYNTAX_HL_CONSTANT},
	{"preproc", EDITOR_SYNTAX_HL_PREPROCESSOR},
	{"keyword", EDITOR_SYNTAX_HL_KEYWORD},
	{"include", EDITOR_SYNTAX_HL_KEYWORD},
	{"comment", EDITOR_SYNTAX_HL_COMMENT},
	{"command", EDITOR_SYNTAX_HL_FUNCTION},
	{"boolean", EDITOR_SYNTAX_HL_CONSTANT},
	{"string", EDITOR_SYNTAX_HL_STRING},
	{"repeat", EDITOR_SYNTAX_HL_KEYWORD},
	{"number", EDITOR_SYNTAX_HL_NUMBER},
	{"module", EDITOR_SYNTAX_HL_MODULE},
	{"method", EDITOR_SYNTAX_HL_FUNCTION},
	{"label", EDITOR_SYNTAX_HL_FUNCTION},
	{"float", EDITOR_SYNTAX_HL_NUMBER},
	{"type", EDITOR_SYNTAX_HL_TYPE},
	{"tag", EDITOR_SYNTAX_HL_TYPE},
	{"doc", EDITOR_SYNTAX_HL_COMMENT}
};

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

	for (size_t i = 0; i < sizeof(g_capture_rules) / sizeof(g_capture_rules[0]); i++) {
		if (editorSyntaxCaptureNameHasPrefix(name, len, g_capture_rules[i].prefix)) {
			return g_capture_rules[i].highlight_class;
		}
	}

	return EDITOR_SYNTAX_HL_NONE;
}

const TSLanguage *editorSyntaxLanguageObject(enum editorSyntaxLanguage language) {
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	if (def == NULL || def->ts_factory == NULL) {
		return NULL;
	}
	return def->ts_factory();
}

static const char *editorSyntaxQueryErrorName(TSQueryError error_type) {
	switch (error_type) {
		case TSQueryErrorNone:
			return "none";
		case TSQueryErrorSyntax:
			return "syntax";
		case TSQueryErrorNodeType:
			return "node-type";
		case TSQueryErrorField:
			return "field";
		case TSQueryErrorCapture:
			return "capture";
		case TSQueryErrorStructure:
			return "structure";
		case TSQueryErrorLanguage:
			return "language";
		default:
			return "unknown";
	}
}

enum editorSyntaxQueryCompileLog {
	EDITOR_SYNTAX_QUERY_COMPILE_LOG_QUIET = 0,
	EDITOR_SYNTAX_QUERY_COMPILE_LOG_ERROR = 1
};

static void editorSyntaxCopyQueryErrorContext(
		const char *query_source, size_t query_len, uint32_t error_offset,
		char context[ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX + 1]) {
	if (context == NULL) {
		return;
	}
	context[0] = '\0';
	if (query_source == NULL || query_len == 0) {
		return;
	}

	size_t start = error_offset;
	if (start > query_len) {
		start = query_len;
	}
	if (start > ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX / 4U) {
		start -= ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX / 4U;
	} else {
		start = 0;
	}

	size_t len = query_len - start;
	if (len > ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX) {
		len = ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX;
	}
	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)query_source[start + i];
		context[i] = (ch < ' ' || ch == 0x7f) ? ' ' : (char)ch;
	}
	context[len] = '\0';
}

static void editorSyntaxRecordQueryCompileError(enum editorSyntaxLanguage language,
		const char *query_source, size_t query_len, uint32_t error_offset,
		TSQueryError error_type, enum editorSyntaxQueryCompileLog log_mode) {
	g_last_query_compile_error.has_error = 1;
	g_last_query_compile_error.language = language;
	g_last_query_compile_error.error_offset = error_offset;
	g_last_query_compile_error.error_type = (int)error_type;
	editorSyntaxCopyQueryErrorContext(query_source, query_len, error_offset,
			g_last_query_compile_error.context);
	g_last_query_compile_error_generation++;

#ifndef NDEBUG
	if (log_mode == EDITOR_SYNTAX_QUERY_COMPILE_LOG_ERROR) {
		fprintf(stderr,
				"rotide: tree-sitter query compile failed: language=%d offset=%u "
				"error=%s context=\"%s\"\n",
				(int)language, (unsigned int)error_offset,
				editorSyntaxQueryErrorName(error_type), g_last_query_compile_error.context);
	}
#else
	(void)log_mode;
#endif
}

int editorSyntaxCopyLastQueryCompileError(struct editorSyntaxQueryCompileError *error_out) {
	if (error_out == NULL) {
		return 0;
	}
	*error_out = g_last_query_compile_error;
	return g_last_query_compile_error.has_error;
}

int editorSyntaxDrainLastQueryCompileError(struct editorSyntaxQueryCompileError *error_out) {
	if (error_out != NULL) {
		*error_out = g_last_query_compile_error;
	}
	if (!g_last_query_compile_error.has_error ||
			g_drained_query_compile_error_generation ==
					g_last_query_compile_error_generation) {
		return 0;
	}
	g_drained_query_compile_error_generation = g_last_query_compile_error_generation;
	return 1;
}

void editorSyntaxTestResetLastQueryCompileError(void) {
	memset(&g_last_query_compile_error, 0, sizeof(g_last_query_compile_error));
	g_last_query_compile_error_generation = 0;
	g_drained_query_compile_error_generation = 0;
}

static int editorSyntaxCompileQuery(enum editorSyntaxLanguage language,
		const char *query_source, size_t query_len, TSQuery **query_out,
		enum editorSyntaxQueryCompileLog log_mode) {
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
		editorSyntaxRecordQueryCompileError(language, query_source, query_len,
				error_offset, error_type, log_mode);
		return 0;
	}

	*query_out = query;
	return 1;
}

int editorSyntaxTestCompileQueryForDiagnostics(enum editorSyntaxLanguage language,
		const char *query_source) {
	if (query_source == NULL) {
		return 0;
	}
	TSQuery *query = NULL;
	int ok = editorSyntaxCompileQuery(language, query_source, strlen(query_source), &query,
			EDITOR_SYNTAX_QUERY_COMPILE_LOG_QUIET);
	if (query != NULL) {
		ts_query_delete(query);
	}
	return ok;
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

static int editorSyntaxLocalCaptureRoleMatches(const char *name, uint32_t name_len,
		const char *role) {
	size_t role_len = strlen(role);
	if (name_len < role_len) {
		return 0;
	}
	if (strncmp(name, role, role_len) != 0) {
		return 0;
	}
	return name_len == role_len || name[role_len] == '.';
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
			if (editorSyntaxLocalCaptureRoleMatches(name, name_len, "local.scope")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_SCOPE;
			} else if (editorSyntaxLocalCaptureRoleMatches(name, name_len, "local.definition")) {
				capture_roles[i] = EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_DEFINITION;
			} else if (editorSyntaxLocalCaptureRoleMatches(name, name_len, "local.reference")) {
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
	(void)editorSyntaxCompileQuery(language, embedded_query, embedded_len, &query,
			EDITOR_SYNTAX_QUERY_COMPILE_LOG_ERROR);
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
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	if (def == NULL || def->highlight_parts == NULL || def->highlight_part_count <= 0) {
		return 0;
	}
	struct editorSyntaxQueryCacheEntry *slot =
			editorSyntaxQueryCacheSlot(language, EDITOR_SYNTAX_QUERY_CACHE_KIND_HIGHLIGHT);
	if (slot == NULL) {
		return 0;
	}
	return editorSyntaxEnsureQueryCache(slot, language,
			def->highlight_parts, def->highlight_part_count,
			1, 0, 0, 0);
}

const struct editorSyntaxQueryCacheEntry *editorSyntaxHighlightQueryCachePtr(
		enum editorSyntaxLanguage language) {
	if (!editorSyntaxEnsureHighlightQuery(language)) {
		return NULL;
	}
	const struct editorSyntaxQueryCacheEntry *cache =
			editorSyntaxQueryCacheSlot(language, EDITOR_SYNTAX_QUERY_CACHE_KIND_HIGHLIGHT);
	if (cache == NULL || cache->query == NULL || cache->capture_classes == NULL) {
		return NULL;
	}
	return cache;
}

int editorSyntaxEnsureLocalsQuery(enum editorSyntaxLanguage language) {
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	if (def == NULL || def->locals_parts == NULL || def->locals_part_count <= 0) {
		return 0;
	}
	struct editorSyntaxQueryCacheEntry *slot =
			editorSyntaxQueryCacheSlot(language, EDITOR_SYNTAX_QUERY_CACHE_KIND_LOCALS);
	if (slot == NULL) {
		return 0;
	}
	return editorSyntaxEnsureQueryCache(slot, language,
			def->locals_parts, def->locals_part_count,
			0, 1, 0, 0);
}

static int editorSyntaxEnsureInjectionQuery(enum editorSyntaxLanguage language) {
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	if (def == NULL || def->injection_parts == NULL || def->injection_part_count <= 0) {
		return 0;
	}
	struct editorSyntaxQueryCacheEntry *slot =
			editorSyntaxQueryCacheSlot(language, EDITOR_SYNTAX_QUERY_CACHE_KIND_INJECTION);
	if (slot == NULL) {
		return 0;
	}
	return editorSyntaxEnsureQueryCache(slot, language,
			def->injection_parts, def->injection_part_count,
			0, 0, 1, 1);
}

const struct editorSyntaxQueryCacheEntry *editorSyntaxInjectionQueryCachePtr(
		enum editorSyntaxLanguage language) {
	if (!editorSyntaxEnsureInjectionQuery(language)) {
		return NULL;
	}
	const struct editorSyntaxQueryCacheEntry *cache =
			editorSyntaxQueryCacheSlot(language, EDITOR_SYNTAX_QUERY_CACHE_KIND_INJECTION);
	if (cache == NULL || cache->query == NULL || cache->capture_roles == NULL ||
			cache->pattern_injection_metadata == NULL) {
		return NULL;
	}
	return cache;
}

static int editorSyntaxQueryKindIndex(enum editorSyntaxQueryKind kind) {
	switch (kind) {
		case EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT:
			return 0;
		case EDITOR_SYNTAX_QUERY_KIND_INJECTION:
			return 1;
		default:
			return -1;
	}
}

static uint64_t editorSyntaxLanguageEventBit(enum editorSyntaxLanguage language) {
	if ((int)language <= 0 || (int)language >= 64) {
		return 0;
	}
	return 1ULL << (unsigned int)language;
}

void editorSyntaxStateRecordQueryUnavailable(struct editorSyntaxState *state,
		enum editorSyntaxLanguage language,
		enum editorSyntaxQueryKind kind) {
	if (state == NULL) {
		return;
	}
	int kind_idx = editorSyntaxQueryKindIndex(kind);
	uint64_t language_bit = editorSyntaxLanguageEventBit(language);
	if (kind_idx < 0 || language_bit == 0) {
		return;
	}
	if ((g_query_unavailable_reported[kind_idx] & language_bit) != 0) {
		return;
	}
	g_query_unavailable_reported[kind_idx] |= language_bit;
	state->query_unavailable_pending = 1;
	state->query_unavailable_language = language;
	state->query_unavailable_kind = kind;
}

const struct editorSyntaxQueryCacheEntry *editorSyntaxLocalsQueryCacheForLanguage(
		enum editorSyntaxLanguage language) {
	const struct editorSyntaxQueryCacheEntry *cache =
			editorSyntaxQueryCacheSlot(language, EDITOR_SYNTAX_QUERY_CACHE_KIND_LOCALS);
	if (cache == NULL || cache->query == NULL) {
		return NULL;
	}
	return cache;
}

struct editorSyntaxQueryCacheEntry *editorSyntaxQueryCacheEntryForQuery(const TSQuery *query) {
	if (query == NULL) {
		return NULL;
	}
	for (int lang = 1; lang < EDITOR_SYNTAX_LANGUAGE_COUNT; lang++) {
		for (int kind = 0; kind < EDITOR_SYNTAX_QUERY_CACHE_KIND_COUNT; kind++) {
			struct editorSyntaxQueryCacheEntry *cache = &g_query_caches[lang][kind];
			if (cache->query == query) {
				return cache;
			}
		}
	}
	return NULL;
}

void editorSyntaxTestSetBudgetOverrides(int enabled,
		uint32_t query_match_limit,
		uint64_t query_time_budget_ns,
		uint64_t parse_time_budget_ns) {
	g_editor_syntax_budget_overrides.enabled = enabled ? 1 : 0;
	g_editor_syntax_budget_overrides.query_match_limit = query_match_limit;
	g_editor_syntax_budget_overrides.query_time_budget_ns = query_time_budget_ns;
	g_editor_syntax_budget_overrides.parse_time_budget_ns = parse_time_budget_ns;
}

void editorSyntaxTestResetBudgetOverrides(void) {
	memset(&g_editor_syntax_budget_overrides, 0, sizeof(g_editor_syntax_budget_overrides));
}

int editorSyntaxTestBudgetOverridesEnabled(void) {
	return g_editor_syntax_budget_overrides.enabled;
}

int editorSyntaxTestCaptureRuleCount(void) {
	return (int)(sizeof(g_capture_rules) / sizeof(g_capture_rules[0]));
}

int editorSyntaxTestCaptureRuleAt(int idx, const char **prefix_out,
		enum editorSyntaxHighlightClass *class_out) {
	if (prefix_out != NULL) {
		*prefix_out = NULL;
	}
	if (class_out != NULL) {
		*class_out = EDITOR_SYNTAX_HL_NONE;
	}
	if (idx < 0 || idx >= editorSyntaxTestCaptureRuleCount()) {
		return 0;
	}
	if (prefix_out != NULL) {
		*prefix_out = g_capture_rules[idx].prefix;
	}
	if (class_out != NULL) {
		*class_out = g_capture_rules[idx].highlight_class;
	}
	return 1;
}

enum editorSyntaxHighlightClass editorSyntaxTestClassFromCaptureName(const char *name) {
	if (name == NULL) {
		return EDITOR_SYNTAX_HL_NONE;
	}
	return editorSyntaxClassFromCaptureName(name, strlen(name));
}

void editorSyntaxReleaseSharedResources(void) {
	memset(g_query_unavailable_reported, 0, sizeof(g_query_unavailable_reported));
	for (int lang = 1; lang < EDITOR_SYNTAX_LANGUAGE_COUNT; lang++) {
		for (int kind = 0; kind < EDITOR_SYNTAX_QUERY_CACHE_KIND_COUNT; kind++) {
			editorSyntaxClearQueryCacheEntry(&g_query_caches[lang][kind]);
		}
	}
}
