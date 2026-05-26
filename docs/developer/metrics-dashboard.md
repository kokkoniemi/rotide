# CI metrics dashboard

Trend charts for the rotide test suite, microbenches, and fuzz targets,
rendered as SVG by `metrics_summary render-svg` and refreshed nightly at
03:17 UTC (or on-demand via the [nightly workflow's
`workflow_dispatch`](../../.github/workflows/nightly.yml)).

The embeds below reference stable `latest/*.svg` URLs on the
`metrics-assets` orphan branch, so this page stays current across any
checkout. Camo image-proxy caching adds minutes-to-hours of lag after a
fresh nightly run; missing images simply mean the underlying SVG isn't on
`metrics-assets` yet (e.g. first nightly hasn't completed, or that series
has fewer than two history points).

For how the SVGs are produced and where they live, see
[testing.md → Visualization](testing.md#visualization).

## Test suite

| Wall time | Stability |
|---|---|
| ![test wall time](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/test-wall-seconds.svg) | ![test stability](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/test-stability.svg) |

## Benchmarks (min / p50 / p95)

| | |
|---|---|
| ![bench document_position_byte_roundtrip](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/bench-document_position_byte_roundtrip.svg) | ![bench row_cache_splice_small_edit](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/bench-row_cache_splice_small_edit.svg) |
| ![bench wrap_recompute_1k_lines](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/bench-wrap_recompute_1k_lines.svg) | ![bench syntax_incremental_5k_lines_c](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/bench-syntax_incremental_5k_lines_c.svg) |
| ![bench screen_diff_unchanged_frame](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/bench-screen_diff_unchanged_frame.svg) | ![bench screen_diff_one_row_changed](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/bench-screen_diff_one_row_changed.svg) |

## Fuzz targets

### vterm

| Coverage | Corpus | Throughput |
|---|---|---|
| ![fuzz vterm cov](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-vterm-cov.svg) | ![fuzz vterm corpus](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-vterm-corpus.svg) | ![fuzz vterm throughput](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-vterm-throughput.svg) |

### lsp

| Coverage | Corpus | Throughput |
|---|---|---|
| ![fuzz lsp cov](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-lsp-cov.svg) | ![fuzz lsp corpus](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-lsp-corpus.svg) | ![fuzz lsp throughput](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-lsp-throughput.svg) |

### dap

| Coverage | Corpus | Throughput |
|---|---|---|
| ![fuzz dap cov](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-dap-cov.svg) | ![fuzz dap corpus](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-dap-corpus.svg) | ![fuzz dap throughput](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-dap-throughput.svg) |

### toml-theme

| Coverage | Corpus | Throughput |
|---|---|---|
| ![fuzz toml-theme cov](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-toml-theme-cov.svg) | ![fuzz toml-theme corpus](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-toml-theme-corpus.svg) | ![fuzz toml-theme throughput](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/fuzz-toml-theme-throughput.svg) |
