// GENERATED from console-ui — edit there, then run `npm run sync`.
import {
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
  type KeyboardEvent,
  type PointerEvent as RPointerEvent,
  type RefObject,
} from "react";
import { Scope } from "../Scope";
import { Key, Led, Lcd } from "../Surface";
import { Meter } from "../Meter";
import { EQ_TYPES, eqResponseDb, type EqBandDef } from "../eqMath";
import { historyRenderer, loudRenderer, spectrumRenderer, vectorRenderer } from "../renderers";
import { system } from "../tokens";
import {
  PAction,
  PChoice,
  PCycle,
  PGroup,
  PKnob,
  PSlider,
  PToggle,
  PanelScope,
  Readout,
  Row,
  useBound,
  useHas,
  useNum,
  useOption,
  useValue,
  type PanelProps,
  type Schema,
} from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";
import { StereoMeter } from "./StereoMeter";

// Overlay Master faceplate (Ozone-style workflow, independent DSP).
//   Screen: the selected module's display | loudness (BS.1770) | tonal balance
//           against your own reference tracks.
//   Strip:  the five reorderable modules in signal order, then Maximizer.
//   Deck:   the selected module's controls.
// Panel keys ARE the plugin's parameter ids. References and module order are
// not parameters: they arrive as extra.references / extra.chainOrder and go
// back through setExtra("chainOrder" | "removeReference").

export const MASTER_MODULES = [
  { key: "eq", name: "EQ", on: "eqOn", color: system.filter },
  { key: "deq", name: "Dynamic EQ", on: "deqOn", color: system.mod },
  { key: "dyn", name: "Dynamics", on: "dynOn", color: system.dynamics },
  { key: "exc", name: "Exciter", on: "excOn", color: system.colour },
  { key: "img", name: "Imager", on: "imgOn", color: system.env },
] as const;
export type MasterModuleKey = (typeof MASTER_MODULES)[number]["key"] | "max";
const DEFAULT_ORDER = MASTER_MODULES.map((m) => m.key) as string[];

/** Same order and values as the plugin's target choice. */
export const MASTER_TARGETS = [
  "-14 LUFS",
  "-16 LUFS",
  "-23 LUFS",
  "-24 LUFS",
  "-11 LUFS",
  "-15 LUFS",
] as const;
export const MASTER_TARGET_NOTES = [
  "Spotify · YouTube · Tidal · Amazon",
  "Apple Music",
  "EBU R128 broadcast",
  "ATSC A/85 broadcast",
  "Spotify Loud",
  "Deezer",
] as const;
export const targetLufs = (label: string): number => {
  const m = label.replace(/−/g, "-").match(/-?\d+(\.\d+)?/);
  return m ? Math.min(0, -Math.abs(parseFloat(m[0]))) : -14;
};

// ---------------------------------------------------------------- schema
const hz = (label: string, def: number, min = 20, max = 20000) => ({
  label,
  def,
  spec: { unit: "Hz" as const, min, max, curve: "log" as const },
});
const dB = (label: string, def: number, min: number, max: number, bipolar = false) => ({
  label,
  def,
  bipolar,
  spec: { unit: "dB" as const, min, max },
});
const ms = (label: string, def: number, min: number, max: number) => ({
  label,
  def,
  spec: { unit: "ms" as const, min, max, curve: "log" as const },
});
const pct = (label: string, def: number, max = 100) => ({
  label,
  def,
  spec: { unit: "%" as const, min: 0, max },
});
const opts = (label: string, def: number, options: readonly string[]) => ({
  label,
  def,
  spec: { options },
});
const BAND_COUNTS = ["1", "2", "3", "4"] as const;
const CHANNELS = ["Stereo", "Mid", "Side"] as const;
const EQ_DEFAULTS = [
  [3, 30, 0],
  [1, 90, 1],
  [0, 250, 1],
  [0, 700, 1],
  [0, 2000, 1],
  [0, 5000, 1],
  [2, 10000, 1],
  [4, 19000, 0],
] as const;
const logNorm = (f: number, lo = 20, hi = 20000) => Math.log(f / lo) / Math.log(hi / lo);

const eqKeys = Object.fromEntries(
  EQ_DEFAULTS.flatMap(([t, f, on], i) => {
    const n = i + 1;
    return [
      [`eq${n}On`, { label: `Band ${n}`, def: on }],
      [`eq${n}Type`, opts("Type", t / 5, EQ_TYPES)],
      [`eq${n}Freq`, hz("Freq", logNorm(f))],
      [`eq${n}Gain`, dB("Gain", 0.5, -18, 18, true)],
      [
        `eq${n}Q`,
        { label: "Q", def: 0.3, spec: { min: 0.1, max: 10, curve: "log" as const, decimals: 2 } },
      ],
      [`eq${n}Slope`, opts("Slope", 0, ["12 dB", "24 dB", "36 dB", "48 dB"])],
      [`eq${n}Ch`, opts("Channel", 0, CHANNELS)],
    ];
  }),
);
const deqKeys = Object.fromEntries(
  [150, 600, 3000, 8000].flatMap((f, i) => {
    const n = i + 1;
    return [
      [`deq${n}On`, { label: `Band ${n}`, def: n === 3 ? 1 : 0 }],
      [`deq${n}Type`, opts("Type", 0, ["Bell", "Low Shelf", "High Shelf"])],
      [`deq${n}Freq`, hz("Freq", logNorm(f))],
      [
        `deq${n}Q`,
        { label: "Q", def: 0.25, spec: { min: 0.3, max: 6, curve: "log" as const, decimals: 2 } },
      ],
      [`deq${n}Thresh`, dB("Threshold", 0.6, -60, 0)],
      [`deq${n}Range`, dB("Range", 1 / 3, -12, 12, true)],
      [`deq${n}Atk`, ms("Attack", 0.4, 0.5, 200)],
      [`deq${n}Rel`, ms("Release", 0.4, 10, 2000)],
      [`deq${n}Ch`, opts("Channel", 0, CHANNELS)],
    ];
  }),
);
const bandKeys = (prefix: string, per: (b: number) => [string, unknown][]) =>
  Object.fromEntries([1, 2, 3, 4].flatMap((b) => per(b).map(([k, v]) => [`${prefix}${b}${k}`, v])));
const dynKeys = bandKeys("dyn", (b) => [
  ["On", { label: `Band ${b}`, def: 1 }],
  ["Thresh", dB("Thresh", 1, -48, 0)],
  [
    "Ratio",
    {
      label: "Ratio",
      def: 0.2,
      spec: { unit: "ratio" as const, min: 1, max: 10, curve: "log" as const },
    },
  ],
  ["Atk", ms("Attack", 0.4, 0.1, 200)],
  ["Rel", ms("Release", 0.4, 10, 2000)],
  ["Gain", dB("Gain", 0.5, -12, 12, true)],
]);
const excKeys = bandKeys("exc", () => [
  ["Drive", dB("Drive", 0.25, 0, 24)],
  ["Mix", pct("Amount", 0)],
  ["Mode", opts("Mode", 0, ["Tape", "Tube", "Diode"])],
]);
const imgKeys = bandKeys("img", () => [["Width", pct("Width", 0.5, 200)]]);

export const masterSchema = {
  target: opts("Target", 0, MASTER_TARGETS),
  outputTrim: dB("Output", 0.5, -12, 12, true),
  compare: { label: "Compare", def: 0 },
  refMonitor: { label: "Reference", def: 0 },
  refMatch: { label: "Level match", def: 1 },
  refIndex: { label: "Reference", def: 0, spec: { min: 0, max: 7 } },
  eqOn: { label: "EQ", def: 0 },
  ...eqKeys,
  deqOn: { label: "Dynamic EQ", def: 0 },
  ...deqKeys,
  dynOn: { label: "Dynamics", def: 0 },
  dynBands: opts("Bands", 1, BAND_COUNTS),
  dynX1: hz("Split 1", 0.4, 40, 600),
  dynX2: hz("Split 2", 0.4, 300, 5000),
  dynX3: hz("Split 3", 0.4, 2000, 16000),
  dynKnee: dB("Knee", 1 / 3, 0, 18),
  dynDetect: opts("Detect", 1, ["Peak", "RMS"]),
  dynMix: pct("Mix", 1),
  ...dynKeys,
  excOn: { label: "Exciter", def: 0 },
  excBands: opts("Bands", 1, BAND_COUNTS),
  excX1: hz("Split 1", 0.45, 40, 600),
  excX2: hz("Split 2", 0.5, 300, 5000),
  excX3: hz("Split 3", 0.4, 2000, 16000),
  ...excKeys,
  imgOn: { label: "Imager", def: 0 },
  imgBands: opts("Bands", 1, BAND_COUNTS),
  imgX1: hz("Split 1", 0.45, 40, 600),
  imgX2: hz("Split 2", 0.5, 300, 5000),
  imgX3: hz("Split 3", 0.4, 2000, 16000),
  imgMono: { label: "Mono below", def: 0, spec: { unit: "Hz" as const, min: 0, max: 300 } },
  ...imgKeys,
  maxOn: { label: "Maximizer", def: 0 },
  maxThresh: dB("Threshold", 1, -24, 0),
  maxCeiling: { label: "Ceiling", def: 2 / 3, spec: { unit: "dBTP" as const, min: -3, max: 0 } },
  maxRelease: ms("Release", 0.45, 10, 500),
  maxOversample: opts("True-peak detection", 1 / 3, ["2×", "4×", "8×", "16×"]),
  maxMode: opts("Character", 0, ["Clean", "Smooth", "Punch"]),
  maxTransient: pct("Transients", 0),
  maxLink: pct("Stereo link", 1),
  ditherOn: { label: "Dither", def: 0 },
  ditherBits: opts("Bits", 0, ["16 bit", "24 bit"]),
  ditherShaped: { label: "Noise shaping", def: 1 },
} as Schema;

// ---------------------------------------------------------------- host data
export interface MasterReference {
  name: string;
  lufs: number;
  lra: number;
  truePeak: number;
  tonal?: number[];
}
export interface MasterExtra {
  references?: MasterReference[];
  missingReferences?: string[];
  referenceError?: string;
  chainOrder?: string[];
}
export interface MasterFrame {
  spectrum?: ArrayLike<number>;
  lufs?: {
    momentary?: number;
    shortTerm?: number;
    integrated?: number;
    lra?: number;
    inputShortTerm?: number;
  };
  truePeakDb?: number;
  meters?: { gr?: number; outL?: number; outR?: number };
  dynGr?: ArrayLike<number>;
  deqGain?: ArrayLike<number>;
  correlation?: number;
  scopeL?: ArrayLike<number>;
  scopeR?: ArrayLike<number>;
  tonal?: ArrayLike<number>;
  compareMatchDb?: number;
  latency?: number;
}
type GetFrame = (() => MasterFrame | undefined) | undefined;

const fmt = (v: number | undefined, d = 1) =>
  v === undefined || !Number.isFinite(v) || v <= -69.9
    ? "—"
    : `${v < 0 ? "−" : v > 0 ? "+" : ""}${Math.abs(v).toFixed(d)}`;
const fmtLufs = (v: number | undefined) =>
  v === undefined || v <= -69.9 ? "—" : `${v < 0 ? "−" : ""}${Math.abs(v).toFixed(1)}`;

/** Text that updates ~10x a second from host data without re-rendering React. */
function Live({
  read,
  size = 13,
  style,
}: {
  read: () => string;
  size?: number;
  style?: CSSProperties;
}) {
  const el = useRef<HTMLSpanElement>(null);
  const fn = useRef(read);
  fn.current = read;
  useEffect(() => {
    // A 10 Hz interval, not a requestAnimationFrame loop, which would wake at
    // the display rate (60-120 Hz) just to skip nine in ten frames.
    const id = window.setInterval(() => {
      if (el.current) el.current.textContent = fn.current();
    }, 100);
    return () => window.clearInterval(id);
  }, []);
  return (
    <span ref={el} className="cs-readout" style={{ fontSize: size, ...style }}>
      {read()}
    </span>
  );
}

// ---------------------------------------------------------------- geometry
const MAIN_W = 700,
  SCREEN_H = 300,
  PAD_T = 14,
  PAD_B = 22;
const PLOT_H = SCREEN_H - 6 - PAD_T - PAD_B;
const xOf = (f: number, w: number) => (Math.log(f / 20) / Math.log(1000)) * w;
const fOf = (x: number, w: number) => 20 * Math.pow(1000, Math.min(1, Math.max(0, x / w)));
const EQ_RANGE = 18;
const yOfDb = (db: number) => PAD_T + (PLOT_H / 2) * (1 - db / EQ_RANGE);
const dbOfY = (y: number) => EQ_RANGE * (1 - (y - PAD_T) / (PLOT_H / 2));
const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

/** Pointer position in the drawing's own units (the host may scale the panel). */
function localPoint(e: RPointerEvent<Element>, el: Element, w: number, h: number) {
  const r = el.getBoundingClientRect();
  const sx = r.width / w || 1,
    sy = r.height / h || 1;
  return { x: (e.clientX - r.left) / sx, y: (e.clientY - r.top) / sy };
}

// ---------------------------------------------------------------- EQ view
function useEqBands(): (EqBandDef & { ch: number })[] {
  const out: (EqBandDef & { ch: number })[] = [];
  for (let n = 1; n <= 8; n++) {
    const has = useHas(`eq${n}On`),
      on = useValue(`eq${n}On`) >= 0.5;
    out.push({
      on: has && on,
      type: useOption(`eq${n}Type`),
      freq: useNum(`eq${n}Freq`),
      gain: useNum(`eq${n}Gain`),
      q: useNum(`eq${n}Q`),
      slope: useOption(`eq${n}Slope`),
      ch: useOption(`eq${n}Ch`),
    });
  }
  return out;
}

function EqNode({
  n,
  band,
  sel,
  onSelect,
  svg,
}: {
  n: number;
  band: EqBandDef & { ch: number };
  sel: boolean;
  onSelect: (n: number) => void;
  svg: RefObject<SVGSVGElement | null>;
}) {
  const freq = useBound(`eq${n}Freq`),
    gain = useBound(`eq${n}Gain`),
    on = useBound(`eq${n}On`);
  const drag = useRef(false);
  if (!freq) return null;
  const gainShape = band.type <= 2;
  const x = xOf(band.freq, MAIN_W - 6);
  const y = yOfDb(gainShape ? band.gain : 0);
  const setFromPoint = (px: number, py: number) => {
    const f = fOf(px, MAIN_W - 6);
    freq.onChange(logNorm(f));
    if (gain && gainShape) gain.onChange(clamp((dbOfY(py) + 18) / 36, 0, 1));
  };
  return (
    <g
      role="slider"
      tabIndex={0}
      aria-label={`EQ band ${n}: ${EQ_TYPES[band.type]} ${Math.round(band.freq)} Hz${gainShape ? `, ${band.gain.toFixed(1)} dB` : ""}${band.on ? "" : ", off"}`}
      aria-valuenow={Math.round(band.freq)}
      aria-valuemin={20}
      aria-valuemax={20000}
      style={{ cursor: "grab", outline: "none" }}
      onPointerDown={(e) => {
        e.stopPropagation();
        onSelect(n);
        (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
        drag.current = true;
        freq.onGesture("start");
        gain?.onGesture("start");
      }}
      onPointerMove={(e) => {
        if (!drag.current || !svg.current) return;
        const p = localPoint(e, svg.current, MAIN_W - 6, SCREEN_H - 6);
        setFromPoint(p.x, p.y);
      }}
      onPointerUp={() => {
        if (drag.current) {
          drag.current = false;
          freq.onGesture("end");
          gain?.onGesture("end");
        }
      }}
      onDoubleClick={() => {
        if (on) {
          on.onGesture("start");
          on.onChange(band.on ? 0 : 1);
          on.onGesture("end");
        }
      }}
      onFocus={() => onSelect(n)}
      onKeyDown={(e: KeyboardEvent<SVGGElement>) => {
        const step = e.shiftKey ? 0.002 : 0.01;
        if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
          freq.onGesture("start");
          freq.onChange(clamp(freq.value + (e.key === "ArrowRight" ? step : -step), 0, 1));
          freq.onGesture("end");
          e.preventDefault();
        } else if ((e.key === "ArrowUp" || e.key === "ArrowDown") && gain && gainShape) {
          gain.onGesture("start");
          gain.onChange(
            clamp(
              gain.value + (e.key === "ArrowUp" ? 1 : -1) * (e.shiftKey ? 0.1 / 36 : 0.5 / 36),
              0,
              1,
            ),
          );
          gain.onGesture("end");
          e.preventDefault();
        }
      }}
    >
      <circle cx={x} cy={y} r={16} fill="transparent" />
      <circle
        cx={x}
        cy={y}
        r={sel ? 11 : 9}
        fill={sel ? "#ffae3a" : "#1a1106"}
        stroke={band.on ? "#ffae3a" : "rgba(255,174,58,.35)"}
        strokeWidth={1.5}
        strokeDasharray={band.ch ? "3 2" : undefined}
      />
      <text
        x={x}
        y={y + 4}
        textAnchor="middle"
        fontSize={11}
        fontFamily='"Share Tech Mono", monospace'
        fill={sel ? "#1a1106" : band.on ? "#ffae3a" : "rgba(255,174,58,.45)"}
        style={{ pointerEvents: "none" }}
      >
        {n}
      </text>
    </g>
  );
}

function EqView({
  getFrame,
  sel,
  onSelect,
}: {
  getFrame: GetFrame;
  sel: number;
  onSelect: (n: number) => void;
}) {
  const bands = useEqBands();
  const svg = useRef<SVGSVGElement>(null);
  const eqOn = useValue("eqOn") >= 0.5;
  const w = MAIN_W - 6;
  const path = (which: 0 | 1) => {
    const use = bands.filter((b) => b.ch === 0 || b.ch === (which === 0 ? 1 : 2));
    let d = "";
    for (let x = 0; x <= w; x += 3)
      d += `${x ? "L" : "M"}${x} ${yOfDb(clamp(eqOn ? eqResponseDb(use, fOf(x, w)) : 0, -EQ_RANGE, EQ_RANGE)).toFixed(1)}`;
    return d;
  };
  const hasMs = bands.some((b) => b.on && b.ch !== 0);
  return (
    <Scope
      renderer={spectrumRenderer}
      params={{ height: 0.8 }}
      width={MAIN_W}
      height={SCREEN_H}
      getFrame={getFrame}
      label="Output spectrum with the EQ curve"
    >
      <svg
        ref={svg}
        width={w}
        height={SCREEN_H - 6}
        style={{ position: "absolute", left: 3, top: 3 }}
        role="group"
        aria-label="EQ bands: drag a node, double-click to switch it on or off"
      >
        {[-12, -6, 6, 12].map((d) => (
          <line
            key={d}
            x1={0}
            x2={w}
            y1={yOfDb(d) + 0.5}
            y2={yOfDb(d) + 0.5}
            stroke="rgba(255,174,58,.08)"
          />
        ))}
        <line x1={0} x2={w} y1={yOfDb(0) + 0.5} y2={yOfDb(0) + 0.5} stroke="rgba(255,174,58,.28)" />
        {[-12, -6, 6, 12].map((d) => (
          <text
            key={d}
            x={4}
            y={yOfDb(d) + 4}
            fontSize={11}
            fill="rgba(255,174,58,.5)"
            fontFamily='"Share Tech Mono", monospace'
          >
            {d > 0 ? `+${d}` : d}
          </text>
        ))}
        {hasMs && (
          <path
            d={path(1)}
            fill="none"
            stroke="#3cd6dd"
            strokeWidth={1.5}
            strokeDasharray="5 4"
            opacity={0.9}
          />
        )}
        <path
          d={path(0)}
          fill="none"
          stroke="#ffae3a"
          strokeWidth={2.2}
          style={{ filter: "drop-shadow(0 0 5px rgba(255,138,0,.7))" }}
          opacity={eqOn ? 1 : 0.4}
        />
        {bands.map((b, i) => (
          <EqNode key={i} n={i + 1} band={b} sel={sel === i + 1} onSelect={onSelect} svg={svg} />
        ))}
        {hasMs && (
          <text
            x={w - 8}
            y={16}
            textAnchor="end"
            fontSize={11}
            fill="#3cd6dd"
            fontFamily='"Share Tech Mono", monospace'
          >
            {"— MID · - - SIDE"}
          </text>
        )}
      </svg>
    </Scope>
  );
}

// ---------------------------------------------------------------- dynamic EQ view
function DynEqView({
  getFrame,
  sel,
  onSelect,
}: {
  getFrame: GetFrame;
  sel: number;
  onSelect: (n: number) => void;
}) {
  const w = MAIN_W - 6;
  const bands = [1, 2, 3, 4].map((n) => ({
    n,
    on: useValue(`deq${n}On`) >= 0.5,
    has: useHas(`deq${n}On`),
    freq: useNum(`deq${n}Freq`),
    range: useNum(`deq${n}Range`),
    thresh: useNum(`deq${n}Thresh`),
  }));
  const bars = useRef<(SVGRectElement | null)[]>([]);
  const svg = useRef<SVGSVGElement>(null);
  const freqB = [
    useBound("deq1Freq"),
    useBound("deq2Freq"),
    useBound("deq3Freq"),
    useBound("deq4Freq"),
  ];
  const rangeB = [
    useBound("deq1Range"),
    useBound("deq2Range"),
    useBound("deq3Range"),
    useBound("deq4Range"),
  ];
  const drag = useRef<number | null>(null);
  useEffect(() => {
    if (!getFrame) return;
    let raf = 0;
    const tick = () => {
      raf = requestAnimationFrame(tick);
      const g = getFrame()?.deqGain;
      bars.current.forEach((el, k) => {
        if (!el) return;
        const v = g?.[k] ?? 0,
          y0 = yOfDb(0),
          y1 = yOfDb(clamp(v, -EQ_RANGE, EQ_RANGE));
        el.setAttribute("y", String(Math.min(y0, y1)));
        el.setAttribute("height", String(Math.max(0.01, Math.abs(y1 - y0))));
      });
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [getFrame]);
  return (
    <Scope
      renderer={spectrumRenderer}
      params={{ height: 0.8 }}
      width={MAIN_W}
      height={SCREEN_H}
      getFrame={getFrame}
      label="Output spectrum with the dynamic EQ bands and their live gain"
    >
      <svg
        ref={svg}
        width={w}
        height={SCREEN_H - 6}
        style={{ position: "absolute", left: 3, top: 3 }}
        role="group"
        aria-label="Dynamic EQ bands: drag sideways for frequency, up and down for range"
        onPointerMove={(e) => {
          const k = drag.current;
          if (k === null || !svg.current) return;
          const p = localPoint(e, svg.current, w, SCREEN_H - 6);
          freqB[k]?.onChange(logNorm(fOf(p.x, w)));
          rangeB[k]?.onChange(clamp((clamp(dbOfY(p.y), -12, 12) + 12) / 24, 0, 1));
        }}
        onPointerUp={() => {
          const k = drag.current;
          if (k !== null) {
            drag.current = null;
            freqB[k]?.onGesture("end");
            rangeB[k]?.onGesture("end");
          }
        }}
      >
        <line x1={0} x2={w} y1={yOfDb(0) + 0.5} y2={yOfDb(0) + 0.5} stroke="rgba(255,174,58,.28)" />
        {bands.map(
          (b, k) =>
            b.has && (
              <g key={b.n} opacity={b.on ? 1 : 0.4}>
                <rect
                  ref={(el) => {
                    bars.current[k] = el;
                  }}
                  x={xOf(b.freq, w) - 9}
                  y={yOfDb(0)}
                  width={18}
                  height={0.01}
                  fill="rgba(60,214,221,.55)"
                />
                <line
                  x1={xOf(b.freq, w)}
                  x2={xOf(b.freq, w)}
                  y1={yOfDb(0)}
                  y2={yOfDb(b.range)}
                  stroke="#3cd6dd"
                  strokeDasharray="3 3"
                />
                <g
                  role="slider"
                  tabIndex={0}
                  aria-label={`Dynamic EQ band ${b.n}: ${Math.round(b.freq)} Hz, range ${b.range.toFixed(1)} dB`}
                  aria-valuenow={Math.round(b.freq)}
                  aria-valuemin={20}
                  aria-valuemax={20000}
                  style={{ cursor: "grab", outline: "none" }}
                  onFocus={() => onSelect(b.n)}
                  onPointerDown={(e) => {
                    onSelect(b.n);
                    svg.current?.setPointerCapture?.(e.pointerId);
                    drag.current = k;
                    freqB[k]?.onGesture("start");
                    rangeB[k]?.onGesture("start");
                  }}
                >
                  <circle cx={xOf(b.freq, w)} cy={yOfDb(b.range)} r={16} fill="transparent" />
                  <circle
                    cx={xOf(b.freq, w)}
                    cy={yOfDb(b.range)}
                    r={sel === b.n ? 11 : 9}
                    fill={sel === b.n ? "#3cd6dd" : "#061a1a"}
                    stroke="#3cd6dd"
                    strokeWidth={1.5}
                  />
                  <text
                    x={xOf(b.freq, w)}
                    y={yOfDb(b.range) + 4}
                    textAnchor="middle"
                    fontSize={11}
                    fontFamily='"Share Tech Mono", monospace'
                    fill={sel === b.n ? "#061a1a" : "#3cd6dd"}
                    style={{ pointerEvents: "none" }}
                  >
                    {b.n}
                  </text>
                </g>
              </g>
            ),
        )}
        <text
          x={8}
          y={16}
          fontSize={11}
          fill="rgba(60,214,221,.85)"
          fontFamily='"Share Tech Mono", monospace'
        >
          NODE = RANGE · BAR = GAIN NOW
        </text>
      </svg>
    </Scope>
  );
}

// ---------------------------------------------------------------- band split view (dynamics / exciter / imager)
function SplitView({
  prefix,
  getFrame,
  width = MAIN_W,
  grKey,
  title,
}: {
  prefix: "dyn" | "exc" | "img";
  getFrame: GetFrame;
  width?: number;
  grKey?: "dynGr";
  title: string;
}) {
  const w = width - 6;
  const count = 1 + useOption(`${prefix}Bands`);
  const xs = [useBound(`${prefix}X1`), useBound(`${prefix}X2`), useBound(`${prefix}X3`)];
  const freqs = [useNum(`${prefix}X1`), useNum(`${prefix}X2`), useNum(`${prefix}X3`)];
  const ranges: [number, number][] = [
    [40, 600],
    [300, 5000],
    [2000, 16000],
  ];
  const svg = useRef<SVGSVGElement>(null);
  const drag = useRef<number | null>(null);
  const grBars = useRef<(SVGRectElement | null)[]>([]);
  useEffect(() => {
    if (!getFrame || !grKey) return;
    let raf = 0;
    const tick = () => {
      raf = requestAnimationFrame(tick);
      const g = getFrame()?.[grKey];
      grBars.current.forEach((el, k) => {
        if (el) el.setAttribute("height", String(clamp((g?.[k] ?? 0) / 12, 0, 1) * (PLOT_H * 0.5)));
      });
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [getFrame, grKey]);
  const edges = [0, ...freqs.slice(0, count - 1).map((f) => xOf(f, w)), w];
  return (
    <Scope
      renderer={spectrumRenderer}
      params={{ height: 0.8 }}
      width={width}
      height={SCREEN_H}
      getFrame={getFrame}
      label={`${title}: output spectrum with the band splits`}
    >
      <svg
        ref={svg}
        width={w}
        height={SCREEN_H - 6}
        style={{ position: "absolute", left: 3, top: 3 }}
        role="group"
        aria-label={`${title} band splits: drag a line`}
        onPointerMove={(e) => {
          const k = drag.current;
          if (k === null || !svg.current) return;
          const f = clamp(
            fOf(localPoint(e, svg.current, w, SCREEN_H - 6).x, w),
            ranges[k][0],
            ranges[k][1],
          );
          xs[k]?.onChange(
            clamp(Math.log(f / ranges[k][0]) / Math.log(ranges[k][1] / ranges[k][0]), 0, 1),
          );
        }}
        onPointerUp={() => {
          const k = drag.current;
          if (k !== null) {
            drag.current = null;
            xs[k]?.onGesture("end");
          }
        }}
      >
        {edges.slice(0, -1).map((x0, b) => (
          <g key={b}>
            <text
              x={(x0 + edges[b + 1]) / 2}
              y={18}
              textAnchor="middle"
              fontSize={12}
              fill="rgba(255,174,58,.8)"
              fontFamily='"Share Tech Mono", monospace'
            >{`BAND ${b + 1}`}</text>
            {grKey && (
              <rect
                ref={(el) => {
                  grBars.current[b] = el;
                }}
                x={(x0 + edges[b + 1]) / 2 - 12}
                y={44}
                width={24}
                height={0}
                fill="rgba(255,90,74,.7)"
              />
            )}
          </g>
        ))}
        {freqs.slice(0, count - 1).map(
          (f, k) =>
            xs[k] && (
              <g
                key={k}
                role="slider"
                tabIndex={0}
                aria-label={`${title} split ${k + 1}: ${Math.round(f)} Hz`}
                aria-valuenow={Math.round(f)}
                aria-valuemin={ranges[k][0]}
                aria-valuemax={ranges[k][1]}
                style={{ cursor: "ew-resize", outline: "none" }}
                onPointerDown={(e) => {
                  svg.current?.setPointerCapture?.(e.pointerId);
                  drag.current = k;
                  xs[k]!.onGesture("start");
                }}
                onKeyDown={(e) => {
                  if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
                  e.preventDefault();
                  const b = xs[k]!;
                  b.onGesture("start");
                  b.onChange(clamp(b.value + (e.key === "ArrowRight" ? 0.01 : -0.01), 0, 1));
                  b.onGesture("end");
                }}
              >
                <rect
                  x={xOf(f, w) - 10}
                  y={0}
                  width={20}
                  height={SCREEN_H - 6}
                  fill="transparent"
                />
                <line
                  x1={xOf(f, w)}
                  x2={xOf(f, w)}
                  y1={24}
                  y2={SCREEN_H - 30}
                  stroke="#3cd6dd"
                  strokeWidth={2}
                />
                <text
                  x={xOf(f, w) + 5}
                  y={SCREEN_H - 34}
                  fontSize={11}
                  fill="#3cd6dd"
                  fontFamily='"Share Tech Mono", monospace'
                >
                  {f >= 1000 ? `${(f / 1000).toFixed(1)}k` : Math.round(f)}
                </text>
              </g>
            ),
        )}
        {grKey && (
          <text
            x={w - 8}
            y={36}
            textAnchor="end"
            fontSize={11}
            fill="rgba(255,90,74,.9)"
            fontFamily='"Share Tech Mono", monospace'
          >
            BAR = GAIN REDUCTION, 12 dB FULL
          </text>
        )}
      </svg>
    </Scope>
  );
}

function ImagerView({ getFrame }: { getFrame: GetFrame }) {
  const vec = useMemo(
    () =>
      getFrame
        ? () => {
            const f = getFrame();
            return { left: f?.scopeL ?? [], right: f?.scopeR ?? [] };
          }
        : undefined,
    [getFrame],
  );
  return (
    <div style={{ display: "flex", gap: 4 }}>
      <div style={{ position: "relative" }}>
        <Scope
          renderer={vectorRenderer}
          params={{ width: 1 }}
          width={300}
          height={SCREEN_H}
          getFrame={vec}
          label="Vectorscope of the output"
        />
        <Overlay style={{ left: 12, bottom: 10 }}>
          {getFrame ? (
            <Live read={() => `CORR ${fmt(getFrame()?.correlation, 2)}`} size={12} />
          ) : (
            <Readout size={12}>CORR —</Readout>
          )}
        </Overlay>
      </div>
      <SplitView prefix="img" getFrame={getFrame} width={MAIN_W - 304} title="Imager" />
    </div>
  );
}

function MaxView({ getFrame }: { getFrame: GetFrame }) {
  const tgt = targetLufs(MASTER_TARGETS[useOption("target")] ?? MASTER_TARGETS[0]);
  const loud = useMemo(
    () => (getFrame ? () => ({ shortTerm: getFrame()?.lufs?.shortTerm ?? -70 }) : undefined),
    [getFrame],
  );
  const gr = useMemo(
    () =>
      getFrame
        ? () => {
            const f = getFrame();
            return {
              gr: clamp((f?.meters?.gr ?? 0) / 12, 0, 1),
              level: clamp(Math.max(f?.meters?.outL ?? 0, f?.meters?.outR ?? 0), 0, 1),
            };
          }
        : undefined,
    [getFrame],
  );
  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
      <Scope
        renderer={loudRenderer}
        params={{ target: tgt }}
        width={MAIN_W}
        height={170}
        getFrame={loud}
        label={`Short-term loudness against the ${tgt} LUFS target`}
      />
      <Scope
        renderer={historyRenderer}
        params={{}}
        width={MAIN_W}
        height={SCREEN_H - 174}
        getFrame={gr}
        label="Maximizer gain reduction (12 dB full scale) over output level"
      />
    </div>
  );
}

// ---------------------------------------------------------------- loudness column
function LoudnessPanel({ getFrame, meters }: { getFrame: GetFrame; meters: PanelProps["meters"] }) {
  const t = useOption("target");
  const tgt = targetLufs(MASTER_TARGETS[t] ?? MASTER_TARGETS[0]);
  const f = () => getFrame?.();
  const noHost = !getFrame;
  const cell = (label: string, read: () => string) => (
    <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline" }}>
      <Readout size={11} style={{ opacity: 0.7, letterSpacing: ".1em" }}>
        {label}
      </Readout>
      {noHost ? <Readout size={14}>—</Readout> : <Live read={read} size={14} />}
    </div>
  );
  return (
    <div
      className="cs-lcd"
      style={{
        width: 280,
        height: SCREEN_H,
        boxSizing: "border-box",
        display: "flex",
        flexDirection: "column",
        gap: 6,
        padding: "10px 14px",
        alignItems: "stretch",
        justifyContent: "flex-start",
      }}
    >
      <Readout size={11} style={{ letterSpacing: ".14em", opacity: 0.75 }}>
        LOUDNESS · BS.1770
      </Readout>
      <div style={{ display: "flex", alignItems: "baseline", gap: 6 }}>
        {noHost ? (
          <Readout size={36}>—</Readout>
        ) : (
          <Live read={() => fmtLufs(f()?.lufs?.integrated)} size={36} />
        )}
        <Readout size={13}>LUFS-I</Readout>
      </div>
      {cell("SHORT-TERM", () => fmtLufs(f()?.lufs?.shortTerm))}
      {cell("MOMENTARY", () => fmtLufs(f()?.lufs?.momentary))}
      {cell("LRA", () => {
        const v = f()?.lufs?.lra;
        return v === undefined ? "—" : `${v.toFixed(1)} LU`;
      })}
      {cell("TRUE PEAK MAX", () => {
        const v = f()?.truePeakDb;
        return v === undefined || v < -119 ? "—" : `${fmt(v)} dBTP`;
      })}
      {cell("TO TARGET", () => {
        const i = f()?.lufs?.integrated;
        return i === undefined || i <= -69.9 ? "—" : `${fmt(tgt - i)} dB`;
      })}
      <Row align="center" gap={8}>
        <PCycle k="target" style={{ flexGrow: 1, minWidth: 0 }} />
        <PAction name="resetLoudness" style={{ minHeight: 36 }}>
          Reset
        </PAction>
      </Row>
      <Readout size={11} style={{ opacity: 0.7 }}>
        {MASTER_TARGET_NOTES[t] ?? ""}
      </Readout>
      <div style={{ display: "flex", flexDirection: "column", gap: 4, marginTop: "auto" }}>
        {meters?.in && (
          <Row align="center" gap={8}>
            <Readout size={11} style={{ width: 28, opacity: 0.7 }}>
              IN
            </Readout>
            <div style={{ flexGrow: 1 }}>
              <StereoMeter read={meters.in} segments={16} label="Input level" />
            </div>
          </Row>
        )}
        {meters?.out && (
          <Row align="center" gap={8}>
            <Readout size={11} style={{ width: 28, opacity: 0.7 }}>
              OUT
            </Readout>
            <div style={{ flexGrow: 1 }}>
              <StereoMeter read={meters.out} segments={16} label="Output level" />
            </div>
          </Row>
        )}
      </div>
    </div>
  );
}

// ---------------------------------------------------------------- tonal balance + references
const TB_W = 292 - 24,
  TB_H = 86;
function tonalPath(v: ArrayLike<number>) {
  let d = "";
  for (let i = 0; i < v.length; i++) {
    const x = (i / (v.length - 1)) * TB_W,
      y = TB_H / 2 - (clamp(v[i], -18, 18) * (TB_H / 2 - 4)) / 18;
    d += `${i ? "L" : "M"}${x.toFixed(1)} ${y.toFixed(1)}`;
  }
  return d;
}

function TonalPanel({
  getFrame,
  extra,
  setExtra,
}: {
  getFrame: GetFrame;
  extra?: MasterExtra;
  setExtra?: PanelProps["setExtra"];
}) {
  const refs = extra?.references ?? [];
  const missing = extra?.missingReferences ?? [];
  const idx = useBound("refIndex");
  const active = idx ? Math.round(useNum("refIndex")) : 0;
  const mix = useRef<SVGPathElement>(null);
  const ref = refs[active];
  useEffect(() => {
    if (!getFrame) return;
    let raf = 0,
      last = 0;
    const tick = (ts: number) => {
      raf = requestAnimationFrame(tick);
      if (ts - last < 250) return;
      last = ts;
      const t = getFrame()?.tonal;
      mix.current?.setAttribute("d", t && t.length ? tonalPath(t) : "");
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [getFrame]);
  const pick = (i: number) => {
    if (!idx) return;
    idx.onGesture("start");
    idx.onChange(i / 7);
    idx.onGesture("end");
  };
  return (
    <div
      className="cs-lcd"
      style={{
        flexGrow: 1,
        minWidth: 0,
        height: SCREEN_H,
        boxSizing: "border-box",
        display: "flex",
        flexDirection: "column",
        gap: 6,
        padding: "10px 12px",
        alignItems: "stretch",
        justifyContent: "flex-start",
      }}
    >
      <Row align="center" gap={8}>
        <Readout size={11} style={{ letterSpacing: ".14em", opacity: 0.75, flexGrow: 1 }}>
          TONAL BALANCE
        </Readout>
        <PToggle k="refMonitor" style={{ minHeight: 34, padding: "0 10px" }}>
          Listen
        </PToggle>
        <PToggle k="refMatch" style={{ minHeight: 34, padding: "0 10px" }}>
          Match
        </PToggle>
      </Row>
      <svg
        width={TB_W}
        height={TB_H}
        style={{ flexShrink: 0 }}
        role="img"
        aria-label={
          ref
            ? `Tonal balance of the master against ${ref.name}`
            : "Tonal balance of the master (no reference loaded)"
        }
      >
        <line x1={0} x2={TB_W} y1={TB_H / 2} y2={TB_H / 2} stroke="rgba(255,174,58,.25)" />
        {ref?.tonal && (
          <path
            d={tonalPath(ref.tonal)}
            fill="none"
            stroke="#3cd6dd"
            strokeWidth={1.5}
            strokeDasharray="4 3"
          />
        )}
        <path ref={mix} d="" fill="none" stroke="#ffae3a" strokeWidth={2} />
        <text
          x={2}
          y={TB_H - 3}
          fontSize={11}
          fill="rgba(255,174,58,.5)"
          fontFamily='"Share Tech Mono", monospace'
        >
          20
        </text>
        <text
          x={TB_W - 2}
          y={TB_H - 3}
          textAnchor="end"
          fontSize={11}
          fill="rgba(255,174,58,.5)"
          fontFamily='"Share Tech Mono", monospace'
        >
          20k
        </text>
      </svg>
      <Readout size={11} style={{ opacity: 0.7 }}>
        {ref ? "— MASTER (10 s AVG) · - - REFERENCE" : "— MASTER (10 s AVG) · LOAD A REFERENCE"}
      </Readout>
      <div
        aria-label="Reference tracks"
        style={{
          display: "flex",
          flexDirection: "column",
          gap: 6,
          overflowY: "auto",
          overflowX: "hidden",
          flexGrow: 1,
          minHeight: 0,
        }}
      >
        {refs.map((r, i) => (
          <div key={`${r.name}-${i}`} style={{ display: "flex", gap: 8, flexShrink: 0 }}>
            <Key
              pressed={i === active}
              onClick={() => pick(i)}
              label={`Reference ${r.name}, ${fmtLufs(r.lufs)} LUFS`}
              style={{
                flexGrow: 1,
                minWidth: 0,
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                gap: 8,
                fontSize: 11,
                padding: "0 8px",
                minHeight: 36,
              }}
            >
              <span style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
                {r.name}
              </span>
              <span>{fmtLufs(r.lufs)}</span>
            </Key>
            {setExtra && (
              <Key
                label={`Remove ${r.name}`}
                onClick={() => setExtra("removeReference", i)}
                style={{ padding: 0 }}
              >
                ✕
              </Key>
            )}
          </div>
        ))}
        {missing.map((m, i) => (
          <div
            key={`missing-${m}`}
            style={{ display: "flex", gap: 8, alignItems: "center", flexShrink: 0 }}
          >
            <Readout
              size={11}
              style={{
                flexGrow: 1,
                minWidth: 0,
                overflow: "hidden",
                textOverflow: "ellipsis",
                whiteSpace: "nowrap",
                color: "#ff5a4a",
              }}
            >
              MISSING {m}
            </Readout>
            {setExtra && (
              <Key
                label={`Forget ${m}`}
                onClick={() => setExtra("removeReference", refs.length + i)}
                style={{ padding: 0 }}
              >
                ✕
              </Key>
            )}
          </div>
        ))}
      </div>
      <PAction name="loadReference" style={{ minHeight: 36 }}>
        Load reference…
      </PAction>
    </div>
  );
}

// ---------------------------------------------------------------- module strip
function ModuleStrip({
  order,
  sel,
  setSel,
  setOrder,
}: {
  order: string[];
  sel: MasterModuleKey;
  setSel: (k: MasterModuleKey) => void;
  setOrder?: (o: string[]) => void;
}) {
  const on = Object.fromEntries(MASTER_MODULES.map((m) => [m.key, useValue(m.on) >= 0.5]));
  const has = Object.fromEntries(MASTER_MODULES.map((m) => [m.key, useHas(m.on)]));
  const maxOn = useValue("maxOn") >= 0.5,
    hasMax = useHas("maxOn");
  const i = order.indexOf(sel);
  const move = (d: -1 | 1) => {
    if (!setOrder || i < 0) return;
    const j = i + d;
    if (j < 0 || j >= order.length) return;
    const o = [...order];
    [o[i], o[j]] = [o[j], o[i]];
    setOrder(o);
  };
  const byKey = Object.fromEntries(MASTER_MODULES.map((m) => [m.key, m]));
  return (
    <div
      role="tablist"
      aria-label="Modules in signal order"
      style={{ display: "flex", gap: 8, alignItems: "center", height: 44 }}
    >
      <Readout size={11} style={{ opacity: 0.7, letterSpacing: ".12em" }}>
        IN →
      </Readout>
      {order.map((k) => {
        const m = byKey[k];
        if (!m || !has[k]) return null;
        return (
          <button
            key={k}
            type="button"
            role="tab"
            aria-selected={sel === k}
            className={`cs-key${sel === k ? " cs-on" : ""}`}
            onClick={() => setSel(k as MasterModuleKey)}
            style={{
              minHeight: 40,
              display: "flex",
              alignItems: "center",
              gap: 8,
              padding: "0 12px",
            }}
            aria-label={`${m.name}, ${on[k] ? "on" : "off"}`}
          >
            <span
              style={{
                width: 10,
                height: 10,
                borderRadius: 3,
                background: on[k] ? m.color : "transparent",
                border: `2px solid ${m.color}`,
                boxShadow: on[k] ? `0 0 8px ${m.color}` : "none",
              }}
            />
            {m.name}
          </button>
        );
      })}
      {hasMax && (
        <button
          type="button"
          role="tab"
          aria-selected={sel === "max"}
          className={`cs-key${sel === "max" ? " cs-on" : ""}`}
          onClick={() => setSel("max")}
          style={{
            minHeight: 40,
            display: "flex",
            alignItems: "center",
            gap: 8,
            padding: "0 12px",
          }}
          aria-label={`Maximizer and output, ${maxOn ? "on" : "off"}`}
        >
          <span
            style={{
              width: 10,
              height: 10,
              borderRadius: 3,
              background: maxOn ? system.out : "transparent",
              border: `2px solid ${system.out}`,
            }}
          />
          Maximizer · Out
        </button>
      )}
      <span style={{ flexGrow: 1 }} />
      {setOrder && sel !== "max" && (
        <>
          <Readout size={11} style={{ opacity: 0.7 }}>
            MOVE
          </Readout>
          <Key label="Move the selected module earlier" disabled={i <= 0} onClick={() => move(-1)}>
            ◂
          </Key>
          <Key
            label="Move the selected module later"
            disabled={i < 0 || i >= order.length - 1}
            onClick={() => move(1)}
          >
            ▸
          </Key>
        </>
      )}
    </div>
  );
}

// ---------------------------------------------------------------- decks
const DECK: CSSProperties = { display: "flex", gap: 12, width: "100%", alignItems: "stretch" };

function EqDeck({ sel, setSel }: { sel: number; setSel: (n: number) => void }) {
  const type = useOption(`eq${sel}Type`);
  const gainShape = type <= 2,
    hasSlope = type >= 1 && type <= 4,
    hasQ = type === 0 || type === 5;
  const bandOn = [1, 2, 3, 4, 5, 6, 7, 8].map((n) => useValue(`eq${n}On`) >= 0.5);
  return (
    <div style={DECK}>
      <PGroup
        title="EQ"
        chip={system.filter}
        keys={["eq1Freq"]}
        enable="eqOn"
        style={{ flexGrow: 1, minWidth: 0 }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          <div role="tablist" aria-label="EQ bands" style={{ display: "flex", gap: 8 }}>
            {[1, 2, 3, 4, 5, 6, 7, 8].map((n) => (
              <button
                key={n}
                type="button"
                role="tab"
                aria-selected={sel === n}
                className={`cs-key${sel === n ? " cs-on" : ""}`}
                onClick={() => setSel(n)}
                style={{
                  minHeight: 36,
                  flexGrow: 1,
                  display: "flex",
                  gap: 6,
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <Led state={bandOn[n - 1] ? "on" : "off"} />
                {n}
              </button>
            ))}
          </div>
          <Row align="center" justify="flex-start" gap={16}>
            <PToggle k={`eq${sel}On`} style={{ minHeight: 40, padding: "0 12px" }}>
              {bandOn[sel - 1] ? "Band on" : "Band off"}
            </PToggle>
            <PCycle k={`eq${sel}Type`} style={{ minWidth: 120 }} />
            <PChoice k={`eq${sel}Ch`} variant="seg" style={{ width: 240 }} />
            {hasSlope && <PChoice k={`eq${sel}Slope`} variant="seg" style={{ width: 300 }} />}
          </Row>
          <Row justify="flex-start" gap={24}>
            <PKnob k={`eq${sel}Freq`} size="xl" />
            {gainShape && <PKnob k={`eq${sel}Gain`} size="xl" />}
            {hasQ && <PKnob k={`eq${sel}Q`} size="l" />}
          </Row>
        </div>
      </PGroup>
    </div>
  );
}

function DynEqDeck({
  sel,
  setSel,
  getFrame,
}: {
  sel: number;
  setSel: (n: number) => void;
  getFrame: GetFrame;
}) {
  const bandOn = [1, 2, 3, 4].map((n) => useValue(`deq${n}On`) >= 0.5);
  return (
    <div style={DECK}>
      <PGroup
        title="Dynamic EQ"
        chip={system.mod}
        keys={["deq1Freq"]}
        enable="deqOn"
        style={{ flexGrow: 1, minWidth: 0 }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          <Row align="center" justify="flex-start" gap={8}>
            <div
              role="tablist"
              aria-label="Dynamic EQ bands"
              style={{ display: "flex", gap: 8, width: 360 }}
            >
              {[1, 2, 3, 4].map((n) => (
                <button
                  key={n}
                  type="button"
                  role="tab"
                  aria-selected={sel === n}
                  className={`cs-key${sel === n ? " cs-on" : ""}`}
                  onClick={() => setSel(n)}
                  style={{
                    minHeight: 36,
                    flexGrow: 1,
                    display: "flex",
                    gap: 6,
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <Led state={bandOn[n - 1] ? "on" : "off"} />
                  {n}
                </button>
              ))}
            </div>
            <PToggle k={`deq${sel}On`} style={{ minHeight: 36, padding: "0 12px" }}>
              {bandOn[sel - 1] ? "Band on" : "Band off"}
            </PToggle>
            <PChoice k={`deq${sel}Type`} variant="seg" style={{ width: 300 }} />
            <PChoice k={`deq${sel}Ch`} variant="seg" style={{ width: 230 }} />
          </Row>
          <Row justify="flex-start" gap={22}>
            <PKnob k={`deq${sel}Freq`} size="xl" />
            <PKnob k={`deq${sel}Thresh`} size="xl" />
            <PKnob k={`deq${sel}Range`} size="xl" />
            <PKnob k={`deq${sel}Q`} size="l" />
            <PKnob k={`deq${sel}Atk`} size="l" />
            <PKnob k={`deq${sel}Rel`} size="l" />
            <div style={{ display: "flex", flexDirection: "column", gap: 4, alignSelf: "center" }}>
              <Readout size={11} style={{ opacity: 0.7 }}>
                GAIN NOW
              </Readout>
              {getFrame ? (
                <Live read={() => `${fmt(getFrame()?.deqGain?.[sel - 1] ?? 0)} dB`} size={16} />
              ) : (
                <Readout size={16}>—</Readout>
              )}
            </div>
          </Row>
        </div>
      </PGroup>
    </div>
  );
}

function SplitControls({ prefix }: { prefix: "dyn" | "exc" | "img" }) {
  const count = 1 + useOption(`${prefix}Bands`);
  return (
    <Row align="center" justify="flex-start" gap={14}>
      <Readout size={11} style={{ opacity: 0.7 }}>
        BANDS
      </Readout>
      <PChoice k={`${prefix}Bands`} variant="seg" style={{ width: 200 }} />
      {count > 1 && <PSlider k={`${prefix}X1`} width={170} accent="#3cd6dd" />}
      {count > 2 && <PSlider k={`${prefix}X2`} width={170} accent="#3cd6dd" />}
      {count > 3 && <PSlider k={`${prefix}X3`} width={170} accent="#3cd6dd" />}
    </Row>
  );
}

function DynDeck({ getFrame }: { getFrame: GetFrame }) {
  const count = 1 + useOption("dynBands");
  return (
    <div style={{ ...DECK, flexDirection: "column", gap: 8 }}>
      <Row align="center" justify="flex-start" gap={14}>
        <PToggle k="dynOn" style={{ minHeight: 36, padding: "0 14px" }}>
          {useValue("dynOn") >= 0.5 ? "Dynamics on" : "Dynamics off"}
        </PToggle>
        <SplitControls prefix="dyn" />
        <span style={{ flexGrow: 1 }} />
        <PChoice k="dynDetect" variant="seg" style={{ width: 150 }} />
      </Row>
      <div style={{ display: "flex", gap: 10 }}>
        {[1, 2, 3, 4].slice(0, count).map((b) => (
          <PGroup
            key={b}
            title={`Band ${b}`}
            chip={system.dynamics}
            keys={[`dyn${b}Thresh`]}
            enable={`dyn${b}On`}
            style={{ flex: "1 1 0", minWidth: 0 }}
            extra={
              getFrame ? (
                <Live read={() => `GR ${(getFrame()?.dynGr?.[b - 1] ?? 0).toFixed(1)}`} size={11} />
              ) : undefined
            }
          >
            <Row justify="space-around" gap={6}>
              <PKnob k={`dyn${b}Thresh`} size="l" />
              <PKnob k={`dyn${b}Ratio`} size="l" />
            </Row>
            <Row justify="space-around" gap={6}>
              <PKnob k={`dyn${b}Atk`} size="m" />
              <PKnob k={`dyn${b}Rel`} size="m" />
              <PKnob k={`dyn${b}Gain`} size="m" />
            </Row>
          </PGroup>
        ))}
        <PGroup
          title="Global"
          chip={system.out}
          keys={["dynMix", "dynKnee"]}
          style={{ width: 150, boxSizing: "border-box", alignItems: "center" }}
        >
          <PKnob k="dynMix" size="l" />
          <PKnob k="dynKnee" size="m" />
        </PGroup>
      </div>
    </div>
  );
}

function ExcDeck() {
  const count = 1 + useOption("excBands");
  return (
    <div style={{ ...DECK, flexDirection: "column", gap: 8 }}>
      <Row align="center" justify="flex-start" gap={14}>
        <PToggle k="excOn" style={{ minHeight: 36, padding: "0 14px" }}>
          {useValue("excOn") >= 0.5 ? "Exciter on" : "Exciter off"}
        </PToggle>
        <SplitControls prefix="exc" />
      </Row>
      <div style={{ display: "flex", gap: 10 }}>
        {[1, 2, 3, 4].slice(0, count).map((b) => (
          <PGroup
            key={b}
            title={`Band ${b}`}
            chip={system.colour}
            keys={[`exc${b}Drive`]}
            style={{ flex: "1 1 0", minWidth: 0 }}
          >
            <PChoice k={`exc${b}Mode`} variant="seg" />
            <Row justify="space-around">
              <PKnob k={`exc${b}Mix`} size="xl" />
              <PKnob k={`exc${b}Drive`} size="l" />
            </Row>
          </PGroup>
        ))}
      </div>
    </div>
  );
}

function ImgDeck() {
  const count = 1 + useOption("imgBands");
  return (
    <div style={{ ...DECK, flexDirection: "column", gap: 8 }}>
      <Row align="center" justify="flex-start" gap={14}>
        <PToggle k="imgOn" style={{ minHeight: 36, padding: "0 14px" }}>
          {useValue("imgOn") >= 0.5 ? "Imager on" : "Imager off"}
        </PToggle>
        <SplitControls prefix="img" />
      </Row>
      <div style={{ display: "flex", gap: 10 }}>
        {[1, 2, 3, 4].slice(0, count).map((b) => (
          <PGroup
            key={b}
            title={`Band ${b}`}
            chip={system.env}
            keys={[`img${b}Width`]}
            style={{ flex: "1 1 0", minWidth: 0, alignItems: "center" }}
          >
            <PKnob k={`img${b}Width`} size="xl" />
          </PGroup>
        ))}
        <PGroup
          title="Low end"
          chip={system.out}
          keys={["imgMono"]}
          style={{ width: 170, boxSizing: "border-box", alignItems: "center" }}
        >
          <PKnob k="imgMono" size="xl" />
        </PGroup>
      </div>
    </div>
  );
}

function MaxDeck({ getFrame }: { getFrame: GetFrame }) {
  const grRead = useMemo(() => () => clamp((getFrame?.()?.meters?.gr ?? 0) / 12, 0, 1), [getFrame]);
  return (
    <div style={DECK}>
      <PGroup
        title="Maximizer"
        chip={system.filter}
        keys={["maxThresh", "maxCeiling"]}
        enable="maxOn"
        style={{ flexGrow: 1, minWidth: 0 }}
        extra={
          getFrame ? (
            <Live read={() => `GR ${(getFrame()?.meters?.gr ?? 0).toFixed(1)} dB`} size={12} />
          ) : undefined
        }
      >
        <div style={{ display: "flex", gap: 22, alignItems: "flex-start" }}>
          <PKnob k="maxThresh" size="xxl" showValue />
          <PKnob k="maxCeiling" size="xl" />
          <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
            <PChoice k="maxMode" variant="seg" style={{ width: 300 }} />
            <Row justify="flex-start" gap={18}>
              <PKnob k="maxRelease" size="l" />
              <PKnob k="maxTransient" size="l" />
              <PKnob k="maxLink" size="l" />
            </Row>
          </div>
          <div style={{ display: "flex", flexDirection: "column", gap: 8, width: 220 }}>
            <Readout size={11} style={{ opacity: 0.7 }}>
              TRUE-PEAK DETECTION
            </Readout>
            <PChoice k="maxOversample" variant="seg" />
            {getFrame && (
              <>
                <Readout size={11} style={{ opacity: 0.7 }}>
                  GAIN REDUCTION
                </Readout>
                <Meter read={grRead} kind="gr" segments={16} label="Maximizer gain reduction" />
              </>
            )}
          </div>
        </div>
      </PGroup>
      <PGroup
        title="Output"
        chip={system.out}
        keys={["outputTrim", "ditherOn"]}
        style={{ width: 230, boxSizing: "border-box" }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 10, alignItems: "stretch" }}>
          <Row justify="center">
            <PKnob k="outputTrim" size="l" />
          </Row>
          <Row align="center" gap={8}>
            <PToggle k="ditherOn" style={{ minHeight: 36, padding: "0 10px" }} />
            <PToggle k="ditherShaped" style={{ minHeight: 36, padding: "0 10px" }}>
              Shaped
            </PToggle>
          </Row>
          <PChoice k="ditherBits" variant="seg" />
        </div>
      </PGroup>
    </div>
  );
}

// ---------------------------------------------------------------- panel
export interface MasterPanelProps extends PanelProps {
  initialModule?: MasterModuleKey;
}

export function MasterPanel({
  store,
  map,
  specs,
  presetName,
  onPresetStep,
  meters,
  getFrame,
  extra,
  setExtra,
  onUndo,
  onRedo,
  initialModule,
}: MasterPanelProps) {
  const [mod, setMod] = useState<MasterModuleKey>(initialModule ?? "eq");
  const [eqSel, setEqSel] = useState(3);
  const [deqSel, setDeqSel] = useState(3);
  const ex = extra as MasterExtra | undefined;
  const [localOrder, setLocalOrder] = useState<string[]>(DEFAULT_ORDER);
  const order = setExtra ? (ex?.chainOrder?.length ? ex.chainOrder : DEFAULT_ORDER) : localOrder;
  const setOrder = setExtra ? (o: string[]) => setExtra("chainOrder", o) : setLocalOrder;
  const gf = getFrame as GetFrame;
  const onKey = (e: KeyboardEvent<HTMLDivElement>) => {
    if (!(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== "z") return;
    const fn = e.shiftKey ? onRedo : onUndo;
    if (fn) {
      e.preventDefault();
      fn();
    }
  };

  const main =
    mod === "eq" ? (
      <EqView getFrame={gf} sel={eqSel} onSelect={setEqSel} />
    ) : mod === "deq" ? (
      <DynEqView getFrame={gf} sel={deqSel} onSelect={setDeqSel} />
    ) : mod === "dyn" ? (
      <SplitView prefix="dyn" getFrame={gf} grKey="dynGr" title="Dynamics" />
    ) : mod === "exc" ? (
      <SplitView prefix="exc" getFrame={gf} title="Exciter" />
    ) : mod === "img" ? (
      <ImagerView getFrame={gf} />
    ) : (
      <MaxView getFrame={gf} />
    );
  const deck =
    mod === "eq" ? (
      <EqDeck sel={eqSel} setSel={setEqSel} />
    ) : mod === "deq" ? (
      <DynEqDeck sel={deqSel} setSel={setDeqSel} getFrame={gf} />
    ) : mod === "dyn" ? (
      <DynDeck getFrame={gf} />
    ) : mod === "exc" ? (
      <ExcDeck />
    ) : mod === "img" ? (
      <ImgDeck />
    ) : (
      <MaxDeck getFrame={gf} />
    );

  const header = (
    <>
      <PToggle k="compare" style={{ minHeight: 40, padding: "0 12px" }}>
        Compare
      </PToggle>
      {gf && (
        <Lcd style={{ height: 40, fontSize: 12, minWidth: 116, justifyContent: "center" }}>
          <Live read={() => `MATCH ${fmt(gf()?.compareMatchDb)} dB`} size={12} />
        </Lcd>
      )}
      {onUndo && (
        <Key label="Undo" onClick={onUndo} style={{ minHeight: 40 }}>
          Undo
        </Key>
      )}
      {onRedo && (
        <Key label="Redo" onClick={onRedo} style={{ minHeight: 40 }}>
          Redo
        </Key>
      )}
    </>
  );

  return (
    <PanelScope schema={masterSchema} specs={specs} store={store} map={map}>
      <div onKeyDown={onKey}>
        <PluginFrame
          name="MASTER"
          tag="OVERLAY FX"
          subtitle="Your modules, your order → maximizer · measured to BS.1770"
          presetName={presetName}
          onPresetStep={onPresetStep}
          headerRight={header}
          screenHeight={SCREEN_H}
          resetKeys={[mod]}
          screen={
            <div style={{ display: "flex", gap: 6, height: SCREEN_H }}>
              {main}
              <LoudnessPanel getFrame={gf} meters={meters} />
              <TonalPanel getFrame={gf} extra={ex} setExtra={setExtra} />
            </div>
          }
        >
          <div style={{ display: "flex", flexDirection: "column", gap: 10, width: "100%" }}>
            <ModuleStrip order={order} sel={mod} setSel={setMod} setOrder={setOrder} />
            {deck}
          </div>
        </PluginFrame>
      </div>
    </PanelScope>
  );
}
