# MixRack v3 — channel strip, master and Mixology autopilot

This is the runtime behind the Mixer's **Channel strip** and **Master rack**
buttons. It is the same DSP the engine renders with; the standalone Overlay
plugin sources remain the upstream reference, while the baked racks below are
what the DAW actually processes.

## Channel strip

Signal order: Open Mic (vocals) → 8-band Pro EQ → typed compressor → Saturn
saturation → width / mono → output.

### Pro EQ

- 8 bands, canonical ids `band1…band8`:
  `On Type Freq Gain Q Slope Channel Dyn DynThresh DynRange DynAttack DynRelease`.
- Shapes: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, All Pass.
- Slopes: 12 / 24 / 36 / 48 dB/oct (cascaded Butterworth sections).
- Per-band placement: Stereo / Mid / Side.
- Per-band dynamics: signed `DynRange`; **negative cuts** and **positive boosts**
  while the band is above `DynThresh`. The static filter is built at the
  reference gain and the wet/dry blend is scaled, so the band lands on the
  requested gain at every envelope position without per-sample coefficient
  changes.
- HPF/LPF are separate 12–48 dB/oct filters with their own ids.

Legacy ids still work and map to bands 1–4:
`lowFreq/lowGain/lowQ` → band 1, `lmf…` → band 2, `hmf…` → band 3,
`high…` → band 4. Mixology's resolver output therefore keeps applying without
a translation table.

### Compressor

`compType` selects real, distinct envelope/curve maths:

0 VCA · 1 FET · 2 Opto · 3 VariMu · 4 Bus · 5 Punch · 6 Transparent · 7 Limiter

Advanced controls: knee, mix, drive, color output, color amount, feedback,
sidechain HPF, sidechain bell EQ (freq/gain/Q), lookahead, hold, range, amount,
auto release, auto gain.

The `Color` stage is type-specific and normalized so small signals stay at
unity: VCA adds a transformer hint, FET odd-order edge, Opto tube even
harmonics, VariMu tube + transformer bloom, Bus second-order glue, Punch a
harder FET edge, and Transparent/Limiter stay clean. The UI tints the Dynamics
group and character label with the matching colour: VCA red, FET orange, Opto
amber, VariMu violet, Bus cyan, Punch magenta, Transparent green, Limiter ice
blue.

### Saturation

Saturn-style stage with:

- 8 characters: Tape, Tube, Diode, Soft, Warm, Fold, Fuzz, Crunch.
- Drive, mix, bias, output.
- Bass / mid / treble / presence tone shaping on the wet signal.
- Dynamics: **+** compresses loud material, **−** gates/expands quiet material.
- Feedback amount and ringing frequency.
- HQ anti-aliasing (first-order ADAA) on/off.

Ids: `satOn satMode satDrive satMix satBias satTone satToneBass satToneMid
satToneTreble satTonePresence satDynamics satFeedback satFeedbackHz
satOversample satOutput`.

### Utility

`inputTrim` (dB), `phase` (polarity), `width` (0–200 %), `mono`, `outputDb`.

## Master rack

Order: 8-band Pro EQ → 4-band Dynamic EQ → Saturn saturation → typed multiband
dynamics → multiband Saturn exciter → multiband imager → automix trim →
maximizer.

- **8-band Pro EQ** uses the same canonical `band1…band8` surface and response
  as the channel strip.
- **Dynamic EQ** has its own 4 bands (`deq1…deq4`) with `On Type Freq Q Thresh
  Range Atk Rel Ch`; `deqOn` bypasses the block.
- **Multiband dynamics** is 1–4 phase-coherent bands (`dynBands`, `dynX1…X3`)
  with a typed compressor (`dynType`) and per-band threshold/ratio/attack/
  release/gain.
- **Multiband Saturn exciter** is 1–4 bands with per-band drive/mix/style/
  dynamics/tone/feedback/level.
- **Imager** is 1–4 bands with width, plus `imgMono` (side high-pass).
- **Maximizer** exposes threshold, ceiling, release, character, true-peak
  detection factor, transient emphasis and stereo link.

## UI

The Mixer toolbar now has **Channel strip** and **Master rack**. Both open the
live console-ui faceplate, scaled to the window, wired to
`setChannelParam` / `setMasterParam`. Every control writes the same parameter id
the DSP reads — there is no preview/state split.

## Mixology autopilot

The Mixer's **Autopilot** section renders the session, measures it with local
Earshot, and applies a bounded plan:

1. Earshot `measure --music --json` on the render (and an optional reference).
2. Compute band corrections from the reference, or from the song-ready target
   curve when no reference is supplied.
3. Apply the channel pass to the vocal, 808/bass and drum buses.
4. At musical depth and above, render and measure **again**, then build the
   master pass from that new evidence (bottom-up refinement).
5. Apply the master EQ/dynamic-EQ/saturation/maximizer pass.

Earshot is located through `EARSHOT_HOME` / `EARSHOT_PYTHON`, defaulting to
`D:/assets/earshot-main` and falling back to `C:/assets/earshot-main`. The pass
is synchronous and is intended for an explicit user action, not a timer.

The native path is the offline fallback. To use Mixology's full server
pipeline instead — Earshot + JEV strategy + Dev-Brain ranking/governance —
start Mixology and set:

```powershell
$env:NC_MIXOLOGY_SERVER = "http://127.0.0.1:3000"
```

The DAW then posts to `/api/autopilot`, applies the returned bounded plan only
when `safeToApply` is true, and reports the pipeline/governance summary. If the
server is down, it silently falls back to the native Earshot path.

Depth: Gentle (≈-16 LUFS), Musical (≈-11 LUFS), Loud (≈-9 LUFS). The same
three buttons cycle in the Mixer.

## Verification

- `NCMixRackTests.exe "[mixrack]"` — DSP, state and autopilot-adjacent tests.
- `NCMixRackTests.exe "[eq]"`, `"[dsp]"`, `"[deq]"`, `"[sat]"`.
- `ctest --test-dir build -C Release --output-on-failure` — full native suite.
  The four `[mock]` hardware tests drive a mock device and may abort before the
  first assertion on machines where no audio device can be opened; run the
  `~[mock]` suite for a headless pass. A green full run requires a machine with
  a free audio device.
- `cd ui && npm run typecheck && npm test && npm run build`.
- `cd Mixology/source/mixology && npm run lint && npm run test:autopilot`.

## Build note

The GUI target needs the WebView2 SDK. This checkout had the CMake cache
pointing at `C:\Users\Asus\Desktop\Plugins\.deps\WebView2`. The NuGet package
`Microsoft.Web.WebView2` can be restored into that folder; the build then
reconfigures normally.
