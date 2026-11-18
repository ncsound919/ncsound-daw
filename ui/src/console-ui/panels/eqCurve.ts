// GENERATED from console-ui — edit there, then run `npm run sync`.
// Static EQ response for the Channel Strip's flexible bands. This mirrors
// fx::DynamicEqBand / fx::BiquadCoeffs (RBJ biquads; Butterworth-Q cascades for
// cuts and shelves; gain spread across the sections) so the drawn curve is the
// filter the audio thread runs, not a pictorial approximation.

export type StripShape = "bell" | "ls" | "hs" | "notch" | "bp" | "ap" | "lc" | "hc";

interface Coefs {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

const hasGain = (s: StripShape) => s === "bell" || s === "ls" || s === "hs";
const usesButterworth = (s: StripShape) => s === "lc" || s === "hc" || s === "ls" || s === "hs";
export const butterworthQ = (stages: number, k: number) =>
  1 / (2 * Math.sin(((2 * k + 1) * Math.PI) / (4 * Math.max(1, stages))));

function coefs(shape: StripShape, sr: number, freq: number, q: number, gainDb: number): Coefs {
  const f = Math.min(Math.max(freq, 10), sr * 0.5 * 0.98);
  const w0 = (2 * Math.PI * f) / sr,
    cw = Math.cos(w0),
    sw = Math.sin(w0);
  const Q = Math.max(0.05, q),
    alpha = sw / (2 * Q),
    A = Math.pow(10, gainDb / 40),
    sA = Math.sqrt(A);
  let b0 = 1,
    b1 = 0,
    b2 = 0,
    a0 = 1,
    a1 = 0,
    a2 = 0;
  switch (shape) {
    case "bell":
      b0 = 1 + alpha * A;
      b1 = -2 * cw;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cw;
      a2 = 1 - alpha / A;
      break;
    case "ls":
      b0 = A * (A + 1 - (A - 1) * cw + 2 * sA * alpha);
      b1 = 2 * A * (A - 1 - (A + 1) * cw);
      b2 = A * (A + 1 - (A - 1) * cw - 2 * sA * alpha);
      a0 = A + 1 + (A - 1) * cw + 2 * sA * alpha;
      a1 = -2 * (A - 1 + (A + 1) * cw);
      a2 = A + 1 + (A - 1) * cw - 2 * sA * alpha;
      break;
    case "hs":
      b0 = A * (A + 1 + (A - 1) * cw + 2 * sA * alpha);
      b1 = -2 * A * (A - 1 + (A + 1) * cw);
      b2 = A * (A + 1 + (A - 1) * cw - 2 * sA * alpha);
      a0 = A + 1 - (A - 1) * cw + 2 * sA * alpha;
      a1 = 2 * (A - 1 - (A + 1) * cw);
      a2 = A + 1 - (A - 1) * cw - 2 * sA * alpha;
      break;
    case "lc":
      b0 = (1 + cw) * 0.5;
      b1 = -(1 + cw);
      b2 = b0;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break;
    case "hc":
      b0 = (1 - cw) * 0.5;
      b1 = 1 - cw;
      b2 = b0;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break;
    case "bp":
      b0 = alpha;
      b1 = 0;
      b2 = -alpha;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break;
    case "notch":
      b0 = 1;
      b1 = -2 * cw;
      b2 = 1;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break;
    case "ap":
      b0 = 1 - alpha;
      b1 = -2 * cw;
      b2 = 1 + alpha;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break;
  }
  const fr = Math.fround;
  return { b0: fr(b0 / a0), b1: fr(b1 / a0), b2: fr(b2 / a0), a1: fr(a1 / a0), a2: fr(a2 / a0) };
}

function magnitude(c: Coefs, sr: number, freq: number): number {
  const w = (2 * Math.PI * Math.min(Math.max(freq, 0), sr * 0.5)) / sr;
  const nr = c.b0 + c.b1 * Math.cos(w) + c.b2 * Math.cos(2 * w),
    ni = -(c.b1 * Math.sin(w) + c.b2 * Math.sin(2 * w));
  const dr = 1 + c.a1 * Math.cos(w) + c.a2 * Math.cos(2 * w),
    di = -(c.a1 * Math.sin(w) + c.a2 * Math.sin(2 * w));
  const den = Math.hypot(dr, di);
  return den > 1e-12 ? Math.hypot(nr, ni) / den : 0;
}

/** One band's static magnitude in dB at `hz`. `slope` is 0..3 (12..48 dB/oct). */
export function stripBandDb(
  b: { shape: StripShape; freq: number; gain: number; q: number; slope: number },
  hz: number,
  sr = 48000,
): number {
  if (b.shape === "ap") return 0;
  const stages =
    b.shape === "bell" || b.shape === "notch"
      ? 1
      : 1 + Math.min(3, Math.max(0, Math.round(b.slope)));
  const perStage = hasGain(b.shape) ? b.gain / stages : 0;
  let m = 1;
  for (let k = 0; k < stages; k++) {
    const q = usesButterworth(b.shape) ? butterworthQ(stages, k) : Math.max(0.05, b.q);
    m *= magnitude(coefs(b.shape, sr, b.freq, q, perStage), sr, hz);
  }
  return 20 * Math.log10(Math.max(1e-9, m));
}

/** Combined response of every enabled band and cut filter, in dB. */
export function stripResponseDb(
  bands: readonly { shape: StripShape; freq: number; gain: number; q: number; slope: number }[],
  hz: number,
  sr = 48000,
): number {
  let db = 0;
  for (const b of bands) db += stripBandDb(b, hz, sr);
  return db;
}
