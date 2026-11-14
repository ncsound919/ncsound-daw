// Compares the pre-master and post-master captures so a fault can be
// attributed to the master chain rather than guessed at.
import { readFileSync } from "node:fs";

const rate = 48000;
const N = 8192;

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

const bands = [
  [20, 100],
  [100, 300],
  [300, 1000],
  [1000, 3000],
  [3000, 8000],
  [8000, 20000]
];

function report(label, path) {
  let buf;
  try {
    buf = readFileSync(path);
  } catch {
    console.log(`${label}: missing (${path})`);
    return;
  }
  const frames = Math.floor(buf.byteLength / 8);
  const d = new Float32Array(buf.buffer, buf.byteOffset, frames * 2);

  let peak = 0;
  let sumSq = 0;
  for (let i = 0; i < frames; i += 1) {
    const l = d[i * 2];
    const r = d[i * 2 + 1];
    peak = Math.max(peak, Math.abs(l), Math.abs(r));
    sumSq += 0.5 * (l * l + r * r);
  }
  const rms = Math.sqrt(sumSq / Math.max(1, frames));
  const db = (v) => (v > 0 ? +(20 * Math.log10(v)).toFixed(1) : -Infinity);

  const start = Math.max(0, Math.min(frames - N, Math.floor(frames / 2)));
  const re = new Float64Array(N);
  const im = new Float64Array(N);
  for (let i = 0; i < N; i += 1) {
    const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (N - 1));
    re[i] = 0.5 * (d[(start + i) * 2] + d[(start + i) * 2 + 1]) * w;
  }
  fft(re, im);

  const mags = [];
  let total = 0;
  for (let k = 1; k < N / 2; k += 1) {
    const m = Math.hypot(re[k], im[k]);
    mags.push([(k * rate) / N, m]);
    total += m * m;
  }
  const energy = bands.map(([lo, hi]) => {
    let e = 0;
    for (const [f, m] of mags) if (f >= lo && f < hi) e += m * m;
    return `${lo}-${hi}Hz ${total > 0 ? ((100 * e) / total).toFixed(1) : "0"}%`;
  });

  // Aliasing from an un-band-limited oscillator lands as broadband hash well
  // above the musical body, so this ratio is the distortion figure: it moves
  // when an oscillator starts folding harmonics, and is independent of fader
  // level because both terms scale together.
  let bodyE = 0;
  let hashE = 0;
  for (const [f, m] of mags) {
    if (f >= 100 && f < 300) bodyE += m * m;
    if (f >= 4000 && f < 20000) hashE += m * m;
  }
  const crest = rms > 0 ? 20 * Math.log10(peak / rms) : 0;
  const hash = bodyE > 0 && hashE > 0 ? 10 * Math.log10(hashE / bodyE) : -Infinity;

  console.log(
    [
      `${label}: frames=${frames} peak=${peak.toFixed(3)} (${db(peak)} dBFS) rms=${db(rms)} dB`,
      `  ${energy.join(" | ")}`,
      `  crest=${crest.toFixed(1)} dB  hash/body=${hash.toFixed(1)} dB`
    ].join("\n")
  );
}

report("PRE ", process.argv[2] ?? "build/pre-mixdump.f32");
report("POST", process.argv[3] ?? "build/post-mixdump.f32");
