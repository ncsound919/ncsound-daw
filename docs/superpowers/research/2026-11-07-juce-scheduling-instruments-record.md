# Research — JUCE Scheduling, Instruments, and Recording

**Date:** 2026-11-07
**Question:** What does JUCE already provide for sample-accurate MIDI scheduling, instrument hosting/synthesis, and audio+MIDI recording — and what is our current state versus that?
**Method:** read the pinned JUCE 8.0.17-security tree (`third_party/JUCE/modules`), current `app/Source/AudioEngine.*`, `BuiltinSynth.*`, `TrackInsertRack.*`, `engine/MidiSchedule.hpp`, plus JUCE docs/forum. No implementation run.

---

## 1. Current state (audited 2026-11-07)

| Area | Exists | Missing / broken |
|---|---|---|
| **Scheduling** | `engine::scheduleMidiClip` (pure, tempo-map aware, retrigger-on-seek) → `juce::MidiBuffer` with sample offsets → `TrackInsertRack::processTrackInstrument` → instrument `processBlock`. `MidiFile` import/export. | Scheduler **rescans every note of every clip on the track, every block**, and `std::sort`s per clip per block on the audio thread. No cursor, no pre-sorted index. Model is **notes only** — no CC, pitch-bend, aftertouch, program change, MPE. No sub-block splitting for parameter-accurate DSP (not needed for note timing, which MidiBuffer already handles). No MTC/clock. |
| **Instruments** | `BuiltinSynth` (JUCE `Synthesiser`, 8 voices, sine/triangle/saw, state = wave int), VST3 instrument hosting (`HostedInsert::create(..., allowInstruments=true)`, 0-in/stereo-out), per-track instrument lane in the rack, save/restore incl. missing retention. | BuiltinSynth: **no filter, no real ADSR** (attack ramp + release only), `controllerEvent`/`pitchWheelMoved`/`pitchBendReceived` are **empty** (sustain pedal ignored, pitch-bend ignored), 8 fixed voices, no presets, no velocity curve, no mono/legato. Hosted instruments: stereo-only main bus (multi-channel instruments e.g. HALion-class will mis-negotiate), no editor window (`createEditor` never called), browser load path for instruments exists in engine but no UI affordance. |
| **Record (audio)** | Engine capture: preallocated `RecordBuffer` per armed track, input-aligned, latency-trimmed on write, WAV out, overflow counter, tested (`RecordingTests.cpp`). | **TransportBar record button is deliberately unwired** ("not wired in this build" — stale; engine captures today). **No input monitoring**: captured input is never mixed to output, so you cannot hear yourself through the plugin chain while recording (design goal §2.1). 3-minute RAM cap per armed track, no streaming writer. No punch in/out (records start→stop only). No takes/comp lanes (one WAV per stop). No count-in. |
| **Record (MIDI)** | Nothing. No `MidiDeviceManager`, no `MidiInput`, no timestamped capture, no quantize-on-record. | Entirely missing. |
| **Sync** | `samplesToNextClock` (24-PPQN math) + `blockContext` for hosted plugins. | No MTC receive/send, no MIDI clock send, no external sync. |

---

## 2. JUCE scheduling — what to use

- **`MidiBuffer` timestamps are sample offsets relative to the block** (`juce_MidiBuffer.h`: "the sample number will be used to determine the position"; `MidiMessage::getTimeStamp` in `processBlock` is 0..blockSize-1). Our current approach — convert PPQ window → sample offsets via the tempo map, `addEvent(msg, offset)` — is the correct JUCE pattern. No sub-block splitting needed for *note* timing; the instrument's own `processBlock` receives timed events.
- **Efficiency pattern:** pre-sort once (notes already sorted by `sanitizeNotes`), keep a **per-clip read cursor** (index of first not-yet-emitted note) advanced as blocks pass, instead of rescan+sort per block. `MidiMessageSequence` is the JUCE structure for this (sorted events, `getEventIndex`, `getTimeOfEvent`), but a cursor over our sorted `MidiNote` vector is simpler and allocation-free. Either way: **no `std::sort`, no allocation on the callback**.
- **`MidiKeyboardState`** is the JUCE pattern for live input merging (keyboard + device) into a `MidiBuffer` — use it for MIDI recording monitoring and computer-keyboard play.
- **`AudioProcessorGraph` MIDI chunking** (`juce_AudioProcessorGraph.cpp:546`: `midiChunk.addEvents(midiMessages, chunkStartSample, chunkSize, -chunkStartSample)`) is the reference for splitting a MidiBuffer across sub-blocks when a graph splits processing — only relevant if we ever sub-block; we don't.
- **Parameter-accurate automation** is a separate JUCE roadmap item (event-based sub-blocks); out of scope for note scheduling.

**Decision:** keep `scheduleMidiClip` semantics, replace its per-block rescan+sort with a cursor-based scheduler (`MidiScheduler` in `app/`, holding per-clip indices; pure helpers stay in `engine/` for testability).

---

## 3. Instruments — what to use

- **Builtin synth:** `juce::Synthesiser` + `SynthesiserVoice` is the right base (already used). Gaps are all in the voice: add a one-pole lowpass per voice (or `juce::dsp::IIR::Filter`), proper ADSR (attack/decay/sustain/release segments with sample-rate-independent coefficients), sustain-pedal handling in `controllerEvent` (CC64: defer note-offs until pedal up), pitch-bend in `pitchWheelMoved` (semitone bend on `frequency`), velocity curve, and 2–3 presets (wave + filter + env stored in slot state). MPE (`juce::MPEInstrument`) is available but **defer** — it needs per-note pitch/pressure plumbing through the whole stack.
- **Hosted VST3 instruments:** `AudioPluginFormatManager::createPluginInstanceAsync` (already used for effects) + `setPlayConfigDetails(0, 2, ...)` for instruments (already done). The known JUCE trap is **bus layout negotiation** for multi-channel instruments (HALion 7 forum report: 33 output buses, silent output when extra buses aren't disabled). Mitigation: after `setPlayConfigDetails`, verify `getMainBusNumOutputChannels() == 2` and `getTotalNumOutputChannels() == 2`; if the plugin reports extra buses, try `setBusesLayout` with only the main stereo bus enabled, else reject with a clear "unsupported channel layout" error (same pattern as effects). **Do not** adopt `AudioProcessorGraph` for this — the rack lane is sufficient.
- **Editor windows:** `AudioPluginInstance::createEditor()` + `AudioProcessorEditor` in a `DocumentWindow` (JUCE AudioPluginHost pattern). Needed for real instrument tweaking; effects editor windows are the same code path. Currently no editor is ever created — this is the biggest instrument usability gap after the synth voice itself.
- **Scanner flag:** `PluginDescription::isInstrument` (set by `KnownPluginList`, persisted in description XML) already distinguishes instruments in the catalog. The browser must enable `LOAD AS INSTRUMENT` for those rows.

---

## 4. Recording — what to use

### 4.1 Audio

- **Current capture is sound** (Ardour disk-writer rule: callback appends to preallocated buffers, latency trim on write). Two upgrades, in order:
  1. **Wire the transport** (TransportBar button → `startRecording`/`stopRecording`, arm integration already exists in Inspector/ChannelStrip). This alone makes recording usable.
  2. **Input monitoring**: mix `inputChannelData` for armed tracks into the track lane *before* inserts (so monitoring runs through the same chain as playback — design goal), with a direct-monitor toggle and the reported input+output latency as record offset. This is the feature that makes tracking guitar/vocals through the stock chain possible (M10 amp/mic depend on it).
  3. **Punch in/out**: `startRecording`/`stopRecording` already bracket a region; add punch = transport loop region + auto-stop at punch-out, captured audio placed as a clip at the punch-in bar (via existing `setTimelineClips` + `loadAudioFile`).
  4. **Streaming writer** (replaces 3-min RAM cap): `AudioFormatWriter::ThreadedWriter` (`juce_AudioFormatWriter.h:194`) — FIFO fed from the callback, flushed by a `TimeSliceThread`. The callback pushes float arrays; disk never blocks audio. Keep the preallocated buffer as a fallback/overflow detector during migration.
  5. **Takes/comp**: `RecordedTake` already returns per-track files; takes = keep each stop's files, list them per track in the project (`takes[]` in the v2 design §8), comp = manual clip selection. Full comp lanes are a non-goal for v1 (design §3); take list + swipe comp is the target.

- **`AudioIODevice::getInputLatencyInSamples()/getOutputLatencyInSamples()`** for monitoring alignment and record offset (currently `recordLatency` is set manually via `setRecordLatencySamples`).

### 4.2 MIDI

- **Device:** `MidiDeviceManager` (device enumeration + `MidiInput` lifecycle) or direct `MidiInput::openDevice(id, callback)`. Callback arrives on a **high-priority system thread** (`MidiMessageCollector` docs: "avoid doing anything time-consuming") — so the callback must do the minimum: timestamp the message (`MidiMessage::getTimeStamp()` = host time) and push into a lock-free queue (`engine::SpscQueue` from Task 1 — this is what it was for).
- **Timestamping:** convert host timestamp → transport PPQ using the tempo map at capture time (or store samples + convert on the message thread; samples are safer across tempo changes, PPQ is safer across seeks — store both: sample position + PPQ).
- **Placement:** on stop, quantize PPQ to grid (existing `quantizeBeat`), create a `MidiTimelineClip` at the record-start bar, `setMidiClips`. Punch for MIDI = same region logic as audio.
- **Live merge:** `MidiKeyboardState` merges device + computer keyboard into the instrument lane while recording (input monitoring for MIDI = play the instrument live).

### 4.3 Sync (defer, but note)

MTC receive (quarter-frame sweep → transport chase), MIDI clock send (24 PPQ via `samplesToNextClock`, already computed), MTC send. Tracktion Engine lists all of these; Ardour implements MTC chase. **Recommend receive-only in a later phase**; send only when a hardware target is named.

---

## 5. OSS verdict for this pivot

**No new OSS dependencies.** JUCE covers scheduling (`MidiBuffer`), synthesis (`Synthesiser`), hosting (`AudioPluginFormatManager`), recording (`ThreadedWriter`, `MidiInput`), and file I/O (`MidiFile`, `AudioFormatWriter`). Tracktion Engine / Ardour remain *reference reading* for punch/takes/comp UX, not code. The only external pattern worth naming is the **SPSC queue** (already vendored as `engine::SpscQueue`).

Sources: pinned JUCE tree (`juce_MidiBuffer.h`, `juce_MidiMessageSequence.h`, `juce_AudioFormatWriter.h:194`, `juce_AudioProcessorGraph.cpp:546`, `juce_PluginDescription.h:130`), JUCE docs (MidiInput, MidiInputCallback, MidiMessageCollector, Synthesiser, ThreadedWriter, bus-layout tutorial), JUCE forum (sample-accurate MidiBuffer timestamps, HALion bus-layout trap, ThreadedWriter FIFO), current `AudioEngine` recording block (lines 1976–2195), `BuiltinSynth.h`, `MidiSchedule.hpp`.
