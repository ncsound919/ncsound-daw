# Phase 7: Monolith Decomposition - Context

**Gathered:** 2026-11-14
**Status:** Ready for planning
**Source:** Direct request + three locked decisions from the user

<domain>
## Phase Boundary

Reduce the three oversized app translation units to maintainable sizes **without changing a
single audible sample or a single pixel**, then extract real components behind the existing
`AudioEngine` facade.

Measured starting state (from `wc -l` and the function map at planning time):

| File | Lines | Header | Largest single function |
|------|-------|--------|-------------------------|
| `app/Source/AudioEngine.cpp` | 4564 | 868 | `renderBlock` — 741 lines (2402-3143) |
| `app/Source/ArrangeView.cpp` | 3121 | 267 | `mouseDown` — 243 lines; 12 `paint*` functions — ~1000 lines |
| `app/Source/MainComponent.cpp` | 2736 | 147 | `handleMenuResult` — 744 lines, 86 `case` labels; `openMenu` — 155 lines, 84 items |

Total in scope: **10,421 lines of .cpp + 1,282 lines of .h**.

Out of scope: `WebViewBridge.cpp` (1031), `Widgets.cpp` (1125), everything under `engine/`
(already header-per-concern), and any behaviour change, new feature, or API redesign.
</domain>

<decisions>
## Implementation Decisions

### Strategy

- **D-01 — Staged extraction, `AudioEngine` stays a facade.** Two stages. Stage A: move member
  function bodies into sibling translation units (`AudioEngineDevice.cpp`,
  `AudioEngineAssets.cpp`, …) with the class definition untouched. Stage B: extract
  `AssetLibrary` and `TransportClock` as real classes with headers, leaving the rest of the
  engine as the facade that owns them. Stage B only starts after Stage A is green.
- **D-02 — Public API frozen for the whole phase.** No signature in `AudioEngine.h`,
  `ArrangeView.h` or `MainComponent.h` may change. Every call site in `WebViewBridge.cpp`,
  `MainComponent`, `MixerPanel`, `ProjectFile` and the 25 test files must compile unmodified.
  Stage B may move members between classes but the public surface must not shift.
- **D-03 — One concern per commit-step, each ending green.** Every task ends with a full build
  and a full `ctest` run. No step is left half-moved "to finish later".

### Verification

- **D-04 — A render digest is the DSP safety net, and it lands first.** Before any engine line
  moves, `app/Source/RenderDigestTests.cpp` renders a fixed project through
  `processBlockForTesting` and hashes every output sample (FNV-1a over float bit patterns).
  The digest is captured from current code and asserted forever after. Demo assets are
  deterministic (`juce::Random random (0x4e430000 + track * 7919)`), so the digest is stable.
- **D-05 — A move-only guard proves bodies were moved, not edited.** `scripts/verify-move-only.mjs`
  parses function definitions from a recorded baseline commit and from the working tree, and
  fails if any pre-existing function body changed by even one non-whitespace character. New
  functions are allowed. This is the safety net for the two UI files, which have no C++ tests.
- **D-06 — Full suite must stay 195/195.** `ctest --test-dir build -C Release` currently reports
  `Total Tests: 195`. It must report 195 passing (plus the new digest case inside `mix_rack`)
  after every task.
- **D-07 — UI shell must stay 81/81.** `ui` vitest must stay green, because the React shell is
  the only automated coverage of the behaviour `ArrangeView` and `MainComponent` expose.

### Mechanics

- **D-08 — File-static helpers move to `app/Source/AudioEngineInternal.h`.** `twoPi`,
  `smoothingCoefficient`, `softLimit`, `toneProbe()`, `dumpStage()` and `audioPropsOptions()` are
  currently anonymous-namespace. They must become `inline` in one internal header, or every new
  translation unit gets its own copy (tone probe and dump state would then double, which is an
  observable behaviour change: `NC_AUDIO_DUMP` would write two files per stage).
- **D-09 — `renderBlock` splits into its four existing passes.** The comments already name them:
  pass 1 clips into lanes (2402-2500), pass 2 instruments/inserts/PDC/MIDI (2501-2820), pass 3
  native strip/fader/pan/click/master gain (2821-3100), pass 4 master rack/aux/monitor/limit
  (3101-3143). They become private methods on the class; the per-block locals they share stay
  where they are passed explicitly, never via new members.
- **D-10 — `MainComponent`'s 86-case `handleMenuResult` becomes a command table.** A static
  table maps menu id → member function pointer; `handleMenuResult` becomes a lookup. `openMenu`
  builds its 84 items from the same ids so the two can no longer drift.
- **D-11 — No new third-party dependency, no build-system redesign.** Only additive entries in
  `app/CMakeLists.txt` `target_sources`.

### Discretion

- Exact thread-boundary comments and `// ---- ` section banners inside new files.
- Whether `AssetLibrary` (D-01) ends up in `app/Source/` or `engine/include/nc/engine/`. Prefer
  `app/Source/` unless the class turns out to have zero JUCE-component dependency.
- Order of tasks inside plan 07-03 where two moves are independent.
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### The files being split
- `app/Source/AudioEngine.cpp` — 4564 lines, the primary target
- `app/Source/AudioEngine.h` — 868 lines, frozen public API
- `app/Source/ArrangeView.cpp` / `.h` — 3121 / 267 lines
- `app/Source/MainComponent.cpp` / `.h` — 2736 / 147 lines

### Build and test wiring
- `app/CMakeLists.txt` — `NCMixRackTests` (compiles `AudioEngine.cpp`), `NCSoundHub` (compiles all three)
- `build/_ncbuild_ok.cmd` — the only reliable build entry (runs `vcvars64.bat`, builds three targets)
- `tests/CMakeLists.txt` — engine-level tests, 195 total registered
- `.planning/phases/01-audio-recording/PLAN.md` — the plan format this project already uses

### Conventions to copy
- `app/Source/SignalIntegrityTests.cpp` — `TEST_CASE (...)` / `[tag]` style, `INFO`, engine setup and `suspendAudioCallbackForTesting()` idiom
- `app/Source/TrackInsertRack.h` — the project's pattern for an extracted, well-bounded engine component: audio/message thread split documented in the header, retirement by block counter, `JUCE_DECLARE_NON_COPYABLE`
- `app/Source/RecordingTests.cpp` — `processBlockForTesting` usage for headless block rendering
</canonical_refs>

<specifics>
## Specific Ideas

Known rendering constraint that the digest test must respect:

- `renderBlock` flushes denormals for the whole native chain; the aux reverb combs and the delay
  lowpass decay exponentially, so a stale denormal mode changes the digest. Do not add per-test
  FTZ toggles.
- Faders are only smoothed during playback, and smoothers are primed on the play transition
  (`smoothingPrimed`). A digest that starts mid-playback will differ from one that starts stopped.
  The test must start from a reset transport and the first block.

Line-count targets for the finished phase (each file < 1000 lines):

| Target file | Approx lines | Moved from |
|---|---|---|
| `AudioEngine.cpp` | ~350 | ctor/dtor, track params, `voiceToMidi*`, test hooks |
| `AudioEngineDevice.cpp` | ~260 | settings, device state, latency, low-latency mode |
| `AudioEngineAssets.cpp` | ~390 | `loadAudioFile`, sampling, resampling, streaming, peaks |
| `AudioEngineDemo.cpp` | ~240 | `createDemoAssets` |
| `AudioEngineTransport.cpp` | ~520 | transport, tempo, markers, MIDI clips, timeline, automation |
| `AudioEnginePlugins.cpp` | ~800 | catalog, instrument/master/insert hosting, MIDI learn |
| `AudioEngineExport.cpp` | ~500 | `prepareForRender`, `renderToWav`, `renderToFile`, `exportStems` |
| `AudioEngineRecording.cpp` | ~460 | `RecordBuffer`, capture, MIDI record, takes |
| `AudioEngineRender.cpp` | ~900 → ~200 per pass after 07-04 | callbacks, `renderBlock`, aux, mix-rack state |
| `ArrangeView.cpp` (+) `Paint`/`Mouse`/`Clips`/`Project` | ~500 + 4 files | geometry, paint, mouse, clip ops, project I/O |
| `MainComponent.cpp` (+) `Paint`/`Transport`/`Session`/`Project`/`Menus` | ~780 + 5 files | chrome, recording, session, project I/O, menus |
</specifics>

<deferred>
## Deferred Ideas

- `WebViewBridge.cpp` (1031 lines) — same treatment, separate phase; it is mostly one route table.
- `Widgets.cpp` (1125 lines) — mostly independent paint helpers, low value.
- Splitting `AudioEngine.h` (868 lines) by accessor group — only after Stage B, otherwise the
  header churns twice.
- Any change to `engine/include/nc/engine/*` headers — they are already one-type-per-file.
- Performance work on `renderBlock`. Decomposition must be cost-neutral; if the digest passes but
  `dspLoad()` regresses, that is a new phase, not this one.
</deferred>

---

*Phase: 07-monolith-decomposition*
*Context gathered: 2026-11-14 from three locked user decisions plus code reconnaissance*
