#include "language/lsp_responses.h"

#include "language/lsp_json.h"
#include "language/lsp_protocol.h"
#include "support/file_io.h"
#include "support/size_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int editorLspParseCompletionProviderInResponse(const char *response_json, int *supported_out,
                                               char **trigger_chars_out) {
	if (supported_out != NULL) {
		*supported_out = 0;
	}
	if (trigger_chars_out != NULL) {
		*trigger_chars_out = NULL;
	}
	if (response_json == NULL) {
		return 0;
	}
	const char *cp_key = strstr(response_json, "\"completionProvider\"");
	if (cp_key == NULL) {
		return 1;
	}
	const char *colon = strchr(cp_key, ':');
	if (colon == NULL) {
		return 0;
	}
	const char *value = editorLspSkipWs(colon + 1);
	if (value == NULL) {
		return 0;
	}
	if (value[0] == 'n' && strncmp(value, "null", 4) == 0) {
		return 1;
	}
	if (supported_out != NULL) {
		*supported_out = 1;
	}
	if (value[0] != '{') {
		return 1;
	}
	const char *provider_end = editorLspFindJsonObjectEnd(value);
	if (provider_end == NULL) {
		return 1;
	}
	const char *tc_key = editorLspStrstrBounded(value, "\"triggerCharacters\"", provider_end);
	if (tc_key == NULL) {
		return 1;
	}
	const char *tc_colon = strchr(tc_key, ':');
	if (tc_colon == NULL || tc_colon >= provider_end) {
		return 1;
	}
	const char *array_start = editorLspSkipWs(tc_colon + 1);
	if (array_start == NULL || array_start >= provider_end || array_start[0] != '[') {
		return 1;
	}
	struct editorLspString chars = {0};
	const char *scan = array_start + 1;
	while (scan < provider_end) {
		while (scan < provider_end && (*scan == ' ' || *scan == '\t' || *scan == '\n' ||
		                               *scan == '\r' || *scan == ',')) {
			scan++;
		}
		if (scan >= provider_end || *scan == ']') {
			break;
		}
		if (*scan != '"') {
			break;
		}
		char *entry = NULL;
		const char *after = NULL;
		if (!editorLspParseJsonString(scan, &entry, &after) || entry == NULL) {
			break;
		}
		if (entry[0] != '\0' && !editorLspStringAppendBytes(&chars, entry, 1)) {
			free(entry);
			free(chars.buf);
			return 0;
		}
		free(entry);
		scan = after != NULL ? after : provider_end;
	}
	if (trigger_chars_out != NULL) {
		*trigger_chars_out = chars.buf != NULL ? chars.buf : strdup("");
		if (*trigger_chars_out == NULL) {
			free(chars.buf);
			return 0;
		}
	} else {
		free(chars.buf);
	}
	return 1;
}

void editorLspFreeLocations(struct editorLspLocation *locations, int count) {
	if (locations == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(locations[i].path);
	}
	free(locations);
}

void editorLspFreeCompletionItems(struct editorLspCompletionItem *items, int count) {
	if (items == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(items[i].label);
		free(items[i].filter_text);
		free(items[i].insert_text);
		free(items[i].text_edit_new_text);
	}
	free(items);
}

int editorLspCopyCompletionItems(struct editorLspCompletionItem **out_items, int *out_count,
                                 const struct editorLspCompletionItem *items, int count) {
	if (out_items == NULL || out_count == NULL) {
		return 0;
	}
	*out_items = NULL;
	*out_count = 0;
	if (items == NULL || count <= 0) {
		return 1;
	}
	struct editorLspCompletionItem *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}
	for (int i = 0; i < count; i++) {
		if (items[i].label != NULL) {
			copy[i].label = strdup(items[i].label);
			if (copy[i].label == NULL) {
				editorLspFreeCompletionItems(copy, i + 1);
				return 0;
			}
		}
		if (items[i].filter_text != NULL) {
			copy[i].filter_text = strdup(items[i].filter_text);
			if (copy[i].filter_text == NULL) {
				editorLspFreeCompletionItems(copy, i + 1);
				return 0;
			}
		}
		if (items[i].insert_text != NULL) {
			copy[i].insert_text = strdup(items[i].insert_text);
			if (copy[i].insert_text == NULL) {
				editorLspFreeCompletionItems(copy, i + 1);
				return 0;
			}
		}
		copy[i].has_text_edit = items[i].has_text_edit;
		copy[i].text_edit_start_line = items[i].text_edit_start_line;
		copy[i].text_edit_start_character = items[i].text_edit_start_character;
		copy[i].text_edit_end_line = items[i].text_edit_end_line;
		copy[i].text_edit_end_character = items[i].text_edit_end_character;
		if (items[i].text_edit_new_text != NULL) {
			copy[i].text_edit_new_text = strdup(items[i].text_edit_new_text);
			if (copy[i].text_edit_new_text == NULL) {
				editorLspFreeCompletionItems(copy, i + 1);
				return 0;
			}
		}
	}
	*out_items = copy;
	*out_count = count;
	return 1;
}

static int lspResponsesParsePositionFromObject(const char *position_object, const char *limit,
                                               int *line_out, int *character_out) {
	if (position_object == NULL || position_object[0] != '{') {
		return 0;
	}
	const char *line_key = editorLspStrstrBounded(position_object, "\"line\"", limit);
	if (line_key == NULL) {
		return 0;
	}
	const char *line_colon = strchr(line_key, ':');
	if (line_colon == NULL || line_colon >= limit) {
		return 0;
	}
	int line = 0;
	if (!editorLspParseJsonInt(line_colon + 1, &line, NULL) || line < 0) {
		return 0;
	}
	const char *char_key = editorLspStrstrBounded(position_object, "\"character\"", limit);
	if (char_key == NULL) {
		return 0;
	}
	const char *char_colon = strchr(char_key, ':');
	if (char_colon == NULL || char_colon >= limit) {
		return 0;
	}
	int character = 0;
	if (!editorLspParseJsonInt(char_colon + 1, &character, NULL) || character < 0) {
		return 0;
	}
	*line_out = line;
	*character_out = character;
	return 1;
}

static int lspResponsesParsePositionFromStart(const char *range_json, const char *limit,
                                              int *line_out, int *character_out) {
	return editorLspParsePositionFromKey(range_json, "start", limit, line_out, character_out);
}

static int lspResponsesParseCompletionItemObject(const char *object_start, const char *object_end,
                                                 struct editorLspCompletionItem *out) {
	memset(out, 0, sizeof(*out));

	const char *label_key = editorLspStrstrBounded(object_start, "\"label\"", object_end);
	if (label_key == NULL) {
		return 0;
	}
	const char *label_colon = strchr(label_key, ':');
	if (label_colon == NULL || label_colon >= object_end) {
		return 0;
	}
	const char *label_value = editorLspSkipWs(label_colon + 1);
	if (label_value == NULL || label_value[0] != '"') {
		return 0;
	}
	if (!editorLspParseJsonString(label_value, &out->label, NULL) || out->label == NULL) {
		return 0;
	}

	const char *filter_key = editorLspStrstrBounded(object_start, "\"filterText\"", object_end);
	if (filter_key != NULL) {
		const char *filter_colon = strchr(filter_key, ':');
		if (filter_colon != NULL && filter_colon < object_end) {
			const char *filter_value = editorLspSkipWs(filter_colon + 1);
			if (filter_value != NULL && filter_value[0] == '"') {
				char *filter_text = NULL;
				if (editorLspParseJsonString(filter_value, &filter_text, NULL) &&
				    filter_text != NULL) {
					out->filter_text = filter_text;
				}
			}
		}
	}

	const char *insert_key = editorLspStrstrBounded(object_start, "\"insertText\"", object_end);
	if (insert_key != NULL) {
		const char *insert_colon = strchr(insert_key, ':');
		if (insert_colon != NULL && insert_colon < object_end) {
			const char *insert_value = editorLspSkipWs(insert_colon + 1);
			if (insert_value != NULL && insert_value[0] == '"') {
				char *insert_text = NULL;
				if (editorLspParseJsonString(insert_value, &insert_text, NULL) &&
				    insert_text != NULL) {
					out->insert_text = insert_text;
				}
			}
		}
	}

	const char *text_edit_key =
	        editorLspStrstrBounded(object_start, "\"textEdit\"", object_end);
	if (text_edit_key != NULL) {
		const char *te_colon = strchr(text_edit_key, ':');
		if (te_colon != NULL && te_colon < object_end) {
			const char *te_object = editorLspSkipWs(te_colon + 1);
			if (te_object != NULL && te_object[0] == '{') {
				const char *te_end = editorLspFindJsonObjectEnd(te_object);
				if (te_end != NULL && te_end <= object_end) {
					const char *range_key = editorLspStrstrBounded(
					        te_object, "\"range\"", te_end);
					const char *new_text_key = editorLspStrstrBounded(
					        te_object, "\"newText\"", te_end);
					int parsed_range = 0;
					if (range_key != NULL) {
						const char *range_colon = strchr(range_key, ':');
						if (range_colon != NULL && range_colon < te_end) {
							const char *range_object =
							        editorLspSkipWs(range_colon + 1);
							if (range_object != NULL &&
							    range_object[0] == '{') {
								const char *range_end =
								        editorLspFindJsonObjectEnd(
								                range_object);
								if (range_end != NULL &&
								    range_end <= te_end) {
									int sl = 0;
									int sc = 0;
									int el = 0;
									int ec = 0;
									const char *start_pos =
									        editorLspStrstrBounded(
									                range_object,
									                "\"start\"",
									                range_end);
									const char *end_pos =
									        editorLspStrstrBounded(
									                range_object,
									                "\"end\"",
									                range_end);
									if (start_pos != NULL &&
									    end_pos != NULL) {
										const char *sc_colon =
										        strchr(start_pos,
										               ':');
										const char *ec_colon =
										        strchr(end_pos,
										               ':');
										if (sc_colon !=
										            NULL &&
										    ec_colon !=
										            NULL) {
											const char *start_obj = editorLspSkipWs(
											        sc_colon +
											        1);
											const char *end_obj = editorLspSkipWs(
											        ec_colon +
											        1);
											if (start_obj !=
											            NULL &&
											    end_obj !=
											            NULL &&
											    start_obj[0] ==
											            '{' &&
											    end_obj[0] ==
											            '{') {
												if (lspResponsesParsePositionFromObject(
												            start_obj,
												            range_end,
												            &sl,
												            &sc) &&
												    lspResponsesParsePositionFromObject(
												            end_obj,
												            range_end,
												            &el,
												            &ec)) {
													out->text_edit_start_line =
													        sl;
													out->text_edit_start_character =
													        sc;
													out->text_edit_end_line =
													        el;
													out->text_edit_end_character =
													        ec;
													parsed_range =
													        1;
												}
											}
										}
									}
								}
							}
						}
					}
					if (parsed_range && new_text_key != NULL) {
						const char *nt_colon = strchr(new_text_key, ':');
						if (nt_colon != NULL && nt_colon < te_end) {
							const char *nt_value =
							        editorLspSkipWs(nt_colon + 1);
							if (nt_value != NULL &&
							    nt_value[0] == '"') {
								char *new_text = NULL;
								if (editorLspParseJsonString(
								            nt_value, &new_text,
								            NULL) &&
								    new_text != NULL) {
									out->text_edit_new_text =
									        new_text;
									out->has_text_edit = 1;
								}
							}
						}
					}
				}
			}
		}
	}
	return 1;
}

int editorLspParseCompletionResponse(const char *response_json,
                                     struct editorLspCompletionItem **items_out, int *count_out) {
	if (items_out == NULL || count_out == NULL) {
		return 0;
	}
	*items_out = NULL;
	*count_out = 0;
	if (response_json == NULL) {
		return 0;
	}

	const char *result_key = strstr(response_json, "\"result\"");
	if (result_key == NULL) {
		return 0;
	}
	const char *result_colon = strchr(result_key, ':');
	if (result_colon == NULL) {
		return 0;
	}
	const char *result = editorLspSkipWs(result_colon + 1);
	if (result == NULL) {
		return 0;
	}
	if (strncmp(result, "null", 4) == 0) {
		return 1;
	}

	const char *array_start = NULL;
	const char *array_end = NULL;
	if (result[0] == '[') {
		array_start = result;
		array_end = editorLspFindJsonArrayEnd(array_start);
	} else if (result[0] == '{') {
		const char *result_end = editorLspFindJsonObjectEnd(result);
		if (result_end == NULL) {
			return 0;
		}
		const char *items_key = editorLspStrstrBounded(result, "\"items\"", result_end);
		if (items_key == NULL) {
			return 1;
		}
		const char *items_colon = strchr(items_key, ':');
		if (items_colon == NULL || items_colon >= result_end) {
			return 0;
		}
		const char *items_value = editorLspSkipWs(items_colon + 1);
		if (items_value == NULL || items_value[0] != '[') {
			return 1;
		}
		array_start = items_value;
		array_end = editorLspFindJsonArrayEnd(array_start);
	} else {
		return 0;
	}
	if (array_start == NULL || array_end == NULL) {
		return 0;
	}

	struct editorLspCompletionItem *items = NULL;
	int count = 0;
	int cap = 0;

	const char *scan = array_start + 1;
	while (scan < array_end) {
		const char *object_start = strchr(scan, '{');
		if (object_start == NULL || object_start >= array_end) {
			break;
		}
		const char *object_end = editorLspFindJsonObjectEnd(object_start);
		if (object_end == NULL || object_end > array_end) {
			break;
		}
		struct editorLspCompletionItem item = {0};
		if (lspResponsesParseCompletionItemObject(object_start, object_end, &item)) {
			if (count >= cap) {
				int new_cap = cap > 0 ? cap * 2 : 8;
				struct editorLspCompletionItem *grown =
				        realloc(items, sizeof(*items) * (size_t)new_cap);
				if (grown == NULL) {
					editorLspFreeCompletionItems(items, count);
					free(item.label);
					free(item.insert_text);
					free(item.text_edit_new_text);
					return 0;
				}
				items = grown;
				cap = new_cap;
			}
			items[count++] = item;
		} else {
			free(item.label);
			free(item.insert_text);
			free(item.text_edit_new_text);
		}
		scan = object_end;
	}

	*items_out = items;
	*count_out = count;
	return 1;
}

static int lspResponsesAppendLocation(struct editorLspLocation **locations, int *count, int *cap,
                                      const char *path, int line, int character) {
	if (locations == NULL || count == NULL || cap == NULL || path == NULL) {
		return 0;
	}
	if (*count >= *cap) {
		int new_cap = *cap > 0 ? *cap * 2 : 4;
		if (new_cap < *count + 1) {
			new_cap = *count + 1;
		}
		size_t bytes = 0;
		if (!editorSizeMul(sizeof(**locations), (size_t)new_cap, &bytes)) {
			return 0;
		}
		struct editorLspLocation *grown = realloc(*locations, bytes);
		if (grown == NULL) {
			return 0;
		}
		*locations = grown;
		*cap = new_cap;
	}

	char *path_dup = strdup(path);
	if (path_dup == NULL) {
		return 0;
	}
	(*locations)[*count].path = path_dup;
	(*locations)[*count].line = line;
	(*locations)[*count].character = character;
	(*count)++;
	return 1;
}

static int lspResponsesParseLocationObjects(const char *result_json, const char *uri_key,
                                            const char *range_key_primary,
                                            const char *range_key_fallback,
                                            struct editorLspLocation **locations_out,
                                            int *count_out) {
	struct editorLspLocation *locations = NULL;
	int count = 0;
	int cap = 0;

	char key_pattern[64];
	int key_written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", uri_key);
	if (key_written <= 0 || (size_t)key_written >= sizeof(key_pattern)) {
		return 0;
	}

	const char *scan = result_json;
	while (scan != NULL) {
		const char *object_start = strchr(scan, '{');
		if (object_start == NULL) {
			break;
		}
		const char *object_end = editorLspFindJsonObjectEnd(object_start);
		if (object_end == NULL) {
			editorLspFreeLocations(locations, count);
			return 0;
		}
		scan = object_end;

		const char *uri_key_pos =
		        editorLspStrstrBounded(object_start, key_pattern, object_end);
		if (uri_key_pos == NULL) {
			continue;
		}

		const char *uri_colon = strchr(uri_key_pos, ':');
		if (uri_colon == NULL || uri_colon >= object_end) {
			continue;
		}
		const char *uri_value = editorLspSkipWs(uri_colon + 1);
		if (uri_value == NULL || uri_value[0] != '"') {
			continue;
		}

		char *uri = NULL;
		const char *after_uri = NULL;
		if (!editorLspParseJsonString(uri_value, &uri, &after_uri)) {
			continue;
		}
		(void)after_uri;

		char primary_pattern[64];
		int primary_written = snprintf(primary_pattern, sizeof(primary_pattern), "\"%s\"",
		                               range_key_primary);
		if (primary_written <= 0 || (size_t)primary_written >= sizeof(primary_pattern)) {
			free(uri);
			editorLspFreeLocations(locations, count);
			return 0;
		}

		const char *range_pos =
		        editorLspStrstrBounded(object_start, primary_pattern, object_end);
		if (range_pos == NULL && range_key_fallback != NULL) {
			char fallback_pattern[64];
			int fallback_written = snprintf(fallback_pattern, sizeof(fallback_pattern),
			                                "\"%s\"", range_key_fallback);
			if (fallback_written <= 0 ||
			    (size_t)fallback_written >= sizeof(fallback_pattern)) {
				free(uri);
				editorLspFreeLocations(locations, count);
				return 0;
			}
			range_pos =
			        editorLspStrstrBounded(object_start, fallback_pattern, object_end);
		}

		int line = -1;
		int character = -1;
		if (range_pos != NULL) {
			(void)lspResponsesParsePositionFromStart(range_pos, object_end, &line,
			                                         &character);
		}

		if (line >= 0 && character >= 0) {
			char *path = editorLspDecodeFileUri(uri);
			if (path != NULL) {
				if (!lspResponsesAppendLocation(&locations, &count, &cap, path,
				                                line, character)) {
					free(path);
					free(uri);
					editorLspFreeLocations(locations, count);
					return 0;
				}
				free(path);
			}
		}

		free(uri);
	}

	*locations_out = locations;
	*count_out = count;
	return 1;
}

int editorLspParseDefinitionLocations(const char *response_json,
                                      struct editorLspLocation **locations_out, int *count_out) {
	*locations_out = NULL;
	*count_out = 0;

	const char *result_key = strstr(response_json, "\"result\"");
	if (result_key == NULL) {
		return 0;
	}
	const char *result_colon = strchr(result_key, ':');
	if (result_colon == NULL) {
		return 0;
	}
	const char *result = editorLspSkipWs(result_colon + 1);
	if (result == NULL) {
		return 0;
	}
	if (strncmp(result, "null", 4) == 0) {
		return 1;
	}

	if (strstr(result, "\"targetUri\"") != NULL) {
		return lspResponsesParseLocationObjects(result, "targetUri", "targetSelectionRange",
		                                        "targetRange", locations_out, count_out);
	}
	return lspResponsesParseLocationObjects(result, "uri", "range", NULL, locations_out,
	                                        count_out);
}

int editorLspCopyLocations(struct editorLspLocation **out_locations, int *out_count,
                           const struct editorLspLocation *locations, int count) {
	*out_locations = NULL;
	*out_count = 0;
	if (count <= 0) {
		return 1;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(struct editorLspLocation), (size_t)count, &bytes)) {
		return 0;
	}
	struct editorLspLocation *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		copy[i].line = locations[i].line;
		copy[i].character = locations[i].character;
		if (locations[i].path != NULL) {
			copy[i].path = strdup(locations[i].path);
			if (copy[i].path == NULL) {
				editorLspFreeLocations(copy, count);
				return 0;
			}
		}
	}

	*out_locations = copy;
	*out_count = count;
	return 1;
}

void editorLspFreeSymbols(struct editorLspSymbol *symbols, int count) {
	if (symbols == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(symbols[i].name);
	}
	free(symbols);
}

int editorLspCopySymbols(struct editorLspSymbol **out_symbols, int *out_count,
                         const struct editorLspSymbol *symbols, int count) {
	if (out_symbols == NULL || out_count == NULL) {
		return 0;
	}
	*out_symbols = NULL;
	*out_count = 0;
	if (symbols == NULL || count <= 0) {
		return 1;
	}
	struct editorLspSymbol *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}
	for (int i = 0; i < count; i++) {
		copy[i].kind = symbols[i].kind;
		copy[i].line = symbols[i].line;
		copy[i].character = symbols[i].character;
		copy[i].depth = symbols[i].depth;
		copy[i].parent_index = symbols[i].parent_index;
		copy[i].is_last_sibling = symbols[i].is_last_sibling;
		const char *name = symbols[i].name != NULL ? symbols[i].name : "";
		copy[i].name = strdup(name);
		if (copy[i].name == NULL) {
			editorLspFreeSymbols(copy, i);
			return 0;
		}
	}
	*out_symbols = copy;
	*out_count = count;
	return 1;
}

const char *editorLspSymbolKindLabel(int kind) {
	switch (kind) {
		case 1:
			return "File";
		case 2:
			return "Module";
		case 3:
			return "Namespace";
		case 4:
			return "Package";
		case 5:
			return "Class";
		case 6:
			return "Method";
		case 7:
			return "Property";
		case 8:
			return "Field";
		case 9:
			return "Constructor";
		case 10:
			return "Enum";
		case 11:
			return "Interface";
		case 12:
			return "Function";
		case 13:
			return "Variable";
		case 14:
			return "Constant";
		case 15:
			return "String";
		case 16:
			return "Number";
		case 17:
			return "Boolean";
		case 18:
			return "Array";
		case 19:
			return "Object";
		case 20:
			return "Key";
		case 21:
			return "Null";
		case 22:
			return "EnumMember";
		case 23:
			return "Struct";
		case 24:
			return "Event";
		case 25:
			return "Operator";
		case 26:
			return "TypeParameter";
		default:
			return "Symbol";
	}
}

static int lspResponsesAppendSymbol(struct editorLspSymbol **symbols, int *count, int *cap,
                                    char *name, int kind, int line, int character, int depth,
                                    int parent_index) {
	if (*count >= *cap) {
		int new_cap = *cap > 0 ? *cap * 2 : 8;
		struct editorLspSymbol *grown =
		        realloc(*symbols, (size_t)new_cap * sizeof(**symbols));
		if (grown == NULL) {
			return 0;
		}
		*symbols = grown;
		*cap = new_cap;
	}
	(*symbols)[*count].name = name;
	(*symbols)[*count].kind = kind;
	(*symbols)[*count].line = line;
	(*symbols)[*count].character = character;
	(*symbols)[*count].depth = depth;
	(*symbols)[*count].parent_index = parent_index;
	(*symbols)[*count].is_last_sibling = 0;
	(*count)++;
	return 1;
}

static int lspResponsesParseDocumentSymbolObject(const char *object_start, const char *object_end,
                                                 struct editorLspSymbol **symbols, int *count,
                                                 int *cap, int depth, int parent_index) {
	const char *name_key = editorLspFindTopLevelKey(object_start, object_end, "\"name\"");
	if (name_key == NULL) {
		return 1;
	}
	char *name = NULL;
	const char *name_colon = strchr(name_key, ':');
	if (name_colon == NULL || name_colon >= object_end) {
		return 1;
	}
	const char *name_value = editorLspSkipWs(name_colon + 1);
	if (name_value == NULL || name_value[0] != '"' ||
	    !editorLspParseJsonString(name_value, &name, NULL) || name == NULL) {
		return 1;
	}

	int kind = 0;
	const char *kind_key = editorLspFindTopLevelKey(object_start, object_end, "\"kind\"");
	if (kind_key != NULL) {
		const char *colon = strchr(kind_key, ':');
		if (colon != NULL && colon < object_end) {
			(void)editorLspParseJsonInt(editorLspSkipWs(colon + 1), &kind, NULL);
		}
	}

	int line = 0;
	int character = 0;
	int has_position = 0;
	const char *location_key =
	        editorLspFindTopLevelKey(object_start, object_end, "\"location\"");
	if (location_key != NULL) {
		const char *location_colon = strchr(location_key, ':');
		if (location_colon != NULL && location_colon < object_end) {
			const char *location_value = editorLspSkipWs(location_colon + 1);
			if (location_value != NULL && *location_value == '{') {
				const char *location_end =
				        editorLspFindJsonObjectEnd(location_value);
				if (location_end != NULL) {
					const char *range_key = editorLspFindTopLevelKey(
					        location_value, location_end, "\"range\"");
					if (range_key != NULL) {
						has_position = editorLspParsePositionFromKey(
						        range_key, "start", location_end, &line,
						        &character);
					}
				}
			}
		}
	}
	if (!has_position) {
		const char *range_key =
		        editorLspFindTopLevelKey(object_start, object_end, "\"range\"");
		if (range_key != NULL) {
			has_position = editorLspParsePositionFromKey(range_key, "start", object_end,
			                                             &line, &character);
		}
	}
	if (!has_position) {
		const char *sel_key =
		        editorLspFindTopLevelKey(object_start, object_end, "\"selectionRange\"");
		if (sel_key != NULL) {
			has_position = editorLspParsePositionFromKey(sel_key, "start", object_end,
			                                             &line, &character);
		}
	}

	int my_index = *count;
	if (!lspResponsesAppendSymbol(symbols, count, cap, name, kind, line, character, depth,
	                              parent_index)) {
		free(name);
		return 0;
	}

	const char *children_key =
	        editorLspFindTopLevelKey(object_start, object_end, "\"children\"");
	if (children_key != NULL) {
		const char *children_colon = strchr(children_key, ':');
		if (children_colon != NULL && children_colon < object_end) {
			const char *children_value = editorLspSkipWs(children_colon + 1);
			if (children_value != NULL && *children_value == '[') {
				const char *children_end =
				        editorLspFindJsonArrayEnd(children_value);
				if (children_end != NULL) {
					int last_child_index = -1;
					const char *p = children_value + 1;
					while (p < children_end) {
						p = editorLspSkipWs(p);
						if (p == NULL || *p == ']') {
							break;
						}
						if (*p != '{') {
							p++;
							continue;
						}
						const char *child_end =
						        editorLspFindJsonObjectEnd(p);
						if (child_end == NULL) {
							break;
						}
						int child_index = *count;
						if (!lspResponsesParseDocumentSymbolObject(
						            p, child_end, symbols, count, cap,
						            depth + 1, my_index)) {
							return 0;
						}
						last_child_index = child_index;
						p = child_end + 1;
						p = editorLspSkipWs(p);
						if (p == NULL) {
							break;
						}
						if (*p == ',') {
							p++;
						}
					}
					if (last_child_index >= 0) {
						(*symbols)[last_child_index].is_last_sibling = 1;
					}
				}
			}
		}
	}
	return 1;
}

int editorLspParseDocumentSymbols(const char *response_json, struct editorLspSymbol **symbols_out,
                                  int *count_out) {
	if (symbols_out == NULL || count_out == NULL) {
		return 0;
	}
	*symbols_out = NULL;
	*count_out = 0;

	const char *result_key = strstr(response_json, "\"result\"");
	if (result_key == NULL) {
		return 0;
	}
	const char *result_colon = strchr(result_key, ':');
	if (result_colon == NULL) {
		return 0;
	}
	const char *result = editorLspSkipWs(result_colon + 1);
	if (result == NULL) {
		return 0;
	}
	if (strncmp(result, "null", 4) == 0) {
		return 1;
	}
	if (*result != '[') {
		return 0;
	}
	const char *array_end = editorLspFindJsonArrayEnd(result);
	if (array_end == NULL) {
		return 0;
	}

	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	int cap = 0;
	int last_top_index = -1;
	const char *p = result + 1;
	while (p < array_end) {
		p = editorLspSkipWs(p);
		if (p == NULL || *p == ']') {
			break;
		}
		if (*p != '{') {
			p++;
			continue;
		}
		const char *obj_end = editorLspFindJsonObjectEnd(p);
		if (obj_end == NULL) {
			break;
		}
		int top_index = count;
		if (!lspResponsesParseDocumentSymbolObject(p, obj_end, &symbols, &count, &cap, 0,
		                                           -1)) {
			editorLspFreeSymbols(symbols, count);
			return 0;
		}
		last_top_index = top_index;
		p = obj_end + 1;
		p = editorLspSkipWs(p);
		if (p == NULL) {
			break;
		}
		if (*p == ',') {
			p++;
		}
	}
	if (last_top_index >= 0) {
		symbols[last_top_index].is_last_sibling = 1;
	}

	*symbols_out = symbols;
	*count_out = count;
	return 1;
}

void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count) {
	if (diagnostics == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(diagnostics[i].message);
	}
	free(diagnostics);
}

int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
                             const struct editorLspDiagnostic *diagnostics, int count) {
	if (out_diagnostics == NULL || out_count == NULL) {
		return 0;
	}
	*out_diagnostics = NULL;
	*out_count = 0;
	if (diagnostics == NULL || count <= 0) {
		return 1;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(*diagnostics), (size_t)count, &bytes)) {
		return 0;
	}
	struct editorLspDiagnostic *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		copy[i].start_line = diagnostics[i].start_line;
		copy[i].start_character = diagnostics[i].start_character;
		copy[i].end_line = diagnostics[i].end_line;
		copy[i].end_character = diagnostics[i].end_character;
		copy[i].severity = diagnostics[i].severity;
		if (diagnostics[i].message != NULL) {
			copy[i].message = strdup(diagnostics[i].message);
			if (copy[i].message == NULL) {
				editorLspFreeDiagnostics(copy, count);
				return 0;
			}
		}
	}

	*out_diagnostics = copy;
	*out_count = count;
	return 1;
}

void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count) {
	if (edits == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(edits[i].new_text);
	}
	free(edits);
}

int editorLspParseWorkspaceEditChanges(const char *edit_json, const char *target_path,
                                       struct editorLspPendingEdit **edits_out, int *count_out) {
	if (edits_out == NULL || count_out == NULL) {
		return 0;
	}
	*edits_out = NULL;
	*count_out = 0;

	const char *changes_key = strstr(edit_json, "\"changes\"");
	if (changes_key == NULL) {
		return 1;
	}
	const char *changes_colon = strchr(changes_key, ':');
	if (changes_colon == NULL) {
		return 0;
	}
	const char *changes_object = strchr(changes_colon + 1, '{');
	if (changes_object == NULL) {
		return 0;
	}
	const char *changes_end = editorLspFindJsonObjectEnd(changes_object);
	if (changes_end == NULL) {
		return 0;
	}

	const char *scan = changes_object + 1;
	while (scan < changes_end) {
		const char *key = strchr(scan, '"');
		if (key == NULL || key >= changes_end) {
			break;
		}
		char *uri = NULL;
		const char *after_key = NULL;
		if (!editorLspParseJsonString(key, &uri, &after_key) || uri == NULL) {
			return 0;
		}
		const char *colon = strchr(after_key, ':');
		if (colon == NULL || colon >= changes_end) {
			free(uri);
			return 0;
		}
		const char *array_start = strchr(colon + 1, '[');
		if (array_start == NULL || array_start >= changes_end) {
			free(uri);
			return 0;
		}
		const char *array_end = editorLspFindJsonArrayEnd(array_start);
		if (array_end == NULL || array_end > changes_end) {
			free(uri);
			return 0;
		}

		char *path = editorLspDecodeFileUri(uri);
		free(uri);
		int path_matches_target = path != NULL && target_path != NULL &&
		                          editorPathsReferToSameFile(path, target_path);
		if (path_matches_target) {
			struct editorLspPendingEdit *edits = NULL;
			int count = 0;
			int cap = 0;
			const char *item_scan = array_start + 1;
			while (item_scan < array_end) {
				const char *object_start = strchr(item_scan, '{');
				if (object_start == NULL || object_start >= array_end) {
					break;
				}
				const char *object_end = editorLspFindJsonObjectEnd(object_start);
				if (object_end == NULL || object_end > array_end) {
					editorLspFreePendingEdits(edits, count);
					free(path);
					return 0;
				}
				item_scan = object_end;

				const char *range_key = editorLspStrstrBounded(
				        object_start, "\"range\"", object_end);
				const char *range_colon =
				        range_key != NULL ? strchr(range_key, ':') : NULL;
				const char *range_object =
				        range_colon != NULL ? strchr(range_colon + 1, '{') : NULL;
				const char *range_end =
				        range_object != NULL
				                ? editorLspFindJsonObjectEnd(range_object)
				                : NULL;
				char *new_text = NULL;
				if (!editorLspFindStringField(object_start, "newText", &new_text) ||
				    new_text == NULL) {
					new_text = strdup("");
				}
				if (new_text == NULL) {
					editorLspFreePendingEdits(edits, count);
					free(path);
					return 0;
				}

				int start_line = 0;
				int start_character = 0;
				int end_line = 0;
				int end_character = 0;
				if (range_end != NULL &&
				    editorLspParsePositionFromKey(range_object, "start", range_end,
				                                  &start_line, &start_character) &&
				    editorLspParsePositionFromKey(range_object, "end", range_end,
				                                  &end_line, &end_character)) {
					if (count >= cap) {
						int new_cap = cap > 0 ? cap * 2 : 4;
						size_t bytes = 0;
						if (!editorSizeMul(sizeof(*edits), (size_t)new_cap,
						                   &bytes)) {
							free(new_text);
							editorLspFreePendingEdits(edits, count);
							free(path);
							return 0;
						}
						struct editorLspPendingEdit *grown =
						        realloc(edits, bytes);
						if (grown == NULL) {
							free(new_text);
							editorLspFreePendingEdits(edits, count);
							free(path);
							return 0;
						}
						edits = grown;
						cap = new_cap;
					}
					edits[count].start_line = start_line;
					edits[count].start_character = start_character;
					edits[count].end_line = end_line;
					edits[count].end_character = end_character;
					edits[count].new_text = new_text;
					count++;
				} else {
					free(new_text);
				}
			}
			free(path);
			*edits_out = edits;
			*count_out = count;
			return 1;
		}
		free(path);
		scan = array_end;
	}

	return 1;
}
