# vterm Fuzzer

Fuzzes the vendored libvterm parser by feeding arbitrary bytes through
`vterm_input_write`, then reading every cell back under ASan/UBSan.

```
make fuzz-vterm-smoke   # bounded iteration count (default 1000); ~90s locally, what CI runs per PR
make fuzz-vterm         # indefinite; what nightly should run
```

Use `FUZZ_VTERM_SMOKE_RUNS=N` to change the bounded run. Seeds live in
`corpus/`; add minimized crashes or new escape-sequence coverage there, plus a
small regression test when the bug is fixed.
