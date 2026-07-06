# ============================================================================
# Toolchain
# ============================================================================
CC ?= cc
STRIP ?= strip
STRIPFLAGS ?= --strip-unneeded
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy

# ============================================================================
# Project layout
# ============================================================================
BUILD_DIR := build
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
	embedded_template markdown markdown_inline toml yaml xml make diff latex bibtex hcl lua glsl kotlin svelte vue helm

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
	$(patsubst %,-I$(TS_GRAMMARS_DIR)/%/src,$(TS_GRAMMARS)) \
	-D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE

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

TREE_SITTER_CPPFLAGS = $(CPPFLAGS)
TREE_SITTER_CFLAGS = $(VENDOR_CFLAGS) \
	-Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-unused-label \
	-Wno-comment

LIBVTERM_CPPFLAGS = $(CPPFLAGS) \
	-I$(LIBVTERM_DIR)/include -I$(LIBVTERM_DIR)/src
LIBVTERM_CFLAGS = $(VENDOR_CFLAGS) \
	-Wno-unused-parameter -Wno-unused-value -Wno-sign-compare \
	-Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-cast-qual \
	-Wno-missing-field-initializers -Wno-empty-body -Wno-old-style-declaration \
	-Wno-maybe-uninitialized -Wno-unknown-warning-option

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
		terminal.c alloc.c save_syscalls.c file_io.c perf_trace.c json.c) \
	$(addprefix $(SRC_DIR)/text/, \
		document.c text_buffer.c text_summary.c text_tree.c utf8.c row.c) \
	$(addprefix $(SRC_DIR)/editing/, \
		document_bridge.c document_position.c buffer_search.c \
		edit_pipeline.c post_edit_notify.c row_cache.c text_source.c \
		buffer_core.c edit.c selection.c history.c) \
	$(addprefix $(SRC_DIR)/workspace/, \
		tabs.c drawer.c drawer_modes.c drawer_mode_menu.c \
		drawer_mode_git.c drawer_mode_lsp.c drawer_mode_dap.c \
		drawer_tree.c drawer_file_ops.c drawer_layout.c \
		file_search.c git.c git_ops.c git_view.c watch.c project_search.c \
		recovery.c workspace_state.c layout.c) \
	$(addprefix $(SRC_DIR)/input/, \
		actions_edit.c actions_file_tab.c actions_language.c \
		actions_terminal_debug.c actions_workspace.c mouse.c \
		prompt.c text_pairs.c input_system.c system_cua.c system_vim.c dispatch.c) \
	$(addprefix $(SRC_DIR)/render/, \
		write_buf.c ansi_style.c display_text.c drawer_view.c \
		pane_view.c status_bar.c tab_bar.c terminal_view.c \
		wrap.c viewport.c screen.c popup.c) \
	$(addprefix $(SRC_DIR)/config/, \
		common.c keymap.c runtime_config.c editor_config.c \
		input_config.c theme_builtin.c theme_parse.c lsp_config.c dap_config.c) \
	$(addprefix $(SRC_DIR)/language/, \
		syntax.c queries.c syntax_budget.c syntax_captures.c \
		syntax_detect.c syntax_indent.c syntax_injections.c \
		syntax_locals.c syntax_predicates.c syntax_worker.c \
		syntax_visible_cache.c languages.c lsp.c lsp_documents.c \
		lsp_features.c lsp_framing.c lsp_json.c lsp_mock.c lsp_protocol.c \
		lsp_registry.c lsp_responses.c lsp_transport.c autocomplete.c) \
	$(addprefix $(SRC_DIR)/debug/, \
		dap_breakpoints.c dap_client.c dap_console.c dap_control.c \
		dap_inspection.c dap_output.c dap_protocol.c dap_session.c dap.c) \
	$(addprefix $(SRC_DIR)/terminal/, \
		pty.c terminal_pane.c)

TEST_SRCS = $(addprefix tests/, \
	rotide_tests_main.c test_document_text_editing.c \
	test_syntax_activation.c test_syntax_parse.c \
	test_syntax_captures.c test_syntax_background.c \
	test_syntax_state.c test_syntax_registry.c \
	test_save_recovery.c test_workspace_persistence.c \
	test_workspace_theme_config.c test_workspace_keymap_view.c \
	test_config_scan.c \
	test_workspace_io.c test_dap.c test_dap_framing.c test_file_watch.c \
	test_git_ops.c test_git_input.c test_git_view.c \
	test_lsp_framing.c test_lsp_protocol.c test_lsp_lifecycle.c \
	test_lsp_completion.c test_lsp_diagnostics.c test_lsp_navigation.c \
	test_input_system.c test_input_vim.c test_input_actions.c test_input_selection.c test_input_mouse.c \
	test_input_search.c test_input_undo.c \
	test_render_frame.c test_render_chrome.c test_render_panes.c \
	test_render_terminal.c test_layout.c test_pty.c \
	test_terminal_pane.c test_text_invariants.c test_text_summary.c \
	test_text_tree.c test_syntax_incremental_equiv.c test_runner_internals.c \
	test_long_session.c \
	test_grid_snapshot_suite.c \
	test_metrics_jsonl.c \
	test_metrics_libfuzzer_parse.c \
	test_metrics_summary.c \
	test_metrics_render_svg.c \
	test_golden_apply.c \
	runner_support.c seed.c parallel_runner.c editor_state_snapshot.c \
	metrics_jsonl.c metrics_libfuzzer_parse.c \
	metrics_jsonl_read.c metrics_summary_cmd.c metrics_render_svg.c \
	grid_snapshot_update.c grid_snapshot_format.c golden_apply_lib.c \
	test_grid_snapshot.c \
	test_support.c test_helpers.c alloc_test_hooks.c save_syscalls_test_hooks.c)

# ============================================================================
# Objects
# ============================================================================
SRCS = $(CORE_SRCS) $(TREE_SITTER_SRCS) $(LIBVTERM_SRCS)
OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)
CORE_OBJS = $(CORE_SRCS:%.c=$(BUILD_DIR)/%.o)
TREE_SITTER_OBJS = $(TREE_SITTER_SRCS:%.c=$(BUILD_DIR)/%.o)
LIBVTERM_OBJS = $(LIBVTERM_SRCS:%.c=$(BUILD_DIR)/%.o)
TEST_OBJS = $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)

# Everything except the rotide entry-point TU, so the test binary can link
# the editor without colliding on `main`.
EDITOR_OBJS = $(filter-out $(BUILD_DIR)/$(SRC_DIR)/rotide.o,$(CORE_OBJS)) \
	$(TREE_SITTER_OBJS) $(LIBVTERM_OBJS)

DEPFILES = $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)
TEST_BIN = $(BUILD_DIR)/tests/rotide_tests
BENCH_BUFFER_BIN = $(BUILD_DIR)/tests/bench_text_storage
BENCH_BUFFER_SRC = tests/bench_text_storage.c
BENCH_BUFFER_OBJ = $(BENCH_BUFFER_SRC:%.c=$(BUILD_DIR)/%.o)

BENCH_MICRO_BIN = $(BUILD_DIR)/tests/rotide_bench
# bench_microbenches drives editorRefreshScreen for the screen-diff cases,
# which needs reset_editor_state and friends. Pull in just the test helpers
# the bench actually uses — alloc_test_hooks and save_syscalls_test_hooks are
# transitive deps of test_helpers.c's reset path.
BENCH_MICRO_SRCS = tests/bench_microbenches.c tests/bench_runner.c \
	tests/metrics_jsonl.c \
	tests/test_helpers.c tests/alloc_test_hooks.c \
	tests/save_syscalls_test_hooks.c
BENCH_MICRO_OBJS = $(BENCH_MICRO_SRCS:%.c=$(BUILD_DIR)/%.o)

# Tiny standalone tool: parses captured libFuzzer stderr + corpus dir and
# appends a kind=fuzz row to a metrics JSONL file. Lives next to the fuzz
# infrastructure but builds with the normal C toolchain (no fuzz flags).
METRICS_FUZZ_EMIT_BIN = $(BUILD_DIR)/tests/metrics_fuzz_emit
METRICS_FUZZ_EMIT_SRCS = tests/metrics_fuzz_emit.c \
	tests/metrics_libfuzzer_parse.c tests/metrics_jsonl.c
METRICS_FUZZ_EMIT_OBJS = $(METRICS_FUZZ_EMIT_SRCS:%.c=$(BUILD_DIR)/%.o)

# Reader / regression-detector for tests/metrics.jsonl. Subcommands:
# summary, check-fuzz-stale, check-bench-regression.
METRICS_SUMMARY_BIN = $(BUILD_DIR)/tests/metrics_summary
METRICS_SUMMARY_SRCS = tests/metrics_summary.c \
	tests/metrics_jsonl_read.c tests/metrics_summary_cmd.c \
	tests/metrics_render_svg.c
METRICS_SUMMARY_OBJS = $(METRICS_SUMMARY_SRCS:%.c=$(BUILD_DIR)/%.o)

# Golden-snapshot apply / diff-preview tools. Consume the JSONL stash
# emitted by `rotide_tests --update-golden`.
GOLDEN_APPLY_BIN = $(BUILD_DIR)/tests/golden_apply
GOLDEN_APPLY_SRCS = tests/golden_apply.c \
	tests/golden_apply_lib.c tests/grid_snapshot_format.c
GOLDEN_APPLY_OBJS = $(GOLDEN_APPLY_SRCS:%.c=$(BUILD_DIR)/%.o)

GOLDEN_DIFF_REPORT_BIN = $(BUILD_DIR)/tests/golden_diff_report
GOLDEN_DIFF_REPORT_SRCS = tests/golden_diff_report.c \
	tests/golden_apply_lib.c tests/grid_snapshot_format.c
GOLDEN_DIFF_REPORT_OBJS = $(GOLDEN_DIFF_REPORT_SRCS:%.c=$(BUILD_DIR)/%.o)

FUZZ_CC ?= clang
# Nightly soak time per target (seconds). 30 minutes by default — matches
# the value in TEST_IMPROVEMENT_PLAN.md and runs comfortably inside a
# GitHub-hosted runner's job budget.
FUZZ_NIGHTLY_TIME ?= 1800

FUZZ_VTERM_BIN = $(BUILD_DIR)/tests/fuzz/vterm/fuzz_vterm
FUZZ_VTERM_HARNESS = tests/fuzz/vterm/fuzz_vterm.c
FUZZ_VTERM_CORPUS = tests/fuzz/vterm/corpus
FUZZ_VTERM_CORPUS_GROWN = tests/fuzz/vterm/corpus_grown
FUZZ_VTERM_SMOKE_RUNS ?= 1000

FUZZ_LSP_BIN = $(BUILD_DIR)/tests/fuzz/lsp/fuzz_lsp
FUZZ_LSP_HARNESS = tests/fuzz/lsp/fuzz_lsp.c
FUZZ_LSP_CORPUS = tests/fuzz/lsp/corpus
FUZZ_LSP_CORPUS_GROWN = tests/fuzz/lsp/corpus_grown
FUZZ_LSP_SMOKE_RUNS ?= 5000
FUZZ_LSP_SRCS = $(SRC_DIR)/language/lsp_framing.c

FUZZ_DAP_BIN = $(BUILD_DIR)/tests/fuzz/dap/fuzz_dap
FUZZ_DAP_HARNESS = tests/fuzz/dap/fuzz_dap.c
FUZZ_DAP_CORPUS = tests/fuzz/dap/corpus
FUZZ_DAP_CORPUS_GROWN = tests/fuzz/dap/corpus_grown
FUZZ_DAP_SMOKE_RUNS ?= 5000
FUZZ_DAP_SRCS = $(SRC_DIR)/debug/dap_client.c

FUZZ_TOML_THEME_BIN = $(BUILD_DIR)/tests/fuzz/toml/fuzz_toml_theme
FUZZ_TOML_THEME_HARNESS = tests/fuzz/toml/fuzz_toml_theme.c
FUZZ_TOML_THEME_CORPUS = tests/fuzz/toml/corpus
FUZZ_TOML_THEME_CORPUS_GROWN = tests/fuzz/toml/corpus_grown
FUZZ_TOML_THEME_SMOKE_RUNS ?= 5000
FUZZ_TOML_THEME_SRCS = $(SRC_DIR)/config/theme_parse.c \
	$(SRC_DIR)/config/theme_builtin.c \
	$(SRC_DIR)/config/common.c \
	$(SRC_DIR)/support/alloc.c \
	$(SRC_DIR)/support/file_io.c \
	$(SRC_DIR)/support/save_syscalls.c
# libFuzzer ships its own coverage instrumentation under -fsanitize=fuzzer;
# explicit -fsanitize-coverage=trace-pc-guard conflicts with that on modern
# clang. -Wno-unknown-warning-option swallows the gcc-only flags inside
# LIBVTERM_CFLAGS (which clang doesn't recognise).
FUZZ_FLAGS = -O1 -g -fno-omit-frame-pointer \
	-fsanitize=fuzzer,address,undefined \
	-Wno-unknown-warning-option \
	-DROTIDE_FUZZ

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
# Style tooling
# ============================================================================
FORMAT_FILES := $(shell find $(SRC_DIR) tests -type f \( -name '*.c' -o -name '*.h' \) \
	! -path '$(QUERIES_HEADER)' ! -path '$(DEFAULT_CONFIG_HEADER)' 2>/dev/null)
LINT_FILES := $(sort $(CORE_SRCS) $(TEST_SRCS) $(BENCH_BUFFER_SRC) \
	$(BENCH_MICRO_SRCS) $(METRICS_FUZZ_EMIT_SRCS) $(METRICS_SUMMARY_SRCS) \
	$(GOLDEN_APPLY_SRCS) $(GOLDEN_DIFF_REPORT_SRCS))

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

# Convenience alias so `make rotide` still works after the binary moved
# under $(BUILD_DIR)/.
.PHONY: rotide
rotide: $(BUILD_DIR)/rotide

$(BUILD_DIR)/rotide: $(BUILD_DIR)/$(SRC_DIR)/rotide.o $(EDITOR_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) $(OBJS) -lutil -o $@

$(QUERIES_HEADER): $(QUERIES_MANIFEST) scripts/embed_queries.sh $(QUERIES_SCM)
	$(call LOG,GEN,$@)scripts/embed_queries.sh $(QUERIES_MANIFEST) $@

$(DEFAULT_CONFIG_HEADER): $(DEFAULT_CONFIG_INPUT) scripts/embed_default_config.sh
	$(call LOG,GEN,$@)scripts/embed_default_config.sh $(DEFAULT_CONFIG_INPUT) $@

$(BUILD_DIR)/$(SRC_DIR)/config/common.o: $(DEFAULT_CONFIG_HEADER)
$(BUILD_DIR)/$(SRC_DIR)/language/queries.o: $(QUERIES_HEADER)
$(BUILD_DIR)/$(SRC_DIR)/language/languages.o: $(QUERIES_HEADER)

$(LIBVTERM_OBJS): $(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(call LOG,CC,$<)$(CC) $(LIBVTERM_CPPFLAGS) $(LIBVTERM_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(TREE_SITTER_OBJS): $(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(call LOG,CC,$<)$(CC) $(TREE_SITTER_CPPFLAGS) $(TREE_SITTER_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(call LOG,CC,$<)$(CC) $(CPPFLAGS) $(CFLAGS) $(PTHREAD_FLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) $(EDITOR_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) -rdynamic $^ -lutil -o $@

$(BENCH_BUFFER_BIN): $(BENCH_BUFFER_OBJ) $(EDITOR_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) $^ -lutil -o $@

$(BENCH_MICRO_BIN): $(BENCH_MICRO_OBJS) $(EDITOR_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $(PTHREAD_FLAGS) $^ -lutil -o $@

$(METRICS_FUZZ_EMIT_BIN): $(METRICS_FUZZ_EMIT_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $^ -o $@

$(METRICS_SUMMARY_BIN): $(METRICS_SUMMARY_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $^ -o $@

$(GOLDEN_APPLY_BIN): $(GOLDEN_APPLY_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $^ -o $@

$(GOLDEN_DIFF_REPORT_BIN): $(GOLDEN_DIFF_REPORT_OBJS)
	@mkdir -p $(dir $@)
	$(call LOG,LD,$@)$(CC) $(LDFLAGS) $^ -o $@

# Single-step compile-and-link so libvterm sources see FUZZ_FLAGS instead of
# the standard build's flags. Don't reuse $(LIBVTERM_OBJS) — they would have
# been built without the sanitizer coverage hooks libFuzzer needs.
$(FUZZ_VTERM_BIN): $(FUZZ_VTERM_HARNESS) $(LIBVTERM_SRCS)
	@mkdir -p $(dir $@)
	$(call LOG,FUZZ_CC,$@)$(FUZZ_CC) $(FUZZ_FLAGS) $(LIBVTERM_CPPFLAGS) \
		$(LIBVTERM_CFLAGS) $^ -o $@

# lsp_framing.c is intentionally dependency-light (only support/size_utils.h,
# header-only) so the fuzz binary doesn't have to drag in the rest of the
# editor. CPPFLAGS picks up the -I$(SRC_DIR) needed for the include path.
$(FUZZ_LSP_BIN): $(FUZZ_LSP_HARNESS) $(FUZZ_LSP_SRCS)
	@mkdir -p $(dir $@)
	$(call LOG,FUZZ_CC,$@)$(FUZZ_CC) $(FUZZ_FLAGS) $(CPPFLAGS) $^ -o $@

# dap_client.c is similarly self-contained; same compile strategy.
$(FUZZ_DAP_BIN): $(FUZZ_DAP_HARNESS) $(FUZZ_DAP_SRCS)
	@mkdir -p $(dir $@)
	$(call LOG,FUZZ_CC,$@)$(FUZZ_CC) $(FUZZ_FLAGS) $(CPPFLAGS) $^ -o $@

# theme_parse.c needs theme_builtin.c (for editorThemeInitDefault et al.) and
# common.c (trim/comment-strip/quoted-value helpers). common.c also references
# support allocation/path/save helpers, so keep those TUs linked into the
# standalone fuzz binary. default_config_data.h is a generated header pulled in
# by common.c, so depend on it explicitly.
$(FUZZ_TOML_THEME_BIN): $(FUZZ_TOML_THEME_HARNESS) $(FUZZ_TOML_THEME_SRCS) $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(call LOG,FUZZ_CC,$@)$(FUZZ_CC) $(FUZZ_FLAGS) $(CPPFLAGS) \
		$(FUZZ_TOML_THEME_HARNESS) $(FUZZ_TOML_THEME_SRCS) -o $@

# ============================================================================
# Test / release / docs targets
# ============================================================================
TEST_FLAGS ?= --validate-reset --jobs 4
DOCS_MEDIA_FLAGS ?=

# Opt-in metrics emission. Set on the command line:
#   make test METRICS_OUT=tests/metrics.jsonl
#   make bench METRICS_OUT=tests/metrics.jsonl
# Fuzz smoke/nightly targets honour METRICS_OUT the same way. The runner and
# bench binaries enrich rows from
# ROTIDE_METRICS_GIT_SHA / GIT_REF / CI_RUN_ID env vars when set; CI
# workflows wire those to the matching GitHub Actions GITHUB_* values.
ifneq ($(strip $(METRICS_OUT)),)
TEST_FLAGS  += --metrics-out $(METRICS_OUT)
BENCH_FLAGS += --metrics-out $(METRICS_OUT)
endif

test: test-tree-sitter-sizes $(TEST_BIN)
	$(call LOG,TEST,$(TEST_BIN))./$(TEST_BIN) $(TEST_FLAGS)

tree-sitter-sizes:
	$(call LOG,SIZE,tree-sitter)scripts/report_tree_sitter_sizes.sh --object-root $(BUILD_DIR)

test-tree-sitter-sizes:
	$(call LOG,TEST,tree-sitter-size)scripts/check_tree_sitter_sizes.sh

# Flake-hunt soak: run every test --repeat N. The runner varies the test seed
# per repeat (deterministically, from the one recorded base seed), so a test
# that passes and fails across repeats is counted as a flake. Nightly-only (see
# nightly.yml) so the per-commit wall-time series isn't inflated N×. With
# METRICS_OUT set it emits a row carrying repeat>1, which the SVG dashboard
# sources for the flakiness chart.
FLAKE_HUNT_REPEAT ?= 20
FLAKE_HUNT_FLAGS ?= --jobs 4 --repeat $(FLAKE_HUNT_REPEAT)
ifneq ($(strip $(METRICS_OUT)),)
FLAKE_HUNT_FLAGS += --metrics-out $(METRICS_OUT)
endif

test-flake-hunt: $(TEST_BIN)
	$(call LOG,FLAKE,$(TEST_BIN))./$(TEST_BIN) $(FLAKE_HUNT_FLAGS)

bench-buffer: $(BENCH_BUFFER_BIN)
	$(call LOG,BENCH,$(BENCH_BUFFER_BIN))./$(BENCH_BUFFER_BIN) $(BENCH_BUFFER_FLAGS)

bench: $(BENCH_MICRO_BIN)
	$(call LOG,BENCH,$(BENCH_MICRO_BIN))./$(BENCH_MICRO_BIN) $(BENCH_FLAGS)

# Whole-program cold-open bench. Uses rotide's --render-once flag (a
# general non-interactive single-frame mode that any headless caller
# can use; the editor itself is unaware this is a benchmark) and times
# the rotide binary under hyperfine. The fixture is a synthetic ~17 MiB
# C source generated once and cached in /tmp; delete it to regenerate.
# Override BENCH_RENDER_RUNS / BENCH_RENDER_WARMUP to change the
# sampling; override HYPERFINE if your binary lives elsewhere.
HYPERFINE ?= hyperfine
BENCH_RENDER_FIXTURE ?= /tmp/rotide-bench-10MB.c
BENCH_RENDER_RUNS ?= 20
BENCH_RENDER_WARMUP ?= 5

$(BENCH_RENDER_FIXTURE):
	$(call LOG,GEN,$@)\
		{ \
			for i in $$(seq 1 350000); do \
				printf 'static int fn_%d(int x) { return x + %d; }\n' "$$i" "$$i"; \
			done; \
		} > $@; \
		size=$$(wc -c < $@); \
		echo "  generated $$size bytes ($$(($$size / 1024 / 1024)) MiB)"

bench-render-once: rotide $(BENCH_RENDER_FIXTURE)
	@command -v $(HYPERFINE) >/dev/null 2>&1 || { \
		echo "$(HYPERFINE) not installed. Install via 'cargo install hyperfine' or your package manager." >&2; \
		exit 1; \
	}
	$(call LOG,HYPERFINE,bench-render-once)$(HYPERFINE) \
		--warmup $(BENCH_RENDER_WARMUP) --runs $(BENCH_RENDER_RUNS) \
		'./$(BUILD_DIR)/rotide --render-once $(BENCH_RENDER_FIXTURE) > /dev/null'

# Convenience entry point for the --update-golden workflow. Default
# behaviour is preview-only — captures the stash and runs
# golden_diff_report so you can review the proposed changes. Add APPLY=1
# to actually rewrite the source files. UPDATE_GOLDEN_FLAGS lets you
# scope which tests run (e.g. UPDATE_GOLDEN_FLAGS='--filter chrome').
UPDATE_GOLDEN_FLAGS ?= --validate-reset --jobs 4
UPDATE_GOLDEN_STASH = tests/artifacts/goldens.jsonl

update-goldens: $(TEST_BIN) $(GOLDEN_APPLY_BIN) $(GOLDEN_DIFF_REPORT_BIN)
	$(call LOG,GOLDEN,capture)mkdir -p tests/artifacts; \
		rm -f $(UPDATE_GOLDEN_STASH); \
		./$(TEST_BIN) --update-golden $(UPDATE_GOLDEN_STASH) $(UPDATE_GOLDEN_FLAGS); \
		: ensure the stash exists even when no test mismatched, so the diff and apply tools see an empty stash instead of a missing file; \
		touch $(UPDATE_GOLDEN_STASH); \
		echo; \
		./$(GOLDEN_DIFF_REPORT_BIN) --stash $(UPDATE_GOLDEN_STASH); \
		rc=$$?; \
		echo; \
		if [ "$(APPLY)" = "1" ]; then \
			./$(GOLDEN_APPLY_BIN) --stash $(UPDATE_GOLDEN_STASH); \
		elif [ $$rc -eq 1 ]; then \
			echo "(re-run with APPLY=1 to rewrite the source files above)"; \
		fi

format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		echo "$(CLANG_FORMAT) not installed. Install clang-format 18+ or set CLANG_FORMAT=..." >&2; \
		exit 1; \
	}
	$(call LOG,FORMAT,clang-format)$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		echo "$(CLANG_FORMAT) not installed. Install clang-format 18+ or set CLANG_FORMAT=..." >&2; \
		exit 1; \
	}
	$(call LOG,FMTCHK,clang-format)$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

lint: $(GENERATED_HEADERS)
	@command -v $(CLANG_TIDY) >/dev/null 2>&1 || { \
		echo "$(CLANG_TIDY) not installed. Install clang-tidy 18+ or set CLANG_TIDY=..." >&2; \
		exit 1; \
	}
	$(call LOG,LINT,clang-tidy)$(CLANG_TIDY) $(LINT_FILES) -- $(CPPFLAGS) $(CFLAGS) $(PTHREAD_FLAGS) 2>&1 | awk -f scripts/lint-filter.awk

lint-prefixes:
	$(call LOG,LINT,prefixes)tools/lint-prefixes.sh

lint-check: format-check lint lint-prefixes

fuzz-vterm: $(FUZZ_VTERM_BIN)
	$(call LOG,FUZZ,vterm)./$(FUZZ_VTERM_BIN) $(FUZZ_VTERM_CORPUS)

# Stage the corpus into a tempdir so libFuzzer's new finds don't persist
# back into the committed seed set. Smoke is run-count-bounded so the
# wall time is predictable across CI runs. Stderr is captured to a log so
# the metrics emitter can parse the final-stats block; the log is
# replayed to the terminal so the human still sees it.
fuzz-vterm-smoke: $(FUZZ_VTERM_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-S,vterm)tmp=$$(mktemp -d -t rotide-fuzz-vterm.XXXXXX); \
		log=$$(mktemp -t rotide-fuzz-vterm-log.XXXXXX); \
		cp $(FUZZ_VTERM_CORPUS)/* $$tmp/; \
		./$(FUZZ_VTERM_BIN) -print_final_stats=1 \
			-runs=$(FUZZ_VTERM_SMOKE_RUNS) $$tmp 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target vterm --log $$log \
				--corpus-dir $$tmp --metrics-out $(METRICS_OUT) || true; \
		fi; \
		rm -rf $$tmp; rm -f $$log; \
		exit $$rc

fuzz-lsp: $(FUZZ_LSP_BIN)
	$(call LOG,FUZZ,lsp)./$(FUZZ_LSP_BIN) $(FUZZ_LSP_CORPUS)

fuzz-lsp-smoke: $(FUZZ_LSP_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-S,lsp)tmp=$$(mktemp -d -t rotide-fuzz-lsp.XXXXXX); \
		log=$$(mktemp -t rotide-fuzz-lsp-log.XXXXXX); \
		cp $(FUZZ_LSP_CORPUS)/* $$tmp/; \
		./$(FUZZ_LSP_BIN) -print_final_stats=1 \
			-runs=$(FUZZ_LSP_SMOKE_RUNS) $$tmp 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target lsp --log $$log \
				--corpus-dir $$tmp --metrics-out $(METRICS_OUT) || true; \
		fi; \
		rm -rf $$tmp; rm -f $$log; \
		exit $$rc

fuzz-dap: $(FUZZ_DAP_BIN)
	$(call LOG,FUZZ,dap)./$(FUZZ_DAP_BIN) $(FUZZ_DAP_CORPUS)

fuzz-dap-smoke: $(FUZZ_DAP_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-S,dap)tmp=$$(mktemp -d -t rotide-fuzz-dap.XXXXXX); \
		log=$$(mktemp -t rotide-fuzz-dap-log.XXXXXX); \
		cp $(FUZZ_DAP_CORPUS)/* $$tmp/; \
		./$(FUZZ_DAP_BIN) -print_final_stats=1 \
			-runs=$(FUZZ_DAP_SMOKE_RUNS) $$tmp 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target dap --log $$log \
				--corpus-dir $$tmp --metrics-out $(METRICS_OUT) || true; \
		fi; \
		rm -rf $$tmp; rm -f $$log; \
		exit $$rc

fuzz-toml-theme: $(FUZZ_TOML_THEME_BIN)
	$(call LOG,FUZZ,toml-theme)./$(FUZZ_TOML_THEME_BIN) $(FUZZ_TOML_THEME_CORPUS)

fuzz-toml-theme-smoke: $(FUZZ_TOML_THEME_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-S,toml-theme)tmp=$$(mktemp -d -t rotide-fuzz-toml-theme.XXXXXX); \
		log=$$(mktemp -t rotide-fuzz-toml-theme-log.XXXXXX); \
		cp $(FUZZ_TOML_THEME_CORPUS)/* $$tmp/; \
		./$(FUZZ_TOML_THEME_BIN) -print_final_stats=1 \
			-runs=$(FUZZ_TOML_THEME_SMOKE_RUNS) $$tmp 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target toml-theme --log $$log \
				--corpus-dir $$tmp --metrics-out $(METRICS_OUT) || true; \
		fi; \
		rm -rf $$tmp; rm -f $$log; \
		exit $$rc

# Nightly soaks: persist the grown corpus across CI runs. The `corpus_grown`
# directory is gitignored and populated from the committed seed set on the
# first run; subsequent runs restore it via actions/cache and let libFuzzer
# add new finds in place. -max_total_time is the only stop condition so each
# target soaks for a predictable wall-clock duration. Override per-target
# with FUZZ_NIGHTLY_TIME=<seconds>.
fuzz-vterm-nightly: $(FUZZ_VTERM_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-N,vterm)mkdir -p $(FUZZ_VTERM_CORPUS_GROWN); \
		cp -n $(FUZZ_VTERM_CORPUS)/* $(FUZZ_VTERM_CORPUS_GROWN)/ 2>/dev/null || true; \
		log=$$(mktemp -t rotide-fuzz-vterm-log.XXXXXX); \
		./$(FUZZ_VTERM_BIN) -print_final_stats=1 \
			-max_total_time=$(FUZZ_NIGHTLY_TIME) $(FUZZ_VTERM_CORPUS_GROWN) 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target vterm --log $$log \
				--corpus-dir $(FUZZ_VTERM_CORPUS_GROWN) \
				--metrics-out $(METRICS_OUT) \
				--soak-seconds $(FUZZ_NIGHTLY_TIME) || true; \
		fi; \
		rm -f $$log; \
		exit $$rc

fuzz-lsp-nightly: $(FUZZ_LSP_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-N,lsp)mkdir -p $(FUZZ_LSP_CORPUS_GROWN); \
		cp -n $(FUZZ_LSP_CORPUS)/* $(FUZZ_LSP_CORPUS_GROWN)/ 2>/dev/null || true; \
		log=$$(mktemp -t rotide-fuzz-lsp-log.XXXXXX); \
		./$(FUZZ_LSP_BIN) -print_final_stats=1 \
			-max_total_time=$(FUZZ_NIGHTLY_TIME) $(FUZZ_LSP_CORPUS_GROWN) 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target lsp --log $$log \
				--corpus-dir $(FUZZ_LSP_CORPUS_GROWN) \
				--metrics-out $(METRICS_OUT) \
				--soak-seconds $(FUZZ_NIGHTLY_TIME) || true; \
		fi; \
		rm -f $$log; \
		exit $$rc

fuzz-dap-nightly: $(FUZZ_DAP_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-N,dap)mkdir -p $(FUZZ_DAP_CORPUS_GROWN); \
		cp -n $(FUZZ_DAP_CORPUS)/* $(FUZZ_DAP_CORPUS_GROWN)/ 2>/dev/null || true; \
		log=$$(mktemp -t rotide-fuzz-dap-log.XXXXXX); \
		./$(FUZZ_DAP_BIN) -print_final_stats=1 \
			-max_total_time=$(FUZZ_NIGHTLY_TIME) $(FUZZ_DAP_CORPUS_GROWN) 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target dap --log $$log \
				--corpus-dir $(FUZZ_DAP_CORPUS_GROWN) \
				--metrics-out $(METRICS_OUT) \
				--soak-seconds $(FUZZ_NIGHTLY_TIME) || true; \
		fi; \
		rm -f $$log; \
		exit $$rc

fuzz-toml-theme-nightly: $(FUZZ_TOML_THEME_BIN) $(METRICS_FUZZ_EMIT_BIN)
	$(call LOG,FUZZ-N,toml-theme)mkdir -p $(FUZZ_TOML_THEME_CORPUS_GROWN); \
		cp -n $(FUZZ_TOML_THEME_CORPUS)/* $(FUZZ_TOML_THEME_CORPUS_GROWN)/ 2>/dev/null || true; \
		log=$$(mktemp -t rotide-fuzz-toml-theme-log.XXXXXX); \
		./$(FUZZ_TOML_THEME_BIN) -print_final_stats=1 \
			-max_total_time=$(FUZZ_NIGHTLY_TIME) $(FUZZ_TOML_THEME_CORPUS_GROWN) 2>$$log; \
		rc=$$?; \
		cat $$log >&2; \
		if [ -n "$(METRICS_OUT)" ]; then \
			./$(METRICS_FUZZ_EMIT_BIN) --target toml-theme --log $$log \
				--corpus-dir $(FUZZ_TOML_THEME_CORPUS_GROWN) \
				--metrics-out $(METRICS_OUT) \
				--soak-seconds $(FUZZ_NIGHTLY_TIME) || true; \
		fi; \
		rm -f $$log; \
		exit $$rc

test-sanitize:
	$(call LOG,CLEAN,build)$(MAKE) clean
	$(call LOG,MAKE,test-sanitize)$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZER_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZER_LDFLAGS)" test

# Builds with -DROTIDE_TEXT_TREE_DEEP_CHECK so every ReplaceRange recomputes
# the root summary from scratch and asserts it matches the maintained value.
# O(N) per edit, so run on demand rather than in the default test loop.
test-text-tree-deep-check:
	$(call LOG,CLEAN,build)$(MAKE) clean
	$(call LOG,MAKE,test-text-tree-deep-check)$(MAKE) \
		CFLAGS="$(CFLAGS) $(SANITIZER_CFLAGS) -DROTIDE_TEXT_TREE_DEEP_CHECK" \
		LDFLAGS="$(LDFLAGS) $(SANITIZER_LDFLAGS)" test

test-determinism: $(TEST_BIN)
	$(call LOG,TEST,determinism)scripts/check_test_determinism.sh ./$(TEST_BIN) $(TEST_FLAGS)

test-crash-handler: $(TEST_BIN)
	$(call LOG,TEST,crash)scripts/check_crash_handler.sh ./$(TEST_BIN)

test-quarantine-age:
	$(call LOG,TEST,quarantine-age)scripts/check_quarantine_age.sh

test-quarantine-passing: $(TEST_BIN)
	$(call LOG,TEST,quarantine-pass)scripts/check_quarantine_passing.sh ./$(TEST_BIN) $(TEST_FLAGS)

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
	$(call LOG,STRIP,$(BUILD_DIR)/rotide)$(STRIP) $(STRIPFLAGS) $(BUILD_DIR)/rotide

docs-media:
	$(call LOG,DOCS,media)python3 scripts/capture_docs_media.py $(DOCS_MEDIA_FLAGS)

docs-diagrams:
	$(call LOG,DOCS,diagrams)scripts/render_docs_diagrams.sh

# Lines-of-code metrics. Emits one kind="loc" row per scope/domain (first-party
# subsystem, tests, and each vendored library, kept separate). With METRICS_OUT
# set the rows append there for the SVG dashboard, exactly like `make bench`:
#   make loc METRICS_OUT=tests/metrics.jsonl
# Skips emission when no tracked source changed since the previous sample (see
# scripts/count_loc.sh); ROTIDE_LOC_FORCE=1 overrides for a manual run.
loc:
	$(call LOG,LOC,count)METRICS_OUT="$(METRICS_OUT)" scripts/count_loc.sh

-include $(DEPFILES)

.PHONY: clean test test-sanitize test-text-tree-deep-check test-determinism test-tsan test-crash-handler test-quarantine-age test-quarantine-passing test-tree-sitter-sizes tree-sitter-sizes release docs-media docs-diagrams loc bench-buffer bench bench-render-once format format-check lint lint-prefixes lint-check fuzz-vterm fuzz-vterm-smoke fuzz-vterm-nightly fuzz-lsp fuzz-lsp-smoke fuzz-lsp-nightly fuzz-dap fuzz-dap-smoke fuzz-dap-nightly fuzz-toml-theme fuzz-toml-theme-smoke fuzz-toml-theme-nightly update-goldens

clean:
	$(call LOG,CLEAN,$(BUILD_DIR))rm -rf $(BUILD_DIR)
	$(call LOG,CLEAN,headers)rm -f $(GENERATED_HEADERS)
