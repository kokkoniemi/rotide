# Rotide Test Harness Improvement Plan

## Snapshot of where we are

- ~1,150 tests in [tests/](tests/), ~25k lines across ~30 files. Wall time ~26s for `make test`, sequential.
- `-Werror`, ASan + UBSan via `make test-sanitize`. Reasonable warning flags.
- Custom runner: each test is `int (*run)(void)` returning 0/1, dispatched from
  [rotide_tests_main.c](tests/rotide_tests_main.c) with `reset_editor_state()` between tests.
- Real screen capture via [`refresh_screen_and_capture`](tests/test_helpers.c#L508), real PTY
  tests via [test_pty.c](tests/test_pty.c) and [test_terminal_pane.c](tests/test_terminal_pane.c)
  with vterm grid scraping.
- Failure injection: [alloc_test_hooks.c](tests/alloc_test_hooks.c),
  [save_syscalls_test_hooks.h](tests/save_syscalls_test_hooks.h),
  syntax budget/parse-fail countdowns, an LSP mock with rich knobs.
- Test-only stat hooks already exposed via [editor_test_api.h](tests/editor_test_api.h)
  (full-rebuild vs incremental, text-source builds/dups, row-cache splices).
- Single global `editorConfig E`. Tests are inherently sequential in-process.

The foundation is unusually strong for a kilo-derivative. The gaps are not
"more frameworks". They are (a) no runner ergonomics for the size the suite has
become, (b) no randomized/property coverage of the invariants the rebuild-stat
counters already exist to support, (c) no fuzzers on the genuinely risky
parser/transport boundaries, and (d) no normalized snapshot helper for screen
output, leaving raw-byte regex matching as the only available mechanism.

---

## Rejecting weak options

Reject outright:

- **Adding cmocka / Criterion / Google Benchmark.** The custom runner is fine
  and tightly integrated; replacing it buys nothing. Google Benchmark would
  drag a C++ build target in for no reason.
- **Pexpect / Python PTY layer.** Rotide already drives a real PTY in C with
  vterm grid scraping. Bolting a Python harness on top duplicates that for
  worse error messages and a new toolchain dependency.
- **vttest compatibility automation.** Output is interactive and meant for
  human inspection; automating it is a multi-week project with low marginal
  signal over what fuzzed escape feeding will already catch.
- **Mutation testing (Mull).** Premature. Useful only after the suite is much
  more behavioral and significantly faster.
- **`-Werror` "added gradually".** Already on. Skip.
- **"Add a `--script foo.edtest` headless DSL."** Rotide tests already drive
  `editorConfig E` and the input pipeline directly; a new DSL is a parallel
  surface to maintain that gives no new coverage. Keep tests in C.
- **TAP / JUnit XML output.** Add only if/when CI dashboards need it. Today
  `PASS/FAIL` lines are fine and `grep -c FAIL` works.
- **Generic coverage / static analysis push** as top-priority work. Coverage
  is a measurement, not a test; clang-tidy/cppcheck on this codebase will
  produce hundreds of style hits and one or two real bugs. Worthwhile, but
  far below the items below.
- **Keypress-to-screen latency p99 SLOs.** Rotide doesn't have a hot render
  loop with a frame budget. A latency dashboard would chase noise. Render
  *throughput* on realistic frames is what matters; track that instead.

Accept with caveats:

- Snapshot/golden tests: yes, but only if they compare *normalized vterm grids*,
  not raw bytes. Raw-byte snapshots over the existing
  `refresh_screen_and_capture` would be brittle the day someone changes a
  styling sequence.
- Fuzzing: yes, but only on a small set of high-risk input boundaries
  enumerated below, not "all parsers."
- `hyperfine` whole-program benchmarks: yes for the build/start path and a
  large-file open scenario. Skip for in-process hot paths; a microbench mode
  in the runner is better signal there.

---

## Cross-cutting infrastructure (not phases; fold into the relevant phase or land alongside)

These are not standalone phases because each is 1–3 days. But they are the
operational scaffolding without which the phases ship as "interesting" rather
than "trustworthy in CI six months from now."

### Sanitizer matrix: TSan is missing

The plan currently runs ASan + UBSan. [src/language/syntax_worker.c](src/language/syntax_worker.c)
has a real `pthread_create`/`pthread_mutex` worker thread, and the LSP/DAP
transport does I/O off the main thread. **Add `make test-tsan`** that runs
the syntax-worker, LSP, DAP, and file-watch suites under
`-fsanitize=thread`. Don't try to TSan the whole suite. TSan plus a forked
worker pool gets noisy. Run nightly, not per-PR.

### Flake quarantine policy

`--repeat` and `--shuffle` (Phase 1) will surface 2–5 latent flaky tests in
the first week. Without a written policy, the suite degrades into
"the usual suspects." Convention:

- A `tests/QUARANTINE.md` listing each quarantined test, the GitHub issue
  link, the date quarantined, and the owner.
- The runner reads this file and skips listed tests by default with a
  `SKIP <name> (quarantined: <issue>)` line.
- `--no-quarantine` runs them anyway (used by the nightly job, which fails
  loudly if a quarantined test starts passing. That's a signal to remove
  it, not ignore it).
- Max age 30 days; older entries fail the per-PR build until either fixed
  or explicitly re-upped with a comment.

### Determinism CI gate

The whole edifice of seeded property tests and fuzz repros assumes
determinism. Add a CI job that runs the property suites *twice* with the
same `--seed` and `diff`s output (PASS/FAIL lines plus any captured op
logs). If it diverges, you've found nondeterminism *before* it costs an
engineer half a day on a non-reproducible bug.

### Test API contract

Phases 3 and 7 will grow [editor_test_api.h](tests/editor_test_api.h).
Without a written rule, it eventually contains helpers that *only the
tests use* and the tests start testing the test API. One paragraph in
[AGENTS.md](AGENTS.md):

> The test API may expose read-only views of internal state and counters.
> It must not provide mutators that production code wouldn't itself call.
> Adding a mutator means the test is asserting an arrangement that
> production cannot reach. Write the test against a real code path
> instead, or add the missing production path.

### Continuous improvement metrics

Track in `tests/metrics.jsonl` (one line per CI run), appended by the
runner: wall time, test count, flake count (tests that needed retry),
fuzz corpus size, fuzz edge count, mean property-test ops/sec. Without
this, in 12 months you won't know whether the testing investment paid
off. A small `scripts/metrics_chart.py` that plots these is enough;
do not build a dashboard.

---

## Phases

Ordered by ratio of confidence-gained to engineer-time. Each phase is shippable
on its own; later phases assume earlier ones landed but don't strictly require it.

### Phase 1: Runner ergonomics (small change, daily payoff)

**Build:** Extend [rotide_tests_main.c](tests/rotide_tests_main.c) to accept:

- `--filter <substring>`: match against `name`. Substring, not regex; cheap.
- `--tag <name>` / `--exclude-tag <name>`: add an optional `tags` field to
  `editorTestCase` (e.g. `@slow`, `@pty`, `@needs_clangd`, `@fuzz_repro`) and
  select by tag rather than name. Substring filtering is fragile at 1,150 tests
  (`--filter pty` matches `paste_typed`); CI subset selection should be tag-driven.
- `--list`: print test names and tags, exit 0.
- `--fail-fast`: stop at first FAIL, print the failing name last.
- `--repeat N`: run each selected test N times, useful for flake hunting.
- `--seed <u64>` and a `tests/seed.h` exposing `rotide_test_seed()` so randomized
  tests in later phases can opt in. Print the seed in every FAIL line.
- `--shuffle`: shuffle test order with the seed (catches ordering coupling
  in `reset_editor_state`).
- `--watch`: re-run the currently-filtered set on file change in `src/` or
  `tests/` (inotify on Linux). Cheap to add, biggest single dev-loop win on
  top of `--filter`/`--tag`.

**Also in this phase: reset-state validator.** A debug-build mode that
snapshots a hash of `editorConfig E` plus the alloc-hook live counters, LSP
mock state, and syntax-worker queue depth before and after each test, and
asserts they match. Without this, `--shuffle` is a research tool; with it,
shuffle becomes safe to enable in CI. This is the load-bearing item that
makes Phase 2's per-suite isolation expose latent state coupling instead of
papering over it. ~1 day on top of the runner flags above.

**Why for Rotide:** the suite is already 1,150 tests. Today the only way to
re-run one failing case is to comment out the others; that is the single biggest
friction point. Shuffle + seed + the reset-state validator will surface any
latent test-order coupling the giant `reset_editor_state` is hiding, and tell
you precisely which global drifted.

**Impact:** high (every developer, every day). **Complexity:** ~200 LoC for
the runner flags plus ~100 LoC for the state validator, ~1.5 days total.
**Tradeoffs:** none worth mentioning. **Order:** do first.

### Phase 2: Per-suite subprocess execution + parallel worker pool

**Build:** Wrap each `editorTestSuite` (not each test; fork-per-test would
outweigh the test runtime) in a fork. A small worker pool (`-jN`, default
`nproc`) hands suites to children; the parent collects PASS/FAIL summaries.
Crashes/aborts in one suite no longer take down the whole run; you get
"`<name>` crashed: signal 11" instead of `make test` going silent.

**Crash artifacts (non-negotiable):** "crashed: signal 11" without a stack
trace just relocates the silent failure. Each child installs SIGSEGV/SIGABRT/
SIGBUS handlers that dump a libbacktrace stack, the failing test name, the
current `--seed`, and any active op log to `tests/artifacts/<suite>/`. The
parent collects these on non-zero exit; CI uploads the directory as a job
artifact. Without this, post-Phase-2 crashes are loud but undebuggable on
machines you don't own.

**Why for Rotide:** the global `editorConfig E`, the LSP mock singleton, and
the alloc-failure global all make in-process parallelism unsafe. Suites are the
natural unit because they already share fixtures and don't share state across
suite boundaries by convention. Suite-level isolation also makes it safe to
add fuzz/property suites that intentionally mutate global state aggressively.

**Impact:** high. Drops wall time roughly linearly in cores (the suite is
embarrassingly parallel at suite granularity), and turns silent crashes into
diagnosable failures. **Complexity:** ~300 LoC; the trickier part is the
parent-side reporter, not the fork itself. **Tradeoffs:** each child re-loads
the 40 MB binary, but mmap+COW makes that cheap; expect <5% per-suite overhead.
**Order:** second.

### Phase 3: Property tests for the rope/document/row_cache invariants

**Build:** A new `tests/test_text_invariants.c` suite with a tiny operation
generator:

```c
enum { OP_INSERT, OP_DELETE, OP_REPLACE, OP_RESET, OP_UNDO, OP_REDO };
```

The generator picks N ops with the seeded RNG, applies them, and after each op
asserts:

1. `editorDocumentLength(document) == sum_of_row_sizes_with_newlines`
2. `editorDocumentByteOffsetToPosition` round-trips with `PositionToByteOffset`
3. `editorActiveSourceMatchesRows()` already exists in
   [test_support.c](tests/test_support.c): call it after every op.
4. The existing rebuild-vs-splice counters from
   [editor_test_api.h](tests/editor_test_api.h): assert that "small edits do
   not full-rebuild." This is exactly what those counters were added for and
   what this test enforces.
5. Undo+redo back to a recorded snapshot must restore byte-equal text.
6. **Differential equivalence to a brain-dead reference rope.** A ~200-LoC
   `char*`-and-`memmove` reference implementation receives the same op log
   as the production rope; after every op, the two must produce a
   byte-identical buffer dump and identical position↔offset answers.
   Invariant assertions (1–5) catch "internally consistent but wrong" only
   by accident; both ends of a bad delete can pass `editorActiveSourceMatchesRows`
   while still being wrong. Differential testing catches that by
   construction. **This is the single highest-leverage item in this phase**
   and the place where regressions in the editor spine will actually be
   caught.

On failure, print the seed and the exact op log so the case is reproducible
as a one-line repro test. For differential failures, also dump the first
diverging byte offset and a ±32-byte window from each implementation.

**Why for Rotide:** the rope/document/row_cache split is the spine of the
editor and the documented "non-negotiable" in [AGENTS.md](AGENTS.md). The
test stats exist *because* this is the invariant most worth defending.
Targeted unit tests cover known shapes; randomized op sequences cover the
unknown ones, and the counters give you a real assertion that splice-fast-paths
are still firing.

**Impact:** very high. This is where silent regressions of the editor
spine would land. **Complexity:** ~400 LoC. **Tradeoffs:** seeded randomized
tests can be flaky if assertions are too tight; counter assertions for
"splice ratio" should be ratio-based, not exact counts. **Order:** third.

### Phase 4: libFuzzer harnesses on the four input boundaries that actually take untrusted bytes

Pick these four, in this order:

1. **vterm-fed escape stream into `editorTerminalPane`.** Single function
   harness: feed `data, size` via `vterm_input_write`, pump, assert cursor
   in bounds, scrollback below cap, no aborts. The vendored libvterm
   parser is the highest-risk untrusted-input surface in the binary today.
2. **LSP/DAP framing parsers.** `Content-Length:`-framed JSON-RPC streams from
   subprocesses. A fuzz target that feeds chunked, malformed, oversized
   headers into [lsp_transport.c](src/language/lsp_transport.c) and
   [dap_client.c](src/debug/dap_client.c). Even with a friendly server,
   real servers send pathological framing during shutdown races.
3. **Theme TOML parser** ([theme_parse.c](src/config/theme_parse.c)) and the
   keymap/editor TOML paths. Users edit these by hand; a corrupt config
   should never crash the editor.

*Footnote, not a numbered target:* the recovery snapshot reader in
[recovery.c](src/workspace/recovery.c) reads Rotide-written files and is
far lower priority than the three user/network-input parsers above. Cover
its corruption-tolerance via the long-session test in Phase 7 (mid-run,
truncate the snapshot file and verify next start doesn't crash) rather than
a dedicated fuzzer.

Build each as a `LLVMFuzzerTestOneInput` translation unit guarded by
`-DROTIDE_FUZZ`. Add `make fuzz-vterm`, `make fuzz-lsp`, etc., that build
that single TU plus its minimum link closure with `-fsanitize=fuzzer,address,
undefined` and `-fsanitize-coverage=trace-pc-guard,trace-cmp`. Commit a small
`tests/fuzz/<target>/corpus/` seed corpus (<50 KB total) and import any
crash repros as regression unit tests under `tests/test_*_fuzz_repro.c`.

**Operational hygiene (otherwise "we ran the fuzzer for 30 minutes" is a
liar's metric):**

- Run `-merge=1` weekly to minimize the corpus and keep coverage.
- Track edge counts per every-other-night run; alert if a 48h fuzz adds zero new edges
  (target is stuck: corpus needs new seeds, harness is bottlenecked, or the
  parser is genuinely covered).
- Persist the working corpus across runs in CI cache; a cold corpus on every
  run wastes the first ~5 minutes rediscovering basic edges.
- Edge counts feed the `tests/metrics.jsonl` file from the cross-cutting
  section so coverage trend is visible over months, not just per run.

**Why for Rotide:** these are the only four byte-level boundaries with
untrusted input. Fuzzing anything else is theatre. The vterm boundary in
particular has the worst blast radius (the screen) and the most opaque parser.

**Impact:** very high on real defects, near-zero on day-to-day workflow.
**Complexity:** medium. Clang-only build, separate link line, corpus
hygiene. **Tradeoffs:** fuzzers are best run nightly, not per-PR; a 60-second
smoke target per PR is enough on the merge path. **Order:** fourth, but the
vterm fuzzer alone is a worthwhile standalone deliverable if scope is tight.

### Phase 5: Tree-sitter incremental ≡ full reparse property test

**Build:** Using the existing [tests/syntax/supported/](tests/syntax/supported/)
fixtures, for each supported language:

1. Load fixture as the buffer.
2. Apply N seeded random edits via `editorInsertText` /
   `editorDocumentReplaceRange`.
3. Snapshot the captured-token list from the incremental parse path.
4. Force a full reparse of the same final byte sequence.
5. Assert captured tokens match.

Run this as a single suite parametrized by language. Use a small N (~20 edits)
in the per-PR run, large N (~500) in nightly.

**Why for Rotide:** the editor relies on incremental parsing for highlight,
indent, and locals. Divergence between incremental and full parse silently
corrupts highlighting; existing tests check incremental and full paths
separately but never compare them against each other. The fixtures already
exist; this is mostly glue.

**Impact:** high. **Complexity:** ~250 LoC plus a small per-language
parametrization table. **Tradeoffs:** some grammars (markdown injections,
HTML→JS/CSS) will produce differences that are arguably acceptable; document
known divergences as skip-list entries with an issue link. **Order:** fifth.

### Phase 6: Normalized vterm grid snapshot helper

**Build:** A `test_grid_snapshot.h` helper that:

1. Captures `refresh_screen_and_capture()`'s bytes.
2. Feeds them into a `VTerm` of the editor's current size.
3. Returns a deterministic, stable text representation:
   - one line per row, trailing spaces stripped,
   - cursor as `^` markers under the row, optional,
   - styled spans rendered as `{fg=N,bg=N,bold}…{/}` only on test request.
4. Plus `ASSERT_GRID_EQ(expected_multiline_string, ...)` and an
   `--update-golden` runner flag that rewrites the expected string in the test
   source via a marker comment.
5. A `scripts/golden_diff_report.py` that, given the set of golden updates
   in a working tree, emits a side-by-side HTML report. Without this, when
   one PR legitimately updates 40 goldens, reviewers will rubber-stamp them.
   `--update-golden` lands second; the diff renderer lands at the same time
   so updates have a review path on day one.

Convert the worst offenders in [test_render_frame.c](tests/test_render_frame.c)
and [test_render_panes.c](tests/test_render_panes.c) (the ones doing
multi-`strstr` against escape sequences) to grid snapshots. Don't convert
tests that are explicitly checking escape-sequence emission (cursor style,
OSC52); those should stay byte-level.

**Why for Rotide:** the existing render tests are valuable but read like
log-grep lines. A grid snapshot says "the user sees this," which is what the
test should be asserting and what makes a regression diff readable.

**Impact:** medium-high. Improves both readability and the chance a render
regression is caught with a one-character diff in the expected string.
**Complexity:** medium. Vterm reuse is straightforward; the
`--update-golden` source-rewriting is the fiddly part and should land second.
**Tradeoffs:** golden tests have a maintenance cost; only convert tests
where the byte-level assertions are obscuring intent. **Order:** sixth.

### Phase 7: Long-session memory growth test using the existing alloc hooks

**Build:** A small suite that, per scenario, captures a baseline RSS,
runs an op loop for K iterations (open/edit/close, terminal-pane spawn/feed/
kill, syntax reparse cycles, LSP open/close), and asserts:

1. The alloc-hook live-allocation count returns to within a small bound of
   baseline.
2. RSS (via `getrusage(RUSAGE_SELF).ru_maxrss`) growth per iteration trends
   to zero, not to a slope.
3. The alloc-hook diff between baseline and final, **grouped by caller**
   (the hooks already see the call site via `__builtin_return_address`).
   "RSS grew 4 MB" is a red light with no next step; "RSS grew 4 MB and
   80% of retained allocations came from `editorScrollbackAppend`" is a
   bug report. Failure output dumps the top-10 retained-bytes sites.

Run under sanitizers in CI. ASan/LSan already catches outright leaks; this
catches *retained* allocations and unbounded caches (scrollback, undo history,
syntax visible cache, LSP document map): bugs ASan never reports.

**Why for Rotide:** "memory grows over a long session" is the canonical
editor complaint. The alloc hooks make this measurable today and nothing
uses them for trend assertions.

**Impact:** medium-high. Catches a class of bug ASan misses by design.
**Complexity:** small. **Tradeoffs:** RSS measurement on Linux is granular
(pages); use iteration counts large enough that drift dominates noise.
**Order:** seventh.

### Phase 8: Targeted microbenchmark mode + one whole-program hyperfine scenario

**Build:** A `tests/rotide_bench` binary (or a `--bench` mode of the test
binary) that:

- Iterates a small named set of microbenchmarks: rope read, document
  position↔byte round-trip, row_cache splice, wrap recompute on a 1000-line
  buffer, screen-diff against unchanged frame, screen-diff with one row
  changed, syntax incremental edit on a 5k-line C file.
- Reports `name min p50 p95` in nanoseconds via `clock_gettime(CLOCK_MONOTONIC)`,
  one line each, JSON output behind `--json bench.json`.

Plus one `hyperfine` scenario in CI: cold-open a generated 10 MB C file,
render once, exit. That's the one whole-program metric users notice.

**Methodology (the part that determines whether this is signal or noise):**

- Microbenches: run N=20 iterations per metric, report median and IQR.
  Compare to baseline using "median delta > 3 × baseline IQR" as the
  regression test, not a flat percentage. A flat "+10% warn / +20% fail" is
  guaranteed to be flaky on a shared CI runner because per-metric noise
  floors differ by an order of magnitude.
- Hyperfine: `--warmup 5 --runs 20`, report median; fail only on >3× IQR
  shift, informational-only on per-PR runs initially.
- On per-PR runs, post a comment with the bench delta table; do not fail
  the build until the methodology has been validated against ~2 weeks of
  noise data on the actual CI runner. Self-hosted bench runner is the
  alternative if GitHub-hosted noise stays unworkable; pick one explicitly
  rather than drifting into "we have benchmarks but nobody trusts them."
- Per-metric medians and IQRs feed `tests/metrics.jsonl` so trends are
  visible across months.

**Why for Rotide:** these are the operations that, when slow, are felt as
"the editor got laggy."

**Impact:** medium. Catches slow-creep regressions on hot paths.
**Complexity:** small for the harness, medium for the comparison policy
(noise control on a shared CI runner is the real challenge; see "CI" below).
**Tradeoffs:** benchmarks on GitHub-hosted runners are noisy. Treat
fail-on-regression as nightly-only initially; per-PR run is "report only."
**Order:** last of the build-out phases.

---

## CI changes that follow from the above

- Per push / PR: `make test -jN` (after Phase 2), `make test-sanitize -jN`,
  one `make fuzz-vterm-smoke` of 60s with the committed corpus, and the
  determinism gate from the cross-cutting section.
- Nightly: `make test-tsan` on the threaded subset, full fuzz runs (each
  target ~30 min), property tests with large N, hyperfine + microbench
  against the committed baseline, the quarantined-tests-pass-now check, and
  append a row to `tests/metrics.jsonl`. Post results as a comment on
  `main`'s last commit.
- Don't add a separate "coverage" job until Phase 3 lands; until then it
  measures the wrong thing.

## Things deliberately not in this plan

- A new test framework. The custom runner is sufficient.
- A scripting DSL for headless editor sessions.
- Pexpect / Python tooling.
- `vttest`.
- Per-test subprocess isolation (per-suite is the right grain).
- Mutation testing.
- Anything labelled "p99 latency budget."
