# Phase 2 Context — Hosted Track Inserts & Session Integrity

**Milestone:** M2 hosting/project half (pattern playback is deferred to Phase 4)
**Depends on:** current native build and test baseline
**Plan:** [`PHASE-02-PLAN.md`](./PHASE-02-PLAN.md)

---

## 1. Problem

The mixer shows four user insert slots per track, but they are static labels from `Session.h`. The browser’s load callback only publishes a status message. `PluginHost` is a single master-only instance and scans synchronously in-process. `ProjectDocument` stores only `masterInsert`, so there is nowhere to persist a track plugin.

This phase makes the visible insert slots real and makes the project durable. It is the prerequisite for monitoring through effects during recording and for the stock suite.

---

## 2. Locked decisions

### 2.1 Hosting model

- **Effects only** in this phase: audio in, audio out, stereo. Instruments, MIDI routing and side-chain buses are Phase 4.
- **Four user insert slots** remain, numbered 1–4 in the UI and `0–3` in code. Slot 0 in the UI is the baked native channel strip (`NAT`) and is not a plugin slot.
- **Pre-fader order:** clip render → user inserts → baked channel strip → fader/pan → bus sum. The master path is bus sum → master rack → master insert → monitor. This changes the current post-fader rack order deliberately; the old order makes compressor thresholds move with the fader.
- **One instance per loaded slot.** No shared plugin instances between tracks.
- **Track chain latency is the sum of non-bypassed insert latencies.** Track-level PDC delays every shorter track by `worstTrackLatency - ownLatency`. A bypassed plugin is removed from the chain and its latency no longer counts.
- **Compensation cap:** 16,384 samples. If the worst track exceeds it, the host clamps the compensation, reports the over-budget slot, and does not silently drift.

### 2.2 Lifetime and real-time safety

- Plugin instances are **created, prepared, saved and destroyed on the message thread**.
- The audio thread uses **raw pointers** in active slots. It never increments/decrements a shared pointer, allocates, frees, locks or touches the filesystem.
- A fixed-capacity **SPSC command FIFO** carries `replace`, `remove` and `bypass` commands. The audio thread drains it at block start.
- A **retire FIFO** returns replaced/removed pointers to the message thread. The message thread deletes them on its next poll. The old pointer is written to the retire FIFO when it is swapped out, before any processing with the new pointer, so deletion after dequeue is safe.
- Only one pending load per slot. A second load request while one is pending is rejected with a visible “still loading” status, not queued indefinitely.
- Plugin creation uses `AudioPluginFormatManager::createPluginInstanceAsync`; its callback runs on the message thread. A request token prevents a late callback from installing a plugin into a slot the user has since re-targeted.
- Transport state for `AudioPlayHead` is published through atomics. The current `PluginHost::getPosition()` uses a `CriticalSection`; that lock is removed from the audio path.

### 2.3 Scanner

- Scanning uses `juce::KnownPluginList` + `juce::PluginDirectoryScanner` on a background thread.
- `KnownPluginList::setCustomScanner()` installs a `CustomScanner` backed by a **child process**, following JUCE’s own `AudioPluginHost` pattern:
  - `ChildProcessCoordinator` in the editor process.
  - `ChildProcessWorker` in the same executable, selected by `initialiseFromCommandLine()` before the main window is created.
- Each file gets a **total timeout** (10 s default) and a kill on timeout. The reference host’s 50 ms poll is not a total timeout and is not copied.
- A timeout marks that file failed, keeps the UI responsive, and continues with the next file. A crashed worker is restarted for the next file.
- Scanning never loads plugin code in the editor process. **Processing still runs in-process**; runtime plugin crashes are a Phase 6 concern and are not claimed as sandboxed.

### 2.4 Project state

- `projectSchemaVersion` becomes **2**.
- `TrackState` gains four insert slots. Each `PluginSlotState` stores:
  - `identifier` (compatibility/fallback),
  - `descriptionXml` (full `PluginDescription` XML, primary restore path),
  - `format`,
  - `stateBase64`,
  - `bypassed`.
- Version 1 projects load with empty insert slots and save as version 2. Versions above 2 are refused.
- Missing plugins are **retained as metadata**: description XML (carrying name/manufacturer), identifier and saved state stay in the slot; the UI shows `MISSING`, and a later save preserves the slot.
- State blobs are capped (2 MB per slot) and sanitized. Empty identifiers are dropped. No unbounded string from a project reaches a plugin.

### 2.5 UI

- The browser lists both registry entries and scanned third-party descriptions through a single `PluginCatalog`.
- Only loadable external **effects** show an enabled `LOAD ON TRACK` button in this phase. Native/archived/instrument entries show an honest disabled reason; they do not pretend to load.
- ChannelStrip and Inspector insert labels come from the engine, not from `Session.h`. The static `userInsertAt()` names remain only as a fallback for a never-loaded slot.
- Slot context menu: **Bypass**, **Remove**, **Replace**. Bypass state is visible in the slot. Remove clears the slot; it does not delete the plugin file.
- Loading, bypassing, removing and missing-plugin states must be visible without reopening the window.

---

## 3. Invariants the implementation must preserve

- Audio callback remains sample-accurate for existing clips; tempo, loop, metronome and transport behavior do not change.
- Master insert behavior and master monitor section (dim/mono) remain intact.
- No new dependency outside JUCE and the existing Catch2/CMake setup.
- No change to the project’s public audio assets or demo media.
- Existing v1 project files remain loadable.
- If the audio device is absent, the editor still opens and save/load still works.
- The build script and test targets remain the verification entry points; new tests are added to the existing targets rather than a parallel harness.

---

## 4. Out of scope

- Recording, input monitoring, punch and takes (Phase 3).
- MIDI clips, pattern editor, instruments, keyboard (Phase 4).
- Stock VST3 builds and default stock chains (Phase 5).
- Runtime plugin sandboxing, pluginval, installer and clean-VM testing (Phase 6).
- Aero Amp / Open Mic (M10, post-v1).
- macOS/Linux, AU/ARA/CLAP, side-chain routing, multi-bus plugins.

---

## 5. Files expected to change

| Area | Files |
|---|---|
| Schema/model | `engine/include/nc/engine/ProjectState.hpp`, `ProjectJson.hpp`; `vstdaw.schema.json` only if the separate design schema is deliberately converged |
| Tests | `tests/test_project_state.cpp`, `tests/test_project_json.cpp`, `app/Source/MixRackTests.cpp` or new `app/Source/TrackInsertTests.cpp` |
| Catalog/scanner | new `app/Source/PluginCatalog.*`, new `app/Source/PluginScanner.*`, `app/Source/Main.cpp` |
| Hosting | `app/Source/PluginHost.*` (split/replace), new `app/Source/HostedInsert.*`, new `app/Source/TrackInsertRack.*` |
| Engine | `app/Source/AudioEngine.*` |
| UI | `app/Source/BrowserOverlay.*`, `ChannelStrip.*`, `InspectorPanel.*`, `MixerPanel.*`, `Widgets.*`, `MainComponent.*`, `Session.h` |
| Build | `app/CMakeLists.txt`, possibly `CMakeLists.txt` |
| Docs | this folder, `docs/UI-REVIEW.md` status note after implementation |

---

## 6. Definition of done for this phase

- [ ] Four real insert slots per track, each capable of hosting a scanned VST3 effect.
- [ ] Audio flows through the chain in the documented order; bypass/remove are live.
- [ ] PDC aligns two tracks with different plugin latencies within one sample.
- [ ] Save/reopen restores plugin identity and state; missing plugins are retained and reported.
- [ ] Scanner runs out-of-process, times out, and leaves the editor responsive.
- [ ] Master insert uses the same host path and remains functional.
- [ ] No host-owned allocation/free/lock on the audio callback; a test or instrumentation check demonstrates it.
- [ ] Existing v1 projects open; new projects save as v2.
- [ ] Tests and manual gate in `PHASE-02-PLAN.md` pass.
