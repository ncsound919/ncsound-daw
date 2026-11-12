# Studio FX golden vectors (vendored)

These files are byte copies of the shared ecosystem's canonical FX artifacts.
They are the ground truth `test_studiofx_parity.cpp` renders against: if the
C++ port drifts from the Kotlin/TS engines, the parity test fails.

- `parity-fixtures.json` — golden input/output vectors emitted by the Kotlin
  engine (`FxParityFixtureTest`, `shared/kotlin-core`). Schema
  `nc.studiofx.parity.v1`: 1024 mono samples @ 48 kHz, one case per
  implemented module plus a full-chain case.
- `catalog.json` — the module catalog emitted by `FxChainCodec.catalogJson()`
  (`shared/kotlin-core`). Schema `nc.studio-fx-catalog.v1`. The C++ spec table
  (`engine/include/nc/engine/StudioFxCatalogData.hpp`) is generated from it;
  regenerate the header after re-vendoring.

## Provenance

- Upstream repo: `overlay-shared`
- Pinned upstream commit: `2528abd98ad5aeba54dd07f69230dfdc42b06dd1`
- Source paths: `shared/audio-fx/parity-fixtures.json`, `shared/audio-fx/catalog.json`
- Vendored: 2026-11-12

## Re-vendoring

From a machine with both checkouts:

```bash
cp <phone-ecosystem>/shared/audio-fx/parity-fixtures.json tests/fixtures/studiofx/
cp <phone-ecosystem>/shared/audio-fx/catalog.json tests/fixtures/studiofx/
node tools/gen-studiofx-catalog.mjs   # regenerates StudioFxCatalogData.hpp
```

Then run `ctest -R studiofx` (or the full `nc_tests` binary): every parity
case must still pass. A vector change without a deliberate DSP change in the
Kotlin engine means the upstream fixtures moved — do not touch the C++ until
the upstream change is understood.
