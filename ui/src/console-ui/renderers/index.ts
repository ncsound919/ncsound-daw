// GENERATED from console-ui — edit there, then run `npm run sync`.
import {
  AMBER,
  AMBER_DEEP,
  CREAM,
  CYAN,
  filterDb,
  glow,
  hash,
  label,
  logX,
  noGlow,
  polyline,
  xHz,
  type Ctx,
  type Renderer,
} from "./common";

export * from "./common";

/* ---------------------------------------------------------------- filter */
export interface FilterParams {
  fc: number;
  q?: number;
  type?: "lp" | "hp" | "bp";
  poles?: 2 | 4;
  modOct?: number;
  modRate?: number;
}
export const filterRenderer: Renderer<FilterParams, Record<string, never>, { fc?: number }> = {
  grid: "log",
  draw(g, w, h, t, p, _s, frame) {
    const q = p.q ?? 1.6,
      mod = p.modOct ?? 0,
      rate = p.modRate ?? 0.35,
      type = p.type ?? "lp",
      poles = p.poles ?? 4;
    const top = 18,
      bot = -54;
    const yy = (db: number) => ((top - Math.max(bot, Math.min(top, db))) / (top - bot)) * h;
    const curve = (fc: number): [number, number][] => {
      const pts: [number, number][] = [];
      for (let x = 0; x <= w; x += 2) pts.push([x, yy(filterDb(xHz(x, w), fc, q, type, poles))]);
      return pts;
    };
    if (mod > 0) {
      g.setLineDash([3, 4]);
      g.lineWidth = 1;
      g.strokeStyle = "rgba(60,214,221,.45)";
      polyline(g, curve(p.fc * Math.pow(2, -mod)));
      g.stroke();
      polyline(g, curve(p.fc * Math.pow(2, mod)));
      g.stroke();
      g.setLineDash([]);
    }
    const fc = frame?.fc ?? p.fc * Math.pow(2, mod * Math.sin(t * Math.PI * 2 * rate));
    const pts = curve(fc);
    polyline(g, pts);
    g.lineTo(w, h);
    g.lineTo(0, h);
    g.closePath();
    const gr = g.createLinearGradient(0, 0, 0, h);
    gr.addColorStop(0, "rgba(255,138,0,.28)");
    gr.addColorStop(1, "rgba(255,138,0,0)");
    g.fillStyle = gr;
    g.fill();
    polyline(g, pts);
    g.lineWidth = 2;
    g.strokeStyle = AMBER;
    glow(g, AMBER_DEEP, 10);
    g.stroke();
    noGlow(g);
    const x = logX(fc, w);
    g.fillStyle = CYAN;
    glow(g, CYAN, 8);
    g.beginPath();
    g.arc(x, yy(filterDb(fc, fc, q, type, poles)), 4, 0, 7);
    g.fill();
    noGlow(g);
    label(
      g,
      fc >= 1000 ? `${(fc / 1000).toFixed(2)} kHz` : `${Math.round(fc)} Hz`,
      Math.min(w - 60, x + 8),
      14,
      "rgba(255,174,58,.8)",
    );
  },
};

/* -------------------------------------------------------------------- eq */
export interface EqBand {
  f: number;
  g: number;
  q: number;
  t: "b" | "ls" | "hs";
}
export interface EqParams {
  bands: EqBand[];
  hpf?: number;
  selected?: number;
  range?: number;
  curve?: (hz: number) => number;
}
export const eqRenderer: Renderer<EqParams, { bars: number[] }, { spectrum?: ArrayLike<number> }> =
  {
    grid: "log",
    init: () => ({ bars: [] }),
    draw(g, w, h, t, p, s, frame) {
      const n = 84;
      if (s.bars.length !== n) s.bars = new Array(n).fill(0);
      for (let i = 0; i < n; i++) {
        const fr = i / n;
        const live = frame?.spectrum
          ? frame.spectrum[Math.floor(fr * frame.spectrum.length)]
          : undefined;
        const target =
          live ??
          0.78 -
            fr * 0.42 +
            0.14 * Math.sin(fr * 11 + t * 2.1) * Math.sin(t * 0.7 + i) +
            0.12 * hash(i + Math.floor(t * 20));
        s.bars[i] += (target - s.bars[i]) * 0.35;
      }
      const bw = w / n;
      for (let i = 0; i < n; i++) {
        const bh = Math.max(0, s.bars[i]) * h * 0.85;
        const gr = g.createLinearGradient(0, h - bh, 0, h);
        gr.addColorStop(0, "rgba(255,174,58,.22)");
        gr.addColorStop(1, "rgba(255,174,58,.03)");
        g.fillStyle = gr;
        g.fillRect(i * bw + 0.5, h - bh, bw - 1, bh);
      }
      const range = p.range ?? 18,
        hpf = p.hpf ?? 0;
      const yy = (db: number) => h / 2 - (db / range) * (h / 2 - 8);
      const resp =
        p.curve ??
        ((f: number) => {
          let db = 0;
          p.bands.forEach((b) => {
            const o = Math.log2(f / b.f);
            if (b.t === "b") db += b.g * Math.exp(-(o * o) / (2 * Math.pow(0.9 / b.q, 2)));
            else if (b.t === "ls") db += b.g / (1 + Math.exp(o * 3));
            else db += b.g / (1 + Math.exp(-o * 3));
          });
          if (hpf > 0 && f < hpf * 2) db -= 12 * Math.max(0, Math.log2((hpf * 2) / f));
          return db;
        });
      g.strokeStyle = "rgba(255,174,58,.25)";
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(0, h / 2 + 0.5);
      g.lineTo(w, h / 2 + 0.5);
      g.stroke();
      g.beginPath();
      for (let x = 0; x <= w; x += 2) {
        const y = yy(resp(xHz(x, w)));
        x ? g.lineTo(x, y) : g.moveTo(x, y);
      }
      g.lineWidth = 2.2;
      g.strokeStyle = AMBER;
      glow(g, AMBER_DEEP, 12);
      g.stroke();
      noGlow(g);
      p.bands.forEach((b, i) => {
        const x = logX(b.f, w),
          y = yy(resp(b.f)),
          sel = i === p.selected;
        if (sel) {
          g.fillStyle = "rgba(255,138,0,.18)";
          g.beginPath();
          g.arc(x, y, 16 + 2 * Math.sin(t * 3), 0, 7);
          g.fill();
        }
        g.fillStyle = sel ? AMBER : "#1a1106";
        g.strokeStyle = AMBER;
        g.lineWidth = 1.5;
        g.beginPath();
        g.arc(x, y, 9, 0, 7);
        g.fill();
        g.stroke();
        g.fillStyle = sel ? "#1a1106" : AMBER;
        g.font = '11px "Share Tech Mono", monospace';
        g.textAlign = "center";
        g.fillText(String(i + 1), x, y + 4);
        g.textAlign = "left";
      });
      [
        [100, "100"],
        [1000, "1k"],
        [10000, "10k"],
      ].forEach(([f, l]) =>
        label(g, l as string, logX(f as number, w) + 3, h - 4, "rgba(255,174,58,.5)", 9),
      );
    },
  };

/* -------------------------------------------------------------- spectrum */
/** Analyser bars only (no curve). Without a frame it draws a fixed, still
 *  preview shape: nothing moves unless a host sends a spectrum. */
export const spectrumRenderer: Renderer<
  { height?: number },
  { bars: number[] },
  { spectrum?: ArrayLike<number> }
> = {
  grid: "log",
  init: () => ({ bars: [] }),
  draw(g, w, h, _t, p, s, frame) {
    const n = 84;
    if (s.bars.length !== n) s.bars = new Array(n).fill(0);
    const src = frame?.spectrum;
    for (let i = 0; i < n; i++) {
      const target =
        src && src.length
          ? src[Math.floor((i / n) * src.length)]
          : 0.6 - 0.35 * (i / n) + 0.08 * Math.sin(i * 0.9);
      s.bars[i] += (target - s.bars[i]) * 0.35;
    }
    const bw = w / n,
      scale = p.height ?? 0.85;
    for (let i = 0; i < n; i++) {
      const bh = Math.max(0, s.bars[i]) * h * scale;
      const gr = g.createLinearGradient(0, h - bh, 0, h);
      gr.addColorStop(0, "rgba(255,174,58,.24)");
      gr.addColorStop(1, "rgba(255,174,58,.03)");
      g.fillStyle = gr;
      g.fillRect(i * bw + 0.5, h - bh, bw - 1, bh);
    }
    [
      [100, "100"],
      [1000, "1k"],
      [10000, "10k"],
    ].forEach(([f, l]) =>
      label(g, l as string, logX(f as number, w) + 3, h - 4, "rgba(255,174,58,.5)"),
    );
  },
};

/* --------------------------------------------------------------- history */
export const historyRenderer: Renderer<
  { label?: string },
  { hist: [number, number][] },
  { gr?: number; level?: number }
> = {
  grid: "linear",
  init: () => ({ hist: [] }),
  draw(g, w, h, t, _p, s, frame) {
    const n = Math.floor(w / 2);
    const beat = (t * 1.53) % 1;
    const gr =
      frame?.gr ??
      Math.min(
        1,
        (beat < 0.08 ? 0.75 : 0.75 * Math.exp(-(beat - 0.08) * 5)) + 0.1 * hash(Math.floor(t * 30)),
      );
    const lvl =
      frame?.level ?? 0.55 + 0.3 * Math.exp(-beat * 4) + 0.08 * hash(Math.floor(t * 30) + 3);
    s.hist.push([gr, lvl]);
    while (s.hist.length > n) s.hist.shift();
    const off = n - s.hist.length;
    g.fillStyle = "rgba(216,213,206,.12)";
    s.hist.forEach((v, i) => {
      const bh = v[1] * h;
      g.fillRect((i + off) * 2, h - bh, 1.5, bh);
    });
    g.beginPath();
    g.moveTo(off * 2, 0);
    s.hist.forEach((v, i) => g.lineTo((i + off) * 2, v[0] * h * 0.5));
    g.lineTo((s.hist.length - 1 + off) * 2, 0);
    g.closePath();
    g.fillStyle = "rgba(255,138,0,.3)";
    g.fill();
    polyline(
      g,
      s.hist.map((v, i) => [(i + off) * 2, v[0] * h * 0.5]),
    );
    g.strokeStyle = AMBER;
    g.lineWidth = 1.5;
    glow(g, AMBER_DEEP, 6);
    g.stroke();
    noGlow(g);
    label(g, "GR", 4, h / 2 + 12, "rgba(255,174,58,.6)", 9);
    label(g, "IN", 4, h - 4, "rgba(255,174,58,.6)", 9);
  },
};

/* ------------------------------------------------------------------ taps */
export interface TapsParams {
  feedback: number;
  topology: "single" | "dual" | "pingpong" | "ratio";
  taps?: number;
  duck?: boolean;
  diffusion?: boolean;
  blobs?: boolean;
  rate?: number;
}
/** Live echo pattern from a host: time (ms) and level per repeat and side, plus
 *  the current duck gain (0..1, 1 = not ducking). Static: nothing moves unless
 *  the settings or the ducking do. */
export interface EchoTap {
  ms: number;
  level: number;
}
export interface TapsFrame {
  duck?: number;
  left?: EchoTap[];
  right?: EchoTap[];
  spanMs?: number;
}
function drawLiveTaps(g: Ctx, w: number, h: number, f: TapsFrame): void {
  const mid = h / 2,
    x0 = 18,
    left = f.left ?? [],
    right = f.right ?? [];
  const all = [...left, ...right];
  const span = f.spanMs ?? Math.max(100, ...all.map((e) => e.ms)) * 1.08;
  const xOf = (ms: number) => x0 + (ms / span) * (w - x0 - 12);
  g.strokeStyle = "rgba(255,174,58,.25)";
  g.beginPath();
  g.moveTo(0, mid + 0.5);
  g.lineTo(w, mid + 0.5);
  g.stroke();
  label(g, "L", 4, 12, "rgba(255,174,58,.55)", 10);
  label(g, "R", 4, h - 4, "rgba(255,174,58,.55)", 10);
  g.fillStyle = CREAM;
  g.fillRect(x0 - 2, mid - h * 0.42, 3, h * 0.84);
  const duck = f.duck ?? 1;
  const side = (taps: EchoTap[], sgn: number) =>
    taps.forEach((e) => {
      const x = xOf(e.ms),
        len = Math.min(1, e.level) * duck * (h / 2 - 8),
        y1 = mid + sgn * len;
      g.strokeStyle = AMBER;
      g.lineWidth = 2;
      glow(g, AMBER_DEEP, 6);
      g.beginPath();
      g.moveTo(x, mid);
      g.lineTo(x, y1);
      g.stroke();
      g.fillStyle = AMBER;
      g.beginPath();
      g.arc(x, y1, 3, 0, 7);
      g.fill();
    });
  side(left, -1);
  side(right, 1);
  noGlow(g);
  for (let ms = 250; ms < span; ms += span > 2500 ? 1000 : span > 1200 ? 500 : 250) {
    const x = xOf(ms);
    g.strokeStyle = "rgba(255,174,58,.10)";
    g.beginPath();
    g.moveTo(x + 0.5, 0);
    g.lineTo(x + 0.5, h);
    g.stroke();
    label(
      g,
      ms >= 1000 ? `${(ms / 1000).toFixed(ms % 1000 ? 2 : 0)} s` : `${ms} ms`,
      x + 3,
      h - 4,
      "rgba(255,174,58,.45)",
      10,
    );
  }
  if (duck < 0.99)
    label(g, `DUCK ${Math.round((1 - duck) * 100)}%`, w - 70, 14, "rgba(60,214,221,.85)", 10);
}

export const tapsRenderer: Renderer<TapsParams, Record<string, never>, TapsFrame> = {
  grid: "linear",
  draw(g, w, h, t, p, _s, frame) {
    if (frame && (frame.left || frame.right)) {
      drawLiveTaps(g, w, h, frame);
      return;
    }
    const n = p.taps ?? 9,
      fb = p.feedback,
      rate = p.rate ?? 1.2,
      mid = h / 2,
      x0 = 18,
      sp = (w - x0 - 10) / n;
    g.strokeStyle = "rgba(255,174,58,.25)";
    g.beginPath();
    g.moveTo(0, mid + 0.5);
    g.lineTo(w, mid + 0.5);
    g.stroke();
    label(g, "L", 4, 12, "rgba(255,174,58,.55)", 9);
    label(g, "R", 4, h - 4, "rgba(255,174,58,.55)", 9);
    const hot = Math.floor(((t * rate) % 1) * (n + 1)) - 1;
    const duck = p.duck ? 0.55 + 0.45 * Math.abs(Math.sin(t * 1.7)) : 1;
    g.fillStyle = CREAM;
    g.fillRect(x0 - 2, mid - h * 0.42, 3, h * 0.84);
    for (let i = 0; i < n; i++) {
      const a = Math.pow(fb, i) * duck;
      const x = x0 + (i + 1) * sp * (p.topology === "ratio" && i % 2 ? 0.88 : 1);
      const sides =
        p.topology === "single" ? [0] : p.topology === "dual" ? [-1, 1] : [i % 2 ? 1 : -1];
      for (const sd of sides) {
        const len = a * (h / 2 - 8),
          y1 = sd > 0 ? mid + len : mid - len,
          on = i === hot;
        g.strokeStyle = on ? CREAM : AMBER;
        g.lineWidth = on ? 3 : 2;
        glow(g, AMBER_DEEP, on ? 16 : 6);
        g.beginPath();
        g.moveTo(x, mid);
        g.lineTo(x, y1);
        g.stroke();
        g.fillStyle = on ? CREAM : AMBER;
        g.beginPath();
        g.arc(x, y1, on ? 4 : 3, 0, 7);
        g.fill();
        if (p.blobs) {
          const bw = sp * 0.8,
            sgn = sd === 0 ? -1 : sd;
          g.strokeStyle = on
            ? "rgba(255,243,220,.9)"
            : `rgba(255,174,58,${(0.35 + 0.5 * a).toFixed(2)})`;
          g.lineWidth = 1.4;
          g.beginPath();
          for (let u = 0; u <= 1.001; u += 0.02) {
            const env = Math.exp(-u * 4) * Math.sin((Math.min(1, u * 12) * Math.PI) / 2);
            const yy = mid + sgn * Math.abs(Math.sin(u * 38 + i + t * 3)) * len * env;
            u === 0 ? g.moveTo(x, mid) : g.lineTo(x + u * bw, yy);
          }
          g.stroke();
        }
        if (p.diffusion) {
          g.fillStyle = `rgba(255,174,58,${(0.08 * a).toFixed(3)})`;
          g.beginPath();
          g.ellipse(x + sp * 0.3, (mid + y1) / 2, sp * 0.45, Math.abs(y1 - mid) / 2 + 2, 0, 0, 7);
          g.fill();
        }
      }
    }
    noGlow(g);
    if (p.duck)
      label(g, `DUCK ${Math.round((1 - duck) * 100)}%`, w - 64, 12, "rgba(60,214,221,.8)", 9);
  },
};

/* ------------------------------------------------------------- waterfall */
export interface WaterfallParams {
  predelay: number;
  decay: number;
  hfDamp?: number;
  lowCut?: number;
  slices?: number;
}
export const waterfallRenderer: Renderer<WaterfallParams> = {
  grid: "none",
  draw(g, w, h, t, p) {
    const N = p.slices ?? 26,
      pre = p.predelay,
      dec = p.decay,
      hc = p.hfDamp ?? 0.5,
      lc = p.lowCut ?? 0.3;
    const ws = w * 0.74,
      depthX = w * 0.22,
      depthY = h * 0.5,
      cur = (t * 0.22) % 1;
    for (let i = N - 1; i >= 0; i--) {
      const d = i / (N - 1),
        ox = 14 + d * depthX,
        y0 = h - 16 - d * depthY;
      const di = dec * (1 - hc * d * 0.85) * (d < 0.15 ? 1 - lc * 0.6 : 1);
      const A = (d < 0.2 ? 1 - lc * (0.2 - d) * 3 : 1) * (1 - d * 0.25);
      const pts: [number, number][] = [];
      for (let x = 0; x <= ws; x += 3) {
        const u = x / ws;
        let env = 0;
        if (u >= pre) {
          const v = u - pre;
          env =
            Math.exp(-v / di) *
            (0.7 + 0.3 * hash(i * 97 + Math.floor(x / 3) + Math.floor(t * 6) * 0.5));
          if (v < 0.04 && hash(x * 1.3 + i) > 0.85) env = Math.max(env, 0.9);
        }
        pts.push([ox + x, y0 - env * h * 0.42 * A]);
      }
      g.beginPath();
      g.moveTo(ox, y0);
      pts.forEach(([x, y]) => g.lineTo(x, y));
      g.lineTo(ox + ws, y0);
      g.closePath();
      g.fillStyle = "rgba(14,9,3,.94)";
      g.fill();
      g.strokeStyle = `rgba(255,174,58,${(0.25 + 0.75 * (1 - d)).toFixed(2)})`;
      g.lineWidth = i === 0 ? 2 : 1.2;
      if (i < 3) glow(g, AMBER_DEEP, 8);
      polyline(g, pts);
      g.stroke();
      noGlow(g);
    }
    const cx = 14 + cur * ws;
    g.strokeStyle = "rgba(60,214,221,.55)";
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(cx, h - 16);
    g.lineTo(cx + depthX, h - 16 - depthY);
    g.stroke();
    label(g, "TIME →", 14, h - 3);
    label(g, "FREQ ↗", 14 + ws + depthX * 0.55, h - 22 - depthY * 0.55);
    g.strokeStyle = "rgba(60,214,221,.8)";
    g.setLineDash([3, 3]);
    g.beginPath();
    g.moveTo(14 + pre * ws, h - 16);
    g.lineTo(14 + pre * ws + depthX, h - 16 - depthY);
    g.stroke();
    g.setLineDash([]);
  },
};

/* ---------------------------------------------------------------- shaper */
export interface ShapeLane {
  name: string;
  color: string;
  fill?: string;
  enabled: boolean;
  points: [number, number][];
}
export interface ShaperParams {
  lanes: ShapeLane[];
  selected: number;
  steps?: number;
  rate?: number;
  top?: number;
  bottom?: number;
}
export function shapeAt(pts: [number, number][], u: number): number {
  const x = u - Math.floor(u);
  for (let i = 1; i < pts.length; i++) {
    if (x <= pts[i][0]) {
      const a = pts[i - 1],
        b = pts[i];
      return a[1] + (b[1] - a[1]) * ((x - a[0]) / Math.max(1e-6, b[0] - a[0]));
    }
  }
  return pts[pts.length - 1]?.[1] ?? 0;
}
export const shaperRenderer: Renderer<ShaperParams, Record<string, never>, { phase?: number }> = {
  grid: "none",
  draw(g, w, h, t, p, _s, frame) {
    const steps = p.steps ?? 16,
      rate = p.rate ?? 0.5,
      top = p.top ?? 10,
      bot = h - (p.bottom ?? 26),
      H = bot - top;
    for (let i = 0; i <= steps; i++) {
      const x = Math.round((i / steps) * w) + 0.5;
      g.strokeStyle = i % 4 === 0 ? "rgba(255,174,58,.18)" : "rgba(255,174,58,.06)";
      g.beginPath();
      g.moveTo(x, top);
      g.lineTo(x, bot);
      g.stroke();
    }
    const ph = frame?.phase ?? (t * rate) % 1;
    p.lanes.forEach((ln, li) => {
      if (li === p.selected) return;
      g.strokeStyle = ln.color;
      g.globalAlpha = ln.enabled ? 0.35 : 0.12;
      g.lineWidth = 1.5;
      g.beginPath();
      for (let x = 0; x <= w; x += 3) {
        const y = bot - shapeAt(ln.points, x / w) * H;
        x ? g.lineTo(x, y) : g.moveTo(x, y);
      }
      g.stroke();
      g.globalAlpha = 1;
    });
    const ln = p.lanes[p.selected];
    if (ln) {
      g.beginPath();
      g.moveTo(0, bot);
      for (let x = 0; x <= w; x += 2) g.lineTo(x, bot - shapeAt(ln.points, x / w) * H);
      g.lineTo(w, bot);
      g.closePath();
      const gr = g.createLinearGradient(0, top, 0, bot);
      gr.addColorStop(0, ln.fill ?? "rgba(255,138,0,.35)");
      gr.addColorStop(1, "rgba(255,138,0,0)");
      g.fillStyle = gr;
      g.fill();
      g.beginPath();
      for (let x = 0; x <= w; x += 2) {
        const y = bot - shapeAt(ln.points, x / w) * H;
        x ? g.lineTo(x, y) : g.moveTo(x, y);
      }
      g.strokeStyle = ln.color;
      g.lineWidth = 2.5;
      glow(g, ln.color, 12);
      g.stroke();
      noGlow(g);
      ln.points.forEach(([px, py]) => {
        g.fillStyle = "#140d04";
        g.strokeStyle = ln.color;
        g.lineWidth = 2;
        g.beginPath();
        g.arc(px * w, bot - py * H, 5, 0, 7);
        g.fill();
        g.stroke();
      });
      const x = ph * w,
        y = bot - shapeAt(ln.points, ph) * H;
      g.strokeStyle = "rgba(255,243,220,.5)";
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(x, top);
      g.lineTo(x, bot);
      g.stroke();
      g.fillStyle = CREAM;
      glow(g, ln.color, 16);
      g.beginPath();
      g.arc(x, y, 6, 0, 7);
      g.fill();
      noGlow(g);
    }
    for (let i = 0; i < steps; i++) {
      const on = Math.floor(ph * steps) === i;
      g.fillStyle = on ? AMBER : i % 4 === 0 ? "rgba(255,174,58,.35)" : "rgba(255,174,58,.14)";
      g.fillRect((i / steps) * w + 2, h - 14, w / steps - 4, 8);
    }
  },
};

/* ----------------------------------------------------------------- crush */
export interface CrushParams {
  levels: number;
  holdPx: number;
  jitter?: number;
}
export const crushRenderer: Renderer<CrushParams> = {
  grid: "none",
  draw(g, w, h, t, p) {
    const levels = Math.max(1, p.levels),
      sw = Math.max(2, p.holdPx),
      jit = p.jitter ?? 0.1,
      mid = h / 2,
      amp = h * 0.36;
    const f = (x: number) =>
      Math.sin((x / w) * 5 * Math.PI * 2 + t * 1.2) * 0.75 +
      Math.sin((x / w) * 13 * Math.PI * 2 + t * 2.1) * 0.22;
    g.strokeStyle = "rgba(216,213,206,.25)";
    g.lineWidth = 1.5;
    g.beginPath();
    for (let x = 0; x <= w; x += 2) {
      const y = mid - f(x) * amp;
      x ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
    g.strokeStyle = AMBER;
    g.lineWidth = 2;
    glow(g, AMBER_DEEP, 10);
    g.beginPath();
    for (let x = 0; x <= w; x += sw) {
      const xs = x + (hash(x + Math.floor(t * 12)) - 0.5) * jit * sw;
      const y = mid - (Math.round(f(xs) * levels) / levels) * amp;
      x === 0 ? g.moveTo(x, y) : g.lineTo(x, y);
      g.lineTo(Math.min(w, x + sw), y);
    }
    g.stroke();
    noGlow(g);
    g.strokeStyle = "rgba(255,174,58,.12)";
    g.lineWidth = 1;
    for (let i = -levels; i <= levels; i += 2) {
      const y = mid - (i / levels) * amp + 0.5;
      g.beginPath();
      g.moveTo(0, y);
      g.lineTo(w, y);
      g.stroke();
    }
  },
};

/* -------------------------------------------------------------- transfer */
/** Lo-Fi distortion curves. Must match overlay_retro_color/Source/LoFiDsp.h
 *  `shape()` (index = native "Distort Mode" choice). */
export function lofiShape(x: number, mode: number): number {
  const clamp = (v: number) => Math.max(-1, Math.min(1, v));
  switch (mode) {
    case 1:
      return Math.tanh(x * 0.9 + 0.12 * x * x);
    case 2: {
      let y = x;
      while (y > 1) y = 2 - y;
      while (y < -1) y = -2 - y;
      return y;
    }
    case 3:
      return clamp(x * 1.4);
    case 4: {
      const y = clamp(x * 3);
      return (y + 0.25 * y * y) * 0.8;
    }
    case 5:
      return x >= 0 ? 1 - Math.exp(-x) : -0.6 * (1 - Math.exp(x / 0.6));
    default:
      return Math.tanh(x);
  }
}
export interface TransferParams {
  distOn: boolean;
  mode: number;
  driveDb: number;
  distMix: number;
  digOn: boolean;
  levels: number;
  digMix: number;
}
/** Static input -> output curve of Distort then Digital's quantiser, computed
 *  from the current settings (no motion: nothing here is measured audio). The
 *  focus band and the sample-rate reduction are not part of a static curve. */
export const transferRenderer: Renderer<TransferParams> = {
  grid: "linear",
  draw(g, w, h, _t, p) {
    const pad = 10,
      sx = (x: number) => pad + ((x + 1) / 2) * (w - 2 * pad),
      sy = (y: number) => h / 2 - (Math.max(-1.25, Math.min(1.25, y)) * (h / 2 - pad)) / 1.25;
    g.strokeStyle = "rgba(255,174,58,.25)";
    g.setLineDash([3, 4]);
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(sx(-1), sy(-1));
    g.lineTo(sx(1), sy(1));
    g.stroke();
    g.setLineDash([]);
    const drive = Math.pow(10, p.driveDb / 20),
      norm = 1 / Math.sqrt(drive);
    g.beginPath();
    for (let i = 0; i <= 240; i++) {
      const x = -1 + (2 * i) / 240;
      let y = x;
      if (p.distOn) y = x * (1 - p.distMix) + lofiShape(x * drive, p.mode) * norm * p.distMix;
      if (p.digOn) y = y * (1 - p.digMix) + (Math.round(y * p.levels) / p.levels) * p.digMix;
      i ? g.lineTo(sx(x), sy(y)) : g.moveTo(sx(x), sy(y));
    }
    g.lineWidth = 2;
    g.strokeStyle = AMBER;
    glow(g, AMBER_DEEP, 10);
    g.stroke();
    noGlow(g);
    label(g, "IN", w - 22, h / 2 - 4, "rgba(255,174,58,.5)", 10);
    label(g, "OUT", sx(0) + 4, 12, "rgba(255,174,58,.5)", 10);
  },
};

/* ---------------------------------------------------------------- wobble */
export const wobbleRenderer: Renderer<{ wow: number; noise: number }> = {
  grid: "linear",
  draw(g, w, h, t, p) {
    const mid = h / 2;
    g.strokeStyle = "rgba(255,174,58,.3)";
    g.lineWidth = 1;
    g.beginPath();
    for (let x = 0; x <= w; x += 2) {
      const y = mid + Math.sin((x / w) * 18 + t * 2) * h * 0.28;
      x ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
    g.strokeStyle = AMBER;
    g.lineWidth = 2;
    glow(g, AMBER_DEEP, 10);
    g.beginPath();
    for (let x = 0; x <= w; x += 2) {
      const u = x / w;
      const drift =
        p.wow * 0.9 * Math.sin(u * 3 + t * 1.1) + p.wow * 0.25 * Math.sin(u * 23 + t * 7);
      const y =
        mid +
        Math.sin(u * 18 + t * 2 + drift) * h * 0.28 * (0.85 + 0.15 * Math.sin(t * 0.7)) +
        (hash(x + Math.floor(t * 24)) - 0.5) * p.noise * 5;
      x ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
    noGlow(g);
    for (let i = 0; i < 26 * p.noise; i++) {
      const s = i + Math.floor(t * 12) * 31;
      g.fillStyle = `rgba(255,243,220,${(0.3 + 0.6 * hash(s + 5)).toFixed(2)})`;
      g.fillRect(hash(s) * w, hash(s + 1) * h, 1.5, 1.5);
    }
  },
};

/* ---------------------------------------------------------------- vector */
export const vectorRenderer: Renderer<
  { width: number },
  Record<string, never>,
  { left?: ArrayLike<number>; right?: ArrayLike<number> }
> = {
  grid: "none",
  draw(g, w, h, t, p, _s, frame) {
    const cx = w / 2,
      cy = h / 2,
      r = Math.min(w, h) / 2 - 6;
    g.strokeStyle = "rgba(255,174,58,.14)";
    g.beginPath();
    g.moveTo(cx, cy - r);
    g.lineTo(cx, cy + r);
    g.moveTo(cx - r, cy);
    g.lineTo(cx + r, cy);
    g.moveTo(cx - r * 0.7, cy - r * 0.7);
    g.lineTo(cx + r * 0.7, cy + r * 0.7);
    g.moveTo(cx + r * 0.7, cy - r * 0.7);
    g.lineTo(cx - r * 0.7, cy + r * 0.7);
    g.stroke();
    g.beginPath();
    g.arc(cx, cy, r, Math.PI, 2 * Math.PI);
    g.stroke();
    const L = frame?.left,
      R = frame?.right;
    const n = L && R ? Math.min(L.length, R.length) : 420;
    for (let i = 0; i < n; i++) {
      let m: number, sd: number;
      if (L && R) {
        m = (L[i] + R[i]) / 2;
        sd = (L[i] - R[i]) / 2;
      } else {
        const k = t * 40 + i * 0.37;
        m =
          Math.sin(k * 0.9) * 0.6 +
          Math.sin(k * 2.3 + 1) * 0.3 +
          (hash(i + Math.floor(t * 30)) - 0.5) * 0.25;
        sd =
          (Math.sin(k * 1.7 + 2) * 0.5 + (hash(i * 7 + Math.floor(t * 30)) - 0.5) * 0.6) * p.width;
      }
      g.fillStyle = `rgba(255,174,58,${(0.25 + 0.5 * hash(i)).toFixed(2)})`;
      g.fillRect(cx + sd * r * 0.9, cy - m * r * 0.9, 1.6, 1.6);
    }
    label(g, "M", cx + 3, 10, "rgba(255,174,58,.6)", 9);
    label(g, "L", 4, 12, "rgba(255,174,58,.6)", 9);
    label(g, "R", w - 10, 12, "rgba(255,174,58,.6)", 9);
  },
};

/* ------------------------------------------------------------------ adsr */
export interface AdsrParams {
  a: number;
  d: number;
  s: number;
  r: number;
  color?: string;
}
export const adsrRenderer: Renderer<AdsrParams, Record<string, never>, { stage?: number }> = {
  grid: "linear",
  draw(g, w, h, t, p) {
    const pad = 6,
      W = w - pad * 2,
      H = h - pad * 2,
      hold = 0.25,
      tot = p.a + p.d + hold + p.r;
    const X = (u: number) => pad + (u / tot) * W,
      Y = (l: number) => pad + (1 - l) * H;
    const pts: [number, number][] = [
      [X(0), Y(0)],
      [X(p.a), Y(1)],
      [X(p.a + p.d), Y(p.s)],
      [X(p.a + p.d + hold), Y(p.s)],
      [X(tot), Y(0)],
    ];
    polyline(g, pts);
    g.closePath();
    g.fillStyle = "rgba(255,138,0,.14)";
    g.fill();
    polyline(g, pts);
    g.strokeStyle = p.color ?? AMBER;
    g.lineWidth = 2;
    glow(g, p.color ?? AMBER_DEEP, 8);
    g.stroke();
    noGlow(g);
    const u = ((t * 0.45) % 1) * tot;
    const lv =
      u < p.a
        ? u / p.a
        : u < p.a + p.d
          ? 1 - ((1 - p.s) * (u - p.a)) / p.d
          : u < p.a + p.d + hold
            ? p.s
            : p.s * (1 - (u - p.a - p.d - hold) / p.r);
    g.fillStyle = CYAN;
    glow(g, CYAN, 8);
    g.beginPath();
    g.arc(X(u), Y(Math.max(0, lv)), 3.5, 0, 7);
    g.fill();
    noGlow(g);
  },
};

/* ------------------------------------------------------------------- lfo */
export type LfoShape = "sin" | "tri" | "saw" | "sqr" | "rnd";
export function lfoValue(shape: LfoShape, phase: number): number {
  const ph = phase - Math.floor(phase);
  switch (shape) {
    case "tri":
      return ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4;
    case "saw":
      return ph * 2 - 1;
    case "sqr":
      return ph < 0.5 ? 1 : -1;
    case "rnd":
      return hash(Math.floor(ph * 8) + 11) * 2 - 1;
    default:
      return Math.sin(ph * Math.PI * 2);
  }
}
export const lfoRenderer: Renderer<
  { shape: LfoShape; cycles?: number; rate?: number; color?: string },
  Record<string, never>,
  { phase?: number }
> = {
  grid: "linear",
  draw(g, w, h, t, p, _s, frame) {
    const cyc = p.cycles ?? 2,
      col = p.color ?? CYAN,
      mid = h / 2,
      amp = h / 2 - 8;
    g.strokeStyle = col;
    g.lineWidth = 2;
    glow(g, col, 8);
    g.beginPath();
    for (let x = 0; x <= w; x += 2) {
      const y = mid - lfoValue(p.shape, (x / w) * cyc) * amp;
      x ? g.lineTo(x, y) : g.moveTo(x, y);
    }
    g.stroke();
    const ph = frame?.phase ?? (t * (p.rate ?? 0.5)) % 1;
    g.fillStyle = "#e8feff";
    glow(g, col, 14);
    g.beginPath();
    g.arc(ph * w, mid - lfoValue(p.shape, ph * cyc) * amp, 4.5, 0, 7);
    g.fill();
    noGlow(g);
  },
};

/* ------------------------------------------------------------------ wave */
export interface WaveParams {
  loopStart: number;
  loopEnd: number;
  spray?: number;
  peaks?: ArrayLike<number>;
  /** Crossfade length as a fraction of the loop: shades the two blended zones. */
  xfade?: number;
  /** Hosts with no playhead data set this false: then no synthetic playhead or
   *  grain sparks are drawn (a moving line with no audio behind it is fake). */
  demoMotion?: boolean;
}
export const waveRenderer: Renderer<WaveParams, { peaks: number[] }, { playhead?: number }> = {
  grid: "linear",
  init: () => ({ peaks: [] }),
  draw(g, w, h, t, p, s, frame) {
    const n = Math.floor(w / 3);
    if (p.peaks) {
      s.peaks = Array.from(
        { length: n },
        (_, i) => p.peaks![Math.floor((i / n) * p.peaks!.length)] ?? 0,
      );
    } else if (s.peaks.length !== n) {
      s.peaks = Array.from({ length: n }, (_, i) => {
        const u = i / n;
        const env = u < 0.03 ? u / 0.03 : Math.exp(-(u - 0.03) * 1.5);
        return env * (0.45 + 0.55 * Math.abs(Math.sin(i * 1.7) * Math.cos(i * 0.37)));
      });
    }
    const ls = p.loopStart,
      le = p.loopEnd;
    g.fillStyle = "rgba(255,138,0,.07)";
    g.fillRect(ls * w, 0, (le - ls) * w, h);
    s.peaks.forEach((pk, i) => {
      const x = i * 3,
        a = pk * h * 0.42,
        inL = x / w >= ls && x / w <= le;
      g.fillStyle = inL ? AMBER : "rgba(255,174,58,.35)";
      g.fillRect(x, h / 2 - a, 2, a * 2);
    });
    g.strokeStyle = AMBER;
    g.lineWidth = 1.5;
    g.setLineDash([4, 3]);
    [ls, le].forEach((m) => {
      g.beginPath();
      g.moveTo(m * w + 0.5, 0);
      g.lineTo(m * w + 0.5, h);
      g.stroke();
    });
    g.setLineDash([]);
    if (p.xfade && p.xfade > 0 && le > ls) {
      // The voice blends [loopEnd - xf, loopEnd] with [loopStart - xf, loopStart].
      const xf = Math.min(p.xfade * (le - ls), (le - ls) * 0.5, ls) * w;
      g.fillStyle = "rgba(60,214,221,.16)";
      g.fillRect(le * w - xf, 0, xf, h);
      g.fillRect(ls * w - xf, 0, xf, h);
    }
    if (frame?.playhead === undefined && p.demoMotion === false) return;
    const px = (frame?.playhead ?? ls + ((t * 0.18) % 1) * (le - ls)) * w;
    g.strokeStyle = CREAM;
    glow(g, AMBER, 8);
    g.beginPath();
    g.moveTo(px, 0);
    g.lineTo(px, h);
    g.stroke();
    const spray = p.spray ?? 0.08;
    for (let i = 0; i < 16; i++) {
      const seed = i + Math.floor(t * 6) * 16;
      g.fillStyle = `rgba(60,214,221,${(0.9 - ((t * 6) % 1) * 0.7).toFixed(2)})`;
      glow(g, CYAN, 6);
      g.beginPath();
      g.arc(
        px + (hash(seed) - 0.5) * spray * w * 2,
        h * (0.2 + 0.6 * hash(seed + 99)),
        1.5 + 2.5 * hash(seed + 7),
        0,
        7,
      );
      g.fill();
    }
    noGlow(g);
  },
};

/* ----------------------------------------------------------------- scope */
export const scopeRenderer: Renderer<
  { cutoff?: number; cycles?: number },
  Record<string, never>,
  { samples?: ArrayLike<number> }
> = {
  grid: "linear",
  draw(g, w, h, t, p, _s, frame) {
    g.lineWidth = 2;
    g.strokeStyle = AMBER;
    glow(g, AMBER_DEEP, 10);
    g.beginPath();
    const smp = frame?.samples;
    if (smp && smp.length) {
      for (let x = 0; x <= w; x += 2) {
        const y = h / 2 - (smp[Math.floor((x / w) * (smp.length - 1))] ?? 0) * h * 0.45;
        x ? g.lineTo(x, y) : g.moveTo(x, y);
      }
    } else {
      const cut = (p.cutoff ?? 4) + 2.2 * Math.sin(t * 1.3),
        cyc = p.cycles ?? 2.5;
      for (let x = 0; x <= w; x += 2) {
        const th = ((x / w) * cyc + t * 0.35) * Math.PI * 2;
        let sum = 0;
        for (let k = 1; k <= 12; k++)
          sum += Math.sin(k * th + k * 0.3 * Math.sin(t)) / k / (1 + Math.pow(k / cut, 4));
        const y = h / 2 - sum * h * 0.3;
        x ? g.lineTo(x, y) : g.moveTo(x, y);
      }
    }
    g.stroke();
    noGlow(g);
  },
};

/* ---------------------------------------------------------------- slices */
export interface SliceRegion {
  start: number;
  end: number;
}
export interface SlicesParams {
  /** Absolute peak per bucket, 0..1. Omit for a synthesised drum loop. */
  peaks?: ArrayLike<number>;
  slices: SliceRegion[];
  selected?: number;
  /** Amplitude threshold line (threshold chop mode), 0..1. */
  threshold?: number;
  /** Beat grid lines for BPM chop mode. */
  beats?: number;
  /** Trim window; audio outside is dimmed. */
  trim?: SliceRegion;
  /** Top margin reserved for overlays. */
  top?: number;
}
function demoPeaks(n: number): number[] {
  const hits = [0, 0.125, 0.25, 0.3125, 0.375, 0.5, 0.625, 0.6875, 0.75, 0.875, 0.9375];
  return Array.from({ length: n }, (_, i) => {
    const u = i / n;
    let a = 0.04 + 0.03 * hash(i);
    for (const hp of hits) {
      const d = u - hp;
      if (d >= 0 && d < 0.1)
        a = Math.max(a, (hp * 8) % 2 < 1 ? Math.exp(-d * 38) : 0.7 * Math.exp(-d * 60));
    }
    return Math.min(1, a * (0.8 + 0.2 * hash(i + 3)));
  });
}
export const slicesRenderer: Renderer<SlicesParams, { demo: number[] }, { playhead?: number }> = {
  grid: "none",
  init: () => ({ demo: [] }),
  draw(g, w, h, t, p, s, frame) {
    const top = p.top ?? 0,
      H = h - top,
      mid = top + H / 2;
    const n = Math.floor(w / 2);
    let peaks: ArrayLike<number>;
    if (p.peaks && p.peaks.length) peaks = p.peaks;
    else {
      if (s.demo.length !== n) s.demo = demoPeaks(n);
      peaks = s.demo;
    }
    const at = (u: number) => peaks[Math.min(peaks.length - 1, Math.floor(u * peaks.length))] ?? 0;
    p.slices.forEach((sl, i) => {
      const x0 = sl.start * w,
        x1 = sl.end * w,
        sel = i === p.selected;
      g.fillStyle = sel
        ? "rgba(255,138,0,.16)"
        : i % 2
          ? "rgba(255,174,58,.035)"
          : "rgba(255,174,58,.07)";
      g.fillRect(x0, top, x1 - x0, H);
    });
    if (p.beats && p.beats > 0) {
      g.strokeStyle = "rgba(60,214,221,.18)";
      g.lineWidth = 1;
      for (let b = 1; b < p.beats; b++) {
        const x = Math.round((b / p.beats) * w) + 0.5;
        g.beginPath();
        g.moveTo(x, top);
        g.lineTo(x, h);
        g.stroke();
      }
    }
    for (let x = 0; x < w; x += 2) {
      const u = x / w,
        a = at(u) * (H / 2 - 6);
      const inTrim = !p.trim || (u >= p.trim.start && u <= p.trim.end);
      const inSel =
        p.selected !== undefined &&
        p.slices[p.selected] &&
        u >= p.slices[p.selected].start &&
        u < p.slices[p.selected].end;
      g.fillStyle = !inTrim ? "rgba(255,174,58,.18)" : inSel ? CREAM : AMBER;
      g.fillRect(x, mid - a, 1.5, a * 2);
    }
    if (p.trim) {
      g.fillStyle = "rgba(0,0,0,.45)";
      g.fillRect(0, top, p.trim.start * w, H);
      g.fillRect(p.trim.end * w, top, w - p.trim.end * w, H);
      [p.trim.start, p.trim.end].forEach((m, i) => {
        const x = m * w;
        g.strokeStyle = CYAN;
        g.lineWidth = 2;
        glow(g, CYAN, 8);
        g.beginPath();
        g.moveTo(x, top);
        g.lineTo(x, h);
        g.stroke();
        noGlow(g);
        label(g, i ? "END" : "START", i ? x - 30 : x + 4, h - 6, CYAN, 10);
      });
    }
    if (p.threshold !== undefined) {
      const ty = p.threshold * (H / 2 - 6);
      g.strokeStyle = "rgba(60,214,221,.85)";
      g.setLineDash([5, 4]);
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(0, mid - ty);
      g.lineTo(w, mid - ty);
      g.moveTo(0, mid + ty);
      g.lineTo(w, mid + ty);
      g.stroke();
      g.setLineDash([]);
    }
    p.slices.forEach((sl, i) => {
      const x = Math.round(sl.start * w) + 0.5,
        sel = i === p.selected;
      g.strokeStyle = sel ? CREAM : "rgba(255,174,58,.75)";
      g.lineWidth = sel ? 2 : 1;
      g.beginPath();
      g.moveTo(x, top);
      g.lineTo(x, h);
      g.stroke();
      g.fillStyle = sel ? AMBER : "#1d1307";
      g.strokeStyle = AMBER;
      g.lineWidth = 1;
      g.fillRect(x, top, 22, 15);
      g.strokeRect(x + 0.5, top + 0.5, 21, 14);
      label(g, String(i + 1).padStart(2, "0"), x + 3, top + 11, sel ? "#1a1106" : AMBER, 10);
    });
    const ph = frame?.playhead;
    if (ph !== undefined) {
      g.strokeStyle = CREAM;
      glow(g, AMBER, 8);
      g.beginPath();
      g.moveTo(ph * w, top);
      g.lineTo(ph * w, h);
      g.stroke();
      noGlow(g);
    }
    void t;
  },
};

/* ------------------------------------------------------------------- keys */
export interface KeysParams {
  /** MIDI notes to light up (chord / voicing under inspection). */
  notes?: number[];
  /** Root note emphasised with a footer marker. */
  root?: number;
  /** Lowest MIDI note drawn (default 48 = C3). */
  low?: number;
  /** Number of semitones drawn (default 25 = two octaves + 1). */
  count?: number;
}

const BLACK_PCS = [1, 3, 6, 8, 10];
const isBlackKey = (n: number): boolean => BLACK_PCS.includes(((n % 12) + 12) % 12);

/** Piano keyboard with active-note highlights: chord detail, sketch seeds. */
export const keysRenderer: Renderer<KeysParams, Record<string, never>, { notes?: number[] }> = {
  grid: "none",
  draw(g, w, h, _t, p, _s, frame) {
    const low = p.low ?? 48;
    const count = Math.max(1, p.count ?? 25);
    const active = new Set<number>(frame?.notes ?? p.notes ?? []);

    const whiteAt = new Map<number, number>();
    let whites = 0;
    for (let i = 0; i < count; i += 1) {
      const n = low + i;
      if (!isBlackKey(n)) {
        whiteAt.set(n, whites);
        whites += 1;
      }
    }
    const ww = w / Math.max(1, whites);
    if (whiteAt.size === 0) return;

    for (let i = 0; i < count; i += 1) {
      const n = low + i;
      if (isBlackKey(n)) continue;
      const x = (whiteAt.get(n) ?? 0) * ww;
      const on = active.has(n);
      g.fillStyle = on ? AMBER_DEEP : "#d8d4cc";
      g.fillRect(x, 0, Math.max(1, ww - 1), h);
      g.fillStyle = "rgba(5,5,6,.55)";
      g.fillRect(x + Math.max(1, ww - 1), 0, 1, h);
      if (p.root === n) {
        g.fillStyle = AMBER;
        g.fillRect(x, h - 3, Math.max(1, ww - 1), 3);
      }
    }

    for (let i = 0; i < count; i += 1) {
      const n = low + i;
      if (!isBlackKey(n)) continue;
      const prev = whiteAt.get(n - 1);
      if (prev === undefined) continue;
      const x = prev * ww + ww * 0.68;
      const on = active.has(n);
      g.fillStyle = on ? AMBER_DEEP : "#141416";
      g.fillRect(x, 0, ww * 0.64, h * 0.62);
    }
  },
};
