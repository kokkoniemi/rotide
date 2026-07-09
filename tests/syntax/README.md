# Syntax Fixtures

Fixtures under `supported/<lang>/` back syntax activation, highlighting,
injection, and incremental-parse tests. Directory names should match shipped
language modes in `src/rotide.h` and `src/language/*`.

- `activation.*`: filename, extension, shebang, or title detection.
- `highlight.*`: capture/color coverage.
- `incremental.*`: incremental parse equivalence.
- `incomplete.*`, `contract.*`, and `injections.*`: targeted parser or
  injection cases.

Parser overlays such as JSDoc and markdown-inline do not get standalone fixture
directories; keep those examples in their host language fixtures. `planned/` is
placeholder-only.

Tests resolve these fixtures from the startup repo root.
