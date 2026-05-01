#include "test_case.h"
#include "test_support.h"

static int test_utf8_decode_valid_sequences(void) {
	unsigned int cp = 0;
	const char e_acute[] = "\xC3\xA9";
	const char euro[] = "\xE2\x82\xAC";
	const char grin[] = "\xF0\x9F\x98\x80";

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint("A", 1, &cp));
	ASSERT_EQ_INT('A', cp);

	ASSERT_EQ_INT(2, editorUtf8DecodeCodepoint(e_acute, (int)sizeof(e_acute) - 1, &cp));
	ASSERT_EQ_INT(0xE9, cp);

	ASSERT_EQ_INT(3, editorUtf8DecodeCodepoint(euro, (int)sizeof(euro) - 1, &cp));
	ASSERT_EQ_INT(0x20AC, cp);

	ASSERT_EQ_INT(4, editorUtf8DecodeCodepoint(grin, (int)sizeof(grin) - 1, &cp));
	ASSERT_EQ_INT(0x1F600, cp);

	return 0;
}

static int test_utf8_decode_invalid_sequences(void) {
	unsigned int cp = 0;
	const char invalid_lead[] = "\xFF";
	const char truncated[] = "\xC3";
	const char bad_continuation[] = "\xE2\x28\xA1";
	const char overlong[] = "\xC0\xAF";

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(invalid_lead, (int)sizeof(invalid_lead) - 1,
				&cp));
	ASSERT_EQ_INT(0xFF, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(truncated, (int)sizeof(truncated) - 1, &cp));
	ASSERT_EQ_INT(0xC3, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(bad_continuation,
				(int)sizeof(bad_continuation) - 1, &cp));
	ASSERT_EQ_INT(0xE2, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(overlong, (int)sizeof(overlong) - 1, &cp));
	ASSERT_EQ_INT(0xC0, cp);

	return 0;
}

static int test_rope_copy_and_dup_range(void) {
	struct editorRope rope;
	editorRopeInit(&rope);

	ASSERT_TRUE(editorRopeResetFromString(&rope, "hello world", strlen("hello world")));
	ASSERT_EQ_INT((int)strlen("hello world"), (int)editorRopeLength(&rope));

	uint32_t read_len = 0;
	const char *chunk = editorRopeRead(&rope, 6, &read_len);
	ASSERT_TRUE(chunk != NULL);
	ASSERT_EQ_INT(5, read_len);
	ASSERT_TRUE(strncmp(chunk, "world", 5) == 0);

	char copy[6] = {0};
	ASSERT_TRUE(editorRopeCopyRange(&rope, 0, 5, copy));
	ASSERT_EQ_STR("hello", copy);

	size_t dup_len = 0;
	char *dup = editorRopeDupRange(&rope, 6, 11, &dup_len);
	ASSERT_TRUE(dup != NULL);
	ASSERT_EQ_INT(5, (int)dup_len);
	ASSERT_EQ_STR("world", dup);

	free(dup);
	editorRopeFree(&rope);
	return 0;
}

static int test_rope_replace_range_across_large_text(void) {
	struct editorRope rope;
	editorRopeInit(&rope);

	size_t source_len = 2400;
	char *source = malloc(source_len + 1);
	ASSERT_TRUE(source != NULL);
	for (size_t i = 0; i < source_len; i++) {
		source[i] = (char)('a' + (int)(i % 26));
	}
	source[source_len] = '\0';

	ASSERT_TRUE(editorRopeResetFromString(&rope, source, source_len));
	ASSERT_TRUE(editorRopeReplaceRange(&rope, 900, 700, "XYZ", 3));
	ASSERT_EQ_INT((int)(source_len - 700 + 3), (int)editorRopeLength(&rope));

	size_t full_len = 0;
	char *full = editorRopeDupRange(&rope, 0, editorRopeLength(&rope), &full_len);
	ASSERT_TRUE(full != NULL);
	ASSERT_EQ_INT((int)(source_len - 700 + 3), (int)full_len);
	ASSERT_TRUE(memcmp(full, source, 900) == 0);
	ASSERT_TRUE(memcmp(full + 900, "XYZ", 3) == 0);
	ASSERT_TRUE(memcmp(full + 903, source + 1600, source_len - 1600) == 0);

	free(full);
	free(source);
	editorRopeFree(&rope);
	return 0;
}

static int test_document_line_index_tracks_blank_lines_and_trailing_newline(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "alpha\n\nbeta\ngamma\n";
	ASSERT_TRUE(editorDocumentResetFromString(&document, text, strlen(text)));
	ASSERT_EQ_INT((int)strlen(text), (int)editorDocumentLength(&document));
	ASSERT_EQ_INT(4, editorDocumentLineCount(&document));

	size_t line_start = 0;
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 0, &line_start));
	ASSERT_EQ_INT(0, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 1, &line_start));
	ASSERT_EQ_INT(6, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 2, &line_start));
	ASSERT_EQ_INT(7, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 3, &line_start));
	ASSERT_EQ_INT(12, (int)line_start);

	int line_idx = -1;
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 0, &line_idx));
	ASSERT_EQ_INT(0, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 6, &line_idx));
	ASSERT_EQ_INT(1, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 8, &line_idx));
	ASSERT_EQ_INT(2, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 16, &line_idx));
	ASSERT_EQ_INT(3, line_idx);

	editorDocumentFree(&document);
	return 0;
}

static int test_document_replace_range_updates_text_and_line_index(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "one\ntwo\nthree\n";
	ASSERT_TRUE(editorDocumentResetFromString(&document, text, strlen(text)));
	ASSERT_TRUE(editorDocumentReplaceRange(&document, 4, 4, "two\n2b\n", 7));
	ASSERT_EQ_INT(4, editorDocumentLineCount(&document));

	size_t line_start = 0;
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 0, &line_start));
	ASSERT_EQ_INT(0, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 1, &line_start));
	ASSERT_EQ_INT(4, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 2, &line_start));
	ASSERT_EQ_INT(8, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 3, &line_start));
	ASSERT_EQ_INT(11, (int)line_start);

	size_t full_len = 0;
	char *full = editorDocumentDupRange(&document, 0, editorDocumentLength(&document), &full_len);
	ASSERT_TRUE(full != NULL);
	ASSERT_EQ_INT((int)strlen("one\ntwo\n2b\nthree\n"), (int)full_len);
	ASSERT_EQ_STR("one\ntwo\n2b\nthree\n", full);

	int line_idx = -1;
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 10, &line_idx));
	ASSERT_EQ_INT(2, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, full_len, &line_idx));
	ASSERT_EQ_INT(3, line_idx);

	free(full);
	editorDocumentFree(&document);
	return 0;
}

static int test_document_reset_from_text_source_streams_bytes(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "alpha\nbeta\ngamma\n";
	struct editorTextSource source = {0};
	editorTextSourceInitString(&source, text, strlen(text));
	ASSERT_TRUE(editorDocumentResetFromTextSource(&document, &source));
	ASSERT_EQ_INT((int)strlen(text), (int)editorDocumentLength(&document));

	size_t len = 0;
	char *dup = editorDocumentDupRange(&document, 0, editorDocumentLength(&document), &len);
	ASSERT_TRUE(dup != NULL);
	ASSERT_EQ_INT((int)strlen(text), (int)len);
	ASSERT_EQ_STR(text, dup);

	free(dup);
	editorDocumentFree(&document);
	return 0;
}

static int test_document_position_offset_roundtrip(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "alpha\nbeta\n";
	ASSERT_TRUE(editorDocumentResetFromString(&document, text, strlen(text)));

	size_t offset = 0;
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 0, 0, &offset));
	ASSERT_EQ_INT(0, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 0, 5, &offset));
	ASSERT_EQ_INT(5, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 1, 0, &offset));
	ASSERT_EQ_INT(6, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 1, 4, &offset));
	ASSERT_EQ_INT(10, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 2, 0, &offset));
	ASSERT_EQ_INT(11, (int)offset);

	int line_idx = -1;
	size_t column = 0;
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 0, &line_idx, &column));
	ASSERT_EQ_INT(0, line_idx);
	ASSERT_EQ_INT(0, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 5, &line_idx, &column));
	ASSERT_EQ_INT(0, line_idx);
	ASSERT_EQ_INT(5, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 6, &line_idx, &column));
	ASSERT_EQ_INT(1, line_idx);
	ASSERT_EQ_INT(0, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 10, &line_idx, &column));
	ASSERT_EQ_INT(1, line_idx);
	ASSERT_EQ_INT(4, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 11, &line_idx, &column));
	ASSERT_EQ_INT(2, line_idx);
	ASSERT_EQ_INT(0, (int)column);

	editorDocumentFree(&document);
	return 0;
}

static int test_editor_build_active_text_source_uses_document_after_open(void) {
	char path[64];
	ASSERT_TRUE(write_temp_text_file(path, sizeof(path), "alpha\nbeta\n"));

	editorDocumentTestResetStats();
	ASSERT_TRUE(editorOpen(path));
	ASSERT_EQ_INT(1, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorRowCacheTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorRowCacheTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorRowCacheTestFullRebuildCount());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_document_incremental_updates_for_basic_edits(void) {
	add_row("abc");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	editorDocumentTestResetStats();

	E.cy = 0;
	E.cx = 2;
	editorInsertChar('x');
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	E.cx = 1;
	editorInsertNewline();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	E.cy = 1;
	E.cx = 1;
	editorDelChar();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 1
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&range));
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(4, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, editorRowCacheTestFullRebuildCount());
	ASSERT_EQ_INT(4, editorRowCacheTestSpliceUpdateCount());
	return 0;
}

static int test_editor_buffer_offset_roundtrip_uses_document_mapping(void) {
	add_row("alpha");
	add_row("beta");

	size_t offset = 0;
	ASSERT_TRUE(editorBufferPosToOffset(0, 0, &offset));
	ASSERT_EQ_INT(0, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(0, 5, &offset));
	ASSERT_EQ_INT(5, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(1, 0, &offset));
	ASSERT_EQ_INT(6, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(1, 4, &offset));
	ASSERT_EQ_INT(10, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(2, 0, &offset));
	ASSERT_EQ_INT(11, (int)offset);

	int cy = -1;
	int cx = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(0, &cy, &cx));
	ASSERT_EQ_INT(0, cy);
	ASSERT_EQ_INT(0, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(5, &cy, &cx));
	ASSERT_EQ_INT(0, cy);
	ASSERT_EQ_INT(5, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(6, &cy, &cx));
	ASSERT_EQ_INT(1, cy);
	ASSERT_EQ_INT(0, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(10, &cy, &cx));
	ASSERT_EQ_INT(1, cy);
	ASSERT_EQ_INT(4, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(11, &cy, &cx));
	ASSERT_EQ_INT(2, cy);
	ASSERT_EQ_INT(0, cx);
	return 0;
}

static int test_editor_buffer_offsets_rebuild_document_after_row_mutation(void) {
	add_row("alpha");
	add_row("beta");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	editorDocumentTestResetStats();

	E.cy = 1;
	E.cx = 2;
	editorInsertChar('X');
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());

	size_t offset = 0;
	ASSERT_TRUE(editorBufferPosToOffset(1, 5, &offset));
	ASSERT_EQ_INT(11, (int)offset);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());

	int cy = -1;
	int cx = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(11, &cy, &cx));
	ASSERT_EQ_INT(1, cy);
	ASSERT_EQ_INT(5, cx);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	return 0;
}

static int test_editor_document_lazy_rebuild_after_low_level_row_mutation(void) {
	add_row("abc");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	editorDocumentTestResetStats();

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('X');
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	return 0;
}

static int test_editor_document_restored_for_undo_redo(void) {
	add_row("abc");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	E.cy = 0;
	E.cx = 1;
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	editorInsertChar('X');
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, 1);
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	return 0;
}

static int test_editor_document_edit_capture_uses_active_source(void) {
	add_row("alpha");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	editorActiveTextSourceBuildTestResetCount();
	editorActiveTextSourceDupTestResetCount();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceBuildTestCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	ASSERT_EQ_INT(0, E.edit_pending_entry_valid);

	editorInsertChar('Z');
	ASSERT_EQ_INT(1, E.edit_pending_entry_valid);
	ASSERT_EQ_INT(EDITOR_EDIT_INSERT_TEXT, E.edit_pending_entry.kind);
	ASSERT_EQ_INT(0, (int)E.edit_pending_entry.removed_len);
	ASSERT_EQ_INT(1, (int)E.edit_pending_entry.inserted_len);
	ASSERT_TRUE(E.edit_pending_entry.inserted_text != NULL);
	ASSERT_MEM_EQ("Z", E.edit_pending_entry.inserted_text, 1);

	editorHistoryDiscardEdit();
	ASSERT_EQ_INT(0, E.edit_pending_entry_valid);
	return 0;
}

static int test_editor_document_selection_and_delete_use_active_source(void) {
	add_row("alpha");
	add_row("beta");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 1,
		.end_cy = 1,
		.end_cx = 2
	};

	editorDocumentTestResetStats();
	editorActiveTextSourceDupTestResetCount();
	char *selected = NULL;
	size_t selected_len = 0;
	ASSERT_EQ_INT(1, editorExtractRangeText(&range, &selected, &selected_len));
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	ASSERT_EQ_INT(7, (int)selected_len);
	ASSERT_TRUE(selected != NULL);
	ASSERT_MEM_EQ("lpha\nbe", selected, selected_len);
	free(selected);

	editorDocumentTestResetStats();
	editorActiveTextSourceBuildTestResetCount();
	editorActiveTextSourceDupTestResetCount();
	ASSERT_EQ_INT(1, editorDeleteRange(&range));
	ASSERT_EQ_INT(0, editorActiveTextSourceBuildTestCount());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	return 0;
}

static int test_editor_buffer_find_uses_active_source_without_full_dup(void) {
	add_row("alpha");
	add_row("beta");

	editorActiveTextSourceDupTestResetCount();
	int row = -1;
	int col = -1;
	ASSERT_TRUE(editorBufferFindForward("ta", 0, -1, &row, &col));
	ASSERT_EQ_INT(1, row);
	ASSERT_EQ_INT(2, col);
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());

	ASSERT_TRUE(editorBufferFindBackward("ph", 1, 4, &row, &col));
	ASSERT_EQ_INT(0, row);
	ASSERT_EQ_INT(2, col);
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	return 0;
}

static int test_editor_document_save_uses_active_source(void) {
	char path[] = "/tmp/rotide-test-save-mirror-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorDocumentTestResetStats();
	editorSave();
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, E.dirty);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, (int)content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);
	free(contents);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_utf8_continuation_detection(void) {
	ASSERT_TRUE(editorIsUtf8ContinuationByte(0x80));
	ASSERT_TRUE(editorIsUtf8ContinuationByte(0xBF));
	ASSERT_TRUE(!editorIsUtf8ContinuationByte(0x7F));
	ASSERT_TRUE(!editorIsUtf8ContinuationByte(0xC2));
	return 0;
}

static int test_grapheme_extend_classification(void) {
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x0301));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0xFE0F));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x1F3FB));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x200C));
	ASSERT_TRUE(!editorIsGraphemeExtendCodepoint(0x200D));
	ASSERT_TRUE(!editorIsGraphemeExtendCodepoint('A'));
	return 0;
}

static int test_char_display_width_basics(void) {
	const char e_acute[] = "\xC3\xA9";
	const char invalid[] = "\xFF";

	ASSERT_EQ_INT(1, editorCharDisplayWidth("A", 1));
	ASSERT_EQ_INT(0, editorCharDisplayWidth(&e_acute[1], 1));
	ASSERT_EQ_INT(1, editorCharDisplayWidth(invalid, (int)sizeof(invalid) - 1));
	return 0;
}

static int test_row_char_boundaries(void) {
	const char text[] = "A\xC3\xA9" "Z";
	add_row_bytes(text, sizeof(text) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(1, editorRowClampCxToCharBoundary(row, 2));
	ASSERT_EQ_INT(0, editorRowPrevCharIdx(row, 1));
	ASSERT_EQ_INT(1, editorRowPrevCharIdx(row, 3));
	ASSERT_EQ_INT(3, editorRowNextCharIdx(row, 1));
	ASSERT_EQ_INT(4, editorRowNextCharIdx(row, 3));
	return 0;
}

static int test_row_cluster_boundaries_combining(void) {
	const char text[] = "a\xCC\x81" "b";
	add_row_bytes(text, sizeof(text) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(3, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT(0, editorRowPrevClusterIdx(row, 3));
	ASSERT_EQ_INT(4, editorRowNextClusterIdx(row, 3));
	ASSERT_EQ_INT(0, editorRowClampCxToClusterBoundary(row, 2));
	ASSERT_EQ_INT(3, editorRowClampCxToClusterBoundary(row, 3));
	return 0;
}

static int test_row_cluster_boundaries_zwj_sequence(void) {
	const char woman_technologist[] = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
	add_row_bytes(woman_technologist, sizeof(woman_technologist) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT((int)sizeof(woman_technologist) - 1, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT(0, editorRowPrevClusterIdx(row, (int)sizeof(woman_technologist) - 1));
	return 0;
}

static int test_row_cluster_boundaries_regional_indicators(void) {
	const char flag_sequence[] = "\xF0\x9F\x87\xAB\xF0\x9F\x87\xAE\xF0\x9F\x87\xA8";
	add_row_bytes(flag_sequence, sizeof(flag_sequence) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(8, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT((int)sizeof(flag_sequence) - 1, editorRowNextClusterIdx(row, 8));
	ASSERT_EQ_INT(8, editorRowPrevClusterIdx(row, (int)sizeof(flag_sequence) - 1));
	return 0;
}

static int test_row_cx_to_rx_with_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(8, editorRowCxToRx(row, 2));
	ASSERT_EQ_INT(9, editorRowCxToRx(row, 3));
	return 0;
}

static int test_row_rx_to_cx_with_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(0, editorRowRxToCx(row, 0));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 1));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 7));
	ASSERT_EQ_INT(2, editorRowRxToCx(row, 8));
	ASSERT_EQ_INT(3, editorRowRxToCx(row, 9));
	return 0;
}

static int test_editor_update_row_expands_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(9, row->rsize);
	ASSERT_EQ_STR("a       b", row->render);
	return 0;
}

static int test_editor_update_row_tab_alignment_after_multibyte(void) {
	const char text[] = "\xC3\xB6\tX";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	const char expected[] = "\xC3\xB6       X";
	ASSERT_EQ_INT((int)sizeof(expected) - 1, row->rsize);
	ASSERT_MEM_EQ(expected, row->render, (size_t)row->rsize);
	return 0;
}

static int test_editor_update_row_escapes_c0_and_esc_in_render(void) {
	const char text[] = "A\x1b\x7f";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	const char expected[] = "A^[^?";
	ASSERT_EQ_INT((int)sizeof(expected) - 1, row->rsize);
	ASSERT_MEM_EQ(expected, row->render, (size_t)row->rsize);
	return 0;
}

static int test_editor_update_row_escapes_c1_codepoints_in_render(void) {
	const char text[] = "\xC2\x9BZ";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	const char expected[] = "\\x9BZ";
	ASSERT_EQ_INT((int)sizeof(expected) - 1, row->rsize);
	ASSERT_MEM_EQ(expected, row->render, (size_t)row->rsize);
	return 0;
}

static int test_editor_update_row_preserves_printable_utf8_with_80_9f_continuations(void) {
	const char text[] = "\xC4\x80X";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	ASSERT_EQ_INT((int)sizeof(text) - 1, row->rsize);
	ASSERT_MEM_EQ(text, row->render, (size_t)row->rsize);
	return 0;
}

static int test_row_cx_to_rx_with_escaped_controls(void) {
	const char text[] = "A\x1b" "B";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	ASSERT_EQ_INT(0, editorRowCxToRx(row, 0));
	ASSERT_EQ_INT(1, editorRowCxToRx(row, 1));
	ASSERT_EQ_INT(3, editorRowCxToRx(row, 2));
	ASSERT_EQ_INT(4, editorRowCxToRx(row, 3));
	return 0;
}

static int test_row_rx_to_cx_with_escaped_controls(void) {
	const char text[] = "A\x1b" "B";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	ASSERT_EQ_INT(0, editorRowRxToCx(row, 0));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 1));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 2));
	ASSERT_EQ_INT(2, editorRowRxToCx(row, 3));
	ASSERT_EQ_INT(3, editorRowRxToCx(row, 4));
	return 0;
}

static int test_insert_and_delete_row_updates_dirty(void) {
	add_row("one");
	add_row("two");
	E.dirty = 0;
	ASSERT_EQ_INT(2, E.numrows);
	E.cy = 0;
	E.cx = 0;
	editorInsertNewline();
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_EQ_INT(1, E.dirty);

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&range));
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("one", E.rows[0].chars);
	ASSERT_EQ_STR("two", E.rows[1].chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_delete_row_rejects_idx_at_numrows(void) {
	add_row("only");
	E.dirty = 0;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 4,
		.end_cy = 0,
		.end_cx = 4
	};
	ASSERT_EQ_INT(0, editorDeleteRange(&range));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("only", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_insert_and_delete_chars(void) {
	add_row("abc");
	E.dirty = 0;

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('X');
	ASSERT_EQ_STR("aXbc", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.dirty);

	struct editorSelectionRange delete_one = {
		.start_cy = 0,
		.start_cx = 2,
		.end_cy = 0,
		.end_cx = 3
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_one));
	ASSERT_EQ_STR("aXc", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.dirty);

	struct editorSelectionRange delete_two = {
		.start_cy = 0,
		.start_cx = 1,
		.end_cy = 0,
		.end_cx = 3
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_two));
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(3, E.dirty);

	struct editorSelectionRange noop = {
		.start_cy = 0,
		.start_cx = 1,
		.end_cy = 0,
		.end_cx = 1
	};
	ASSERT_EQ_INT(0, editorDeleteRange(&noop));
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_del_char_at_rejects_idx_at_row_size(void) {
	add_row("abc");
	E.dirty = 0;

	struct editorSelectionRange noop = {
		.start_cy = 0,
		.start_cx = 3,
		.end_cy = 0,
		.end_cx = 3
	};
	ASSERT_EQ_INT(0, editorDeleteRange(&noop));
	ASSERT_EQ_INT(3, E.rows[0].size);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_editor_insert_char_creates_initial_row(void) {
	editorInsertChar('Q');
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("Q", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(2, E.dirty);
	return 0;
}

static int test_editor_insert_newline_splits_row(void) {
	add_row("hello");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 2;

	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("he", E.rows[0].chars);
	ASSERT_EQ_STR("llo", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_insert_newline_at_row_start(void) {
	add_row("hello");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;

	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("", E.rows[0].chars);
	ASSERT_EQ_STR("hello", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_del_char_cluster_and_merge(void) {
	const char with_combining[] = "a\xCC\x81" "b";
	add_row_bytes(with_combining, sizeof(with_combining) - 1);
	E.cy = 0;
	E.cx = 3;
	E.dirty = 0;

	editorDelChar();
	ASSERT_EQ_STR("b", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(1, E.dirty);

	reset_editor_state();
	add_row("abc");
	E.cy = 0;
	E.cx = 3;
	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_INT(1, editorInsertText("def", 3));
	E.dirty = 0;
	E.cy = 1;
	E.cx = 0;

	editorDelChar();
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abcdef", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(2, E.dirty);
	return 0;
}

static int test_editor_rows_to_str(void) {
	add_row("a");
	add_row("bc");
	add_row("");

	size_t buflen = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(6, buflen);
	ASSERT_MEM_EQ("a\nbc\n\n", joined, (size_t)buflen);
	free(joined);

	editorDocumentTestResetStats();
	E.cy = 1;
	E.cx = 1;
	editorInsertChar('X');
	joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(7, buflen);
	ASSERT_MEM_EQ("a\nbXc\n\n", joined, (size_t)buflen);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	free(joined);
	return 0;
}

static int test_editor_rows_to_str_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	size_t buflen = 0;
	errno = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(4, buflen);
	ASSERT_MEM_EQ("abc\n", joined, buflen);
	free(joined);
	return 0;
}

static int test_editor_open_reads_rows_and_clears_dirty(void) {
	char path[] = "/tmp/rotide-test-open-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, "alpha\r\nbeta\n\n", 13) == 0);
	ASSERT_TRUE(close(fd) == 0);

	editorOpen(path);

	ASSERT_EQ_STR(path, E.filename);
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_EQ_STR("alpha", E.rows[0].chars);
	ASSERT_EQ_STR("beta", E.rows[1].chars);
	ASSERT_EQ_STR("", E.rows[2].chars);
	ASSERT_EQ_INT(0, E.dirty);

	unlink(path);
	return 0;
}

static int test_editor_open_rejects_binary_file_without_mutating_buffer(void) {
	char path[] = "/tmp/rotide-test-open-binary-XXXXXX";
	const char bytes[] = {'r', 'o', 't', 'i', 'd', 'e', '\0', 'b', 'i', 'n'};
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, bytes, sizeof(bytes)) == 0);
	ASSERT_TRUE(close(fd) == 0);

	add_row("keep");
	E.dirty = 7;
	ASSERT_EQ_INT(0, editorOpen(path));

	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);
	ASSERT_EQ_INT(7, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Binary files are not supported") != NULL);

	unlink(path);
	return 0;
}

const struct editorTestCase g_document_text_editing_tests[] = {
	{"utf8_decode_valid_sequences", test_utf8_decode_valid_sequences},
	{"utf8_decode_invalid_sequences", test_utf8_decode_invalid_sequences},
	{"rope_copy_and_dup_range", test_rope_copy_and_dup_range},
	{"rope_replace_range_across_large_text", test_rope_replace_range_across_large_text},
	{"document_line_index_tracks_blank_lines_and_trailing_newline", test_document_line_index_tracks_blank_lines_and_trailing_newline},
	{"document_replace_range_updates_text_and_line_index", test_document_replace_range_updates_text_and_line_index},
	{"document_position_offset_roundtrip", test_document_position_offset_roundtrip},
	{"document_reset_from_text_source_streams_bytes", test_document_reset_from_text_source_streams_bytes},
	{"editor_build_active_text_source_uses_document_after_open", test_editor_build_active_text_source_uses_document_after_open},
	{"editor_document_incremental_updates_for_basic_edits", test_editor_document_incremental_updates_for_basic_edits},
	{"editor_buffer_offset_roundtrip_uses_document_mapping", test_editor_buffer_offset_roundtrip_uses_document_mapping},
	{"editor_buffer_offsets_rebuild_document_after_row_mutation", test_editor_buffer_offsets_rebuild_document_after_row_mutation},
	{"editor_document_lazy_rebuild_after_low_level_row_mutation", test_editor_document_lazy_rebuild_after_low_level_row_mutation},
	{"editor_document_restored_for_undo_redo", test_editor_document_restored_for_undo_redo},
	{"editor_document_edit_capture_uses_active_source", test_editor_document_edit_capture_uses_active_source},
	{"editor_document_selection_and_delete_use_active_source", test_editor_document_selection_and_delete_use_active_source},
	{"editor_document_save_uses_active_source", test_editor_document_save_uses_active_source},
	{"editor_buffer_find_uses_active_source_without_full_dup", test_editor_buffer_find_uses_active_source_without_full_dup},
	{"utf8_continuation_detection", test_utf8_continuation_detection},
	{"grapheme_extend_classification", test_grapheme_extend_classification},
	{"char_display_width_basics", test_char_display_width_basics},
	{"row_char_boundaries", test_row_char_boundaries},
	{"row_cluster_boundaries_combining", test_row_cluster_boundaries_combining},
	{"row_cluster_boundaries_zwj_sequence", test_row_cluster_boundaries_zwj_sequence},
	{"row_cluster_boundaries_regional_indicators", test_row_cluster_boundaries_regional_indicators},
	{"row_cx_to_rx_with_tabs", test_row_cx_to_rx_with_tabs},
	{"row_rx_to_cx_with_tabs", test_row_rx_to_cx_with_tabs},
	{"editor_update_row_expands_tabs", test_editor_update_row_expands_tabs},
	{"editor_update_row_tab_alignment_after_multibyte", test_editor_update_row_tab_alignment_after_multibyte},
	{"editor_update_row_escapes_c0_and_esc_in_render", test_editor_update_row_escapes_c0_and_esc_in_render},
	{"editor_update_row_escapes_c1_codepoints_in_render", test_editor_update_row_escapes_c1_codepoints_in_render},
	{"editor_update_row_preserves_printable_utf8_with_80_9f_continuations", test_editor_update_row_preserves_printable_utf8_with_80_9f_continuations},
	{"row_cx_to_rx_with_escaped_controls", test_row_cx_to_rx_with_escaped_controls},
	{"row_rx_to_cx_with_escaped_controls", test_row_rx_to_cx_with_escaped_controls},
	{"insert_and_delete_row_updates_dirty", test_insert_and_delete_row_updates_dirty},
	{"editor_delete_row_rejects_idx_at_numrows", test_editor_delete_row_rejects_idx_at_numrows},
	{"insert_and_delete_chars", test_insert_and_delete_chars},
	{"editor_del_char_at_rejects_idx_at_row_size", test_editor_del_char_at_rejects_idx_at_row_size},
	{"editor_insert_char_creates_initial_row", test_editor_insert_char_creates_initial_row},
	{"editor_insert_newline_splits_row", test_editor_insert_newline_splits_row},
	{"editor_insert_newline_at_row_start", test_editor_insert_newline_at_row_start},
	{"editor_del_char_cluster_and_merge", test_editor_del_char_cluster_and_merge},
	{"editor_rows_to_str", test_editor_rows_to_str},
	{"editor_rows_to_str_uses_document_when_row_cache_corrupt", test_editor_rows_to_str_uses_document_when_row_cache_corrupt},
	{"editor_open_reads_rows_and_clears_dirty", test_editor_open_reads_rows_and_clears_dirty},
	{"editor_open_rejects_binary_file_without_mutating_buffer", test_editor_open_rejects_binary_file_without_mutating_buffer},
};

const int g_document_text_editing_test_count =
		(int)(sizeof(g_document_text_editing_tests) / sizeof(g_document_text_editing_tests[0]));
