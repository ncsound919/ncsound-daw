// Residual analyser: subtract the best-fit sine at f0 from a capture and show
// what is left, over time and in frequency. Distinguishes a settling reverb
// tail (decays) from a block-rate artifact (steady, block-coherent).
//   node scripts/residual.mjs <file.f32> <f0> [blockLen]
import { readFileSync } from "node:fs";

const [, , path, f0Arg, blockArg] = process.argv;
const f0 = Number(f0Arg ?? 1000);
const block = Number(blockArg ?? 480);
const rate = 48000;

const buf = readFileSync(path);
const frames = Math.floor(buf.byteLength / 8);
const d = new Float32Array(buf.buffer, buf.byteOffset, frames * 2);
const x = new Float64Array(frames);
for (let i = 0; i < frames; i += 1) x[i] = 0.5 * (d[i * 2] + d[i * 2 + 1]);

const start = Math.min(rate, frames - rate * 4);
const end = Math.min(frames, start + rate * 4);
const w = (2 * Math.PI * f0) / rate;

let ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
for (let n = start; n < end; n += 1) {
  const s = Math.sin(w * n), c = Math.cos(w * n);
  ss += s * s; cc += c * c; sc += s * c; xs += x[n] * s; xc += x[n] * c;
}
const det = ss * cc - sc * sc;
const a = (xs * cc - xc * sc) / det;
const b = (xc * ss - xs * sc) / det;
const amp = Math.hypot(a, b);
const db = (v) => 20 * Math.log10(Math.max(v, 1e-14));

const r = new Float64Array(end - start);
let sig = 0, res = 0;
for (let n = start; n < end; n += 1) {
  r[n - start] = x[n] - (a * Math.sin(w * n) + b * Math.cos(w * n));
  sig += x[n] * x[n];
  res += r[n - start] * r[n - start];
}
console.log(`${path}  f0=${f0}  block=${block}`);
console.log(`  sine amp ${amp.toFixed(5)} (${db(amp).toFixed(1)} dBFS)`);
console.log(`  residual ${db(Math.sqrt(res / r.length)).toFixed(1)} dBFS  =>  ${(100 * Math.sqrt(res / sig)).toFixed(4)} %  (${db(Math.sqrt(res / sig)).toFixed(1)} dB)`);

// Residual RMS per second: a reverb tail decays, an artifact does not.
const perSec = [];
for (let s = 0; s * rate < r.length; s += 1) {
  const from = s * rate;
  const to = Math.min(r.length, from + rate);
  let e = 0;
  for (let i = from; i < to; i += 1) e += r[i] * r[i];
  perSec.push(db(Math.sqrt(e / Math.max(1, to - from))).toFixed(1));
}
console.log(`  residual per second: ${perSec.join(" ")} dBFS`);

// Block folding: if the residual is block-coherent, one shape repeats and the
// folded RMS swings far between the worst and best phase.
const rms = new Float64Array(block), cnt = new Float64Array(block);
for (let i = 0; i < r.length; i += 1) {
  const p = i % block;
  rms[p] += r[i] * r[i]; cnt[p] += 1;
}
let worst = 0, worstP = 0, best = Infinity, bestP = 0;
for (let p = 0; p < block; p += 1) {
  rms[p] = Math.sqrt(rms[p] / (cnt[p] || 1));
  if (rms[p] > worst) { worst = rms[p]; worstP = p; }
  if (rms[p] < best) { best = rms[p]; bestP = p; }
}
console.log(`  folded@${block}: worst ${db(worst).toFixed(1)} (phase ${worstP}), best ${db(best).toFixed(1)} (phase ${bestP}), ratio ${(worst / Math.max(best, 1e-14)).toFixed(1)}x`);

// Same fold at the tone period: a pure steady tone residual would be constant.
const rms48 = new Float64Array(48), cnt48 = new Float64Array(48);
for (let i = 0; i < r.length; i += 1) { const p = i % 48; rms48[p] += r[i] * r[i]; cnt48[p] += 1; }
let w48 = 0, b48 = Infinity;
for (let p = 0; p < 48; p += 1) { rms48[p] = Math.sqrt(rms48[p] / (cnt48[p] || 1)); w48 = Math.max(w48, rms48[p]); b48 = Math.min(b48, rms48[p]); }
console.log(`  folded@48: ratio ${(w48 / Math.max(b48, 1e-14)).toFixed(1)}x`);

// Spectrum of the residual, strongest lines (skip the DC and fundamental bins).
const N = 32768;
const src = r.subarray(0, N);
const re = new Float64Array(N), im = new Float64Array(N);
for (let i = 0; i < N; i += 1) {
  const t = (2 * Math.PI * i) / (N - 1);
  re[i] = src[i] * (0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t));
}
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i += 1) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k += 1) {
        const ur = re[i + k], ui = im[i + k];
        const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}
fft(re, im);
const binHz = rate / N;
const pow = new Float64Array(N / 2);
for (let k = 2; k < N / 2; k += 1) pow[k] = re[k] * re[k] + im[k] * im[k];
const peaks = [];
for (let k = 2; k < N / 2 - 1; k += 1) {
  if (pow[k] > pow[k - 1] && pow[k] >= pow[k + 1]) peaks.push([k * binHz, pow[k]]);
}
peaks.sort((p, q) => q[1] - p[1]);
const fundPow = Math.pow(amp * N * 0.25, 2);
console.log(`  strongest residual lines: ${peaks.slice(0, 8).map(([f, p]) => `${f.toFixed(1)}Hz:${db(Math.sqrt(p / fundPow)).toFixed(0)}dBc`).join("  ")}`);
