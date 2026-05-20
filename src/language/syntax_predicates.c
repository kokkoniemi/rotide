/* Tree-sitter query predicate evaluation.
 *
 * Owns the implementations of #eq?, #not-eq?, #match?, #not-match?,
 * #any-of?, #not-any-of?, #is?, and #is-not? against TSQueryMatch
 * captures. Pure logic plus a small regex cache; no parse/inject state
 * is owned here. The entry point editorSyntaxMatchPassesPredicates is
 * declared in syntax_internal.h.
 */
#include "language/syntax_internal.h"
#include "tree_sitter/api.h"

#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int syntaxPredicatesFindCaptureNode(const TSQueryMatch *match, uint32_t capture_id,
                                           TSNode *node_out) {
	if (match == NULL || node_out == NULL) {
		return 0;
	}
	for (uint16_t i = 0; i < match->capture_count; i++) {
		if (match->captures[i].index == capture_id) {
			*node_out = match->captures[i].node;
			return 1;
		}
	}
	return 0;
}

static int syntaxPredicatesArgText(const TSQuery *query, const TSQueryMatch *match,
                                   const struct editorSyntaxPredicateContext *ctx,
                                   const TSQueryPredicateStep *arg, int scratch_idx,
                                   const char **text_out, size_t *len_out) {
	if (query == NULL || match == NULL || ctx == NULL || arg == NULL || text_out == NULL ||
	    len_out == NULL) {
		return 0;
	}

	if (arg->type == TSQueryPredicateStepTypeString) {
		uint32_t str_len = 0;
		const char *str = ts_query_string_value_for_id(query, arg->value_id, &str_len);
		if (str == NULL) {
			return 0;
		}
		*text_out = str;
		*len_out = (size_t)str_len;
		return 1;
	}

	if (arg->type == TSQueryPredicateStepTypeCapture) {
		TSNode node;
		if (!syntaxPredicatesFindCaptureNode(match, arg->value_id, &node)) {
			return 0;
		}
		return editorSyntaxNodeText(ctx->state, ctx->source, node, scratch_idx, text_out,
		                            len_out);
	}

	return 0;
}

static int syntaxPredicatesRegexMatchCompiled(const char *text, size_t text_len, regex_t *regex) {
	if (text == NULL || regex == NULL) {
		return 0;
	}

	char *text_buf = malloc(text_len + 1);
	if (text_buf == NULL) {
		free(text_buf);
		return 0;
	}
	memcpy(text_buf, text, text_len);
	text_buf[text_len] = '\0';
	int matched = regexec(regex, text_buf, 0, NULL, 0) == 0;
	free(text_buf);
	return matched;
}

static int syntaxPredicatesRegexMatch(const char *text, size_t text_len, const char *pattern,
                                      size_t pattern_len) {
	if (text == NULL || pattern == NULL) {
		return 0;
	}

	char *pattern_buf = malloc(pattern_len + 1);
	if (pattern_buf == NULL) {
		return 0;
	}
	memcpy(pattern_buf, pattern, pattern_len);
	pattern_buf[pattern_len] = '\0';

	regex_t regex;
	int compiled = regcomp(&regex, pattern_buf, REG_EXTENDED | REG_NOSUB);
	free(pattern_buf);
	if (compiled != 0) {
		return 0;
	}

	int matched = syntaxPredicatesRegexMatchCompiled(text, text_len, &regex);
	regfree(&regex);
	return matched;
}

static int syntaxPredicatesRegexMatchCached(const TSQuery *query, uint32_t string_id,
                                            const char *text, size_t text_len) {
	if (query == NULL || text == NULL) {
		return 0;
	}

	struct editorSyntaxQueryCacheEntry *cache = editorSyntaxQueryCacheEntryForQuery(query);
	if (cache == NULL || cache->compiled_regexes == NULL ||
	    cache->compiled_regex_compiled == NULL || cache->compiled_regex_failed == NULL ||
	    string_id >= cache->string_count) {
		uint32_t pattern_len = 0;
		const char *pattern = ts_query_string_value_for_id(query, string_id, &pattern_len);
		if (pattern == NULL) {
			return 0;
		}
		return syntaxPredicatesRegexMatch(text, text_len, pattern, (size_t)pattern_len);
	}

	if (!cache->compiled_regex_compiled[string_id] &&
	    !cache->compiled_regex_failed[string_id]) {
		uint32_t pattern_len = 0;
		const char *pattern = ts_query_string_value_for_id(query, string_id, &pattern_len);
		if (pattern == NULL) {
			cache->compiled_regex_failed[string_id] = 1;
			return 0;
		}

		char *pattern_buf = malloc((size_t)pattern_len + 1);
		if (pattern_buf == NULL) {
			return 0;
		}
		memcpy(pattern_buf, pattern, pattern_len);
		pattern_buf[pattern_len] = '\0';

		if (regcomp(&cache->compiled_regexes[string_id], pattern_buf,
		            REG_EXTENDED | REG_NOSUB) == 0) {
			cache->compiled_regex_compiled[string_id] = 1;
		} else {
			cache->compiled_regex_failed[string_id] = 1;
		}
		free(pattern_buf);
	}

	if (!cache->compiled_regex_compiled[string_id]) {
		return 0;
	}

	return syntaxPredicatesRegexMatchCompiled(text, text_len,
	                                          &cache->compiled_regexes[string_id]);
}

static int syntaxPredicatesTargetNode(const TSQuery *query, const TSQueryMatch *match,
                                      const TSQueryPredicateStep *args, uint32_t arg_count,
                                      TSNode *node_out, const char **property_out,
                                      size_t *property_len_out) {
	if (query == NULL || match == NULL || args == NULL || arg_count == 0 || node_out == NULL ||
	    property_out == NULL || property_len_out == NULL) {
		return 0;
	}

	if (args[0].type == TSQueryPredicateStepTypeCapture) {
		if (!syntaxPredicatesFindCaptureNode(match, args[0].value_id, node_out)) {
			return 0;
		}
		if (arg_count < 2 || args[1].type != TSQueryPredicateStepTypeString) {
			return 0;
		}
		uint32_t property_len = 0;
		const char *property =
		        ts_query_string_value_for_id(query, args[1].value_id, &property_len);
		if (property == NULL) {
			return 0;
		}
		*property_out = property;
		*property_len_out = property_len;
		return 1;
	}

	if (args[0].type == TSQueryPredicateStepTypeString && match->capture_count > 0) {
		*node_out = match->captures[0].node;
		uint32_t property_len = 0;
		const char *property =
		        ts_query_string_value_for_id(query, args[0].value_id, &property_len);
		if (property == NULL) {
			return 0;
		}
		*property_out = property;
		*property_len_out = property_len;
		return 1;
	}

	return 0;
}

typedef int (*syntaxPredicatesHandlerFn)(const TSQuery *query, const TSQueryMatch *match,
                                         const struct editorSyntaxPredicateContext *ctx,
                                         const TSQueryPredicateStep *args, uint32_t arg_count);

static int syntaxPredicatesAlwaysPass(const TSQuery *query, const TSQueryMatch *match,
                                      const struct editorSyntaxPredicateContext *ctx,
                                      const TSQueryPredicateStep *args, uint32_t arg_count) {
	(void)query;
	(void)match;
	(void)ctx;
	(void)args;
	(void)arg_count;
	return 1;
}

static int syntaxPredicatesEqShared(int negated, const TSQuery *query, const TSQueryMatch *match,
                                    const struct editorSyntaxPredicateContext *ctx,
                                    const TSQueryPredicateStep *args, uint32_t arg_count) {
	if (arg_count < 2) {
		return 1;
	}
	const char *left = NULL;
	size_t left_len = 0;
	const char *right = NULL;
	size_t right_len = 0;
	if (!syntaxPredicatesArgText(query, match, ctx, &args[0], 0, &left, &left_len) ||
	    !syntaxPredicatesArgText(query, match, ctx, &args[1], 1, &right, &right_len)) {
		return 0;
	}
	int equal = (left_len == right_len) && memcmp(left, right, left_len) == 0;
	return negated ? !equal : equal;
}

static int syntaxPredicatesEq(const TSQuery *query, const TSQueryMatch *match,
                              const struct editorSyntaxPredicateContext *ctx,
                              const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesEqShared(0, query, match, ctx, args, arg_count);
}

static int syntaxPredicatesNotEq(const TSQuery *query, const TSQueryMatch *match,
                                 const struct editorSyntaxPredicateContext *ctx,
                                 const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesEqShared(1, query, match, ctx, args, arg_count);
}

static int syntaxPredicatesMatchShared(int negated, const TSQuery *query, const TSQueryMatch *match,
                                       const struct editorSyntaxPredicateContext *ctx,
                                       const TSQueryPredicateStep *args, uint32_t arg_count) {
	if (arg_count < 2) {
		return 1;
	}
	const char *text = NULL;
	size_t text_len = 0;
	const char *pattern = NULL;
	size_t pattern_len = 0;
	if (!syntaxPredicatesArgText(query, match, ctx, &args[0], 0, &text, &text_len) ||
	    !syntaxPredicatesArgText(query, match, ctx, &args[1], 1, &pattern, &pattern_len)) {
		return 0;
	}
	int matched = 0;
	if (args[1].type == TSQueryPredicateStepTypeString) {
		matched = syntaxPredicatesRegexMatchCached(query, args[1].value_id, text, text_len);
	} else {
		matched = syntaxPredicatesRegexMatch(text, text_len, pattern, pattern_len);
	}
	return negated ? !matched : matched;
}

static int syntaxPredicatesMatch(const TSQuery *query, const TSQueryMatch *match,
                                 const struct editorSyntaxPredicateContext *ctx,
                                 const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesMatchShared(0, query, match, ctx, args, arg_count);
}

static int syntaxPredicatesNotMatch(const TSQuery *query, const TSQueryMatch *match,
                                    const struct editorSyntaxPredicateContext *ctx,
                                    const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesMatchShared(1, query, match, ctx, args, arg_count);
}

static int syntaxPredicatesAnyOfShared(int negated, const TSQuery *query, const TSQueryMatch *match,
                                       const struct editorSyntaxPredicateContext *ctx,
                                       const TSQueryPredicateStep *args, uint32_t arg_count) {
	if (arg_count < 2) {
		return 1;
	}
	const char *target = NULL;
	size_t target_len = 0;
	if (!syntaxPredicatesArgText(query, match, ctx, &args[0], 0, &target, &target_len)) {
		return 0;
	}

	int found = 0;
	for (uint32_t i = 1; i < arg_count; i++) {
		if (args[i].type != TSQueryPredicateStepTypeString) {
			continue;
		}
		uint32_t value_len = 0;
		const char *value =
		        ts_query_string_value_for_id(query, args[i].value_id, &value_len);
		if (value == NULL) {
			continue;
		}
		if (target_len == value_len && memcmp(target, value, target_len) == 0) {
			found = 1;
			break;
		}
	}

	return negated ? !found : found;
}

static int syntaxPredicatesAnyOf(const TSQuery *query, const TSQueryMatch *match,
                                 const struct editorSyntaxPredicateContext *ctx,
                                 const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesAnyOfShared(0, query, match, ctx, args, arg_count);
}

static int syntaxPredicatesNotAnyOf(const TSQuery *query, const TSQueryMatch *match,
                                    const struct editorSyntaxPredicateContext *ctx,
                                    const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesAnyOfShared(1, query, match, ctx, args, arg_count);
}

/* `#is?` / `#is-not?` recognize only the `local` property today; other properties
 * are treated as not-set. Kept as a single helper per the predicate-table plan. */
static int syntaxPredicatesIsShared(int negated, const TSQuery *query, const TSQueryMatch *match,
                                    const struct editorSyntaxPredicateContext *ctx,
                                    const TSQueryPredicateStep *args, uint32_t arg_count) {
	TSNode target;
	const char *property = NULL;
	size_t property_len = 0;
	if (!syntaxPredicatesTargetNode(query, match, args, arg_count, &target, &property,
	                                &property_len)) {
		return 1;
	}
	int is_property = 0;
	if (editorSyntaxStringEquals(property, property_len, "local")) {
		is_property = editorSyntaxLocalsContextNodeIsLocal(ctx->locals, target);
	}
	return negated ? !is_property : is_property;
}

static int syntaxPredicatesIs(const TSQuery *query, const TSQueryMatch *match,
                              const struct editorSyntaxPredicateContext *ctx,
                              const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesIsShared(0, query, match, ctx, args, arg_count);
}

static int syntaxPredicatesIsNot(const TSQuery *query, const TSQueryMatch *match,
                                 const struct editorSyntaxPredicateContext *ctx,
                                 const TSQueryPredicateStep *args, uint32_t arg_count) {
	return syntaxPredicatesIsShared(1, query, match, ctx, args, arg_count);
}

struct syntaxPredicatesEntry {
	const char *name;
	syntaxPredicatesHandlerFn handler;
};

static const struct syntaxPredicatesEntry g_syntax_predicates_table[] = {
        {"set!", syntaxPredicatesAlwaysPass},      {"eq?", syntaxPredicatesEq},
        {"not-eq?", syntaxPredicatesNotEq},        {"match?", syntaxPredicatesMatch},
        {"not-match?", syntaxPredicatesNotMatch},  {"any-of?", syntaxPredicatesAnyOf},
        {"not-any-of?", syntaxPredicatesNotAnyOf}, {"is?", syntaxPredicatesIs},
        {"is-not?", syntaxPredicatesIsNot}};

static int syntaxPredicatesEvaluate(const TSQuery *query, const TSQueryMatch *match,
                                    const struct editorSyntaxPredicateContext *ctx,
                                    const TSQueryPredicateStep *steps, uint32_t step_count) {
	if (query == NULL || match == NULL || ctx == NULL || steps == NULL || step_count == 0) {
		return 1;
	}
	if (steps[0].type != TSQueryPredicateStepTypeString) {
		return 1;
	}

	uint32_t command_len = 0;
	const char *command = ts_query_string_value_for_id(query, steps[0].value_id, &command_len);
	if (command == NULL) {
		return 1;
	}

	const TSQueryPredicateStep *args = &steps[1];
	uint32_t arg_count = step_count - 1;

	for (size_t i = 0;
	     i < sizeof(g_syntax_predicates_table) / sizeof(g_syntax_predicates_table[0]); i++) {
		if (editorSyntaxStringEquals(command, command_len,
		                             g_syntax_predicates_table[i].name)) {
			return g_syntax_predicates_table[i].handler(query, match, ctx, args,
			                                            arg_count);
		}
	}
	return 1;
}

int editorSyntaxMatchPassesPredicates(const TSQuery *query, uint32_t pattern_index,
                                      const TSQueryMatch *match,
                                      const struct editorSyntaxPredicateContext *ctx) {
	if (query == NULL || match == NULL || ctx == NULL) {
		return 1;
	}

	uint32_t step_count = 0;
	const TSQueryPredicateStep *steps =
	        ts_query_predicates_for_pattern(query, pattern_index, &step_count);
	if (steps == NULL || step_count == 0) {
		return 1;
	}

	uint32_t i = 0;
	while (i < step_count) {
		uint32_t start = i;
		while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) {
			i++;
		}
		uint32_t end = i;
		if (end > start) {
			if (!syntaxPredicatesEvaluate(query, match, ctx, &steps[start],
			                              end - start)) {
				return 0;
			}
		}
		i++;
	}

	return 1;
}
