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
CAPTURE_WIDTH = 2560
CAPTURE_HEIGHT = 1520
CAPTURE_FONT_SIZE = 32
DEFAULT_CAPTURE_THEME = "github-dark"

BUILTIN_THEMES: tuple[tuple[str, str], ...] = (
    ("terminal", "Terminal ANSI"),
    ("a11y-dark", "A11y Dark"),
    ("a11y-light", "A11y Light"),
    ("acme", "Acme"),
    ("silentium", "Silentium"),
    ("256noir", "256noir"),
    ("github-light", "GitHub Light"),
    ("github-dark", "GitHub Dark"),
    ("modus-operandi", "Modus Operandi"),
    ("modus-operandi-tinted", "Modus Operandi Tinted"),
    ("modus-vivendi", "Modus Vivendi"),
    ("modus-vivendi-tinted", "Modus Vivendi Tinted"),
    ("molokai", "Molokai"),
    ("kanagawa-wave", "Kanagawa Wave"),
    ("kanagawa-dragon", "Kanagawa Dragon"),
    ("kanagawa-lotus", "Kanagawa Lotus"),
)

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
clangd_enabled = {clangd_enabled}
html_enabled = false
css_enabled = false
json_enabled = false
javascript_enabled = {javascript_enabled}
eslint_enabled = false
clangd_command = "clangd"
javascript_command = "typescript-language-server --stdio"
autocomplete_max_items = 8

[keymap]
project_search = "ctrl+t"
lsp_drawer = "ctrl+u"
git_drawer = "ctrl+k"
"""


@dataclass(frozen=True)
class Scene:
    name: str
    output: str
    description: str
    tape_body: tuple[str, ...]
    theme: str = DEFAULT_CAPTURE_THEME
    lsp_scene: bool = False
    required_commands: tuple[str, ...] = ()
    open_file: str = "src/rotide.c"
    clangd_enabled: bool = False
    javascript_enabled: bool = False
    full_config: bool = False
    git_repo: bool = False


def vhs_type(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'Type "{escaped}"'


def vhs_run(command: str) -> tuple[str, ...]:
    return (
        vhs_type(command),
        "Enter",
    )


BASE_SCENES: tuple[Scene, ...] = (
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
        description="Project drawer navigation over a full RotIDE fixture tree.",
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
        javascript_enabled=True,
    ),
    Scene(
        name="lsp-clang-problems",
        output="lsp-clang-problems.png",
        description="Clangd diagnostics collected through LSP and shown in the Problems drawer.",
        tape_body=(
            "Sleep 5000ms",
            "Ctrl+U",
            "Sleep 1800ms",
        ),
        lsp_scene=True,
        required_commands=("clangd",),
        open_file="clang-problems.c",
        clangd_enabled=True,
    ),
    Scene(
        name="settings-config",
        output="settings-config.png",
        description="The full generated global config file, including editor, theme, LSP, and keymap settings.",
        tape_body=(
            "Sleep 1600ms",
        ),
        open_file=".home/.rotide/config.toml",
        full_config=True,
    ),
    Scene(
        name="git-changes",
        output="git-changes.png",
        description="Git changes drawer opening a generated diff tab for a modified RotIDE source file.",
        tape_body=(
            "Sleep 2400ms",
            "Ctrl+K",
            "Sleep 1000ms",
            "Down 3",
            "Enter",
            "Sleep 1400ms",
        ),
        git_repo=True,
    ),
)

THEME_SCENES: tuple[Scene, ...] = tuple(
    Scene(
        name=f"theme-{theme_name}",
        output=f"theme-{theme_name}.png",
        description=f"{theme_label} theme applied to the same RotIDE source fixture.",
        tape_body=(
            "Sleep 1600ms",
        ),
        theme=theme_name,
    )
    for theme_name, theme_label in BUILTIN_THEMES
)

SCENES: tuple[Scene, ...] = BASE_SCENES + THEME_SCENES


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
    if "clangd" in missing:
        print("Install clangd with your system package manager.", file=sys.stderr)
    raise SystemExit(1)


def run_checked(command: list[str], cwd: Path = REPO_ROOT) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def run_checked_quiet(command: list[str], cwd: Path = REPO_ROOT) -> None:
    subprocess.run(
        command,
        cwd=cwd,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def build_rotide(no_build: bool) -> None:
    if no_build:
        if not ROTIDE_BIN.exists():
            raise SystemExit("./rotide does not exist; run make or omit --no-build")
        return
    run_checked(["make"])


def repo_files_for_fixture() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=REPO_ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    files = result.stdout.decode("utf-8").split("\0")
    skipped_prefixes = (
        "docs/media/screenshots/",
        "vendor/tree_sitter/",
    )
    return [
        rel for rel in files
        if rel and not rel.endswith(".png") and
        not any(rel.startswith(prefix) for prefix in skipped_prefixes)
    ]


def copy_fixture_workspace(workspace: Path) -> None:
    for rel in repo_files_for_fixture():
        src = REPO_ROOT / rel
        dst = workspace / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    (workspace / "docs").mkdir(parents=True, exist_ok=True)
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
    (workspace / "clang-problems.c").write_text(
        "#include <stdio.h>\n"
        "\n"
        "int main(void) {\n"
        "\tprintf(\"%d\\n\", missing_symbol);\n"
        "\treturn 0;\n"
        "}\n",
        encoding="utf-8",
    )


def prepare_git_repo(workspace: Path) -> None:
    run_checked_quiet(["git", "init", "-b", "main"], cwd=workspace)
    run_checked_quiet(["git", "config", "user.email", "docs-media@example.invalid"], cwd=workspace)
    run_checked_quiet(["git", "config", "user.name", "RotIDE Docs Media"], cwd=workspace)
    run_checked_quiet(["git", "add", "."], cwd=workspace)
    run_checked_quiet(["git", "commit", "-m", "Initial docs media fixture"], cwd=workspace)
    with (workspace / "src" / "rotide.c").open("a", encoding="utf-8") as f:
        f.write("\n/* docs-media: modified line for the git screenshot */\n")
    run_checked_quiet(["git", "add", "src/rotide.c"], cwd=workspace)
    (workspace / "docs" / "capture-notes.md").write_text(
        "# Capture Notes\n\n"
        "This modified file makes the Git drawer show realistic workspace changes.\n",
        encoding="utf-8",
    )
    (workspace / "docs" / "untracked-note.md").write_text(
        "# Untracked Note\n\n"
        "This untracked file gives the Git drawer another status group.\n",
        encoding="utf-8",
    )


def write_config(home: Path, scene: Scene) -> Path:
    config_dir = home / ".rotide"
    config_dir.mkdir(parents=True, exist_ok=True)
    path = config_dir / "config.toml"
    if scene.full_config:
        config_text = (REPO_ROOT / "config.toml.example").read_text(encoding="utf-8")
        default_theme_selector = '[theme]\nname = "terminal"'
        if default_theme_selector not in config_text:
            raise SystemExit("config.toml.example no longer has the expected [theme] default")
        config_text = config_text.replace(default_theme_selector,
                f'[theme]\nname = "{scene.theme}"', 1)
        path.write_text(config_text, encoding="utf-8")
    else:
        path.write_text(COMMON_CONFIG_TEMPLATE.format(
            theme=scene.theme,
            clangd_enabled=str(scene.clangd_enabled).lower(),
            javascript_enabled=str(scene.javascript_enabled).lower(),
        ), encoding="utf-8")
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
        f"Set FontSize {CAPTURE_FONT_SIZE}",
        f"Set Width {CAPTURE_WIDTH}",
        f"Set Height {CAPTURE_HEIGHT}",
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
    if scene.git_repo:
        prepare_git_repo(workspace)
    write_config(home, scene)
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
