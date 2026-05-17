# ============================================================================
# Toolchain
# ============================================================================
CC ?= cc
STRIP ?= strip
STRIPFLAGS ?= --strip-unneeded

# ============================================================================
# Project layout
# ============================================================================
SRC_DIR := src
VENDOR_DIR := vendor
LIBVTERM_DIR := $(VENDOR_DIR)/libvterm
TS_DIR := $(VENDOR_DIR)/tree_sitter
TS_GRAMMARS_DIR := $(TS_DIR)/grammars

# Every tree-sitter grammar contributes parser.c; those with a custom external
# scanner additionally contribute scanner.c, which is picked up via $(wildcard)
# below so adding/removing a grammar only requires editing this one list.
TS_GRAMMARS := \
	c cpp go bash html javascript jsdoc css json typescript tsx \
	python php rust java regex csharp haskell ruby ocaml julia scala \
	embedded_template markdown markdown_inline toml yaml xml make diff

# ============================================================================
# Compiler flags
# ============================================================================
CFLAGS ?= -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef \
	-fno-common -pedantic -std=c2x
LDFLAGS ?=
PTHREAD_FLAGS ?= -pthread
DEPFLAGS = -MMD -MP

CPPFLAGS ?= -I$(SRC_DIR) \
	-I$(LIBVTERM_DIR)/include \
	-I$(TS_DIR)/runtime/include -I$(TS_DIR)/runtime/src \
	$(patsubst %,-I$(TS_GRAMMARS_DIR)/%/src,$(TS_GRAMMARS))

RELEASE_CFLAGS ?= -Os -ffunction-sections -fdata-sections \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident
RELEASE_LDFLAGS ?= -Wl,--gc-sections

SANITIZER_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	-DROTIDE_TEXT_TREE_DEEP_CHECK
SANITIZER_LDFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

TSAN_FLAGS ?= -fsanitize=thread -fno-omit-frame-pointer -O1 -g
TSAN_LDFLAGS ?= -fsanitize=thread -fno-omit-frame-pointer
# TSan's shadow mapping conflicts with the default Linux ASLR layout on
# glibc; setarch -R disables ASLR for the child so TSan can lay out its
# shadow at a fixed offset.
TSAN_LAUNCHER ?= setarch $(shell uname -m) -R
TSAN_TEST_TAGS ?= threads lsp dap file_watch pty

# Vendored sources are third-party. Drop the strict warning set we apply to
# our own code and silence the benign warnings these trees legitimately emit.
VENDOR_CFLAGS = $(filter-out -Werror -Wundef -Wshadow -Wdouble-promotion -pedantic,$(CFLAGS))

TREE_SITTER_CPPFLAGS = $(CPPFLAGS) -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE
TREE_SITTER_CFLAGS = $(VENDOR_CFLAGS) \
	-Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough -Wno-unused-but-set-variable

LIBVTERM_CPPFLAGS = $(CPPFLAGS) -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE \
	-I$(LIBVTERM_DIR)/include -I$(LIBVTERM_DIR)/src
LIBVTERM_CFLAGS = $(VENDOR_CFLAGS) \
	-Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-cast-qual \
	-Wno-missing-field-initializers -Wno-empty-body -Wno-old-style-declaration

# ============================================================================
# Sources
# ============================================================================
LIBVTERM_SRCS = $(addprefix $(LIBVTERM_DIR)/src/, \
	encoding.c keyboard.c mouse.c parser.c pen.c \
	screen.c state.c unicode.c vterm.c)

TREE_SITTER_SRCS = $(TS_DIR)/runtime/src/lib.c \
	$(foreach g,$(TS_GRAMMARS),$(TS_GRAMMARS_DIR)/$(g)/src/parser.c) \
	$(foreach g,$(TS_GRAMMARS),$(wildcard $(TS_GRAMMARS_DIR)/$(g)/src/scanner.c))

CORE_SRCS = $(SRC_DIR)/rotide.c \
	$(addprefix $(SRC_DIR)/support/, \
		terminal.c alloc.c save_syscalls.c file_io.c) \
	$(addprefix $(SRC_DIR)/text/, \
		document.c text_summary.c text_tree.c utf8.c row.c) \
	$(addprefix $(SRC_DIR)/editing/, \
		document_bridge.c document_position.c buffer_search.c \
		edit_pipeline.c post_edit_notify.c row_cache.c text_source.c \
		buffer_core.c edit.c selection.c history.c) \
	$(addprefix $(SRC_DIR)/workspace/, \
		tabs.c drawer.c drawer_modes.c drawer_mode_menu.c \
		drawer_mode_git.c drawer_mode_lsp.c drawer_mode_dap.c \
		drawer_tree.c drawer_file_ops.c drawer_layout.c \
		file_search.c git.c watch.c project_search.c \
		recovery.c workspace_state.c layout.c) \
	$(addprefix $(SRC_DIR)/input/, \
		actions_edit.c actions_file_tab.c actions_language.c \
		actions_terminal_debug.c actions_workspace.c mouse.c \
		prompt.c text_pairs.c dispatch.c) \
	$(addprefix $(SRC_DIR)/render/, \
		write_buf.c ansi_style.c display_text.c drawer_view.c \
		pane_view.c status_bar.c tab_bar.c terminal_view.c \
		wrap.c viewport.c screen.c popup.c) \
	$(addprefix $(SRC_DIR)/config/, \
		common.c keymap.c runtime_config.c editor_config.c \
		theme_builtin.c theme_parse.c lsp_config.c dap_config.c) \
	$(addprefix $(SRC_DIR)/language/, \
		syntax.c queries.c syntax_budget.c syntax_captures.c \
		syntax_detect.c syntax_indent.c syntax_injections.c \
		syntax_locals.c syntax_predicates.c syntax_worker.c \
		syntax_visible_cache.c languages.c lsp.c lsp_documents.c \
		lsp_features.c lsp_json.c lsp_mock.c lsp_protocol.c \
		lsp_registry.c lsp_responses.c lsp_transport.c autocomplete.c) \
	$(addprefix $(SRC_DIR)/debug/, \
		dap_client.c dap_console.c dap.c) \
	$(addprefix $(SRC_DIR)/terminal/, \
		pty.c terminal_pane.c)

TEST_SRCS = $(addprefix tests/, \
	rotide_tests_main.c test_document_text_editing.c \
	test_syntax_activation.c test_syntax_parse.c \
	test_syntax_captures.c test_syntax_background.c \
	test_syntax_state.c test_syntax_registry.c \
	test_save_recovery.c test_workspace_persistence.c \
	test_workspace_theme_config.c test_workspace_keymap_view.c \
	test_workspace_io.c test_dap.c test_file_watch.c \
	test_lsp_protocol.c test_lsp_lifecycle.c test_lsp_completion.c \
	test_lsp_diagnostics.c test_lsp_navigation.c \
	test_input_actions.c test_input_selection.c test_input_mouse.c \
	test_input_search.c test_input_undo.c \
	test_render_frame.c test_render_chrome.c test_render_panes.c \
	test_render_terminal.c test_layout.c test_pty.c \
	test_terminal_pane.c test_text_invariants.c test_text_summary.c \
	test_text_tree.c test_syntax_incremental_equiv.c test_runner_internals.c \
	runner_support.c seed.c parallel_runner.c editor_state_snapshot.c \
	test_support.c test_helpers.c alloc_test_hooks.c save_syscalls_test_hooks.c)

# ============================================================================
# Objects
# ============================================================================
SRCS = $(CORE_SRCS) $(TREE_SITTER_SRCS) $(LIBVTERM_SRCS)
OBJS = $(SRCS:.c=.o)
CORE_OBJS = $(CORE_SRCS:.c=.o)
TREE_SITTER_OBJS = $(TREE_SITTER_SRCS:.c=.o)
LIBVTERM_OBJS = $(LIBVTERM_SRCS:.c=.o)
TEST_OBJS = $(TEST_SRCS:.c=.o)

# Everything except the rotide entry-point TU, so the test binary can link
# the editor without colliding on `main`.
EDITOR_OBJS = $(filter-out $(SRC_DIR)/rotide.o,$(CORE_OBJS)) \
	$(TREE_SITTER_OBJS) $(LIBVTERM_OBJS)

DEPFILES = $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)
TEST_BIN = tests/rotide_tests
BENCH_BUFFER_BIN = tests/bench_text_storage
BENCH_BUFFER_SRC = tests/bench_text_storage.c
BENCH_BUFFER_OBJ = $(BENCH_BUFFER_SRC:.c=.o)

# ============================================================================
# Generated headers
# ============================================================================
QUERIES_MANIFEST := scripts/queries_manifest.txt
QUERIES_HEADER := $(SRC_DIR)/language/syntax_query_data.h
QUERIES_SCM := $(shell awk '/^[[:space:]]*#/ || NF==0 { next } { for (i=2; i<=NF; i++) print $$i }' $(QUERIES_MANIFEST))
DEFAULT_CONFIG_INPUT := config.toml.example
DEFAULT_CONFIG_HEADER := $(SRC_DIR)/config/default_config_data.h
GENERATED_HEADERS := $(QUERIES_HEADER) $(DEFAULT_CONFIG_HEADER)

# ============================================================================
# Build logging (set V=1 for full compile commands)
# ============================================================================
V ?= 0
MAKEFLAGS += --no-print-directory
ifeq ($(V),1)
LOG =
else
LOG = @printf '  %-7s %s\n' '$(1)' '$(2)';
endif

# ============================================================================
# Build rules
# ============================================================================
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

$(TREE_SITTER_OBJS): %.o: %.c
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.c
	$(call LOG,CC,$<)$(CC) $(CPPFLAGS) $(CFLAGS) $(PTHREAD_FLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) $(EDITOR_OBJS)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) -rdynamic $^ -lutil -o $@

$(BENCH_BUFFER_BIN): $(BENCH_BUFFER_OBJ) $(EDITOR_OBJS)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) $^ -lutil -o $@

# ============================================================================
# Test / release / docs targets
# ============================================================================
TEST_FLAGS ?= --validate-reset --jobs 4
DOCS_MEDIA_FLAGS ?=

test: $(TEST_BIN)
	$(call LOG,TEST,$(TEST_BIN))./$(TEST_BIN) $(TEST_FLAGS)

bench-buffer: $(BENCH_BUFFER_BIN)
	$(call LOG,BENCH,$(BENCH_BUFFER_BIN))./$(BENCH_BUFFER_BIN) $(BENCH_BUFFER_FLAGS)

test-sanitize:
	$(call LOG,CLEAN,build)$(MAKE) clean
	$(call LOG,MAKE,test-sanitize)$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZER_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZER_LDFLAGS)" test

test-determinism: $(TEST_BIN)
	$(call LOG,TEST,determinism)scripts/check_test_determinism.sh ./$(TEST_BIN) $(TEST_FLAGS)

test-crash-handler: $(TEST_BIN)
	$(call LOG,TEST,crash)scripts/check_crash_handler.sh ./$(TEST_BIN)

test-tsan:
	$(call LOG,CLEAN,build)$(MAKE) clean
	$(call LOG,MAKE,test-tsan)$(MAKE) CFLAGS="$(CFLAGS) $(TSAN_FLAGS)" \
		LDFLAGS="$(LDFLAGS) $(TSAN_LDFLAGS)" $(TEST_BIN)
	@for tag in $(TSAN_TEST_TAGS); do \
		printf '  %-7s %s\n' 'TSAN' "--tag $$tag"; \
		$(TSAN_LAUNCHER) ./$(TEST_BIN) --tag $$tag $(TEST_FLAGS) || exit $$?; \
	done

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

.PHONY: clean test test-sanitize test-determinism test-tsan test-crash-handler release docs-media docs-diagrams bench-buffer

clean:
	$(call LOG,CLEAN,objects)rm -f $(OBJS) $(TEST_OBJS) $(BENCH_BUFFER_OBJ) $(DEPFILES) $(TEST_BIN) $(BENCH_BUFFER_BIN) rotide $(GENERATED_HEADERS)
	$(call LOG,CLEAN,tree)find $(SRC_DIR) tests $(TS_DIR) -type f \( -name '*.o' -o -name '*.d' \) -delete
