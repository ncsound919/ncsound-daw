// GENERATED from console-ui — edit there, then run `npm run sync`.
// Parametric EQ response, the TypeScript twin of fx::DynamicEqBand's static
// filter (overlay-fx libs/fx_core: RBJ biquads, Butterworth-Q cascades for cuts
// and shelves, gain spread over the sections). The Master panel draws its EQ
// curve with this; test/eq-vectors.json holds values from the C++ side.

export const EQ_TYPES = [
  "Bell",
  "Low Shelf",
  "High Shelf",
  "Low Cut",
  "High Cut",
  "Notch",
] as const;
export type EqTypeIndex = 0 | 1 | 2 | 3 | 4 | 5;

export interface EqBandDef {
  on: boolean;
  type: number; // EQ_TYPES index
  freq: number; // Hz
  gain: number; // dB (bell/shelves)
  q: number; // bell/notch
  slope: number; // 0..3 -> 12..48 dB/oct (cuts, shelves)
}

interface Coefs {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

const hasGain = (t: number) => t === 0 || t === 1 || t === 2;
const usesButterworth = (t: number) => t >= 1 && t <= 4;
const butterworthQ = (stages: number, k: number) =>
  1 / (2 * Math.sin(((2 * k + 1) * Math.PI) / (4 * Math.max(1, stages))));

/** Same maths (and float rounding of the coefficients) as fx::BiquadCoeffs::from. */
function coefs(type: number, sr: number, freq: number, q: number, gainDb: number): Coefs {
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
  switch (type) {
    case 0:
      b0 = 1 + alpha * A;
      b1 = -2 * cw;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cw;
      a2 = 1 - alpha / A;
      break;
    case 1:
      b0 = A * (A + 1 - (A - 1) * cw + 2 * sA * alpha);
      b1 = 2 * A * (A - 1 - (A + 1) * cw);
      b2 = A * (A + 1 - (A - 1) * cw - 2 * sA * alpha);
      a0 = A + 1 + (A - 1) * cw + 2 * sA * alpha;
      a1 = -2 * (A - 1 + (A + 1) * cw);
      a2 = A + 1 + (A - 1) * cw - 2 * sA * alpha;
      break;
    case 2:
      b0 = A * (A + 1 + (A - 1) * cw + 2 * sA * alpha);
      b1 = -2 * A * (A - 1 + (A + 1) * cw);
      b2 = A * (A + 1 + (A - 1) * cw - 2 * sA * alpha);
      a0 = A + 1 - (A - 1) * cw + 2 * sA * alpha;
      a1 = 2 * (A - 1 - (A + 1) * cw);
      a2 = A + 1 - (A - 1) * cw - 2 * sA * alpha;
      break;
    case 3:
      b0 = (1 + cw) * 0.5;
      b1 = -(1 + cw);
      b2 = b0;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break; // low cut = high-pass
    case 4:
      b0 = (1 - cw) * 0.5;
      b1 = 1 - cw;
      b2 = b0;
      a0 = 1 + alpha;
      a1 = -2 * cw;
      a2 = 1 - alpha;
      break; // high cut = low-pass
    case 5:
      b0 = 1;
      b1 = -2 * cw;
      b2 = 1;
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

/** True when the band changes the sound (off bands and 0 dB bells/shelves do not). */
export function bandAudible(b: EqBandDef): boolean {
  return b.on && (!hasGain(b.type) || Math.abs(b.gain) >= 0.01);
}

/** Linear magnitude of one band at `hz`. */
export function bandMagnitude(b: EqBandDef, hz: number, sr = 48000): number {
  if (!bandAudible(b)) return 1;
  const stages =
    b.type === 0 || b.type === 5 ? 1 : 1 + Math.min(3, Math.max(0, Math.round(b.slope)));
  const perStage = hasGain(b.type) ? b.gain / stages : 0;
  let m = 1;
  for (let k = 0; k < stages; k++)
    m *= magnitude(
      coefs(b.type, sr, b.freq, usesButterworth(b.type) ? butterworthQ(stages, k) : b.q, perStage),
      sr,
      hz,
    );
  return m;
}

/** Response of several bands in dB at `hz`. */
export function eqResponseDb(bands: readonly EqBandDef[], hz: number, sr = 48000): number {
  let m = 1;
  for (const b of bands) m *= bandMagnitude(b, hz, sr);
  return 20 * Math.log10(Math.max(1e-9, m));
}
