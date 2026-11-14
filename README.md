# NC Sound Hub

A desktop DAW and plug-in host built on JUCE 8, with a React workspace rendered
in a WebView2 panel. The tree holds the audio engine (`engine/`), the Windows
application and plug-in targets (`app/`), the web workspace (`ui/`), the vendored
mix-rack sources (`third_party/chordstudio/`) and the native and UI test suites.

## Requirements

Windows 10/11 with Visual Studio 2022 (MSVC + Windows SDK), CMake 3.22+, Ninja,
and Node 20+ for the workspace. The dependencies that need explaining — JUCE,
the ChordStudio sources and WebView2 — are covered in `docs/DEPENDENCIES.md`.

## Build and test

```bat
build\_build.cmd        rem configure + build + ctest
build\_b.cmd            rem incremental build only
```

The suites run directly too:

```bat
ctest --test-dir build -C Release --output-on-failure
cd ui && npm test              rem vitest
cd ui && npm run typecheck
```

## Licence

The project as a whole is distributed under the **GNU General Public License
v3.0** (`LICENSE`), because it statically links ChordStudio (GPL-3.0) into the
application and both plug-in targets. A commercial ChordStudio licence is what
would allow shipping under different terms; that is an owner decision, not
something this repository can assume.

`THIRD-PARTY-NOTICES.md` lists every bundled component. The two Monotype display
faces the brand mockups used are not licensed for redistribution and are no
longer in the tree; the workspace uses the bundled SIL-OFL families instead.
