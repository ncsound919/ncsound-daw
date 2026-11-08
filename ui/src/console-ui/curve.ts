// GENERATED from console-ui — edit there, then run `npm run sync`.
// Drawn modulation curve, the TypeScript twin of fx::Curve (overlay-fx
// libs/fx_core/src/Curve.cpp). The plugin is the source of truth: the panel
// edits a local copy, sends the CV1 text, and the host canonicalises it.
// evaluateCurve() must match fx::Curve::evaluate(); test/curve-vectors.json
// holds values produced by the C++ side and is checked by both test suites.
//
// A curve is a sorted list of points on a cyclic [0,1) phase. The segment
// LEAVING a point is shaped by its weight: 0 hard (linear), 1 medium
// (smoothstep), 2 soft (cosine). The last point joins the first across the wrap.

export type Weight = 0 | 1 | 2;
export interface CurvePoint { x: number; y: number; w: Weight }
export type Curve = readonly CurvePoint[];

export const CURVE_CAPACITY = 128;
export const WEIGHT_NAMES = ["Hard", "Medium", "Soft"] as const;

const clamp01 = (v: number) => (v < 0 ? 0 : v > 1 ? 1 : v);
const wrap01 = (v: number) => v - Math.floor(v);

export const DEFAULT_CURVE: Curve = [{ x: 0, y: 0.5, w: 0 }, { x: 1, y: 0.5, w: 0 }];

function interp(y0: number, y1: number, t: number, w: Weight): number {
  if (w === 1) t = t * t * (3 - 2 * t);
  else if (w === 2) t = 0.5 * (1 - Math.cos(Math.PI * t));
  return y0 + t * (y1 - y0);
}

/** Stable sort by x (ties keep their order, like the C++ insertion sort). */
export function sortCurve(c: Curve): CurvePoint[] {
  return c.map((p, i) => ({ p, i })).sort((a, b) => a.p.x - b.p.x || a.i - b.i).map((e) => e.p);
}

export function evaluateCurve(c: Curve, phase: number): number {
  const n = c.length;
  if (n < 2) return 0.5;
  const ph = wrap01(phase);
  if (ph < c[0].x || ph >= c[n - 1].x) {
    const xa = c[n - 1].x, xb = c[0].x + 1;
    const p = ph < xa ? ph + 1 : ph;
    const t = xb > xa ? (p - xa) / (xb - xa) : 0;
    return interp(c[n - 1].y, c[0].y, t, c[n - 1].w);
  }
  let i = 0;
  while (c[i + 1].x <= ph) i++;
  const xa = c[i].x, xb = c[i + 1].x;
  const t = xb > xa ? (ph - xa) / (xb - xa) : 0;
  return interp(c[i].y, c[i + 1].y, t, c[i].w);
}

/** CV1 text -> points. Malformed text gives the default curve (as the plugin does). */
export function parseCurve(text: string | undefined): CurvePoint[] {
  const parts = (text ?? "").split("|");
  if (parts.length < 2 || parts[0] !== "CV1") return [...DEFAULT_CURVE];
  const n = Number.parseInt(parts[1], 10);
  if (!(n >= 2 && n <= CURVE_CAPACITY) || parts.length < 2 + n) return [...DEFAULT_CURVE];
  const out: CurvePoint[] = [];
  for (let i = 0; i < n; i++) {
    const v = parts[2 + i].split(",");
    if (v.length !== 3) return [...DEFAULT_CURVE];
    const x = Number(v[0]), y = Number(v[1]), w = Number.parseInt(v[2], 10);
    if (!Number.isFinite(x) || !Number.isFinite(y) || !(w >= 0 && w <= 2)) return [...DEFAULT_CURVE];
    // Stored as float32, like the plugin, so segment boundaries land on the same side.
    out.push({ x: Math.fround(clamp01(x)), y: Math.fround(clamp01(y)), w: w as Weight });
  }
  return sortCurve(out);
}

const fmt = (v: number) => String(Number(v.toPrecision(7)));
export function serializeCurve(c: Curve): string {
  return `CV1|${c.length}${c.map((p) => `|${fmt(p.x)},${fmt(p.y)},${p.w}`).join("")}`;
}

// ---------------------------------------------------------------- edits
// All edits return a new sorted array; the input is never mutated.

/** Adds a point (an existing point at the same x is replaced). Returns the curve and the new point's index. */
export function addPoint(c: Curve, x: number, y: number, w: Weight = 0): { curve: CurvePoint[]; index: number } {
  if (c.length >= CURVE_CAPACITY) return { curve: [...c], index: -1 };
  x = clamp01(x); y = clamp01(y);
  const out = [...c];
  let i = 0;
  while (i < out.length && out[i].x < x) i++;
  if (i < out.length && out[i].x === x) out[i] = { x, y, w };
  else out.splice(i, 0, { x, y, w });
  return { curve: out, index: i };
}

export function removePoint(c: Curve, index: number): CurvePoint[] {
  if (c.length <= 2 || index < 0 || index >= c.length) return [...c];
  return c.filter((_, i) => i !== index);
}

/** Moves a point; returns the curve and where that point ended up after sorting. */
export function movePoint(c: Curve, index: number, x: number, y: number): { curve: CurvePoint[]; index: number } {
  if (index < 0 || index >= c.length) return { curve: [...c], index };
  const moved = { ...c[index], x: clamp01(x), y: clamp01(y) };
  const tagged = c.map((p, i) => ({ p: i === index ? moved : p, me: i === index }));
  const sorted = tagged.map((e, i) => ({ ...e, i })).sort((a, b) => a.p.x - b.p.x || a.i - b.i);
  return { curve: sorted.map((e) => e.p), index: sorted.findIndex((e) => e.me) };
}

export function setWeight(c: Curve, index: number, w: Weight): CurvePoint[] {
  return c.map((p, i) => (i === index ? { ...p, w } : p));
}

export function flipH(c: Curve): CurvePoint[] {
  return sortCurve(c.map((p) => ({ ...p, x: 1 - p.x })));
}

export function flipV(c: Curve): CurvePoint[] {
  return c.map((p) => ({ ...p, y: 1 - p.y }));
}

/** Tiles the curve `times` times into one cycle (capped by capacity). */
export function repeatCurve(c: Curve, times: number): CurvePoint[] {
  const t = Math.min(Math.floor(times), Math.floor(CURVE_CAPACITY / Math.max(1, c.length)));
  if (t <= 1) return [...c];
  const out: CurvePoint[] = [];
  for (let k = 0; k < t; k++) for (const p of c) out.push({ ...p, x: p.x / t + k / t });
  return out;
}

/** Cyclic shift of every point by `amount` of the cycle. */
export function nudgeCurve(c: Curve, amount: number): CurvePoint[] {
  return sortCurve(c.map((p) => ({ ...p, x: wrap01(p.x + amount) })));
}

/** New random levels for every point (positions kept). Deterministic per seed. */
export function randomizeY(c: Curve, seed: number): CurvePoint[] {
  let s = (seed >>> 0) || 1;
  const rnd = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  return c.map((p) => ({ ...p, y: Math.round(rnd() * 1000) / 1000 }));
}

// ---------------------------------------------------------------- shapes
export const CURVE_SHAPES = {
  sine: [{ x: 0, y: 0.5, w: 2 }, { x: 0.25, y: 1, w: 2 }, { x: 0.5, y: 0.5, w: 2 }, { x: 0.75, y: 0, w: 2 }],
  triangle: [{ x: 0, y: 0, w: 0 }, { x: 0.5, y: 1, w: 0 }],
  rampUp: [{ x: 0, y: 0, w: 0 }, { x: 0.99, y: 1, w: 0 }],
  rampDown: [{ x: 0, y: 1, w: 0 }, { x: 0.99, y: 0, w: 0 }],
  square: [{ x: 0, y: 1, w: 0 }, { x: 0.4999, y: 1, w: 0 }, { x: 0.5, y: 0, w: 0 }, { x: 0.9999, y: 0, w: 0 }],
  pump: [{ x: 0, y: 0, w: 2 }, { x: 0.45, y: 1, w: 0 }, { x: 0.97, y: 1, w: 0 }],
  flat: [{ x: 0, y: 1, w: 0 }, { x: 1, y: 1, w: 0 }],
} as const satisfies Record<string, Curve>;
export type CurveShape = keyof typeof CURVE_SHAPES;
export const CURVE_SHAPE_LABELS: Record<CurveShape, string> = {
  sine: "Sine", triangle: "Tri", rampUp: "Ramp ↑", rampDown: "Ramp ↓", square: "Square", pump: "Pump", flat: "Flat",
};

/** Sampled polyline of a curve (for drawing). */
export function sampleCurve(c: Curve, n = 256): [number, number][] {
  return Array.from({ length: n + 1 }, (_, i) => [i / n, evaluateCurve(c, i === n ? 0.999999 : i / n)]);
}
