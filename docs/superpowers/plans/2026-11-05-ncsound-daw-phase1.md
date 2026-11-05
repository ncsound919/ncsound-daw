# NCSound DAW — Phase 1 Implementation Plan (M0–M2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Windows DAW that opens, loads any VST3, plays a four-bar pattern through it, and reopens the saved project identically.

**Architecture:** ChordStudio's audio engine is extracted into a standalone `nc_engine` library behind a `Session` facade that knows nothing about any shell. A new JUCE standalone app hosts a React/WebView2 UI built on the NCSound kit, talking through one generated bridge. The session file is `vstdaw` schema v2.

**Tech Stack:** C++20, JUCE 8.0.14 (9.0.x if Spike 0 rejects Tracktion Engine), CMake ≥ 3.22, Ninja, CPM.cmake, Catch2 v3, nlohmann/json, React 19 + Vite + TypeScript, WebView2.

**Spec:** `docs/superpowers/specs/2026-11-05-ncsound-daw-design.md`

Repo root: Desktop/Plugins/VST DAW (already holds the schema, design doc and this plan).

## Global Constraints

- Windows x64 only. MSVC 19.44 / VS 2022, CMake ≥ 3.22.
- No allocation, lock acquisition or file I/O on the audio callback.
- JUCE pinned to 8.0.14 via CPM; `JUCE_DIR` overrides the fetch.
- Palette tokens: `ink #181818`, `well #0B0B0B`, `surface #222222`, `surfaceHi #2C2C2C`, `paper #F8E8C8`, `accent #C25A2A`, `accentHot #E0824D`.
- Two font families per surface maximum; JetBrains Mono for every number, id and timestamp.
- A project with a newer `schemaVersion` is rejected with a message, never half-loaded.
- Every task ends with a commit. No task is done without its tests passing.

## Tooling Decisions (2026-11-05)

Full research and sources: `docs/superpowers/research/2026-11-05-oss-acceleration.md`.

- Build with the Ninja generator; PCH for our headers only; LTO off outside Release; sccache in CI only; never unity-build JUCE sources.
- Plugin hosting uses the MIT VST3 SDK 3.8 helpers plus JUCE's out-of-process scanner; pluginval runs as a CI binary and is never linked.
- UI bridge: JUCE 9's `@juce-framework/webview` if Spike 0 rejects Tracktion Engine; otherwise JUCE 8's in-tree interop.
- Layout and pattern editors: spike `audio/ui` (MIT) before hand-rolling them in M4.
- Recording uses `dr_wav`; stretch uses `signalsmith-stretch`.
- Stock suite draws on Airwindows (MIT), CloudSeedCore (MIT) and the BSD modules of `chowdsp_utils`.
- Licence rule: nothing GPL or AGPL is linked. Rubber Band, libsndfile, Guitarix, the CloudSeed JUCE ports and `chowdsp_eq` are excluded.

## Review Focus

1. A plugin that hangs during scan must leave the window responsive and report the plugin as timed out.
2. Opening a project whose plugin is missing must surface the missing slot to the UI, not drop it silently.
3. A hosted plugin returning NaN or infinity must be contained, not silence the graph permanently.
4. Launching with no audio device must show a banner, not crash.
5. A truncated or hostile project file must produce an error message, never a crash or a silent partial load.

---

### Task 1: Monorepo, build rail, test harness

**Files:**
- Create: `CMakeLists.txt`, `cmake/CPM.cmake`, `tests/CMakeLists.txt`, `tests/test_smoke.cpp`, `.gitignore`, `.clang-format`, `.github/workflows/ci.yml`

**Interfaces:**
- Produces: CMake target `nc_tests`; function `nc_add_library(name)` defined in `cmake/NcTargets.cmake` that applies C++20 and warning flags to every target.

- [x] **Step 1: Write the failing test** — `tests/test_smoke.cpp`: `TEST_CASE("engine version is reported") { REQUIRE(std::string(nc::version()).starts_with("0.")); }`.
- [x] **Step 2: Run it** — `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target nc_tests`. Expected: configure or compile fails, no `nc::version`.
- [x] **Step 3: Implement** — root `CMakeLists.txt` with `project(NCSoundDAW VERSION 0.1.0 LANGUAGES CXX)`, CPM pinned to tag `0.40.0`, Catch2 `3.7.1`, `add_subdirectory(tests)`; `cmake/NcTargets.cmake` sets C++20, `/W4 /WX`, PCH hooks and keeps LTO off outside Release; `engine/src/version.cpp` returning `"0.1.0"`; declare `const char* nc::version()` in `engine/include/nc/version.hpp`.
- [x] **Step 4: Run it** — same build, then `ctest --test-dir build --output-on-failure`. Expected: 1 test passes.
- [x] **Step 5: Commit** — `git init && git add -A && git commit -m "build: monorepo rail, CPM, Catch2 harness"`.

### Task 2: Brand tokens and monogram

**Files:**
- Create: `ui-kit/package.json`, `ui-kit/src/tokens.ts`, `ui-kit/src/tokens.css`, `brand/monogram.svg`, `brand/README.md`
- Test: `ui-kit/src/tokens.test.ts`

**Interfaces:**
- Produces: `export const tokens = { ink, well, surface, surfaceHi, paper, paperDim, accent, accentHot, rust, danger }` and matching `--nc-*` CSS custom properties.

- [ ] **Step 1: Write the failing test** — asserts `tokens.accent === "#C25A2A"` and that every token value matches `^#[0-9A-F]{6}$` or `^rgba\(`.
- [ ] **Step 2: Run it** — `cd ui-kit && npm install && npm test`. Expected: FAIL, no module.
- [ ] **Step 3: Implement** — tokens as above; `monogram.svg` drawn as concentric record arcs with waveform bars in `#C25A2A` and `#F8E8C8`, 64×64 viewBox, transparent, single path per arc group.
- [ ] **Step 4: Run it** — `npm test`. Expected: PASS. Visually check `monogram.svg` at 24 px in a browser before committing.
- [ ] **Step 5: Commit** — `git add ui-kit brand && git commit -m "feat(ui-kit): NCSound tokens and monogram"`.

### Spike 0: Tracktion Engine go/no-go

**Files:**
- Create: `docs/spike-0-tracktion.md`

- [ ] **Step 1: Build the demos** — clone `tracktion/tracktion_engine` with submodules; build `DemoRunner` with Ninja on this machine.
- [ ] **Step 2: Measure** — record clean build time, incremental build time, release binary size and peak RAM in `docs/spike-0-tracktion.md`.
- [ ] **Step 3: Prove the model** — a four-track edit with one third-party VST3 and one Overlay FX: play, automate a parameter, render offline, reload.
- [ ] **Step 4: Decide** — if the clean build is ≤15 min and playback is stable at 128 samples, adopt Tracktion Engine and replace Tasks 3–9 with one integration task; otherwise keep the extraction plan. Write the verdict in the same file, then commit.

---

### Task 3: Engine library compiles

**Files:**
- Create: `engine/CMakeLists.txt`, `engine/src/*` copied from `ChordStudio-master/Source/audio/*`, `engine/include/nc/engine/*.hpp`
- Modify: namespaces `chordstudio::audio` → `nc::engine`

**Interfaces:**
- Produces: static library `nc_engine`, header root `engine/include/`.

- [ ] **Step 1: Write the failing test** — `tests/test_engine_boot.cpp`: construct `nc::engine::AudioEngine`, call `prepare(48000.0, 256)`, assert `getSampleRate() == 48000.0`.
- [ ] **Step 2: Run it** — build `nc_tests`. Expected: FAIL, `nc_engine` target missing.
- [ ] **Step 3: Implement** — copy `AudioEngine`, `Track`, `PluginHost`, `PluginSlot`, `RtPublisher.h`, `Ducker`, `Loudness`, `BandLimited`, `AudioClipPlayer`, `ClipStreamer`, `SampleChopper`, `SampleEdit`, `OverlayApplier`, `OverlaySession`, `InputSampler`; leave `DrumEngine` out and delete its references; rename the namespace with a mechanical pass; add the target.
- [ ] **Step 4: Run it** — `ctest --test-dir build -C Debug --output-on-failure`. Expected: build succeeds with warnings-as-errors off for this task only.
- [ ] **Step 5: Commit** — `git commit -m "feat(engine): extract audio engine into nc_engine"`.

### Task 4: Cut the shell dependency

**Files:**
- Create: `engine/include/nc/engine/ISessionHost.hpp`
- Modify: `engine/src/AudioEngine.h`, `engine/src/AudioEngine.cpp`

**Interfaces:**
- Produces: `class ISessionHost { virtual void onTransportChanged(TransportState) = 0; virtual void onPluginLoadFailed(const std::string& nodeId, const std::string& reason) = 0; virtual void onProjectLoadIssues(const std::vector<std::string>&) = 0; };`

- [ ] **Step 1: Write the failing test** — a `RecordingHost` implementing the interface; load a project referencing a plugin id that does not exist; assert `onPluginLoadFailed` fired once with that id.
- [ ] **Step 2: Run it** — expected: FAIL, no interface.
- [ ] **Step 3: Implement** — interface header; `AudioEngine` stores `ISessionHost*`; replace every `MainComponent` reference with host calls; keep behaviour identical where a callback already existed.
- [ ] **Step 4: Run it** — `ctest`. Expected: new test passes, existing engine tests unchanged.
- [ ] **Step 5: Commit** — `git commit -m "refactor(engine): replace MainComponent coupling with ISessionHost"`.

### Task 5: Session facade and transport

**Files:**
- Create: `engine/include/nc/engine/Session.hpp`, `engine/src/Session.cpp`
- Test: `tests/test_session_transport.cpp`

**Interfaces:**
- Produces: `class Session { void prepare(double sampleRate, int maxBlockSize); void setHost(ISessionHost*); void setTempo(double bpm); void play(); void stop(); void render(float* const* out, int channels, int samples) noexcept; double positionBeats() const; };`

- [ ] **Step 1: Write the failing test** — at 48 kHz, 120 bpm, 4/4: `play()`, render 64 blocks of 256 frames, assert `positionBeats()` equals 32.0 ± 1e-6.
- [ ] **Step 2: Run it** — expected: FAIL.
- [ ] **Step 3: Implement** — transport owns sample position and converts to beats; `render` delegates to the engine and advances only while playing.
- [ ] **Step 4: Run it** — `ctest -R session_transport`. Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(engine): session facade with sample-accurate transport"`.

### Task 6: Plugin hosting behind an interface

**Files:**
- Create: `engine/include/nc/engine/IPluginScanner.hpp`, `engine/src/PluginScanner.cpp`
- Modify: `engine/src/PluginHost.cpp`, `engine/src/PluginSlot.cpp`

**Interfaces:**
- Produces: `struct ScanResult { std::string path; std::string name; std::string vendor; bool ok; std::string error; int64_t latencySamples; };` and `std::vector<ScanResult> scanFolder(const std::filesystem::path&, int timeoutMs, std::atomic<bool>& cancel);`

- [ ] **Step 1: Write the failing test** — scan a folder containing one non-plugin file; assert one result with `ok == false` and a non-empty error, and that the call returns within `timeoutMs + 1000`.
- [ ] **Step 2: Run it** — expected: FAIL.
- [ ] **Step 3: Implement** — wrap JUCE's out-of-process `PluginDirectoryScanner`; the VST3 SDK 3.8 hosting helpers (`VST3::Hosting::Module`, `PluginFactory`, `getModulePaths`) are MIT and may be used directly. Enforce the timeout with a watchdog; never scan in-process as a fallback. pluginval is an external CI binary only.
- [ ] **Step 4: Run it** — `ctest -R scanner`. Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(engine): scanner interface with enforced timeout"`.

### Task 7: Real-time safety gate

**Files:**
- Create: `tests/test_rt_safety.cpp`, `tests/support/AllocCounter.h`

**Interfaces:**
- Consumes: `Session::render` from Task 5.

- [ ] **Step 1: Write the failing test** — replace global `operator new`/`delete` with a thread-local counter; prepare a session with one built-in pass-through node; render 64 blocks; assert allocation count is 0.
- [ ] **Step 2: Run it** — expected: FAIL with a non-zero count.
- [ ] **Step 3: Implement** — remove the offending allocation (pre-size buffers in `prepare`, no `std::string` or `juce::var` construction inside `render`).
- [ ] **Step 4: Run it** — `ctest -R rt_safety`. Expected: PASS with count 0.
- [ ] **Step 5: Commit** — `git commit -m "test(engine): pin zero-allocation audio callback"`.

### Task 8: Offline render determinism

**Files:**
- Create: `engine/include/nc/engine/Render.hpp`, `engine/src/Render.cpp`
- Test: `tests/test_render_determinism.cpp`

**Interfaces:**
- Produces: `bool renderOffline(Session&, const std::filesystem::path& wav, double seconds);`

- [ ] **Step 1: Write the failing test** — render the same 2-second session twice to two files; assert SHA-256 of both payloads is equal and non-silent.
- [ ] **Step 2: Run it** — expected: FAIL.
- [ ] **Step 3: Implement** — offline loop at 512 frames, 32-bit float WAV via JUCE `WavAudioFormat`, no wall-clock dependence.
- [ ] **Step 4: Run it** — `ctest -R determinism`. Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(engine): deterministic offline render"`.

### Task 9: Session schema v2 and project I/O

**Files:**
- Create: `schema/session.v2.schema.json`, `engine/include/nc/engine/ProjectFile.hpp`, `engine/src/ProjectFile.cpp`
- Test: `tests/test_project_io.cpp`, `tests/fixtures/minimal.v2.json`, `tests/fixtures/future.v2.json`

**Interfaces:**
- Produces: `struct ProjectLoadResult { bool ok; std::string error; std::vector<std::string> issues; };` and `ProjectLoadResult loadProject(const std::filesystem::path&, Session&);`

- [ ] **Step 1: Write the failing tests** — (a) minimal fixture round-trips: save, load, save again, byte-identical; (b) `future.v2.json` with `schemaVersion: 3` returns `ok == false` and an error naming the version.
- [ ] **Step 2: Run them** — expected: FAIL.
- [ ] **Step 3: Implement** — nlohmann/json serialization, `version` gate before any mutation, unknown plugin ids collected into `issues` instead of throwing.
- [ ] **Step 4: Run them** — `ctest -R project_io`. Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(engine): schema v2 project load with reject-forward"`.

### Task 10: Host app and WebView shell

**Files:**
- Create: `host/CMakeLists.txt`, `host/src/Main.cpp`, `host/src/HostWindow.cpp`, `host/src/ResourceProvider.cpp`, `host/webui/` (Vite app), `tests/test_resource_provider.cpp`

**Interfaces:**
- Produces: `class ResourceProvider { std::optional<Resource> get(const std::string& path) const; };` serving `ui/index.html` for unknown routes.

- [ ] **Step 1: Write the failing test** — `ResourceProvider::get("/")` returns HTML starting with `<!DOCTYPE`; `get("/nope.png")` returns a 404-shaped result, never an exception.
- [ ] **Step 2: Run it** — expected: FAIL.
- [ ] **Step 3: Implement** — JUCE standalone app, 1280×800 window, `WebBrowserComponent` with the resource provider, frontend served from `host/webui/dist` embedded as binary data at build time.
- [ ] **Step 4: Run it** — `ctest -R resource_provider` passes; launch the app and confirm the page loads.
- [ ] **Step 5: Commit** — `git commit -m "feat(host): standalone shell with embedded WebView UI"`.

### Task 11: Generated bridge

**Files:**
- Create: `host/routes.json`, `tools/gen-routes.mjs`, `host/src/Bridge.gen.cpp`, `host/webui/src/lib/bridge.gen.ts`, `tests/test_bridge_routes.cpp`

**Interfaces:**
- Produces: C++ `juce::var dispatch(const juce::String& route, const juce::var& args)`; TS `bridge.transport.play()`, `bridge.transport.setTempo(bpm)`.

- [ ] **Step 1: Write the failing test** — `dispatch("transport.setTempo", 140.0)` changes session tempo to 140; `dispatch("nope", {})` returns an error object and does not throw.
- [ ] **Step 2: Run it** — expected: FAIL.
- [ ] **Step 3: Implement** — `routes.json` as the single source; codegen emits both files; `npm run gen:routes -- --check` fails CI when the generated files drift. If Spike 0 rejects Tracktion Engine, target JUCE 9 and replace the generator with a typed wrapper over `@juce-framework/webview`'s `withNativeFunction`; on Tracktion Engine stay on JUCE 8 and keep the generator.
- [ ] **Step 4: Run it** — `ctest -R bridge` and `npm run gen:routes -- --check`. Expected: PASS, no drift.
- [ ] **Step 5: Commit** — `git commit -m "feat(host): generated bridge from one route table"`.

### Task 12: Transport, track list and mixer screens

**Files:**
- Create: `host/webui/src/screens/Transport.tsx`, `Arrangement.tsx`, `Mixer.tsx`, `host/webui/src/state/session.ts`

**Interfaces:**
- Consumes: `bridge.transport.*` from Task 11; `bridge.session.*` routes are added by this task.

- [ ] **Step 1: Write the failing test** — React test renders Transport, presses play, asserts `bridge.transport.play` was called once and the button shows `Stop`.
- [ ] **Step 2: Run it** — `cd host/webui && npm test`. Expected: FAIL.
- [ ] **Step 3: Implement** — three screens on the NCSound kit primitives; keyboard: Space toggles transport, 1–3 switches screens; every control reachable by Tab with a 2 px `accent` focus ring.
- [ ] **Step 4: Run it** — `npm test` passes; manual check of focus order.
- [ ] **Step 5: Commit** — `git commit -m "feat(ui): transport, arrangement and mixer screens"`.

### Task 13: Plugin browser, scan and load

**Files:**
- Create: `host/webui/src/screens/Plugins.tsx`, `host/src/PluginController.cpp`
- Test: `tests/test_plugin_controller.cpp`

**Interfaces:**
- Consumes: `scanFolder` (Task 6), `dispatch` routes `plugins.scan`, `plugins.load`.

- [ ] **Step 1: Write the failing test** — with a fake scan result list, `plugins.load` on a track assigns the plugin id to that track; loading an unknown id returns an error and leaves the track unchanged.
- [ ] **Step 2: Run it** — expected: FAIL.
- [ ] **Step 3: Implement** — async scan with progress events; browser grouped by section from the manifest; loading opens the plugin's editor in a child window on double-click.
- [ ] **Step 4: Run it** — `ctest -R plugin_controller` passes; scan a real VST3 folder and confirm the list populates without freezing.
- [ ] **Step 5: Commit** — `git commit -m "feat(host): plugin browser, scan and load"`.

### Task 14: Phase 1 exit gate

**Files:**
- Create: `tests/e2e/phase1.md` (scripted manual gate), `docs/phase-1-report.md`

- [ ] **Step 1: Automate what can be** — a test that builds a project with one hosted plugin, renders offline, saves, reloads, renders again, and asserts identical hashes.
- [ ] **Step 2: Run the scripted gate** — load a real third-party VST3, play a 4-bar pattern, save, close, reopen; record the result with screenshots in `docs/phase-1-report.md`.
- [ ] **Step 3: Check the Review Focus list** — hang during scan, missing plugin on load, NaN from a plugin, no audio device, truncated project. Each must produce its defined behaviour.
- [ ] **Step 4: Commit** — `git commit -m "docs: phase 1 exit report"`.

---

## Later plans

| Plan | Milestones | Delivers |
|---|---|---|
| 2 | M3 | Recording: inputs, monitoring, punch, takes and swipe comp |
| 3 | M4–M5 | Full UI kit and the stock FX rack wired as defaults |
| 4 | M6–M8 | Rompler rebuild, Supa Level, browser tools to VST3, effect consolidation |
| 5 | M9–M10 | Hardening, installer, then Aero Amp and Open Mic |

Each plan is written when its predecessor's exit gate passes.

## Self-review notes

- Spec coverage: M0 (Tasks 1–2), M1 (Tasks 3–9), M2 (Tasks 10–14). Recording, stock rack, instruments and consolidation are deliberately deferred to later plans, matching the spec's milestone order.
- Every task names exact files, a test that fails first, and a command that proves it passes.
- Interfaces are forward-referenced once: `Session` (Task 5) is consumed by Tasks 7, 8, 9, 13; `dispatch` (Task 11) by Tasks 12–13.
