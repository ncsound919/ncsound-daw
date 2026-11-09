# Finish the Listed Work Implementation Plan

> **For agentic workers:** Native implementation in this session (no subagent tooling in this environment). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the six items the user listed: complete plugin integration, Import Audio, instrument-plugin sync tests, Mixology in the Mixer and header, anchored +/- zoom (Pro Tools parity), all under TDD.

**Architecture:** UI-first TDD per item (failing test, minimal code, green suite); native routes are added once for Import Audio + Mixology, then `npm run gen:routes`, then one native build + ctest.

**Tech Stack:** React + Vitest UI in `ui/`; JUCE/MSVC native in `app/Source`; bridge routes generated from `WebViewBridge.cpp`.

**Spec (the user's list, as reconstructed from the session):**
1. Complete plugin integration (browser -> engine load path, instrument vs master targets).
2. Test audio/file upload (Import Audio end to end).
3. Test instrument-plugin sync (pitch SYNC mirror coverage).
4. Integrate Mixology into mixing and header.
5. More Pro Tools features (scoped: +/- zoom anchors on the playhead like Pro Tools zooms on the edit cursor).
6. TDD throughout (failing test first, full suite green before done).

## Global Constraints

- Native builds run ONLY via the repo wrapper build script (no direct MSVC invocation).
- UI checks run from `ui/` with the real binary: tsc, vitest run, vite build.
- After adding/renaming a native route run `npm run gen:routes` (source of truth: `app/Source/WebViewBridge.cpp`).
- Never stage the pre-existing untracked `fix-inserts.cjs`.
- Record verified vs unverified results in `DAW_WORK_LOG.md`.

## Review Focus

- Import Audio with cancelled dialog: refusing must leave state untouched, never a half-added clip.
- Mixology preset applied with fewer tracks than the bus map expects: skips must be reported, never a silent write to track 0 (native already guarantees this; the UI test pins the report text).
- Instrument picked while the master browser is open: must refuse with a status flash, never load an instrument on the master.
- +/- zoom when the playhead is outside the window: anchor clamps to [0,1], window stays in [1,256].
- Wheel-zoom reciprocal behavior must not regress (existing `zoom.test.ts` stays green).

## Task 1: Anchor the +/- zoom (buttons and keyboard) on the playhead

**Files:**
- Modify: `ui/src/workspace/Arrangement.tsx`
- Test: `ui/src/workspace/arrange.test.tsx`

**Interfaces:**
- Consumes: existing `zoomWindow(viewStart, visibleBars, ratio, direction, MIN_BARS, MAX_BARS)` from `./format`.
- Produces: shared zoom helper inside `Arrangement.tsx` used by buttons and keys (wheel behavior unchanged).

- [ ] **Step 1: Write the failing test** — render the arrangement with a playhead mid-window, click `+`, and assert the window equals `zoomWindow(start, bars, playheadRatio, "in", 4, 256)` (not the old scale-around-start). Same for `-` with `"out"`.
- [ ] **Step 2: Run test to verify it fails** — `node node_modules/vitest/vitest.mjs run src/workspace/arrange.test.tsx` from `ui/`. Expected: FAIL.
- [ ] **Step 3: Implement the anchored zoom** — compute the ratio from the playhead bar when inside the window, else clamp to [0,1]; wire buttons and `+`/`-`/`=` keys to it.
- [ ] **Step 4: Run test to verify it passes** — same command. Expected: PASS, whole file green.
- [ ] **Step 5: Commit.**

## Task 2: Pin instrument-plugin sync with tests

**Files:**
- Test: `ui/src/workspace/Workspace.test.tsx` (extend harness), `ui/src/workspace/chrome.test.tsx` (SYNC toggle)
- Modify: none expected (characterization tests; if a test fails on current code, fix the code minimally).

**Interfaces:**
- Consumes: `Workspace` props `actions.instrumentPitch`, `pitchSync` state, `Inspector` `onPitchSync`.

- [ ] **Step 1: Write the failing tests** — (a) sync on + track-pitch change calls `actions.instrumentPitch` with the mirrored patch; (b) sync off + same change makes no call; (c) Inspector SYNC button calls `onPitchSync` with the flipped value.
- [ ] **Step 2: Run tests to verify they fail** — new assertions fail on first run.
- [ ] **Step 3: Implement the minimal fix only if a test fails on current code.**
- [ ] **Step 4: Run tests to verify they pass** — full `Workspace.test.tsx` + `chrome.test.tsx` green.
- [ ] **Step 5: Commit.**

## Task 3: Complete plugin integration (instrument vs master target guard)

**Files:**
- Modify: `ui/src/workspace/Workspace.tsx` (browser `onLoad` wrapper)
- Test: `ui/src/workspace/Workspace.test.tsx`

**Interfaces:**
- Consumes: `browserTarget` (`{track, slot} | {master} | null`), `PluginInfo.category`.
- Produces: refusal with status flash; no backend call.

- [ ] **Step 1: Write the failing test** — with the master browser open, loading an `instrument` plugin must NOT call `onMasterPluginLoad`; a status flash names the refusal. Effect plugins still load.
- [ ] **Step 2: Run test to verify it fails** — Expected: FAIL, master load called with the instrument.
- [ ] **Step 3: Implement the guard** in the `onLoad` wrapper passed to `Browser`.
- [ ] **Step 4: Run test to verify it passes.**
- [ ] **Step 5: Commit.**

## Task 4: Mixology in the Mixer and the header

**Files:**
- Modify: `app/Source/WebViewBridge.cpp` (routes `mixologyPresets`, `applyMixologyPreset`), `ui/src/lib/bridge.ts` (backend methods), `ui/src/App.tsx` (handlers), `ui/src/workspace/Mixer.tsx` (preset row), `ui/src/workspace/HeaderBar.tsx` (menu entries)
- Test: `ui/src/workspace/mixer.test.tsx` (new, focused), `ui/src/workspace/chrome.test.tsx` (menu lists the 5 presets)

**Interfaces:**
- Consumes: `nc::mixologyPresetNames()` / `nc::applyMixologyPreset(engine, index)`; generated `NativeRoute` names.
- Produces: `backend.mixologyPresets(): Promise<string[]>`, `backend.applyMixologyPreset(index): Promise<AppState>`; `onApplyMixology(index)` action.

- [ ] **Step 1: Write the failing UI tests** — Mixer shows the 5 preset names and applying one calls the backend and flashes the report text; header File menu lists the presets (mocked backend, no native needed).
- [ ] **Step 2: Run tests to verify they fail** — Expected: FAIL, methods/routes do not exist.
- [ ] **Step 3: Implement natively** — add both routes in `WebViewBridge.cpp` following the `scanPlugins` pattern; run `npm run gen:routes`; add `backend` methods, App handlers, Mixer row, HeaderBar entries.
- [ ] **Step 4: Run tests to verify they pass** — new tests + full UI suite green; routes check clean.
- [ ] **Step 5: Commit.**

## Task 5: Import Audio end to end

**Files:**
- Modify: `app/Source/WebViewBridge.cpp` (route `importAudio`), `ui/src/lib/bridge.ts`, `ui/src/App.tsx`, `ui/src/workspace/HeaderBar.tsx` (enable the menu item when the handler exists)
- Test: `ui/src/workspace/chrome.test.tsx` (menu enabled, calls backend, status shows the new clip); update the old assertion that Import Audio is disabled.

**Interfaces:**
- Consumes: `AudioEngine::loadAudioFile` + the clip-placement path the `transcribeClip` route uses; selected track + playhead bar from state.
- Produces: `backend.importAudio(): Promise<AppState>`; cancelling the dialog leaves state untouched.

- [ ] **Step 1: Write the failing UI tests** — Import Audio menu item is enabled when `onImportAudio` exists and calls it on click; keep a test that the item is hidden/disabled without a handler.
- [ ] **Step 2: Run tests to verify it fails** — Expected: FAIL, item disabled with no handler prop.
- [ ] **Step 3: Implement natively + UI** — `importAudio` route (FileChooser audio patterns, cancel-safe, clip placed on the selected track at the playhead, returns state); regen; `backend.importAudio`; App handler; HeaderBar wiring.
- [ ] **Step 4: Run tests to verify they pass** — full UI suite green.
- [ ] **Step 5: Commit.**

## Task 6: Verify everything and record it

- [ ] **Step 1: UI gate** — `tsc -b --noEmit` exit 0; full `vitest run` green; `vite build` exit 0.
- [ ] **Step 2: Native gate** — wrapper build exit 0; `ctest --test-dir build -j 4` (record counts even if some fail; failures go in the log by name).
- [ ] **Step 3: Desktop smoke** — relinked exe boots; CDP readout confirms the arrangement loads.
- [ ] **Step 4: Work log** — append verified vs unverified per item to `DAW_WORK_LOG.md`.
- [ ] **Step 5: Commit.**

## Self-Review

1. **Spec coverage:** items 1-6 each have a task (TDD is the method inside every task). Native route additions are covered by UI tests with mocked backend plus the regen check, build, and ctest.
2. **Step scan:** each step is one action with a checkable result.
3. **Type consistency:** route names (`mixologyPresets`, `applyMixologyPreset`, `importAudio`) are used identically in native, generated, bridge, and UI steps.
4. **Review Focus:** each of the five risks names its pinning test in the owning task.
5. **Proportion:** plan states interfaces and assertions, not bodies.
