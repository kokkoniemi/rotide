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
make release
make docs-media
make docs-diagrams
```

- `make`: builds `rotide`.
- `make test`: builds and runs `tests/rotide_tests`. Passes
  `--validate-reset` by default so any future regression that leaves
  `editorConfig E` dirty across `reset_editor_state` fails the suite.
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
- `make release`: builds a size-oriented binary and strips it.
- `make docs-media`: regenerates screenshots under `docs/media/screenshots/`.
- `make docs-diagrams`: renders PlantUML sources from `docs/diagrams/src/` to
  committed SVG files under `docs/diagrams/svg/`.

### Runner flags

`tests/rotide_tests` accepts the following flags; pass them via
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
- `--no-quarantine` / `--quarantine <path>`: bypass or override
  `tests/QUARANTINE.md`. The nightly CI run should use
  `--no-quarantine` so flakes that have started passing again surface
  loudly.

If LeakSanitizer is flaky locally:

```bash
ASAN_OPTIONS=detect_leaks=0 make test-sanitize
```

Mention that limitation when reporting validation.

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
