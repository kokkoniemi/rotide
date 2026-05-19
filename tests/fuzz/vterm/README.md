# vterm fuzzer

libFuzzer harness over the vendored libvterm parser. Feeds arbitrary
bytes via `vterm_input_write`, then reads every cell back so ASan
catches buffer overruns inside the parser/state/screen pipeline.

## Run

```
make fuzz-vterm-smoke   # bounded iteration count (default 1000); ~90s locally, what CI runs per PR
make fuzz-vterm         # indefinite; what nightly should run
```

Both build with `clang -fsanitize=fuzzer,address,undefined`. Override
the smoke iteration count with `FUZZ_VTERM_SMOKE_RUNS=N`.

The smoke target stages the corpus into a tempdir before running so
libFuzzer's discovered mutations don't accrete back into the committed
seed set. Run `fuzz-vterm` directly when you want the corpus to grow.

Per-iteration cost is dominated by the 24×80 cell scan the harness
performs after each parse, multiplied by ASan/UBSan instrumentation —
expect ~5 exec/s on a CI runner, ~10 exec/s on a developer machine.
Raising `FUZZ_VTERM_SMOKE_RUNS` is fine, just don't be surprised by
the wall time: each extra 1000 runs adds roughly 90 seconds.

## Corpus

Seeds live in `corpus/`. Add new seeds whenever:

- you reproduce a real crash (drop the minimised input here),
- a new escape sequence enters the codebase that we want exercised,
- a 48h run adds zero new edges (the corpus is stale).

Keep the directory under 50 KiB total. Minimise periodically:

```
./tests/fuzz/vterm/fuzz_vterm -merge=1 corpus/ corpus_grown/
```

## Reproducing crashes

libFuzzer writes failing inputs to `crash-<sha1>` in the working
directory. Re-running the binary with that file as argument replays
the input deterministically. Crash files belong as new corpus entries
under `corpus/` and as one-line regression tests under
`tests/test_*_fuzz_repro.c` once the underlying bug is fixed.
