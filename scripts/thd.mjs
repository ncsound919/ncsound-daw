// Distortion analyser for a captured sine: reports THD, THD+N and the strongest
// inharmonic components, which is how a metallic/aliased stage is identified.
import { readFileSync } from "node:fs";

const [, , path, hzArg] = process.argv;
const rate = 48000;
const N = 65536;

const buf = readFileSync(path);
const frames = Math.floor(buf.byteLength / 8);
const d = new Float32Array(buf.buffer, buf.byteOffset, frames * 2);
const mono = new Float64Array(frames);
for (let i = 0; i < frames; i += 1) mono[i] = 0.5 * (d[i * 2] + d[i * 2 + 1]);

// Skip the first second: transport start and limiter settling.
const start = Math.min(frames - N - 1, rate);
const re = new Float64Array(N);
const im = new Float64Array(N);
for (let i = 0; i < N; i += 1) {
  // 4-term Blackman-Harris: sidelobes far enough down to see -120 dB harmonics.
  const t = (2 * Math.PI * i) / (N - 1);
  const w = 0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t);
  re[i] = mono[start + i] * w;
}

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i += 1) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wr = Math.cos(ang);
    const wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1;
      let ci = 0;
      for (let k = 0; k < len / 2; k += 1) {
        const ur = re[i + k];
        const ui = im[i + k];
        const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr;
        im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr;
        im[i + k + len / 2] = ui - vi;
        const nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
}
fft(re, im);

const binHz = rate / N;
const power = new Float64Array(N / 2);
for (let k = 1; k < N / 2; k += 1) power[k] = re[k] * re[k] + im[k] * im[k];

// Fundamental: strongest bin, refined by parabolic interpolation.
let peakBin = 1;
for (let k = 2; k < N / 2; k += 1) if (power[k] > power[peakBin]) peakBin = k;
const a = power[peakBin - 1];
const b = power[peakBin];
const c = power[peakBin + 1];
const delta = (0.5 * (a - c)) / (a - 2 * b + c || 1);
const f0 = (peakBin + delta) * binHz;

// Sum a window around one frequency (+/- 4 bins covers the window's main lobe).
const near = (f, half = 4) => {
  const centre = Math.round(f / binHz);
  let sum = 0;
  for (let k = Math.max(1, centre - half); k <= Math.min(N / 2 - 1, centre + half); k += 1) sum += power[k];
  return sum;
};

const fund = near(f0, 5);
const harmonicMask = new Uint8Array(N / 2);
for (let h = 1; h * f0 < rate / 2; h += 1) {
  const centre = Math.round((h * f0) / binHz);
  for (let k = Math.max(1, centre - 5); k <= Math.min(N / 2 - 1, centre + 5); k += 1) harmonicMask[k] = 1;
}

let harm = 0;
for (let h = 2; h * f0 < rate / 2; h += 1) harm += near(h * f0);

let total = 0;
for (let k = Math.max(1, Math.round(20 / binHz)); k < N / 2; k += 1) total += power[k];

const dbRatio = (x) => (x > 0 ? 10 * Math.log10(x / fund) : -Infinity);

// Strongest component that is neither the fundamental nor a harmonic of it.
const spots = [];
for (let k = 2; k < N / 2 - 1; k += 1) {
  if (harmonicMask[k]) continue;
  if (power[k] > power[k - 1] && power[k] >= power[k + 1]) spots.push([k * binHz, power[k]]);
}
spots.sort((x, y) => y[1] - x[1]);

const thd = Math.sqrt(harm / fund) * 100;
const thdn = Math.sqrt(Math.max(0, total - fund) / fund) * 100;

console.log(`${path}`);
console.log(`  fundamental ${f0.toFixed(2)} Hz  requested ${hzArg ?? "-"}`);
console.log(`  THD   ${thd.toFixed(3)} %  (${(20 * Math.log10(thd / 100)).toFixed(1)} dB)`);
console.log(`  THD+N ${thdn.toFixed(3)} %  (${(20 * Math.log10(thdn / 100)).toFixed(1)} dB)`);
for (const [f, p] of spots.slice(0, 6)) console.log(`  inharmonic ${f.toFixed(1)} Hz at ${dbRatio(p).toFixed(1)} dBc`);
