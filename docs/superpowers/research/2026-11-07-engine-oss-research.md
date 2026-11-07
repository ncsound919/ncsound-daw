# Research — Engine gaps vs. OSS acceleration

**Date:** 2026-11-07
**Question:** `engine/` searches 0 hits for MIDI / `AudioProcessor` / `AudioDeviceIOCallback` / `processBlock` / WASAPI / ASIO / JACK. What is *actually* missing, and which OSS tools cut the implementation time without licence or architecture traps?
**Method:** read `engine/`, `app/Source/AudioEngine.*`, `PluginHost.*`, `TrackInsertRack.*`, `HostedInsert.*`, `ProjectFile.*`, `ArrangeView.*`, the pinned JUCE 8.0.17-security tree, prior research (`2026-11-05-oss-acceleration.md`, `2026-11-06-daw-completion-research.md`), and current OSS sources. No implementation run.

---

## 1. The headline correction: `engine/` ≠ the engine

Your search is correct for `engine/` and misleading for the repo. `engine/` is deliberately a **pure, JUCE-free header library** (`nc_engine`, CMakeLists.txt:9-26): timeline math, PDC rules, JSON, limits, silence scan. That is why it has 0 hits for device/hosting/MIDI — by design, not by omission.

The real engine lives in `app/Source/` and is **far further along than ROADMAP.md claims** (which still describes the pre-Phase-2 baseline). Verified in-tree:

| Area you flagged | `engine/` | `app/Source/` reality | Actual gap |
|---|---|---|---|
| Device / callback | 0 hits | `AudioEngine : AudioIODeviceCallback`, `initialiseWithDefaultDevices(2,2)` → fallback `(0,2)`, `audioDeviceIOCallbackWithContext` → `renderBlock()` shared with `renderToWav()` (offline export) | Works. No render-graph abstraction, sample-rate conversion is linear-interp only, no dither on export |
| Mixer / clip loop | `ClipTimeline` math only | Per-sample clip render → per-track block buffer → inserts/PDC → baked strip → fader/pan → master rack → master insert → dim/mono → device. SPSC command rail (4096), atomics, no alloc on callback | Linear chain only; no Sends/returns/sidechain graph; PDC capped at 16384 |
| MIDI | `SyncClock::samplesToNextClock` only | `juce::MidiBuffer midi;` (empty) in `PluginHost.cpp:87` and `HostedInsert.cpp:134` — effects-only | **Entirely missing**: note model, scheduling, recording, MTC/clock, instrument hosting, piano roll |
| Plugin hosting | `PluginPolicy` refusal strings | Real: `PluginCatalog` + out-of-process `PluginScanner` (child-process, timeout), `HostedInsert`, `TrackInsertRack` (4 slots/track + master, retire-on-swap, PDC, NaN containment), scan/load/save/restore/missing-plugin | Master still dual-path (`PluginHost` + `TrackInsertRack`); instruments rejected; no parameter automation for hosted plugins beyond baked rack |
| Persistence | `ProjectDocument` v2, `toJson`/`fromJson`, `sanitize()` | `ProjectFile.cpp`: atomic write via `.saving` + `replaceFileIn`, size cap, version-refuse-forward. Automation lanes exist (`Automation.*`) | No tempo map (single `double bpm`), no user markers (hardcoded ruler flags in `ArrangeView.cpp:1155`), no undo stack (1-level snapshot, no redo), no DAWproject/MIDI-file interchange, no peak files |
| Threading | `commandCapacity=4096` constant only | SPSC rail for clips, raw-pointer + retire FIFO for inserts, atomics for transport/gain/pan, double-buffered streamed assets | Primitives are **trapped in `AudioEngine`** — not reusable/testable in `nc_tests`; no generic SPSC/MPSC header |
| Clip editing | `silenceRange`/`scanSilence` primitives | `ArrangeView`: move/trim-left/right/fade/gain drag, `splitClip`, `duplicateSelected`, `deleteSelected`, strip-silence, 1-level undo | Logic is in the **UI class** on int bars — not in `engine/`, not sample-accurate, not unit-testable, no multi-level undo/redo |
| Audio decode/encode | — | `AudioFormatManager::registerBasicFormats()` (WAV/AIFF/FLAC/MP3), `renderToWav`, recorded WAV takes | No resampling quality path, no dither, no MP3/FLAC export option surfaced |

**Conclusion:** don't plan "build an audio engine" — plan "extract, complete, and MIDI-enable the engine that already exists in `app/`".

---

## 2. What JUCE (already pinned) already gives you — adopt before any new OSS

JUCE 8.0.17-security is already vendored and already the licence base. These remove most of the list with **zero new dependencies**:

| Need | JUCE provides | Use |
|---|---|---|
| WASAPI/ASIO/JACK | `AudioDeviceManager` / `AudioIODevice` (ASIO SDK bundled GPLv3 per JUCE changelog) | Already used; add device selector + latency readback |
| Render graph + PDC | `AudioProcessorGraph` (+ `SequenceAndLatency` PDC) | **Reference, not adopt** — see §4. Current `LatencyComp.hpp` + `TrackInsertRack` delay lines already implement the same rules for a linear chain |
| MIDI notes/scheduling | `MidiBuffer`, `MidiMessage`, `MidiFile`, `MidiMessageSequence`, `MidiKeyboardState` | Scheduling + file I/O |
| MIDI device I/O | `MidiInput` / `MidiDeviceList`, `MidiDeviceManager` | Recording + MTC/clock receive |
| Audio decode/encode | `AudioFormatManager` (WAV/AIFF/FLAC/MP3/Ogg), `AudioFormatWriter`, `ThreadedWriter` | Import, record, export |
| Resampling | `juce::Resampler`, `AudioFormatReader` resampling wrapper | Asset rate conversion |
| Offline render | Call `renderBlock` in a loop (already done) | Export |

**Rule:** if JUCE does it, do it with JUCE. New OSS is only justified where JUCE has nothing (tempo map, DAWproject, SPSC header, dither).

---

## 3. OSS candidates per gap

### 3.1 The big fork: Tracktion Engine — spike, don't adopt blind

- **What:** 115k-line JUCE module. Edit/track/clip model, VST3 hosting, PDC, automation with modifiers, offline render, recording, warping, **undo/redo, MIDI (incl. MTC, MPE, MIDI file export), DAWproject import**, crash tracing.
- **Licence:** GPLv3 free / commercial ($35/seat/mo indie ≤$200k → $300/seat/mo, 12-mo, lifetime seat max). Separate from JUCE licence.
- **Why it cuts time:** Tasks "MIDI engine + tempo map + undo + render graph + persistence" largely collapse into `Engine`/`Edit` API.
- **Why it's dangerous here:** (a) pins a specific JUCE commit — conflicts with the 8.0.17-security pin; (b) you write **all** UI — the current `app/` UI + `MixRack` + `ChordStudio` plugins would be stranded or need re-hosting as TE devices; (c) 8 GB build box — clean build time/size unproven; (d) closed-source shipping needs the commercial tier.
- **Verdict:** **3-day spike only if the product ships GPL/AGPL or you accept the TE commercial tier.** Decision rule: clean build ≤15 min + 128-sample playback stable + one VST3 + automation/PDC working → TE wins. Otherwise JUCE-first (§5). This spike was already specified in `2026-11-05-oss-acceleration.md` §2 and never run — run it or explicitly close it.

### 3.2 Ardour — reference only

GPLv2, full DAW not a library. Cannot embed. Valuable as *reading material* for: disk-writer/record alignment (already cited in `AudioEngine.h`), tempo map + marker model, session persistence. The Ardour forum notes TE (GPLv3) ↔ Ardour (GPLv2) incompatibility — do not mix code from both.

### 3.3 Component OSS (adopt selectively, MIT/BSD only)

| Tool | Licence | Cuts | Verdict |
|---|---|---|---|
| VST3 SDK 3.8 hosting helpers (`VST3::Hosting::Module`, `PlugProvider`) | MIT | Hand-written VST3 loading | **Adopt** if ever dropping JUCE hosting; with JUCE hosting, reference only |
| `rigtorp/SPSCQueue` or `boost::lockfree::spsc_queue` | MIT / Boost | Generic SPSC header for `engine/` | **Adopt** — extract `AudioEngine`'s ad-hoc rail (4096) into `engine/include/nc/engine/SpscQueue.hpp`; boosts testability, removes duplication. Prefer writing a 60-line header over adding a dep if you want zero third-party |
| `dr_wav` | PD / MIT-0 | Float WAV read/write | Optional — JUCE WAV writer already does 32-bit float; only adopt if JUCE's writer is insufficient |
| `signalsmith-stretch` | MIT | Pitch/time stretch | Adopt later for clip warp; not v1-critical |
| DAWproject (`bitwig/dawproject` spec + reference) | Open spec | Project interchange: tempo, markers, MIDI notes, automation, plugin state | **Adopt as export/import target in P5** — the spec is the cheapest way to get tempo map/markers "for free" and interop with Bitwig/Studio One/Cubase |
| `pluginval` | GPLv3 | Plugin conformance | **CI tool only, never link** |
| Airwindows / CloudSeedCore / RTNeural / NAM Core | MIT / BSD-3 | Stock FX/amp DSP | Phase 5 stock suite; not engine gaps |

### 3.4 Reject (licence traps for a closed build)

`libsndfile` (LGPL, DLL-only, no buyout), Rubber Band (GPL-2.0+), Guitarix/GxPlugins (GPL), `chowdsp_eq` (GPLv3), CloudSeed GPL ports, Carla bridge (GPL — reading material only). If the product ships GPL/AGPL, re-evaluate — most traps disappear and TE becomes free.

---

## 4. Architecture decisions the plan must make

1. **Render graph: keep linear chain, don't adopt `AudioProcessorGraph`.** Graph owns routing/bus topology; your chain is fixed (clips → 4 inserts → strip → fader → sum → master). `LatencyComp.hpp` already mirrors JUCE/Tracktion/Ardour PDC. Revisit only if sends/sidechain/parallels arrive.
2. **`engine/` stays JUCE-free.** MIDI *model* (notes, tempo map, markers, undo commands) goes in `engine/` as pure structs + tests in `nc_tests`. MIDI *device/hosting* stays in `app/` with `juce::MidiBuffer`. This preserves the current test split and is the point of the extraction.
3. **Tempo map replaces single `bpm`.** `SyncClock`/`ClipTimeline` assume constant BPM everywhere (`secondsPerBar(bpm)`, `ppqAt(samples,bpm,rate)`). A tempo map (`vector<{bar|ppq, bpm}>` + piecewise integration) is the single largest correctness refactor — it touches transport, clip placement, automation (`valueAt(blockBeat)`), and `TransportBlock`. Do it **before** MIDI scheduling, or MIDI PPQ math gets thrown away.
4. **Undo = command pattern, not snapshots.** `ArrangeView`'s 1-level `undoSnapshot` doesn't scale. TE's `UndoManager` and Ardour's command stack are the reference; implement a small `UndoStack` in `engine/` (do/undo, coalescing for drags, redo). Clip ops (split/trim/move) become commands — which is why moving them into `engine/` first pays off.
5. **Resampling + dither are small, do them late.** Linear interp is fine for playback preview; export needs `juce::Resampler` (or proper SRC) + TPDF dither when rendering to <32-bit. Neither blocks MIDI.

---

## 5. Recommended path (JUCE-first)

Keep the current `app/` engine. Spend OSS effort on: (a) JUCE modules you haven't wired (MIDI, formats), (b) a generic SPSC + Undo + TempoMap in `engine/`, (c) DAWproject as the interchange target, (d) the TE spike as a written go/no-go gate before MIDI work starts — because MIDI + tempo map + undo is exactly where TE would save the most.

Sources: local JUCE tree (`third_party/JUCE/modules/juce_audio_processors_headless`, `juce_audio_formats`, `juce_audio_basics/midi`), `docs/superpowers/research/2026-11-05-oss-acceleration.md`, `docs/superpowers/research/2026-11-06-daw-completion-research.md`, Tracktion Engine GitHub/FEATURES.md + licence page, bitwig/dawproject, JUCE docs (AudioProcessorGraph, MidiBuffer, AudioFormatWriter).
