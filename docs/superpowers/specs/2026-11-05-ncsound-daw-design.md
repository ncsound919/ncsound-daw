# NCSound DAW — design

**Status:** draft for approval · 2026-11-05
**Owner:** ncsound919
**Home:** `Desktop/Plugins/VST DAW`

## 1. Intent

A Windows-first recording and production DAW built on the engine that already
exists in ChordStudio, where the NCSound plugin suite is the stock rack and any
VST3 loads beside it. ChordStudio is retired as an application and used as a
source: its audio engine, plugin host and six effects come across; its chord
generator, drums and app shell do not. Apex is retired as an application: its
library model and its ideas fold into the instrument suite.

## 2. Goals

1. Record audio and MIDI, monitor through the plugin chain, low-latency enough
   to track guitar and vocals through the stock chain (amp and mic modeling follow in M10).
2. Host any VST3 instrument or effect, with an out-of-process scanner and
   crash isolation.
3. Ship the suite as stock — preinstalled, in the browser, offered as defaults
   per track type — loaded through the identical path as third-party plugins.
4. One UI system across DAW and plugins, carrying the NCSound brand.
5. Projects versioned and portable; a missing plugin is reported, never
   silently dropped.

## 3. Non-goals for v1

macOS/Linux, AU/CLAP, video, notation, clip-launch grid, collaboration, mobile,
plugin marketplace, full comp lanes (take list with swipe comp instead).

## 4. Anti-goals

A second visual language anywhere. A stock plugin that only works inside the
DAW. Recording you cannot hear while performing. A UI update that rebuilds the
whole app state per knob turn — ChordStudio's deferred BR-3/BR-6 defect must
not be inherited.

## 5. Constraints

Windows x64 (ASIO, WebView2). No allocation, locks or I/O on the audio
callback; `PLUGIN_AUDIT.md` findings are the baseline. JUCE is AGPL or
commercial. Build box: 8 GB RAM, 9.5 GB free on D:.

## 6. Approaches considered

- **A — framework first** (recommended, deferred): name `fx_core` +
  `WebEditorBase` + `console-ui` as the SDK, rebuild plugins on it, then host.
- **B — DAW first** (chosen): build the host and its functions, then wire the
  suite through it.
- **C — suite first**: rejected, delays the product.

Accepted trade-off in B: the UI kit is shaped by the largest surface before
eight small ones test it. Mitigation: the kit ships as a versioned package the
DAW consumes, never a copy.

## 7. Architecture

Monorepo, CMake superbuild, JUCE pinned 8.0.14 (already `fx_core`'s version).

```
engine/    extracted from ChordStudio Source/audio: graph, latency, submix,
           ducking, RtPublisher, PluginHost, PluginSlot, Scanner, export
host/      new JUCE standalone app + WebView2 editor + generated bridge
sdk/       fx_core promoted: DSP primitives, Params, Theme, WebEditorBase
ui-kit/    React: brand tokens, primitives, meters, waveform, param binding
plugins/   stock suite
schema/    session + manifest + config, v2
tools/     golden renders, scan probes, installer
```

The engine gets a `Session` facade so it knows nothing about the shell. The
host pushes **delta** events, never full state snapshots.

## 8. Session format v2

Keeps `vstdaw.schema.json`'s manifest/session/config split, node/edge graph,
patterns, clips, mixer and automation, and adds:

- `assets[]` — recorded audio: id, path, sample rate, channels, length, hash,
  peak cache
- `tracks[]` — kind (`instrument | audio | bus | return | master`), input
  channel, monitor mode, armed, record offset
- `clips[]` — `kind: pattern | audio`, `assetId`, asset offset, gain, fades;
  `takes[]` for comping
- `plugins[]` — `stock: true` for first-party, plus format, version, fallback
- `version` with reject-forward

## 9. Audio path

ASIO first, WASAPI fallback; project sample rate locked at open; buffer
128–512. Recording writes 32-bit float per take into `Media/` from a writer
thread fed by the audio callback. Monitoring runs through the chain by default
with a direct-monitor toggle; record offset = reported input+output latency +
manual trim. Meters and waveforms at 30 Hz, never on the audio thread.

## 10. Tracks and arrangement

Kinds: instrument, audio, bus, return, master. Each carries inserts, sends and
a sidechain source. Arrangement is **one clip timeline**, with patterns edited
on their own page. No clip-launch grid. *(My call — overridable.)*

## 11. Stock rack

Installer provides six Overlay FX, Rompler and Supa Level. Aero Amp and Open Mic are deferred to M10 (section 15).
Defaults: audio and instrument tracks get Channel Strip; master gets Overlay
Master; returns get Reverb and Delay. Tagged `first-party, stock`, loadable in
any other DAW.

## 12. Brand — NCSound Music Hub

**Architecture:** Overlay365 (master) → NCSound Music Hub (division, endorsed)
→ products. The supplied lockup reads "NCSOUND MUSIC HUB / a division of
Overlay 365", which matches the endorsed-family decision already recorded in
`brand/ARCHITECTURE.md`.

### Palette (measured from the supplied logo, not eyeballed)

| Token | Value | Use |
|---|---|---|
| `ink` | `#181818` | window background (dominant colour in the file) |
| `well` | `#0B0B0B` | LCD wells, waveform troughs |
| `surface` | `#222222` | panels |
| `surfaceHi` | `#2C2C2C` | raised / hover |
| `line` | `rgba(248,232,200,0.10)` | hairlines |
| `paper` | `#F8E8C8` | primary text |
| `paperDim` | `rgba(248,232,200,0.62)` | secondary text |
| `accent` | `#C25A2A` | value, active, focus |
| `accentHot` | `#E0824D` | hover, glow, peak |
| `rust` | `#8E3A1E` | deep accent, dipped state |
| `mod` | `#E4A15C` | modulation routing only *(my call)* |
| `danger` | `#E0503C` | destructive, error |

One accent: burnt orange. Cream is text, never a surface fill above 8%
opacity. Cyan from the old console kit is retired — it fights the warm palette
and breaks the one-accent rule.

### Type

- Display: **Bricolage Grotesque** (OFL) — headings and lockups only
- UI/body: **Plus Jakarta Sans** (OFL)
- Technical: **JetBrains Mono** (OFL) — every id, count, time, frequency, hash

Maximum two families on a surface. The wordmark is drawn art, not type; never
re-set it in a UI font.

### Logo status — gap to close

The only asset is a 2752x1536 JPEG at `Plugins/ncsound hub logo.jfif` (2.1 MB)
and a copy in `brand/logos/`. There is **no SVG or transparent PNG** anywhere
in either repo. Usable now for splash, installer, About panel and marketing
(cropped on `#181818`). Not usable in chrome, because it has no alpha and
carries JPEG artefacts. Needed: a vector redraw of the circular
record-plus-waveform monogram for toolbar and plugin headers, with the wordmark
kept as an image. M0 deliverable, your sign-off on the redraw.

### Geometry and motion

4pt grid; radii 8/12/16; motion <=200ms, transform/opacity only; focus ring 2px
`accent`; no decorative glows except focus. The record motif is allowed as a
static device, never wallpaper.

## 13. UI system

One React kit consumed by the host and by every plugin editor through
`WebEditorBase`. Primitives: knob, fader, key, LED, readout, toggle, meter,
waveform canvas, spectrum, panel shell. Parameter binding and gestures come
from `WebEditorBase`; the kit never owns a bridge.

## 14. Milestones

| # | Milestone | Exit |
|---|---|---|
| M0 | Build rail, monorepo, CI, logo vector | clean build, tests run, monogram in repo |
| M1 | Engine extraction | ChordStudio core suites pass on the extracted engine |
| M2 | Host shell + hosting | third-party VST3 loads, pattern plays, project saves |
| M3 | Recording | monitor through the stock Channel Strip, punch, takes recalled |
| M4 | UI kit + DAW screens | transport, browser, mixer, pattern, arrangement |
| M5 | Stock FX wired | defaults on new tracks; six pass pluginval |
| M6 | Stock instruments | Rompler rebuilt; Supa Level; amp and mic deferred |
| M7 | Browser tools to VST3 | Chop Station, Orchestra, Apex voices |
| M8 | Consolidation | one owner per effect; duplicates deleted |
| M9 | Hardening | soak, crash matrix, installer, clean-VM install |
| M10 | Amp and mic modeling | Aero Amp tube stages built, Open Mic models built, both pluginval-clean |

## 15. Risks (verified 2026-11-05)

**Aero Amp cannot ship in v1.** `plugins/aero-amp/Source` is 663 lines total, and
`ToneStack.cpp` / `TubeStage.cpp` are literal placeholder files. `AmpModel.h` is a
three-band biquad tone stack with model names on it, not a tube amp. The separate
`Aero Amp DSP/` folder is a design pack (FMX project, params JSON, AssemblyScript
sketch), not shippable C++. Real tube stages and cabinet work are a build, not a port.

**Open Mic cannot ship in v1.** Its README says "Status: scaffold": manifest loader,
halfband oversampler and one ADAA saturation stage are done and tested; tube,
transformer, BJT and capsule models, source correction and the JUCE shell are not
started.

**Consequence:** v1 records through the stock Channel Strip and Overlay FX. Amp and
mic modeling become M10, after the DAW ships.

Other risks:
- `AudioEngine.h` still references `MainComponent`; the coupling looks confined to a
  listener type, but it must be cut during extraction.
- WebView2 cost per plugin instance.
- 8 GB RAM, 9.5 GB free on D:.
- ASIO driver variance; large-VST-folder scans.

## 16. Decisions

Resolved:
1. Brand: NCSound Music Hub — palette and lockup as in section 12.
2. Accent: burnt orange replaces console amber and Apex cyan.

Open:
1. Arrangement model — clip timeline with a separate pattern page, or a launch grid too.
2. DAW product name.
3. Licence — open (GPL/AGPL) or closed (needs a JUCE commercial licence).
4. v1 stock rack — now seven plugins, unless you want Aero Amp and Open Mic built first.
5. Logo vector redraw — approve the monogram redraw.

## 17. What is left to the builder

Milestone detail, exact v1 function cut, comping depth, monitoring defaults, stock
default chains per track type, and per-plugin migration order once the kit exists.
