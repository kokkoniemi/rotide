# Build and Tests

RotIDE uses a plain Makefile. Build output is compact by default; use `V=1` for
full compiler and linker commands.

## Common Targets

```bash
make
make test
make test-sanitize
make release
make docs-media
make docs-diagrams
```

- `make`: builds `rotide`.
- `make test`: builds and runs `tests/rotide_tests`.
- `make test-sanitize`: cleans, rebuilds tests with AddressSanitizer and
  UndefinedBehaviorSanitizer, then runs them.
- `make release`: builds a size-oriented binary and strips it.
- `make docs-media`: regenerates screenshots under `docs/media/screenshots/`.
- `make docs-diagrams`: renders PlantUML sources from `docs/diagrams/src/` to
  committed SVG files under `docs/diagrams/svg/`.

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
