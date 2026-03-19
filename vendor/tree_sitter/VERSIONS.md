# Vendored Tree-sitter Versions

Pinned source/tooling used by this repository:

- Tree-sitter runtime source ref: `6011985a6c513c3a3badd476d8a7ea41944cdaba` (`tree-sitter-main-6011985`)
- Tree-sitter C grammar source ref: `ae19b676b13bdcc13b7665397e6d9b14975473dd` (`tree-sitter-c-main-ae19b67`)
- Tree-sitter Bash grammar source ref: `a06c2e4415e9bc0346c6b86d401879ffb44058f7` (`tree-sitter-bash-master-a06c2e4`)
- Tree-sitter CLI release for regeneration: `v0.26.7`

The machine-readable source of truth is [`VERSIONS.env`](./VERSIONS.env).
Use [`scripts/refresh_tree_sitter_vendor.sh`](../../scripts/refresh_tree_sitter_vendor.sh) to refresh vendored sources and regenerated parser artifacts.
