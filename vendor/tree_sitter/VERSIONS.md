# Vendored Tree-sitter Versions

Pinned source/tooling used by this repository:

- Tree-sitter runtime source ref: `6011985a6c513c3a3badd476d8a7ea41944cdaba` (`tree-sitter-main-6011985`)
- Tree-sitter C grammar source ref: `ae19b676b13bdcc13b7665397e6d9b14975473dd` (`tree-sitter-c-main-ae19b67`)
- Tree-sitter C++ grammar source ref: `f41e1a044c8a84ea9fa8577fdd2eab92ec96de02` (`tree-sitter-cpp-v0.23.4-f41e1a0`)
- Tree-sitter Go grammar source ref: `2346a3ab1bb3857b48b29d779a1ef9799a248cd7` (`tree-sitter-go-master-2346a3a`)
- Tree-sitter Bash grammar source ref: `a06c2e4415e9bc0346c6b86d401879ffb44058f7` (`tree-sitter-bash-master-a06c2e4`)
- Tree-sitter HTML grammar source ref: `73a3947324f6efddf9e17c0ea58d454843590cc0` (`tree-sitter-html-master-73a3947`)
- Tree-sitter JavaScript grammar source ref: `3a837b6f3658ca3618f2022f8707e29739c91364` (`tree-sitter-javascript-v0.23.1-3a837b6`)
- Tree-sitter CSS grammar source ref: `dda5cfc5722c429eaba1c910ca32c2c0c5bb1a3f` (`tree-sitter-css-master-dda5cfc`)
- Tree-sitter TypeScript grammar source ref: `f975a621f4e7f532fe322e13c4f79495e0a7b2e7` (`tree-sitter-typescript-v0.23.2-f975a62`)
- Tree-sitter JSON grammar source ref: `ee35a6ebefcef0c5c416c0d1ccec7370cfca5a24` (`tree-sitter-json-v0.24.8-ee35a6e`)
- Tree-sitter Python grammar source ref: `293fdc02038ee2bf0e2e206711b69c90ac0d413f` (`tree-sitter-python-v0.25.0-293fdc0`)
- Tree-sitter PHP grammar source ref: `5b5627faaa290d89eb3d01b9bf47c3bb9e797dea` (`tree-sitter-php-v0.24.2-5b5627f`)
- Tree-sitter CLI release for regeneration: `v0.26.8`

The machine-readable source of truth is [`VERSIONS.env`](./VERSIONS.env).
Use [`scripts/refresh_tree_sitter_vendor.sh`](../../scripts/refresh_tree_sitter_vendor.sh) to refresh vendored sources and regenerated parser artifacts.
