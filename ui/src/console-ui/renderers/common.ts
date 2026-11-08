// GENERATED from console-ui — edit there, then run `npm run sync`.
// Shared canvas helpers for Console visualisers. Renderers are pure functions of
// (context, size, time, params, persistent state, optional live frame) so they
// can be unit-tested with a stub context and reused outside React (Rompler).

export type Ctx = CanvasRenderingContext2D;

export interface Renderer<P = Record<string, unknown>, S = Record<string, unknown>, F = unknown> {
  /** Grid style drawn before the renderer, or "none". */
  grid?: "log" | "linear" | "none";
  init?: () => S;
  draw: (g: Ctx, w: number, h: number, t: number, p: P, s: S, frame?: F) => void;
}

export const AMBER = "#ffae3a";
export const AMBER_DEEP = "#ff8a00";
export const CYAN = "#3cd6dd";
export const CREAM = "#fff3dc";
export const MONO_FONT = '"Share Tech Mono", monospace';

export const hash = (n: number): number => {
  const s = Math.sin(n * 127.1) * 43758.5453;
  return s - Math.floor(s);
};
export const logX = (hz: number, w: number): number => (Math.log(hz / 20) / Math.log(1000)) * w;
export const xHz = (x: number, w: number): number => 20 * Math.pow(1000, x / w);

export function glow(g: Ctx, c: string, blur: number): void {
  g.shadowColor = c;
  g.shadowBlur = blur;
}
export function noGlow(g: Ctx): void {
  g.shadowBlur = 0;
}

export function polyline(g: Ctx, pts: [number, number][]): void {
  g.beginPath();
  pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
}

/** Minimum canvas text size (the Console B 11 px type floor). */
export const MIN_TEXT_PX = 11;
export function label(
  g: Ctx,
  text: string,
  x: number,
  y: number,
  color = "rgba(255,174,58,.6)",
  size = MIN_TEXT_PX,
): void {
  g.fillStyle = color;
  g.font = `${Math.max(MIN_TEXT_PX, size)}px ${MONO_FONT}`;
  g.fillText(text, x, y);
}

export function drawGrid(g: Ctx, w: number, h: number, kind: "log" | "linear"): void {
  g.strokeStyle = "rgba(255,174,58,.07)";
  g.lineWidth = 1;
  g.beginPath();
  if (kind === "log") {
    [100, 1000, 10000].forEach((f) => {
      const x = logX(f, w) + 0.5;
      g.moveTo(x, 0);
      g.lineTo(x, h);
    });
    [50, 200, 500, 2000, 5000].forEach((f) => {
      const x = logX(f, w) + 0.5;
      g.moveTo(x, h * 0.1);
      g.lineTo(x, h * 0.9);
    });
  } else {
    for (let i = 1; i < 8; i++) {
      const x = Math.round((w * i) / 8) + 0.5;
      g.moveTo(x, 0);
      g.lineTo(x, h);
    }
  }
  for (let i = 1; i < 4; i++) {
    const y = Math.round((h * i) / 4) + 0.5;
    g.moveTo(0, y);
    g.lineTo(w, y);
  }
  g.stroke();
}

/** Magnitude in dB of a 2-pole section, doubled for 24 dB/oct when `poles` = 4. */
export function filterDb(
  f: number,
  fc: number,
  q: number,
  type: "lp" | "hp" | "bp" = "lp",
  poles: 2 | 4 = 4,
): number {
  const r = f / fc;
  const den = Math.pow(1 - r * r, 2) + Math.pow(r / q, 2);
  const num = type === "hp" ? Math.pow(r, 4) : type === "bp" ? Math.pow(r / q, 2) : 1;
  const db = 10 * Math.log10(num / den);
  return poles === 4 ? db * 2 : db;
}
