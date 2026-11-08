# React Parity Plan — closing the gap with the native UI

Status date: 2026-11-07
Scope: port the full JUCE interface (`app/Source/*`) into the React surface (`ui/`) without losing
features. Everything below is measured against the native classes, not against a redesign.

Legend: **DONE** = implemented and wired · **PARTIAL** = present but reduced · **TODO** = not started

---

## 0. Ground truth: what the native app actually contains

| Native surface | File(s) | Feature set |
|---|---|---|
| Header / menus | `MainComponent.cpp` | FILE/EDIT/VIEW/TRACK/HELP menus, NEW + SETTINGS buttons, CPU readout, status bar with message + alert state |
| Transport | `TransportBar.h/.cpp` | rewind, stop, play, record (arm + punch), loop toggle, metronome, browser toggle, ARRANGE/MIX switch, position LCD (bars\|beats\|ticks + timecode), tempo LCD (drag, shift-fine, double-click reset), 4/4 signature LCD, master L/R meters |
| Arrange | `ArrangeView.h/.cpp` | toolbar: 6 tools (SELECT, TRIM, FADE, GAIN, SPLIT, ERASE) + SPLIT/DUP/DEL functions + snap + grid + zoom ± + bars readout + tool name; ruler with loop region (drag start/end); lanes with clips (name, kind, waveform, fade-in/out handles, gain line, crossfade, comp takes); track headers with M/S/R, mini meter, insert slots; inspector rail (track, instrument, inserts, strip) |
| Mixer | `MixerPanel.h/.cpp`, `ChannelStrip.h/.cpp` | ChannelStrip: 5 slots (0 = native strip, 1–4 user inserts) 22 px rows, sends A + B rotary, pan rotary (bipolar), fader, 18-segment meter, R/M/S; MasterStrip: native + master VST inserts, fader, L/R meters, DIM, MONO, A/B (capture/recall snapshots), AUTO (automix) |
| Plug-in browser | `BrowserOverlay.h/.cpp` | catalog rows, scan progress, instrument/effect filtering, load into selected slot |
| Piano roll | `PianoRoll.h/.cpp` | note draw/erase/resize, velocity, clip-relative beats, scale/grid guides |
| Automation | `AutomationLaneView.h/.cpp` | per-parameter lanes, point editing |
| Clips / undo | `ArrangeView` + `nc::engine::UndoStack` | move, trim, split, duplicate, delete, fade, gain, comp takes, crossfade; undo depth 100 |

---

## 1. Current React state

**DONE**

- Native bridge scaffolding (`WebViewBridge.cpp`) with 23 routes: state, pitch, transport, mix params.
- Transport strip: play/stop/rewind, loop, metronome, bars\|beats LCD, tempo drag, master meters,
  track add/remove, view tabs.
- Arrangement: ruler + ticks, click-to-seek, clips with **real peak envelopes** from
  `AudioEngine::assetPeakEnvelope`, fade curves, header + length badge, selection, playhead,
  per-lane M/S, snap toggle, zoom −/+, bars readout, FIT, ctrl+wheel zoom / wheel pan.
- Mixer: strips matching `ChannelStrip` control set — 4 user insert slots with filled/empty/missing
  states, sends A/B (0–1, % readout), pan, fader, meter, R/M/S; master strip with inserts,
  DIM/MONO (wired), fader, L/R meters.
- Pitch panel: global / track / instrument knobs.
- Styling keyed to `Theme.h` tokens (ink, panel, accent, track tints, Archivo + Share Tech Mono).

**PARTIAL**

- Inserts are display-only (no bypass, no editor launch).
- Master A/B and AUTO omitted deliberately — native drives `masterMix()` menus not exposed by the bridge.
- Arrange tools absent; only functional controls (snap/zoom/FIT) shipped.

**TODO** — everything in §2–§7.

---

## 2. Blocker: the edit-command bridge

Without this, nothing below can be functional. React currently *displays* the timeline; it cannot
change it. Clips live in `ArrangeView::clips` with `UndoStack<Snapshot>` on the native side.

Routes to add (message thread, returning full `AppState`):

| Route | Signature | Notes |
|---|---|---|
| `moveClip` | `(index, track, startBar)` | respects snap |
| `trimClip` | `(index, edge, bars)` | edge = left/right; clamp to source bounds |
| `splitClip` | `(index, bar)` | |
| `duplicateClip` | `(index)` | |
| `deleteClip` | `(index)` | |
| `setClipFade` | `(index, inBars, outBars)` | |
| `setClipGain` | `(index, db)` | |
| `setClipCrossfade` | `(index, bars)` | |
| `cycleCompTake` | `(index, delta)` | |
| `undo` / `redo` | `()` | expose `undoDepth`/`redoDepth` in state |
| `setLoopRegion` | `(startBar, endBar)` | |
| `setGrid` | `(bars)` | |
| `setTool` | `(index)` | if tool state stays native |

**Decision required:** either (a) expose `ArrangeView` commands and keep undo there, or (b) move
clip ownership into `AudioEngine`/`nc::engine` so both UIs are views of one model. (b) is the
correct end state; (a) is faster but leaves two sources of truth.

`AppState.clips` must then carry `index`, `kind`, `notes` (MIDI), `compTake`, `crossfadeBars`, and
`undoDepth`/`redoDepth`.

---

## 3. Arrange chrome (TODO)

- [ ] Tool strip: SELECT / TRIM / FADE / GAIN / SPLIT / ERASE with real behaviour (needs §2).
- [ ] SPLIT / DUP / DEL function buttons.
- [ ] Grid control (bar / beat / off) — native has `gridArea`.
- [ ] Loop region on the ruler: drag start/end handles, reflects `engine.setLoopBars`.
- [ ] Track headers: add **R** (currently M/S only), mini level meter, insert count badge.
- [ ] Inspector rail (native `InspectorPanel`, 212–232 px): track name, instrument, 4 inserts with
      click-to-open, channel strip summary, sends/pan/gain readback.
- [ ] Clip affordances: fade handles, gain line, crossfade shading, comp-take indicator.
- [ ] Vertical track scrolling + `firstVisibleTrack` behaviour (native scrolls instead of shrinking
      below `minLaneHeight = 44`).

## 4. Mixer completion (PARTIAL → TODO)

- [ ] Insert slot interaction: click to open editor window, alt-click bypass; project `bypassed`
      from `PluginSlotState`.
- [ ] Master A/B capture/recall (`masterMix().hasAB`, capture, recall) and AUTO toggle.
- [ ] Drag-reorder inserts (`AudioEngine::moveTrackInsert`).
- [ ] Fader taper parity: native `faderValueForGain` mapping, not linear dB.
- [ ] Horizontal strip scrolling with `minStripWidth = 132`.

## 5. Plug-in browser (TODO)

- [ ] Project catalog: `PluginCatalog` entries, `isInstrument`, `status`, scanned/external.
- [ ] Scan trigger + progress (`scanAsync`, `scanProgress`).
- [ ] Filter by instrument/effect, search.
- [ ] Load: `loadTrackInstrumentFromId`, `loadTrackInsertFromId`, `loadMasterInsert`; target slot
      selection (browser opens with track + slot from the insert click).

## 6. Piano roll + automation (TODO)

- [ ] MIDI note projection per clip (`notes` are already `MidiNote` clip-relative beats).
- [ ] Draw / resize / delete notes, velocity lane, scale + grid guides.
- [ ] Note write-back route with undo.
- [ ] Automation lanes: `AutomationLane` read/write, point editing, lane picker per parameter.

## 7. Shell replacement (TODO, structural)

The WebView is bounded to `bodyArea` (`MainComponent::resized`), so the native header and transport
stay visible above React. Full replacement needs:

- [ ] `webView->setBounds (getLocalBounds())`.
- [ ] Move menu bar (FILE/EDIT/VIEW/TRACK/HELP), NEW/SETTINGS, CPU readout into React.
- [ ] Move status bar (message, alert styling, autosave state) into React.
- [ ] Route all `MainComponent` callbacks (addTrack, removeTrack, import, record, save) to React.
- [ ] Decide whether React owns transport or mirrors it (currently React duplicates it inside body).

---

## 8. Verification

**Verified (fresh output):** `gen:routes` 23 routes · `tsc -b --noEmit` 0 · `vitest run` 16/16 ·
`vite build` 0.

**Not verified:** native compile of the current `WebViewBridge.cpp` (new routes + send/arm/monitor
projection). The app binary at 14:57 predates the last edits, and the log was deleted.

Commands:

```
cd "C:\Users\Asus\Desktop\Plugins\VST DAW"
build\_b.cmd                      # vcvars64 + cmake --build build --target NCSoundHub
build\_build.cmd                  # configure + build + ctest
```

UI checks (node shim on PATH is broken — use the real binary):

```
cd ui
/c/tools/node/node.exe scripts/gen-native-routes.mjs
/c/tools/node/node.exe node_modules/typescript/bin/tsc -b --noEmit
/c/tools/node/node.exe node_modules/vitest/vitest.mjs run
/c/tools/node/node.exe node_modules/vite/bin/vite.js build
```

Preview: `/c/tools/node/node.exe node_modules/vite/bin/vite.js --host 127.0.0.1 --port 5175`
(`--host` matters; the default binds IPv6-only and preview probes fail).

**Do not delete build logs** — losing them is why the native build status is still unproven.

---

## 9. Order of work

1. Prove the native build compiles (blocking).
2. Decide §2 model ownership, then add the edit-command bridge + undo projection.
3. Arrange tools, split/dup/del, loop region, grid.
4. Track header R + mini meter; inspector rail.
5. Mixer interactions (insert bypass/editor, reorder, fader taper, A/B, AUTO).
6. Plug-in browser.
7. Piano roll, then automation lanes.
8. Shell replacement (§7) — last, because it is the only step that removes the JUCE fallback.
