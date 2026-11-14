// Reads the interleaved float32 stereo dump written by NC_AUDIO_DUMP and
// reports the numbers that separate music from digital noise: peak/crest
// factor, DC offset, zero-crossing rate and spectral flatness.
import { readFileSync } from "node:fs";

const path = process.argv[2];
if (!path) {
  console.error("usage: node scripts/analyze-dump.mjs <dump.f32> [rate]");
  process.exit(2);
}

const rate = Number(process.argv[3] ?? 48000);
const buf = readFileSync(path);
const samples = buf.byteLength / 4;
const frames = Math.floor(samples / 2);
const data = new Float32Array(buf.buffer, buf.byteOffset, frames * 2);

let peak = 0;
let sumSq = 0;
let sum = 0;
let crossings = 0;
let diffSq = 0;
let nonFinite = 0;
let prev = 0;

for (let i = 0; i < frames; i += 1) {
  const l = data[i * 2];
  const r = data[i * 2 + 1];
  if (!Number.isFinite(l) || !Number.isFinite(r)) {
    nonFinite += 1;
    continue;
  }
  const mono = 0.5 * (l + r);
  peak = Math.max(peak, Math.abs(l), Math.abs(r));
  sumSq += mono * mono;
  sum += mono;
  if (i > 0 && (mono >= 0) !== (prev >= 0)) crossings += 1;
  const d = mono - prev;
  diffSq += d * d;
  prev = mono;
}

const rms = Math.sqrt(sumSq / Math.max(1, frames));
const dc = sum / Math.max(1, frames);
const db = (v) => (v > 0 ? 20 * Math.log10(v) : -Infinity);

// Spectral flatness over a Hann-windowed 8192-sample block from the middle.
const N = 8192;
const fft = (re, im) => {
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
};

let flatness = null;
let dominant = null;
if (frames >= N) {
  const start = Math.max(0, Math.floor((frames - N) / 2));
  const re = new Float64Array(N);
  const im = new Float64Array(N);
  for (let i = 0; i < N; i += 1) {
    const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (N - 1));
    re[i] = 0.5 * (data[(start + i) * 2] + data[(start + i) * 2 + 1]) * w;
  }
  fft(re, im);
  let logSum = 0;
  let linSum = 0;
  let count = 0;
  let best = 0;
  let bestBin = 0;
  for (let k = 1; k < N / 2; k += 1) {
    const mag = Math.hypot(re[k], im[k]);
    logSum += Math.log(mag + 1e-12);
    linSum += mag;
    count += 1;
    if (mag > best) {
      best = mag;
      bestBin = k;
    }
  }
  flatness = Math.exp(logSum / count) / (linSum / count);
  dominant = (bestBin * rate) / N;
}

console.log(
  JSON.stringify(
    {
      frames,
      seconds: +(frames / rate).toFixed(3),
      peak,
      peakDb: +db(peak).toFixed(1),
      rms: +rms.toFixed(4),
      rmsDb: +db(rms).toFixed(1),
      crest: rms > 0 ? +(peak / rms).toFixed(1) : null,
      dc: +dc.toFixed(5),
      zeroCrossingsPerSecond: Math.round((crossings * rate) / Math.max(1, frames)),
      spectralFlatness: flatness === null ? null : +flatness.toFixed(3),
      dominantHz: dominant === null ? null : Math.round(dominant),
      nonFinite
    },
    null,
    2
  )
);
