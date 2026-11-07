# Plan — Engine Completion (MIDI, Tempo Map, Persistence, Clip Editing)

**Status:** Plan for approval. **No implementation has been started.**
**Date:** 2026-11-07
**Research:** [`../../research/2026-11-07-engine-oss-research.md`](../../research/2026-11-07-engine-oss-research.md)
**Supersedes (for engine scope):** ROADMAP.md Phases 3–4 framing — the hosting/recording/export work it scheduled is already landed in `app/Source/` (uncommitted). Verify that first (Task 0).

---

## 0. Goal and exit criteria

Close the genuine gaps from the `engine/` audit — **MIDI, tempo map, markers, multi-level undo, clip-edit model, threading primitives, resampling/dither** — using OSS/JUCE to cut time, without rewriting the working audio engine.

**Exit criteria (v1 engine-complete):**
- [ ] MIDI clips schedule sample-accurately through hosted instruments; MIDI records from a device; one MIDI file imports/exports.
- [ ] Tempo map (≥2 tempo points) drives transport, clip placement, automation, and hosted-plugin PPQ; old single-BPM projects load.
- [ ] User markers save/load and export.
- [ ] Undo/redo covers clip split/trim/move/delete, MIDI note edit, tempo/marker edit (not just ArrangeView clips).
- [ ] Clip split/trim/move live in `engine/` (sample/bar-accurate) and are unit-tested in `nc_tests`; `ArrangeView` calls them.
- [ ] A generic SPSC queue + command rail live in `engine/` and are used by `AudioEngine` (no ad-hoc rail left).
- [ ] Export to 16/24-bit applies TPDF dither; asset rate conversion uses a real resampler (not linear interp) on the offline path.
- [ ] Master insert runs on the same `TrackInsertRack` path as track inserts (`PluginHost` deleted).
- [ ] DAWproject export (tempo/markers/MIDI/automation) round-trips through Bitwig/Studio One-class readers, or is explicitly deferred with a recorded decision.
- [ ] All gates in §8 pass.

**Non-goals:** audio graph with sends/sidechain/parallels, MPE, MIDI 2.0/UMP, clip launcher, notation, AU/CLAP, macOS/Linux, M10 amp/mic.

---

## 1. Precondition: Tracktion Engine spike (go/no-go, 3 days)

Do this **before** Task 2 (tempo map) — it is the only decision that can invalidate the rest of the plan.

- [ ] Clone TE with submodules, build `DemoRunner` + minimal 4-track edit, offline render, on this machine.
- [ ] Record: clean/incremental build time, binary size, peak RAM, 128-sample playback stability.
- [ ] Load one third-party VST3 + one Overlay FX; confirm parameter automation + PDC.
- [ ] Write go/no-go in `docs/phase-spike-report.md`. **Rule:** clean build ≤15 min + stable playback → TE path (Tasks 2–5 collapse into TE `Edit` API; UI/MixRack re-hosting becomes the work). Otherwise JUCE-first and this plan proceeds as written.
- [ ] Licence gate: if shipping closed-source and TE commercial tier is not accepted, TE is out regardless of the technical result.

**Stop / fallback:** if TE wins, do not execute Tasks 1–6 as written; produce a TE-adoption plan instead.

---

## 2. Task 0 — Baseline snapshot (because the tree is almost entirely uncommitted)

`git status` shows ~60 untracked `app/Source/*` files + modified `AudioEngine.*`/`MainComponent.*` on top of commit `a7f92c6`. **Every task below depends on this.**

- [ ] Record `git status --short`, `git diff --stat`.
- [ ] Create a reversible baseline (branch or commit) of the current tree. Do not reset/discard.
- [ ] Run the existing gate (`build/_verify_mixrack.cmd` or current equivalent); record `nc_tests` + `NCMixRackTests` counts.
- [ ] **Stop if** the baseline doesn't build or tests fail. No refactor on a broken baseline.

---

## 3. Task 1 — Extract threading primitives into `engine/` (small, isolated, first)

**Why first:** MIDI scheduling, undo, and tempo-map changes all need a command rail / lock-free queue. Today they are trapped in `AudioEngine`.

- [ ] `engine/include/nc/engine/SpscQueue.hpp`: bounded SPSC queue (capacity = power of two), `try_push`/`try_pop`, acquire/release semantics, cache-line padding. ~60 lines header-only, no JUCE. (Or vendor `rigtorp/SPSCQueue` MIT — prefer hand-rolled to avoid a new dep; either passes the same tests.)
- [ ] `engine/include/nc/engine/CommandRail.hpp`: typed command FIFO on top of it (clip adds, insert replace/remove, transport seek) — generalise `AudioEngine::pushCommand`/`processCommands`.
- [ ] Tests (`tests/test_spsc_queue.cpp`): fill/drain, wrap-around, full-refusal, 100k push/pop cycles, TSan-clean if available.
- [ ] Migrate `AudioEngine` to use them; delete the ad-hoc `commandQueue[4096]` + `commandRead/Write` atomics. Behaviour identical.

**Acceptance:** `nc_tests` green; no `std::mutex`/`CriticalSection` in `engine/`; `AudioEngine` no longer owns queue logic.

---

## 4. Task 2 — Tempo map + markers (the correctness refactor — before MIDI)

**Why before MIDI:** `SyncClock::ppqAt/samplesAtPpq/samplesToNextClock` and `ClipTimeline::secondsPerBar/barToSeconds` all assume constant BPM. MIDI PPQ scheduling built on them would be thrown away.

- [ ] `engine/include/nc/engine/TempoMap.hpp`: `struct TempoPoint { double ppq; double bpm; }`, piecewise integration `ppqToSeconds`, `secondsToPpq`, `secondsPerBeatAt(ppq)`, `bpmAt(ppq)`. Pure + tested (constant-BPM equivalence, multi-point, seek across a change).
- [ ] `engine/include/nc/engine/Markers.hpp`: `struct Marker { double ppq; std::string name; }`, sorted insert/remove, clamp to `maxBars`.
- [ ] Migrate `SyncClock::blockContext` + `ClipTimeline` to take a `TempoMap` (keep constant-BPM overload forwarding to a 1-point map so existing tests/callers compile).
- [ ] `ProjectDocument`: add `tempoMap` (default `[{0, 120}]`), `markers`. Bump `projectSchemaVersion` 2 → 3. `isLoadable` accepts 1..3; v1/v2 load with the single-BPM map. `ProjectJson` round-trip + hostile-input tests (out-of-order points, NaN BPM, >1024 points → reject/clamp).
- [ ] `AudioEngine::setBpm` → `setTempoMap`; `bpm` atomic becomes `bpmAt(currentPpq)` computed per block. `Automation::valueAt(blockBeat)` switches to PPQ from the map.
- [ ] UI: tempo ruler edits points (minimum viable: click ruler to add point, drag to move); markers replace the hardcoded `INTRO/VERSE/HOOK/OUTRO` flags in `ArrangeView.cpp:1155` with real `Markers` from the document.
- [ ] **OSS:** DAWproject spec is the reference model for tempo/marker serialisation (P5 export reuses it).

**Acceptance:** a 2-tempo-point project plays with correct bar positions after the change; automation aligns; v2 projects load and save as v3; `nc_tests` cover PPQ↔seconds round-trip.

---

## 5. Task 3 — Clip editing model in `engine/` + multi-level undo

**Why:** split/trim/move currently live in `ArrangeView` (UI, int bars, 1-level undo). MIDI note editing needs the same command/undo machinery.

- [ ] `engine/include/nc/engine/ClipEdit.hpp`: pure ops on `ClipState` — `split(clip, atBar) → 2 clips` (adjust `sourceOffsetSeconds`, fades), `trimLeft/trimRight` (adjust start/length/offset, clamp ≥0), `move(clip, toBar, toTrack)`. Sample/bar-accurate, no JUCE.
- [ ] `engine/include/nc/engine/UndoStack.hpp`: command interface (`do`/`undo`), coalescing (drag = 1 undo), redo stack, cap (e.g. 100). Reference: TE `UndoManager`, Ardour command stack — implement small, don't import.
- [ ] Tests: split at edges (0%, 100%, mid), trim past source start/end, move clamping, undo/redo interleavings, undo-after-undo.
- [ ] Rewire `ArrangeView` to call `ClipEdit` + `UndoStack`; delete its `undoSnapshot`/`hasUndo`. Wire Edit → Undo/Redo (redo currently "not wired" in `MainComponent.cpp:1144`).
- [ ] `ProjectDocument` unchanged except v3 fields from Task 2 (clip shape already suffices).

**Acceptance:** `nc_tests` green for all ops; ArrangeView undo/redo covers split/trim/move/delete; no clip math left in the view.

---

## 6. Task 4 — MIDI engine (the largest new surface)

**OSS use:** JUCE `MidiBuffer`/`MidiFile`/`MidiDeviceManager` for transport + file I/O; pure note model in `engine/`.

- [ ] `engine/include/nc/engine/MidiClip.hpp`: `struct MidiNote { int pitch; double startPpq; double lengthPpq; int velocity; int channel; }`, `struct MidiClipState { int track; double startPpq; std::vector<MidiNote> notes; }`. Quantize/scale-fit/humanize helpers (pure, tested). `ProjectDocument` gains `midiClips` (v3, same bump as Task 2 — coordinate the version).
- [ ] **Scheduling (app):** per block, convert clip PPQ range → sample range via `TempoMap`, emit `juce::MidiBuffer` with sample-accurate offsets into the insert chain. Extend `HostedInsert`/`TrackInsertRack::processTrack` to accept a `MidiBuffer` (currently empty) — effects ignore it, instruments consume it.
- [ ] **Instrument hosting:** allow `isInstrument` slots; `HostedInsert::adopt` accepts `getBusCount(false) > 0` (MIDI in) as well as effects; browser enables instrument rows on instrument tracks. This also retires the "INSTRUMENT (PHASE 4)" disabled reason.
- [ ] **Recording:** `MidiDeviceManager` input → timestamped `MidiBuffer` capture on armed tracks (mirror `RecordBuffer` pattern: preallocated, no alloc on callback) → quantize to PPQ → `MidiClipState`.
- [ ] **File I/O:** `juce::MidiFile` import (type 0/1 → `MidiClipState`) + export (clip → track). Round-trip test.
- [ ] **MTC/Clock:** receive-only first (JUCE `MidiMessage::getMidiTimeCodeQuarterFrame` sweep); send clock/MTC only if a hardware sync target is named. Defer MPE.
- [ ] **UI (minimum):** piano-roll-lite for the selected MIDI clip (add/move/delete notes) — reuse `UndoStack` from Task 3. Full piano roll is post-v1 if this slips.

**Acceptance:** create a pattern → plays through a hosted VST3 instrument in sync → save/reopen → edit notes → MIDI file round-trips. No MIDI allocated on the callback (preallocated buffers, `MidiBuffer` cleared per block).

---

## 7. Task 5 — Plugin hosting completion + audio-path completion

- [ ] **Master parity:** migrate master insert from `PluginHost` (legacy, `PluginHost.cpp`) to `TrackInsertRack::processMaster`/`installMaster`. Delete `PluginHost.*` once `MainComponent`/`AudioEngine` call sites move. One hosting path.
- [ ] **Resampling:** offline asset import + `renderToWav` at a rate ≠ asset rate uses `juce::Resampler` (proper filter) instead of linear interp; playback preview may stay linear. Test: 44.1 kHz asset in a 48 kHz session, null/level check.
- [ ] **Dither:** TPDF dither on export when bit depth < 32 (16/24-bit WAV). Test: LSB noise, no DC, deterministic seed per render.
- [ ] **Render graph (decision, not work):** document why the linear chain + `LatencyComp` stays; `AudioProcessorGraph` is adopted only if sends/sidechain are scoped in. No code.
- [ ] **Parameter automation for hosted plugins:** extend `AutomationLane` to plugin parameters (parameter ID + lane) — value applied per block via `AudioProcessor::setParameterNotifyingHost`. (Baked-rack automation already exists.)
- [ ] **Peak files:** optional, post-v1 — one pass over float frames at import (JUCE has no peak format; this is app code).

**Acceptance:** one hosting path; 16-bit export dithered and null-testable; rate conversion no longer clicks on offline render.

---

## 8. Task 6 — Persistence, interchange, hardening

- [ ] **Atomic write:** already in `ProjectFile.cpp` (`.saving` + `replaceFileIn`). Add: fsync/flush before replace, backup of last-good on failed write, crash-recovery file (autosave policy decision — Phase 6 in ROADMAP). Verify with a kill-during-save test.
- [ ] **DAWproject export/import:** tempo map, markers, MIDI clips, automation, track/clip structure. Use the open spec; validate against a Bitwig/Studio One round-trip if available, else against the reference XML. This is the cheapest interop win.
- [ ] **Audio decode/encode:** confirm `registerBasicFormats()` coverage (WAV/AIFF/FLAC/MP3 decode; WAV write) is surfaced in Import/Export UI; add FLAC export if trivial via `AudioFormatWriterOptions`.
- [ ] **Hostile project:** fuzz `fromJson` (truncated, deep-nested, oversized blobs) — limits in `ProjectLimits.hpp` already cap; add the fuzz harness + `pluginval` (strictness ≥5, CI-only) for first-party plugins.

**Acceptance:** DAWproject round-trip or recorded deferral; kill-during-save loses at most the in-flight save; fuzz corpus passes.

---

## 9. Verification gate

After every task:
```bash
cmd.exe //c "C:\Users\Asus\Desktop\Plugins\VST DAW\build\_verify_mixrack.cmd"
```
- [ ] `nc_tests`: all pass (new: SPSC, TempoMap, Markers, ClipEdit, UndoStack, MidiClip, schema v3).
- [ ] `NCMixRackTests`: all pass (native rack + hosting unchanged).
- [ ] No new warnings; no `mutex`/alloc/`malloc` on the callback (grep `renderBlock` path).
- [ ] Manual (per task): real VST3 effect + instrument, MIDI keyboard if available, 44.1/48 kHz session, save/reopen, missing plugin, undo/redo sweep, DAWproject round-trip.

**Stop conditions:** tempo-map PPQ drift, use-after-free on insert swap, MIDI timing jitter >1 block, missing-plugin data loss, or DAWproject round-trip corruption blocks acceptance.

---

## 10. Execution order, estimates, risks

| Order | Task | Rough size | Depends on |
|---|---|---|---|
| 1 | TE spike go/no-go | 3 days | — |
| 2 | Task 0 baseline | hours | approval |
| 3 | Task 1 SPSC/command rail | 1–2 days | 0 |
| 4 | Task 2 tempo map + markers | 4–6 days | 1 |
| 5 | Task 3 clip edit + undo | 3–4 days | 1 |
| 6 | Task 4 MIDI engine | 1–3 weeks (UI-dependent) | 2, 3 |
| 7 | Task 5 hosting/audio completion | 4–6 days | 4 |
| 8 | Task 6 persistence/interchange | 3–5 days | 2, 4 |

**Risks:** tempo map touches every PPQ call site (mitigate: keep constant-BPM forwarding overloads, migrate one caller at a time); MIDI instrument hosting changes `HostedInsert::adopt` validation (mitigate: keep effect path regression-tested); TE spike result may invalidate Tasks 2–5 (mitigate: spike first, cheap); uncommitted tree (mitigate: Task 0).

**Decisions needed from you before execution:**
1. Ship closed-source or GPL/AGPL? (Determines TE + licence traps.)
2. Run the TE spike, or close it and commit to JUCE-first?
3. MIDI v1 scope: pattern/quantized clips only, or full piano roll + MTC send?
4. DAWproject in v1, or defer to post-v1?
5. Approve Task 0 baseline (branch vs. commit) on the uncommitted tree.
