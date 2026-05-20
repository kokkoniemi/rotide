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
#include "tree_sitter/api.h"

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
struct editorSyntaxBudgetConfig
editorSyntaxBudgetConfigForMode(enum editorSyntaxPerformanceMode mode);
uint64_t editorSyntaxComputeDeadlineNs(uint64_t budget_ns);
bool editorSyntaxParseProgressCallback(TSParseState *state);
bool editorSyntaxQueryProgressCallback(TSQueryCursorState *state);
const char *editorSyntaxSourceRead(void *payload, uint32_t byte_index, TSPoint position,
                                   uint32_t *bytes_read);

const struct editorSyntaxQueryCacheEntry *
editorSyntaxHighlightQueryCachePtr(enum editorSyntaxLanguage language);
const struct editorSyntaxQueryCacheEntry *
editorSyntaxInjectionQueryCachePtr(enum editorSyntaxLanguage language);
const struct editorSyntaxQueryCacheEntry *
editorSyntaxLocalsQueryCacheForLanguage(enum editorSyntaxLanguage language);
struct editorSyntaxQueryCacheEntry *editorSyntaxQueryCacheEntryForQuery(const TSQuery *query);
int editorSyntaxEnsureLocalsQuery(enum editorSyntaxLanguage language);

void editorSyntaxStateRecordQueryUnavailable(struct editorSyntaxState *state,
                                             enum editorSyntaxLanguage language,
                                             enum editorSyntaxQueryKind kind);

/*
 * Limit-event and performance-mode helpers implemented in
 * syntax_budget.c. The injection workflow records "depth exceeded" and
 * "slots full" events through these helpers; the parse workflow records
 * "tree has error" through RecordParseTreeHasError; ApplyPerformanceMode
 * is called from configure-for-source-length and from edit/parse paths
 * that need to refresh perf flags after a size change.
 */
void editorSyntaxStateRecordInjectionDepthExceeded(struct editorSyntaxState *state,
                                                   enum editorSyntaxLanguage language, int depth);
void editorSyntaxStateRecordInjectionSlotsFull(struct editorSyntaxState *state,
                                               enum editorSyntaxLanguage language);
void editorSyntaxStateRecordParseTreeHasError(struct editorSyntaxState *state,
                                              enum editorSyntaxLanguage language);
void editorSyntaxStateApplyPerformanceMode(struct editorSyntaxState *state, size_t source_len);

/*
 * Injection workflow implemented in syntax_injections.c. The host parse
 * path drives these: after a host-tree edit, ApplyInputEdit propagates
 * the edit deltas into all live injection trees; ParseInjections then
 * re-runs the injection query against the host tree and reparses the
 * affected ranges. Injected-tree slots are reused via the
 * `(language, ranges)` pool inside `editorSyntaxState`.
 */
void editorSyntaxApplyInputEdit(TSTree *tree, const struct editorSyntaxEdit *edit);
int editorSyntaxStateParseInjections(struct editorSyntaxState *state,
                                     const struct editorTextSource *source,
                                     const struct editorSyntaxEdit *incremental_edit);

/*
 * Parsed/injected-tree lifecycle helpers used by both the host parse
 * code (syntax.c) and the injection code (syntax_injections.c).
 * Implementations remain in syntax.c since the host state lifecycle
 * is the primary owner.
 */
void editorSyntaxParsedTreeInit(struct editorSyntaxParsedTree *parsed,
                                enum editorSyntaxLanguage language);
int editorSyntaxParsedTreeCreateParser(struct editorSyntaxParsedTree *parsed,
                                       enum editorSyntaxLanguage language);
void editorSyntaxParsedTreeDestroy(struct editorSyntaxParsedTree *parsed);
int editorSyntaxParsedTreeParse(struct editorSyntaxParsedTree *parsed,
                                struct editorSyntaxState *state,
                                const struct editorTextSource *source, int incremental);
void editorSyntaxInjectedTreeInit(struct editorSyntaxInjectedTree *injection);
void editorSyntaxInjectedTreeDestroy(struct editorSyntaxInjectedTree *injection);

/*
 * Helpers shared between the predicate-evaluation TU and the rest of
 * syntax.c. `editorSyntaxNodeText` copies a TSNode's source bytes into
 * the state's scratch buffer (primary or secondary slot picked by
 * scratch_idx) and returns a pointer into that scratch. The pointer is
 * valid until the next call with the same scratch_idx.
 */
int editorSyntaxNodeText(struct editorSyntaxState *state, const struct editorTextSource *source,
                         TSNode node, int scratch_idx, const char **text_out, size_t *len_out);
int editorSyntaxLocalsContextNodeIsLocal(const struct editorSyntaxLocalsContext *ctx, TSNode node);

/*
 * Predicate evaluator for #eq?, #not-eq?, #match?, #not-match?, #any-of?,
 * #not-any-of?, #is?, #is-not? against a TSQueryMatch. Implemented in
 * syntax_predicates.c.
 */
int editorSyntaxMatchPassesPredicates(const TSQuery *query, uint32_t pattern_index,
                                      const TSQueryMatch *match,
                                      const struct editorSyntaxPredicateContext *ctx);

/*
 * Locals analysis helpers implemented in syntax_locals.c. The
 * editorSyntaxLocalsContext lifecycle (Init/Free) is owned here so the
 * state-lifecycle and injection-tree-lifecycle in syntax.c can manage
 * embedded locals contexts without duplicating the implementation.
 *
 * BuildLocalsContext walks a parsed tree, applies the language's locals
 * query, and fills `ctx_out` with per-node marks classifying each
 * identifier as "local" or "external".
 *
 * StateEnsureLocalsCached / InvalidateLocalsCaches form the per-tree
 * cache layer used by the highlight-capture path. LanguageHasLocalsQuery
 * is a fast predicate that callers use to skip the locals pass when the
 * language has no locals.scm.
 */
void editorSyntaxLocalsContextInit(struct editorSyntaxLocalsContext *ctx);
void editorSyntaxLocalsContextFree(struct editorSyntaxLocalsContext *ctx);
int editorSyntaxBuildLocalsContext(const TSTree *tree, struct editorSyntaxState *state,
                                   enum editorSyntaxLanguage language,
                                   const struct editorTextSource *source,
                                   struct editorSyntaxLocalsContext *ctx_out);
void editorSyntaxStateInvalidateLocalsCaches(struct editorSyntaxState *state);
int editorSyntaxStateEnsureLocalsCached(struct editorSyntaxState *state,
                                        const struct editorSyntaxParsedTree *parsed,
                                        const struct editorTextSource *source,
                                        enum editorSyntaxLanguage language,
                                        struct editorSyntaxInjectedTree *injection,
                                        const struct editorSyntaxLocalsContext **locals_out);
int editorSyntaxLanguageHasLocalsQuery(enum editorSyntaxLanguage language);

#endif
