# Rotide Test Harness Improvement Plan

## Snapshot of where we are

- ~820 test runs in [tests/](tests/) across ~30 suite files, dispatched from
  [rotide_tests_main.c](tests/rotide_tests_main.c). Wall time ~5s on `-j4`,
  ~26s sequentially.
- `-Werror`, ASan + UBSan via `make test-sanitize`. TSan via `make test-tsan`.
  Determinism gate via `make test-determinism`. Crash-handler smoke via
  `make test-crash-handler`.
- Custom runner with rich flags (filter, tag, exclude-tag, list, fail-fast,
  repeat, seed, shuffle, no-quarantine, jobs) in
  [runner_support.c](tests/runner_support.c).
- Per-suite subprocess parallelism with crash artifacts in
  [parallel_runner.c](tests/parallel_runner.c); crashes dump to
  [tests/artifacts/crashes/](tests/artifacts/crashes/).
- Reset-state validator via [editor_state_snapshot.h](tests/editor_state_snapshot.h)
  with `snapshot_compare_*` tests covering the diff machinery.
- Real screen capture via [`refresh_screen_and_capture`](tests/test_helpers.c),
  real PTY tests via [test_pty.c](tests/test_pty.c) and
  [test_terminal_pane.c](tests/test_terminal_pane.c) with vterm grid scraping.
- Failure injection: [alloc_test_hooks.c](tests/alloc_test_hooks.c),
  [save_syscalls_test_hooks.h](tests/save_syscalls_test_hooks.h), syntax
  budget/parse-fail countdowns, an LSP mock with rich knobs.
- Test-only stat hooks via [editor_test_api.h](tests/editor_test_api.h):
  full-rebuild vs incremental, text-source builds/dups, row-cache splices.
- Differential property tests in [test_text_invariants.c](tests/test_text_invariants.c)
  cover document/row-cache invariants against a `char*`-and-`memmove` reference.
- Tree-sitter incremental ≡ full-reparse parity in
  [test_syntax_incremental_equiv.c](tests/test_syntax_incremental_equiv.c) for
  every supported language.
- Storage microbench: [tests/bench_text_storage.c](tests/bench_text_storage.c)
  (`make bench-buffer`); reports `open_reset` MB/s, random op µs/op, and
  row-cache RSS delta.
- [tests/QUARANTINE.md](tests/QUARANTINE.md) policy live; runner honours it
  and `--no-quarantine` runs the skipped set.
- Single global `editorConfig E`. Tests are inherently sequential
  in-process; parallelism is at suite granularity via fork.

The foundation is now in good shape. Phase 4 (untrusted-input fuzzing)
is complete across all four boundaries — vterm, LSP framing, DAP
framing, and theme TOML — each with a libFuzzer harness, seed corpus,
and per-PR CI smoke. Remaining work is concentrated in broader
**microbench coverage beyond storage**, **the `--update-golden`
workflow + grid-snapshot conversions**, and **the `metrics.jsonl`
continuous-improvement signal**.

---

## Document-storage architecture (post-refactor)

The original plan referenced "the rope/document/row_cache split." That
shape is gone:

- Text storage is now a SumTree-of-pieces under
  [src/text/text_tree.h](src/text/text_tree.h); `editorRope` no longer
  exists.
- [src/text/document.h](src/text/document.h) wraps the tree. All callers
  read line bytes via `editorDocumentLineView` / `editorDocumentLineBytes`
  / `editorDocumentLineDup`.
- `struct erow` holds render-only fields. Raw line bytes are not duplicated
  per row. Any new invariant assertion must phrase length checks as
  "sum of `editorDocumentLineLength(doc, r)` over rows + newlines == doc
  length", not "sum of `row.size`".

All future test work should target `editorDocument` + `editorTextTree` and
treat the row cache as derived state.

---

## Rejecting weak options

Reject outright:

- **Adding cmocka / Criterion / Google Benchmark.** The custom runner is fine
  and tightly integrated; replacing it buys nothing.
- **Pexpect / Python PTY layer.** Rotide already drives a real PTY in C with
  vterm grid scraping.
- **vttest compatibility automation.** Output is interactive and meant for
  human inspection.
- **Mutation testing (Mull).** Premature.
- **"Add a `--script foo.edtest` headless DSL."** Tests already drive
  `editorConfig E` and the input pipeline directly.
- **TAP / JUnit XML output.** Add only if/when CI dashboards need it.
- **Generic coverage / static analysis push** as top-priority work.
- **Keypress-to-screen latency p99 SLOs.** No frame budget to chase.

Accept with caveats:

- Snapshot/golden tests: yes, but only normalised vterm grids, not raw bytes.
- Fuzzing: yes, but only on the small set of high-risk input boundaries
  enumerated below.
- `hyperfine` whole-program benchmarks: yes for the build/start path and a
  large-file open scenario. Skip for in-process hot paths; a microbench mode
  in the runner is better signal there.

---

## Cross-cutting infrastructure

### Sanitizer matrix

- [x] `make test-sanitize` (ASan + UBSan) — runs in CI on push/PR.
- [x] `make test-tsan` target exists in the Makefile.
- [x] `make test-tsan` wired into a nightly CI job
      ([.github/workflows/nightly.yml](.github/workflows/nightly.yml)),
      scoped via `TSAN_TEST_TAGS = threads lsp dap file_watch pty`.

### Flake quarantine policy

- [x] [tests/QUARANTINE.md](tests/QUARANTINE.md) format documented.
- [x] Runner reads it and skips listed tests by default.
- [x] `--no-quarantine` runs them anyway.
- [x] Nightly job that runs with `--no-quarantine` and fails loudly if a
      quarantined test starts passing again — `make test-quarantine-passing`,
      backed by [scripts/check_quarantine_passing.sh](scripts/check_quarantine_passing.sh),
      wired into [nightly.yml](.github/workflows/nightly.yml).
- [x] 30-day age enforcement — `make test-quarantine-age`, backed by
      [scripts/check_quarantine_age.sh](scripts/check_quarantine_age.sh),
      wired into [ci.yml](.github/workflows/ci.yml) per push/PR. Window
      configurable via `ROTIDE_QUARANTINE_MAX_AGE_DAYS`.

### Determinism CI gate

- [x] `make test-determinism` exists.
- [x] [scripts/check_test_determinism.sh](scripts/check_test_determinism.sh)
      runs property suites twice with the same seed and diffs output.
- [x] Wired into CI on push/PR via the `determinism` job in
      [.github/workflows/ci.yml](.github/workflows/ci.yml).

### Test API contract

- [x] Written rule lives in [AGENTS.md](AGENTS.md) ("Test API contract"
      section). Test API exposes read-only views and counters; no mutators
      that production wouldn't itself call.

### Continuous improvement metrics

`tests/metrics.jsonl` is JSON-Lines, one row per producer per invocation.
Shared schema: `kind`, `ts` (ISO 8601 UTC), and optional env-enriched
`git_sha` / `git_ref` / `ci_run_id` read from `ROTIDE_METRICS_GIT_SHA` /
`ROTIDE_METRICS_GIT_REF` / `ROTIDE_METRICS_CI_RUN_ID`. The helper lives in
[tests/metrics_jsonl.{c,h}](tests/metrics_jsonl.h) and writes via
`O_APPEND` with a row size capped under PIPE_BUF so concurrent producers
don't tear lines. The file is gitignored — CI is expected to aggregate
rows out-of-tree (artifact upload or a separate metrics branch).

- [x] **`kind=test_run`** emitted by `rotide_tests --metrics-out PATH`
      with `wall_seconds`, `total_runs`, `passed_runs`, `failed_unique`,
      `crashes`, `reset_violations`, `skipped_quarantine`, `jobs`,
      `repeat`, `seed`, `shuffle`, `validate_reset`, `exit_code`.
- [x] **`kind=bench`** emitted by `rotide_bench --metrics-out PATH`,
      one row per case, with `name`, `samples`, `inner_ops`, `min_ns`,
      `p50_ns`, `p95_ns`, `iqr_ns`.
- [x] **`kind=fuzz`** emitted post-soak by
      [tests/metrics_fuzz_emit.c](tests/metrics_fuzz_emit.c), which
      parses captured libFuzzer stderr via
      [tests/metrics_libfuzzer_parse.{c,h}](tests/metrics_libfuzzer_parse.h)
      and scans the corpus directory. Wired into all four
      `fuzz-*-smoke` and `fuzz-*-nightly` Make targets — opt-in via
      `METRICS_OUT=path/to/metrics.jsonl`. Row fields: `target`,
      `cov_edges`, `ft_features`, `corp_count`/`corp_bytes` (libFuzzer
      reported), `corpus_files`/`corpus_bytes` (on-disk),
      `executed_units`, `avg_exec_per_sec`, `new_units_added`,
      `peak_rss_mb`, `runtime_seconds`, `has_final_stats`.
- [x] **Flake count** in `kind=test_run`: runner tracks per-test
      pass/fail tallies across `--repeat`; a test counts as a flake
      when it has *both* a pass and a fail. Plumbed through
      `__CHILD_SUMMARY` so parallel-mode aggregation matches the
      sequential count. Surfaces as `flakes=N` in the runner summary
      (only when `repeat > 1`) and as `flakes` in the metrics row.
- [x] **Property-test ops/sec** in `kind=test_run`: emitted as raw
      `property_ops` (count) and `property_ops_seconds` (elapsed), so
      consumers compute ops/sec = ops / seconds. API lives in
      [test_helpers.h](tests/test_helpers.h)
      (`test_property_ops_record` / `_total` / `_elapsed_seconds`);
      [test_text_invariants.c](tests/test_text_invariants.c)'s
      `runRandomOpsExtStride` is the first caller. Plumbed through
      `__CHILD_SUMMARY` for parallel-mode aggregation. Other property
      suites (text_summary, syntax_incremental_equiv) can call the
      same API when a baseline is wanted.
- [x] **Reader + regression detector** — implemented in C instead of Python
      to keep the test infrastructure on one toolchain. Binary
      [tests/metrics_summary](tests/metrics_summary.c) reads
      `tests/metrics.jsonl` (or any path via `--in`) and offers three
      subcommands:
      - `summary` — grouped table of recent rows per kind/target/name.
      - `check-fuzz-stale` — exit 1 if any fuzz target's `cov_edges`
        didn't grow within `--window-hours` (default 48). Closes the
        "alert if 48h run adds zero new edges" item.
      - `check-bench-regression` — exit 1 if any bench's latest p50
        moved more than `--factor` × `prev_iqr` (default 3.0). Matches
        the Phase 8 methodology rule.
      Parser lives in
      [tests/metrics_jsonl_read.{c,h}](tests/metrics_jsonl_read.h);
      subcommand logic in
      [tests/metrics_summary_cmd.{c,h}](tests/metrics_summary_cmd.h).
- [ ] Actual plot output (SVG / PNG) — deferred; the comparator covers
      the load-bearing CI alerting use cases, plots are nice-to-have
      once a few weeks of rows exist to visualise.

---

## Phases

Ordered by remaining-work, smallest payoff-per-day first → biggest. Phases
3 and 5 from the original plan are complete and noted inline.

### Phase 1: Runner ergonomics — mostly shipped

- [x] `--filter <substring>` (substring match against name).
- [x] `--tag <name>` / `--exclude-tag <name>` (suite-level tag selection).
- [x] `--list` (prints suite, name, tags; exit 0).
- [x] `--fail-fast` (stop at first FAIL).
- [x] `--repeat N`.
- [x] `--seed <u64>` exposed via [tests/seed.h](tests/seed.h).
- [x] `--shuffle` (deterministic with `--seed`).
- [x] `--no-quarantine`.
- [x] `-jN` parallel suite execution.
- [x] **Reset-state validator** via
      [editor_state_snapshot.h](tests/editor_state_snapshot.h);
      `snapshot_compare_*` tests cover the diff machinery and the parallel
      runner integrates it.
- [ ] `--watch` (inotify on `src/`/`tests/`, re-run filtered set on
      change). Low-priority dev-loop polish.

### Phase 2: Per-suite subprocess execution + parallel worker pool — shipped

- [x] Per-suite `fork()` in [parallel_runner.c](tests/parallel_runner.c);
      `-jN` (default `nproc`).
- [x] SIGSEGV/SIGABRT/SIGBUS handler dumps stack, test name, seed, op log
      to [tests/artifacts/crashes/](tests/artifacts/crashes/).
- [x] `make test-crash-handler` smoke covers the path.

### Phase 3: Property tests for document/row_cache invariants — shipped

[tests/test_text_invariants.c](tests/test_text_invariants.c) covers:

- [x] Differential equivalence to a `char*`-and-`memmove` reference doc
      (the `refDoc` harness).
- [x] `editorDocumentLength(doc) == sum_of_line_lengths_with_newlines`
      (post-refactor wording; `editorDocumentLineLength` is the per-line
      term).
- [x] `editorDocumentByteOffsetToPosition` ↔ `PositionToByteOffset`
      round-trip.
- [x] `assert_active_source_matches_rows` invoked across editing tests in
      [test_input_*.c](tests/) and [test_lsp_diagnostics.c](tests/test_lsp_diagnostics.c).
- [x] Rebuild-vs-splice counter invariants
      (`editorRowCacheTestFullRebuildCount` /
      `editorRowCacheTestSpliceUpdateCount` — "small edits do not
      full-rebuild").
- [x] Undo→redo round-trip back to a recorded snapshot.
- [x] Per-line `LineDup == LineView.data == CopyRange` parity check (added
      with the Phase-8 row-cache retirement).

### Phase 4: libFuzzer harnesses on the four input boundaries that actually take untrusted bytes

Order:

- [x] **vterm-fed escape stream.**
      [tests/fuzz/vterm/fuzz_vterm.c](tests/fuzz/vterm/fuzz_vterm.c) feeds
      `(data, size)` straight into a fresh `VTerm` via `vterm_input_write`,
      flushes damage, then reads every cell back so ASan surfaces parser
      overruns that don't manifest during the write itself.
- [x] **LSP framing parser** ([src/language/lsp_framing.c](src/language/lsp_framing.c),
      extracted from `lsp_transport.c` so the fuzz binary doesn't have
      to link the whole editor). Harness pipes bytes through a memfd
      into `editorLspReadFrame` and drains until the parser refuses
      ([tests/fuzz/lsp/fuzz_lsp.c](tests/fuzz/lsp/fuzz_lsp.c)). First
      run found two real bugs in the existing parser:
      - dead `parsed > SIZE_MAX` overflow check on 64-bit (silent
        wrap on huge `Content-Length:` strings) — fixed,
      - no upper bound on `Content-Length`, so a 20-digit value
        produced a multi-exabyte malloc (ASan
        `allocation-size-too-big`) — fixed via
        `ROTIDE_LSP_MAX_PAYLOAD_BYTES = 64 MiB`.
      Both have regression coverage in
      [tests/test_lsp_framing.c](tests/test_lsp_framing.c).
- [x] **DAP framing parser** ([src/debug/dap_client.c](src/debug/dap_client.c)).
      Wire format is identical to LSP but the parser is a duplicate, so
      it gets its own harness ([tests/fuzz/dap/fuzz_dap.c](tests/fuzz/dap/fuzz_dap.c))
      and corpus. Same two bugs as the pre-fix LSP parser were present
      (missing overflow guard + missing payload cap); both fixed,
      regressions in [tests/test_dap_framing.c](tests/test_dap_framing.c).
- [x] **Theme TOML parser** ([src/config/theme_parse.c](src/config/theme_parse.c)).
      Harness wraps the fuzz input in an `fmemopen` stream and drives
      the same `editorThemeApplyStream` production uses
      ([tests/fuzz/toml/fuzz_toml_theme.c](tests/fuzz/toml/fuzz_toml_theme.c)).
      200k mutation runs found zero crashes — the parser's fixed-size
      stack buffers, overflow-aware line reader, and bounded string
      ops appear to do their job. Coverage in CI is ~215 edges / 487
      features; a regression in this parser will surface there.
- [ ] Keymap / editor-config TOML paths (also TOML, share
      `editorConfigParseQuotedValue` with the theme parser which is
      already covered above; lower marginal value).

*Footnote:* the recovery snapshot reader in
[recovery.c](src/workspace/recovery.c) is lower priority than the three
user/network-input parsers above. Cover its corruption-tolerance via the
Phase 7 long-session test (mid-run, truncate the snapshot file and verify
next start doesn't crash) rather than a dedicated fuzzer.

Build harnesses with `-fsanitize=fuzzer,address,undefined` via `clang`
(see `FUZZ_FLAGS` in the Makefile). libFuzzer ships its own coverage
instrumentation under `-fsanitize=fuzzer`; the explicit
`-fsanitize-coverage=trace-pc-guard` originally listed in this plan
conflicts with modern clang and is omitted.

Operational hygiene (otherwise "we ran the fuzzer for 30 minutes" is a
liar's metric):

- [x] `tests/fuzz/vterm/corpus/` checked in with 18 seeds, 220 bytes
      total (well under the 50 KiB ceiling). Seeds cover plain text,
      cursor moves, SGR colours, mode set, OSC title + clipboard,
      bracketed paste, DCS, reverse index, malformed CSI, invalid UTF-8
      leads, C0 controls, scroll regions, DECALN.
- [x] Smoke target stages the corpus into a tempdir so libFuzzer's
      discovered mutations don't accrete back into the committed seeds.
      `make fuzz-vterm` against the committed corpus is the way to grow
      it deliberately.
- [x] Per-PR `make fuzz-vterm-smoke` wired into
      [.github/workflows/ci.yml](.github/workflows/ci.yml) (1000 runs
      default, ~90 s wall, crash inputs uploaded as job artifact on
      failure).
- [x] `tests/fuzz/lsp/corpus/` checked in with 21 seeds (~1 KB total)
      covering valid frames, header-case variants, missing/malformed
      Content-Length, the overflow repro that triggered the fix, and
      multi-frame streams.
- [x] Per-PR `make fuzz-lsp-smoke` wired into
      [.github/workflows/ci.yml](.github/workflows/ci.yml) (5000 runs
      default, <1 s wall locally; LSP framing is much cheaper per
      iter than the vterm path).
- [x] `tests/fuzz/dap/corpus/` checked in with 17 seeds covering the
      DAP-flavoured equivalents of the LSP seeds.
- [x] Per-PR `make fuzz-dap-smoke` wired into
      [.github/workflows/ci.yml](.github/workflows/ci.yml) (5000 runs
      default; ~2k exec/s on a CI runner).
- [x] `tests/fuzz/toml/corpus/` checked in with 22 seeds (~3 KB total)
      covering minimal selectors, full themes, padded whitespace,
      malformed hex, unknown tables/classes, unterminated strings,
      oversize lines, and embedded NULs.
- [x] Per-PR `make fuzz-toml-theme-smoke` wired into
      [.github/workflows/ci.yml](.github/workflows/ci.yml) (5000 runs
      default; <1 s wall locally).
- [ ] Crash repros imported as regression unit tests under
      `tests/test_*_fuzz_repro.c`. (Two real bugs found so far: both
      have regressions in the per-boundary framing suites
      [tests/test_lsp_framing.c](tests/test_lsp_framing.c) and
      [tests/test_dap_framing.c](tests/test_dap_framing.c); no
      crash-input file regressions yet because no minimised crash
      input outlived the same-PR fix.)
- [ ] Weekly `-merge=1` to minimise corpus. Defer until a `corpus_grown`
      cache crosses ~50 KB.
- [x] Working corpus persisted across CI runs. The nightly
      `fuzz-nightly` matrix job in [.github/workflows/nightly.yml](.github/workflows/nightly.yml)
      restores `tests/fuzz/<target>/corpus_grown/` from the previous
      run, lets libFuzzer add new finds in place, then saves the
      directory back via `actions/cache`. `corpus_grown/` is
      gitignored so the committed seed set stays curated.
- [x] Per-run edge count tracked in `tests/metrics.jsonl` (the
      `kind=fuzz` row described in the cross-cutting metrics section).
- [x] Alert if 48h run adds zero new edges — `tests/metrics_summary
      check-fuzz-stale --in tests/metrics.jsonl` exits 1 when any
      `target`'s `cov_edges` is unchanged across the configured window.
- [x] Full nightly runs (~30 min/target). `make fuzz-{vterm,lsp,dap,toml-theme}-nightly`
      wired into [nightly.yml](.github/workflows/nightly.yml) as a
      single matrix job; soak duration governed by `FUZZ_NIGHTLY_TIME`
      (default 1800 s); the four targets run in parallel on separate
      runners so total wall time stays ~30 min.

### Phase 5: Tree-sitter incremental ≡ full reparse — shipped

- [x] [test_syntax_incremental_equiv.c](tests/test_syntax_incremental_equiv.c)
      parametrised across every supported language via the
      [tests/syntax/supported/](tests/syntax/supported/) fixtures.
- [x] Small N per-PR; larger N can be tuned via the existing seed plumbing
      when run nightly.
- [ ] **Document any acceptable divergences as a skip-list** with linked
      issues if/when grammars in injection-heavy modes (markdown→code,
      HTML→JS/CSS) produce noise.

### Phase 6: Normalised vterm grid snapshot helper — partial

Helper + assertion macro shipped. Source-rewriting (`--update-golden`)
and the HTML diff renderer are still open.

- [x] [tests/test_grid_snapshot.h](tests/test_grid_snapshot.h) helper:
  - [x] Captures `refresh_screen_and_capture()` bytes (with a frame-cache
        reset so the snapshot reflects the full screen, not a delta).
  - [x] Feeds them into a `VTerm` of the editor's current size.
  - [x] Returns deterministic text representation (one line per row,
        trailing spaces stripped, trailing blank rows collapsed).
  - [ ] Optional cursor markers — not yet; the libvterm screen API
        doesn't expose cursor position directly, needs a callback hook.
  - [ ] Optional styled-span rendering — defer until a test actually
        needs styled assertions.
- [x] `ASSERT_GRID_EQ(expected_multiline_string)` macro, plus
      `editor_grid_snapshot_diff()` that emits a unified-ish line diff.
- [x] Self-test suite [test_grid_snapshot_suite.c](tests/test_grid_snapshot_suite.c)
      covers capture, trailing-space strip, diff reporting, identity
      assertion, and a hand-baked chrome layout assertion that
      demonstrates the API in production-test form.
- [x] `--update-golden [path]` runner flag — when set, `ASSERT_GRID_EQ`
      mismatches stash to a JSONL file instead of failing. Default
      stash path is `tests/artifacts/goldens.jsonl`.
      [tests/golden_apply](tests/golden_apply.c) reads the stash and
      rewrites each referenced source file between
      `/* golden-start */` and `/* golden-end */` block markers,
      preserving the start marker's indentation. Implementation lives
      in [tests/grid_snapshot_update.{c,h}](tests/grid_snapshot_update.h)
      (stash writer),
      [tests/grid_snapshot_format.{c,h}](tests/grid_snapshot_format.h)
      (shared C-string-literal emitter), and
      [tests/golden_apply_lib.{c,h}](tests/golden_apply_lib.h) (parser
      + rewriter). Worked example: the baked chrome layout in
      [tests/test_grid_snapshot_suite.c](tests/test_grid_snapshot_suite.c).
- [x] [tests/golden_diff_report](tests/golden_diff_report.c) — text
      unified-diff preview of pending stash updates. Same shape as the
      planned HTML report, but emitted as plain text for terminal /
      CI-log readability. Exits 1 when any entry has a non-empty diff.
      An HTML variant can be layered on later if a PR-comment workflow
      wants it.
- [x] `make update-goldens` convenience target — captures the stash,
      prints the unified-diff preview, and (with `APPLY=1`) rewrites
      the source files. `UPDATE_GOLDEN_FLAGS='--filter NAME'` scopes
      the capture to a subset of tests.
- [x] Convert the worst raw-byte `strstr` offenders in
      [test_render_frame.c](tests/test_render_frame.c) and
      [test_render_panes.c](tests/test_render_panes.c). Three tests
      converted as worked examples:
      - `test_editor_refresh_screen_contains_expected_sequences` —
        replaced the two visible-text strstrs with one `ASSERT_GRID_EQ`;
        the five escape-sequence strstrs (cursor style, OSC) remain
        as-is because they verify on-the-wire byte format.
      - `test_editor_popup_renders_overlay_in_text_area` — replaced the
        two completion-label strstrs with one grid snapshot.
      - `test_editor_refresh_screen_vertical_split_renders_border` —
        subsumed both the vertical-border-glyph and content-present
        checks into one snapshot.
      Other visible-text strstrs in [test_render_panes.c](tests/test_render_panes.c)
      (nested-split marker checks, popup-close-repaints, popup placement
      tests) can be converted with the same `--update-golden` workflow;
      deferred until they actually need maintenance, since each
      conversion commits the test to a specific full-screen layout.

### Phase 7: Long-session memory growth test — shipped

[tests/test_long_session.c](tests/test_long_session.c) ships the harness
plus four scenarios driven through a shared `run_growth_scenario`.

- [x] Per-scenario harness with warmup + K-iteration measured loop.
- [x] Assert live-alloc bytes (`mallinfo2().uordblks`, glibc only) return
      to within a small bound of baseline (256 KiB slop ≈ 1.3 KiB/iter
      regression catch threshold at K=200).
- [x] Assert `getrusage(RUSAGE_SELF).ru_maxrss` growth trends to zero
      (native: 2 MiB slop; sanitizers: 32 MiB slop default — ASan/TSan
      shadow memory inflates RSS; syntax_reparse gets a 128 MiB slop
      because tree-sitter state takes longer to reach steady-state).
- [x] Run under sanitizers in CI (covered by `make test-sanitize`).
- [x] Open / edit / undo / close cycle scenario.
- [x] Syntax reparse-cycle scenario (open .c file, K=100 edit/undo
      rounds, close — exercises tree-sitter incremental path and the
      syntax visible cache).
- [x] Terminal-pane spawn / pump / free scenario (K=40, real fork+exec
      of `true` per iteration; baseline is currently 0 KiB RSS / 0-byte
      live delta).
- [x] LSP open / close scenario (mock-backed via
      `editorLspTestSetMockEnabled`; clangd path).
- [ ] **Top-N retained-bytes report grouped by caller** using
      `__builtin_return_address` in the alloc hook. "RSS grew 4 MB" is a
      red light with no next step; "RSS grew 4 MB and 80% of retained
      allocations came from `editorScrollbackAppend`" is a bug report.
      Requires adding `editorFree(void *)` and converting `free()` call
      sites; defer until a regression actually fires.

Steady-state observation worth flagging for follow-up: the
syntax_reparse scenario retains ~850 bytes/iter in the native build
(85 KiB over 100 iterations). That's well inside the 256 KiB slop, but
the slope is non-zero and points at tree-sitter / syntax-visible-cache
state that doesn't fully release on close. Worth profiling under the
Top-N caller report once that's built.

`ROTIDE_LONG_SESSION_REPORT=1` env var makes each scenario emit a
`long_session_report:` line with baseline / final / delta numbers,
useful for investigating a CI failure without re-running locally.

### Phase 8: Microbench coverage + one whole-program hyperfine scenario

Storage microbench landed with the row-cache retirement. The percentile
harness ships in [tests/bench_runner.{c,h}](tests/bench_runner.h);
benches live in [tests/bench_microbenches.c](tests/bench_microbenches.c);
run via `make bench`.

- [x] [tests/bench_text_storage.c](tests/bench_text_storage.c) reports
      `open_reset` MB/s, random insert/delete/replace µs/op, and row-cache
      RSS delta. Run via `make bench-buffer`.
- [x] **Percentile-reporting harness**: `name min/p50/p95/iqr` in
      nanoseconds via `clock_gettime(CLOCK_MONOTONIC)`, N=20 samples by
      default, `--iterations N`, `--filter SUBSTR`, `--json PATH` flags.
- [x] document position↔byte round-trip (256 KiB doc, 1024 inner ops/sample).
- [x] row_cache splice for a small edit (256 KiB doc, 16 inner
      insert-then-revert cycles/sample).
- [x] wrap recompute on a 1000-line buffer (120-char lines wrapping at
      80 cols, full-cache invalidate + recompute per inner op).
- [x] syntax incremental edit on a 5k-line C-like source (insert byte +
      ApplyEditAndParse, then revert + ApplyEditAndParse, 4 cycles/sample).
- [x] screen-diff against unchanged frame (24×80 view of a 100-line
      doc, 8 refreshes per sample, frame cache primed at setup).
- [x] screen-diff with one row changed (4 insert/refresh/undo/refresh
      cycles per sample). Bench setup wires the editor state via
      `reset_editor_state` from [tests/test_helpers.c](tests/test_helpers.c)
      and redirects stdout to /dev/null so we measure in-process work,
      not the syscall path.
- [x] One `hyperfine` scenario: `make bench-render-once`. Generates a
      ~17 MiB synthetic C source in /tmp on first run (cached) and
      times `./rotide --render-once <fixture>` with default 20 runs /
      5 warmup. `--render-once` is a general non-interactive
      single-frame render mode in [rotide.c](src/rotide.c) — it skips
      raw mode and the TTY window-size probe, uses fixed 80x24
      dimensions, renders one frame via the existing refresh path, and
      exits. Rotide itself doesn't know it's being benchmarked; the
      same flag can drive docs-screenshot or other headless-render
      callers. Local baseline on the dev host: ~2.2 s wall to cold-open
      and render the 17 MiB fixture.

Baseline numbers on the host this was developed on (rough; do not treat
as a regression budget yet — needs CI-runner calibration per the
methodology below):

```
document_position_byte_roundtrip   p50 ≈ 2 µs    p95 ≈ 2.4 µs   iqr ≈ 75 ns
row_cache_splice_small_edit        p50 ≈ 14 µs   p95 ≈ 26 µs    iqr ≈ 9 µs
wrap_recompute_1k_lines            p50 ≈ 646 µs  p95 ≈ 656 µs   iqr ≈ 6 µs
syntax_incremental_5k_lines_c      p50 ≈ 22 ms   p95 ≈ 23 ms    iqr ≈ 300 µs
screen_diff_unchanged_frame        p50 ≈ 71 µs   p95 ≈ 73 µs    iqr ≈ 2 µs   (per refresh)
screen_diff_one_row_changed        p50 ≈ 133 µs  p95 ≈ 138 µs   iqr ≈ 4 µs   (per insert/refresh/undo/refresh)
```

The syntax bench includes a parse-on-revert cycle (inner_ops=4 means
4 forward + 4 reverse `ApplyEditAndParse` calls per sample), so a single
incremental parse on this fixture is roughly p50 ÷ 8 ≈ 2.7 ms.

Methodology:

- [ ] Microbenches: N=20 iterations per metric, report median and IQR.
- [ ] Regression test = "median delta > 3 × baseline IQR", not flat
      percentage.
- [ ] Hyperfine: `--warmup 5 --runs 20`, fail only on >3× IQR shift.
- [ ] Per-PR: post comment with delta table; do not fail build until the
      methodology has been validated against ~2 weeks of CI noise. If
      GitHub-hosted runner noise stays unworkable, move to a self-hosted
      bench runner explicitly rather than drifting.
- [x] Per-metric medians and IQRs feed `tests/metrics.jsonl` —
      `rotide_bench --metrics-out PATH` appends one `kind=bench` row per
      case with `min_ns`/`p50_ns`/`p95_ns`/`iqr_ns`/`samples`/`inner_ops`.

---

## CI changes that follow from the above

[.github/workflows/ci.yml](.github/workflows/ci.yml) now runs `make` +
`make test` + `make test-sanitize` + `make test-determinism` +
`make test-crash-handler` per push/PR.
[.github/workflows/nightly.yml](.github/workflows/nightly.yml) runs
`make test-tsan` on the threaded subset.

Per push / PR:

- [x] `make`, `make test`.
- [x] `make test-sanitize`.
- [x] `make test-determinism`.
- [x] `make test-crash-handler`.
- [x] `make test-quarantine-age`.
- [x] `make fuzz-vterm-smoke` (~90 s with 1000 runs).
- [x] `make fuzz-lsp-smoke` (~1 s with 5000 runs).
- [x] `make fuzz-dap-smoke` (~1 s with 5000 runs).
- [x] `make fuzz-toml-theme-smoke` (<1 s with 5000 runs).

Nightly:

- [x] `make test-tsan` on the threaded subset.
- [x] `make test-quarantine-passing` (fails if any quarantined test now
      passes).
- [x] Full fuzz runs (~30 min/target) — `fuzz-nightly` matrix in
      [nightly.yml](.github/workflows/nightly.yml). Each target soaks
      for `FUZZ_NIGHTLY_TIME` seconds (default 1800) with corpus
      persisted across runs via `actions/cache`.
- [ ] Property tests with large N — Phase 3 harness exposes seed/op count;
      wiring a second invocation with `--seed` + larger op budget is
      cheap.
- [ ] Hyperfine + microbench against committed baseline — Phase 8.
- [x] Per-job metrics rows appended in CI and uploaded as artifacts.
      [ci.yml](.github/workflows/ci.yml) and
      [nightly.yml](.github/workflows/nightly.yml) set
      `ROTIDE_METRICS_GIT_SHA` / `GIT_REF` / `CI_RUN_ID` at the workflow
      level and pass `METRICS_OUT=tests/metrics.jsonl` to the
      build-and-test and fuzz Make targets. Each job uploads its row as
      `metrics-<job>`; a final `metrics-summary` job downloads all of
      them, runs `tests/metrics_summary summary`, prints to the log, and
      uploads the merged file as `metrics-merged` (per-workflow snapshot).
- [x] Cross-run aggregation via `actions/cache`. The `metrics-summary`
      job in each workflow restores `tests/metrics-history.jsonl` from a
      `metrics-history-*` cache (prefix-restore, run-id save), drops any
      rows matching the current `ci_run_id` (so workflow re-runs don't
      double-count), appends the new rows, and saves back. The rolling
      history is uploaded as `metrics-history` / `metrics-history-nightly`
      so it's downloadable for offline analysis.
- [x] Comparator wired into nightly. `metrics-summary` in
      [nightly.yml](.github/workflows/nightly.yml) runs
      `check-fuzz-stale --window-hours 48` and `check-bench-regression`
      against the rolling history. Both emit `::warning::` annotations
      on non-zero exit but don't fail the cron yet — flip to hard-fail
      once the false-positive rate has been characterised.
- [ ] Post results as a comment on `main`'s last commit — requires a
      PR-comment / commit-comment action. Both `metrics-history` and
      `metrics-merged` artifacts are now available for that step to
      consume.

- [ ] Don't add a separate "coverage" job until Phase 6 lands; until then
      it measures the wrong thing.

## Things deliberately not in this plan

- A new test framework. The custom runner is sufficient.
- A scripting DSL for headless editor sessions.
- Pexpect / Python tooling.
- `vttest`.
- Per-test subprocess isolation (per-suite is the right grain).
- Mutation testing.
- Anything labelled "p99 latency budget."
