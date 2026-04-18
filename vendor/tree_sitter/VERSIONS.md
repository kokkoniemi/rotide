# Vendored Tree-sitter Versions

Pinned source/tooling used by this repository:

- Tree-sitter runtime source ref: `6011985a6c513c3a3badd476d8a7ea41944cdaba` (`tree-sitter-main-6011985`)
- Tree-sitter C grammar source ref: `ae19b676b13bdcc13b7665397e6d9b14975473dd` (`tree-sitter-c-main-ae19b67`)
- Tree-sitter C++ grammar source ref: `f41e1a044c8a84ea9fa8577fdd2eab92ec96de02` (`tree-sitter-cpp-v0.23.4-f41e1a0`)
- Tree-sitter Go grammar source ref: `2346a3ab1bb3857b48b29d779a1ef9799a248cd7` (`tree-sitter-go-master-2346a3a`)
- Tree-sitter Bash grammar source ref: `a06c2e4415e9bc0346c6b86d401879ffb44058f7` (`tree-sitter-bash-master-a06c2e4`)
- Tree-sitter HTML grammar source ref: `73a3947324f6efddf9e17c0ea58d454843590cc0` (`tree-sitter-html-master-73a3947`)
- Tree-sitter JavaScript grammar source ref: `58404d8cf191d69f2674a8fd507bd5776f46cb11` (`tree-sitter-javascript-master-58404d8`)
- Tree-sitter CSS grammar source ref: `dda5cfc5722c429eaba1c910ca32c2c0c5bb1a3f` (`tree-sitter-css-master-dda5cfc`)
- Tree-sitter CLI release for regeneration: `v0.26.7`

The machine-readable source of truth is [`VERSIONS.env`](./VERSIONS.env).
Use [`scripts/refresh_tree_sitter_vendor.sh`](../../scripts/refresh_tree_sitter_vendor.sh) to refresh vendored sources and regenerated parser artifacts.
