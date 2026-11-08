# NCSoundHub Finalization Work Log

Date: 2026-11-08
Workspace: `C:\Users\Asus\Desktop\Plugins\VST DAW`
Scope: native engine and JUCE components, with the React/WebView surface kept in sync where routes were already present.

This file records the work completed during the finalization pass, the verification evidence that exists, and the work that remains unverified. It is deliberately explicit about interrupted checks rather than treating a successful compile as proof of runtime behaviour.

---

## 1. Completed work

### 1.1 Marker model and native marker commands

- `AudioEngine::setMarkers` now sanitizes incoming markers through `nc::engine::sanitizeMarkers` before publishing them. This protects project loading and the UI from non-finite PPQ values, oversized names, and excessive marker counts.
- Markers are part of the saved project:
  - `ProjectDocument` carries `markers` and `tempoMap`.
  - `MainComponent::writeProject` writes `engine.markers()`.
  - `MainComponent::readProject` restores them with `engine.setMarkers(document->markers)`.
- The arrangement ruler now paints the engine/project markers at their real PPQ positions. The previous hardcoded INTRO/VERSE/HOOK/OUTRO labels were removed.
- Native Edit menu marker commands were added:
  - `Add Marker at Playhead` — menu ID `2060`
  - `Next Marker` — menu ID `2061`
  - `Previous Marker` — menu ID `2062`
  - `Delete Marker at Playhead` — menu ID `2063`
- Keyboard shortcuts:
  - `Ctrl+M` adds a marker at the playhead.
  - `Ctrl+Shift+M` deletes the marker at the playhead.
  - `Ctrl+Right` jumps to the next marker.
  - `Ctrl+Left` jumps to the previous marker.
- New markers are auto-named `MARKER N`, use the current tempo-map PPQ position, and reject duplicate positions within 0.001 PPQ.
- Deleting and jumping use the same 0.001 PPQ tolerance, so a marker exactly under the playhead is handled reliably.
- Marker navigation converts PPQ through the tempo map, so jumps remain musically correct across tempo changes.
- `New Session` resets BPM to 120 and clears markers.

### 1.2 In-place marker editing

- Markers can now be dragged directly on the arrangement ruler:
  - The marker flag/line and its label are hit-tested.
  - Dragging commits the new PPQ through `AudioEngine::setMarkers`.
  - When snap is enabled, the marker lands on a bar boundary.
  - The drag is kept as a local copy while moving, so crossing another marker does not invalidate the edited marker index.
  - The ruler repaints during the gesture and the status bar reports the final name and bar.
- Double-clicking a marker opens a native rename dialog.
  - Empty names are rejected.
  - The updated name is republished through the engine and repainted.
- Marker drag/rename are direct component edits; they are not currently part of the clip undo stack.

### 1.3 MIDI export correctness

`AudioEngine::exportMidiFile` previously exported only notes, CCs, and pitch bends. Tempo changes and section names were lost.

The exporter now writes:

- One tempo meta event per tempo-map point, at `ppq * 480` ticks, with `60000000 / BPM` microseconds per quarter note.
- A 4/4 time-signature meta event at tick 0.
- Project markers as standard MIDI marker meta events (type 6), at their PPQ positions.
- The selected track's note, CC, and pitch-bend events unchanged.

This means a project with a tempo change re-imports into another DAW at the correct speed and section positions instead of defaulting to 120 BPM.

### 1.4 Silent stem and export fixes (earlier in the pass)

- Silent stem export now honors the requested output format (WAV/FLAC/MP3/AIFF) instead of always writing a WAV.
- Silent stem length now uses `ppqToSeconds` with the active tempo map rather than a constant-BPM calculation.
- Zero-length export requests are rejected instead of producing a misleading empty file.
- Export regression coverage was added for silent FLAC stems and MIDI export.

### 1.5 Native bridge / React surface (earlier in the pass)

- The native WebView bridge reached 36 routes.
- Four user insert slots and the master insert are projected through the bridge.
- MIDI and automation projections are available to the React surface.
- Piano-roll and automation edits use gesture-commit semantics rather than committing on every pointer move.
- Undo/redo and the plugin browser are wired.
- UI checks at that point:
  - `tsc -b --noEmit` exit 0.
  - Vitest 44/44 passing.

---

## 2. Verification evidence

### 2.1 Passing in this pass

- Native export filter:
  - Command: `build/app/NCMixRackTests_artefacts/Debug/NCMixRackTests.exe "[export]"`
  - Result: **13 test cases, 106 assertions, all passed, exit 0**.
  - This includes the new test for tempo-map, time-signature, and marker metadata in MIDI export.
- Native link:
  - `build/nc_final_build.log` reached:
    - `[83/85] Linking CXX executable app\NCMixRackTests_artefacts\Debug\NCMixRackTests.exe`
    - `[84/85] Linking CXX executable "app\NCSoundHub_artefacts\Debug\NCSound Music Hub.exe"`
  - Both executables have a timestamp of **2026-11-08 00:37**.
- Whitespace/format check:
  - `git diff --check -- app/Source` exited **0**.
- Earlier UI checks:
  - `tsc -b --noEmit` exit 0.
  - Vitest **44/44** passing.
- Earlier native marker build:
  - `MARKER_WRAPPER_EXIT=0`.
  - App relinked after the marker menu/shortcut implementation.

### 2.2 Checks that are not proven

- **Full native test suite: result unknown.**
  - The full `NCMixRackTests.exe` run was started after the export suite, but the runner was interrupted/restarted before a result was returned.
  - A retry was also interrupted before output was available.
  - Do not describe the full suite as passing without rerunning it.
- **Interactive UI smoke test: not performed.**
  - The app was not launched and clicked through.
  - Marker drag, marker rename, ruler hit-testing, and the native rename dialog are compile/link verified only.
  - The status-bar text and dialog behaviour still need a manual pass.
- **Build wrapper exit code: not captured cleanly.**
  - The temporary `build/_bt.cmd` wrapper linked both binaries, but its `FINAL_BUILD_EXIT` line was empty in the captured log.
  - The binary timestamps and successful direct execution of the test binary are the reliable evidence.
- **No commit was made.**

---

## 3. Files changed or added

Modified under `app/Source`:

| File | Change |
|---|---|
| `MainComponent.h` | Declarations for add/delete/jump marker commands. |
| `MainComponent.cpp` | Edit menu marker commands `2060`–`2063`, keyboard shortcuts, marker add/delete/jump implementation, MIDI export menu route, New Session reset behavior. |
| `AudioEngine.h` | Marker API declaration and `Markers.hpp` include; prior export/engine changes. |
| `AudioEngine.cpp` | Marker sanitization; MIDI export tempo/time-signature/marker meta events; silent stem format and tempo-map fix. |
| `ArrangeView.h` | Marker hit-test, rename helper, and marker-drag state. |
| `ArrangeView.cpp` | Project markers painted at real PPQ; marker drag/rename interaction on the ruler; current-session ruler marker rendering. |
| `ExportTests.cpp` | Regression tests for silent FLAC stems, MIDI export, and MIDI tempo/marker metadata. |
| `WebViewBridge.cpp` | Earlier native bridge routes and input hardening. |

Added at the project root:

- `DAW_WORK_LOG.md` — this file.

Build helper created during the pass:

- `build/_bt.cmd` — builds `NCMixRackTests` and `NCSoundHub` together and appends the result to `build/nc_final_build.log`. Its logged exit marker was blank, but the link completed; the helper should be corrected or removed before it is relied on as a gate.

Build logs:

- `build/nc_marker_build.log` — successful marker app build.
- `build/nc_final_build.log` — linked test and app binaries; no final exit marker captured.
- `build/nc_midi_test_build.log` — earlier failed wrapper attempt due to vcvars quoting; not a code failure.

---

## 4. Useful commands

Run the export subset:

```bash
cd "C:/Users/Asus/Desktop/Plugins/VST DAW"
"build/app/NCMixRackTests_artefacts/Debug/NCMixRackTests.exe" "[export]"
```

Run the full native suite (result still unverified):

```bash
cd "C:/Users/Asus/Desktop/Plugins/VST DAW"
"build/app/NCMixRackTests_artefacts/Debug/NCMixRackTests.exe"
```

Build the app and tests:

```bash
cd "C:/Users/Asus/Desktop/Plugins/VST DAW"
cmd.exe //c "build\\_b.cmd"
```

Check whitespace:

```bash
cd "C:/Users/Asus/Desktop/Plugins/VST DAW"
git diff --check -- app/Source
```

UI checks (Node shim may be broken; use the real binary):

```bash
cd "C:/Users/Asus/Desktop/Plugins/VST DAW/ui"
/c/tools/node/node.exe node_modules/typescript/bin/tsc -b --noEmit
/c/tools/node/node.exe node_modules/vitest/vitest.mjs run
/c/tools/node/node.exe node_modules/vite/bin/vite.js build
```

---

## 5. Remaining gaps / next work

1. **Run the full native suite to completion** and record the actual case/assertion counts and exit code.
2. **Manually exercise marker interactions** in the running app: add, drag, rename, delete, next/previous, save, and reopen.
3. **Integrate marker edits with undo** if markers are to behave like clips. Currently add/delete/drag/rename are direct engine edits.
4. **Add a tempo-map editor.** The project format and engine support multi-point tempo maps, and export now preserves them, but the native UI still primarily edits a constant BPM through the transport. `setBpm` resets the map to a single point.
5. **MIDI import currently reads notes/CCs/bends but not the source tempo map.** Decide whether importing a MIDI file should replace the project tempo or leave the session tempo unchanged.
6. **Plugin parameter automation** remains outside the current volume/pan automation model.
7. **No visual review of the marker ruler or rename dialog** has been performed.

---

## 6. Working constraints honoured

- Native build and tests were run against the real MSVC/JUCE tree, not a mock.
- No marker state is hardcoded in the ruler anymore; project data is authoritative.
- MIDI metadata is generated from the engine's sanitized tempo map.
- Changes are limited to the DAW workspace; unrelated untracked `telemetry-shield/` and existing `ui/` content were not modified by this logging step.

## Verification update — 8 November 2026

The open items recorded above are now closed or precisely scoped:

- Full native suite: **160/160 ctest targets pass** (139.9 s, `-j 4`), including
  `mix_rack` (139.8 s) and both plugin suites. `NCEightOHeightTests` is no longer
  "Not Run".
- `NCEightOHeightTests`: **2,638 assertions across 8 cases, all passing.**
  Getting there required three real fixes:
  1. `EightOHeightProcessor::releaseResources()` was declared and never defined,
     so the plugin and its test binary could not link at all (`LNK2001`). ctest
     reported the failure as "Not Run", which disguised a broken build as a skip.
  2. The subsonic stage used `1 - exp(-2*pi*fc/fs)` — the complementary lowpass
     coefficient — inside a DC-blocker recursion. At 55 Hz that is a gain of
     about 2.8e-5, so the plugin rendered note-ons at roughly -90 dB: effectively
     silent. The pole coefficient is `exp(-2*pi*fc/fs)`.
  3. One case asserted that CC123 (All Notes Off) silences the pool immediately.
     CC123 releases and rings out by design (`allNotesOff(allowTail)`) and by MIDI
     convention; CC120 (All Sound Off) is the panic. The case now covers both.
- UI: `tsc -b --noEmit` clean, `vitest` 43/43, `vite build` succeeds
  (471.78 kB JS / 144.88 kB gzip, 123.61 kB CSS).

Still unproven, and not claimed: an interactive session against a real project,
audio output on real hardware, 150 % DPI, clean-VM install, an 8-hour soak, and
survival of a third-party plugin crash. There is still no installer in the tree,
and the app build depends on an unpinned out-of-repo `../../ChordStudio-master`.

## Lint and formatting gate - 8 November 2026

`ui/` had no linter or formatter, so the defects below were invisible to CI.
Added ESLint 9 (flat config: typescript-eslint, react-hooks, react-refresh,
eslint-config-prettier) and Prettier, wired as `npm run lint` and
`npm run format:check` in the `ui` CI job ahead of the typecheck.

- Lint reports 0 errors / 69 warnings; Prettier is clean tree-wide.
- The five `(store as any)` casts in `PLearn` are now a typed `LearnStore`.
- `no-unused-expressions` is off for `src/console-ui/renderers` only, because
  the `x ? lineTo : moveTo` polyline idiom is deliberate there.
- Open defect, tracked rather than hidden: 31 `react-hooks/rules-of-hooks`
  violations in `ChannelStripPanel`, `MasterPanel` and `ModulatorPanel`. The
  `useValue` / `useHas` / `useNum` / `useOption` / `useLabel` accessors are real
  hooks (they consume the panel context) being called from conditionals, loops
  and callbacks, which can throw "Rendered more hooks than during the previous
  render" when a panel toggles. Downgraded to warnings so the gate can land and
  block new violations; hoisting the accessors is the follow-up.

### Hook order - the 31 violations are fixed, not just counted

The panel DSL accessors were real hooks (`useContext` on the panel context) being
called from loops, `.map()` bodies, conditionals and callbacks. Each accessor is
now a two-layer pair: a pure read (`panelValue`, `panelNum`, `panelOption`,
`panelLabel`, `panelHas`, `panelSpec`, `panelBound`, `panelText`) that takes the
resolved panel, plus the existing hook as a one-line wrapper over it. Panels call
`usePanelContext()` once per component and use the pure reads everywhere a hook
would be illegal; the 150-odd ordinary call sites keep their original accessors.

That removes the "Rendered more hooks than during the previous render" hazard
when a module, EQ band or filter row toggles, keeps the accessors' API and error
behaviour (`panel control used outside <PanelScope>`, unknown key throws), and
lets `react-hooks/rules-of-hooks` go back to `error` everywhere: the per-file
downgrade in `eslint.config.js` is gone. Targeted calls that were only legal by
luck (fixed-length loops) are now provably legal.

## Release packaging - 8 November 2026

There was no `install()` rule and no CPack anywhere in the tree, so a release
could only be assembled by hand from `build/`. Added `cmake/Packaging.cmake`
(included from the root list, `NC_ENABLE_PACKAGING` to switch off):

- Payload is copied from JUCE's artefact tree, because on Windows a `.vst3` is a
  directory bundle: `VST3/` and `Standalone/` for 808 Designer and Chop Station,
  plus `docs/`. Only our components are packaged.
- `cmake/package-clean.cmake` runs as a CPack pre-build script and deletes what
  JUCE's own install rules drop into the staging tree (its modules, headers and
  `juceaide.exe`), so no toolchain output can ship.
- Linker output (`*.exp`, `*.lib`, `*.pdb`, `*.ilk`) is excluded: it sits in the
  same `VST3/` folder as the bundle.
- ZIP builds everywhere. NSIS64/WIX are appended to the generator list only when
  `makensis`/`candle` is found, so `cpack` cannot fail on a machine without an
  installer toolchain.

Verified: `cpack -G ZIP` produced `ncsound-daw-0.1.0-AMD64.zip` (14 MB)
containing both `.vst3` bundles with their `moduleinfo.json`, both standalone
`.exe` files and the docs, with 0 leaked tools/headers and 0 linker artefacts.

Not verified, and not claimed: an actual install run on a clean machine, and any
NSIS/WIX installer - neither toolchain is installed here, so that branch is
configured but has never been exercised. The app itself is not in the package:
it links against the out-of-repo `ChordStudio-master` sources, so packaging it
would mean shipping a half-empty product.
