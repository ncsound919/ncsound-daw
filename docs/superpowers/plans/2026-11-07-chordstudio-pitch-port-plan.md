# Port ChordStudio pitch + engine patterns to VST DAW — Plan

**Goal:** Bring the best proven elements from `ChordStudio-master` (including the pitch/transpose/Hz work) into `VST DAW` (NCSound DAW), without re-introducing the React/WebView stack unless you approve the UI change.

> Status: **PLAN ONLY — no code changes yet.** Answer §10 then approve to implement.

**Scope locked (2026-11-07):**
- **What:** Pitch + engine patterns (not pitch-only, not full theory/LLM/drums port)
- **Where:** master/global, MIDI instrument, audio clips, percussion VST own pitch
- **UI:** React if it looks better (note: VST DAW roadmap §1 says native JUCE only — this plan treats React as an explicit deviation)
- **Verify:** engine tests only (no full JUCE app build in this pass)

Source: `C:\Users\Asus\Desktop\Plugins\ChordStudio-master` (JUCE 8 + React WebView2, `Source/audio/PitchStack.h`, `Track`, `PluginSlot`, `RtPublisher`, `StretchCache`)
Target: `C:\Users\Asus\Desktop\Plugins\VST DAW` (native JUCE, `engine/` pure + `app/Source/` JUCE, schema v5, 64 tracks)

---

## 1. What exists in VST DAW today (port target)

- **Engine (pure C++, `engine/include/nc/engine/`):** `ProjectState.hpp` v5 (`TrackState` gain/pan/muted/solo + 4 inserts + instrument, `ClipState`, `PluginSlotState`), `MidiClip.hpp` (`MidiNote/CC/Bend`, `sanitize*`), `MidiSchedule.hpp` / `MidiScheduler.hpp` (cursor, O(active)), `TempoMap`, `ProjectLimits` (64 tracks / 1024 clips), `AutomationLane` (Volume/Pan only), `ProjectJson.hpp` (own JSON, capped).
- **App (JUCE, `app/Source/`):** `AudioEngine` (64 `Track` atomics gain/pan/mute/solo, `TrackInsertRack`, `PluginHost` in-process, `MidiTimelineClip`, `renderBlock`), `ChannelStrip` (pan/send knobs + fader), `Widgets::Knob` (normalized 0..1, bipolar, double-click reset — no `ParamSpec` text edit), `InspectorPanel`, `MixerPanel`, `PianoRoll`, `Session`.
- **No pitch:** `grep pitch|transpose|tuning` hits only clip `pitch` note number + `MidiBend`. No global/track/instrument transpose, fine pitch, or A4 Hz.
- **Roadmap:** `docs/superpowers/plans/2026-11-06-daw-completion/ROADMAP.md` — Windows x64, VST3 only, native UI only, Phase 4 = MIDI/instruments. Project v5 → v6 would be needed.

## 2. What ChordStudio proved best (port source)

| Element | File | Why it's worth porting | VST DAW fit |
|---|---|---|---|
| `PitchStack` (transpose/pitch/Hz, `hzToCents`, `sumCents`) | `Source/audio/PitchStack.h` (new, tested) | Pure header, 12 TheoryTests, clamp/NaN-safe, additive 3-scope maths | **Port 1:1** as `engine/include/nc/engine/PitchStack.hpp` — matches VST DAW's header-only engine pattern |
| `Track::applyPitchToMidi` + `AudioEngine` global dispatch (noteOn/off, live MIDI, transport, bounce) | `Source/audio/Track.cpp`, `AudioEngine.cpp` | One choke point, pitch-bend injection (±2st, summed), out-of-range drop, tested via `ChordProjectTests` | **Port** — VST DAW already has `MidiSchedule`/`MidiScheduler`; add `applyPitch` before `MidiBuffer` build |
| `RtPublisher<Chain>` immutable chain snapshot | `Source/audio/RtPublisher.h` | No plugin destroyed on audio thread, no lock on callback | **Port pattern** — VST DAW `TrackInsertRack` already does atomic raw-pointer + retired list; adopt ChordStudio's `RenderClock` epoch for consistency |
| Out-of-process scanner (`ChildProcessCoordinator` + dead-man's pedal) | `Source/audio/PluginHost.h`, `ScannerProtocol.h` | Crash isolation; ChordStudio ships `ChordStudioScanner.exe` | **Port (Phase C)** — VST DAW `PluginHost::scanDefaultLocations()` is in-process sync (roadmap gap: hang risk) |
| `StretchCache` + signalsmith-stretch (MIT, vendored) | `Source/audio/StretchCache.h`, `Source/third_party/signalsmith-stretch/` | Offline length-prefixed cache, streamed file-to-file, bounce vs live parity | **Port (Phase C)** — VST DAW `AudioSuite`/`processAsset` can call same cache for pitch-shifted clips |
| Project persistence v3 (`<pitch>` + per-track attrs, round-trip tested) | `AudioEngine::createProjectState/applyProjectState`, `ProjectTests.cpp` | Hostile-value clamp, omitted-track reset, reject-forward | **Port pattern** — VST DAW does `sanitize()` + `ProjectJson`; add `pitch` object + `pitch` per track, schema 5→6 |
| Bridge contract test (`bridgeContract.test.ts` + `gen-native-routes.mjs`) | `webui/scripts/gen-native-routes.mjs` | UI↔native drift caught in CI | **Port if React chosen** — VST DAW native UI doesn't need it; only relevant if React frontend approved |
| Test harness (727 Theory + 441 Project + malformed matrix + soaks) | `Source/tests/TheoryTests.cpp` | Pitch maths + round-trip + hostile matrix | **Port tests** — add to `tests/` (`test_pitch_stack.cpp`, `test_project_pitch.cpp`) |
| `knobs.hz` / `hzToCents` A4 model | `earshot forge/knobs.py` (A4=440) + `PitchStack::hzToCents` | Single source of truth for note↔Hz | **Already in PitchStack** — keep |

**Explicitly NOT porting v1:** theory core (`ChordEngine`/`Critic`/`Harmony`), FM synth `FMSynth`, drum machine, LLM (`LlamaServer`), WebView bridge, `signalsmith-stretch` audio DSP for clips (Maths-only v1, DSP Phase D optional).

---

## 3. Definitions (locked from ChordStudio)

| Knob | Range | Step | Engine |
|---|---|---|---|
| Transpose | -48…+48 st | 1 st | `pitch += transpose`, clamp 0..127 |
| Pitch | -100…+100 ¢ | 1 ¢ | Pitch-bend inject (14-bit, centre 8192, summed) |
| Hz | 415…466 Hz, def 440 | 0.1 Hz | `cents += 1200*log2(hz/440)` |

Total: `global + track + instrument` summed. Note-off pairing preserved by transposing both.

**VST DAW surfaces per your answer:**
- **Master/global** — one `PitchStack` for whole project (transport, bounce, all tracks). UI: MasterStrip or transport bar.
- **MIDI instrument** — per-track instrument scope (`trackInstrumentPitch`) + per-track scope. Applies to hosted VST instruments + `BuiltinSynth`.
- **Audio clips** — global+track ratio → `StretchCache`-style factor (offline bounce + `processAsset`). No live callback DSP v1 (latency).
- **Percussion VST** — own `PitchStack` on instrument slot when plugin is a drum machine; drum hits still transpose as MIDI notes (drum-channel 10 handled like ChordStudio).

## 4. Target architecture (VST DAW)

### 4.1 Engine (pure, testable without JUCE)
New files:
- `engine/include/nc/engine/PitchStack.hpp` — copy of ChordStudio `PitchStack.h` with `nc::engine` namespace, `PitchReducer` helper `applyPitchToMidiEvents()` testable without `juce::MidiBuffer` (operate on `MidiEvent` vector).
- `engine/include/nc/engine/MidiSchedule.hpp` — add `PitchStack` param to `scheduleMidiClip`/`scheduleMidiClipCursor` (extra overload or `pitchStack` arg, default identity to keep existing tests green).

Changes:
- `ProjectState.hpp`: `TrackState` += `PitchStack trackPitch, instrumentPitch`; `ProjectDocument` += `PitchStack globalPitch`; `projectSchemaVersion 5→6`; `sanitize()` clamps via `PitchStack::clamp()` (NaN/Inf safe, like ChordStudio).
- `ProjectJson.hpp`: `toJson`/`fromJson` write/read `"pitch":{"global":{...},"tracks":[{..., "pitch":{...},"instrumentPitch":{...}}]}`. Old files load defaults (0/0/440).
- `Automation.h`: extend `AutomationParam` with `Transpose, Pitch, TuningHz` (same lane shape; volume/pan lanes unchanged).

### 4.2 App (JUCE)
- `AudioEngine`: `std::atomic<int> globalTranspose`, `std::atomic<float> globalPitchCents`, `globalTuningHz` (like `Track` gain/pan). `setGlobalPitch()`, `globalPitchStack()` (message thread), `setTrackPitch()/setInstrumentPitch()` delegates.
- `AudioEngine::renderBlock` — after `scheduleMidiClipCursor`, call `PitchStack::apply` to transpose `MidiEvent.pitch` and inject `MidiEvent::Kind::bend`. Single choke point (mirrors ChordStudio `Track::renderSource`).
- `TrackInsertRack::Slot` + `HostedInsert` — per-instrument atomics (`instTranspose…`) on `Slot`/`instruments[]`, `stateToXml` equivalent via `PluginSlotState` + JSON `pitch` fields.
- `BuiltinSynth` — take `int transpose` + `float pitchCents` in `setWave`/noteOn (MIDI transpose already covers it if notes pre-transposed; synth just needs `pitchBend`).

### 4.3 Audio clip pitch (new, mirrors StretchCache)
- New `engine/include/nc/engine/AudioPitch.hpp` + `app/Source/StretchCache.h/.cpp` (port ChordStudio, trimmed to file→file render via signalsmith-stretch MIT).
- `AudioEngine::processAsset` — when `process` has `pitchCents`, render via cache (background) or inline (bounce). `renderToWav` uses same path → bounce matches preview.
- Licence: signalsmith-stretch MIT, header-only, no new dep. `libsamplerate` BSD-2 optional later for one-shot sampler (skip v1 per scope).

### 4.4 UI — React vs native (your choice: "if react looks better lets use a react frontend")

**Current VST DAW UI:** native JUCE — `Widgets::Knob` (drag/wheel/key, no ParamSpec text), `ChannelStrip` (5 slots + fader), `InspectorPanel`, `MixerPanel`, `Theme`. 64 tracks, 132 px strips.

**ChordStudio React UI:** `Knob.tsx` (ParamSpec min/max/decimals/unit, bipolar, editable Hz text, ARIA slider, fine drag Shift×0.1, 9 `PitchKnobs` in `MixView`/`ChainView` + `TransportBar`), Vite + Tailwind + `nativeRoutes.generated.ts` contract test. Looks better for 9-knob density + Hz typing.

| | Native JUCE (roadmap) | React/WebView2 (ChordStudio) |
|---|---|---|
| Pitch knobs | 9 `Widgets::Knob` instances (Transpose bipolar, Pitch bipolar, Hz needs text field — Knob has no editable) | `PitchKnobs` copy-paste, editable Hz, `toNorm/toValue` specs already written |
| Effort | 1–2 days, fits `ChannelStrip`/`MasterStrip` | 3–5 days: WebView host, bridge, `gen-native-routes`, mock backend, keep native panels or replace |
| Risk | Low — existing patterns | Medium — WebView2 Windows-only (VST DAW v1 is Windows x64, OK), but duplicates Mixer/Arrange panels |
| Roadmap deviation | None | **Yes** — roadmap §1 says "native UI kit stays. No browser rewrite." Requires roadmap amendment. |

**Plan recommendation:** Ship pitch on native `Widgets::Knob` + `LcdDisplay` for Hz readout (double-click to type via `HorizontalTextEditor` or small `TextEditor` in InspectorPanel) in Phase A–B (engine-only gate). Add React frontend as Phase C *only if* you approve the roadmap deviation and want the `MixView`/`ChainView` density. Both can share `nativeRoutes` contract test if React wins.

## 5. What to skip (and why)
- **React bridge by default:** VST DAW has no WebView host. Adding one is a framework change (JUCE `WebBrowserComponent` + `BinaryData` + `gen-native-routes.mjs` + Vitest). Only do it if you want the ChordStudio look.
- **FM synth / drums / LLM / theory critique:** User scoped "pitch + engine patterns" — not "full ChordStudio port". Those stay in ChordStudio.
- **Rubber Band / aubio / Faust:** Licence/toolchain cost, no gain over signalsmith for loops + resample for one-shots (bench evidence in `plans/pitch-test/REPORT.md`).
- **Master insert FX pitch:** Inserts are effects (no MIDI) in both DAWs — only instrument scope gets pitch. Percussion VST = instrument slot, covered.

## 6. Phases (engine-only verify)

**Phase A — Pitch engine (0.5 day)**
- Add `engine/include/nc/engine/PitchStack.hpp` (port `PitchStack.h`, `nc` namespace, `static constexpr` ranges, `clamp()` NaN-safe).
- Add `engine/include/nc/engine/MidiPitch.hpp` — `applyPitch(std::vector<MidiEvent>&, PitchStack)` + `totalCents()`; tests in `tests/test_pitch_stack.cpp` (copy ChordStudio's 12 checks: Hz→cents -31.8¢, clamp, NaN/Inf, sum, ratio).
- Schema 5→6 + `sanitize()` + `ProjectJson` round-trip + `tests/test_project_pitch.cpp` (global/track/instrument pitch, omitted-track default).
- **Gate:** `ctest -R pitch|project` green (your "engine only" ask).

**Phase B — App wiring (0.5 day)**
- `AudioEngine` global atomics + `renderBlock` transform (mutate `MidiEvent.pitch` + inject bend).
- `TrackInsertRack` per-instrument atomics (Slot + `HostedInsert::setPitch`), `BuiltinSynth` transpose/bend.
- `Automation.h` pitch lanes (optional, same `AutomationLane`).
- **Gate:** existing `nc_tests` + `NCMixRackTests` still green, no callback alloc/lock.

**Phase C — Audio + scanner (1 day, optional but recommended)**
- Vendor `third_party/signalsmith-stretch/` (MIT) + `app/Source/StretchCache.h/.cpp` (trim ChordStudio to file→file).
- `processAsset` pitch path + `renderToWav` (bounce = preview).
- Out-of-process scanner (`ChildProcessCoordinator` + dead-man's pedal) port from `PluginHost`/`ScannerProtocol` — fixes roadmap hang risk.
- **Gate:** bounce vs live spectral diff < threshold (earshot `measure` on rendered WAVs).

**Phase D — UI (native 0.5 day / React 2–3 days)**
- Native: `Widgets::Knob`×9 (MasterStrip global, ChannelStrip track + instrument header) + Hz `TextEditor` in `InspectorPanel`. No bridge.
- *OR* React: WebView host in `MainComponent`, `PitchKnobs` copy, `bridge.ts` + `gen-native-routes` + contract test, keep native `ArrangeView`/`PianoRoll` for now (hybrid).

**Phase E — Hardening**
- Pluginval, hostile-project fuzz (pitch fields already clamped), Plugin Doctor harmonic sweep (1 kHz THD) for stretch vs resample confirmation.

## 7. Risks
- **Native Hz editing:** `Widgets::Knob` has no text entry — need `TextEditor` pop-up; React `Knob` already has it. If native feels clunky, that's the trigger for React.
- **Note-off pairing:** Transpose change mid-note — snapshot pitch at block start (like ChordStudio `RtPublisher` borrow), transpose on+off together.
- **Audio clip latency:** signalsmith 2048–4096 frames → offline only v1; realtime clip pitch needs warp markers (out of scope).
- **Schema migration:** v5→v6 files load defaults (0/0/440); `isLoadable` stays `<= projectSchemaVersion`.
- **React deviation:** If you pick React, roadmap §1 and `PHASE-02-PLAN.md` need amendment (WebView2 Windows-only already matches v1 platform).

## 8. Open questions (one-line)
1. Confirm native first vs React-first (Phase D split)?
2. Audio clip pitch in v1 (Phase C) or defer to follow-up?
3. Percussion VST "own pitch" = instrument slot only (as planned) or separate drum-machine pitch bus?
4. No full JUCE build in this pass — is engine-only gate enough for you?

## 9. Next step
Reply `approve` (or pick Phase D variant) and I implement Phase A–B with engine tests only, exactly as scoped. No code has been written yet.

---
*Teams: source `C:\Users\Asus\Desktop\Plugins\ChordStudio-master`, target `C:\Users\Asus\Desktop\Plugins\VST DAW`. No code changed.*
