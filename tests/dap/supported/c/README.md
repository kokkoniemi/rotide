# C DAP Fixture

This fixture is a small **multi-file** C program shaped for broad debugger
coverage. The work is split across translation units so you can exercise
multi-file debugging: cross-file step-into, breakpoints in several files at
once, and stack frames that span files.

Coverage:

- function breakpoints and line breakpoints
- step into / over / out, including **across files**
- nested stack frames and recursion (`main.c` -> `numeric.c`)
- locals, globals, statics, arrays, pointers, structs, and string values
- branch-dependent execution paths
- thread list and per-thread state
- runtime output events in the DAP output pane

## Files

| File        | Contents                                                              |
| ----------- | --------------------------------------------------------------------- |
| `main.c`    | `main`, run-mode dispatch, globals/statics, branch paths              |
| `runmode.c` | `parse_mode` and `enum run_mode` (`runmode.h`)                        |
| `items.c`   | linked-list `struct item` helpers, `pointer_walk`, `mutate_item`      |
| `numeric.c` | `factorial_recursive`, `fibonacci_iterative`, `sum_array`, dividers   |
| `worker.c`  | `worker_main` thread body and `pause_ms`                              |

## Build

```bash
make -C tests/dap/supported/c
```

Binary output:

- `tests/dap/supported/c/dap_sample.out`

## Suggested launch config

Use this in project `.rotide.toml`:

```toml
[dap.launch.c_dap_fixture]
name = "C DAP Fixture"
adapter = "c"
request = "launch"
program = "${workspaceFolder}/tests/dap/supported/c/dap_sample.out"
cwd = "${workspaceFolder}"
args = ["branch-a"]
stopOnEntry = false
console = "terminal"
```

`args[0]` selects the run mode: `branch-a`, `branch-b`, `zero-div`, or anything
else (including omitted) for the normal path.

## Breakpoint checklist

Set breakpoints by searching for these marker comments. Each lives in the file
named beside it so you can confirm breakpoints resolve across translation units:

- `DAP_BP_MAIN_ENTRY` — `main.c`
- `DAP_BP_FACTORIAL_RECURSE` — `numeric.c`
- `DAP_BP_FIB_LOOP` — `numeric.c`
- `DAP_BP_MUTATE_ITEM` — `items.c`
- `DAP_BP_WORKER_LOOP` — `worker.c`
- `DAP_BP_BRANCH_A` — `main.c`
- `DAP_BP_BRANCH_B` — `main.c`
- `DAP_BP_ZERO_DIVISION_PATH` — `main.c`
- `DAP_BP_BEFORE_EXIT` — `main.c`

Recommended multi-file flow:

1. Launch with `args = ["branch-a"]`, stop at `DAP_BP_MAIN_ENTRY` (`main.c`).
2. Step into `factorial_recursive` — the editor should follow into `numeric.c`,
   then step out back to `main.c`.
3. Step through the `fibonacci_iterative` loop in `numeric.c`; inspect `a`,
   `b`, `next`.
4. Inspect `items`, `items->next`, and `items->next->next` (struct/pointer
   walking defined in `items.c`).
5. Continue to the worker loop (`worker.c`) and inspect thread state
   (`ctx.progress`, `ctx.checksum`).
6. Relaunch with `branch-b`, then `zero-div`, and confirm branch-specific
   breakpoints in `main.c` fire.
7. Inspect final `debug_sink` before process exit.

> Stepping into libc functions such as `printf` will not open source (glibc is
> usually built without local source). The debugger simply continues without
> error rather than trying to open a file that is not on disk.
