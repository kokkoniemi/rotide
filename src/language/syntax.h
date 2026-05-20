#ifndef SYNTAX_H
#define SYNTAX_H

#include <stddef.h>
#include <stdint.h>

#define ROTIDE_MAX_SYNTAX_SPANS_PER_ROW 256

enum editorSyntaxLanguage {
	EDITOR_SYNTAX_NONE = 0,
	EDITOR_SYNTAX_C,
	EDITOR_SYNTAX_CPP,
	EDITOR_SYNTAX_GO,
	EDITOR_SYNTAX_SHELL,
	EDITOR_SYNTAX_HTML,
	EDITOR_SYNTAX_JAVASCRIPT,
	EDITOR_SYNTAX_JSDOC,
	EDITOR_SYNTAX_TYPESCRIPT,
	EDITOR_SYNTAX_TSX,
	EDITOR_SYNTAX_CSS,
	EDITOR_SYNTAX_JSON,
	EDITOR_SYNTAX_PYTHON,
	EDITOR_SYNTAX_PHP,
	EDITOR_SYNTAX_RUST,
	EDITOR_SYNTAX_JAVA,
	EDITOR_SYNTAX_REGEX,
	EDITOR_SYNTAX_CSHARP,
	EDITOR_SYNTAX_HASKELL,
	EDITOR_SYNTAX_RUBY,
	EDITOR_SYNTAX_OCAML,
	EDITOR_SYNTAX_JULIA,
	EDITOR_SYNTAX_SCALA,
	EDITOR_SYNTAX_EJS,
	EDITOR_SYNTAX_ERB,
	EDITOR_SYNTAX_MARKDOWN,
	EDITOR_SYNTAX_MARKDOWN_INLINE,
	EDITOR_SYNTAX_TOML,
	EDITOR_SYNTAX_YAML,
	EDITOR_SYNTAX_XML,
	EDITOR_SYNTAX_MAKE,
	EDITOR_SYNTAX_DIFF,
	EDITOR_SYNTAX_LANGUAGE_COUNT
};

enum editorSyntaxHighlightClass {
	EDITOR_SYNTAX_HL_NONE = 0,
	EDITOR_SYNTAX_HL_COMMENT,
	EDITOR_SYNTAX_HL_KEYWORD,
	EDITOR_SYNTAX_HL_TYPE,
	EDITOR_SYNTAX_HL_FUNCTION,
	EDITOR_SYNTAX_HL_STRING,
	EDITOR_SYNTAX_HL_NUMBER,
	EDITOR_SYNTAX_HL_CONSTANT,
	EDITOR_SYNTAX_HL_VARIABLE,
	EDITOR_SYNTAX_HL_PARAMETER,
	EDITOR_SYNTAX_HL_MODULE,
	EDITOR_SYNTAX_HL_PROPERTY,
	EDITOR_SYNTAX_HL_PREPROCESSOR,
	EDITOR_SYNTAX_HL_OPERATOR,
	EDITOR_SYNTAX_HL_PUNCTUATION,
	EDITOR_SYNTAX_HL_CLASS_COUNT
};

struct editorRowSyntaxSpan {
	int start_render_idx;
	int end_render_idx;
	enum editorSyntaxHighlightClass highlight_class;
};

/* Tab-local Tree-sitter state. The implementation owns host and injected parse
 * trees, query caches, budget events, and visible capture collection.
 */
struct editorSyntaxState;
struct editorTextSource;

struct editorSyntaxPoint {
	uint32_t row;
	uint32_t column;
};

struct editorSyntaxEdit {
	uint32_t start_byte;
	uint32_t old_end_byte;
	uint32_t new_end_byte;
	struct editorSyntaxPoint start_point;
	struct editorSyntaxPoint old_end_point;
	struct editorSyntaxPoint new_end_point;
};

struct editorSyntaxCapture {
	uint32_t start_byte;
	uint32_t end_byte;
	enum editorSyntaxHighlightClass highlight_class;
};

struct editorSyntaxByteRange {
	uint32_t start_byte;
	uint32_t end_byte;
};

enum editorSyntaxPerformanceMode {
	EDITOR_SYNTAX_PERF_NORMAL = 0,
	EDITOR_SYNTAX_PERF_DEGRADED_PREDICATES,
	EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS,
	EDITOR_SYNTAX_PERF_DISABLED
};

#define ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX 80

struct editorSyntaxQueryCompileError {
	int has_error;
	enum editorSyntaxLanguage language;
	uint32_t error_offset;
	int error_type;
	char context[ROTIDE_SYNTAX_QUERY_ERROR_CONTEXT_MAX + 1];
};

enum editorSyntaxQueryKind {
	EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT = 0,
	EDITOR_SYNTAX_QUERY_KIND_INJECTION
};

enum editorSyntaxLimitEventKind {
	EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED = 0,
	EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED,
	EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL,
	EDITOR_SYNTAX_LIMIT_EVENT_PARSE_FAILED,
	EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR
};

struct editorSyntaxLimitEvent {
	enum editorSyntaxLimitEventKind kind;
	enum editorSyntaxLanguage language;
	int row;
	int detail;
};

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilename(const char *filename);
enum editorSyntaxLanguage
editorSyntaxDetectLanguageFromFilenameAndFirstLine(const char *filename, const char *first_line);

/* Create/destroy and parse APIs are per tab. Callers pass editorTextSource so
 * syntax can read document bytes without owning canonical text.
 */
struct editorSyntaxState *editorSyntaxStateCreate(enum editorSyntaxLanguage language);
void editorSyntaxStateDestroy(struct editorSyntaxState *state);

void editorTextSourceInitString(struct editorTextSource *source, const char *text, size_t len);
size_t editorTextSourceLength(const struct editorTextSource *source);
int editorTextSourceCopyRange(const struct editorTextSource *source, size_t start_byte,
                              size_t end_byte, char *dst);
char *editorTextSourceDupRange(const struct editorTextSource *source, size_t start_byte,
                               size_t end_byte, size_t *len_out);

int editorSyntaxStateParseFull(struct editorSyntaxState *state,
                               const struct editorTextSource *source);
int editorSyntaxStateApplyEditAndParse(struct editorSyntaxState *state,
                                       const struct editorSyntaxEdit *edit,
                                       const struct editorTextSource *source);
int editorSyntaxStateConfigureForSourceLength(struct editorSyntaxState *state, size_t source_len);
enum editorSyntaxPerformanceMode
editorSyntaxStatePerformanceMode(const struct editorSyntaxState *state);
size_t editorSyntaxStateSourceLength(const struct editorSyntaxState *state);
int editorSyntaxStateCopyLastChangedRanges(const struct editorSyntaxState *state,
                                           struct editorSyntaxByteRange *ranges, int max_ranges,
                                           int *count_out);
int editorSyntaxStateConsumeBudgetEvents(struct editorSyntaxState *state,
                                         int *parse_budget_exceeded_out,
                                         int *query_budget_exceeded_out);
int editorSyntaxStateConsumeQueryUnavailableEvent(struct editorSyntaxState *state,
                                                  enum editorSyntaxLanguage *language_out,
                                                  enum editorSyntaxQueryKind *kind_out);
int editorSyntaxStateConsumeLimitEvent(struct editorSyntaxState *state,
                                       struct editorSyntaxLimitEvent *event_out);
void editorSyntaxStateRecordCaptureTruncated(struct editorSyntaxState *state, int row);
void editorSyntaxStateRecordParseFailed(struct editorSyntaxState *state, int consecutive_failures);
int editorSyntaxDrainLastQueryCompileError(struct editorSyntaxQueryCompileError *error_out);
int editorSyntaxCopyLastQueryCompileError(struct editorSyntaxQueryCompileError *error_out);

int editorSyntaxStateHasTree(const struct editorSyntaxState *state);
int editorSyntaxStateHasError(const struct editorSyntaxState *state);
int editorSyntaxStateFirstErrorPosition(const struct editorSyntaxState *state, int *row_out,
                                        int *column_out);
const char *editorSyntaxStateRootType(const struct editorSyntaxState *state);
enum editorSyntaxLanguage editorSyntaxStateLanguage(const struct editorSyntaxState *state);
int editorSyntaxStateSuggestIndentAnchor(const struct editorSyntaxState *state, int row, int column,
                                         int *anchor_row_out, int *extra_levels_out);
int editorSyntaxStateCollectCapturesForRange(struct editorSyntaxState *state,
                                             const struct editorTextSource *source,
                                             uint32_t start_byte, uint32_t end_byte,
                                             struct editorSyntaxCapture *captures, int max_captures,
                                             int *count_out);

/* Test hooks for deterministic budget-path tests. */
void editorSyntaxTestSetBudgetOverrides(int enabled, uint32_t query_match_limit,
                                        uint64_t query_time_budget_ns,
                                        uint64_t parse_time_budget_ns);
void editorSyntaxTestResetBudgetOverrides(void);
int editorSyntaxTestBudgetOverridesEnabled(void);
void editorSyntaxTestSetMaxInjectionDepth(int depth);
void editorSyntaxTestResetMaxInjectionDepth(void);
int editorSyntaxTestCaptureRuleCount(void);
int editorSyntaxTestCaptureRuleAt(int idx, const char **prefix_out,
                                  enum editorSyntaxHighlightClass *class_out);
enum editorSyntaxHighlightClass editorSyntaxTestClassFromCaptureName(const char *name);
void editorSyntaxTestSetParseFailureCountdowns(int full_parse_failures,
                                               int incremental_parse_failures);
void editorSyntaxTestResetParseFailureCountdowns(void);
void editorSyntaxTestResetLastQueryCompileError(void);
int editorSyntaxTestCompileQueryForDiagnostics(enum editorSyntaxLanguage language,
                                               const char *query_source);

void editorSyntaxReleaseSharedResources(void);

#endif
