# Plan — JUCE Scheduling, Instruments, and Recording

**Status:** Plan for approval. **No implementation has been started.**
**Date:** 2026-11-07
**Research:** [`../../research/2026-11-07-juce-scheduling-instruments-record.md`](../../research/2026-11-07-juce-scheduling-instruments-record.md)
**Depends on:** Tasks 1–4 (SPSC, tempo map, ClipEdit/Undo, MIDI model + scheduling + instrument lane) — landed, audit fixes applied, test verification pending (see §7).

---

## 0. Goal and exit criteria

Make scheduling efficient and complete, instruments playable and tweakable, and recording wired end-to-end (audio first, then MIDI).

**Exit criteria:**
- [ ] Scheduler emits MIDI with **no per-block rescan/sort/allocation**; cursor-based, tested at 64-track/4096-note stress.
- [ ] MIDI model carries **CC + pitch-bend** (sustain pedal works on the built-in synth); notes still round-trip.
- [ ] Built-in synth has **filter + ADSR + sustain + pitch-bend + presets**; a MIDI clip played through it is recognisable as bass/keys/pad per preset.
- [ ] A hosted VST3 instrument loads from the browser, plays MIDI clips, opens its **editor window**, and survives save/reopen; a multi-channel instrument is either handled or rejected with a clear error (never silent).
- [ ] **Record button works**: arm → record → stop produces a placed clip; input monitoring through the insert chain works with a direct toggle.
- [ ] **Punch in/out** records only inside the loop region and places the take at punch-in.
- [ ] **MIDI recording**: play a keyboard/device while armed → notes land as a clip at the right PPQ, quantized.
- [ ] Long takes stream to disk (`ThreadedWriter`); the 3-minute RAM cap is gone.
- [ ] All gates in §6 pass.

**Non-goals:** MPE, MTC/clock sync (receive-only later), comp lanes (take list only), drum machine/sampler stock instruments, parameter-accurate sub-block automation.

---

## 1. Task 1 — Cursor-based scheduler + CC/pitch-bend model

**Files:** `engine/include/nc/engine/MidiClip.hpp`, `engine/include/nc/engine/MidiSchedule.hpp`, new `app/Source/MidiScheduler.h/.cpp`, `tests/test_midi_schedule.cpp`, `tests/test_midi_clip.cpp`, `engine/include/nc/engine/ProjectJson.hpp` (CC/pitch-bend serialisation).

- [ ] **Model:** add `MidiCC { double beat; int channel; int controller; int value; }` and `MidiBend { double beat; int channel; int bend; }` to `ClipState` (parallel to `notes`), with sanitize (ranges, sorted, clipped to clip, capped). JSON: `"ccs":[{beat,channel,ctl,val}]`, `"bends":[{beat,channel,bend}]`. Round-trip + hostile tests.
- [ ] **Scheduler:** `MidiScheduler` (app, audio-thread): per published clip snapshot, one cursor (next-note index, next-cc index, next-bend index). Per block: advance cursors while event PPQ < blockEnd; emit events with sample offsets; **no sort, no scan, no alloc** (scratch pre-reserved, as now). Seek/loop detection (blockStartPpq < lastBlockPpq) resets cursors and re-emits sounding notes (current retrigger semantics preserved).
- [ ] Keep `scheduleMidiClip` as the pure reference implementation; `MidiScheduler` must produce **identical event streams** on a property test (random notes/CCs/bends, random block sizes, tempo changes) — test both against each other.
- [ ] `AudioEngine::renderBlock`: replace the per-track `scheduleMidiClip` loop with `MidiScheduler::render(track, blockStartPpq, blockEndPpq, tempoMap, rate, numSamples, midiBuffer)`.
- [ ] Stress test: 64 tracks × 4096 notes, 512-sample blocks, assert bounded time per block (no timing flake — assert cursor monotonicity + event counts, not wall clock).

**Acceptance:** event streams match the reference scheduler; no allocation in the block path (grep + allocation-probe test with counting allocator on the scheduler).

---

## 2. Task 2 — Built-in synth upgrade

**Files:** `app/Source/BuiltinSynth.h/.cpp`, `app/Source/BuiltinSynthTests.cpp` (new).

- [ ] **Voice:** per-voice one-pole lowpass (cutoff from preset, key-tracked), ADSR envelope (attack/decay/sustain/release with rate-independent coefficients), velocity curve (linear/exponential select), master tune.
- [ ] **Sustain (CC64):** `controllerEvent` defers `stopNote` release until pedal-up; pedal state per voice, cleared on `reset()`.
- [ ] **Pitch-bend:** `pitchWheelMoved` bends `frequency` ±2 semitones (configurable range), per-voice.
- [ ] **Voices:** 8 → 16, note-stealing already enabled.
- [ ] **Presets:** 3 built-ins (Bass / Keys / Pad = wave + filter + env combos) stored in slot state (extend the current wave-int blob with a versioned struct); `setWave` kept for compatibility.
- [ ] Tests: envelope reaches sustain, release decays, sustain pedal holds then releases, pitch-bend shifts frequency, preset round-trip, no NaN at extreme parameters.

**Acceptance:** preset audition is recognisable; sustain/pitch-bend tests pass; state round-trips through save/reopen.

---

## 3. Task 3 — Hosted instruments: browser + editor + bus layouts

**Files:** `app/Source/AudioEngine.cpp` (`loadTrackInstrument`), `app/Source/BrowserOverlay.*`, `app/Source/HostedInsert.cpp`, new `app/Source/PluginEditorWindow.*`, `app/Source/VstInstrumentTests.cpp` (new, gated on a real VST3 being present — skip cleanly otherwise).

- [ ] **Browser:** rows with `description.isInstrument` get `LOAD AS INSTRUMENT` (enabled), effects keep `LOAD ON TRACK`. Instrument loads target the selected track's instrument lane (`loadTrackInstrument` already exists).
- [ ] **Editor window:** `createEditor()` in a `DocumentWindow` per instrument slot; close on unload/replace; one editor per slot (prevent duplicates); editor is message-thread only, never touches the callback.
- [ ] **Bus layouts:** after `setPlayConfigDetails(0, 2, ...)`, verify main stereo out; if the plugin reports extra output buses, attempt `setBusesLayout` with only main enabled; else reject with "unsupported channel layout" (never silent). Test with a stereo instrument (pass) and document the multi-channel rejection.
- [ ] **Latency:** instrument latency already folds into `chainLatency` (rack); verify with a latency-reporting instrument that notes align with audio clips after PDC (null/impulse test).

**Acceptance:** load → play MIDI clip → open editor → tweak a parameter → save/reopen with state; missing instrument retained as `MISSING` (existing path).

---

## 4. Task 4 — Wire audio recording + input monitoring

**Files:** `app/Source/TransportBar.*`, `app/Source/MainComponent.*`, `app/Source/AudioEngine.*`, `app/Source/RecordingTests.cpp`.

- [ ] **Transport wiring:** record button → `engine.startRecording()` (with error status), stop → `engine.stopRecording(folder, 24, error)` → place each take as a clip on its track at the record-start bar via `loadAudioFile` + `setTimelineClips` (existing paths). Remove the "not wired" tooltip/status. Count-in: 1 bar metronome before capture starts (transport `positionSamples` starts at punch-in; capture offset by count-in length).
- [ ] **Input monitoring:** armed tracks' input channels are mixed into the track lane in `renderBlock` **before** `processTrackInstrument`/inserts (so monitoring runs through the same chain), gated by a per-track monitor toggle (default on for armed, off otherwise) and a direct-monitor bypass. Monitoring level = track fader path as normal. Latency: report `getInputLatencyInSamples() + getOutputLatencyInSamples()` as the record offset in addition to the existing trim.
- [ ] **Punch:** if loop is enabled, recording auto-starts at loop start and auto-stops at loop end; the take clip is placed at loop start. Manual punch (record button while playing) punches in at the current position.
- [ ] Tests: record through `processBlockForTesting` with injected input + monitoring on → output contains input through a fake insert; punch region records only inside the loop; take placement bar is correct after stop.

**Acceptance:** a user can arm, record a guitar/vocal take while hearing it through the channel strip, and hear the placed clip on playback. (Manual gate with a real device.)

---

## 5. Task 5 — MIDI recording + streaming audio writer

**Files:** new `app/Source/MidiRecorder.h/.cpp`, `app/Source/AudioEngine.*`, `app/Source/RecordingTests.cpp`.

- [ ] **MIDI capture:** `MidiDeviceManager` (or `MidiInput::openDevice`) with a `MidiInputCallback` that only timestamps + `SpscQueue::tryPush`es (no allocation, no locks). `MidiRecorder` drains the queue on the message thread, converts (sample, PPQ) pairs to `MidiNote`/`MidiCC`/`MidiBend` (pairing note-ons/offs, quantize to grid), and publishes a `MidiTimelineClip` at record-start via `setMidiClips`.
- [ ] **Live merge/monitoring:** `MidiKeyboardState` merges device + computer keyboard into the armed track's instrument lane during recording (play as you record).
- [ ] **MIDI punch:** same loop-region logic as Task 4; notes outside the punch region are dropped.
- [ ] **Streaming audio:** replace/augment the RAM `RecordBuffer` with `AudioFormatWriter::ThreadedWriter` (FIFO + `TimeSliceThread`): callback pushes float blocks, writer thread flushes WAV. Keep overflow counting. The 3-minute cap becomes disk-bound; `stopRecording` finalises the writer instead of writing from RAM.
- [ ] Tests: fake MIDI callback injects timestamped messages → recorded clip has correct PPQ/pitch/velocity after quantize; CC/pitch-bend captured; punch drops out-of-region notes; threaded writer produces a valid WAV of the expected length (existing recording tests must still pass).

**Acceptance:** play a keyboard while recording → clip appears at the right bar with quantized notes; record 5 minutes of audio without RAM growth proportional to take length.

---

## 6. Verification gate

After every task:
```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```
- [ ] `nc_tests`: all pass (scheduler equivalence, CC/bend round-trip, sanitize).
- [ ] `NCMixRackTests`: all pass (synth, instrument, recording suites).
- [ ] No allocation/lock/file I/O on the callback in the new paths (scheduler cursor, MIDI callback queue, threaded writer push).
- [ ] Manual: real MIDI keyboard + audio device, hosted VST3 instrument editor, punch, monitoring latency by ear, 5-minute take.

**Stop conditions:** scheduler event mismatch vs. reference, callback allocation, monitoring latency misalignment, MIDI timing jitter >1 block, take data loss.

---

## 7. Execution order and pending verification

1. **First:** finish verifying Tasks 1–4 (run `nc_tests` + `NCMixRackTests` green; the interrupted test run and the ClipEdit/scratch audit fixes need a confirmed green gate).
2. Task 1 scheduler (pure + app, test-heavy).
3. Task 2 synth (isolated, no engine changes).
4. Task 3 hosted instruments (needs 2 for a fallback instrument to test against).
5. Task 4 audio record wiring (engine exists; mostly transport + monitoring).
6. Task 5 MIDI record + streaming writer (needs 1's CC/bend model and SPSC).

**Decisions needed before execution:**
1. Monitoring default: on for armed tracks (recommended) or off?
2. MIDI record quantize: grid always, or record raw + quantize on demand?
3. Streaming writer: replace RAM capture outright, or keep RAM as fallback for short takes?
4. Confirm Tasks 1–4 verification gate is green before starting Task 1 here.
