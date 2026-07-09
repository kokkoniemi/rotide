# DAP Framing Fuzzer

Fuzzes `editorDapClientReadFrame` in
[`src/debug/dap_client.c`](../../../src/debug/dap_client.c): the
`Content-Length:` framed JSON parser for debug adapters.

```
make fuzz-dap-smoke   # bounded; default 5000 runs (~1s locally, what CI runs per PR)
make fuzz-dap         # indefinite; what nightly should run
```

Use `FUZZ_DAP_SMOKE_RUNS=N` to change the bounded run. Seeds live in `corpus/`;
add minimized crashes or new framing edge cases there, plus a regression in
[`tests/test_dap_framing.c`](../../test_dap_framing.c). This harness does not
cover JSON decoding or request/event dispatch.
