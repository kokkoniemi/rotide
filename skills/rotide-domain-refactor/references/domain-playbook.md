# Rotide Domain Playbook

## Purpose

This playbook is for refactoring RotIDE toward clearer domain ownership without turning the codebase into abstract architecture theater.

The main question is:

`Which part of the editor should own this behavior and state?`

## What “domain” means here

In this codebase, a domain is a cohesive area of behavior with:
- a small set of owned invariants
- a natural state boundary
- a natural public API
- tests that mostly talk about the same behavior cluster

Typical RotIDE domains:
- document storage
- editing/history/selection
- rendering/output
- input/actions/prompts
- tabs/workspace management
- drawer/project navigation
- save/recovery
- syntax/highlighting
- LSP/navigation
- low-level platform support

## Refactor method

### 1. State the domain and invariants

Examples:
- document domain owns canonical text and offset mapping
- rendering domain owns terminal output formatting, not editing semantics
- drawer domain owns tree state and selection, not file editing

If you cannot say what invariants the domain owns, the split is probably not ready.

### 2. Identify the current leakage

Look for:
- unrelated functions in the same `.c`
- headers exporting helpers that are only used by tests or one module
- duplicated path/string/helper logic
- modules that mutate another domain’s state directly when they should call an API

Useful questions:
- who owns this state?
- who is allowed to mutate it?
- who only needs to observe it?

### 3. Choose the minimum useful boundary

Prefer:
- one extraction with obvious ownership
- one new header with a small API
- one narrow reason for the move

Avoid:
- splitting into tiny files with unclear boundaries
- moving code without reducing coupling
- renaming everything at once

### 4. Decide public vs private surface

Public API:
- needed by another real domain
- stable enough to be named intentionally

Private helper:
- only supports one implementation file
- leaks internals if exported

Test-only API:
- keep out of production-facing headers when possible

### 5. Remove dead weight during the move

Good cleanup targets:
- duplicate helpers
- stale forward declarations
- old wrappers with no callers
- oversized headers exporting unrelated functions

Do not keep obsolete code behind disabled blocks after the refactor lands.

## C-specific boundary rules

Because this is C, domain modeling is mostly about modules and ownership:
- `.c` file = implementation owner
- `.h` file = minimal contract
- `static` = private unless a real cross-domain dependency exists
- shared structs in `rotide.h` should stay only when they are truly global/editor-wide

Good signs:
- a caller includes fewer headers after the refactor
- fewer functions need forward declarations
- fewer modules know internal struct details they do not own

## Suggested current boundaries

These are strong candidates, not rigid law:

### Document domain

Owns:
- `src/text/document.c`
- `src/text/rope.c`
- `src/text/row.c`, `src/text/utf8.c`
- offset/line mapping invariants

Should not own:
- rendering decisions
- prompt behavior

### Edit/history domain

Owns:
- edit application
- selection mutation
- clipboard mutation
- undo/redo grouping

Usually centered in:
- `src/editing/buffer_core.c`
- `src/editing/edit.c`
- `src/editing/selection.c`
- `src/editing/history.c`

### Workspace domain

Owns:
- tabs
- preview tab policy
- task-log tab lifecycle

Usually centered in:
- `src/workspace/tabs.c`

### Drawer domain

Owns:
- tree nodes
- drawer selection/viewport/expand-collapse
- drawer-driven open/preview actions

Usually centered in:
- `src/workspace/drawer.c`

### Recovery/save domain

Owns:
- snapshot persistence/restore
- atomic save path helpers

Usually centered in:
- `src/workspace/recovery.c`
- `src/support/file_io.c`
- save-related pieces still in `src/editing/buffer_core.c`

### Input domain

Owns:
- prompts
- action dispatch
- keypress and mouse workflows

Usually centered in:
- `src/input/dispatch.c`

### Rendering domain

Owns:
- terminal frame generation
- tab bar/drawer/status rendering

Usually centered in:
- `src/render/screen.c`

### Syntax/LSP domains

Own independently:
- parse/highlight state
- LSP process lifecycle and requests

Usually centered in:
- `src/language/syntax.c`, `src/language/queries.c`, `src/language/languages.c`
- `src/language/lsp.c`, `src/language/lsp.h`, `src/language/lsp_internal.h`

## Decision heuristics

Move code when most answers point one way:
- it mutates one domain’s state more than any other
- its tests are about one behavior cluster
- its header users are specific and limited
- its name makes more sense in one module than everywhere else

Keep code where it is when:
- it is glue between two domains and still small/readable
- extracting it would create a fake abstraction
- the new header would just mirror existing globals without reducing coupling

## Validation checklist

After refactoring:
- `make`
- `make test`
- `make test-sanitize` for memory-sensitive or build-graph changes

Manual review questions:
- is ownership clearer?
- did public surface shrink?
- did dead weight actually get removed?
- is the result easier for a human to navigate?
