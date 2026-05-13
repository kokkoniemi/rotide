#!/usr/bin/env python3
"""Generate RotIDE documentation screenshots with VHS.

The script creates a temporary HOME and project workspace, writes a small
RotIDE config for each scene, writes a temporary VHS tape, and asks VHS to
render a PNG into docs/media/screenshots/.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCREENSHOT_DIR = REPO_ROOT / "docs" / "media" / "screenshots"
ROTIDE_BIN = REPO_ROOT / "rotide"

COMMON_CONFIG_TEMPLATE = """\
[editor]
cursor_style = "bar"
cursor_blink = false
line_wrap = false
line_numbers = true
current_line_highlight = true
auto_indent = true
indent_style = "tabs"
indent_width = 4

[theme]
name = "{theme}"

[lsp]
gopls_enabled = false
clangd_enabled = false
html_enabled = false
css_enabled = false
json_enabled = false
javascript_enabled = true
eslint_enabled = true
javascript_command = "typescript-language-server --stdio"
eslint_command = "vscode-eslint-language-server --stdio"
autocomplete_max_items = 8

[keymap]
project_search = "ctrl+t"
lsp_drawer = "ctrl+u"
"""


@dataclass(frozen=True)
class Scene:
    name: str
    output: str
    description: str
    tape_body: tuple[str, ...]
    theme: str = "kanagawa-wave"
    lsp_scene: bool = False
    required_commands: tuple[str, ...] = ()
    open_file: str = "src/rotide.c"


def vhs_type(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'Type "{escaped}"'


def vhs_run(command: str) -> tuple[str, ...]:
    return (
        vhs_type(command),
        "Enter",
    )


SCENES: tuple[Scene, ...] = (
    Scene(
        name="editor-source",
        output="editor-source.png",
        description="RotIDE editing its own C source with syntax highlighting, tabs, drawer, and status line.",
        tape_body=(
            "Sleep 1800ms",
            "Ctrl+E",
            "Sleep 500ms",
            "Ctrl+E",
            "Sleep 1000ms",
        ),
    ),
    Scene(
        name="drawer-tree",
        output="drawer-tree.png",
        description="Project drawer navigation over a compact RotIDE fixture tree.",
        tape_body=(
            "Sleep 1500ms",
            "Ctrl+E",
            "Sleep 300ms",
            "Down",
            "Down",
            "Enter",
            "Sleep 700ms",
        ),
    ),
    Scene(
        name="project-search",
        output="project-search.png",
        description="Project text search results previewing matches in RotIDE source.",
        tape_body=(
            "Sleep 1500ms",
            "Ctrl+T",
            "Sleep 300ms",
            vhs_type("editorDocument"),
            "Sleep 1200ms",
            "Down",
            "Sleep 700ms",
        ),
    ),
    Scene(
        name="lsp-autocomplete-js",
        output="lsp-autocomplete-js.png",
        description="JavaScript autocomplete popup powered by a real TypeScript language server.",
        tape_body=(
            "Sleep 3500ms",
            "Down 5",
            "Right 7",
            vhs_type("."),
            "Sleep 3500ms",
        ),
        lsp_scene=True,
        required_commands=("typescript-language-server", "tsserver"),
        open_file="app.js",
    ),
    Scene(
        name="lsp-eslint-problems",
        output="lsp-eslint-problems.png",
        description="ESLint diagnostics collected through LSP and shown in the Problems drawer.",
        tape_body=(
            "Sleep 4200ms",
            "Ctrl+U",
            "Sleep 1200ms",
        ),
        lsp_scene=True,
        required_commands=(
            "typescript-language-server",
            "tsserver",
            "vscode-eslint-language-server",
            "eslint",
        ),
        open_file="lint-problems.js",
    ),
    Scene(
        name="settings-config",
        output="settings-config.png",
        description="The generated global config file, including editor, theme, LSP, and keymap settings.",
        tape_body=(
            "Sleep 1600ms",
            "PageDown",
            "Sleep 900ms",
        ),
        open_file=".home/.rotide/config.toml",
    ),
    Scene(
        name="theme-kanagawa-wave",
        output="theme-kanagawa-wave.png",
        description="Kanagawa Wave theme applied to the same RotIDE source fixture.",
        tape_body=(
            "Sleep 1600ms",
        ),
        theme="kanagawa-wave",
    ),
    Scene(
        name="theme-modus-operandi",
        output="theme-modus-operandi.png",
        description="Modus Operandi theme applied to the same RotIDE source fixture.",
        tape_body=(
            "Sleep 1600ms",
        ),
        theme="modus-operandi",
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-build", action="store_true", help="use an existing ./rotide")
    parser.add_argument("--scene", action="append", dest="scenes",
            help="generate one scene by name; may be repeated")
    parser.add_argument("--list", action="store_true", help="list available scenes and exit")
    parser.add_argument("--skip-lsp", action="store_true",
            help="skip scenes that require real language servers")
    parser.add_argument("--dry-run", action="store_true",
            help="print planned outputs and generated tape contents without running commands")
    return parser.parse_args()


def command_exists(command: str) -> bool:
    return shutil.which(command) is not None


def selected_scenes(names: list[str] | None) -> list[Scene]:
    if not names:
        return list(SCENES)
    by_name = {scene.name: scene for scene in SCENES}
    missing = [name for name in names if name not in by_name]
    if missing:
        valid = ", ".join(scene.name for scene in SCENES)
        raise SystemExit(f"Unknown scene(s): {', '.join(missing)}\nValid scenes: {valid}")
    return [by_name[name] for name in names]


def print_scene_list() -> None:
    for scene in SCENES:
        suffix = ""
        if scene.required_commands:
            suffix = " [requires: " + ", ".join(scene.required_commands) + "]"
        print(f"{scene.name:22} {scene.output}{suffix}")
        print(f"  {scene.description}")


def preflight(scenes: list[Scene]) -> None:
    missing: list[str] = []
    if not command_exists("python3"):
        missing.append("python3")
    if not command_exists("vhs"):
        missing.append("vhs")
    if not command_exists("ttyd"):
        missing.append("ttyd")

    for scene in scenes:
        for command in scene.required_commands:
            if command not in missing and not command_exists(command):
                missing.append(command)

    if not missing:
        return

    print("Missing required command(s):", ", ".join(missing), file=sys.stderr)
    print("", file=sys.stderr)
    if "python3" in missing:
        print("Install Python 3 with your system package manager.", file=sys.stderr)
    if "vhs" in missing:
        print("Install VHS: https://github.com/charmbracelet/vhs", file=sys.stderr)
    if "ttyd" in missing:
        print("Install ttyd for VHS rendering: https://github.com/tsl0922/ttyd", file=sys.stderr)
    if any(command in missing for command in ("typescript-language-server", "tsserver")):
        print(
            "Install TypeScript autocomplete demo tools, for example:\n"
            "  npm install --global --prefix ~/.local "
            "typescript typescript-language-server",
            file=sys.stderr,
        )
        print("Then ensure ~/.local/bin is on PATH.", file=sys.stderr)
    if any(command in missing for command in ("vscode-eslint-language-server", "eslint")):
        print(
            "Install ESLint diagnostics demo tools, for example:\n"
            "  npm install --global --prefix ~/.local "
            "vscode-langservers-extracted eslint",
            file=sys.stderr,
        )
        print("Then ensure ~/.local/bin is on PATH.", file=sys.stderr)
    raise SystemExit(1)


def run_checked(command: list[str], cwd: Path = REPO_ROOT) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def build_rotide(no_build: bool) -> None:
    if no_build:
        if not ROTIDE_BIN.exists():
            raise SystemExit("./rotide does not exist; run make or omit --no-build")
        return
    run_checked(["make"])


def copy_fixture_workspace(workspace: Path) -> None:
    for directory in (
        workspace / "src",
        workspace / "src" / "language",
        workspace / "src" / "config",
        workspace / "tests" / "syntax" / "supported" / "javascript",
        workspace / "docs",
    ):
        directory.mkdir(parents=True, exist_ok=True)

    files = (
        "README.md",
        "config.toml.example",
        "src/rotide.c",
        "src/rotide.h",
        "src/language/lsp_protocol.c",
        "src/language/lsp_transport.c",
        "src/language/autocomplete.c",
        "src/config/theme_config.c",
        "tests/syntax/supported/javascript/highlight.js",
    )
    for rel in files:
        src = REPO_ROOT / rel
        dst = workspace / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    (workspace / "docs" / "capture-notes.md").write_text(
        "# Capture Notes\n\n"
        "This generated workspace keeps RotIDE documentation screenshots deterministic.\n",
        encoding="utf-8",
    )
    (workspace / "app.js").write_text(
        "const client = {\n"
        "  completionCount: 1,\n"
        "  completionContext: true,\n"
        "  connect() { return this.completionCount; },\n"
        "};\n"
        "\n"
        "client",
        encoding="utf-8",
    )
    (workspace / "lint-problems.js").write_text(
        "const unused = 1\nconsole.log(missingValue)\n",
        encoding="utf-8",
    )
    (workspace / "eslint.config.js").write_text(
        "export default [{\n"
        "  files: ['**/*.js'],\n"
        "  languageOptions: { ecmaVersion: 2022, sourceType: 'module' },\n"
        "  rules: { 'no-undef': 'error', 'no-unused-vars': 'warn', semi: 'error' }\n"
        "}];\n",
        encoding="utf-8",
    )
    (workspace / "package.json").write_text(
        '{"type":"module","devDependencies":{"eslint":"*","typescript":"*"}}\n',
        encoding="utf-8",
    )


def write_config(home: Path, theme: str) -> Path:
    config_dir = home / ".rotide"
    config_dir.mkdir(parents=True, exist_ok=True)
    path = config_dir / "config.toml"
    path.write_text(COMMON_CONFIG_TEMPLATE.format(theme=theme), encoding="utf-8")
    return path


def scene_open_path(scene: Scene, workspace: Path, home: Path) -> Path:
    if scene.open_file.startswith(".home/"):
        return home / scene.open_file[len(".home/"):]
    return workspace / scene.open_file


def tape_for_scene(scene: Scene, workspace: Path, home: Path, frame_output: Path) -> str:
    env = (
        f'cd {sh_quote(str(workspace))} && '
        f'HOME={sh_quote(str(home))} '
        "TERM=xterm-256color "
        f'{sh_quote(str(ROTIDE_BIN))} {sh_quote(str(scene_open_path(scene, workspace, home)))}'
    )
    lines = [
        f"Output {quote_vhs_path(frame_output)}",
        "Set Shell bash",
        "Set FontSize 16",
        "Set Width 1280",
        "Set Height 760",
        "Set Framerate 15",
        "Set PlaybackSpeed 1.0",
        "Set TypingSpeed 50ms",
        "",
        *vhs_run(env),
        *scene.tape_body,
    ]
    return "\n".join(lines) + "\n"


def sh_quote(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def quote_vhs_path(path: Path) -> str:
    return '"' + str(path).replace("\\", "\\\\").replace('"', '\\"') + '"'


def replace_output_with_final_frame(frame_output: Path, output: Path) -> None:
    if frame_output.is_file():
        final_frame = frame_output
    elif frame_output.is_dir():
        text_frames = sorted(frame_output.glob("frame-text-*.png"))
        cursor_frames = sorted(frame_output.glob("frame-cursor-*.png"))
        frames = text_frames if text_frames else cursor_frames
        if not frames:
            raise SystemExit(f"VHS did not create PNG frames in {frame_output}")
        final_frame = frames[-1]
    else:
        raise SystemExit(f"VHS did not create expected output at {frame_output}")

    if output.is_dir():
        shutil.rmtree(output)
    elif output.exists():
        output.unlink()
    shutil.copy2(final_frame, output)
    output.chmod(0o644)


def render_scene(scene: Scene, tmpdir: Path, dry_run: bool) -> None:
    workspace = tmpdir / scene.name / "workspace"
    home = tmpdir / scene.name / "home"
    tape_dir = tmpdir / scene.name / "tapes"
    output = SCREENSHOT_DIR / scene.output
    frame_output = tape_dir / f"{scene.name}-frames.png"

    copy_fixture_workspace(workspace)
    write_config(home, scene.theme)
    tape_dir.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)

    tape = tape_for_scene(scene, workspace, home, frame_output)
    tape_path = tape_dir / f"{scene.name}.tape"
    tape_path.write_text(tape, encoding="utf-8")

    if dry_run:
        print(f"--- {scene.name} -> {output.relative_to(REPO_ROOT)}")
        print(tape)
        return

    print(f"Generating {output.relative_to(REPO_ROOT)}")
    run_checked(["vhs", str(tape_path)])
    replace_output_with_final_frame(frame_output, output)


def main() -> int:
    args = parse_args()
    scenes = selected_scenes(args.scenes)
    if args.skip_lsp:
        scenes = [scene for scene in scenes if not scene.lsp_scene]
        if not scenes:
            raise SystemExit("No scenes selected after --skip-lsp")

    if args.list:
        print_scene_list()
        return 0

    if args.dry_run:
        with tempfile.TemporaryDirectory(prefix="rotide-docs-media-") as tmp:
            tmpdir = Path(tmp)
            for scene in scenes:
                render_scene(scene, tmpdir, dry_run=True)
        return 0

    preflight(scenes)
    build_rotide(args.no_build)

    with tempfile.TemporaryDirectory(prefix="rotide-docs-media-") as tmp:
        tmpdir = Path(tmp)
        for scene in scenes:
            render_scene(scene, tmpdir, dry_run=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
