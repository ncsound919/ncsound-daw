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
  bg: "#171615",
  panel: "#262320",
  panelHi: "#35312d",
  panelLo: "#1d1c1a",
  well: "#101010",
  track: "#262320",
  edge: "#040404",
  text: "#f8e8c8",
  label: "rgba(248,232,200,0.78)",
  muted: "rgba(248,232,200,0.58)",
  value: "#e0824d",
  valueHot: "#ffd9b0",
  lcdText: "#ffae3a",
  lcdBg: "#151413",
  mod: "#e4a15c",
  modSoft: "#ffd9b0",
  danger: "#e0503c",
  meterLow: "#7fd8a0",
  meterMid: "#d8b22e",
  meterHigh: "#e0503c",
} as const;

/** Sub-system chips — warm family only (workspace accent discipline). */
export const system = {
  source: "#e0c48a",
  filter: "#e0824d",
  env: "#f8e8c8",
  colour: "#e4a15c",
  mod: "#e4a15c",
  dynamics: "#e0503c",
  out: "#f8e8c8",
} as const;

/**
 * Knob diameters on a perfect-fourth (4:3) scale. "s" is retired: it renders
 * at M because 28 px was too small to hit reliably. Use a Slider for extras.
 */
export const knobSize = { s: 48, m: 48, l: 62, xl: 78, xxl: 96 } as const;
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
  ui: "'DIN Neuzeit', 'Archivo Variable', system-ui, sans-serif",
  mono: "'DIN Neuzeit', ui-monospace, monospace",
  googleCss:
    "https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,400..800&family=Share+Tech+Mono&display=swap",
} as const;

export function alpha(hex: string, a: number): string {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
}
