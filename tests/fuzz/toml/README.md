# TOML theme fuzzer

libFuzzer harness over [`src/config/theme_parse.c`](../../../src/config/theme_parse.c) —
the hand-rolled line-based TOML parser that reads
`~/.rotide/themes/*.toml` and the `[theme]` selector in the main
config. Not network input, but a shared theme file from the internet
or a typo'd local config shouldn't crash the editor.

## Run

```
make fuzz-toml-theme-smoke   # bounded; default 5000 runs (~<1s locally, what CI runs per PR)
make fuzz-toml-theme         # indefinite; what nightly should run
```

Override the smoke iteration count with `FUZZ_TOML_THEME_SMOKE_RUNS=N`.
The smoke target stages the corpus into a tempdir so libFuzzer's
mutations don't accrete back into the committed seed set.

## What's exercised

The fuzz wrapper at the bottom of
[`theme_parse.c`](../../../src/config/theme_parse.c) (gated behind
`#ifdef ROTIDE_FUZZ`) drives the same `editorThemeApplyStream` that
production calls, but reads from an `fmemopen` stream over the fuzz
input rather than a real file. That covers:

- `editorThemeApplyStream` line tokenizer (1024-byte `fgets` buffer
  with overflow detection),
- `editorThemeParseTable` table-header scanner,
- `editorThemeParseEntry` / `editorThemeParseKeyValue`,
- `editorConfigParseQuotedValue` quoted-string parsing,
- `editorParseThemeColorValue` hex-color and ANSI-name dispatch,
- `editorNormalizeThemeToken` whitespace/dash/case folding,
- the inherit-resolution path through `editorThemeInitBuiltin`.

## Findings so far

The theme parser is well-defended. 200k mutation runs against the
seed corpus produced zero crashes. Fixed-size stack buffers,
bounded string operations, and explicit overflow detection in the
line reader appear to do their job.

That's a real result, not an empty harness — coverage during a 200k
run reaches ~215 edges / 487 features across the parser. If a future
change to `theme_parse.c` introduces an out-of-bounds write or
allocator misuse, the CI smoke run will surface it before merge.

## Corpus

Seeds live in `corpus/`. Add new seeds whenever:

- you reproduce a real crash (drop the minimised input here and add
  a regression test in
  [tests/test_workspace_theme_config.c](../../test_workspace_theme_config.c)),
- the parser gains support for a new syntax (e.g. a new table or key
  shape),
- a 48h run adds zero new edges (the corpus is stale).

Keep the directory under 50 KiB total. Minimise periodically:

```
./tests/fuzz/toml/fuzz_toml_theme -merge=1 corpus/ corpus_grown/
```

## Reproducing crashes

libFuzzer writes failing inputs to `crash-<sha1>` in the working
directory. Re-running the binary with that file as argument replays
the input deterministically.
