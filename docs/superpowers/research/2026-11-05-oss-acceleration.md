# OSS acceleration research — NCSound DAW

**Date:** 2026-11-05
**Method:** six parallel research passes (engine, UI bridge, DSP, hosting, UI components, I/O + build), primary sources only.
**Question:** which open-source projects remove work from the Phase 1–5 plans, and which are licence traps.

---

## Decision summary

| Item | Licence | Verdict | What it removes |
|---|---|---|---|
| `@juce-framework/webview` (JUCE 9 npm) | AGPLv3 / commercial JUCE | **Adopt** (if not on Tracktion Engine) | Plan Task 11 bridge codegen, most of `WebEditorBase` plumbing |
| Tracktion Engine 3.5 | GPLv3 / commercial | **Spike 3 days**, do not adopt blind | Plan Tasks 3–9 (engine, PDC, hosting, automation, render, recording) |
| `audio/ui` React primitives | MIT | **Spike**, likely adopt for M4 | Arrangement + piano roll + mixer + meters |
| VST3 SDK 3.8 hosting helpers | **MIT** | **Adopt** | Hand-written VST3 loading in Tasks 6/13 |
| Airwindows DSP | MIT | **Adopt selectively** | Saturation/character/limiter/cab for the stock suite |
| CloudSeedCore | MIT | **Consider** | Modulated FDN tail for the reverb merge |
| RTNeural / NAM Core | BSD-3 / MIT | **Consider** for M10 | WaveNet inference for Aero Amp |
| dr_wav | PD / MIT-0 | **Adopt** | Float WAV recording in M3 |
| signalsmith-stretch | MIT | **Adopt** | Pitch/time; already used by the Rompler |
| pluginval | GPLv3 | **CI only**, never linked | Plugin conformance gate |
| chowdsp_utils (BSD modules) | per-module | **Adopt BSD half only** | Presets, parameters, oversampling, state |
| Rubber Band | GPL-2.0+ | **Reject** | — |
| libsndfile | LGPL-2.1/3 | **Reject** (DLL-only, no buyout) | — |
| Guitarix / GxPlugins | GPLv2+/v3 | **Reject** | — |
| CloudReverb / njazz CloudSeed ports | GPL-3.0 | **Reject** | — |
| `chowdsp_eq` | GPLv3 | **Reject** | — |

---

## 1. UI bridge — JUCE's own TypeScript package

JUCE 9.0.1 (2026-08-10) published [`@juce-framework/webview`](https://www.npmjs.com/package/@juce-framework/webview); the interop library moved to `modules/juce_gui_extra/native/typescript/webview-interop` ([release note](https://forum.juce.com/t/juce-9-0-1-is-out/69269), [BREAKING_CHANGES](https://github.com/juce-framework/JUCE/blob/master/BREAKING_CHANGES.md)). Latest observed: 9.0.4 (2026-09-28).

Provides: native function calls both directions (Promises), `withResourceProvider` for serving the React bundle from inside the binary, parameter binding to `AudioProcessorParameter`, mock objects so the UI runs in a plain browser against Vite, and frontend/backend version assertions.

Consequences for the plan:

- Task 11's `routes.json` → C++ dispatcher + TS client generation becomes a thin typed wrapper over `withNativeFunction`, not a codegen project.
- Add `NEEDS_WEBVIEW2` in CMake and call `withNativeIntegrationEnabled()`; without it Windows falls back to IE.
- Plugin editors need a per-instance `withUserDataFolder`, or several editors in one host fight over the same WebView2 profile.
- Licence is JUCE's, not MIT: AGPL or commercial.

## 2. Engine fork — Tracktion Engine vs extracting ChordStudio

[Tracktion Engine 3.5](https://github.com/Tracktion/tracktion_engine) is 115k lines, C++20, and provides the edit/track/clip model, plugin delay compensation, VST3 hosting, automation with modifiers, offline render, recording, warping, undo, MPE, clip launcher and DAWproject import. You write **all** the UI, the shell, settings, scanning UI and command layer.

Cost: GPLv3 free, or commercial at $35/seat/mo (indie, ≤$200k revenue) through $300/seat/mo, 12-month commitment, seat count fixed at the historical maximum for the product's life ([pricing](https://engine.tracktion.com/)). JUCE is licensed separately.

Compatibility risk: it pins a specific JUCE commit rather than "any JUCE 8" ([BREAKING-CHANGES](https://github.com/Tracktion/tracktion_engine/blob/develop/BREAKING-CHANGES.md)), and that conflicts with JUCE 9's npm interop. **Adopting TE means JUCE 8 and the older in-tree JS interop.**

**Spike protocol (3 days):**

1. Clone with submodules, build `DemoRunner` and a minimal 4-track edit that renders offline, on this machine (8 GB RAM, ~3 GB free).
2. Record: clean build time, incremental build time, release binary size, peak RAM during build and playback.
3. Load one third-party VST3 and one of the Overlay FX; confirm parameter automation and PDC.
4. Decision rule: if clean build ≤15 min and playback is stable at 128 samples, TE is the cheaper path and Tasks 3–9 collapse. Otherwise extract ChordStudio's engine as planned.

## 3. DAW UI components

[`audio/ui`](https://www.audio-ui.dev/) — MIT, React 19, active (v0.10.0 on 2026-11-03). Arrangement/Timeline with clip snapping and edge-scroll, piano roll with split/join/legato/humanize/scale-fit, mixer with constant-power pan and delay compensation, waveform with regions, meters that update without React re-renders, AudioWorklet clip player, metronome scheduling.

Caveats: renders DOM nodes per note, so a 5,000-note pattern is unproven inside WebView2; its playback/mixer math is Web Audio, which we **do not** use — native engine only, UI primitives only.

Supporting picks: `wavesurfer.js` (BSD-3, v8) for overview waveforms with precomputed peaks; `zustand` (MIT) with transient subscriptions for playhead/meters at 60 Hz; `TanStack Virtual` for track and browser lists. `peaks.js` is LGPL-3 — only if legal accepts it. `dnd-kit` is mid-rewrite; hand-roll drag for clips and notes.

## 4. Plugin hosting

- **VST3 SDK 3.8 is MIT** — the old GPL/proprietary split is gone ([Steinberg portal](https://steinbergmedia.github.io/vst3_dev_portal/pages//What+is+the+VST+3+SDK/Index.html)). Use `VST3::Hosting::Module`, `PluginFactory`, `getModulePaths()`, `validateBundleStructure()`, `PlugProvider`, `HostInterfaceSupport`.
- Crash isolation is scan-time only in JUCE: `PluginDirectoryScanner` spawns a child process and keeps a "dead man's pedal" list. Runtime sandboxing is not available off the shelf under a permissive licence — Carla's bridge is GPL and is reading material, not a dependency.
- **pluginval** is GPLv3 and runs validation out-of-process by default. Invoke the binary from CI; never link it.

## 5. Stock suite DSP

- **Airwindows** is MIT ([LICENSE](https://github.com/airwindows/airwindows/blob/master/LICENSE)). Vendor `libs/airwindows` + the `airwin-registry` target from airwin2rack; **do not** copy `src-juce`, which drags GPL through JUCE/VST3 build files. Style is DSP-in-`process()`, so it is copy-and-refactor per effect, not a linkable SDK. Best candidates: ADClip, Console/C5Raw, Density, ToTape, Cabs.
- **CloudSeedCore** (GhostNoteAudio) is MIT and is the only clean CloudSeed. The popular JUCE ports are GPL-3.0. Bare C++14 core: needs block splitting, smoothing and a `juce::dsp::ProcessorBase` wrapper.
- **RTNeural** (BSD-3, header-only) and **NeuralAmpModelerCore** (MIT) give amp inference without writing WaveNet. NAM's `.nam` captures are per-uploader licensed — ship no community weights without checking.
- **chowdsp_utils**: `core`, `buffers`, `json`, `serialization`, `parameters`, `clap_extensions` are BSD; `chowdsp_eq` is GPLv3. Audit each module header before vendoring.

## 6. Recording and file I/O

`dr_wav` (public domain / MIT-0) handles 32-bit float WAV read/write and drops in as one header; JUCE has no float WAV writer. Peak files are application code — one pass over the float frames while recording.

Reject `libsndfile` (LGPL, DLL-only, "no licence can be bought" per its FAQ) and `Rubber Band` (GPL-2.0+ or paid). `signalsmith-stretch` (MIT, header-only) covers stretch/pitch; compile that translation unit optimised even in Debug or it runs ~10× slow.

## 7. Build acceleration

- **Ninja generator**: measured 1.5–2× on clean builds versus MSBuild for large C++ trees.
- **PCH** for our headers only (`target_precompile_headers`), excluding JUCE sources.
- **LTO off** in Debug/RelWithDebInfo; keep it for release only.
- **sccache in CI** only (local hits are marginal, cold builds pay a penalty). Use `/Z7`, not `/Zi`.
- **Never unity-build over JUCE sources** — modules are already unity builds and it produces duplicate symbols.

## 8. Licence frame

If the DAW ships closed source: JUCE commercial licence is mandatory, GPL/AGPL dependencies are excluded (Rubber Band, pluginval linking, Guitarix, CloudSeed ports, `chowdsp_eq`, Carla). Tracktion Engine adds a separate subscription. If it ships open source under GPL/AGPL, most of these costs disappear and TE becomes free.

## 9. Proposed plan amendments

- **Task 1:** Ninja generator, PCH for own headers, LTO off in Debug, sccache job in CI.
- **Spike 0 (new, before Task 3):** Tracktion Engine protocol above; a written go/no-go in `docs/phase-1-report.md`.
- **Task 6/13:** VST3 SDK MIT helpers + JUCE out-of-process scanner; pluginval in CI as an external binary.
- **Task 11:** if JUCE 9, replace the codegen step with `@juce-framework/webview`; if TE wins, stay on JUCE 8 and keep the thin in-tree JS bridge.
- **M4:** spike `audio/ui` at 2,000 notes inside WebView2 before committing to it.
- **M5/M8:** Airwindows shortlist, CloudSeedCore for the tail, `chowdsp_utils` BSD modules.
- **M10:** RTNeural/NAM Core; no bundled captures.

## 10. Unverified

- No published build-time or binary-size figures for Tracktion Engine 3.5 — the spike exists because of this.
- `audio/ui` large-pattern performance inside WebView2 is untested.
- Tracktion has not published a non-GPL pluginval licence; assume none.
- NAM community capture licensing remains per-uploader and unresolved.
