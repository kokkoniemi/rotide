# TOML Theme Fuzzer

Fuzzes `themeParseApplyStream` in
[`src/config/theme_parse.c`](../../../src/config/theme_parse.c): the TOML theme
parser and `[theme]` selector path.

```
make fuzz-toml-theme-smoke   # bounded; default 5000 runs (~<1s locally, what CI runs per PR)
make fuzz-toml-theme         # indefinite; what nightly should run
```

Use `FUZZ_TOML_THEME_SMOKE_RUNS=N` to change the bounded run. Seeds live in
`corpus/`; add minimized crashes or new parser syntax there, plus a regression
in [`tests/test_workspace_theme_config.c`](../../test_workspace_theme_config.c).
