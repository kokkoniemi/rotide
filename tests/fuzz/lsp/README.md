# LSP framing fuzzer

libFuzzer harness over [`src/language/lsp_framing.c`](../../../src/language/lsp_framing.c) —
the `Content-Length:`-framed JSON-RPC parser that talks to LSP servers
(clangd, gopls, eslint, etc.). The fuzzer feeds arbitrary bytes through
a memfd into `editorLspReadFrame` and drains frames until the parser
gives up, so ASan/UBSan surface header overruns, integer overflow in
the Content-Length digit parser, and any malloc that an attacker-
controlled payload size could trigger.

## Run

```
make fuzz-lsp-smoke   # bounded; default 5000 runs (~1s locally, what CI runs per PR)
make fuzz-lsp         # indefinite; what nightly should run
```

Both build with `clang -fsanitize=fuzzer,address,undefined`. Override
the smoke iteration count with `FUZZ_LSP_SMOKE_RUNS=N`.

The smoke target stages the corpus into a tempdir before running so
libFuzzer's discovered mutations don't accrete back into the committed
seed set. Run `fuzz-lsp` directly when you want the corpus to grow.

Throughput is high (~5k exec/s on a developer machine, ~2k on a CI
runner) because the parser path is small and the harness avoids
per-iteration process spawns.

## Corpus

Seeds live in `corpus/`. Add new seeds whenever:

- you reproduce a real crash (drop the minimised input here),
- a new framing edge case enters the codebase (e.g. additional
  header fields, alternate line terminators),
- a 48h run adds zero new edges (the corpus is stale).

Keep the directory under 50 KiB total. Minimise periodically:

```
./tests/fuzz/lsp/fuzz_lsp -merge=1 corpus/ corpus_grown/
```

## Boundary it exercises

`editorLspReadFrame` is the only place rotide consumes bytes whose
shape is controlled by another process. Specifically:

- header byte-by-byte read loop bounded by `ROTIDE_LSP_MAX_HEADER_BYTES`,
- `Content-Length:` digit parsing (overflow-safe against size_t wrap),
- `ROTIDE_LSP_MAX_PAYLOAD_BYTES` ceiling so a 20-digit Content-Length
  cannot coax the client into a multi-exabyte malloc.

What it does **not** cover: JSON payload decoding (handled by
`lsp_json.c` / `lsp_protocol.c`), behavior under partial reads on a
real pipe (the harness uses a memfd so poll returns ready
immediately), or the response-id dispatch downstream of
`editorLspReadFrame`. Those paths are exercised by the existing
[test_lsp_protocol.c](../../test_lsp_protocol.c) and
[test_lsp_completion.c](../../test_lsp_completion.c) suites.

## Reproducing crashes

libFuzzer writes failing inputs to `crash-<sha1>` in the working
directory. Re-running the binary with that file as argument replays
the input deterministically. Crash files belong as new corpus entries
under `corpus/` and as one-line regression tests under
[tests/test_lsp_framing.c](../../test_lsp_framing.c) once the
underlying bug is fixed.
