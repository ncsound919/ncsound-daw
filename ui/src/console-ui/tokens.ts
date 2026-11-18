// GENERATED from console-ui — edit there, then run `npm run sync`.
// Console B design tokens — the single source of truth for colour, size and
// type across ChordStudio, the Rompler and Overlay FX. console.css mirrors the
// colour values as --cs-* custom properties (a test keeps the two in sync), and
// canvas renderers read them from here.
//
// Accent discipline:
//   value  (amber)  — arcs, active LEDs, selection, readouts
//   mod    (cyan)   — modulation rings, sources, sidechain keys
//   everything else — neutral ramp. Sub-system chips are the only other hues.

export const color = {
  bg: "#0e0e11",
  panel: "#141417",
  panelHi: "#202026",
  panelLo: "#1a1a1f",
  well: "#0a0a0c",
  track: "#26262c",
  edge: "#26262c",
  text: "#f1eee7",
  label: "#b5b0a6",
  muted: "#8b867d",
  value: "#ff8a00",
  valueHot: "#ffc98a",
  lcdText: "#ffb347",
  lcdBg: "#120c05",
  mod: "#35d0d8",
  modSoft: "#a9f0f4",
  danger: "#ff5a4a",
  meterLow: "#55d18b",
  meterMid: "#ff8a00",
  meterHigh: "#ff5a4a",
} as const;

/** Sub-system chips — the protocol's module accent map, nothing invented. */
export const system = {
  source: "#e0a356",
  filter: "#ff8a00",
  env: "#f1eee7",
  colour: "#b98cff",
  mod: "#35d0d8",
  dynamics: "#e0567a",
  out: "#f1eee7",
} as const;

/**
 * Dial ladder from the UI protocol: 34 / 42 / 54 / 68 / 82. "s" is retired and
 * renders at sm; use a Slider below that.
 */
export const knobSize = { s: 42, m: 54, l: 68, xl: 82, xxl: 96 } as const;
export type KnobSize = keyof typeof knobSize;

/** Invisible hit margin around every knob, each side. */
export const HIT_PAD = 8;

/** Button geometry (px). Keys: 36 standard, 44 primary; segment buttons 34
 *  inside the 2 px well; nothing narrower than 44; 8 px between keys, 16 px
 *  between clusters. The audit script (scripts/audit-ui.mjs) enforces these. */
export const keySize = { h: 36, hLg: 44, seg: 34, minW: 44, gap: 8, cluster: 16 } as const;

/** Pixels of vertical drag for a full 0..1 sweep. */
export const DRAG_TRAVEL = 200;
export const FINE_FACTOR = 0.1;
export const WHEEL_STEP = 0.02;
export const WHEEL_STEP_FINE = 0.002;

/** Visualiser frame budget: redraw at most every 32 ms (~30 fps). */
export const FRAME_MS = 32;

export const font = {
  ui: "'Archivo Variable', Archivo, 'Segoe UI', sans-serif",
  mono: "'JetBrains Mono', ui-monospace, 'Cascadia Mono', monospace",
  googleCss:
    "https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,400..800&family=JetBrains+Mono:wght@400;500&display=swap",
} as const;

export function alpha(hex: string, a: number): string {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
}
