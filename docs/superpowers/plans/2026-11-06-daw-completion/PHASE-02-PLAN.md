# Phase 2 Plan — Hosted Track Inserts & Session Integrity

**Status:** Ready for approval. **No implementation has been started.**
**Milestone:** M2 hosting/project half (pattern playback is deferred to Phase 4)
**Depends on:** current native build and test baseline
**Context:** [`PHASE-02-CONTEXT.md`](./PHASE-02-CONTEXT.md)
**Research:** [`../../research/2026-11-06-daw-completion-research.md`](../../research/2026-11-06-daw-completion-research.md)

---

## 0. Goal and exit criteria

### Goal

Make the four visible user insert slots on every track real: load a scanned VST3 effect, process audio through it in a fixed pre-fader chain, bypass/remove it, save its state, reopen it, and retain it as `MISSING` when the plugin cannot be found. The master insert must use the same host path and must be safe to replace while audio is running.

### Exit criteria

- [ ] A real external VST3 effect loads on any of slots 1–4 on any of tracks 1–16.
- [ ] The effect processes the track’s audio; the track remains in sync with other tracks after PDC.
- [ ] Bypass and remove work while transport is running.
- [ ] Loading/replacing while transport is running does not crash, block the audio callback, or leave a dangling pointer.
- [ ] Save/reopen restores plugin identity, state and bypass.
- [ ] A missing plugin is shown as `MISSING`, retains its saved state, and is preserved on the next save.
- [ ] The scanner runs out-of-process with a total timeout; a hung worker is killed and the editor remains responsive.
- [ ] Master insert still works and uses the same host abstraction.
- [ ] No host-owned allocation, free, lock or file I/O occurs inside the audio callback.
- [ ] Version 1 projects still open; new projects save as version 2.
- [ ] Automated and manual gates in §12 pass.

### Non-goals

Recording, monitoring, MIDI/pattern playback, instruments, stock VST3 builds, runtime plugin sandboxing, and M10 products. See `PHASE-02-CONTEXT.md` §4.

---

## 1. Architecture

### 1.1 Signal flow

```
clips → per-track block buffer
      → insert 1 → insert 2 → insert 3 → insert 4
      → per-track PDC delay (worstTrackLatency − ownLatency)
      → baked native channel strip
      → fader + pan
      → bus sum
      → master rack
      → master insert
      → monitor (dim/mono)
      → device outputs
```

PDC is applied at block level inside `TrackInsertRack::processTrack`, after the plugin chain and before the native strip. Delay is linear, so this is equivalent to delaying after the fader for v1’s non-automated faders; it keeps the delay line and its tests in one object. The baked strip is treated as zero-latency; if it gains latency later, fold that into `chainLatency`.

This deliberately moves the baked strip before the fader. The current code runs the rack after the fader, which makes compressor/EQ behavior depend on fader position. The change is part of the phase and must be A/B checked.

### 1.2 Object ownership

```
Message thread                         Audio thread
--------------                         ------------
PluginCatalog                          TrackInsertRack
  KnownPluginList                        active raw pointers
  AudioPluginFormatManager               command FIFO (drain at block start)
  scanned descriptions                   retire FIFO (push swapped-out ptr)

InsertSlot (message-owned)
  owned: unique_ptr<InsertProcessor> ──raw pointer──▶ active
  saved: PluginSlotState
  missing: bool
```

- Plugin instances are created, prepared, state-saved and destroyed on the message thread.
- The audio thread only reads/writes raw pointers and atomics.
- A replaced/removed pointer is moved to `retiredOwned` on the message thread and pushed to the retire FIFO by the audio thread when it is swapped out. The message thread deletes it after dequeue.
- `retiredOwned` keeps the object alive between the message-thread move and the audio-thread swap. It is not deleted until the retire event is observed.

### 1.3 Latency

For each track:

```
chainLatency(track) = Σ latency(active, non-bypassed insert)
worstTrackLatency   = max(chainLatency(track)) over active tracks
trackDelay(track)   = clamp(worstTrackLatency − chainLatency(track), 0, 16384)
```

- `LatencyComp::compensationDelay()` supplies the math.
- Delay lines are prepared with 16,384 samples of capacity per track.
- If `worstTrackLatency` exceeds the cap, compensation clamps and the rack reports `overBudget`; the UI shows a warning. It must not silently drift.
- Master insert latency is global and reported; relative track alignment is unaffected because all tracks pass through the master together.

### 1.4 Command protocol

```cpp
enum class InsertCommandType { replace, remove, bypass };

struct InsertCommand
{
    InsertCommandType type;
    int track;          // 0..15
    int slot;           // 0..3
    InsertProcessor* processor; // null for remove
    bool bypassed;
    uint32_t generation;        // request token, echoed in the retire event
};
```

Retire events carry `{ type, track, slot, old, generation }`. A null `old` is still an ack for loading into an empty slot; a `remove` event tells the message thread to clear the saved metadata.

- Fixed-capacity SPSC FIFO (1024 entries).
- Audio thread drains at the top of the callback, before rendering.
- Replace/remove pushes the old pointer to the retire FIFO; bypass only flips an atomic.
- One pending load per slot. A second request while pending is rejected visibly.
- The UI never calls a plugin’s `processBlock` or `prepareToPlay` directly.

---

## 2. Preconditions

### Task 0 — Baseline snapshot and proof

**Files:** none (repository state)

- [ ] Record `git status --short` and `git diff --stat`; do not reset or discard the existing uncommitted work.
- [ ] After approval, create a reversible local baseline (branch or commit) containing the current UI/hosting work.
- [ ] Run the existing gate and record the counts:

```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```

- [ ] Confirm these binaries exist:
  - `build/app/NCSoundHub_artefacts/Debug/NCSound Music Hub.exe`
  - `build/app/NCMixRackTests_artefacts/Debug/NCMixRackTests.exe`
  - `build/tests/nc_tests.exe`

**Stop if:** the baseline does not build or either test binary fails. Do not refactor on a broken baseline.

---

## 3. Task 1 — Project schema v2

**Objective:** Persist four insert slots per track without breaking v1 projects.

**Files:**

- `engine/include/nc/engine/ProjectState.hpp`
- `engine/include/nc/engine/ProjectJson.hpp`
- `tests/test_project_state.cpp`
- `tests/test_project_json.cpp`
- `vstdaw.schema.json` (optional documentation; see the note below)

### Step 1 — Write failing tests

- [ ] v1 JSON fixture loads, `version == 1`, every track has four empty inserts.
- [ ] v2 document with `descriptionXml`, identifier, format, state and bypass round-trips exactly.
- [ ] A track with an oversized insert array truncates to four; extra entries are ignored.
- [ ] A slot with neither `identifier` nor `descriptionXml` is dropped; a slot with `descriptionXml` but no identifier is retained (the XML carries the name/manufacturer); a slot with only `stateBase64` is dropped.
- [ ] `stateBase64` and `descriptionXml` above 2 MB are cleared, not loaded.
- [ ] `version > 2` is still parsed and `isLoadable()` returns false.
- [ ] `version == 1` is loadable and saves as version 2.
- [ ] Sanitizing twice equals sanitizing once with inserts.

### Step 2 — Run the failing tests

```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```

Expected: new tests fail because `TrackState::inserts` does not exist.

### Step 3 — Implement

- [ ] `projectSchemaVersion = 2`.
- [ ] Add `std::string descriptionXml` to `PluginSlotState`.
- [ ] Update `PluginSlotState::isEmpty()` to return true only when both `identifier` and `descriptionXml` are empty; otherwise a description-only slot would be treated as empty.
- [ ] Add `std::array<PluginSlotState, 4> inserts {}` to `TrackState`; include `<array>`.
- [ ] `toJson`: write `"inserts":[...]` inside each track, including empty slots (stable shape) or only non-empty slots (smaller). **Decision:** write four entries for stable diffs and simple restore; omit `stateBase64` when empty.
- [ ] `fromJson`: read up to four entries, default missing entries to empty; ignore extra.
- [ ] `sanitize`: clamp/clear oversized blobs; drop slots with neither identifier nor description XML; preserve bypass.
- [ ] Keep `isLoadable` accepting versions 1 through 2.
- [ ] Do **not** silently treat `vstdaw.schema.json` as the project format. It currently describes a separate v1 manifest/session/config model; its `schemaVersion` is not `ProjectDocument::version`. The C++ `ProjectJson` remains the executable contract. Either add a dedicated project schema or document the divergence. Converging the two formats is a separate task, not a hidden side effect of this phase.

### Step 4 — Verify

```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```

- [ ] All `nc_tests` pass.
- [ ] Existing v1 project fixtures open and re-save as v2.
- [ ] `NCMixRackTests` still pass.

**Acceptance:** schema tests pass; no UI or hosting code changed yet.

---

## 4. Task 2 — Plugin catalog and out-of-process scanner

**Objective:** Replace the synchronous in-process scan in `PluginHost` with a catalog that scans through a child process and publishes a snapshot to the UI.

**Files:**

- New `app/Source/PluginCatalog.h/.cpp`
- New `app/Source/PluginScanner.h/.cpp`
- `app/Source/Main.cpp`
- `app/CMakeLists.txt`
- `CMakeLists.txt` or a new `app/hosting.cmake` if the target is split
- New `app/Source/PluginHostTests.cpp` (or `PluginCatalogTests.cpp`)
- `build/_verify_mixrack.cmd`

### Step 1 — Build target first

Create a static `nc_hosting` library shared by the app and a console test target. Start it with only the files that exist for this task; add the hosting sources as their tasks land:

```
nc_hosting
  PluginCatalog.cpp       (Task 2)
  PluginScanner.cpp       (Task 2)
  HostedInsert.cpp        (added in Task 3)
  TrackInsertRack.cpp     (added in Task 4)
links: nc_engine, juce_audio_processors, juce_audio_basics,
       juce_events, juce_core
```

Apply `JUCE_PLUGINHOST_VST3=1` to `nc_hosting` as a public compile definition; JUCE modules are compiled per target, so the app’s definition does not propagate through the static library.

Add `NCPluginHostTests` (`juce_add_console_app`) linking `nc_hosting` and `Catch2::Catch2WithMain`. Add both to `_verify_mixrack.cmd`. Do not add `HostedInsert.cpp` or `TrackInsertRack.cpp` to the target before those files exist; build the catalog/scanner slice first.

Do not put GUI code in `nc_hosting`; the catalog must be testable without a window.

### Step 2 — Catalog API

Define a single row type used by the browser:

```cpp
struct CatalogEntry
{
    juce::String id;             // registry id or description identifier
    juce::String name;
    juce::String manufacturer;
    juce::String category;
    PluginStatus status;
    bool external = false;
    bool isInstrument = false;
    juce::PluginDescription description;
};
```

`PluginCatalog`:

```cpp
class PluginCatalog
{
public:
    // A plain copy keeps the catalog independent of the GUI registry object.
    explicit PluginCatalog (std::vector<PluginInfo> registryEntries,
                            std::unique_ptr<ScannerBackend> = {},
                            std::chrono::milliseconds timeout = std::chrono::milliseconds { 10000 });

    std::vector<CatalogEntry> snapshot() const;
    std::optional<CatalogEntry> find (const juce::String& id) const;

    void scanAsync (std::function<void (float, juce::String)> progress,
                    std::function<void (bool, juce::String)> finished);
    void cancelScan();
    bool isScanning() const;

    void createAsync (const PluginDescription&,
                      double sampleRate, int blockSize,
                      std::function<void (std::unique_ptr<juce::AudioPluginInstance>,
                                          juce::String)> callback);

private:
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::vector<CatalogEntry> entries;
    // registry entries are merged with known.getTypes() after each scan
};
```

`PluginInfo` is a plain struct; the catalog does not link `PluginRegistry.cpp` or any GUI code. `MainComponent` passes a copy of `registry.plugins`.

`PluginCatalog` must join/cancel its scan thread and kill its worker in its destructor. Reject a second `scanAsync` while one is running with a visible status; do not run two `KnownPluginList` scans concurrently.

- `createAsync` wraps `AudioPluginFormatManager::createPluginInstanceAsync`; the callback is already on the message thread.
- `snapshot()` returns a copy; the UI never reads `KnownPluginList` concurrently with a scan.
- Merge rules:
  - External scanned descriptions are always `external = true`.
  - Registry entries keep their `PluginStatus`.
  - Deduplicate by normalized `(pluginFormatName, identifier)`; if two descriptions have the same name/manufacturer/format, keep the one with the later `lastFileModTime` and report a duplicate count.
  - A registry entry that represents a native/baked module is **not** marked loadable as a VST3.
- Do not claim stock entries are external; `external` controls whether `LOAD ON TRACK` is enabled in this phase.

### Step 3 — Scanner implementation

Use JUCE’s AudioPluginHost pattern, not an in-process loop. Put the process boundary behind a small test seam:

```cpp
struct ScanResult
{
    bool ok = false;
    bool timedOut = false;
    juce::String descriptionsXml;
    juce::String error;
};

class ScannerBackend
{
public:
    virtual ~ScannerBackend() = default;
    virtual ScanResult scan (const juce::String& formatName,
                             const juce::String& fileOrIdentifier) = 0;
    virtual void cancel() = 0;
};
```

- `ChildProcessScannerBackend` is the production implementation and owns `ScannerSuperprocess`.
- `FakeScannerBackend` in tests returns XML, a timeout, or a crash without launching a process. This is how the timeout/cancel logic is tested automatically.
- `PluginScannerSubprocess` (in `PluginScanner.cpp`, declared in `PluginScanner.h`):
  - derives from `juce::ChildProcessWorker` and `juce::AsyncUpdater`;
  - `initialiseFromCommandLine(commandLine, processUID)`;
  - `handleMessageFromCoordinator` reads `(formatName, fileOrIdentifier)`;
  - finds the matching `AudioPluginFormat` from `addDefaultFormatsToManager`;
  - calls `findAllTypesForFile`, serializes each `PluginDescription` to XML, sends the XML back;
  - `handleConnectionLost` quits the process.
- `ScannerSuperprocess` inside `PluginScanner.cpp`:
  - derives from `juce::ChildProcessCoordinator`;
  - launches `File::getSpecialLocation(File::currentExecutableFile)` with the same `processUID`;
  - sends the scan request, waits on a condition variable with a **total deadline** (default 10 s, injectable for tests);
  - on timeout calls `killWorkerProcess()`, returns `timeout`, and allows the next file to launch a fresh worker.
- `PluginCatalog::scanAsync`:
  - runs on a `juce::Thread` (not the message thread);
  - installs `known.setCustomScanner(std::make_unique<CustomPluginScanner>(...))`;
  - uses `juce::PluginDirectoryScanner` over default VST3 locations;
  - calls `scanNextFile(true, name)` until done/cancelled;
  - posts progress and completion to the message thread with `MessageManager::callAsync`;
  - writes the dead-man’s-pedal file under the app data directory.
- `Main.cpp`:
  - before constructing the main window, create `PluginScannerSubprocess`;
  - if `initialiseFromCommandLine(commandLine, scannerProcessUID)` returns true, store it and return (worker mode);
  - otherwise continue to the normal GUI.

**Timeout test hooks:** in debug builds only, read `NC_SCANNER_TEST_HANG_MS` (worker sleeps before scanning) and `NC_SCANNER_TIMEOUT_MS` (shortens the coordinator deadline). Use them during the manual app test in §12 to prove the real child-process kill path. Do not ship either behavior in release; the automated timeout test uses `FakeScannerBackend`.

### Step 4 — Tests

- [ ] Catalog merge: registry-only list, scanned-only list, duplicate names, duplicate identifiers.
- [ ] `PluginDescription` XML round-trip preserves name, manufacturer, format, fileOrIdentifier, uniqueId and category.
- [ ] A non-plugin file produces no entries and a failed-file count.
- [ ] Cancelled scan stops before the next file and reports `cancelled`.
- [ ] Fake-backend timeout: `FakeScannerBackend` reports `timedOut`; the catalog marks the file failed, remains usable, and accepts the next file.
- [ ] Fake-backend crash: `FakeScannerBackend` reports a lost connection; the catalog restarts the backend for the next file.
- [ ] Scanning while the UI timer polls snapshot does not race (run under a debug assertion; no `KnownPluginList` direct access from UI).
- [ ] Real child-process timeout is a manual gate in §12, not an automated test: run the app with the debug hook `NC_SCANNER_TEST_HANG_MS=2000` and a short timeout; verify the worker is killed and the editor stays responsive. Do not claim the child-process path is automatically covered by the fake backend.

### Step 5 — Verify

```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```

**Acceptance:** no in-process `findAllTypesForFile` remains in the editor path; browser can receive a snapshot; timeout test passes.

**Risk checkpoint:** if the child-process worker cannot be made to launch reliably with the GUI executable, stop and report. Do not fall back to in-process scanning silently.

---

## 5. Task 3 — HostedInsert and real-time-safe transport

**Objective:** One plugin instance wrapper that owns state, latency, bypass and a lock-free transport snapshot.

**Files:**

- New `app/Source/HostedInsert.h/.cpp`
- `app/Source/PluginHost.h/.cpp` (split/remove)
- `app/Source/PluginCatalog.*`
- New tests in `app/Source/PluginHostTests.cpp`

### Interface

```cpp
class InsertProcessor
{
public:
    virtual ~InsertProcessor() = default;
    virtual void prepare (double sampleRate, int blockSize) = 0;
    virtual void process (juce::AudioBuffer<float>&, const engine::TransportBlock&) = 0;
    virtual int latencySamples() const = 0;
    virtual void setBypassed (bool) = 0;
    virtual bool isBypassed() const = 0;
    virtual juce::String name() const = 0;
    virtual bool getState (engine::PluginSlotState&) const = 0;
    virtual bool setState (const engine::PluginSlotState&) = 0;
};
```

- `HostedInsert` owns `std::unique_ptr<juce::AudioPluginInstance>` and implements `InsertProcessor`.
- Async creation callbacks carry a `std::shared_ptr<LoadContext>` with an atomic `cancelled` flag. `AudioEngine` and `PluginCatalog` destructors set it; a late callback observes it and deletes the instance on the message thread. Do not capture a raw `this` in a callback that can outlive the engine.
- `HostedInsert::adopt` validates:
  - instance is not null;
  - it is an effect, not an instrument (`getBusCount(true) > 0`);
  - a stereo input/output bus layout can be set; otherwise return a clear error.
- `prepare` calls `setRateAndBufferSizeDetails`, `prepareToPlay`; no audio-thread call.
- `process`:
  - writes the block’s transport snapshot to an atomic `TransportPlayHead`;
  - calls `processBlock` with an empty `MidiBuffer` (effects only);
  - scans output for non-finite samples; zeroes them and marks `sanitized`;
  - never allocates, locks or frees host-owned memory.
- `getState` / `setState`:
  - `descriptionXml` from `description.createXml()->toString()`;
  - `stateBase64` from `getStateInformation`;
  - 2 MB caps;
  - `setState` parses base64 and calls `setStateInformation` only when the instance exists.

### Transport

Replace `PluginHost`’s `CriticalSection blockLock` with a `TransportPlayHead`:

```cpp
struct TransportSnapshot
{
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> ppq { 0.0 };
    std::atomic<double> ppqBarStart { 0.0 };
    std::atomic<int64_t> timeSamples { 0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> looping { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 0.0 };
};
```

`getPosition()` loads atomics only. No lock on the audio thread. Set `sampleRate` in `prepare`; compute `timeInSeconds` as `timeSamples / sampleRate`. This fixes the current hardcoded 48 kHz conversion in `PluginHost::getPosition()`, which is wrong at 44.1/96 kHz.

### Tests

- [ ] Fake `AudioPluginInstance`/`InsertProcessor` state round-trip.
- [ ] Bypass toggles and latency reporting.
- [ ] State blob above 2 MB is rejected/truncated.
- [ ] `TransportPlayHead::getPosition()` returns the current block’s tempo/ppq/time without a lock (single-threaded test).
- [ ] At 44.1 kHz and 96 kHz, `timeInSeconds` equals `timeSamples / sampleRate`; no hardcoded 48 kHz remains.
- [ ] A fake processor returning NaN yields finite output and `sanitized == true`.
- [ ] Unsupported bus layout returns an error, not a crash.

**Acceptance:** one instance wrapper works for master and track slots; no `CriticalSection` in the audio path.

---

## 6. Task 4 — Insert slots, command rail and ownership

**Objective:** Swap instances while audio runs without allocation/free on the callback.

**Files:**

- New `app/Source/TrackInsertRack.h/.cpp`
- New tests in `app/Source/PluginHostTests.cpp`

### Slot structure

```cpp
class InsertSlot
{
public:
    // message thread
    bool requestReplace (std::unique_ptr<InsertProcessor>, juce::String& error);
    bool requestRemove (juce::String& error);
    void setBypassed (bool);

    // audio thread
    InsertProcessor* active() const noexcept;
    void applyCommand (const InsertCommand&) noexcept;
    void pushRetired (InsertProcessor*) noexcept;

    // message thread
    void pollRetired();
    bool isMissing() const;
    engine::PluginSlotState savedState() const;
};
```

- `owned` is a `std::unique_ptr<InsertProcessor>` on the message thread.
- `active` is `std::atomic<InsertProcessor*>`.
- `retiredOwned` is a `std::vector<std::unique_ptr<InsertProcessor>>` on the message thread.
- `requestReplace`:
  1. reject only if `pending`; a `MISSING` slot is replaceable, and a successful replace clears `missing`;
  2. check the command FIFO has room; if not, return `host busy` before moving anything;
  3. enqueue `replace` with the new raw pointer and a monotonically increasing `requestGeneration`;
  4. move the current `owned` (which may be null) into `retiredOwned`; it stays alive while the audio thread still points at it;
  5. set `owned = new`, clear `missing`, and set `pending = true`;
  6. do not delete the old processor; it remains in `retiredOwned` until the audio thread acknowledges the swap.
- `applyCommand` (audio):
  1. `old = active.exchange(new)` for replace;
  2. push `RetireEvent { track, slot, old, generation }` to the retire FIFO, **even when `old == nullptr`**; this is the ack for a load into an empty slot;
  3. never delete or touch the message-thread owned pointers.
- `pollRetired` (message):
  - pop retire events; erase the matching pointer from `retiredOwned` (ignore null);
  - if the event’s generation equals the slot’s request generation, clear `pending`;
  - for a `remove` event, clear `savedState` and `missing`; the description/identifier are part of `savedState`, so no separate name field is needed;
  - this is the only destruction point.
- `requestRemove` uses the same FIFO-capacity check and path with `new = nullptr`; the old pointer is retired, `owned` is moved to `retiredOwned`, and the saved metadata is cleared only after the ack.
- `bypass` is an atomic store; no command needed.
- FIFO overflow: fixed 1024; if full, the message thread refuses the request and reports `host busy`. Never drop a pointer on the floor.

### Tests

- [ ] Replace a slot while `process` is called repeatedly; old object is retired exactly once and destroyed only after `pollRetired`.
- [ ] Remove a slot; the chain goes dry; the old pointer is retired.
- [ ] Bypass does not replace the pointer.
- [ ] 1,000 replace cycles do not leak or double-free (constructor/destructor counters on the fake).
- [ ] FIFO-full path refuses the request and leaves the active slot unchanged.
- [ ] `pollRetired` from the message thread is the only destruction path.

**Acceptance:** a lifetime test proves no use-after-free and no destruction on the audio thread.

---

## 7. Task 5 — Track chains, PDC and NaN containment

**Objective:** Four slots per track, correct alignment, finite output, and a testable processing unit independent of the audio device.

**Files:**

- `app/Source/TrackInsertRack.h/.cpp`
- `engine/include/nc/engine/LatencyComp.hpp` (use, don't rewrite)
- New tests in `app/Source/PluginHostTests.cpp`

### Chain API

```cpp
class TrackInsertRack
{
public:
    static constexpr int maxTracks = nc::limits::maxTracks;
    static constexpr int slotsPerTrack = 4;
    static constexpr int maxCompensationSamples = 16384;

    void prepare (double sampleRate, int maxBlockSize, int activeTracks);
    void setActiveTrackCount (int activeTracks);
    void beginBlock();
    void processTrack (int track, juce::AudioBuffer<float>& buffer,
                       const engine::TransportBlock& block);
    void processMaster (juce::AudioBuffer<float>& buffer,
                        const engine::TransportBlock& block);

    int chainLatency (int track) const;
    int worstTrackLatency() const;
    bool overBudget() const;
    InsertSlot& slot (int track, int slotIndex);
};
```

- Chain processing order is slot 0 → 3.
- `latencySamples()` sums only active, non-bypassed processors; clamp each to `maxCompensationSamples`.
- `beginBlock` computes `worstTrackLatency` once from the active chains; the engine calls it once per callback.
- `processTrack`:
  1. run each active processor;
  2. sanitize non-finite samples after each stage;
  3. if the processor produces NaN/Inf, zero those samples and increment a diagnostic counter; do not let them reach the sum;
  4. apply `worstTrackLatency − chainLatency(track)` through the track’s delay line.
- `processMaster` runs the master slot; master latency is reported separately.
- `prepare` allocates all delay lines and views once; `process` never allocates.

### PDC helper usage

Use:

```cpp
const auto worst = nc::engine::totalLatency (latencies.data(), count);
const auto delay = nc::engine::compensationDelay (worst, ownLatency);
```

- If `worst > maxCompensationSamples`, set `overBudget` and clamp each delay to the cap.
- Test with two tracks: fake insert A has 0 latency, fake insert B has 64; feed an impulse at sample 0 through both; after delay, both impulses are at sample 64.
- Test bypass: bypass B; A is no longer delayed; B passes dry at sample 0.
- Test remove: chain latency drops, PDC recomputes.
- Test NaN: fake processor writes NaN/Inf; output is finite; diagnostic counter increments.
- Test over-budget: fake reports 20,000; `overBudget` is true, delay clamped, no crash.

**Acceptance:** PDC tests pass; no NaN reaches the bus; chain processing is allocation-free for the fake.

---

## 8. Task 6 — AudioEngine integration and master migration

**Objective:** Route real track audio through the chains and replace the master-only `PluginHost`.

**Files:**

- `app/Source/AudioEngine.h/.cpp`
- `app/Source/PluginHost.h/.cpp` (remove after migration)
- `app/Source/MainComponent.h/.cpp` (call sites)
- `app/Source/PluginHostTests.cpp` / new `AudioEngineBlockTests.cpp`

### Engine API to add

```cpp
// track slots, 0..3
bool loadTrackInsert (int track, int slot, const juce::PluginDescription&, juce::String& error);
bool restoreTrackInsert (int track, int slot, const engine::PluginSlotState&, juce::String& error);
void unloadTrackInsert (int track, int slot);
void setTrackInsertBypassed (int track, int slot, bool);
juce::String trackInsertName (int track, int slot) const;
bool trackInsertIsMissing (int track, int slot) const;
engine::PluginSlotState trackInsertState (int track, int slot) const;
int trackInsertLatency (int track, int slot) const;

// master
bool loadMasterInsert (const juce::PluginDescription&, juce::String& error);
void unloadMasterInsert();
engine::PluginSlotState masterInsertState() const;
int masterInsertLatency() const;

// lifetime
void pollRetiredInserts();
```

- Validate track `0..activeTrackCount-1` and slot `0..3`; reject out-of-range.
- `setActiveTrackCount` prepares new track chains/delay lines on the message thread before publishing the new count; shrinking does not destroy loaded plugins.
- `loadTrackInsert` uses the catalog’s async creation and a per-slot request token.
- `restoreTrackInsert`:
  - locate the description by `descriptionXml`, then identifier, then name/manufacturer/format;
  - if not found, mark the slot `MISSING`, retain the saved state, and return false with a reason;
  - if found, enqueue an async load and apply state in the callback.
- `trackInsertState` returns the saved metadata even when the slot is missing or pending.
- `pollRetiredInserts` is called from the UI timer and engine destructor.

### Audio callback restructure

- Allocate a per-track block buffer in `audioDeviceAboutToStart`:
  - one `juce::AudioBuffer<float>` with `2 * trackCount` channels and `maxBlock` samples;
  - create 2-channel views per track; no allocation in the callback.
- Render clips into the per-track views (no fader/pan yet).
- Call `insertRack.beginBlock()` once per callback before processing tracks.
- Process each track through `insertRack.processTrack`.
- Per sample, apply:
  1. baked `mixRack.processSample`;
  2. fader gain + pan;
  3. sum and peak.
- Master: sum → `masterRack.process` → `masterInsert.process` → monitor.
- Click: keep the existing click generator; add it to the master sum before the master rack, preserving current behavior.
- If `numSamples > prepared max`, skip inserts for that block and use the existing clamped fallback; report `overBlockSize`.
- `audioDeviceStopped` must not destroy plugin instances. It resets delay lines/meters only.
- `~AudioEngine` removes the audio callback before destroying the insert rack.

### Master migration

- Remove `PluginHost` as a separate master-only class once all call sites use the catalog + `InsertSlot`.
- If a compatibility wrapper is temporarily needed, it must delegate to the same `HostedInsert` and be deleted before the phase is accepted. No second hosting path.

### Tests

- [ ] Preferred: extract a pure `renderBlockForTest` helper from the callback and test two tracks with different latency, bypass/remove, master-insert order, and zero host allocations.
- [ ] If extraction would duplicate the callback or require a fake device, keep automated coverage at `TrackInsertRack` + `LatencyComp` and cover engine wiring manually. Record exactly which path was automated; do not claim engine automation that does not exist.

**Acceptance:** app builds and plays; master insert still works; track audio is audible through a loaded plugin; no crash on load/remove while playing.

---

## 9. Task 7 — Browser and live insert UI

**Objective:** The browser loads into the selected track/slot; the mixer and inspector show the real state.

**Files:**

- `app/Source/BrowserOverlay.h/.cpp`
- `app/Source/ChannelStrip.h/.cpp`
- `app/Source/InspectorPanel.h/.cpp`
- `app/Source/MixerPanel.h/.cpp`
- `app/Source/MainComponent.h/.cpp`
- `app/Source/Session.h`
- `app/Source/Widgets.h/.cpp` (only if a slot affordance is missing)

### Browser

- Constructor takes `PluginCatalog&` (and the registry snapshot used to build it).
- List model uses `CatalogEntry`, not raw `PluginInfo`.
- `onLoad` becomes `std::function<void(const CatalogEntry&)>`.
- Load button:
  - enabled only when `entry.external && !entry.isInstrument`;
  - label `LOAD ON TRACK` for external effects;
  - disabled reason text for native/archived/instruments: `NATIVE / BAKED`, `NOT A VST3`, `INSTRUMENT (PHASE 4)`.
- Scan button:
  - calls `catalog.scanAsync`;
  - disabled while scanning;
  - shows progress and current file;
  - status on completion/failure/cancel.
- Double-click loads only if the entry is loadable; otherwise it selects and shows the reason.
- `setTarget(trackName, slotIndex)` remains; status text must include track and slot.

### ChannelStrip / Inspector

- Slot 0 remains `NAT` and is not clickable for loading.
- Slots 1–4:
  - name from `engine.trackInsertName`;
  - fallback to `userInsertAt` only when the engine reports empty (never-loaded project);
  - `setBypassed` reflects the engine;
  - `onClick` opens the browser for that slot;
  - `onBypass` calls `engine.setTrackInsertBypassed`;
  - missing slot shows `MISSING` and the saved plugin name in the tooltip;
  - right-click menu: Bypass, Remove, Replace.
- `syncFromEngine` updates names/bypass for all five slots; it must not rebuild the whole strip or steal focus.
- `InspectorPanel`: expose `syncFromEngine()` (or make the existing refresh callable from the main timer); update inserts on the same timer.
- `MixerPanel::syncFromEngine` already loops strips; ensure it calls the new insert sync.
- `MainComponent::timerCallback` polls `engine.pollRetiredInserts()` and refreshes visible strips/inspector.

### MainComponent load path

```cpp
browser.onLoad = [this] (const CatalogEntry& entry)
{
    if (! entry.external || entry.isInstrument)
    {
        status = "Only external VST3 effects can be loaded in this phase";
        return;
    }

    juce::String error;
    if (! engine.loadTrackInsert (browserTrack, browserSlot, entry.description, error))
    {
        status = error;
        return;
    }

    status = "Loading " + entry.name + " on " + trackName + " / INSERT " + String (browserSlot + 1);
};
```

- The browser closes only after the async load succeeds, or stays open with a loading state. **Decision:** stay open with `LOADING`; close on success; keep the error visible on failure.
- A second load request for the same slot while pending returns `still loading`.
- Master menu uses `catalog.snapshot()` and `engine.loadMasterInsert`.

### Manual checks

- [ ] Load a VST3 on track 1 slot 1; strip shows its name.
- [ ] Load on track 2 slot 4; independent.
- [ ] Bypass from the strip; slot shows bypass; audio changes.
- [ ] Remove from the strip; slot returns to empty; audio is dry.
- [ ] Load a second plugin in the same slot; the first is retired; no crash.
- [ ] Load while playing; no callback stall or click beyond the expected chain change.
- [ ] Open the browser from both mixer and inspector; target track/slot is correct.
- [ ] Native/archived/instrument entries cannot be “loaded” with a false success message.

**Acceptance:** no static insert label remains on a track that has a loaded plugin; all visible controls do something.

---

## 10. Task 8 — Project save, restore and missing-plugin report

**Objective:** Reopen the exact insert chain, or report what is missing without losing it.

**Files:**

- `app/Source/MainComponent.h/.cpp`
- `app/Source/ProjectFile.h/.cpp`
- `app/Source/AudioEngine.h/.cpp`
- `tests/test_project_json.cpp` (model coverage already added)
- New `app/Source/PluginHostTests.cpp` cases for slot metadata

### Save

- `writeProject` builds `ProjectDocument` from the engine:
  - for each track, each of four slots: `engine.trackInsertState(track, slot)`;
  - master: `engine.masterInsertState()`;
  - existing mix rack, clips, tempo, loop, track gain/pan/mute/solo unchanged.
- `getStateInformation` is called on the message thread. If transport is playing, stop it for an explicit save (or accept the plugin’s thread-safety contract and document it). **Decision for this phase:** explicit Save stops transport, snapshots state, writes, then restores the previous play state. Autosave policy is Phase 6.
- Save writes version 2.
- A missing slot writes its retained `identifier`, `descriptionXml`, `format`, `stateBase64`, and `bypassed` exactly as loaded.

### Load

- `readProject`:
  1. parse and sanitize;
  2. refuse `version > 2`;
  3. restore non-plugin state first (mix rack, tracks, clips, tempo);
  4. for each track/slot, call `engine.restoreTrackInsert`;
  5. collect `MissingPlugin` entries and load errors;
  6. show a non-modal banner/status: `2 plugins missing — Track 3 / INSERT 2: Pro-Q 3` with a `DETAILS` action.
- `restoreTrackInsert` resolution order:
  1. exact `descriptionXml` (parse `PluginDescription::loadFromXml`);
  2. exact identifier;
  3. name + manufacturer + format;
  4. otherwise `MISSING`.
- Missing slots are not replaced by an empty slot. The strip shows `MISSING`, a right-click `Replace` opens the browser, and `Remove` clears the metadata after confirmation.
- Loaded slots appear as their async loads complete; a project with 64 plugins must not block the message thread for the whole load.
- If a load fails after a description match (e.g., plugin file deleted), mark `MISSING` with the error; preserve state.

### Tests

- [ ] Save a document with four slots on track 1, save, reload, compare all fields.
- [ ] Load with a catalog that lacks the plugin; slot remains in the project and `isMissing()` is true.
- [ ] Save again; missing slot metadata is byte-identical.
- [ ] Replace a missing slot; save; new plugin persists.
- [ ] v1 project loads with empty inserts and saves v2.
- [ ] Invalid base64/XML does not crash; slot is marked invalid/missing.

### Manual

- [ ] Save with a real VST3 on track 1, close the app, reopen, verify name/state/bypass.
- [ ] Rename/move the plugin file (or use a project authored with a missing plugin), reopen; verify `MISSING`, no silent empty slot.
- [ ] Save the project with the missing slot; reopen again; verify it is still there.
- [ ] Replace the missing plugin; save/reopen; verify.
- [ ] Open a v1 project from before this phase; verify it loads and saves as v2.

**Acceptance:** no data loss path; missing plugins are visible and durable.

---

## 11. Task 9 — Master insert parity

**Objective:** The master insert uses the same `HostedInsert`/`InsertSlot`/catalog path and remains safe.

- [ ] Master slot is an `InsertSlot` with one instance; not a separate class.
- [ ] Master menu lists catalog entries; loading uses async creation.
- [ ] Master bypass/remove uses the same command rail.
- [ ] Project save/restore uses `masterInsertState` / `restoreMasterInsert`.
- [ ] Master latency is displayed in the master rack footer or insert tooltip.
- [ ] Load/remove while playing does not click/crash beyond the expected chain change.
- [ ] Existing master insert project files (v1) still restore by identifier.

**Acceptance:** no `PluginHost`-only path remains; master and track behavior match.

---

## 12. Verification gate

### 12.1 Automated

Run after every task:

```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```

Then run the new host tests directly if the script does not yet include them:

```bash
build\app\NCPluginHostTests_artefacts\Debug\NCPluginHostTests.exe
```

Required results:

- [ ] `nc_tests`: all pass, including schema v1/v2, sanitize, future-version refusal.
- [ ] `NCMixRackTests`: all pass; native rack behavior unchanged.
- [ ] `NCPluginHostTests`: catalog merge, XML round-trip, fake-backend timeout/crash, state round-trip, command lifetime, PDC alignment, bypass/remove, NaN containment, over-budget, allocation probe.
- [ ] Build has no new errors. Pre-existing `C4244` warnings and `vswhere.exe` stderr are not new failures; record any new warning.

### 12.2 Manual gate

Use a real installed VST3 effect (EQ, compressor or delay) and, if available, a latency-reporting plugin.

1. **Load/process**
   - Scan; load on track 1 slot 1; play; hear the effect.
   - Load a different effect on track 2 slot 4; both independent.
2. **Bypass/remove**
   - Bypass; verify audio and slot state; un-bypass.
   - Remove; verify dry signal and empty slot.
3. **Live replace**
   - Load while playing; replace twice; no crash, no stuck audio, no dangling UI.
4. **PDC**
   - Put a known-latency plugin on one track and none on another; loop a transient; verify alignment by ear and, if possible, a null test.
   - Bypass the latency plugin; verify timing changes cleanly.
5. **Save/reopen**
   - Save, close, reopen; names, bypass and state restored.
6. **Missing plugin**
   - Move/rename the plugin file; reopen; `MISSING` shown; state retained; save/reopen; still retained; replace/remove works.
7. **Scanner resilience**
   - Debug hooks: `NC_SCANNER_TEST_HANG_MS=2000`, `NC_SCANNER_TIMEOUT_MS=500`; scan; UI stays responsive; timeout reported; next plugin scans.
   - Cancel a scan; no partial/garbage entries.
8. **No device**
   - Disable the audio device; open the editor; load/save/remove still work; no crash on close.
9. **v1 project**
   - Open an old project; verify clips/mix; save as v2; reopen.
10. **UI dead-control sweep**
    - Every insert slot, bypass, remove, browser scan/load, master menu does something or has an honest disabled reason.
11. **DPI**
    - Run at 100% and 150%; insert labels and context menus remain readable. (Full DPI gate is Phase 6.)

### 12.3 Review focus

Answer these before accepting the phase:

- [ ] Does any host-owned code allocate, free, lock, or touch the filesystem on the audio callback?
- [ ] Can a plugin be replaced/removed while audio is running without a use-after-free?
- [ ] Is state captured before the old instance is destroyed?
- [ ] Does a missing plugin retain its state and get reported by track/slot?
- [ ] Does a hung scanner get killed, and does the next file scan?
- [ ] Do two tracks with different plugin latencies align?
- [ ] Does bypass/remove recalculate PDC?
- [ ] Are non-finite samples contained?
- [ ] Does a version 1 project open and save as version 2?
- [ ] Does the master insert still work?
- [ ] Are all visible insert labels live rather than static?
- [ ] Is there any false “loaded” status for native/archived/instrument entries?
- [ ] Does the browser stay open on failure and show the error?
- [ ] Is the app still responsive during a 64-plugin project restore?

**Stop conditions:** any use-after-free, callback allocation, PDC misalignment, missing-plugin data loss, or scanner freeze blocks acceptance. Do not paper over with a bypassed test.

---

## 13. Suggested execution order and checkpoints

1. Task 0 baseline.
2. Task 1 schema + tests (small, isolated).
3. Task 2 catalog/scanner (largest infrastructure risk).
4. Task 3 `HostedInsert`.
5. Task 4 slots/command rail.
6. Task 5 chain/PDC.
7. Task 6 engine integration.
8. Task 7 UI.
9. Task 8 project restore.
10. Task 9 master parity.
11. Task 12 verification.

Checkpoint after each task:

- build green;
- new tests pass;
- existing tests pass;
- commit the task separately (after approval to commit);
- update `docs/UI-REVIEW.md` only when UI behavior actually changes.

Do not start Task 3 before the scanner timeout path is proven; do not start Task 7 before the engine API and PDC tests pass.

---

## 14. Rollback plan

- The schema change is additive; keep the v1 reader.
- `nc_hosting` is a separate static library; if the child-process scanner cannot ship, the catalog can be reverted without touching the audio path.
- The audio callback rewrite is the highest-risk change. Keep the old per-sample path behind a compile-time switch until the new path passes PDC and listening tests; remove the switch before acceptance. Do not leave two runtime paths in the shipping build.
- If a plugin crashes the app during testing, record the plugin and version, remove it from the test matrix, and do not mark the phase complete until the crash is understood or scoped to Phase 6.

---

## 15. Plan self-check

- [x] Goal maps to the hosting/project half of M2; pattern playback is explicitly deferred to Phase 4.
- [x] Every task has files, tests and a verification command.
- [x] Schema migration has backward compatibility and hostile-input cases.
- [x] Scanner isolation has a concrete JUCE API and a total timeout.
- [x] Audio-thread lifetime is specified, including destruction.
- [x] PDC math and cap are specified.
- [x] Missing-plugin behavior is durable, not best-effort.
- [x] Master path is not left behind.
- [x] Out-of-scope work is explicit.
- [x] No implementation was performed while planning.

**Approval needed before execution.** The first action after approval is Task 0 (baseline snapshot), not a code refactor.
