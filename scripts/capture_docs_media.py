#!/usr/bin/env python3
"""Generate RotIDE documentation screenshots with VHS.

The script creates a temporary HOME and project workspace, writes a small
RotIDE config for each scene, writes a temporary VHS tape, and asks VHS to
render a PNG into docs/media/screenshots/.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCREENSHOT_DIR = REPO_ROOT / "docs" / "media" / "screenshots"
ROTIDE_BIN = REPO_ROOT / "build" / "rotide"
CAPTURE_WIDTH = 2560
CAPTURE_HEIGHT = 1520
CAPTURE_FONT_SIZE = 32
CAPTURE_FONT_FAMILY = "FiraMono Nerd Font Mono"
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
nerd_fonts = {nerd_fonts}
auto_indent = true
indent_style = "tabs"
indent_width = 4

[input]
system = "{input_system}"

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
    open_file: str = "src/text/text_buffer.c"
    clangd_enabled: bool = False
    javascript_enabled: bool = False
    full_config: bool = False
    git_repo: bool = False
    dap_session: bool = False
    terminal_shell: bool = False
    nerd_fonts: bool = True
    launch_in_workspace_root: bool = True
    input_system: str = "vim"


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
        name="editor-source-cua",
        output="editor-source-cua.png",
        description="RotIDE editing C source with the CUA input system enabled.",
        tape_body=(
            "Sleep 1800ms",
            "Ctrl+L",
            "Sleep 600ms",
        ),
        input_system="cua",
    ),
    Scene(
        name="editor-source-no-drawer",
        output="editor-source-no-drawer.png",
        description="Editor with the project drawer hidden, leaving the full width for source.",
        tape_body=(
            "Sleep 1800ms",
            vhs_type(" e"),
            "Sleep 1000ms",
        ),
    ),
    Scene(
        name="drawer-tree",
        output="drawer-tree.png",
        description="Project drawer navigation over a full RotIDE fixture tree.",
        tape_body=(
            # The drawer opens in tree mode but focus starts in the editor.
            # `<leader>e` toggles the tree drawer, so the first press collapses
            # it and the second reopens it with focus moved into the drawer.
            "Sleep 1500ms",
            vhs_type(" e"),
            "Sleep 300ms",
            vhs_type(" e"),
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
            vhs_type(" f"),
            "Sleep 300ms",
            vhs_type("UINT32_MAX"),
            "Sleep 1200ms",
            "Down",
            "Sleep 700ms",
        ),
    ),
    Scene(
        name="lsp-autocomplete-c",
        output="lsp-autocomplete-c.png",
        description="C autocomplete popup powered by clangd suggesting struct members.",
        tape_body=(
            "Sleep 5000ms",
            vhs_type("8j7la"),
            "Sleep 300ms",
            vhs_type("."),
            "Sleep 5000ms",
        ),
        lsp_scene=True,
        required_commands=("clangd",),
        open_file="autocomplete.c",
        clangd_enabled=True,
        launch_in_workspace_root=False,
    ),
    Scene(
        name="lsp-clang-problems",
        output="lsp-clang-problems.png",
        description="Clangd diagnostics collected through LSP and shown in the Problems drawer.",
        tape_body=(
            "Sleep 5000ms",
            vhs_type(" l"),
            "Sleep 1800ms",
        ),
        lsp_scene=True,
        required_commands=("clangd",),
        open_file="clang-problems.c",
        clangd_enabled=True,
        launch_in_workspace_root=False,
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
        launch_in_workspace_root=False,
    ),
    Scene(
        name="main-menu",
        output="main-menu.png",
        description="Main menu drawer with grouped actions for Find, File, Tabs, Edit, and View.",
        tape_body=(
            "Sleep 1500ms",
            vhs_type(" m"),
            "Sleep 600ms",
        ),
    ),
    Scene(
        name="git-changes",
        output="git-changes.png",
        description="Git changes drawer opening a generated diff tab for a modified RotIDE source file.",
        tape_body=(
            # Diff tabs default to the changed-hunks view, so the appended
            # modification is on screen; the old `z` press toggled to the
            # whole-file view and pushed the change off the bottom.
            "Sleep 2400ms",
            vhs_type(" g"),
            "Sleep 1000ms",
            "Down 13",
            "Enter",
            "Sleep 1200ms",
        ),
        git_repo=True,
    ),
    Scene(
        name="git-history",
        output="git-history.png",
        description="Generated Git history view with branch and tag decorations.",
        tape_body=(
            "Sleep 1800ms",
            vhs_type(":"),
            vhs_type("git log"),
            "Enter",
            "Sleep 1400ms",
        ),
        git_repo=True,
    ),
    Scene(
        name="split-panes-terminal",
        output="split-panes-terminal.png",
        description="Nested editor splits with a deterministic terminal session in the lower pane.",
        tape_body=(
            "Sleep 1800ms",
            vhs_type(":"),
            vhs_type("vsplit docs/capture-notes.md"),
            "Enter",
            "Sleep 800ms",
            vhs_type(":"),
            vhs_type("term"),
            "Enter",
            "Sleep 1400ms",
        ),
        terminal_shell=True,
    ),
    Scene(
        name="dap-paused-session",
        output="dap-paused-session.png",
        description="Paused DAP session with source location, locals, stack, console, and controls.",
        tape_body=(
            "Sleep 1800ms",
            vhs_type(" d"),
            "Sleep 500ms",
            "Down 3",
            "Enter",
            "Sleep 3000ms",
            "Ctrl+L",
            "Sleep 600ms",
        ),
        open_file="debug-demo.c",
        dap_session=True,
        launch_in_workspace_root=False,
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


def run_checked_quiet(command: list[str], cwd: Path = REPO_ROOT,
                      env: dict[str, str] | None = None) -> None:
    subprocess.run(
        command,
        cwd=cwd,
        check=True,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def build_rotide(no_build: bool) -> None:
    if no_build:
        if not ROTIDE_BIN.exists():
            raise SystemExit("./build/rotide does not exist; run make or omit --no-build")
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
    (workspace / "autocomplete.c").write_text(
        "struct app_state {\n"
        "    int request_count;\n"
        "    int response_count;\n"
        "    const char *session_id;\n"
        "};\n"
        "\n"
        "int main(void) {\n"
        "    struct app_state app;\n"
        "    app\n"
        "}\n",
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
    (workspace / "debug-demo.c").write_text(
        "#include <stdio.h>\n"
        "\n"
        "static int sum_values(const int *values, int count) {\n"
        "\tint total = 0;\n"
        "\tfor (int i = 0; i < count; i++) {\n"
        "\t\ttotal += values[i];\n"
        "\t}\n"
        "\treturn total;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "\tint values[] = {3, 5, 8, 13, 21};\n"
        "\tint total = sum_values(values, 5);\n"
        "\tprintf(\"total: %d\\n\", total);\n"
        "\treturn 0;\n"
        "}\n",
        encoding="utf-8",
    )


def write_terminal_shell(workspace: Path) -> Path:
    path = workspace / "docs-terminal-shell.sh"
    path.write_text(
        "#!/bin/sh\n"
        "printf '\\033[1;36mRotIDE workspace terminal\\033[0m\\n\\n'\n"
        "printf '  branch   main\\n'\n"
        "printf '  build    clean\\n'\n"
        "printf '  tests    1847 passed\\n'\n"
        "printf '  cwd      %s\\n\\n' \"$PWD\"\n"
        "printf '\\033[32mready\\033[0m $ '\n"
        "while IFS= read -r line; do\n"
        "\tprintf 'command: %s\\n' \"$line\"\n"
        "\tprintf '\\033[32mready\\033[0m $ '\n"
        "done\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def write_mock_dap_adapter(workspace: Path) -> Path:
    path = workspace / "mock-dap.py"
    path.write_text(
        """#!/usr/bin/env python3
import json
import sys

source_path = sys.argv[1]
seq = 100


def send(message):
    global seq
    message.setdefault("seq", seq)
    seq += 1
    payload = json.dumps(message, separators=(",", ":")).encode()
    sys.stdout.buffer.write(f"Content-Length: {len(payload)}\\r\\n\\r\\n".encode() + payload)
    sys.stdout.buffer.flush()


def response(request, body=None):
    message = {"type": "response", "request_seq": request["seq"],
               "success": True, "command": request["command"]}
    if body is not None:
        message["body"] = body
    send(message)


while True:
    content_length = None
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            raise SystemExit(0)
        if line in (b"\\r\\n", b"\\n"):
            break
        if line.lower().startswith(b"content-length:"):
            content_length = int(line.split(b":", 1)[1])
    if content_length is None:
        continue
    request = json.loads(sys.stdin.buffer.read(content_length))
    command = request.get("command")
    arguments = request.get("arguments", {})

    if command == "initialize":
        response(request, {"supportsConfigurationDoneRequest": True})
    elif command == "launch":
        response(request)
        send({"type": "event", "event": "initialized"})
    elif command == "setBreakpoints":
        response(request, {"breakpoints": []})
    elif command == "configurationDone":
        response(request)
        send({"type": "event", "event": "output",
              "body": {"category": "console", "output": "Demo adapter attached\\n"}})
        send({"type": "event", "event": "stopped",
              "body": {"reason": "breakpoint", "threadId": 1}})
    elif command == "threads":
        response(request, {"threads": [{"id": 1, "name": "main thread"},
                                         {"id": 2, "name": "worker thread"}]})
    elif command == "stackTrace":
        response(request, {"stackFrames": [
            {"id": 101, "name": "sum_values", "line": 6, "column": 3,
             "source": {"name": "debug-demo.c", "path": source_path}},
            {"id": 102, "name": "main", "line": 13, "column": 14,
             "source": {"name": "debug-demo.c", "path": source_path}}]})
    elif command == "scopes":
        response(request, {"scopes": [
            {"name": "Arguments", "variablesReference": 1},
            {"name": "Locals", "variablesReference": 2}]})
    elif command == "variables":
        ref = arguments.get("variablesReference")
        variables = ([
            {"name": "values", "value": "0x7fffffffe120", "type": "const int *",
             "variablesReference": 0},
            {"name": "count", "value": "5", "type": "int", "variablesReference": 0},
        ] if ref == 1 else [
            {"name": "i", "value": "2", "type": "int", "variablesReference": 0},
            {"name": "total", "value": "16", "type": "int", "variablesReference": 0},
            {"name": "values[2]", "value": "8", "type": "int", "variablesReference": 0},
        ])
        response(request, {"variables": variables})
    else:
        response(request)
""",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


# Deterministic commit history for the Git log screenshot. Each entry appends a
# line to docs/CHANGELOG.md and lands as its own commit with a fixed date so the
# rendered log stays reproducible.
DOCS_MEDIA_HISTORY: tuple[tuple[str, str, str], ...] = (
    ("2026-06-12T09:15:00+03:00", "Add text buffer summary cache",
     "Cache per-line summaries so large buffers scroll without recomputing widths."),
    ("2026-06-15T11:40:00+03:00", "Wire incremental tree-sitter parsing",
     "Reparse only edited ranges to keep highlighting responsive on big files."),
    ("2026-06-17T16:05:00+03:00", "Add project-wide text search drawer",
     "Search the whole workspace and preview matches inline."),
    ("2026-06-19T10:20:00+03:00", "Surface LSP problems and symbols",
     "Collect clangd diagnostics and document symbols in the drawer."),
    ("2026-06-22T14:50:00+03:00", "Add split-pane and terminal layout",
     "Nest editor splits and embed shell sessions in a pane."),
    ("2026-06-24T09:35:00+03:00", "Add DAP debugger controls",
     "Step, pause, and inspect locals over the Debug Adapter Protocol."),
    ("2026-06-26T17:10:00+03:00", "Implement Git changes drawer",
     "Stage, unstage, and open diffs for worktree changes."),
    ("2026-06-28T12:00:00+03:00", "Add Git history log view",
     "Render the commit graph with branch and tag decorations."),
    ("2026-06-30T15:25:00+03:00", "Polish built-in theme palette",
     "Tune contrast across the bundled light and dark themes."),
)


def git_commit_at(workspace: Path, message: str, date: str) -> None:
    env = os.environ.copy()
    env.update({"GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date})
    run_checked_quiet(["git", "commit", "-m", message], cwd=workspace, env=env)


def prepare_git_repo(workspace: Path) -> None:
    run_checked_quiet(["git", "init", "-b", "main"], cwd=workspace)
    run_checked_quiet(["git", "config", "user.email", "docs-media@example.invalid"], cwd=workspace)
    run_checked_quiet(["git", "config", "user.name", "RotIDE Docs Media"], cwd=workspace)
    run_checked_quiet(["git", "add", "."], cwd=workspace)
    git_commit_at(workspace, "Initial docs media fixture", "2026-06-10T09:00:00+03:00")

    changelog = workspace / "docs" / "CHANGELOG.md"
    changelog.write_text(
        "# Changelog\n\nNotable changes to the RotIDE workspace demo.\n",
        encoding="utf-8",
    )
    run_checked_quiet(["git", "add", "docs/CHANGELOG.md"], cwd=workspace)
    git_commit_at(workspace, "Start workspace changelog", "2026-06-11T08:30:00+03:00")

    for date, message, note in DOCS_MEDIA_HISTORY:
        with changelog.open("a", encoding="utf-8") as f:
            f.write(f"- {message}: {note}\n")
        run_checked_quiet(["git", "add", "docs/CHANGELOG.md"], cwd=workspace)
        git_commit_at(workspace, message, date)

    (workspace / "docs" / "release-notes.md").write_text(
        "# Release Notes\n\nAdd terminal panes, Git views, and debugger controls.\n",
        encoding="utf-8",
    )
    run_checked_quiet(["git", "add", "docs/release-notes.md"], cwd=workspace)
    git_commit_at(workspace, "Document workspace tooling", "2026-07-01T14:30:00+03:00")
    run_checked_quiet(["git", "tag", "v0.4-demo"], cwd=workspace)
    run_checked_quiet(["git", "branch", "feature/debug-ui", "HEAD~1"], cwd=workspace)
    with (workspace / "src" / "text" / "text_buffer.c").open("a", encoding="utf-8") as f:
        f.write("\n/* docs-media: modified line for the git screenshot */\n")
    run_checked_quiet(["git", "add", "src/text/text_buffer.c"], cwd=workspace)
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


def write_config(home: Path, workspace: Path, scene: Scene) -> Path:
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
        config_text = COMMON_CONFIG_TEMPLATE.format(
            theme=scene.theme,
            clangd_enabled=str(scene.clangd_enabled).lower(),
            javascript_enabled=str(scene.javascript_enabled).lower(),
            nerd_fonts=str(scene.nerd_fonts).lower(),
            input_system=scene.input_system,
        )
        if scene.dap_session:
            adapter = write_mock_dap_adapter(workspace)
            source = workspace / scene.open_file
            config_text += (
                "\n"
                "[dap.adapters]\n"
                f'docs = "python3 {adapter} {source}"\n'
            )
            (workspace / ".rotide.toml").write_text(
                "[dap.launch.docs_demo]\n"
                'name = "Debug Demo"\n'
                'adapter = "docs"\n'
                'request = "launch"\n'
                f'program = "{source}"\n'
                f'cwd = "{workspace}"\n',
                encoding="utf-8",
            )
        path.write_text(config_text, encoding="utf-8")
    return path


def scene_open_path(scene: Scene, workspace: Path, home: Path) -> Path:
    if scene.open_file.startswith(".home/"):
        return home / scene.open_file[len(".home/"):]
    return workspace / scene.open_file


def vhs_open_file_via_finder(filename: str, input_system: str) -> tuple[str, ...]:
    open_finder = ("Ctrl+P",) if input_system == "cua" else (vhs_type(" p"),)
    return (
        "Sleep 1200ms",
        *open_finder,
        "Sleep 400ms",
        vhs_type(filename),
        "Sleep 600ms",
        "Enter",
        "Sleep 600ms",
    )


def tape_for_scene(scene: Scene, workspace: Path, home: Path, frame_output: Path) -> str:
    shell_env = ""
    if scene.terminal_shell:
        shell_env = f'SHELL={sh_quote(str(workspace / "docs-terminal-shell.sh"))} '
    if scene.launch_in_workspace_root:
        launch_cmd = (
            f'cd {sh_quote(str(workspace))} && '
            f'HOME={sh_quote(str(home))} '
            f'{shell_env}'
            "TERM=xterm-256color "
            f'{sh_quote(str(ROTIDE_BIN))}'
        )
        finder_preamble: tuple[str, ...] = vhs_open_file_via_finder(
            scene.open_file, scene.input_system)
    else:
        launch_cmd = (
            f'cd {sh_quote(str(workspace))} && '
            f'HOME={sh_quote(str(home))} '
            f'{shell_env}'
            "TERM=xterm-256color "
            f'{sh_quote(str(ROTIDE_BIN))} {sh_quote(str(scene_open_path(scene, workspace, home)))}'
        )
        finder_preamble = ()
    lines = [
        f"Output {quote_vhs_path(frame_output)}",
        "Set Shell bash",
        f'Set FontFamily "{CAPTURE_FONT_FAMILY}"',
        f"Set FontSize {CAPTURE_FONT_SIZE}",
        f"Set Width {CAPTURE_WIDTH}",
        f"Set Height {CAPTURE_HEIGHT}",
        "Set Framerate 15",
        "Set PlaybackSpeed 1.0",
        "Set TypingSpeed 50ms",
        "",
        *vhs_run(launch_cmd),
        *finder_preamble,
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
    if scene.terminal_shell:
        write_terminal_shell(workspace)
    write_config(home, workspace, scene)
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
