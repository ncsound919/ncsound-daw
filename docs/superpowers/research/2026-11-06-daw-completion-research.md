# Research — Track Hosting, Scanner Isolation and Session Durability

**Date:** 2026-11-06
**Supports:** Phase 2 plan (`docs/superpowers/plans/2026-11-06-daw-completion/PHASE-02-PLAN.md`)
**Method:** read the pinned JUCE tree in `third_party/JUCE`, read the current app/engine code, and checked the public JUCE docs for API contracts. No implementation was run.

---

## 1. Questions

1. How should the editor create and own VST3 instances while audio is running, without allocation or locks on the callback?
2. Can JUCE’s scanner be made hang-safe, and what is the actual extension point?
3. Does JUCE already provide plugin delay compensation, and should this app reuse it?
4. What must be stored in the project so a plugin reopens correctly, and so a missing plugin is not silently lost?
5. Which current code paths violate real-time safety or block later recording/MIDI work?

---

## 2. Findings

### 2.1 Plugin creation is available synchronously and asynchronously

`AudioPluginFormatManager` exposes both:

```cpp
std::unique_ptr<AudioPluginInstance> createPluginInstance (
    const PluginDescription&, double initialSampleRate, int initialBufferSize,
    String& errorMessage) const;

void createPluginInstanceAsync (
    const PluginDescription&, double initialSampleRate, int initialBufferSize,
    AudioPluginFormat::PluginCreationCallback callback);
```

The header explicitly states that the async callback is called **on the message thread**, that the caller must not block the message thread, and that the caller owns the returned instance. This is the right entry point for the browser load path: the UI can show `LOADING`, and a request token can reject a late result if the target slot changed.

Source: `third_party/JUCE/modules/juce_audio_processors_headless/format/juce_AudioPluginFormatManager.h`, around lines 94–128.
Docs: <https://docs.juce.com/master/classjuce_1_1AudioPluginFormatManager.html>

**Decision:** use `createPluginInstanceAsync` for user-initiated loads; keep a synchronous creation helper only for tests. Prepare the instance on the message thread, then hand a raw pointer to the audio thread through the command FIFO.

### 2.2 JUCE’s directory scanner is in-process by default, but it has a custom-scanner hook

`PluginDirectoryScanner::scanNextFile()` calls `KnownPluginList::scanAndAddFile()`, which (with no custom scanner) calls `format.findAllTypesForFile()` in the current process. There is no child process in `juce_PluginDirectoryScanner.cpp`.

Source: `third_party/JUCE/modules/juce_audio_processors/scanning/juce_PluginDirectoryScanner.cpp`, around lines 92–133.

However, `KnownPluginList` has an explicit extension point:

```cpp
class CustomScanner
{
public:
    virtual bool findPluginTypesFor (AudioPluginFormat& format,
                                     OwnedArray<PluginDescription>& result,
                                     const String& fileOrIdentifier) = 0;
    virtual void scanFinished();
    bool shouldExit() const noexcept;
};

void setCustomScanner (std::unique_ptr<CustomScanner>);
```

Source: `third_party/JUCE/modules/juce_audio_processors/scanning/juce_KnownPluginList.h`, around lines 188–220.

JUCE’s own `AudioPluginHost` uses this with `ChildProcessCoordinator` / `ChildProcessWorker`:

- `third_party/JUCE/extras/AudioPluginHost/Source/UI/MainHostWindow.cpp`:
  - `Superprocess` derives from `ChildProcessCoordinator` and sends `(formatName, fileOrIdentifier)` to the worker.
  - `CustomPluginScanner::findPluginTypesFor()` creates/reuses the superprocess, waits for XML, and returns false if the connection is lost.
  - The worker is launched with `File::getSpecialLocation(File::currentExecutableFile)` and a `processUID`.
- `third_party/JUCE/extras/AudioPluginHost/Source/HostStartup.cpp`:
  - `PluginScannerSubprocess` derives from `ChildProcessWorker`; `initialiseFromCommandLine(commandLine, processUID)` selects scanner mode before the main window exists.
- `third_party/JUCE/modules/juce_events/interprocess/juce_ConnectedChildProcess.h`:
  - `launchWorkerProcess()`, `sendMessageToWorker()`, `killWorkerProcess()`.

**Decision:** copy the architecture, not the timeout behavior. The reference host’s `getResponse()` waits 50 ms and loops forever; that keeps the UI on the scanner thread alive but does not report a hang. Phase 2 adds a total deadline and calls `killWorkerProcess()` on expiry, then reports the file as failed and continues.

**Limitation:** this isolates *scanning* only. Once a plugin is loaded into a track, its `processBlock` runs in the editor process. A plugin that crashes during processing can still take down the app. That is a Phase 6 recovery/sandbox concern, not something this scanner solves.

### 2.3 JUCE’s `AudioProcessorGraph` does implement PDC, but it also owns routing

The pinned graph source builds a render sequence with `SequenceAndLatency`, computes per-node delays, and inserts `DelayChannelOp`s for inputs that are ahead of the maximum input latency. It is a real PDC implementation, not a stub.

Source: `third_party/JUCE/modules/juce_audio_processors_headless/processors/juce_AudioProcessorGraph.cpp`, around lines 1029–1215 (`SequenceAndLatency`, `getNodeDelay`, `needsDelay`, `addDelayChannelOp`).

**Why not use it for this phase:** a graph owns node routing, bus layout and connection topology. Replacing the existing per-track native strip/fader path with one graph would be a larger rewrite than the phase needs, and a graph per track would still need cross-track compensation. The repo already has `engine/include/nc/engine/LatencyComp.hpp` with the same rules:

- node latency = worst input latency + own latency,
- connection delay = max input latency − source latency,
- graph latency = longest terminal path,
- track compensation = worst track latency − own latency,
- offline render pre-roll/drop.

**Decision:** keep the custom four-slot chain and use `LatencyComp`. Add cross-track delay lines. Revisit `AudioProcessorGraph` only if multi-bus/side-chain routing arrives.

### 2.4 Plugin identity is not safe to store as an identifier alone

`PluginDescription` has `createIdentifierString()`, and the docs describe it as a stable identifier. A JUCE forum report documents a case where the string changes when the plugin file moves, because the implementation hashes `fileOrIdentifier`. Treat the identifier as a fallback, not the only key.

Sources:

- <https://docs.juce.com/develop/classjuce_1_1PluginDescription.html>
- <https://forum.juce.com/t/createidentifierstring-is-not-independent-of-the-plug-ins-file-location/58890>

**Decision:** persist the full `PluginDescription` XML in every slot (`descriptionXml`) plus the identifier and format name. Restore by parsing the XML, then by matching identifier, then by name/manufacturer/format. If all fail, keep the slot as `MISSING` with its state blob.

### 2.5 Plugin state is opaque, and the host must preserve it even when the plugin is absent

`AudioPluginInstance` inherits `AudioProcessor::getStateInformation(MemoryBlock&)` and `setStateInformation(const void*, int)`. The state is plugin-defined and can be large. The current master path base64-encodes it and stores it in `ProjectDocument::masterInsert`; the same shape is appropriate per track.

**Decision:** add `descriptionXml` to `PluginSlotState`, cap each base64/XML blob (2 MB), and retain the state in a missing slot. Do not call `setStateInformation` on a null instance; report the missing plugin instead.

### 2.6 Current transport locking is not real-time safe

`PluginHost::getPosition()` takes `blockLock` to read `currentBlock`, while `PluginHost::processBlock()` also takes the same lock to write it. `getPosition()` is called by the plugin from inside `processBlock`, on the audio thread. A message-thread lock holder can therefore block the audio callback.

Source: `app/Source/PluginHost.cpp`, `processBlock()` and `getPosition()`.

**Decision:** replace the mutex-protected block snapshot with atomic transport fields (tempo, ppq, time samples, playing, looping, loop points) or a per-block immutable snapshot swapped without locks. The plugin’s `getPosition()` reads only atomics.

### 2.7 Current device setup has no inputs

`AudioEngine::AudioEngine()` calls:

```cpp
deviceManager.initialiseWithDefaultDevices (0, 2);
```

That is correct for playback-only Phase 2, but it is the concrete reason recording cannot start in Phase 3. Phase 3 must request input channels, handle devices with no inputs, and use the input buffers in `audioDeviceIOCallbackWithContext`.

Source: `app/Source/AudioEngine.cpp`, constructor.

### 2.8 Recording primitives for the next phase

JUCE provides the pieces Phase 3 needs; they are noted here so the hosting refactor does not block them:

- `AudioFormatWriter::ThreadedWriter` for a lock-free writer thread and FIFO (`AudioFormatWriter::ThreadedWriter` docs).
- `AudioFormatWriter::writeFromFloatArrays` / `writeFromAudioSampleBuffer` for float data.
- `AudioIODevice::getInputLatencyInSamples()` and `getOutputLatencyInSamples()` for monitoring alignment.
- `AudioDeviceManager::initialise` with input channels; input buffers are the first arguments of the device callback.

Sources:

- <https://docs.juce.com/master/classjuce_1_1AudioFormatWriter_1_1ThreadedWriter.html>
- <https://docs.juce.com/develop/classjuce_1_1AudioFormatWriter.html>
- <https://docs.juce.com/master/classjuce_1_1AudioIODevice.html>

**Decision for Phase 2:** do not implement recording. Leave the callback structured so input buffers can be added without another full rewrite: per-track block buffers, a block context, and a clear master sum.

### 2.9 Project schema is v1 with no track inserts

`engine/include/nc/engine/ProjectState.hpp` defines `projectSchemaVersion = 1`, `TrackState` with only name/gain/pan/mute/solo, and a single `masterInsert`. `ProjectJson.hpp` writes/reads exactly that shape. `vstdaw.schema.json` is a separate v1 document schema (manifest/session/config) and is not used by `ProjectJson`; do not bump its `schemaVersion` as if it were the project version.

**Decision:** bump the C++ project model to v2, add four slots per track, and keep v1 loadable. Update `vstdaw.schema.json` only if the team explicitly converges the two formats; otherwise document the divergence. This is a persisted-format change; sanitize and test it before UI work.

---

## 3. Alternatives considered

| Alternative | Why rejected for Phase 2 |
|---|---|
| One `AudioProcessorGraph` for the whole session | Replaces native per-track routing, bus layout and fader path; graph per track still needs cross-track PDC; larger blast radius |
| In-process scanner on a background thread | UI stays responsive, but a hung plugin cannot be killed or reported; the design’s hang/timeout gate would be unmet |
| Build a separate scanner executable | Works, but duplicates JUCE’s proven same-executable worker pattern and adds a second binary/install artifact |
| Store only `createIdentifierString()` | Path-dependent in reported cases; missing-plugin recovery becomes guesswork |
| Drop missing plugins on load | Data loss; violates the design’s “never silently drop” requirement |
| Post-fader inserts | Compressor thresholds and time-based effects change with the fader; not a standard insert position |

---

## 4. Open risks to resolve during execution

1. **Plugin bus layouts.** Forcing stereo in/out can fail for some effects. The loader must check `setBusesLayout` and report a clear “unsupported channel layout” error, not crash.
2. **Latency changes after prepare.** Some plugins change latency dynamically. The chain should read latency at block boundaries, recompute PDC, and cap compensation; a sudden over-budget change must be reported.
3. **State save while playing.** `getStateInformation` runs on the message thread while the plugin may be processing. This is common host behavior but plugin-dependent. If a plugin misbehaves, stop transport during explicit save; autosave in Phase 6 needs a safer snapshot policy.
4. **Scanner total timeout.** The JUCE reference does not provide one. The implementation must distinguish “worker still working” from “worker dead” and kill/restart cleanly.
5. **Native rack order change.** Moving the baked strip before the fader changes existing mixes. Phase 2 needs an A/B listening check and a regression render before accepting it.
6. **No `.planning`/GSD CLI.** The plan is repository-native; the verification loop is manual until the GSD tooling exists.

---

## 5. Source list

### Local pinned JUCE

- `third_party/JUCE/modules/juce_audio_processors_headless/format/juce_AudioPluginFormatManager.h`
- `third_party/JUCE/modules/juce_audio_processors/scanning/juce_PluginDirectoryScanner.cpp`
- `third_party/JUCE/modules/juce_audio_processors/scanning/juce_KnownPluginList.h`
- `third_party/JUCE/extras/AudioPluginHost/Source/UI/MainHostWindow.cpp`
- `third_party/JUCE/extras/AudioPluginHost/Source/HostStartup.cpp`
- `third_party/JUCE/modules/juce_events/interprocess/juce_ConnectedChildProcess.h`
- `third_party/JUCE/modules/juce_audio_processors_headless/processors/juce_AudioProcessorGraph.cpp`

### Public documentation

- JUCE `AudioPluginFormatManager`: <https://docs.juce.com/master/classjuce_1_1AudioPluginFormatManager.html>
- JUCE `PluginDirectoryScanner`: <https://docs.juce.com/master/classjuce_1_1PluginDirectoryScanner.html>
- JUCE `PluginDescription`: <https://docs.juce.com/develop/classjuce_1_1PluginDescription.html>
- JUCE `AudioProcessorGraph`: <https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html>
- JUCE `AudioFormatWriter::ThreadedWriter`: <https://docs.juce.com/master/classjuce_1_1AudioFormatWriter_1_1ThreadedWriter.html>
- JUCE `AudioFormatWriter`: <https://docs.juce.com/develop/classjuce_1_1AudioFormatWriter.html>
- JUCE `AudioIODevice`: <https://docs.juce.com/master/classjuce_1_1AudioIODevice.html>
- JUCE forum, identifier path dependence: <https://forum.juce.com/t/createidentifierstring-is-not-independent-of-the-plug-ins-file-location/58890>

### Current repository code

- `app/Source/PluginHost.h/.cpp` — single master instance, synchronous scan, locked transport snapshot.
- `app/Source/AudioEngine.h/.cpp` — playback-only device init, per-sample track mix, master rack + insert.
- `app/Source/BrowserOverlay.h/.cpp` — registry-only browser; scan button publishes status.
- `app/Source/Session.h` — static track/insert names.
- `engine/include/nc/engine/ProjectState.hpp` — v1 document, master insert only.
- `engine/include/nc/engine/ProjectJson.hpp` — v1 reader/writer.
- `engine/include/nc/engine/LatencyComp.hpp` — host-side PDC rules.
