# Vendored ChordStudio sources

Copied from `C:/Users/Asus/Desktop/Plugins/ChordStudio-master` on 2026-11-17.

- 25 files, 169.2 KB: the include closure of the `nc_mixrack` sources
  and of this repository's own sources that include ChordStudio headers.
- Licence: GPL-3.0 (`LICENSE`); the mix rack links it statically, so the
  derivative is GPL-3.0. See `docs/DEPENDENCIES.md`.
- `lock.cmake` holds a sha256 per file; CMake fails when one drifts.

Refresh with `python tools/vendor-chordstudio.py <checkout>`, then commit
this directory and `lock.cmake` together. Never edit these files in place:
changes belong upstream in ChordStudio.
