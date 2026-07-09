# LSP Framing Fuzzer

Fuzzes `editorLspReadFrame` in
[`src/language/lsp_framing.c`](../../../src/language/lsp_framing.c): the
`Content-Length:` framed JSON-RPC parser for LSP servers.

```
make fuzz-lsp-smoke   # bounded; default 5000 runs (~1s locally, what CI runs per PR)
make fuzz-lsp         # indefinite; what nightly should run
```

Use `FUZZ_LSP_SMOKE_RUNS=N` to change the bounded run. Seeds live in `corpus/`;
add minimized crashes or new framing edge cases there, plus a regression in
[`tests/test_lsp_framing.c`](../../test_lsp_framing.c). This harness does not
cover JSON payload decoding, real-pipe partial reads, or response dispatch.
