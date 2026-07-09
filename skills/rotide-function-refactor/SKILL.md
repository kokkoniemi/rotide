---
name: rotide-function-refactor
description: Review and refactor a single C function (or small cluster) in RotIDE — find correctness bugs, smells, duplication, weak error handling, and stage safe, behavior-preserving changes before any design change.
---

# Rotide Function Refactor

Use this when the task targets a specific function, hot spot, or small cluster of related functions and the goal is to understand it deeply, then improve it without breaking behavior. Module/header/ownership splits stay with `rotide-maintainer`.

## First Inspect

Do not propose changes until you have read the surrounding code. Skipping this step produces wrong suggestions.

1. The target function in full, including every early return and cleanup path.
2. 2–5 neighboring functions in the same `.c` file — they reveal local conventions for errno, return codes, cleanup labels, allocation, and logging.
3. The public header that declares the function (if any) — contract, ownership words, `_out` parameters.
4. Direct callers (`rg -n 'funcname\b' src/ tests/`) — what they pass, what they expect on failure, whether they check the return.
5. Direct callees — especially anything that allocates, locks, mutates `E.*`, or touches `editorDocument`.
6. Related tests under `tests/` — these define the behavior contract you must preserve.
7. `git log -p -- <file>` for recent intent, only if the function looks like it has churned.

If the function is a key handler / command, trace the path: `enum editorAction` lookup → keymap → handler. If it touches text state, confirm what mutates `E.dirty`. If it touches LSP or syntax, confirm tab-local state assumptions.

## What to look for

Triage every finding as one of:

- **bug** — wrong output, UB, leak, use-after-free, missed cleanup on early return, ignored return, off-by-one, signed/unsigned mismatch, stale state after partial failure, broken invariant, lock/reentrancy/signal issue, integer overflow, buffer truncation.
- **smell** — long function, deeply nested branches, repeated conditions, duplicated logic across siblings, mixed abstraction levels, hidden globals, ambiguous ownership, state machine encoded as scattered conditionals, command-handler spaghetti, hard-to-test logic, asymmetric cleanup.
- **style** — naming, const-correctness, formatting that survived `make format`. Surface but do not over-invest.

Always separate the three. Bugs justify a fix even in a "just refactor" task; smells are the point of the refactor; style is opportunistic.

## Guardrails

- Preserve user-visible and test-visible behavior unless the task explicitly changes it. The fix for "this is confusing" is rarely "and also do X differently now".
- Do not invent project conventions. Infer them from neighbors or call them out as unknown. RotIDE uses errno-style failure in some layers, boolean returns in others, `goto cleanup` in others — match the local pattern.
- Cleanup-style `goto` to a label named `cleanup`, `done`, `err`, or `out` is idiomatic here. Do not flatten it just to avoid `goto`.
- `editorDocument` is canonical writable text state. `editorRow` is derived render/cache. Refactors must not move authority across that line.
- Text mutations update `E.dirty`; navigation/search/view changes do not. Do not change that during a refactor.
- Syntax and LSP state are tab-local. Do not refactor them into globals.
- Comment policy ([`AGENTS.md`](../../AGENTS.md)): omit comments that restate code, narrate control flow, or reference plan/task history. Keep only invariants, edge cases, and surprising-behavior notes.
- Test API ([`tests/editor_test_api.h`](../../tests/editor_test_api.h)) must not gain mutators that production code would not itself call. If a refactor would require one, refactor the production path instead.
- Naming: public (header-declared) `editorXxx`; file-local `<module>Xxx` per [`tools/module-prefixes.tsv`](../../tools/module-prefixes.tsv). New `.c` files must add an entry there.
- Cast intentionally-ignored return values to `(void)` to satisfy `cert-err33-c`.
- Prefer extracting a static helper when it removes duplication or isolates a clear sub-step. Do not extract a helper that is used once and only renames a block — that is noise.

## Stages

Work in stages and stop at the stage the task actually needs. Do not silently combine stages.

1. **Behavior-preserving cleanup.** Fix bugs found along the way, normalize cleanup paths, deduplicate, rename for clarity, extract a helper when it pays for itself. No contract change.
2. **Control-flow simplification.** Collapse nested branches, hoist invariants, replace scattered conditionals with an explicit state/dispatch table. Still no contract change.
3. **Contract / API change.** New parameter, new return shape, split function, moved ownership. Only when stages 1–2 left something the task still requires. Update callers and tests in the same change.

Tests required:
- Stage 1: existing tests must pass unchanged.
- Stage 2: same, plus a new test if a previously-undertested branch becomes load-bearing.
- Stage 3: callers and tests updated; add coverage for the new contract.

## Output

Default to a short, adaptive report. Do not pad with empty sections.

- **Context summary** — one paragraph: what the function does, who calls it, what its failure mode is, what convention it follows. Mark anything you could not determine as "unknown".
- **Findings** — flat list, each line: `[bug|smell|style] <location> — <what> — <why it matters> — <suggested fix>`. Order by severity, then by file order.
- **Plan** — which stage(s) you propose, in order, with the tests each stage needs.
- **Changes** — apply via the Edit tool, not as a pasted diff. If context is insufficient to change safely, say what is missing instead of guessing.

## Validation

- Always: `make`, then `make test`.
- Touched memory/UB/storage/save/recovery/syntax/LSP code: `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`.
- Renamed/added externally-visible names or new `.c` file: `make format-check` and `make lint-prefixes`.
- Significant restructuring: `make lint` (clang-tidy) — `-Werror` is on, treat warnings as blockers.

## References

- [`AGENTS.md`](../../AGENTS.md) — project-wide non-negotiables, style, comment policy, validation.
- [`tools/module-prefixes.tsv`](../../tools/module-prefixes.tsv) — naming for static helpers.
