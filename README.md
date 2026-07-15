# RotIDE

RotIDE is a terminal text editor focuses on predictable behavior, explicit data
flow, and strong regression coverage.

_The current status_: RotIDE is under active development and in pre alpha stage.
The core functionality is in place, and I already use it as my main code editor.
For anyone else, I don't yet recommend to use it for any serious work.

![RotIDE editing its own source](docs/media/screenshots/theme-github-dark.png)

➡️  [More screenshots here](docs/media/screenshots/README.md)

## Highlights

- UTF-8/grapheme-safe editing with multi-tab and split-pane workflow.
- Project drawer: file tree, file search, project-wide text search, Git changes,
  debugger, and LSP Problems/Symbols.
- Modal **Vim** editing with configurable per-mode and leader bindings.
- Tree-sitter highlighting for 48 languages; LSP definitions, symbols,
  problems, and autocomplete where enabled.
- Atomic saves with crash-recovery snapshots; built-in and custom TOML themes.

See the [user guide](docs/user/README.md) for the full feature list,
keybindings, and configuration.

## Quick start

Requirements:

- POSIX-like environment
- C compiler with C2x support

Build and run:

```bash
make
./build/rotide
```

### Fil-C builds

Production builds use [Fil-C](https://fil-c.org), a memory-safe C compiler.
Install a [pizfix binary release](https://fil-c.org/install_pizfix) and symlink
it to `~/.fil-c` (or set `FILC_ROOT=/path/to/install`):

```bash
make filc        # memory-safe dev build at build/filc/rotide
make test-filc   # full test suite under Fil-C
make release     # production: Fil-C, static-pie, stripped, at build/rotide
```

`make release-native` keeps the previous non-Fil-C release path. Fil-C uses
`-O2` for development, tests, and releases; it costs more CPU and RSS in
exchange for guaranteed memory safety. The default `make` remains a plain
native build.

## Documentation

- [User guide](docs/user/README.md) — features, keybindings, configuration,
  screenshots.
- [Developer docs](docs/developer/README.md) — architecture, workflows, build
  and tests.
- [Metrics dashboard](docs/developer/metrics-dashboard.md) — CI trend charts
  for tests, benchmarks, fuzzing, and lines of code.

## License

See [`LICENSE`](LICENSE).
