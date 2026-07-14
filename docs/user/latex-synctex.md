# LaTeX and SyncTeX

RotIDE uses Texlab as the broker between a LaTeX source buffer and an external
PDF viewer. Texlab resolves the main document in multi-file projects, starts
builds, performs forward search, and sends inverse-search locations back to
RotIDE.

## Prerequisites

- Texlab must run on the host. Inverse search with one-based viewer line
  numbers requires Texlab 5.23 or newer.
- Build with SyncTeX enabled. Both `latexmk -synctex=1` and
  `pdflatex -synctex=1` are supported.
- Install a supported host PDF viewer: Okular, Evince, or Zathura.
- When LaTeX runs in Distrobox, keep the project under `$HOME` so paths
  recorded in `.synctex.gz` resolve to the same files on the host.

## Quick start

Configure Texlab globally in `~/.rotide/config.toml`:

```toml
[lsp]
texlab_enabled = true
texlab_command = "~/.local/bin/texlab"
texlab_pdf_viewer = "okular"
texlab_build_command = "distrobox enter latex -- make build"
texlab_build_on_save = false
texlab_forward_search_after_build = false
texlab_aux_directory = ""
texlab_pdf_directory = ""
```

The build command above uses the project's Makefile. A direct `latexmk`
configuration is also possible:

```toml
texlab_build_command = "distrobox enter latex -- latexmk -pdf -interaction=nonstopmode -synctex=1 %f"
```

Texlab substitutes `%f` with the resolved main document. RotIDE splits command
strings into an executable and arguments; double quotes group an argument.
Shell pipelines and redirections are not interpreted.

Launch RotIDE from the project root so Texlab and `.rotide.toml` use that
project:

```sh
cd ~/Documents/my-latex-project
rotide main.tex
```

The settings are sent when Texlab starts. Restart RotIDE after changing them.

Inside a saved `.tex` or `.bib` buffer:

- `:latex build` asks Texlab to build the project's main document. It returns
  immediately; build diagnostics appear in the Problems drawer.
- `:latex view` forward-searches from the cursor to the configured viewer.
- `Ctrl-O` returns after an inverse-search jump.

For automatic build and forward search on save:

```toml
texlab_build_on_save = true
texlab_forward_search_after_build = true
```

Texlab turns the LaTeX log into diagnostics. Use a terminal pane or task when
you need the complete raw build output.

## Makefile and pdflatex

Using `pdflatex` directly is fine. The essential flag is `-synctex=1`:

```make
LATEXFLAGS = -interaction=nonstopmode -halt-on-error -synctex=1

build:
	pdflatex $(LATEXFLAGS) main.tex
```

Run enough passes for references and bibliographies, or let `latexmk` manage
them. Forward search needs the PDF and matching `.synctex.gz` from the latest
build.

Leave `texlab_aux_directory` and `texlab_pdf_directory` empty when output is
beside the main `.tex` file. Set them when the build uses a separate auxiliary
or PDF directory, for example:

```toml
texlab_aux_directory = "build"
texlab_pdf_directory = "build"
```

For projects with different layouts, place only the safe layout overrides in
the project root's `.rotide.toml`:

```toml
[lsp]
texlab_pdf_viewer = "okular"
texlab_aux_directory = "build"
texlab_pdf_directory = "build"
texlab_forward_search_after_build = true
```

Executable commands and `texlab_build_on_save` remain global-only so an
untrusted repository cannot replace a command or silently enable a build.

## Okular inverse search

Open **Settings → Configure Okular → Editor**, choose **Custom Text Editor**,
and set:

```text
texlab inverse-search -i "%f" --line1 %l
```

Then Shift-click the PDF to jump to the source. Viewer line numbers are
one-based, so use `--line1`. Texlab's shorter `-l` option means zero-based
`--line0` and causes an off-by-one jump here.

The `okular` forward-search preset expands to:

```text
okular --unique "file:%p#src:%l%f"
```

## Evince

Install the Python 3 `evince-synctex` bridge from
<https://github.com/latex-lsp/evince-synctex>. Then configure:

```toml
texlab_pdf_viewer = "evince"
```

The preset starts `evince-synctex`; it performs forward search and registers
Ctrl-click inverse search automatically.

## Zathura

Configure the viewer preset:

```toml
texlab_pdf_viewer = "zathura"
```

Add this to `~/.config/zathura/zathurarc`:

```text
set synctex-editor-command "texlab inverse-search -i %{input} --line1 %{line}"
```

## Custom forward-search command

A global custom command overrides `texlab_pdf_viewer`. Texlab substitutes
`%f` with the source file, `%l` with the line, and `%p` with the PDF:

```toml
texlab_forward_search_command = "okular --unique \"file:%p#src:%l%f\""
```

## Troubleshooting

- **Viewer opens but does not jump:** rebuild with `-synctex=1` and ensure the
  PDF and `.synctex.gz` belong to the current sources.
- **Wrong output PDF:** set `texlab_aux_directory` and
  `texlab_pdf_directory` to match the build output layout.
- **Jump is one line off:** use `--line1`, not `-l` or `--line0`, in the
  viewer's inverse-search command.
- **Inverse search does nothing:** verify `texlab inverse-search --help` lists
  `--line1`, and confirm RotIDE's Texlab instance is still running.
- **Two RotIDE windows are open:** Texlab uses one
  `$XDG_RUNTIME_DIR/texlab.sock`; the most recently started Texlab owns
  inverse-search clicks.
- **RotIDE does not raise its terminal window:** the cursor still moves, but
  window focus is controlled by the terminal emulator and window manager.
- **A Distrobox build records unusable paths:** keep the project under
  `$HOME`, or make the container and host paths identical.

RotIDE does not parse `.synctex.gz` or render PDFs itself. Texlab and the PDF
viewer own synchronization; RotIDE handles the source-buffer actions and
standard LSP `window/showDocument` jumps.
