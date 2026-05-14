# C DAP Fixture

This fixture is a single C program shaped for broad debugger coverage:

- function breakpoints and line breakpoints
- step into / over / out
- nested stack frames and recursion
- locals, globals, statics, arrays, pointers, structs, and string values
- branch-dependent execution paths
- thread list and per-thread state
- runtime output events in the DAP output pane

## Build

```bash
make -C tests/dap/supported/c
```

Binary output:

- `tests/dap/supported/c/rotide_dap_sample`

## Suggested launch config

Use this in project `.rotide.toml`:

```toml
[dap.launch.c_dap_fixture]
name = "C DAP Fixture"
adapter = "c"
request = "launch"
program = "${workspaceFolder}/tests/dap/supported/c/rotide_dap_sample"
cwd = "${workspaceFolder}"
args = ["branch-a"]
stopOnEntry = false
console = "terminal"
```

## Breakpoint checklist

Set breakpoints by searching for these marker comments in
`rotide_dap_sample.c`:

- `DAP_BP_MAIN_ENTRY`
- `DAP_BP_FACTORIAL_RECURSE`
- `DAP_BP_FIB_LOOP`
- `DAP_BP_MUTATE_ITEM`
- `DAP_BP_WORKER_LOOP`
- `DAP_BP_BRANCH_A`
- `DAP_BP_BRANCH_B`
- `DAP_BP_ZERO_DIVISION_PATH`
- `DAP_BP_BEFORE_EXIT`

Recommended manual flow:

1. Launch with `args = ["branch-a"]`, stop at `DAP_BP_MAIN_ENTRY`.
2. Step into recursion (`factorial_recursive`) and out.
3. Step through `fibonacci_iterative` loop and inspect `a`, `b`, `next`.
4. Inspect `items`, `items->next`, and `items->next->next` in locals/watch.
5. Continue to worker code and inspect thread state (`ctx.progress`, `ctx.checksum`).
6. Relaunch with `branch-b`, then `zero-div`, and confirm branch-specific breakpoints fire.
7. Inspect final `debug_sink` before process exit.

