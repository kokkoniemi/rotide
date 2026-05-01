/* Internal header shared between syntax.c (the parser/edit/orchestration TU)
 * and queries.c (the query loading/cache TU). Holds shared struct types,
 * cross-TU function declarations, and module-internal enums/macros.
 *
 * NOT a public API. Public types and lifecycle are in language/syntax.h.
 */
#ifndef ROTIDE_LANGUAGE_SYNTAX_INTERNAL_H
#define ROTIDE_LANGUAGE_SYNTAX_INTERNAL_H

#include "language/syntax.h"
#include "rotide.h"

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tree_sitter/api.h"

#define ROTIDE_SYNTAX_PERF_DEGRADED_PREDICATES_BYTES ((size_t)(512 * 1024))
#define ROTIDE_SYNTAX_PERF_DEGRADED_INJECTIONS_BYTES ((size_t)(2 * 1024 * 1024))
#define ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_NORMAL 8192U
#define ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_DEGRADED 4096U
#define ROTIDE_SYNTAX_QUERY_MATCH_LIMIT_DEGRADED_INJECTIONS 2048U

#define ROTIDE_SYNTAX_QUERY_BUDGET_NS_NORMAL (20000000ULL)
#define ROTIDE_SYNTAX_QUERY_BUDGET_NS_DEGRADED (12000000ULL)
#define ROTIDE_SYNTAX_QUERY_BUDGET_NS_DEGRADED_INJECTIONS (8000000ULL)

#define ROTIDE_SYNTAX_PARSE_BUDGET_NS_NORMAL (50000000ULL)
#define ROTIDE_SYNTAX_PARSE_BUDGET_NS_DEGRADED (30000000ULL)
#define ROTIDE_SYNTAX_PARSE_BUDGET_NS_DEGRADED_INJECTIONS (20000000ULL)

#define ROTIDE_SYNTAX_QUERY_KIND_COUNT 2
#define ROTIDE_SYNTAX_LIMIT_EVENT_CAP 16
#define ROTIDE_SYNTAX_MAX_INJECTION_TREES 16
#define ROTIDE_SYNTAX_DEFAULT_MAX_INJECTION_DEPTH 5
extern int g_editor_syntax_max_injection_depth;

enum editorSyntaxCaptureRole {
	EDITOR_SYNTAX_CAPTURE_ROLE_NONE = 0,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_SCOPE,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_DEFINITION,
	EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_REFERENCE,
	EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_CONTENT,
	EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_LANGUAGE
};

enum editorSyntaxQueryCacheKind {
	EDITOR_SYNTAX_QUERY_CACHE_KIND_HIGHLIGHT = 0,
	EDITOR_SYNTAX_QUERY_CACHE_KIND_LOCALS,
	EDITOR_SYNTAX_QUERY_CACHE_KIND_INJECTION,
	EDITOR_SYNTAX_QUERY_CACHE_KIND_COUNT
};

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
	int tree_error_reported;
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
	int query_unavailable_pending;
	enum editorSyntaxLanguage query_unavailable_language;
	enum editorSyntaxQueryKind query_unavailable_kind;
	struct editorSyntaxLimitEvent limit_events[ROTIDE_SYNTAX_LIMIT_EVENT_CAP];
	int limit_event_start;
	int limit_event_count;
	int injection_depth_exceeded_reported;
	int injection_slots_full_reported;
	int capture_truncated_unknown_reported;
	int *capture_truncated_rows;
	int capture_truncated_row_count;
	int capture_truncated_row_cap;
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

/* Cross-TU helpers exported by queries.c. */
const TSLanguage *editorSyntaxLanguageObject(enum editorSyntaxLanguage language);
int editorSyntaxStringEquals(const char *s, size_t len, const char *literal);
int editorSyntaxLengthFitsTreeSitter(size_t len);
struct editorSyntaxBudgetConfig editorSyntaxBudgetConfigForMode(
		enum editorSyntaxPerformanceMode mode);
uint64_t editorSyntaxComputeDeadlineNs(uint64_t budget_ns);
bool editorSyntaxParseProgressCallback(TSParseState *state);
bool editorSyntaxQueryProgressCallback(TSQueryCursorState *state);
const char *editorSyntaxSourceRead(void *payload, uint32_t byte_index,
		TSPoint position, uint32_t *bytes_read);

const struct editorSyntaxQueryCacheEntry *editorSyntaxHighlightQueryCachePtr(
		enum editorSyntaxLanguage language);
const struct editorSyntaxQueryCacheEntry *editorSyntaxInjectionQueryCachePtr(
		enum editorSyntaxLanguage language);
const struct editorSyntaxQueryCacheEntry *editorSyntaxLocalsQueryCacheForLanguage(
		enum editorSyntaxLanguage language);
struct editorSyntaxQueryCacheEntry *editorSyntaxQueryCacheEntryForQuery(const TSQuery *query);
int editorSyntaxEnsureLocalsQuery(enum editorSyntaxLanguage language);

void editorSyntaxStateRecordQueryUnavailable(struct editorSyntaxState *state,
		enum editorSyntaxLanguage language,
		enum editorSyntaxQueryKind kind);

#endif
