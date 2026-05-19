# DAP framing fuzzer

libFuzzer harness over [`src/debug/dap_client.c`](../../../src/debug/dap_client.c) —
the `Content-Length:`-framed JSON parser that talks to debug adapters.
Same wire format as LSP; covered by its own harness because the DAP
parser is a duplicate of the LSP one and duplicate parsers tend to
drift.

## Run

```
make fuzz-dap-smoke   # bounded; default 5000 runs (~1s locally, what CI runs per PR)
make fuzz-dap         # indefinite; what nightly should run
```

Override the smoke iteration count with `FUZZ_DAP_SMOKE_RUNS=N`. The
smoke target stages the corpus into a tempdir so libFuzzer's mutations
don't accrete back into the committed seed set.

## Boundary it exercises

`editorDapClientReadFrame` is the read side of the DAP transport.
Untrusted bytes from the spawned adapter process flow through:

- header byte-by-byte read loop bounded by `ROTIDE_DAP_MAX_HEADER_BYTES`,
- `Content-Length:` digit parsing (overflow-safe against size_t wrap),
- `ROTIDE_DAP_MAX_PAYLOAD_BYTES` ceiling so a malicious or buggy
  adapter cannot coax the client into a multi-gigabyte malloc.

What it does **not** cover: JSON decoding (handled by `dap.c`) or the
event/request dispatch downstream of `editorDapClientReadFrame`. Those
are exercised by the existing [test_dap.c](../../test_dap.c) suite.

## Reproducing crashes

libFuzzer writes failing inputs to `crash-<sha1>` in the working
directory. Crash files belong as new corpus entries here and as
regression tests in [tests/test_dap_framing.c](../../test_dap_framing.c)
once fixed.
