# Build and Tests

RotIDE uses a plain Makefile. Build output is compact by default; use `V=1` for
full compiler and linker commands.

For the test pipeline itself — what we run, what each layer catches,
how to reproduce a failure — see [testing.md](testing.md). This page is
the command reference.

## Common Targets

```bash
make
make test
make test-sanitize
make test-tsan
make test-determinism
make test-crash-handler
make test-quarantine-age
make test-quarantine-passing
make bench
make bench-buffer
make bench-render-once
make update-goldens
make fuzz-vterm-smoke
make fuzz-lsp-smoke
make fuzz-dap-smoke
make fuzz-toml-theme-smoke
make release
make docs-media
make docs-diagrams
```

- `make`: builds `build/rotide`.
- `make test`: builds and runs `build/tests/rotide_tests` with
  `--validate-reset --jobs 4` by default.
- `make test-sanitize`: cleans, rebuilds tests with AddressSanitizer and
  UndefinedBehaviorSanitizer, then runs them.
- `make test-tsan`: cleans, rebuilds with ThreadSanitizer, and runs the
  threaded subset (suites tagged `threads`, `lsp`, `dap`, `file_watch`,
  `pty`). Intended for nightly CI, not per-PR. Linux only. The target
  wraps the binary in `setarch -R` so TSan's shadow mapping fits
  alongside the editor's large BSS-resident state.
- `make test-determinism`: runs the binary twice with a fixed seed and
  diffs the outcome lines (`PASS`/`FAIL`/`SKIP`/drift/summary). Catches
  nondeterminism in seeded property tests and runner output before it
  costs an engineer half a day chasing an unreproducible bug.
- `make test-crash-handler`: triggers a synthetic SIGSEGV in one test via
  `ROTIDE_TEST_CRASH=<suite>/<test>` and asserts the runner reports a
  `CRASH` line, writes the artifact under
  `tests/artifacts/crashes/<suite>/<test>.crash`, and exits non-zero.
  Use this whenever the crash-handler code path changes.
- `make test-quarantine-age`: fails if an active
  `tests/QUARANTINE.md` entry is older than the configured window
  (`ROTIDE_QUARANTINE_MAX_AGE_DAYS`, default 30).
- `make test-quarantine-passing`: runs the quarantined set with
  `--no-quarantine` and fails if any quarantined test now passes.
- `make bench`: runs `build/tests/rotide_bench` microbenches and prints
  min/p50/p95/IQR nanoseconds.
- `make bench-buffer`: runs storage throughput/RSS checks.
- `make bench-render-once`: uses `hyperfine` to time
  `./build/rotide --render-once` on a generated large C fixture.
- `make update-goldens`: captures grid-snapshot diffs; add `APPLY=1` to
  rewrite `golden-start` / `golden-end` blocks.
- `make fuzz-*-smoke`: builds the matching libFuzzer harness with
  `clang -fsanitize=fuzzer,address,undefined`, stages the seed corpus in a
  tempdir, and runs a bounded per-PR smoke.
- `make fuzz-*-nightly`: long-running fuzz soaks that write new finds to
  `tests/fuzz/<target>/corpus_grown/`.
- `make release`: builds a size-oriented binary and strips it.
- `make docs-media`: regenerates screenshots under `docs/media/screenshots/`.
- `make docs-diagrams`: renders PlantUML sources from `docs/diagrams/src/` to
  committed SVG files under `docs/diagrams/svg/`.

### Runner flags

`build/tests/rotide_tests` accepts the following flags; pass them via
`make test TEST_FLAGS="..."`:

- `--filter <substr>`: run only tests whose name contains `<substr>`.
- `--tag <name>` / `--exclude-tag <name>`: select suites by tag
  (`document`, `syntax`, `lsp`, `dap`, `pty`, `slow`, `threads`, etc.).
  `--list` prints suite/name/tags and exits.
- `--seed <u64>` + `--shuffle`: deterministic test reordering. The seed
  is printed on every `FAIL` and in the trailing summary; pass the same
  seed back to reproduce.
- `--repeat <N>`: re-run each selected test N times (flake hunting).
- `--fail-fast`: stop at the first FAIL.
- `--validate-reset`: assert that `reset_editor_state` restores
  `editorConfig E` to a canonical state between tests (default in
  `make test`).
- `--jobs <N>`: run up to N suites in parallel as forked children. Each
  child writes its stdout/stderr to `tests/artifacts/logs/<suite>.log`
  and, if it crashes, a stack dump to
  `tests/artifacts/crashes/<suite>/<test>.crash`. The parent emits
  output in suite-index order, so determinism is preserved. Wall time
  scales roughly linearly in cores.
- `--metrics-out <path>`: append one `kind=test_run` JSONL row.
- `--update-golden [path]`: make `ASSERT_GRID_EQ` mismatches append to a
  stash instead of failing. The default path is
  `tests/artifacts/goldens.jsonl`; pair with `make update-goldens`.
- `--no-quarantine` / `--quarantine <path>`: bypass or override
  `tests/QUARANTINE.md`. The nightly CI run should use
  `--no-quarantine` so flakes that have started passing again surface
  loudly.

If LeakSanitizer is flaky locally:

```bash
ASAN_OPTIONS=detect_leaks=0 make test-sanitize
```

Mention that limitation when reporting validation.

### Metrics

Any target that runs tests, benches, or fuzz smoke can append metrics:

```bash
make test METRICS_OUT=tests/metrics.jsonl
make bench METRICS_OUT=tests/metrics.jsonl
make fuzz-lsp-smoke METRICS_OUT=tests/metrics.jsonl
```

Rows are JSON Lines. CI sets `ROTIDE_METRICS_GIT_SHA`,
`ROTIDE_METRICS_GIT_REF`, and `ROTIDE_METRICS_CI_RUN_ID` so rows can be
merged across jobs and deduplicated across workflow reruns.

Summarize or check a merged file with:

```bash
make build/tests/metrics_summary
./build/tests/metrics_summary summary --in tests/metrics.jsonl
./build/tests/metrics_summary check-fuzz-stale --in tests/metrics-history.jsonl
./build/tests/metrics_summary check-bench-regression --in tests/metrics-history.jsonl
```

### Golden Snapshots

`ASSERT_GRID_EQ` comparisons normally fail with a line diff. Update mode
captures the actual grid to a stash:

```bash
make update-goldens UPDATE_GOLDEN_FLAGS='--filter popup'
make update-goldens APPLY=1 UPDATE_GOLDEN_FLAGS='--filter popup'
```

Only calls whose expected literal is wrapped by `/* golden-start */` and
`/* golden-end */` can be rewritten.

### Fuzz Targets

| Target | Boundary | Corpus | Smoke runs |
|---|---|---|---|
| `fuzz-vterm-smoke` | libvterm escape stream | `tests/fuzz/vterm/corpus/` | `FUZZ_VTERM_SMOKE_RUNS` (default 1000) |
| `fuzz-lsp-smoke` | LSP frame parser | `tests/fuzz/lsp/corpus/` | `FUZZ_LSP_SMOKE_RUNS` (default 5000) |
| `fuzz-dap-smoke` | DAP frame parser | `tests/fuzz/dap/corpus/` | `FUZZ_DAP_SMOKE_RUNS` (default 5000) |
| `fuzz-toml-theme-smoke` | theme TOML parser | `tests/fuzz/toml/corpus/` | `FUZZ_TOML_THEME_SMOKE_RUNS` (default 5000) |

Nightly variants use `FUZZ_NIGHTLY_TIME` seconds per target (default
1800) and write to `corpus_grown/`.

## Diagram Tooling

Install PlantUML so `plantuml` is on PATH, then run:

```bash
make docs-diagrams
```

The diagram sources use PlantUML stdlib C4 includes, for example:

```plantuml
!include <C4/C4_Container>
```

The generated SVGs are committed because Markdown renderers can display them
without requiring every reader to install PlantUML.

## Generated Headers

Two checked-in inputs generate C headers during normal builds:

- `scripts/queries_manifest.txt` plus query files generate
  `src/language/syntax_query_data.h`.
- `config.toml.example` generates `src/config/default_config_data.h`.

The generated headers are build artifacts and are removed by `make clean`.

## Tree-sitter Vendor Refresh

Pinned runtime and grammar versions are tracked under `vendor/tree_sitter/`.
Refresh them with:

```bash
./scripts/refresh_tree_sitter_vendor.sh
```

After a vendor refresh, run at least:

```bash
make
make test
make test-sanitize
```

## Validation Expectations

For docs-only changes, run:

```bash
make
make test
```

For Makefile, generated-header, syntax, LSP, save/recovery, storage, or other
build-sensitive work, also run:

```bash
make test-sanitize
```

Warnings are blockers because `-Werror` is enabled.
