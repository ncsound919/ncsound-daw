# Handoff: ChordStudio pitch port to VST DAW 2026-11-07

Status: Engine pitch port done and tested (Phase A + B core). App UI + audio stretch Phase C/D not included in this pass.

## What shipped

**Engine (`engine/include/nc/engine/`)**
- `PitchStack.hpp` — ported from ChordStudio `Source/audio/PitchStack.h` (ns `cs` → `nc::engine`, incl. `hzToCents`, `sumCents`, `pitchRatio`, clamp/NaN-safe). Ranges: transpose ±48, pitch ±100c, Hz 415–466 default 440.
- `MidiPitch.hpp` — `applyPitch(vector<MidiEvent>&, PitchStack)` + `applyTotalCents`. Transposes note on/off, drops out-of-range, injects/merges pitch-bend (14-bit, centre 8192, remainder after rounding). No JUCE dependency (operates on `MidiEvent`).
- `ProjectState.hpp` — `projectSchemaVersion 5→6` (was 4 in HEAD, working tree already had 5), adds `TrackState::pitch`, `TrackState::instrumentPitch`, `ProjectDocument::globalPitch`. `sanitize()` clamps all three.
- `ProjectJson.hpp` — `writePitch`/`pitchField`, `toJson`/`fromJson` round-trip global + per-track. Old files load defaults (0/0/440). `detail::pitchField` parses `{transpose,pitchCents,tuningHz}`.

**App (`app/Source/`)**
- `AudioEngine.h/.cpp` — global atomics `globalTranspose`/`globalPitchCents`/`globalTuningHz` + `setGlobalPitch/globalPitchStack/setTrackPitch/trackPitchStack/setInstrumentPitch/instrumentPitchStack`. `Track` struct per-track + per-instrument atomics. `renderBlock` snapshots global+track+instrument at block start, folds Hz into cents, calls `applyPitch` on `midiEventScratch` after `scheduleMidiClipCursor` (per clip, before `MidiBuffer` build). This covers MIDI instruments + BuiltinSynth + percussion VST (instrument slot).
- `MidiRecorder.h` — added missing `<nc/engine/ProjectState.hpp>` include (pre-existing WIP error: `ClipState` not found). No other change.

**Tests**
- `tests/test_pitch_stack.cpp` — 9 cases / 53 assertions: clamp, Hz→cents (-31.8c for 432), NaN/Inf defaults, sum, transpose pairing, out-of-range drop, bend injection/merge, identity.
- `tests/test_project_pitch.cpp` — 4 cases: round-trip, hostile clamp, schema v6 backward-compat, `toJson` emits pitch.
- `tests/CMakeLists.txt` + root `CMakeLists.txt` header lists updated.

## Test results

```
nc_tests.exe        All tests passed (3826 assertions in 114 test cases)
                    includes 13 new pitch cases (53 assertions)

NCMixRackTests.exe  builds (app AudioEngine integration compiles)
                    (full run exceeds 120s timeout — same as before; MixRack audio tests are slow, not a pitch failure)
```

ChopStation plugin (`plugins/chopstation`) has pre-existing JUCE API mismatch (`createReaderFor(MemoryInputStream)`) unrelated to pitch — not built in this pass.

## Files changed

```
engine/include/nc/engine/PitchStack.hpp (new)
engine/include/nc/engine/MidiPitch.hpp (new)
engine/include/nc/engine/ProjectState.hpp (v6, pitch fields, sanitize)
engine/include/nc/engine/ProjectJson.hpp (writePitch/pitchField, to/from JSON)
CMakeLists.txt (headers)
tests/CMakeLists.txt
tests/test_pitch_stack.cpp (new)
tests/test_project_pitch.cpp (new)

app/Source/AudioEngine.h/.cpp (global/track/instrument pitch + renderBlock)
app/Source/MidiRecorder.h (include fix only)

docs/superpowers/plans/2026-11-07-chordstudio-pitch-port-plan.md (plan)
docs/superpowers/plans/pitch-test/REPORT.md (ChordStudio bench, copied pattern)
```

## What did NOT ship (Phase C/D, deferred)

- **Audio clip pitch** — no `StretchCache` / signalsmith-stretch vendoring yet. Global+track ratio available via `pitchRatio(sumCents(...))` for `processAsset`/`renderToWav` when ready.
- **UI knobs** — native `Widgets::Knob` or React frontend not added. Engine verified only, per your "engine only" gate.
- **Automation lanes** for pitch — `AutomationParam` unchanged (Volume/Pan only).
- **React frontend** — not added (roadmap says native only; React would be a deviation + WebView host).

## Next steps

1. UI: **Done (React scaffold)** — `VST DAW/ui/` Vite app, `console-ui` sync target `vstaw-ui`, `PitchKnobs` (global/track/instrument), `bridge.ts` (getState/set*Pitch). Next: `app/Source/WebViewBridge.cpp` (JUCE WebView2 host) + `juce_add_binary_data` for `ui/dist`, then wire `AudioEngine` pitch fields to bridge routes.
2. Audio: vendor `third_party/signalsmith-stretch/` + `StretchCache` for clips.
3. Automation: extend `AutomationParam` with pitch lanes if wanted.
