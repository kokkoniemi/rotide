# CI metrics dashboard

Trend charts for the rotide test suite, microbenches, and fuzz targets,
rendered as SVG by `metrics_summary render-svg`.

Cadence differs by series. The **test-suite** and **lines-of-code** charts
advance on every CI run: each push appends `test_run` / `loc` rows to the
rolling history and re-renders the dashboard, and a push to `main` updates the
embedded `latest/` copies ([ci.yml](../../.github/workflows/ci.yml)). The
**bench** and **fuzz** charts advance nightly at 03:17 UTC, when the [nightly
workflow](../../.github/workflows/nightly.yml) adds those rows and re-renders
(also on-demand via its `workflow_dispatch`). The nightly run also produces the
flakiness row from the `--repeat` flake-hunt soak.

The embeds below reference stable `latest/*.svg` URLs on the
`metrics-assets` orphan branch, so this page stays current across any
checkout. Camo image-proxy caching adds minutes-to-hours of lag after a
fresh nightly run; missing images simply mean the underlying SVG isn't on
`metrics-assets` yet (e.g. first nightly hasn't completed, or that series
has fewer than two history points).

For how the SVGs are produced and where they live, see
[testing.md → Visualization](testing.md#visualization).

## Test suite

| Runtime | Pass rate |
|---|---|
| ![test runtime](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/test-wall-seconds.svg) | ![test pass rate](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/test-pass-rate.svg) |

| Stability | Flakiness |
|---|---|
| ![test stability](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/test-stability.svg) | ![test flakiness](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/test-flakes.svg) |

## Lines of code

Sampled per push to `main` (via [`make loc`](../../scripts/count_loc.sh)), so an
idle period adds no points and the line only moves when code actually changes.
First-party and vendored code are on separate charts/scales — `vendor/tree_sitter`
alone is ~8.5M lines of generated parser tables and would otherwise crush the
first-party scale. The **by-domain** chart shows which subsystems are large and
how each evolves; the **churn** chart (added + deleted per domain) stays nonzero
whenever work happens, so it surfaces activity the size chart flattens (e.g. an
equal create+delete within one domain).

| First-party by domain | First-party total |
|---|---|
| ![loc first-party by domain](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/loc-first-party-by-domain.svg) | ![loc first-party total](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/loc-first-party-total.svg) |

| Churn by domain (added + deleted) | Test suite |
|---|---|
| ![loc churn by domain](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/loc-churn-by-domain.svg) | ![loc tests](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/loc-tests.svg) |

| Vendored (separate scale) | |
|---|---|
| ![loc vendored](https://raw.githubusercontent.com/kokkoniemi/rotide/metrics-assets/latest/loc-vendor.svg) | |

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
