// GENERATED from console-ui — edit there, then run `npm run sync`.
// Parameter specs: map a normalised 0..1 control position to a real value and
// format it for display. Plugins keep their own parameter ids; the UI only
// needs a spec per control to render and format consistently.

export type Unit =
  | "" | "Hz" | "dB" | "dBTP" | "ms" | "%" | "st" | "ct" | "ratio" | "pan" | "x" | "rate" | "deg" | "bpm" | "s" | "note" | "ticks" | "steps";

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
/** MIDI note name, MPC convention (36 = C1). */
export const noteName = (n: number): string => `${NOTE_NAMES[((Math.round(n) % 12) + 12) % 12]}${Math.floor(Math.round(n) / 12) - 2}`;

export interface ParamSpec {
  min?: number;
  max?: number;
  /** "log" requires min > 0. Ignored when `skew` is set. */
  curve?: "lin" | "log";
  /** JUCE NormalisableRange skew factor (norm = ((v-min)/(max-min))^skew).
   *  Pass the plugin's own skew so knob travel, readouts and resets match the
   *  host exactly; < 1 gives more travel to the low end. */
  skew?: number;
  unit?: Unit;
  decimals?: number;
  /** Discrete choices; overrides min/max/unit. */
  options?: readonly string[];
}

export const clamp01 = (n: number): number => (n < 0 ? 0 : n > 1 ? 1 : n);

export function toValue(norm: number, spec: ParamSpec): number {
  const v = clamp01(norm);
  if (spec.options) return Math.round(v * (spec.options.length - 1));
  const min = spec.min ?? 0;
  const max = spec.max ?? 1;
  if (spec.skew !== undefined && spec.skew > 0 && spec.skew !== 1) {
    return min + (max - min) * (v > 0 ? Math.exp(Math.log(v) / spec.skew) : 0);
  }
  if (spec.curve === "log") {
    if (min <= 0) throw new Error("log curve needs min > 0");
    return min * Math.pow(max / min, v);
  }
  return min + v * (max - min);
}

export function toNorm(value: number, spec: ParamSpec): number {
  if (spec.options) return spec.options.length < 2 ? 0 : clamp01(value / (spec.options.length - 1));
  const min = spec.min ?? 0;
  const max = spec.max ?? 1;
  if (max === min) return 0;
  if (spec.skew !== undefined && spec.skew > 0 && spec.skew !== 1) {
    const p = clamp01((value - min) / (max - min));
    return p > 0 ? clamp01(Math.pow(p, spec.skew)) : 0;
  }
  if (spec.curve === "log") return clamp01(Math.log(value / min) / Math.log(max / min));
  return clamp01((value - min) / (max - min));
}

const signed = (n: number, d: number): string => (n > 0 ? "+" : n < 0 ? "−" : "") + Math.abs(n).toFixed(d);

export function formatValue(norm: number, spec: ParamSpec): string {
  if (spec.options) return spec.options[toValue(norm, spec)] ?? "";
  const x = toValue(norm, spec);
  const d = spec.decimals;
  switch (spec.unit) {
    case "Hz":
      return x >= 1000 ? `${(x / 1000).toFixed(x >= 10000 ? 1 : 2)} kHz` : `${Math.round(x)} Hz`;
    case "dB":
      return `${signed(x, d ?? 1)} dB`;
    case "dBTP":
      return `${signed(x, d ?? 1)} dBTP`;
    case "ms":
      return x >= 1000 ? `${(x / 1000).toFixed(2)} s` : `${x < 10 ? x.toFixed(1) : Math.round(x)} ms`;
    case "s":
      return `${x.toFixed(d ?? 2)} s`;
    case "%":
      return `${Math.round(x)}%`;
    case "st":
      return `${signed(Math.round(x), 0)} st`;
    case "ct":
      return `${signed(Math.round(x), 0)} ct`;
    case "ratio":
      return `${x.toFixed(d ?? 1)}:1`;
    case "pan":
      return Math.abs(x) < 0.5 ? "C" : `${x < 0 ? "L" : "R"}${Math.round(Math.abs(x))}`;
    case "x":
      return `×${x.toFixed(d ?? 2)}`;
    case "rate":
      return `${x.toFixed(d ?? 2)} Hz`;
    case "deg":
      return `${Math.round(x)}°`;
    case "bpm":
      return `${x.toFixed(d ?? 1)} BPM`;
    case "note":
      return `${noteName(x)} · ${Math.round(x)}`;
    default:
      return x.toFixed(d ?? 0) + (spec.unit ? ` ${spec.unit}` : "");
  }
}

/**
 * Parse text typed into a control back into a real value for `spec`.
 * Accepts what formatValue prints and what people type: "1.2 kHz", "1.2k",
 * "−3 dB" (Unicode minus), "250 ms", "1.5 s" on an ms control, "4:1",
 * "×1.5", "L20" / "R35" / "C", and option labels (case-insensitive).
 * Returns NaN when the text cannot be read; the caller should ignore it.
 */
export function parseValue(text: string, spec: ParamSpec): number {
  const raw = text.trim().replace(/[\u2212\u2013]/g, "-").replace(/,/g, ".");
  if (spec.options) {
    const i = spec.options.findIndex((o) => o.toLowerCase() === raw.toLowerCase());
    if (i >= 0) return i;
    const n = Number.parseInt(raw, 10);
    return Number.isNaN(n) ? NaN : Math.max(0, Math.min(spec.options.length - 1, n - 1));
  }
  if (spec.unit === "pan") {
    const m = raw.match(/^([LRC])\s*(\d+(?:\.\d+)?)?$/i);
    if (m) {
      const side = m[1].toUpperCase();
      const amt = m[2] ? Number.parseFloat(m[2]) : 0;
      return side === "C" ? 0 : side === "L" ? -amt : amt;
    }
  }
  if (spec.unit === "note") {
    const n = raw.match(/^([A-G])(#|b)?\s*(-?\d+)$/i);
    if (n) {
      const pc = NOTE_NAMES.indexOf(n[1].toUpperCase()) + (n[2] === "#" ? 1 : n[2] === "b" ? -1 : 0);
      return (Number.parseInt(n[3], 10) + 2) * 12 + pc;
    }
  }
  const m = raw.replace(/^[×x*]/i, "").match(/^([-+]?\d*\.?\d+(?:e[-+]?\d+)?)\s*([a-z%:]*)/i);
  if (!m) return NaN;
  let v = Number.parseFloat(m[1]);
  const suffix = m[2].toLowerCase();
  if (suffix === "k" || suffix === "khz") v *= 1000;
  else if (suffix === "s" && spec.unit === "ms") v *= 1000;
  else if (suffix === "ms" && spec.unit === "s") v /= 1000;
  return v;
}
