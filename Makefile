CC ?= cc
SRC_DIR := src
CPPFLAGS ?= -I$(SRC_DIR) -Ivendor/libvterm/include -Ivendor/tree_sitter/runtime/include -Ivendor/tree_sitter/runtime/src -Ivendor/tree_sitter/grammars/c/src -Ivendor/tree_sitter/grammars/cpp/src -Ivendor/tree_sitter/grammars/go/src -Ivendor/tree_sitter/grammars/bash/src -Ivendor/tree_sitter/grammars/html/src -Ivendor/tree_sitter/grammars/javascript/src -Ivendor/tree_sitter/grammars/jsdoc/src -Ivendor/tree_sitter/grammars/css/src -Ivendor/tree_sitter/grammars/json/src -Ivendor/tree_sitter/grammars/typescript/src -Ivendor/tree_sitter/grammars/tsx/src -Ivendor/tree_sitter/grammars/python/src -Ivendor/tree_sitter/grammars/php/src -Ivendor/tree_sitter/grammars/rust/src -Ivendor/tree_sitter/grammars/java/src -Ivendor/tree_sitter/grammars/regex/src -Ivendor/tree_sitter/grammars/csharp/src -Ivendor/tree_sitter/grammars/haskell/src -Ivendor/tree_sitter/grammars/ruby/src -Ivendor/tree_sitter/grammars/ocaml/src -Ivendor/tree_sitter/grammars/julia/src -Ivendor/tree_sitter/grammars/scala/src -Ivendor/tree_sitter/grammars/embedded_template/src -Ivendor/tree_sitter/grammars/markdown/src -Ivendor/tree_sitter/grammars/markdown_inline/src -Ivendor/tree_sitter/grammars/toml/src -Ivendor/tree_sitter/grammars/yaml/src -Ivendor/tree_sitter/grammars/xml/src -Ivendor/tree_sitter/grammars/make/src -Ivendor/tree_sitter/grammars/diff/src
CFLAGS ?= -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef -fno-common -pedantic -std=c2x
LDFLAGS ?=
PTHREAD_FLAGS ?= -pthread
RELEASE_CFLAGS ?= -Os -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident
RELEASE_LDFLAGS ?= -Wl,--gc-sections
STRIP ?= strip
STRIPFLAGS ?= --strip-unneeded
SANITIZER_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
SANITIZER_LDFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
DEPFLAGS = -MMD -MP
V ?= 0
DOCS_MEDIA_FLAGS ?=
MAKEFLAGS += --no-print-directory
ifeq ($(V),1)
LOG =
else
LOG = @printf '  %-7s %s\n' '$(1)' '$(2)';
endif
TREE_SITTER_CPPFLAGS = $(CPPFLAGS) -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE
TREE_SITTER_WARNING_CFLAGS = -Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough -Wno-unused-but-set-variable
TREE_SITTER_CFLAGS = $(filter-out -Werror -Wundef -Wshadow -Wdouble-promotion -pedantic,$(CFLAGS)) \
	$(TREE_SITTER_WARNING_CFLAGS)

LIBVTERM_CPPFLAGS = $(CPPFLAGS) -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE \
	-Ivendor/libvterm/include -Ivendor/libvterm/src
LIBVTERM_WARNING_CFLAGS = -Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-cast-qual
LIBVTERM_CFLAGS = $(filter-out -Werror -Wundef -Wshadow -Wdouble-promotion -pedantic,$(CFLAGS)) \
	$(LIBVTERM_WARNING_CFLAGS)
LIBVTERM_SRCS = vendor/libvterm/src/encoding.c \
	vendor/libvterm/src/keyboard.c \
	vendor/libvterm/src/mouse.c \
	vendor/libvterm/src/parser.c \
	vendor/libvterm/src/pen.c \
	vendor/libvterm/src/screen.c \
	vendor/libvterm/src/state.c \
	vendor/libvterm/src/unicode.c \
	vendor/libvterm/src/vterm.c

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
	vendor/tree_sitter/grammars/jsdoc/src/parser.c \
	vendor/tree_sitter/grammars/jsdoc/src/scanner.c \
	vendor/tree_sitter/grammars/css/src/parser.c \
	vendor/tree_sitter/grammars/css/src/scanner.c \
	vendor/tree_sitter/grammars/json/src/parser.c \
	vendor/tree_sitter/grammars/typescript/src/parser.c \
	vendor/tree_sitter/grammars/typescript/src/scanner.c \
	vendor/tree_sitter/grammars/tsx/src/parser.c \
	vendor/tree_sitter/grammars/tsx/src/scanner.c \
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
	vendor/tree_sitter/grammars/ruby/src/scanner.c \
	vendor/tree_sitter/grammars/ocaml/src/parser.c \
	vendor/tree_sitter/grammars/ocaml/src/scanner.c \
	vendor/tree_sitter/grammars/julia/src/parser.c \
	vendor/tree_sitter/grammars/julia/src/scanner.c \
	vendor/tree_sitter/grammars/scala/src/parser.c \
	vendor/tree_sitter/grammars/scala/src/scanner.c \
	vendor/tree_sitter/grammars/embedded_template/src/parser.c \
	vendor/tree_sitter/grammars/markdown/src/parser.c \
	vendor/tree_sitter/grammars/markdown/src/scanner.c \
	vendor/tree_sitter/grammars/markdown_inline/src/parser.c \
	vendor/tree_sitter/grammars/markdown_inline/src/scanner.c \
	vendor/tree_sitter/grammars/toml/src/parser.c \
	vendor/tree_sitter/grammars/toml/src/scanner.c \
	vendor/tree_sitter/grammars/yaml/src/parser.c \
	vendor/tree_sitter/grammars/yaml/src/scanner.c \
	vendor/tree_sitter/grammars/xml/src/parser.c \
	vendor/tree_sitter/grammars/xml/src/scanner.c \
	vendor/tree_sitter/grammars/make/src/parser.c \
	vendor/tree_sitter/grammars/diff/src/parser.c
CORE_SRCS = $(SRC_DIR)/rotide.c \
	$(SRC_DIR)/support/terminal.c $(SRC_DIR)/support/alloc.c \
	$(SRC_DIR)/support/save_syscalls.c $(SRC_DIR)/support/file_io.c \
	$(SRC_DIR)/text/document.c $(SRC_DIR)/text/rope.c \
	$(SRC_DIR)/text/utf8.c $(SRC_DIR)/text/row.c \
	$(SRC_DIR)/editing/document_bridge.c \
	$(SRC_DIR)/editing/document_position.c \
	$(SRC_DIR)/editing/buffer_search.c \
	$(SRC_DIR)/editing/edit_pipeline.c \
	$(SRC_DIR)/editing/post_edit_notify.c \
	$(SRC_DIR)/editing/row_cache.c \
	$(SRC_DIR)/editing/text_source.c \
	$(SRC_DIR)/editing/buffer_core.c \
	$(SRC_DIR)/editing/edit.c $(SRC_DIR)/editing/selection.c \
	$(SRC_DIR)/editing/history.c \
	$(SRC_DIR)/workspace/tabs.c $(SRC_DIR)/workspace/drawer.c \
	$(SRC_DIR)/workspace/drawer_modes.c \
	$(SRC_DIR)/workspace/drawer_mode_menu.c \
	$(SRC_DIR)/workspace/drawer_mode_git.c \
	$(SRC_DIR)/workspace/drawer_mode_lsp.c \
	$(SRC_DIR)/workspace/drawer_mode_dap.c \
	$(SRC_DIR)/workspace/drawer_tree.c \
	$(SRC_DIR)/workspace/drawer_file_ops.c \
	$(SRC_DIR)/workspace/drawer_layout.c \
	$(SRC_DIR)/workspace/file_search.c \
	$(SRC_DIR)/workspace/git.c \
	$(SRC_DIR)/workspace/watch.c \
	$(SRC_DIR)/workspace/project_search.c \
	$(SRC_DIR)/workspace/recovery.c \
	$(SRC_DIR)/workspace/workspace_state.c \
	$(SRC_DIR)/workspace/layout.c \
	$(SRC_DIR)/input/actions_edit.c \
	$(SRC_DIR)/input/actions_file_tab.c \
	$(SRC_DIR)/input/actions_language.c \
	$(SRC_DIR)/input/actions_terminal_debug.c \
	$(SRC_DIR)/input/actions_workspace.c \
	$(SRC_DIR)/input/mouse.c \
	$(SRC_DIR)/input/prompt.c \
	$(SRC_DIR)/input/text_pairs.c \
	$(SRC_DIR)/input/dispatch.c \
	$(SRC_DIR)/render/write_buf.c \
	$(SRC_DIR)/render/ansi_style.c \
	$(SRC_DIR)/render/display_text.c \
	$(SRC_DIR)/render/drawer_view.c \
	$(SRC_DIR)/render/pane_view.c \
	$(SRC_DIR)/render/status_bar.c \
	$(SRC_DIR)/render/tab_bar.c \
	$(SRC_DIR)/render/terminal_view.c \
	$(SRC_DIR)/render/wrap.c \
	$(SRC_DIR)/render/viewport.c \
	$(SRC_DIR)/render/screen.c \
	$(SRC_DIR)/render/popup.c \
	$(SRC_DIR)/config/common.c $(SRC_DIR)/config/keymap.c \
	$(SRC_DIR)/config/runtime_config.c \
	$(SRC_DIR)/config/editor_config.c $(SRC_DIR)/config/theme_config.c \
	$(SRC_DIR)/config/lsp_config.c $(SRC_DIR)/config/dap_config.c \
	$(SRC_DIR)/language/syntax.c $(SRC_DIR)/language/queries.c \
	$(SRC_DIR)/language/syntax_budget.c \
	$(SRC_DIR)/language/syntax_captures.c \
	$(SRC_DIR)/language/syntax_detect.c \
	$(SRC_DIR)/language/syntax_indent.c \
	$(SRC_DIR)/language/syntax_injections.c \
	$(SRC_DIR)/language/syntax_locals.c \
	$(SRC_DIR)/language/syntax_predicates.c \
	$(SRC_DIR)/language/syntax_worker.c \
	$(SRC_DIR)/language/syntax_visible_cache.c \
	$(SRC_DIR)/language/languages.c \
	$(SRC_DIR)/language/lsp.c \
	$(SRC_DIR)/language/lsp_documents.c \
	$(SRC_DIR)/language/lsp_features.c \
	$(SRC_DIR)/language/lsp_json.c \
	$(SRC_DIR)/language/lsp_mock.c \
	$(SRC_DIR)/language/lsp_protocol.c \
	$(SRC_DIR)/language/lsp_registry.c \
	$(SRC_DIR)/language/lsp_responses.c \
	$(SRC_DIR)/language/lsp_transport.c \
	$(SRC_DIR)/debug/dap_client.c \
	$(SRC_DIR)/debug/dap_console.c \
	$(SRC_DIR)/debug/dap.c \
	$(SRC_DIR)/terminal/pty.c \
	$(SRC_DIR)/terminal/terminal_pane.c \
	$(SRC_DIR)/language/autocomplete.c
SRCS = $(CORE_SRCS) $(TREE_SITTER_SRCS) $(LIBVTERM_SRCS)
OBJS = $(SRCS:.c=.o)
CORE_OBJS = $(CORE_SRCS:.c=.o)
TREE_SITTER_OBJS = $(TREE_SITTER_SRCS:.c=.o)
LIBVTERM_OBJS = $(LIBVTERM_SRCS:.c=.o)
EDITOR_OBJS = $(filter-out $(SRC_DIR)/rotide.o,$(CORE_OBJS)) $(TREE_SITTER_OBJS) \
	$(LIBVTERM_OBJS)
TEST_SRCS = tests/rotide_tests_main.c tests/test_document_text_editing.c \
	tests/test_syntax.c tests/test_syntax_registry.c \
	tests/test_save_recovery.c tests/test_workspace_config.c \
	tests/test_file_watch.c \
	tests/test_lsp.c tests/test_input_search.c tests/test_render_terminal.c \
	tests/test_layout.c tests/test_pty.c tests/test_terminal_pane.c \
	tests/test_support.c tests/test_helpers.c tests/alloc_test_hooks.c \
	tests/save_syscalls_test_hooks.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BIN = tests/rotide_tests
DEPFILES = $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

QUERIES_MANIFEST := scripts/queries_manifest.txt
QUERIES_HEADER := $(SRC_DIR)/language/syntax_query_data.h
QUERIES_SCM := $(shell awk '/^[[:space:]]*#/ || NF==0 { next } { for (i=2; i<=NF; i++) print $$i }' $(QUERIES_MANIFEST))
DEFAULT_CONFIG_INPUT := config.toml.example
DEFAULT_CONFIG_HEADER := $(SRC_DIR)/config/default_config_data.h
GENERATED_HEADERS := $(QUERIES_HEADER) $(DEFAULT_CONFIG_HEADER)

.DEFAULT_GOAL := rotide

rotide: $(SRC_DIR)/rotide.o $(EDITOR_OBJS)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) $(OBJS) -lutil -o $@

$(QUERIES_HEADER): $(QUERIES_MANIFEST) scripts/embed_queries.sh $(QUERIES_SCM)
	$(call LOG,GEN,$@)scripts/embed_queries.sh $(QUERIES_MANIFEST) $@

$(DEFAULT_CONFIG_HEADER): $(DEFAULT_CONFIG_INPUT) scripts/embed_default_config.sh
	$(call LOG,GEN,$@)scripts/embed_default_config.sh $(DEFAULT_CONFIG_INPUT) $@

$(SRC_DIR)/config/common.o: $(DEFAULT_CONFIG_HEADER)
$(SRC_DIR)/language/queries.o: $(QUERIES_HEADER)
$(SRC_DIR)/language/languages.o: $(QUERIES_HEADER)

$(LIBVTERM_OBJS): %.o: %.c
	$(call LOG,CC,$<)$(CC) $(LIBVTERM_CPPFLAGS) $(LIBVTERM_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/runtime/src/lib.o: vendor/tree_sitter/runtime/src/lib.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/c/src/parser.o: vendor/tree_sitter/grammars/c/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/cpp/src/parser.o: vendor/tree_sitter/grammars/cpp/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/cpp/src/scanner.o: vendor/tree_sitter/grammars/cpp/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/go/src/parser.o: vendor/tree_sitter/grammars/go/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/bash/src/parser.o: vendor/tree_sitter/grammars/bash/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/bash/src/scanner.o: vendor/tree_sitter/grammars/bash/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/html/src/parser.o: vendor/tree_sitter/grammars/html/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/html/src/scanner.o: vendor/tree_sitter/grammars/html/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/javascript/src/parser.o: vendor/tree_sitter/grammars/javascript/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/javascript/src/scanner.o: vendor/tree_sitter/grammars/javascript/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/jsdoc/src/parser.o: vendor/tree_sitter/grammars/jsdoc/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/jsdoc/src/scanner.o: vendor/tree_sitter/grammars/jsdoc/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/css/src/parser.o: vendor/tree_sitter/grammars/css/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/css/src/scanner.o: vendor/tree_sitter/grammars/css/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/json/src/parser.o: vendor/tree_sitter/grammars/json/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/typescript/src/parser.o: vendor/tree_sitter/grammars/typescript/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/typescript/src/scanner.o: vendor/tree_sitter/grammars/typescript/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/tsx/src/parser.o: vendor/tree_sitter/grammars/tsx/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/tsx/src/scanner.o: vendor/tree_sitter/grammars/tsx/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/python/src/parser.o: vendor/tree_sitter/grammars/python/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/python/src/scanner.o: vendor/tree_sitter/grammars/python/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/php/src/parser.o: vendor/tree_sitter/grammars/php/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/php/src/scanner.o: vendor/tree_sitter/grammars/php/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/rust/src/parser.o: vendor/tree_sitter/grammars/rust/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/rust/src/scanner.o: vendor/tree_sitter/grammars/rust/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/java/src/parser.o: vendor/tree_sitter/grammars/java/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/regex/src/parser.o: vendor/tree_sitter/grammars/regex/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/csharp/src/parser.o: vendor/tree_sitter/grammars/csharp/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/csharp/src/scanner.o: vendor/tree_sitter/grammars/csharp/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/haskell/src/parser.o: vendor/tree_sitter/grammars/haskell/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/haskell/src/scanner.o: vendor/tree_sitter/grammars/haskell/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/ruby/src/parser.o: vendor/tree_sitter/grammars/ruby/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/ruby/src/scanner.o: vendor/tree_sitter/grammars/ruby/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/ocaml/src/parser.o: vendor/tree_sitter/grammars/ocaml/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/ocaml/src/scanner.o: vendor/tree_sitter/grammars/ocaml/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/julia/src/parser.o: vendor/tree_sitter/grammars/julia/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/julia/src/scanner.o: vendor/tree_sitter/grammars/julia/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/scala/src/parser.o: vendor/tree_sitter/grammars/scala/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/scala/src/scanner.o: vendor/tree_sitter/grammars/scala/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/embedded_template/src/parser.o: vendor/tree_sitter/grammars/embedded_template/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/markdown/src/parser.o: vendor/tree_sitter/grammars/markdown/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/markdown/src/scanner.o: vendor/tree_sitter/grammars/markdown/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/markdown_inline/src/parser.o: vendor/tree_sitter/grammars/markdown_inline/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/markdown_inline/src/scanner.o: vendor/tree_sitter/grammars/markdown_inline/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/toml/src/parser.o: vendor/tree_sitter/grammars/toml/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/toml/src/scanner.o: vendor/tree_sitter/grammars/toml/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/yaml/src/parser.o: vendor/tree_sitter/grammars/yaml/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/yaml/src/scanner.o: vendor/tree_sitter/grammars/yaml/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/xml/src/parser.o: vendor/tree_sitter/grammars/xml/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/xml/src/scanner.o: vendor/tree_sitter/grammars/xml/src/scanner.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/make/src/parser.o: vendor/tree_sitter/grammars/make/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

vendor/tree_sitter/grammars/diff/src/parser.o: vendor/tree_sitter/grammars/diff/src/parser.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.c
	$(call LOG,CC,$<)$(CC) $(CPPFLAGS) $(CFLAGS) $(PTHREAD_FLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) $(EDITOR_OBJS)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) $^ -lutil -o $@

test: $(TEST_BIN)
	$(call LOG,TEST,$(TEST_BIN))./$(TEST_BIN)

test-sanitize:
	$(call LOG,CLEAN,build)$(MAKE) clean
	$(call LOG,MAKE,test-sanitize)$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZER_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZER_LDFLAGS)" test

release:
	$(call LOG,CLEAN,build)$(MAKE) clean
	$(call LOG,MAKE,release)$(MAKE) CFLAGS="$(CFLAGS) $(RELEASE_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(RELEASE_LDFLAGS)" rotide
	$(call LOG,STRIP,rotide)$(STRIP) $(STRIPFLAGS) rotide

docs-media:
	$(call LOG,DOCS,media)python3 scripts/capture_docs_media.py $(DOCS_MEDIA_FLAGS)

docs-diagrams:
	$(call LOG,DOCS,diagrams)scripts/render_docs_diagrams.sh

-include $(DEPFILES)

.PHONY: clean test test-sanitize release docs-media docs-diagrams
clean:
	$(call LOG,CLEAN,objects)rm -f $(OBJS) $(TEST_OBJS) $(DEPFILES) $(TEST_BIN) rotide $(GENERATED_HEADERS)
	$(call LOG,CLEAN,tree)find $(SRC_DIR) tests vendor/tree_sitter -type f \( -name '*.o' -o -name '*.d' \) -delete
