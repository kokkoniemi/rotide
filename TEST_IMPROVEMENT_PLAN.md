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

The foundation is now in good shape. Remaining work is concentrated in
**fuzzing the genuinely-untrusted input boundaries**, a **normalised vterm
grid snapshot helper**, a **long-session memory-growth test**, broader
**microbench coverage beyond storage**, and **wiring the existing
sanitiser/determinism/crash targets into CI** (today only `make test` and
`make test-sanitize` run on push/PR).

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
- [ ] **Nightly job that runs with `--no-quarantine` and fails loudly if a
      quarantined test starts passing again.**
- [ ] **30-day age enforcement** — per-PR build fails on stale entries
      unless explicitly re-upped.

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

- [ ] **`tests/metrics.jsonl`** appended by the runner per CI run: wall time,
      test count, flake count, fuzz corpus size, fuzz edge count,
      property-test ops/sec.
- [ ] **`scripts/metrics_chart.py`** plotting trends over time.

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

- [ ] **vterm-fed escape stream into `editorTerminalPane`.** Single-function
      harness: feed `data, size` via `vterm_input_write`, pump, assert
      cursor in bounds, scrollback below cap, no aborts.
- [ ] **LSP framing parser** (`Content-Length:`-framed JSON-RPC in
      [src/language/lsp_transport.c](src/language/lsp_transport.c)).
      Chunked, malformed, oversized headers.
- [ ] **DAP framing parser** ([src/debug/dap.c](src/debug/dap.c) /
      transport layer).
- [ ] **Theme TOML parser** ([src/config/theme_parse.c](src/config/theme_parse.c))
      and the keymap/editor TOML paths.

*Footnote:* the recovery snapshot reader in
[recovery.c](src/workspace/recovery.c) is lower priority than the three
user/network-input parsers above. Cover its corruption-tolerance via the
Phase 7 long-session test (mid-run, truncate the snapshot file and verify
next start doesn't crash) rather than a dedicated fuzzer.

Build each as a `LLVMFuzzerTestOneInput` translation unit guarded by
`-DROTIDE_FUZZ`. Add `make fuzz-vterm`, `make fuzz-lsp`, etc., with
`-fsanitize=fuzzer,address,undefined` and
`-fsanitize-coverage=trace-pc-guard,trace-cmp`.

Operational hygiene (otherwise "we ran the fuzzer for 30 minutes" is a
liar's metric):

- [ ] `tests/fuzz/<target>/corpus/` seed corpus checked in (<50 KB total).
- [ ] Crash repros imported as regression unit tests under
      `tests/test_*_fuzz_repro.c`.
- [ ] Weekly `-merge=1` to minimise corpus.
- [ ] Per-run edge count tracked in `tests/metrics.jsonl`; alert if 48h
      run adds zero new edges.
- [ ] Working corpus persisted across CI runs (cache).
- [ ] 60-second smoke target on per-PR; full ~30 min runs nightly.

### Phase 5: Tree-sitter incremental ≡ full reparse — shipped

- [x] [test_syntax_incremental_equiv.c](tests/test_syntax_incremental_equiv.c)
      parametrised across every supported language via the
      [tests/syntax/supported/](tests/syntax/supported/) fixtures.
- [x] Small N per-PR; larger N can be tuned via the existing seed plumbing
      when run nightly.
- [ ] **Document any acceptable divergences as a skip-list** with linked
      issues if/when grammars in injection-heavy modes (markdown→code,
      HTML→JS/CSS) produce noise.

### Phase 6: Normalised vterm grid snapshot helper

- [ ] `tests/test_grid_snapshot.h` helper:
  - [ ] Captures `refresh_screen_and_capture()` bytes.
  - [ ] Feeds them into a `VTerm` of the editor's current size.
  - [ ] Returns deterministic text representation (one line per row,
        trailing spaces stripped, optional cursor markers, optional styled
        spans).
- [ ] `ASSERT_GRID_EQ(expected_multiline_string, ...)` macro.
- [ ] `--update-golden` runner flag that rewrites expected strings via
      marker comments.
- [ ] `scripts/golden_diff_report.py` for side-by-side HTML diff of
      pending golden updates. Lands at the same time as `--update-golden`
      so updates have a review path on day one.
- [ ] Convert the worst raw-byte `strstr` offenders in
      [test_render_frame.c](tests/test_render_frame.c) and
      [test_render_panes.c](tests/test_render_panes.c). Keep byte-level
      assertions for tests that specifically check escape-sequence emission
      (cursor style, OSC52).

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
- [ ] wrap recompute on a 1000-line buffer.
- [ ] screen-diff against unchanged frame.
- [ ] screen-diff with one row changed.
- [ ] syntax incremental edit on a 5k-line C file.
- [ ] One `hyperfine` scenario: cold-open a generated 10 MB C file, render
      once, exit. The only whole-program metric users notice.

Baseline numbers on the host this was developed on (rough; do not treat
as a regression budget yet — needs CI-runner calibration per the
methodology below):

```
document_position_byte_roundtrip   p50 ≈ 2 µs   p95 ≈ 2 µs    iqr ≈ 30 ns
row_cache_splice_small_edit        p50 ≈ 14 µs  p95 ≈ 26 µs   iqr ≈ 9 µs
```

Methodology:

- [ ] Microbenches: N=20 iterations per metric, report median and IQR.
- [ ] Regression test = "median delta > 3 × baseline IQR", not flat
      percentage.
- [ ] Hyperfine: `--warmup 5 --runs 20`, fail only on >3× IQR shift.
- [ ] Per-PR: post comment with delta table; do not fail build until the
      methodology has been validated against ~2 weeks of CI noise. If
      GitHub-hosted runner noise stays unworkable, move to a self-hosted
      bench runner explicitly rather than drifting.
- [ ] Per-metric medians and IQRs feed `tests/metrics.jsonl`.

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
- [ ] `make fuzz-vterm-smoke` (60s) — add post-Phase 4.

Nightly:

- [x] `make test-tsan` on the threaded subset.
- [ ] Full fuzz runs (~30 min/target) — add post-Phase 4.
- [ ] Property tests with large N — Phase 3 harness exposes seed/op count;
      wiring a second invocation with `--seed` + larger op budget is
      cheap.
- [ ] Hyperfine + microbench against committed baseline — Phase 8.
- [ ] Quarantined-tests-pass-now check (runs full suite with
      `--no-quarantine`, fails if any `^- ` entry from
      [tests/QUARANTINE.md](tests/QUARANTINE.md) shows PASS).
- [ ] Append a row to `tests/metrics.jsonl`; post results as a comment on
      `main`'s last commit.

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
