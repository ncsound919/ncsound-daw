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

## Dependency pinning - 8 November 2026

`app/CMakeLists.txt` built the native mix rack from
`set(NC_PLUGINS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../ChordStudio-master")`: a
sibling path with no version and no history. A clean clone could not configure
the app at all, so CI has only ever built the engine.

Two facts killed the obvious fix. The ChordStudio copy on this machine has no
`.git`, and nothing in it records an upstream remote, so there is no commit hash
for `FetchContent` or a submodule to point at. What it does have is a 674-line
GPL-3.0 `LICENSE`.

The dependency is small - 25 files, 165 KB - so it is vendored instead:

- `tools/vendor-chordstudio.py` walks the quoted-include closure from both sides:
  the `${NC_PLUGINS_ROOT}/*.cpp` listed in `app/CMakeLists.txt` and our own
  sources. It copies the closure to `third_party/chordstudio/`, preserving paths,
  writes `lock.cmake` with a sha256 per file, and copies the licence texts.
- `cmake/ChordStudioDeps.cmake` resolves the root (`-DNC_PLUGINS_ROOT`, then the
  `NC_PLUGINS_ROOT` environment variable, then the vendored copy as default) and
  treats the lock as both the required-file list and the drift check, so there is
  no second list to fall out of date.

The two-sided walk was not optional. The first attempt started only from the seven
ChordStudio translation units and vendored 17 files; the build then failed on
`app/Source/MixRack.cpp` including `fx/Biquad.h`, which is ours-to-theirs rather
than theirs-to-theirs. 25 files after the fix.

Verified: configure reports
`ChordStudio sources: .../third_party/chordstudio (vendored, 25 files)`;
`nc_mixrack` and `NCMixRackTests` build from it; `ctest -R mix_rack` passes (1/1,
149 s). Appending one newline to a vendored header makes configure exit 1 with
"The vendored ChordStudio copy no longer matches its lock:
plugins/overlay-fx/.../Biquad.h"; re-running the vendor tool restores it and
configure passes again (6.9 s, exit 0).

Not verified, and found while testing: a *fresh* build directory resolves the
vendored pin correctly but then fails at `juce_add_gui_app`, because WebView2 is
found only through `JUCE_WEBVIEW2_PACKAGE_LOCATION` sitting in the existing
`build/CMakeCache.txt`. That is the remaining blocker for a clean configure and it
is recorded in `docs/DEPENDENCIES.md`; it belongs with phase 2, not this pin. The
licence consequence is recorded and unresolved: ChordStudio is GPL-3.0 and
statically linked, so these binaries are derivatives whatever is decided about
JUCE.

## Clean configure - 8 November 2026

Pinning ChordStudio was not enough to configure from scratch. A fresh build
directory resolved the vendored copy and then died in `juce_add_gui_app`:

```
CMake Error ... Could NOT find WebView2 (missing: WebView2_include_dir WebView2_library)
```

The package was installed, in `Plugins/.deps/WebView2`, and JUCE's
`FindWebView2.cmake` only reads the NuGet folder under `%USERPROFILE%` or
`JUCE_WEBVIEW2_PACKAGE_LOCATION`. That variable existed in exactly one place:
`build/CMakeCache.txt`. So every new build directory - a fresh clone, CI - failed
at the same line, and the fix was invisible unless you already knew the path.

Committing the package was the obvious alternative and the wrong one: `WebView2.h`
is 2.9 MB and `WebView2LoaderStatic.lib` is 11 MB of Microsoft binaries, installed
rather than vendored. `cmake/WebView2Deps.cmake` resolves the location instead, in
order: `-DJUCE_WEBVIEW2_PACKAGE_LOCATION`, `-DNC_WEBVIEW2_PACKAGE_LOCATION`, the
`NC_WEBVIEW2_PACKAGE_LOCATION` environment variable, then JUCE's NuGet folder. It
sets a normal variable rather than a cache entry, so configuring with the
environment variable set does not bake this machine's absolute path into the build
directory.

Testing changed the validation twice. The first version globbed
`*Microsoft.Web.WebView2*` and accepted the first hit, which meant passing the
package directory itself looked valid - the glob matched
`Microsoft.Web.WebView2.nuspec` - and then handed JUCE a path with no
`build/native/include` in it. It now requires a directory containing
`build/native/include/WebView2.h`. The failure message also fires before
`juce_add_gui_app`, so it names the two fixes instead of surfacing as a JUCE
internal error three hundred lines into the log.

Verified: fresh `build/_wv2_fresh` configured from nothing with only
`NC_WEBVIEW2_PACKAGE_LOCATION` in the environment - exit 0, 243.8 s,
`WebView2 package: ...Microsoft.Web.WebView2.1.0.4314.35`, `Found WebView2`,
`ChordStudio sources: ... (vendored, 25 files)`. The existing `build` directory
still configures (its cache already holds the variable, which takes precedence).
The three resolver branches were exercised directly with `cmake -P`: no variable
and no NuGet package, a variable pointing at a directory with no package, and a
variable pointing at the package directory itself - each fails with its own
message; the correct parent directory prints the package line.

Not verified: a full hub build out of the fresh directory (configure only), and CI,
which still configures nothing - this fixes the configure step, it does not give
CI a job.

## CI from a clean clone - 8 November 2026

`ci.yml` had a job called `native-windows` that configured "full app", built it and
ran the JUCE test suites. It could not have built any of that. `third_party/JUCE`
is in `.gitignore`, the runner never fetches it, and the top-level CMakeLists has
one branch for JUCE being present and one for it being absent - the absent branch
prints "JUCE not found - skipping plugin targets" and exits 0. So the job compiled
the header engine, reported success, and said nothing about the app.

The `pluginval` job has the same hole and is worse: it runs
`cmake --build build --target NCChopStation`, a target that does not exist in CI,
so that job has been failing rather than validating anything.

Fixed with one script instead of inline workflow steps, because a script is the
part I can actually run here:

- `tools/fetch-native-deps.ps1` clones JUCE shallowly at the tag named in
  `third_party/JUCE_PIN.txt`, then compares `HEAD` with the pinned commit and
  throws if they differ. The pin file stays the single source of truth; a moved
  tag fails the job instead of silently changing the framework.
- The same script downloads `Microsoft.Web.WebView2.1.0.4314.35` and extracts it
  to `deps/webview2` (now gitignored - 14 MB is not going in the tree). It is
  downloaded as `.zip` because `Expand-Archive` refuses the `.nupkg` extension;
  the first version of the script hit exactly that.
- Both `native-windows` and `pluginval` run it before configure and export
  `NC_WEBVIEW2_PACKAGE_LOCATION`, which `cmake/WebView2Deps.cmake` accepts.

The first version of the script was written through a shell heredoc that ate the
backslashes in `'build/native/include/WebView2.h'`, turning it into a real
newline mid-literal. It now uses forward slashes throughout, which PowerShell and
CMake both accept on Windows.

Verified: `powershell -File tools/fetch-native-deps.ps1` from an empty state
cloned JUCE and ended at `dabd5ad13cd869cba41046947fffc8e5ce5cd662`, exactly the
pinned commit, and extracted the WebView2 package with
`build/native/include/WebView2.h` and `build/native/x64/WebView2LoaderStatic.lib`
present; a second run reported "already at" and "already extracted" and took
seconds. The workflow YAML parses, and the step list for `native-windows` now
reads: checkout, setup-node, native dependencies, point CMake at WebView2, Web UI
build, msvc, configure, build, test.

Not verified: any of this on a GitHub runner - no runner here, and a clean-tree
compile would mean a full JUCE + app build. The first CI execution is the real
test of the job, and the two jobs should be watched on the next push.



## Audit fixes - 8 November 2026

A static review of the app turned up three lifetime bugs, two medium issues and
some smaller ones. Four are fixed here; the rest are recorded at the end.

The piano roll was opened as a bare `juce::DocumentWindow` with no owner and no
close path, so it outlived the arrangement view that opened it and leaked on
every open. It is now a `PianoRollWindow` that deletes itself when its close
button is used, and the commit callback holds a `SafePointer` to the view
instead of a raw `this`.

A hosted plugin's editor window kept a raw `AudioProcessor*` and was only closed
when its rack row happened to be rebuilt, so unloading or replacing a plugin
could leave the window pointing at a destroyed processor. The window now watches
an inline registry (`PluginEditorRegistry.h`, header-only so the CMake source
list is untouched), and `TrackInsertRack::collectGarbage()` closes the editors of
each retire batch before destroying the inserts - one choke point rather than
eight call sites. The destructor also calls `editorBeingDeleted()` with the
`AudioProcessorEditor` it actually created; the first version passed the generic
content component and failed to compile (C2664), which is what the build was
stuck on.

`ProjectFile` renamed its temporary into place without syncing, so a crash right
after a save could leave a zero-length project. The temporary is now flushed
before the rename and the final file flushed after it. `collab/.gitignore` now
ignores `sessions.json`, which holds live session tokens.

Verified: `NCSoundHub` and `NCMixRackTests` build clean (124/124 steps, no
warnings in the touched files) and `ctest -R mix_rack` passes 1/1 in 223.5 s.
The first build failure was not a code defect: `cmake --build` invoked straight
from bash has no MSVC `INCLUDE`, so every translation unit failed with C1083 on
`<algorithm>`; builds now run through a `vcvars64` wrapper.

Not verified: nothing exercises window close or editor lifetime - the registry
and the piano-roll teardown are reviewed, not tested. There is no sanitizer or
Application Verifier build here, so the use-after-free fix rests on the identity
checks rather than a runtime detector.

Still open from the audit: the retired tempo/midi/automation snapshots grow
without bound (that needs a reclamation design, not a patch), and the export
path still sleeps on the message thread between blocks.



## Snapshot reclamation - 8 November 2026

Replacing a tempo map, the MIDI clip list or an automation lane hands the old
snapshot to a retired list, because an audio callback that loaded the old
pointer before the swap may still be reading it. Nothing ever freed those lists
- they were cleared only when the device stopped - so a long editing session
grew without bound: one tempo map per BPM drag, one lane per automation move.

Each retired snapshot is now stamped with the audio-block counter at the moment
it was replaced, and `reclaimRetiredSnapshots()` frees it once a later callback
has started. That is safe because callbacks run one at a time: if a block has
started after the swap, the block that could have held the old pointer has
returned. The counter is bumped with release and read with acquire, so the
message thread cannot free memory a callback is still touching. With no device
open there is no callback at all, so the first poll frees everything. The poll
runs from `pollRetiredInserts()`, the message-thread tick that already collects
retired inserts, so no new timer was added.

Verified: `NCSoundHub` and `NCMixRackTests` build clean (127/127);
`ctest -R mix_rack` passes 1/1. The new case, "replaced tempo and MIDI
snapshots are reclaimed", makes 10,000 BPM edits and 2,000 MIDI clip
replacements and then asserts the retained lists drain to zero; run on its own
it passes in 3 s. The suite's total wall time was 223.5 s before this change and
418.9 s on this run - the new case is 3 s of that, so the difference is not
attributable to the reclamation, but I did not isolate it either.

Not verified: no soak run and no memory graph from a real session yet, which is
A4 in the plan.



## Export waits instead of sleeping - 8 November 2026

`renderToFile` parks the engine with `offlineRender`, then calls
`prepareForRender()`, which resizes the buffers the audio callback renders
into. Before doing that it slept for two block durations plus 20 ms. Two blocks
is what the wait was meant to be; as a fixed sleep it was a guess, and one
overrunning block - a slow plug-in, a large buffer, a loaded machine - would let
the resize happen under a callback that was still running.

The callback now signals a `WaitableEvent` when it returns silence during an
offline render, and `renderToFile` waits for two of those signals (500 ms each)
before touching a buffer. Two, not one: the callback that was already inside
`renderBlock` when the flag was set has to return, and so does the one after it.
Callbacks run one at a time, so the second signal proves nothing is still using
the buffers. The event is signalled only while an export is parked, so normal
playback costs nothing, and with no device open there is nothing to wait for.

Verified: build clean (125/125). The export tests - the path that actually
changes - pass: 13 test cases, 106 assertions, 107 s, including the stems case
(one render per track) and the repeated-mixdown case, so the wait ran many times
without deadlocking. The full `mix_rack` suite was green at the previous commit
but was not re-run after this change.

Not verified: no test forces an overrunning callback to show the old sleep
losing that race. The argument is the ordering, not a reproduced failure; the
soak in A4 is where a loaded machine would expose it.



## Window lifetime tests - 8 November 2026

The 8 November fix closed a plugin editor window when its processor was retired,
but nothing tested it - no test created a window at all. Four cases now do,
through the same registry call the rack uses.

They need no plug-in: a stub `AudioProcessor` reports an editor and returns a
320x240 `AudioProcessorEditor`, so the window is constructed, registered and
closed exactly as the app does it. Each case asserts the watcher count before
and after plus the processor's active editor. That covers closing by processor
identity, leaving a different processor's window alone, destroying a window
directly, and a window created with no processor at all.

One detail the test leans on is worth recording: `AudioProcessorEditor`'s
destructor asserts `processor.getActiveEditor() != this`, so JUCE itself treats
a wrapper that does not call `editorBeingDeleted()` as a bug. That is exactly
the first version of the fix, which passed a `Component*` that never matched -
which is why the test checks `getActiveEditor()` and not just the window count.

Verified: build clean (127/127); the four cases pass with 19 assertions, through
real `DocumentWindow`s on this desktop - so a native window can be created and
destroyed inside the console test process, which was the open question. The full
`mix_rack` suite passes after the change too: 1/1 in 173.1 s - the fastest
run yet, with four more cases than the 418.9 s run, which settles the timing
question from the snapshot entry: that was machine load, not a regression.

Not verified: the piano roll. `PianoRollWindow` lives in an anonymous namespace
inside `ArrangeView.cpp`, so no test can name it; covering it needs an
extraction first.


## Piano roll window tests - 8 November 2026

A3's second half. The piano-roll pop-up was fixed to delete itself on close, but
nothing tested it: `PianoRollWindow` was private to `ArrangeView.cpp`, so no
test could name it. It now lives in `PianoRollWindow.{h,cpp}` and takes a
`unique_ptr<Component>` instead of a `unique_ptr<PianoRoll>`. ArrangeView still
passes a PianoRoll and the window takes ownership either way, so the seam costs
no behaviour and keeps `PianoRoll.cpp` - and its engine dependencies - out of
the test target.

Three cases in `PianoRollWindowTests.cpp` cover the close path a user has: the
close button deletes the window and the content it owns, closing one window
leaves another open, and the window never enters the `pluginEditors` registry -
registering it would let the rack's `closeWindowsFor()` delete a window that has
nothing to do with the insert. The stub content counts live instances, so the
cases prove deletion without reading freed memory.

Verified: `NCMixRackTests` builds clean; `[window]` is 7 cases, 39 assertions,
all passed; the full suite passes (82 cases, 54,097 assertions, exit 0);
`NCSoundHub` links, so the edited `ArrangeView.cpp` compiles with the extraction.

One assertion was wrong before the code was: `centreWithSize (760, 420)` sizes
the window, and its content is inset by the window border (758 px), so the case
now checks the window width and that the content is non-empty.

Not verified: the piano roll's own editing behaviour is still untested - this
covers window lifetime only - and nothing has watched these windows on screen.


## Release readiness, phases A4 and B - 8 November 2026

The rest of the remaining steps, run as far as a terminal can take them.

**A4 - soak, verifier blocked.** Ten cycles of `pluginval --strictness-level 5`
over both built Debug VST3s: twenty runs, zero failures. Application Verifier
did not run: the elevated `appverif -enable Basics -for NCMixRackTests.exe`
returns "Layer [Basics] not registered. Maybe you forgot to install the provider
dll containing it?", so the provider is missing from this machine and the check
needs an SDK feature installed before it can be discharged. Memory flatness is
therefore unmeasured - the soak proves the plug-ins keep loading and validating,
not that their footprint is stable.

**B1 - pluginval covers both plug-ins.** `ci.yml`'s push job now builds and
validates Chop Station and 808 Designer at strictness 5, one path per line.
`.github/workflows/nightly-pluginval.yml` runs both nightly at level 10, with
`DISABLED_TESTS: Discrete parameter state restoration` set and the reason in the
file: the documented `juce::AudioParameterBool` false positive at level 8 or
above, and 808 Designer declares two of them (`Mono Lock` and `Character`).
Locally pluginval 1.0.4 returned SUCCESS for both bundles at level 5.

**B2 - blocked.** pluginval 1.0.4 has no acceptance or record option; `--help`
lists `--validate`, `--run-tests` and `--data-file` only. The acceptance testing
documented on `develop` - a config JSON plus a golden `.wav`, recorded with
`--record-missing` - is not in the binary that exists, so the DSP-regression
gate cannot be built against it. No configs were invented to look covered.

**B3 - package gate.** `tools/verify-package.sh` takes a package tree or ZIP and
asserts both bundles with their `Contents/x86_64-win` binary and
`Contents/Resources/moduleinfo.json`, then that no `.exp`, `.lib`, `.pdb`,
`.ilk`, `juceaide` or `include/` shipped. `.github/workflows/release.yml` builds
the plug-ins on a `v*` tag, runs `cpack -G ZIP`, verifies the archive, uploads it
as an artifact and attaches it to the release. Verified against the existing
package: passes; against a copy with a `.lib` touched into the bundle: fails
exit 1. All three workflow files parse as YAML.

**Not done.** B4 (first clean-clone CI run) needs a runner; B5 (150% DPI and a
keyboard-only pass) needs hosts and a display; the 8-hour soak and the memory
graph need A4's verifier and a real session. Phase C is untouched: its first
task is an owner decision (the JUCE licence and the ChordStudio GPL-3.0
consequence), and C2-C4 depend on that answer.

Verified after all of it: `ctest --test-dir build -j 4` passes **160/160** in
157.0 s, including `mix_rack` (156.9 s), both plug-in suites and the four window
cases.

## Plug-in population from the Desktop Plugins folders - 8 November 2026

The browser only knew the registry and the two VST3 install folders, so bundles
sitting in a project checkout were invisible. `PluginCatalog` now takes extra
search roots: `setAdditionalSearchRoots()` appends them after the default
locations, so an installed bundle still wins over a build copy, and it is fed
from `plugins/search-paths.txt` beside `registry.json` plus the semicolon-list
in `NC_PLUGIN_SEARCH_PATHS`. `MainComponent` passes that file through
`pluginSearchRootsFor()`, and `NCScannerProbe --catalog <registry> [roots...]`
prints one `SCANNED` line per bundle so the population can be checked without
the UI.

Scanning `C:/Users/Asus/Desktop/Plugins` recursively (which reaches
`ChordStudio-master/plugins`) together with the two install folders produced
**124 catalog rows**: the 21 registry rows plus **103 bundles that were
previously unseen**. Nine registry rows matched a scanned bundle by
`vst3BundleName` and eight of those instantiate; the twelve unmatched rows are
the stock placeholders that have no bundle on this machine, and they still list
as before - no bundle was invented for them. The scan ended on "A plugin scan
timed out and was killed", so one bundle did not answer within the per-file
timeout; that is reported rather than hidden.

Two VoxStudio copies (the per-user install and `Desktop/Plugins/VoxStudio.vst3`)
both list, because both are real bundles; nothing collapses same-named files
from different paths.

### The sweep gate

`[bundle-sweep]` is opt-in (hidden `.vst3` tag) and walks every `.vst3` bundle
under `NC_TEST_VST3_DIR`: it scans the bundle, instantiates the first audio
class, renders four 512-sample blocks, round-trips the state and creates and
deletes the editor. Each bundle is recorded and the case fails once at the end,
so one stale bundle cannot hide the rest. Run over `Desktop/Plugins`:

    Swept 46 bundles: 44 instantiated, 43 editors opened, 2 failed

The two failures are single copies inside generated build trees, and the app's
own child-process scanner agrees - both return `ok=1 ... count=0`, so they
expose no audio class at all:

- `ChordStudio-master/plugins/percussion/build-fix1/Percussion_artefacts/Release/VST3/Percussion.vst3`
  (the packaged `Percussion.vst3` copies load)
- `VoxStudio/build-msvc/VST3/Debug/VoxStudio.vst3` (the installed copy and
  `Desktop/Plugins/VoxStudio.vst3` load)

So the browser does not list them and they are not counted as good. 43 of the
44 that load open an editor; one plug-in has no editor, which is not a failure.
Editor creation is the headless stand-in for the editor spot-check - native
windows cannot be captured here, so **no editor was looked at**, only created
and destroyed.

Verified on this source: `ctest --test-dir build -j 4` passes **160/160** in
247.6 s, and `[catalog]` passes 9 cases with 52 assertions.

**Header size, channel cutoff, 8-track names (TDD, 2026-11-09).** Finished the
follow-ups: the web header now mirrors the native menu sectors, the master
strip no longer clips its tips box, and all eight arrange headers show full
names.

- Track names: M/S/R moved to the second header row (name owns row one) and
  the peak meter flexes, so DRUMS..AUX 2 render unclipped at ~120 px.
  arrange.test.tsx pins the row structure and the `0.0 dB` text with the full
  `T2 · 0.0 dB` title. Verified in the dev page (all 8 names fully visible).
- Channel cutoff: the Mix master strip can outgrow short windows and
  `.ws-mixbody` hides the excess, cutting the tips box mid-line. The master
  strip now scrolls internally (`min-height: 0; overflow-y: auto`); probed
  1280x800 and 900x600 with zero hard-clipped elements.
- Status strip: native `setStatus` lines now ride `buildState`
  (`statusMessage`/`statusAlert`) into `AppState`; the strip falls back to
  them when no local flash is active. Workspace.test.tsx covers it.
- Session/export routes (all reuse the existing FileChooser menu handlers):
  openSession, saveSession, saveSessionAs, mixdownWav, exportStems,
  collectMedia, plus removeMasterPlugin and a `hasMasterPlugin` state bit.
  Regenerated routes (48), bridge methods, App handlers, Workspace props.
- Header parity: FILE gains SESSION/EXPORT/AGENT sectors (Save As, Collect
  Media, Mixdown WAV, Export Stems wired; FLAC/MP3/Audition and the Agent
  pair honest-disabled with their desktop locations); EDIT gains
  UNDO with depths, Transcribe Clip to MIDI, MARKERS, AUDIOSUITE, MASTER
  INSERT (Load moved here + wired Remove), MASTER RACK, Select All;
  TRACK has the three kind-specific adds plus select previous/next;
  HELP gains Preferences (opens the prefs dialog) and an honest
  Documentation row. chrome.test.tsx covers wired enablement/calls,
  honest-disabled states and desktop-location hints (14/14).
- Verified: tsc exit 0, vitest 9 files / 71 tests pass, vite build exit 0,
  dev-page CDP check of the FILE and EDIT menus, native wrapper rebuild exit
  0 (only pre-existing AudioEngine/SpscQueue warnings), ctest 160/160 pass,
  desktop CDP smoke on the relinked exe: FILE menu 6 wired entries enabled +
  5 desktop-only disabled, Save Session with no project file returns
  NO PROJECT FILE YET / USE SAVE SESSION AS in the strip (full route round
  trip), all 16 track headers render full names, master tips reachable
  (scrolls when needed, visible at test size). App closed after smoke so the
  next relink does not hit LNK1104.
- Not verified: real FileChooser dialogs, real mixdown/stems output, prefs
  dialog contents beyond opening, hardware audition of anything audible.
