# UI-REVIEW — NCSound Music Hub (DAW shell)

**Scope:** `app/Source` — header, transport, arrange view, inspector, mixer, plug-in browser.
**Baseline:** `docs/superpowers/specs/2026-11-05-ncsound-daw-design.md` (no phase `UI-SPEC.md` exists yet).
**Date:** 2026-11-06. Method: code-level audit of every painted surface plus the layout/hit-test maths.
**Note on process:** this repo has no `.planning/` phase directory and `gsd-sdk` is not installed, so the
GSD phase gates (`init.phase-op`, SUMMARY/PLAN lookup) do not apply. The audit ran standalone against the
design spec, which is the only design contract in the repository. Output lives at `docs/UI-REVIEW.md`
instead of `{phase}-UI-REVIEW.md`.

> **Restyle update, 2026-11-06:** the type stack described in Pillar 4 was replaced after this audit.
> The app now ships Anton (display), Space Grotesk (UI) and Space Mono (numbers). See the restyle pass
> before Verification for the fonts, ramp, geometry, clip dimming and compact-layout changes.

---

## Score

| Pillar | As found | After fixes |
|---|---|---|
| Copywriting | 1/4 | 3/4 |
| Visuals | 2/4 | 3/4 |
| Color | 2/4 | 4/4 |
| Typography | 1/4 | 3/4 |
| Spacing | 2/4 | 3/4 |
| Experience Design | 2/4 | 3/4 |
| **Overall** | **10/24** | **19/24** |

---

## Pillar 1 — Copywriting (1/4)

The chrome narrated states that were not true. This is the most damaging defect in a DAW, because an
operator cannot tell a working session from a demo.

Findings:

1. **Save lied.** `File > Save Session` and `Ctrl+S` printed `Session saved · 14:32`. Nothing is written;
   project I/O is Task 9 and unbuilt.
2. **The engine badge lied.** The header always read `AUDIO ENGINE ONLINE` with `48.0 kHz · 128 smp`
   hard-coded, whatever the device actually opened with.
3. **The CPU meter lied.** `DSP 18%` was `sin(t)` plus noise, re-rolled every tick, unrelated to audio.
4. **Record lied.** Pressing record latched the button and started playback, implying capture. No input is
   opened for recording anywhere.
5. **Scan lied.** `RESCAN VST3` animated a progress bar to 100% on a 30 Hz timer with no scanner linked,
   then reported "Scan complete".
6. **Dead controls had confident labels.** `4/4 · 16 BARS` (bars are 16 by luck, not by computation),
   `TRUE PEAK` for a sample peak, `POST-FADER METERS`, `24 BIT / 48 kHz` on every channel, `SNAP 1/16`
   for drags that only quantise to whole bars, `24/48` stamped on fabricated waveforms.
7. Placeholder `a  insert` (stray double space, lower-case) sat beside uppercase labels.

Fixes applied: every one of the above now either reads from the engine or states plainly that the
function is not wired in this build. `SNAP` reports the quantisation it actually performs (`BAR`/`OFF`).
`TRUE PEAK` reads `PEAK`. `LOAD ROUTES THIS PLUG-IN TO THE SELECTED CHANNEL` became
`LOADS INTO THE SELECTED CHANNEL`.

Remaining: the session data itself is still a preview (see Experience Design).

## Pillar 2 — Visuals (2/4)

Strengths: a real, coherent instrument identity — warm ink/cream/orange, monogram, LCD wells, brushed
grains, hardware tells. Clip rendering (waveform vs. piano-roll blocks) reads instantly.

Findings:

1. **Everything was raised.** `theme::face()` (gradient + inner highlight + three-layer drop shadow +
   grain) was applied to mixer strips, the master strip, and every panel. With no flat surface anywhere,
   depth stopped carrying hierarchy.
2. **Noise beat signal at small sizes.** Grain on every surface, scanlines inside LCDs, decorative
   screw heads, playhead glow on both sides. The spec bans decorative glows and makes the record motif
   "static, never wallpaper".
3. **Fake data drawn as data**: a smooth 4-band EQ curve with four node handles in the inspector,
   rendered but not wired to any filter, and a mini-meter per track fed by an animation.
4. Selection in the arrange headers was a 5.5 % tint wash — easy to miss at 1440 px.
5. **Headers painted over the clips.** `paintTrackHeaders()` derived its strip rects from `laneBounds()`
   — the timeline lane — and ran after `paintClips()`. Every track's header background therefore covered
   its own clips, while the actual header column was left unpainted except for the M/S/R hit boxes.
   The arrangement could not show clips at all. This is the highest-severity finding in the audit.

Fixes applied: headers now paint into `trackHeaderColumn`, so clips are visible and the header column has
its strips; the fabricated EQ panel is gone (its 84 px went to the fader, see Spacing); meters are real;
selection keeps its tint plus a `SEL` marker and accent border. The elevation/grain treatment is
unchanged — reducing it is a design decision, not a defect fix, and is left for your call.

## Pillar 3 — Color (2/4)

The palette tokens are correct and used consistently; the failure was discipline plus contrast.

Findings:

1. **A second colour language.** `theme::success` (green `#7CAE76`, absent from the spec's palette) drove
   the DSP bars, the transport status LED, and the inspector's CHANNEL section chip, and every meter
   ramp was green→amber→red. The spec allows one accent (burnt orange) and reserves `mod` for
   modulation routing.
2. **Contrast failures.** `muted` at 43 % cream on `#181818` ≈ 3.4:1 and `faint` at 24 % ≈ 1.6:1, and
   both were used for real text (hints, `AUTO`, scale ticks, clip metadata) at 5 px. Below every
   accessibility floor.
3. Nine track tints are defensible (documented as the only non-accent hues), but they were also used for
   button fills, competing with state colours.

Fixes applied: `success` removed; DSP bars, status LED and section chips now use the accent ramp; meters
use cream → orange → red (`meterLow/Mid/High`), which reads as a VU scale and stays in-palette. `paperDim`
raised to 78 %, `muted` to 58 % (≈6:1 on ink), `faint` to 44 % and no longer used for prose.

## Pillar 4 — Typography (1/4)

Findings:

1. **Five families on one surface**: Bremen Black (display), Bank Gothic (labels), Digital Readout
   (numbers), Inter (UI), DM Sans (figures). The spec allows two per surface; the kit shipped five, and
   Bank Gothic carried all micro copy.
2. **Thirty distinct sizes**, clustered between 4.8 and 7.0 px: `4.8` tool numbers, `5.0` clip bar labels,
   `5.1` device name, `5.2` marker labels, `5.3` shortcut hints, `5.4` scale ticks… At 100 % DPI this is
   not readable text, and at 150 % it turns to mush. There was no floor and no ramp.
3. Caps everywhere with zero tracking — at 5 px, tracking is the difference between legible and noise.
4. Hierarchy was inverted: the lockup tagline and the clip metadata sat at the same weight/size band, so
   scanning had no entry point.

Fixes applied: three roles — Inter (UI, floor 12 px), Bremen Black (headings/lockup, floor 11.5 px),
Digital Readout (numeric readouts, floor 10.5 px). Bank Gothic and DM Sans are dropped from the binary
data. A seven-step ramp lives in `theme::size` (9.5 / 10.5 / 12 / 13.5 / 16 / 20 / 26) and the font
factories clamp to their role's floor, so no call site can re-introduce 5 px type. Caps labels get
`withExtraKerningFactor(0.07)`. Key rows were grown to fit: track names 13 px, clip names 11.5 px, plugin
rows 12 px, LCD digits 24–26 px.

Remaining: at the 720 px minimum height some secondary rows (INS detail, marker labels) are still tight
and will ellipsise rather than overflow — acceptable, but the next pass should give the arrange header a
compact mode instead of shrinking type.

## Pillar 5 — Spacing (2/4)

Findings:

1. **No 4 pt grid.** Header 42, transport 84, status 24, slots 19, ruler 46, gaps 2/3/5/6/7/9/11/13,
   module widths 254/306/190/116/76 — and radii 2, 2.5, 3, 3.5, 4, 5, 6, 7, 10, 12 against a spec of
   8/12/16.
2. **Hit targets below any usable size**: track M/S/R were 19×16 (WCAG 2.2 wants 24×24; touch wants 44),
   insert slots 19 px tall, tool buttons 28×28, headers 26 px tall.
3. **A real layout bug**: in `InspectorPanel::resized()` the channel area got whatever was left after six
   fixed sections, then had 78 px trimmed from the top and 36 px from the bottom. At the 720 px minimum
   window that area is ~80 px, so the fader, meter and W·PAN readouts collapsed to negative height and
   vanished. Below 900 px the inspector effectively had no fader.

Fixes applied: 4 pt-ish rhythm (40 header, 88 transport, 28 status, 48/44 toolbar+ruler, 8/12/16 gutters).
M/S/R are 26×26, insert rows 22–24 px, tool buttons 32×28, toolbar buttons 26–32 px, transport 36–48 px.
The inspector now allocates fixed sections first and gives the channel the remainder, so the fader keeps a
usable height at every supported window size — and the dead TONE panel that consumed 84 px is gone.

Remaining: radii are still mixed (2–12) and the 4 pt grid is applied to the major bands, not every inset.

## Pillar 6 — Experience Design (2/4)

Findings:

1. **No keyboard path.** `Knob` set `setWantsKeyboardFocus(true)` but had no `keyPressed` and painted no
   focus ring, so Tab landed on a control that could not be operated and gave no indication it was
   focused. `Fader`, `Slot` and `SegmentedControl` were not focusable at all. The spec requires every
   control reachable by Tab with a 2 px `accent` focus ring.
2. **Destructive edits were irreversible.** The erase tool deleted a clip on click; the split tool cut a
   clip. `Undo` was permanently disabled. In a DAW that is a data-loss path, not a UX nit.
3. **No tooltips** anywhere, though tooltip colours were configured. Icon-only transport buttons relied on
   guesswork.
4. **Idle cost.** The 30 Hz timer repainted the entire track-header column and ran `mixer.syncFromEngine()`
   whether or not anything changed — permanent CPU burn at rest, the exact anti-goal the spec names
   ("a UI update that rebuilds the whole app state").
5. Fabricated meters made every strip look alive while silent, so "is audio actually passing?" had no
   reliable answer.

Fixes applied: focus rings and arrow/Page/Home/End/Return handling on knobs and faders; focus rings and
Enter/Space activation on slots; arrow-key segmented control; focus ring on hardware buttons; tooltips on
transport and header controls. One-step undo (`Ctrl+Z` and `Edit > Undo`) covers erase, split and add,
with the menu item enabled only when there is something to undo. Meters read real engine peaks. The main
timer refreshes meters/header at 30 Hz only while the transport runs and drops to 2 Hz at rest.

Remaining: no full keyboard navigation of the timeline (clip move/trim by keyboard), redo is not
implemented, and the session is still a preview: no clip sources are wired, so peaks stay at zero until
the engine gets audio — which is now visible instead of simulated.

---

## Restyle pass — 2026-11-06 (fonts, scale, geometry)

This pass replaced the audit-era type system and tightened the surface geometry.

**Faces.** Anton for display (lockup, track names, panel titles), Space Grotesk regular/bold for UI copy,
Space Mono bold for every numeric readout. Inter, Bremen Black, Bank Gothic and Digital Readout are gone
from the binary data. The old five-family kit was the root of the Pillar 4 failure.

**Ramp.** `theme::size` is now micro 11 / label 12 / body 13.5 / strong 15.5 / title 18 / hero 22 /
readout 30. The font factories clamp to the ramp, so a call site cannot reintroduce the 5 px text this
audit found. Caps labels keep 6 % tracking; display uses 1 %.

**Geometry.** `theme::radius` is three tokens only: tight 2, control 4, panel 6. Borders are 1 px; the
blurred drop shadow was removed from `theme::face()`, leaving a hard edge and one top highlight. Knobs are
46–56 px (was 34–40); fader caps are 32×18; transport buttons 38/52/44; arrange tools 34 px and track
M/S/R 28×28. The transport LCD is 68 px high with 28 px digits.

**Density.** Arrange toolbar 54 / ruler 50; clip header 22 px; mixer header 36 and master strip 64/80/30;
inspector send rows 44 px. Clip fills were dimmed to 0.22/0.14 alpha, headers to 0.42/0.28 and waveforms
to 0.40, so clip content reads against the lane instead of glowing.

**Compact layouts.** The inspector switches below 600 px inner height. Its compact channel row shares the
band between PAN/WIDTH and a 72 px fader; the old stacked row left the fader a negative height and it
disappeared. Channel strips now do the same: PAN and a 40 px fader share the row, and the console gained
20 px of strip height. The old stacked strip layout had also collapsed its fader, leaving its scale labels
printed against the readout. Arrange track headers drop the instrument row below 84 px lane height and the
INS detail below 96 px. The window is clamped to the 1366×768 panel.

**Copy encoding.** Separators are ASCII ` / `. JUCE's `const char*` constructor is ASCII-only
(`CharPointer_ASCII`), so UTF-8 middle dots compiled into the binary as `Â·`; the slash removes the
mojibake without changing the layout.

**Stacking.** `MainComponent::setView` now raises the visible surface after switching, so the arrange
toolbar cannot remain over the mixer. The tracks column header is one bold `8 CHANNELS` line; the previous
two-line block clipped against the first lane edge.

**Verified.** Native-window captures at 100 % (1366×768) of the arrange view, mixer, track header, clip
header and inspector, including 3–10× nearest-neighbour crops. Build is 0 errors; the pre-existing C4244
narrowing warnings remain. Screenshots include the OS cursor, which appears as a small cream shape.

**Not verified: 150 %.** On this 1366×768 panel the available logical area at 150 % is 890×427, below the
app's 1280×720 design minimum. The transport LCDs, header bands and inspector overlap at that size. A
dedicated dense layout (hide secondary transport/header modules, drop or tab the inspector) is required
before 150 % can be claimed; the compact inspector alone is not enough.

---

## Verification

- `cmake --build build --target NCSoundHub` (MSVC 19.44 via `vcvars64`, Debug, Ninja): **succeeds**, 0 errors.
  Link produces `NCSoundHub_artefacts/Debug/NCSound Music Hub.exe` (13 MB).
- Launch smoke test: the executable starts, stays running for 6 s with no console output or crash, and is
  then terminated (no stray process left behind). Re-run after the header-column paint fix.
- Visual appearance (restyle): verified with native-window captures at 100 % rather than code inspection.
  The arrange track header, clip header and inspector were also checked in 3–10× crops for clipping and
  collisions. This closes the audit's original pixel-level gap for those surfaces.
- Not verified: 150 % scaling. The 890×427 logical area on this panel is below the design minimum and the
  fixed header/transport bands overlap; see the restyle limitation above. Do not treat 150 % as supported.
- Pre-existing `C4244` narrowing warnings remain in `MainComponent`, `ChannelStrip`, `InspectorPanel` and
  `ArrangeView`; they are conversions in drawing calls and were not introduced by this pass.

## Files changed

`Theme.h` (ramp, contrast, palette, focus ring, synthetic-meter helper removed), `Widgets.h/.cpp`
(focus rings, keyboard, meter ramp, type), `AudioEngine.h/.cpp` (real sample rate, buffer size, device
state, measured DSP load, peak reset), `TransportBar`, `ChannelStrip`, `MixerPanel`, `InspectorPanel`,
`ArrangeView` (undo, 26 px targets, type), `MainComponent` (honest header/menus, tooltips, idle timer),
`BrowserOverlay` (honest scan, 54 px rows, type), `app/CMakeLists.txt` (two fonts dropped).

---

## Edit and track identity pass (2026-11-06)

**Insert names.** `Slot` was painting the plug-in name into the 34 px strip reserved for `FX` and painting
`FX` across the remaining width, so long names collapsed to `FXOVE...`. The name now owns the card, `FX` has
the trailing strip, and the card edge/LED follows the track tint.

**Track identity.** The eight track tints are saturated signal colours (orange, coral, amber, acid green,
gold, terracotta, teal, magenta). Selected lane fills, track-header frames, clip frames, inspector frames
and mixer-strip frames all use that tint rather than the generic accent. Clip fill/header/waveform alphas
were raised so the identity reads at a glance.

**Selected channel.** The arrange inspector is now 212-232 px instead of 250-330 px, returning roughly
80-100 px to the timeline. R/M/S moved into the inspector header, PAN and WIDTH are centred over the full
channel panel, and the gain fader gets its own centred row with a `GAIN` readout and meter.

**Clips.** Every clip carries gain and fade-in/fade-out lengths. Selected clips draw a gain line, a diamond
handle, a `GAIN x.x dB` chip, fade curves and corner fade handles. Drag the fade corners with Select/Fade,
drag vertically with Gain, and trim either edge with Trim. Splits preserve gain and carry the appropriate
fade to each side.

**Tools + functions.** The arrange toolbar is SELECT / TRIM / FADE / GAIN / SPLIT / ERASE, with SPLIT, DUP
and DEL functions for the selected clip. Shortcuts: V, T, F, G, X, E for tools; Ctrl+E split at playhead;
Ctrl+D duplicate; Delete/Backspace delete. The Edit menu mirrors split, duplicate and delete.

**Verified.** Build is 0 errors. Native 100 % captures show the fixed insert names, track-tinted frames,
smaller inspector and centred knobs. Functional checks: gain drag changed the selected clip to `GAIN 8.8 dB`
and moved the line; fade drag lengthened the fade-in; DUP created a second `TAMBOURINE` at bar 13 and
reported `Duplicated TAMBOURINE / bar 13`; Split at Playhead reported `Split MAIN GROOVE at bar 7`.
Screenshots include the OS cursor. The 150 % limitation above is unchanged; this pass did not attempt a
dense high-DPI layout.
