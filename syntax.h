#ifndef SYNTAX_H
#define SYNTAX_H

#include "rotide.h"

#include <stddef.h>
#include <stdint.h>

struct editorSyntaxState;

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

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilename(const char *filename);
enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilenameAndFirstLine(
		const char *filename, const char *first_line);

struct editorSyntaxState *editorSyntaxStateCreate(enum editorSyntaxLanguage language);
void editorSyntaxStateDestroy(struct editorSyntaxState *state);

void editorTextSourceInitString(struct editorTextSource *source, const char *text, size_t len);
size_t editorTextSourceLength(const struct editorTextSource *source);
int editorTextSourceCopyRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, char *dst);
char *editorTextSourceDupRange(const struct editorTextSource *source,
		size_t start_byte, size_t end_byte, size_t *len_out);

int editorSyntaxStateParseFull(struct editorSyntaxState *state,
		const struct editorTextSource *source);
int editorSyntaxStateApplyEditAndParse(struct editorSyntaxState *state,
		const struct editorSyntaxEdit *edit,
		const struct editorTextSource *source);
int editorSyntaxStateConfigureForSourceLength(struct editorSyntaxState *state, size_t source_len);
enum editorSyntaxPerformanceMode editorSyntaxStatePerformanceMode(
		const struct editorSyntaxState *state);
size_t editorSyntaxStateSourceLength(const struct editorSyntaxState *state);
int editorSyntaxStateCopyLastChangedRanges(const struct editorSyntaxState *state,
		struct editorSyntaxByteRange *ranges, int max_ranges, int *count_out);
int editorSyntaxStateConsumeBudgetEvents(struct editorSyntaxState *state,
		int *parse_budget_exceeded_out,
		int *query_budget_exceeded_out);

int editorSyntaxStateHasTree(const struct editorSyntaxState *state);
const char *editorSyntaxStateRootType(const struct editorSyntaxState *state);
enum editorSyntaxLanguage editorSyntaxStateLanguage(const struct editorSyntaxState *state);
int editorSyntaxStateCollectCapturesForRange(struct editorSyntaxState *state,
		const struct editorTextSource *source,
		uint32_t start_byte, uint32_t end_byte,
		struct editorSyntaxCapture *captures, int max_captures, int *count_out);

/* Test hooks for deterministic budget-path tests. */
void editorSyntaxTestSetBudgetOverrides(int enabled,
		uint32_t query_match_limit,
		uint64_t query_time_budget_ns,
		uint64_t parse_time_budget_ns);
void editorSyntaxTestResetBudgetOverrides(void);
int editorSyntaxTestBudgetOverridesEnabled(void);

void editorSyntaxReleaseSharedResources(void);

#endif
