# C DAP Fixture

Small multi-file C debuggee for manual DAP checks: breakpoints, cross-file
stepping, recursion, locals/globals/statics, pointers/structs, threads, and
output events.

```bash
make -C tests/dap/supported/c
```

Output: `tests/dap/supported/c/dap_sample.out`. Suggested project
`.rotide.toml` launch:

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

`args[0]` selects `branch-a`, `branch-b`, `zero-div`, or the default path.
Search for `DAP_BP_*` comments to set known breakpoints across translation
units. Stepping into libc usually has no local source and should continue
without editor errors.
