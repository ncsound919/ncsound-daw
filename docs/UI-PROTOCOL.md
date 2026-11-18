# DAW UI — Overlay UI Protocol skin

The React workspace (`ui/src/workspace`) is skinned by the Overlay UI Protocol.
The protocol itself lives in `C:\Users\Asus\Downloads\ov\ui protocol`; the
implementation carries the same law, not a re-interpretation of it.

## Where the design system lives

| File | Role |
|------|------|
| `ui/src/protocol/tokens.css` | Canonical protocol tokens, copied verbatim. |
| `ui/src/protocol/protocol.css` | Enforcement layer, imported last in `Workspace.tsx`. It maps every legacy `--ws-*` token onto the protocol ladder and restyles the shell, surfaces and controls. |
| `ui/src/console-ui/tokens.ts` | Component tokens for knobs / keys. Mirrors the protocol colour and dial ladder. |
| `ui/src/console-ui/console.css` | Structural knob / slider CSS. The protocol layer overrides only paint (dark cap, ticks, pointer tip). |

Rules of the house, in one sentence:

> One amber accent means "this is a value / this is live"; state is carried by
> luminance, never hue; every number is tabular JetBrains Mono, every label is a
> 9–11 px caps Archivo; nothing is below 9 px, nothing below 24 px; controls share
> a floor inside their card; the graph is the primary editor.

## Surface mapping

```
canvas  #08080a   timeline wells, mix background
inset   #0a0a0c   graphs, meters, dial wells
shell   #0e0e11   app chrome, header, toolbar, status
panel   #141417   cards, strips, track headers
raised  #1a1a1f   menus, popovers, dialogs
control #202026   resting buttons
```

Accent: `#ff8a00`, hot `#ffc98a`, dim `rgba(255,138,0,.14)`, line `.34`.
Semantics only where amber cannot speak: cyan `#35d0d8` modulation, green
`#55d18b` success, red `#ff5a4a` clip/danger, rose `#e0567a` gain reduction.

Track identity uses the protocol's module accent map (`--m-*`). Tint strength is
still driven by the Settings → Track tint control (`--ws-tint-amt`), and clip
boldness by `--ws-clip-bold`; both feed `color-mix()` in the protocol layer.

## Shell

| Band | Height | Notes |
|------|--------|-------|
| Header | 52 px | Brand, menus, view tabs as chain pills with a power dot, DSP meter, FIT / NEW |
| Transport | 60 px | 32 px hardware buttons, LCD wells (`#120c05` / `#ffb347`), master meter with 0 / −12 / −24 / −48 marks |
| Toolbar | 40 px | Segmented edit tools, clip functions, snap, session mode, zoom |
| Ruler | 34 px | 9 px mono bar numbers, amber loop band with widened hit area, 1 px playhead |
| Status | 26 px | LED, last action, shortcut line, session counters |

Compact (`max-width: 1180px`) drops the logo subtitle, shortcut line and header
status chip; nothing shrinks below the type ladder.

## Controls

- **Knobs** draw 13 graduations across the 270° sweep, a dark machined cap with a
  one-pixel lit edge, an inner ring, a pointer line and a glowing tip dot. The
  readout splits value and unit (`1.20` + `kHz`), both mono.
- **Every clickable** has hover, `[aria-pressed]` amber fill, a 1 px press and the
  protocol focus ring (`0 0 0 1.5px rgba(255,138,0,.55)`, 10 px halo).
- **Hit floors**: 20 px mini buttons and channel switches grow an invisible hit
  area to ≥ 24 px; the loop band gets the same treatment.
- **Meters** are segmented insets with amber fill, red clip, white hold and scale
  marks at −12 / −24 / −48 (−60 on the fader). The transport master meter carries
  `role="meter"` and live `aria-valuenow` / `aria-valuetext`.
- **Disabled / inactive** is grey (`--t-dim`) at the same hue; bypassed inserts are
  62 % opacity. Nothing inactive turns amber.
- **Mixer strips** name the track, not its index (`DRUMS`, `BASS`, …), with
  `TRACK n · instrument` underneath. The three dials keep their labels and values
  in one unscaled 9 px read row (`PAN C · REV 18% · DLY 8%`), so scaling the dials
  never drags type below the floor.
- **Master strip** has no "MASTER CHAIN" heading and no footer OUT label; monitor
  switches sit under the insert chain and the visualizer fills the rest.

## Verifying a change

```powershell
cd ui
npm run typecheck
npm test
npm run build
```

Design review URLs (the workspace reads `?view=`):

```
http://localhost:5174/?view=arrange
http://localhost:5174/?view=mix
http://localhost:5174/?view=inspector
http://localhost:5174/?view=piano
http://localhost:5174/?view=automation
http://localhost:5174/?view=settings
```

Audit at **1320×780** (standard) and **1120×660** (compact). No overflow:
`scrollWidth/scrollHeight === clientWidth/clientHeight` on the canvas, every card,
strip and deck. Screens are fixed instruments — cards compress, they never scroll.
