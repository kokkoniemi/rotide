CC ?= cc
SRC_DIR := src
CPPFLAGS ?= -I$(SRC_DIR) -Ivendor/tree_sitter/runtime/include -Ivendor/tree_sitter/runtime/src -Ivendor/tree_sitter/grammars/c/src -Ivendor/tree_sitter/grammars/cpp/src -Ivendor/tree_sitter/grammars/go/src -Ivendor/tree_sitter/grammars/bash/src -Ivendor/tree_sitter/grammars/html/src -Ivendor/tree_sitter/grammars/javascript/src -Ivendor/tree_sitter/grammars/css/src -Ivendor/tree_sitter/grammars/json/src -Ivendor/tree_sitter/grammars/typescript/src -Ivendor/tree_sitter/grammars/python/src -Ivendor/tree_sitter/grammars/php/src -Ivendor/tree_sitter/grammars/rust/src -Ivendor/tree_sitter/grammars/java/src -Ivendor/tree_sitter/grammars/regex/src -Ivendor/tree_sitter/grammars/csharp/src -Ivendor/tree_sitter/grammars/haskell/src -Ivendor/tree_sitter/grammars/ruby/src
CFLAGS ?= -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef -fno-common -pedantic -std=c2x
LDFLAGS ?=
SANITIZER_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
SANITIZER_LDFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
DEPFLAGS = -MMD -MP
TREE_SITTER_CPPFLAGS = $(CPPFLAGS) -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE
TREE_SITTER_WARNING_CFLAGS = -Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough
TREE_SITTER_CFLAGS = $(filter-out -Werror -Wundef -Wshadow -Wdouble-promotion -pedantic,$(CFLAGS)) \
	$(TREE_SITTER_WARNING_CFLAGS)

TREE_SITTER_SRCS = vendor/tree_sitter/runtime/src/lib.c \
	vendor/tree_sitter/grammars/c/src/parser.c \
	vendor/tree_sitter/grammars/cpp/src/parser.c \
	vendor/tree_sitter/grammars/cpp/src/scanner.c \
	vendor/tree_sitter/grammars/go/src/parser.c \
	vendor/tree_sitter/grammars/bash/src/parser.c \
	vendor/tree_sitter/grammars/bash/src/scanner.c \
	vendor/tree_sitter/grammars/html/src/parser.c \
	vendor/tree_sitter/grammars/html/src/scanner.c \
	vendor/tree_sitter/grammars/javascript/src/parser.c \
	vendor/tree_sitter/grammars/javascript/src/scanner.c \
	vendor/tree_sitter/grammars/css/src/parser.c \
	vendor/tree_sitter/grammars/css/src/scanner.c \
	vendor/tree_sitter/grammars/json/src/parser.c \
	vendor/tree_sitter/grammars/typescript/src/parser.c \
	vendor/tree_sitter/grammars/typescript/src/scanner.c \
	vendor/tree_sitter/grammars/python/src/parser.c \
	vendor/tree_sitter/grammars/python/src/scanner.c \
	vendor/tree_sitter/grammars/php/src/parser.c \
	vendor/tree_sitter/grammars/php/src/scanner.c \
	vendor/tree_sitter/grammars/rust/src/parser.c \
	vendor/tree_sitter/grammars/rust/src/scanner.c \
	vendor/tree_sitter/grammars/java/src/parser.c \
	vendor/tree_sitter/grammars/regex/src/parser.c \
	vendor/tree_sitter/grammars/csharp/src/parser.c \
	vendor/tree_sitter/grammars/csharp/src/scanner.c \
	vendor/tree_sitter/grammars/haskell/src/parser.c \
	vendor/tree_sitter/grammars/haskell/src/scanner.c \
	vendor/tree_sitter/grammars/ruby/src/parser.c \
	vendor/tree_sitter/grammars/ruby/src/scanner.c
CORE_SRCS = $(SRC_DIR)/rotide.c \
	$(SRC_DIR)/support/terminal.c $(SRC_DIR)/support/alloc.c \
	$(SRC_DIR)/support/save_syscalls.c $(SRC_DIR)/support/file_io.c \
	$(SRC_DIR)/text/document.c $(SRC_DIR)/text/rope.c \
	$(SRC_DIR)/text/utf8.c $(SRC_DIR)/text/row.c \
	$(SRC_DIR)/editing/buffer_core.c \
	$(SRC_DIR)/editing/edit.c $(SRC_DIR)/editing/selection.c \
	$(SRC_DIR)/editing/history.c \
	$(SRC_DIR)/workspace/tabs.c $(SRC_DIR)/workspace/drawer.c \
	$(SRC_DIR)/workspace/recovery.c \
	$(SRC_DIR)/input/dispatch.c \
	$(SRC_DIR)/render/screen.c \
	$(SRC_DIR)/config/common.c $(SRC_DIR)/config/keymap.c \
	$(SRC_DIR)/config/editor_config.c $(SRC_DIR)/config/theme_config.c \
	$(SRC_DIR)/config/lsp_config.c \
	$(SRC_DIR)/language/syntax.c $(SRC_DIR)/language/lsp.c
SRCS = $(CORE_SRCS) $(TREE_SITTER_SRCS)
OBJS = $(SRCS:.c=.o)
CORE_OBJS = $(CORE_SRCS:.c=.o)
TREE_SITTER_OBJS = $(TREE_SITTER_SRCS:.c=.o)
EDITOR_OBJS = $(filter-out $(SRC_DIR)/rotide.o,$(CORE_OBJS)) $(TREE_SITTER_OBJS)
TEST_SRCS = tests/rotide_tests_main.c tests/test_document_text_editing.c \
	tests/test_syntax.c tests/test_save_recovery.c tests/test_workspace_config.c \
	tests/test_lsp.c tests/test_input_search.c tests/test_render_terminal.c \
	tests/test_support.c tests/test_helpers.c tests/alloc_test_hooks.c \
	tests/save_syscalls_test_hooks.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BIN = tests/rotide_tests
DEPFILES = $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

rotide: $(SRC_DIR)/rotide.o $(EDITOR_OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

vendor/tree_sitter/runtime/src/lib.o: vendor/tree_sitter/runtime/src/lib.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/c/src/parser.o: vendor/tree_sitter/grammars/c/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/cpp/src/parser.o: vendor/tree_sitter/grammars/cpp/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/cpp/src/scanner.o: vendor/tree_sitter/grammars/cpp/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/go/src/parser.o: vendor/tree_sitter/grammars/go/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/bash/src/parser.o: vendor/tree_sitter/grammars/bash/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/bash/src/scanner.o: vendor/tree_sitter/grammars/bash/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/html/src/parser.o: vendor/tree_sitter/grammars/html/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/html/src/scanner.o: vendor/tree_sitter/grammars/html/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/javascript/src/parser.o: vendor/tree_sitter/grammars/javascript/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/javascript/src/scanner.o: vendor/tree_sitter/grammars/javascript/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/css/src/parser.o: vendor/tree_sitter/grammars/css/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/css/src/scanner.o: vendor/tree_sitter/grammars/css/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/json/src/parser.o: vendor/tree_sitter/grammars/json/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/typescript/src/parser.o: vendor/tree_sitter/grammars/typescript/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/typescript/src/scanner.o: vendor/tree_sitter/grammars/typescript/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/python/src/parser.o: vendor/tree_sitter/grammars/python/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/python/src/scanner.o: vendor/tree_sitter/grammars/python/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/php/src/parser.o: vendor/tree_sitter/grammars/php/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/php/src/scanner.o: vendor/tree_sitter/grammars/php/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/rust/src/parser.o: vendor/tree_sitter/grammars/rust/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/rust/src/scanner.o: vendor/tree_sitter/grammars/rust/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/java/src/parser.o: vendor/tree_sitter/grammars/java/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/regex/src/parser.o: vendor/tree_sitter/grammars/regex/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/csharp/src/parser.o: vendor/tree_sitter/grammars/csharp/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/csharp/src/scanner.o: vendor/tree_sitter/grammars/csharp/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/haskell/src/parser.o: vendor/tree_sitter/grammars/haskell/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/haskell/src/scanner.o: vendor/tree_sitter/grammars/haskell/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/ruby/src/parser.o: vendor/tree_sitter/grammars/ruby/src/parser.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/ruby/src/scanner.o: vendor/tree_sitter/grammars/ruby/src/scanner.c
	$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) $(EDITOR_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

test-sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZER_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZER_LDFLAGS)" test

-include $(DEPFILES)

.PHONY: clean test test-sanitize
clean:
	rm -f $(OBJS) $(TEST_OBJS) $(DEPFILES) $(TEST_BIN) rotide
	find $(SRC_DIR) tests vendor/tree_sitter -type f \( -name '*.o' -o -name '*.d' \) -delete
