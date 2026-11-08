// GENERATED from console-ui — edit there, then run `npm run sync`.
import {
  useEffect,
  useMemo,
  useRef,
  useState,
  type KeyboardEvent,
  type MouseEvent as RMouseEvent,
  type PointerEvent as RPointerEvent,
} from "react";
import { Key, Led } from "../Surface";
import { system } from "../tokens";
import {
  CURVE_SHAPES,
  CURVE_SHAPE_LABELS,
  WEIGHT_NAMES,
  addPoint,
  flipH,
  flipV,
  movePoint,
  nudgeCurve,
  parseCurve,
  randomizeY,
  removePoint,
  repeatCurve,
  sampleCurve,
  serializeCurve,
  setWeight,
  type CurvePoint,
  type CurveShape,
  type Weight,
} from "../curve";
import {
  PAction,
  PChoice,
  PCycle,
  PGroup,
  PKnob,
  PSlider,
  PanelScope,
  Readout,
  Row,
  useBound,
  useHas,
  useLabel,
  useOption,
  useValue,
  type PanelProps,
  type Schema,
} from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";
import { StereoMeter } from "./StereoMeter";

// Overlay Modulator faceplate. Two pages:
//   SHAPERS - six drawn-curve shapers (ShaperBox-style): Volume, Filter, Pan,
//             Width, Crush, Drive. Each runs its own curve at its own tempo
//             division; the plugin runs them before the classic effects.
//   CLASSIC - the original LFO effects (ring, filter mod, chorus, flanger,
//             phaser, tremolo, auto-pan, rotary), unchanged.
// Panel keys ARE the plugin's parameter ids (identity map). Curves are not
// parameters: they arrive as `extra.curves` (shaper name -> CV1 text) and go
// back through setExtra("curve", { shaper, text }). The playhead, value dot and
// level bars come only from the host frame; without one nothing moves.

export const SHAPERS = [
  { id: "volume", prefix: "shVol", name: "Volume", color: "#ece9e2", depth: 1 },
  { id: "filter", prefix: "shFlt", name: "Filter", color: "#ff8a00", depth: 0.8 },
  { id: "pan", prefix: "shPan", name: "Pan", color: "#b8e07a", depth: 0.8 },
  { id: "width", prefix: "shWid", name: "Width", color: "#3cd6dd", depth: 0.8 },
  { id: "crush", prefix: "shCrs", name: "Crush", color: "#ff5a4a", depth: 0.7 },
  { id: "drive", prefix: "shDrv", name: "Drive", color: "#c79bff", depth: 0.7 },
] as const;
export type ShaperDef = (typeof SHAPERS)[number];

/** Same list and order as the plugin's shaper::rateNames(). */
export const SHAPER_RATES = [
  "8 bars",
  "4 bars",
  "2 bars",
  "1 bar",
  "1/2D",
  "1/2",
  "1/2T",
  "1/4D",
  "1/4",
  "1/4T",
  "1/8D",
  "1/8",
  "1/8T",
  "1/16D",
  "1/16",
  "1/16T",
  "1/32D",
  "1/32",
  "1/32T",
  "1/64",
] as const;
/** Same starting curves as the plugin's shaper::defaultCurveText(). */
export const SHAPER_DEFAULT_CURVES: Record<ShaperDef["id"], string> = {
  volume: "CV1|3|0,0,2|0.45,1,0|0.97,1,0",
  filter: "CV1|2|0,0,2|0.5,1,2",
  pan: "CV1|4|0,0.5,2|0.25,1,2|0.5,0.5,2|0.75,0,2",
  width: "CV1|4|0,0.5,2|0.25,1,2|0.5,0.5,2|0.75,0,2",
  crush: "CV1|4|0,0,0|0.4999,0,0|0.5,1,0|0.9999,1,0",
  drive: "CV1|2|0,0,0|0.99,1,0",
};
/** What the curve means for each shaper (axis captions). */
const CURVE_MEANING: Record<ShaperDef["id"], [string, string]> = {
  // Six characters at most: they sit in the 56 px gutter left of the plot.
  volume: ["SILENT", "FULL"],
  filter: ["LO FRQ", "HI FRQ"],
  pan: ["LEFT", "RIGHT"],
  width: ["MONO", "WIDE"],
  crush: ["CLEAN", "CRUSH"],
  drive: ["CLEAN", "DRIVE"],
};
export const MOD_LFO_WAVES = ["Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H"] as const;

const pct = (label: string, def: number): Schema[string] => ({
  label,
  def,
  spec: { unit: "%", min: 0, max: 100 },
});
const hz = (label: string, def: number, min: number, max: number): Schema[string] => ({
  label,
  def,
  spec: { unit: "Hz", min, max, curve: "log" },
});

const shaperSchema = Object.fromEntries(
  SHAPERS.flatMap((s) => [
    [`${s.prefix}On`, { label: `${s.name} on`, def: 0 }],
    [`${s.prefix}Rate`, { label: "Rate", def: 8 / 19, spec: { options: SHAPER_RATES } }],
    [`${s.prefix}Depth`, pct("Depth", s.depth)],
    [`${s.prefix}Env`, pct("Envelope", 0)],
  ]),
);

export const modulatorSchema = {
  ...shaperSchema,
  fltType: {
    label: "Filter type",
    def: 0,
    spec: { options: ["Low-pass", "High-pass", "Band-pass"] },
  },
  fltLo: hz("Low", 0.37, 20, 20000),
  fltHi: hz("High", 0.8, 20, 20000),
  fltReso: { label: "Reso", def: 0.07, spec: { min: 0.5, max: 8 } },
  crsBits: { label: "Bits", def: 0.33, spec: { min: 1, max: 16 } },
  crsDown: { label: "Downsample", def: 0.23, spec: { unit: "x", min: 1, max: 32 } },
  drvMax: { label: "Drive", def: 0.5, spec: { unit: "dB", min: 0, max: 36 } },
  drvType: { label: "Drive type", def: 0, spec: { options: ["Tape", "Tube", "Diode"] } },
  trigMode: { label: "Trigger", def: 0, spec: { options: ["Host sync", "MIDI", "Audio"] } },
  trigThreshold: { label: "Threshold", def: 0.6, spec: { unit: "dB", min: -60, max: 0 } },
  envAttack: { label: "Attack", def: 0.2, spec: { unit: "ms", min: 0.5, max: 200 } },
  envRelease: { label: "Release", def: 0.25, spec: { unit: "ms", min: 10, max: 2000 } },
  envGain: { label: "Env gain", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  smooth: { label: "Smooth", def: 0.14, spec: { unit: "ms", min: 0, max: 50 } },
  shaperMix: pct("Shaper mix", 1),
  output: { label: "Output", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  // Classic chain
  wave: { label: "LFO wave", def: 0, spec: { options: MOD_LFO_WAVES } },
  ringOn: { label: "Ring on", def: 0 },
  ringFreq: hz("Freq", 0.45, 20, 2000),
  ringMix: pct("Mix", 1),
  fmodOn: { label: "Filter mod on", def: 0 },
  fmodRate: { label: "Rate", def: 0.35, spec: { unit: "rate", min: 0.01, max: 10 } },
  fmodDepth: pct("Depth", 0.5),
  fmodCutoff: hz("Cutoff", 0.5, 100, 12000),
  fmodReso: { label: "Reso", def: 0.2, spec: { min: 0.1, max: 8 } },
  fmodMix: pct("Mix", 1),
  chorusOn: { label: "Chorus on", def: 0 },
  chorusRate: { label: "Rate", def: 0.35, spec: { unit: "rate", min: 0.01, max: 4 } },
  chorusDepth: pct("Depth", 0.5),
  chorusMix: pct("Mix", 0.5),
  chorusFb: { label: "Feedback", def: 0.14, spec: { unit: "%", min: 0, max: 70 } },
  chorusWidth: pct("Width", 0.7),
  flangerOn: { label: "Flanger on", def: 0 },
  flangerRate: { label: "Rate", def: 0.3, spec: { unit: "rate", min: 0.05, max: 2 } },
  flangerDepth: pct("Depth", 0.6),
  flangerMix: pct("Mix", 0.5),
  flangerFb: { label: "Feedback", def: 0.55, spec: { unit: "%", min: 0, max: 90 } },
  phaserOn: { label: "Phaser on", def: 0 },
  phaserRate: { label: "Rate", def: 0.3, spec: { unit: "rate", min: 0.05, max: 5 } },
  phaserDepth: pct("Depth", 0.6),
  phaserMix: pct("Mix", 0.5),
  phaserFb: { label: "Feedback", def: 0.44, spec: { unit: "%", min: 0, max: 90 } },
  phaserStages: { label: "Stages", def: 1 / 3, spec: { options: ["4", "6", "8", "12"] } },
  tremOn: { label: "Tremolo on", def: 0 },
  tremRate: { label: "Rate", def: 0.5, spec: { unit: "rate", min: 0.1, max: 20 } },
  tremDepth: pct("Depth", 0.6),
  tremShape: { label: "Shape", def: 0, spec: { options: ["Sine", "Triangle", "Square"] } },
  tremStereo: { label: "Stereo", def: 0, spec: { unit: "deg", min: 0, max: 180 } },
  panOn: { label: "Auto-pan on", def: 0 },
  panRate: { label: "Rate", def: 0.3, spec: { unit: "rate", min: 0.05, max: 10 } },
  panDepth: pct("Depth", 0.8),
  rotaryOn: { label: "Rotary on", def: 0 },
  rotaryRate: { label: "Rate", def: 0.4, spec: { unit: "rate", min: 0.1, max: 10 } },
  rotaryDepth: pct("Depth", 0.6),
  rotaryMix: pct("Mix", 0.6),
} as Schema;

// ---------------------------------------------------------------- live frame
/** Host data the curve editor reads (see overlay-fx nativePanel.panelFrame). */
export interface ModulatorFrame {
  shaperPhase?: ArrayLike<number>;
  shaperValue?: ArrayLike<number>;
  shaperLevel?: ArrayLike<number>;
  shaperLevelBins?: number;
}

// ---------------------------------------------------------------- curves
/** Curves for the six shapers. With a host (setExtra present) they come from
 *  `extra.curves`; a local draft covers an edit until the host has caught up.
 *  Without a host (preview) they live here. */
function useCurves(extra: PanelProps["extra"], setExtra: PanelProps["setExtra"]) {
  const hostCurves = (extra?.curves ?? undefined) as Record<string, string> | undefined;
  const [local, setLocal] = useState<Record<string, string>>(() => ({ ...SHAPER_DEFAULT_CURVES }));
  const [draft, setDraft] = useState<Record<string, string>>({});
  const timers = useRef(new Map<string, number>());
  const pending = useRef(new Map<string, string>());
  const lastSent = useRef(new Map<string, number>());
  useEffect(
    () => () => {
      for (const t of timers.current.values()) window.clearTimeout(t);
    },
    [],
  );

  const hosted = !!setExtra;
  const available = !hosted || !!hostCurves;
  const text = (id: string) =>
    (hosted ? (draft[id] ?? hostCurves?.[id]) : local[id]) ??
    SHAPER_DEFAULT_CURVES[id as ShaperDef["id"]];

  const flush = (id: string) => {
    const t = pending.current.get(id);
    if (t === undefined || !setExtra) return;
    pending.current.delete(id);
    lastSent.current.set(id, performance.now());
    setExtra("curve", { shaper: id, text: t });
  };
  /** `final` = end of a gesture (sent at once); otherwise sends are throttled to ~30 Hz. */
  const set = (id: string, points: readonly CurvePoint[], final = true) => {
    const t = serializeCurve(points);
    if (!hosted) {
      setLocal((m) => ({ ...m, [id]: t }));
      return;
    }
    setDraft((m) => ({ ...m, [id]: t }));
    pending.current.set(id, t);
    for (const k of [id, `${id}:draft`]) {
      const old = timers.current.get(k);
      if (old) window.clearTimeout(old);
    }
    const since = performance.now() - (lastSent.current.get(id) ?? 0);
    if (final || since > 33) flush(id);
    else
      timers.current.set(
        id,
        window.setTimeout(() => flush(id), 33 - since),
      );
    // Drop the draft once the host has had time to echo the edit.
    timers.current.set(
      `${id}:draft`,
      window.setTimeout(
        () =>
          setDraft((m) => {
            const n = { ...m };
            delete n[id];
            return n;
          }),
        600,
      ),
    );
  };
  return { available, hosted, text, set };
}

// ---------------------------------------------------------------- editor
const ED_W = 1288,
  ED_H = 404,
  PLOT_X = 64,
  PLOT_R = 28,
  PLOT_T = 60,
  PLOT_B = 62;
const PW = ED_W - PLOT_X - PLOT_R,
  PH = ED_H - PLOT_T - PLOT_B;
export const SNAP_STEPS = [0, 4, 8, 16, 32, 64] as const;
const clamp01 = (v: number) => Math.max(0, Math.min(1, v));
const px = (x: number) => PLOT_X + x * PW;
const py = (y: number) => PLOT_T + (1 - y) * PH;

function Marker({ p, color, selected }: { p: CurvePoint; color: string; selected: boolean }) {
  const x = px(p.x),
    y = py(p.y),
    r = selected ? 8 : 6.5;
  const fill = selected ? color : "#140d05";
  const common = { fill, stroke: color, strokeWidth: 2 };
  if (p.w === 0)
    return <rect x={x - r} y={y - r} width={r * 2} height={r * 2} rx={1.5} {...common} />;
  if (p.w === 1)
    return (
      <path
        d={`M${x} ${y - r * 1.3}L${x + r * 1.3} ${y}L${x} ${y + r * 1.3}L${x - r * 1.3} ${y}Z`}
        {...common}
      />
    );
  return <circle cx={x} cy={y} r={r} {...common} />;
}

function CurveEditor({
  shaper,
  points,
  onEdit,
  getFrame,
  on,
  index,
  snap,
  sel,
  setSel,
}: {
  shaper: ShaperDef;
  points: readonly CurvePoint[];
  onEdit: (pts: CurvePoint[], final: boolean) => void;
  getFrame?: () => unknown;
  on: boolean;
  index: number;
  snap: number;
  sel: number;
  setSel: (i: number) => void;
}) {
  const svg = useRef<SVGSVGElement>(null);
  const drag = useRef<{ index: number; pts: CurvePoint[]; moved: boolean } | null>(null);
  const head = useRef<SVGLineElement>(null);
  const dot = useRef<SVGCircleElement>(null);
  const bars = useRef<SVGPathElement>(null);
  const path = useMemo(
    () =>
      sampleCurve(points, 512)
        .map(([x, y], i) => `${i ? "L" : "M"}${px(x).toFixed(1)} ${py(y).toFixed(1)}`)
        .join(""),
    [points],
  );

  // Live overlay: one DOM update per display frame, no React re-render.
  useEffect(() => {
    if (!getFrame) return;
    let raf = 0;
    const tick = () => {
      const f = (getFrame() ?? {}) as ModulatorFrame;
      const ph = f.shaperPhase?.[index];
      const val = f.shaperValue?.[index];
      const show = on && ph !== undefined;
      if (head.current) {
        head.current.setAttribute("x1", String(px(ph ?? 0)));
        head.current.setAttribute("x2", String(px(ph ?? 0)));
        head.current.style.opacity = show ? "1" : "0";
      }
      if (dot.current) {
        dot.current.setAttribute("cx", String(px(ph ?? 0)));
        dot.current.setAttribute("cy", String(py(clamp01(val ?? 0))));
        dot.current.style.opacity = show && val !== undefined ? "1" : "0";
      }
      if (bars.current) {
        const n = f.shaperLevelBins ?? 0,
          lv = f.shaperLevel;
        let d = "";
        if (on && lv && n > 0) {
          const w = PW / n;
          for (let k = 0; k < n; k++) {
            const v = Math.sqrt(clamp01(lv[index * n + k] ?? 0)); // sqrt: quiet material stays visible
            if (v > 0.002)
              d += `M${(PLOT_X + k * w + 1).toFixed(1)} ${PLOT_T + PH}v${(-v * PH).toFixed(1)}h${(w - 2).toFixed(1)}v${(v * PH).toFixed(1)}Z`;
          }
        }
        bars.current.setAttribute("d", d);
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [getFrame, index, on]);

  const toPlot = (e: RPointerEvent<SVGSVGElement>) => {
    const r = svg.current!.getBoundingClientRect();
    const sx = r.width / ED_W || 1,
      sy = r.height / ED_H || 1;
    return {
      x: ((e.clientX - r.left) / sx - PLOT_X) / PW,
      y: 1 - ((e.clientY - r.top) / sy - PLOT_T) / PH,
    };
  };
  const snapX = (x: number, free: boolean) =>
    clamp01(free || !snap ? x : Math.round(x * snap) / snap);
  const hit = (x: number, y: number) => {
    let best = -1,
      bd = 14 * 14;
    points.forEach((p, i) => {
      const dx = (p.x - x) * PW,
        dy = (p.y - y) * PH;
      const d = dx * dx + dy * dy;
      if (d < bd) {
        bd = d;
        best = i;
      }
    });
    return best;
  };

  const down = (e: RPointerEvent<SVGSVGElement>) => {
    if (e.button !== 0) return;
    const { x, y } = toPlot(e);
    if (x < -0.02 || x > 1.02 || y < -0.05 || y > 1.05) return;
    svg.current?.setPointerCapture?.(e.pointerId);
    let i = hit(x, y);
    let pts = [...points];
    const added = i < 0;
    if (added) {
      // New point takes the weight of the segment it splits, so the shape around it keeps its feel.
      const before = points.filter((p) => p.x <= x);
      const a = addPoint(
        pts,
        snapX(x, e.shiftKey),
        clamp01(y),
        (before[before.length - 1] ?? points[points.length - 1]).w,
      );
      if (a.index < 0) return;
      pts = a.curve;
      i = a.index;
      onEdit(pts, false);
    }
    setSel(i);
    drag.current = { index: i, pts, moved: added };
  };
  const move = (e: RPointerEvent<SVGSVGElement>) => {
    const d = drag.current;
    if (!d) return;
    const { x, y } = toPlot(e);
    const m = movePoint(d.pts, d.index, snapX(x, e.shiftKey), clamp01(y));
    d.pts = m.curve;
    d.index = m.index;
    d.moved = true;
    setSel(m.index);
    onEdit(m.curve, false);
  };
  const up = () => {
    const d = drag.current;
    if (!d) return;
    drag.current = null;
    if (d.moved) onEdit(d.pts, true); // a click on a point without moving it changes nothing
  };
  const dbl = (e: RMouseEvent<SVGSVGElement>) => {
    const r = svg.current!.getBoundingClientRect();
    const sx = r.width / ED_W || 1,
      sy = r.height / ED_H || 1;
    const i = hit(
      ((e.clientX - r.left) / sx - PLOT_X) / PW,
      1 - ((e.clientY - r.top) / sy - PLOT_T) / PH,
    );
    if (i >= 0 && points.length > 2) {
      onEdit(removePoint(points, i), true);
      setSel(Math.max(0, i - 1));
    }
  };
  const key = (e: KeyboardEvent<SVGSVGElement>) => {
    if (sel < 0 || sel >= points.length) return;
    const p = points[sel];
    const fine = e.shiftKey;
    const sx = fine || !snap ? 1 / 256 : 1 / snap,
      sy = fine ? 1 / 256 : 1 / 32;
    let next: CurvePoint[] | null = null,
      ni = sel;
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      const m = movePoint(points, sel, p.x + (e.key === "ArrowLeft" ? -sx : sx), p.y);
      next = m.curve;
      ni = m.index;
    } else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
      next = movePoint(points, sel, p.x, p.y + (e.key === "ArrowUp" ? sy : -sy)).curve;
    } else if (e.key === "Delete" || e.key === "Backspace") {
      if (points.length > 2) {
        next = removePoint(points, sel);
        ni = Math.max(0, sel - 1);
      }
    } else if (e.key === "w" || e.key === "W") {
      next = setWeight(points, sel, ((p.w + 1) % 3) as Weight);
    } else if (e.key === "Tab") {
      return;
    } else if (e.key === "[" || e.key === "]") {
      setSel((sel + (e.key === "]" ? 1 : points.length - 1)) % points.length);
      e.preventDefault();
      return;
    } else return;
    e.preventDefault();
    if (next) {
      onEdit(next, true);
      setSel(ni);
    }
  };

  const [lo, hi] = CURVE_MEANING[shaper.id];
  const steps = snap || 16;
  return (
    <svg
      ref={svg}
      width={ED_W}
      height={ED_H}
      tabIndex={0}
      role="application"
      style={{
        position: "absolute",
        inset: 0,
        touchAction: "none",
        cursor: "crosshair",
        outlineOffset: -3,
      }}
      aria-label={`${shaper.name} curve editor, ${points.length} points. Click to add, drag to move (Shift = off-grid), double-click or Delete to remove, W = point weight, [ and ] select points, arrows move the selected point.`}
      onPointerDown={down}
      onPointerMove={move}
      onPointerUp={up}
      onPointerCancel={up}
      onDoubleClick={dbl}
      onKeyDown={key}
    >
      {Array.from({ length: steps + 1 }, (_, i) => {
        const x = px(i / steps) + 0.5,
          major = i % (steps >= 16 ? 4 : 1) === 0;
        return (
          <line
            key={i}
            x1={x}
            x2={x}
            y1={PLOT_T}
            y2={PLOT_T + PH}
            stroke={major ? "rgba(255,174,58,.28)" : "rgba(255,174,58,.11)"}
          />
        );
      })}
      {[0.25, 0.5, 0.75].map((y) => (
        <line
          key={y}
          x1={PLOT_X}
          x2={PLOT_X + PW}
          y1={py(y) + 0.5}
          y2={py(y) + 0.5}
          stroke="rgba(255,174,58,.11)"
        />
      ))}
      <rect
        x={PLOT_X + 0.5}
        y={PLOT_T + 0.5}
        width={PW}
        height={PH}
        fill="none"
        stroke="rgba(255,174,58,.35)"
      />
      <path ref={bars} d="" fill="rgba(255,243,220,.10)" />
      <path
        d={`${path}L${px(1)} ${py(0)}L${px(0)} ${py(0)}Z`}
        fill={`${shaper.color}1f`}
        opacity={on ? 1 : 0.5}
      />
      <path
        d={path}
        fill="none"
        stroke={shaper.color}
        strokeWidth={2.5}
        opacity={on ? 1 : 0.5}
        style={{ filter: on ? `drop-shadow(0 0 5px ${shaper.color}aa)` : "none" }}
      />
      <line
        ref={head}
        x1={PLOT_X}
        x2={PLOT_X}
        y1={PLOT_T}
        y2={PLOT_T + PH}
        stroke="#fff3dc"
        strokeWidth={1.5}
        style={{ opacity: 0 }}
      />
      <circle
        ref={dot}
        r={6}
        fill="#fff3dc"
        style={{ opacity: 0, filter: "drop-shadow(0 0 6px #fff3dc)" }}
      />
      {points.map((p, i) => (
        <Marker key={i} p={p} color={shaper.color} selected={i === sel} />
      ))}
      <text
        x={PLOT_X - 8}
        y={PLOT_T + 11}
        textAnchor="end"
        fill="rgba(255,174,58,.6)"
        fontSize={11}
        fontFamily='"Share Tech Mono", monospace'
      >
        {hi}
      </text>
      <text
        x={PLOT_X - 8}
        y={PLOT_T + PH}
        textAnchor="end"
        fill="rgba(255,174,58,.6)"
        fontSize={11}
        fontFamily='"Share Tech Mono", monospace'
      >
        {lo}
      </text>
    </svg>
  );
}

// ---------------------------------------------------------------- screen
function ShaperTab({
  s,
  i,
  sel,
  onSel,
}: {
  s: ShaperDef;
  i: number;
  sel: number;
  onSel: (i: number) => void;
}) {
  const has = useHas(`${s.prefix}On`);
  const on = useValue(`${s.prefix}On`) >= 0.5;
  const rate = useLabel(`${s.prefix}Rate`);
  if (!has) return null;
  return (
    <button
      type="button"
      role="tab"
      aria-selected={i === sel}
      className={`cs-key${i === sel ? " cs-on" : ""}`}
      onClick={() => onSel(i)}
      aria-label={`${s.name} shaper, ${on ? "on" : "off"}, ${rate}`}
      style={{ minHeight: 38, display: "flex", alignItems: "center", gap: 8, padding: "0 12px" }}
    >
      <span
        style={{
          width: 10,
          height: 10,
          borderRadius: 3,
          background: on ? s.color : "transparent",
          border: `2px solid ${s.color}`,
          boxShadow: on ? `0 0 8px ${s.color}` : "none",
        }}
      />
      {s.name}
      <span className="cs-readout" style={{ fontSize: 11, opacity: 0.75 }}>
        {rate}
      </span>
    </button>
  );
}

function ShaperScreen({
  sel,
  setSel,
  curves,
  getFrame,
}: {
  sel: number;
  setSel: (i: number) => void;
  curves: ReturnType<typeof useCurves>;
  getFrame?: () => unknown;
}) {
  const s = SHAPERS[sel];
  const on = useValue(`${s.prefix}On`) >= 0.5;
  const text = curves.text(s.id);
  const points = useMemo(() => parseCurve(text), [text]);
  const [snapI, setSnapI] = useState(3);
  const [pt, setPt] = useState(0);
  const [seed, setSeed] = useState(1);
  useEffect(() => {
    setPt(0);
  }, [sel]);
  const selIdx = Math.min(pt, points.length - 1);
  const edit = (pts: CurvePoint[], final = true) => curves.set(s.id, pts, final);
  const snap = SNAP_STEPS[snapI];
  const selP = points[selIdx];

  return (
    <div
      className="cs-scope"
      style={{
        position: "relative",
        width: ED_W,
        height: ED_H,
        boxSizing: "border-box",
        borderRadius: 6,
        background: "radial-gradient(ellipse at 50% 40%, #1d1307, #0e0903 80%)",
        boxShadow: "inset 0 2px 6px #000, 0 0 0 1px #000",
        overflow: "hidden",
      }}
    >
      {curves.available ? (
        <CurveEditor
          shaper={s}
          points={points}
          onEdit={edit}
          getFrame={getFrame}
          on={on}
          index={sel}
          snap={snap}
          sel={selIdx}
          setSel={setPt}
        />
      ) : (
        <Overlay style={{ left: 0, right: 0, top: 170, textAlign: "center" }}>
          <Readout size={13}>THIS HOST BUILD DOES NOT REPORT CURVES</Readout>
        </Overlay>
      )}
      <Overlay
        interactive
        style={{ left: 12, right: 12, top: 10, display: "flex", gap: 8, alignItems: "center" }}
      >
        <div role="tablist" aria-label="Shapers" style={{ display: "flex", gap: 8 }}>
          {SHAPERS.map((x, i) => (
            <ShaperTab key={x.id} s={x} i={i} sel={sel} onSel={setSel} />
          ))}
        </div>
        <span style={{ flexGrow: 1 }} />
        {curves.available && selP && (
          <Readout size={11} style={{ opacity: 0.85, marginRight: 8 }}>
            {`PT ${selIdx + 1}/${points.length} · ${(selP.x * 100).toFixed(1)}% · ${(selP.y * 100).toFixed(0)}`}
          </Readout>
        )}
        <Readout size={11} style={{ opacity: 0.75 }}>
          SNAP
        </Readout>
        <button
          type="button"
          className="cs-lcd"
          onClick={() => setSnapI((snapI + 1) % SNAP_STEPS.length)}
          aria-label={`Snap: ${snap ? `${snap} steps` : "off"}. Next`}
          style={{
            minHeight: 36,
            minWidth: 64,
            padding: "0 12px",
            fontSize: 12,
            cursor: "pointer",
            justifyContent: "center",
          }}
        >
          {snap ? `1/${snap}` : "OFF"} ⇅
        </button>
      </Overlay>
      {curves.available && (
        <Overlay
          interactive
          style={{ left: 12, right: 12, bottom: 10, display: "flex", gap: 8, alignItems: "center" }}
        >
          <div
            className="cs-seg"
            role="group"
            aria-label="Weight of the selected point"
            style={{ width: 222, marginRight: 2 }}
          >
            {WEIGHT_NAMES.map((w, i) => (
              <button
                key={w}
                type="button"
                aria-pressed={selP?.w === i}
                onClick={() => edit(setWeight(points, selIdx, i as Weight))}
              >
                {w}
              </button>
            ))}
          </div>
          <Key
            label="Delete selected point"
            disabled={points.length <= 2}
            onClick={() => {
              edit(removePoint(points, selIdx));
              setPt(Math.max(0, selIdx - 1));
            }}
          >
            Del
          </Key>
          <span style={{ width: 10 }} />
          {(Object.keys(CURVE_SHAPES) as CurveShape[]).map((k) => (
            <Key
              key={k}
              label={`Replace with ${CURVE_SHAPE_LABELS[k]}`}
              onClick={() => {
                edit([...CURVE_SHAPES[k]]);
                setPt(0);
              }}
              style={{ padding: "0 9px" }}
            >
              {CURVE_SHAPE_LABELS[k]}
            </Key>
          ))}
          <span style={{ flexGrow: 1 }} />
          <Key
            label="Flip horizontally"
            onClick={() => edit(flipH(points))}
            style={{ padding: "0 9px" }}
          >
            ⇆
          </Key>
          <Key
            label="Flip vertically"
            onClick={() => edit(flipV(points))}
            style={{ padding: "0 9px" }}
          >
            ⇅
          </Key>
          <Key
            label="Repeat twice per cycle"
            onClick={() => edit(repeatCurve(points, 2))}
            style={{ padding: "0 9px" }}
          >
            ×2
          </Key>
          <Key
            label="Shift curve earlier"
            onClick={() => edit(nudgeCurve(points, -1 / (snap || 16)))}
            style={{ padding: "0 9px" }}
          >
            ◂
          </Key>
          <Key
            label="Shift curve later"
            onClick={() => edit(nudgeCurve(points, 1 / (snap || 16)))}
            style={{ padding: "0 9px" }}
          >
            ▸
          </Key>
          <Key
            label="Random levels"
            onClick={() => {
              edit(randomizeY(points, seed * 2654435761));
              setSeed(seed + 1);
            }}
            style={{ padding: "0 9px" }}
          >
            Dice
          </Key>
        </Overlay>
      )}
    </div>
  );
}

// ---------------------------------------------------------------- deck
function ShaperDeck({ s }: { s: ShaperDef }) {
  const extras = s.id === "filter" || s.id === "crush" || s.id === "drive";
  return (
    <PGroup
      title={`${s.name} shaper`}
      chip={s.color}
      keys={[`${s.prefix}On`, `${s.prefix}Depth`]}
      enable={`${s.prefix}On`}
      style={{ width: extras ? 540 : 330, boxSizing: "border-box" }}
    >
      <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
        <Row align="center" justify="flex-start" gap={10}>
          <Readout size={11} style={{ opacity: 0.75 }}>
            CYCLE
          </Readout>
          <PCycle k={`${s.prefix}Rate`} style={{ minWidth: 96 }} />
          {s.id === "filter" && (
            <PChoice
              k="fltType"
              variant="seg"
              style={{ flexGrow: 1 }}
              labels={["LP", "HP", "BP"]}
            />
          )}
          {s.id === "drive" && <PChoice k="drvType" variant="seg" style={{ flexGrow: 1 }} />}
        </Row>
        <Row justify="flex-start" gap={14}>
          <PKnob k={`${s.prefix}Depth`} size="xl" />
          <PKnob k={`${s.prefix}Env`} size="m" />
          {s.id === "filter" && (
            <>
              <PKnob k="fltLo" size="l" />
              <PKnob k="fltHi" size="l" />
              <PKnob k="fltReso" size="m" />
            </>
          )}
          {s.id === "crush" && (
            <>
              <PKnob k="crsBits" size="l" />
              <PKnob k="crsDown" size="l" />
            </>
          )}
          {s.id === "drive" && <PKnob k="drvMax" size="l" />}
        </Row>
      </div>
    </PGroup>
  );
}

function ShapersDeck({ s, meters }: { s: ShaperDef; meters: PanelProps["meters"] }) {
  const audio = useOption("trigMode") === 2;
  return (
    <div style={{ display: "flex", gap: 12, width: "100%" }}>
      <ShaperDeck s={s} />
      <PGroup
        title="Trigger"
        chip={system.mod}
        keys={["trigMode"]}
        style={{ width: 250, boxSizing: "border-box" }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          <PChoice k="trigMode" variant="seg" labels={["Host", "MIDI", "Audio"]} />
          <Row justify="center">
            <div style={{ opacity: audio ? 1 : 0.45 }}>
              <PKnob k="trigThreshold" size="l" />
            </div>
          </Row>
        </div>
      </PGroup>
      <PGroup
        title="Envelope"
        chip={system.env}
        keys={["envAttack", "envRelease", "envGain"]}
        style={{ width: 230, boxSizing: "border-box" }}
      >
        <Row gap={10}>
          <PKnob k="envAttack" size="m" />
          <PKnob k="envRelease" size="m" />
          <PKnob k="envGain" size="m" />
        </Row>
      </PGroup>
      <PGroup
        title="Output"
        chip={system.out}
        keys={["shaperMix", "output", "smooth"]}
        style={{ flexGrow: 1, minWidth: 0, boxSizing: "border-box" }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          <Row align="flex-end" gap={10}>
            <PKnob k="shaperMix" size="xl" />
            <PKnob k="smooth" size="m" />
            <PKnob k="output" size="m" />
          </Row>
          {meters?.out && (
            <div style={{ width: "100%" }}>
              <StereoMeter read={meters.out} segments={16} label="Output level" />
            </div>
          )}
        </div>
      </PGroup>
    </div>
  );
}

// ---------------------------------------------------------------- classic page
const CLASSIC = [
  { title: "Ring mod", on: "ringOn", knobs: ["ringFreq", "ringMix"], chip: system.colour },
  {
    title: "Filter mod",
    on: "fmodOn",
    knobs: ["fmodCutoff", "fmodDepth", "fmodRate"],
    sliders: ["fmodReso", "fmodMix"],
    chip: system.filter,
  },
  {
    title: "Chorus",
    on: "chorusOn",
    knobs: ["chorusDepth", "chorusRate", "chorusMix"],
    sliders: ["chorusFb", "chorusWidth"],
    chip: system.mod,
  },
  {
    title: "Flanger",
    on: "flangerOn",
    knobs: ["flangerDepth", "flangerRate", "flangerMix"],
    sliders: ["flangerFb"],
    chip: system.mod,
  },
  {
    title: "Phaser",
    on: "phaserOn",
    knobs: ["phaserDepth", "phaserRate", "phaserMix"],
    sliders: ["phaserFb"],
    cycle: "phaserStages",
    chip: system.mod,
  },
  {
    title: "Tremolo",
    on: "tremOn",
    knobs: ["tremDepth", "tremRate"],
    sliders: ["tremStereo"],
    cycle: "tremShape",
    chip: system.env,
  },
  { title: "Auto-pan", on: "panOn", knobs: ["panDepth", "panRate"], chip: system.env },
  {
    title: "Rotary",
    on: "rotaryOn",
    knobs: ["rotaryDepth", "rotaryRate", "rotaryMix"],
    chip: system.source,
  },
] as const;

function ClassicChain() {
  const onShapers = SHAPERS.map((s) => useValue(`${s.prefix}On`) >= 0.5).filter(Boolean).length;
  const states = CLASSIC.map((c) => ({ t: c.title, on: useValue(c.on) >= 0.5, has: useHas(c.on) }));
  return (
    <div
      className="cs-lcd"
      style={{
        height: "100%",
        boxSizing: "border-box",
        display: "flex",
        alignItems: "center",
        gap: 10,
        padding: "0 18px",
        flexWrap: "wrap",
      }}
      aria-label="Signal flow"
    >
      <Readout size={12} style={{ letterSpacing: ".12em" }}>
        IN →
      </Readout>
      <Readout size={12} style={{ opacity: onShapers ? 1 : 0.4 }}>
        SHAPERS ({onShapers}) →
      </Readout>
      {states
        .filter((s) => s.has)
        .map((s) => (
          <span key={s.t} style={{ display: "inline-flex", alignItems: "center", gap: 6 }}>
            <Led state={s.on ? "on" : "off"} />
            <Readout size={12} style={{ opacity: s.on ? 1 : 0.4 }}>
              {s.t.toUpperCase()} →
            </Readout>
          </span>
        ))}
      <Readout size={12} style={{ letterSpacing: ".12em" }}>
        OUT
      </Readout>
    </div>
  );
}

function ClassicDeck() {
  return (
    <div
      style={{
        display: "grid",
        gridTemplateColumns: "repeat(4, minmax(0, 1fr))",
        gridAutoRows: "min-content",
        gap: 12,
        width: "100%",
      }}
    >
      {CLASSIC.map((c) => (
        <PGroup
          key={c.title}
          title={c.title}
          chip={c.chip}
          keys={[c.on, ...c.knobs]}
          enable={c.on}
          style={{ boxSizing: "border-box", minWidth: 0 }}
        >
          <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
            <Row justify="space-around" gap={6}>
              {c.knobs.map((k, i) => (
                <PKnob key={k} k={k} size={i === 0 ? "l" : "m"} />
              ))}
            </Row>
            {"cycle" in c && (
              <Row align="center" gap={10}>
                <Readout size={11} style={{ opacity: 0.75, width: 64 }}>
                  {c.cycle === "phaserStages" ? "STAGES" : "SHAPE"}
                </Readout>
                <PCycle k={c.cycle} style={{ flexGrow: 1 }} />
              </Row>
            )}
            {"sliders" in c && c.sliders.map((k) => <PSlider key={k} k={k} width={270} />)}
          </div>
        </PGroup>
      ))}
    </div>
  );
}

// ---------------------------------------------------------------- panel
export function ModulatorPanel({
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
  initialPage,
}: PanelProps & { initialPage?: "shapers" | "classic" }) {
  const [page, setPage] = useState<"shapers" | "classic">(initialPage ?? "shapers");
  const [sel, setSel] = useState(0);
  const curves = useCurves(extra, setExtra);
  const onKey = (e: KeyboardEvent<HTMLDivElement>) => {
    if (!(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== "z") return;
    const fn = e.shiftKey ? onRedo : onUndo;
    if (fn) {
      e.preventDefault();
      fn();
    }
  };
  const header = (
    <>
      <div className="cs-seg" role="tablist" aria-label="Page" style={{ width: 210 }}>
        <button
          type="button"
          role="tab"
          aria-selected={page === "shapers"}
          aria-pressed={page === "shapers"}
          onClick={() => setPage("shapers")}
        >
          Shapers
        </button>
        <button
          type="button"
          role="tab"
          aria-selected={page === "classic"}
          aria-pressed={page === "classic"}
          onClick={() => setPage("classic")}
        >
          Classic
        </button>
      </div>
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
      <PAction name="resetCurves" style={{ minHeight: 40 }}>
        Reset curves
      </PAction>
    </>
  );
  return (
    <PanelScope schema={modulatorSchema} specs={specs} store={store} map={map}>
      <div onKeyDown={onKey}>
        {page === "shapers" ? (
          <PluginFrame
            name="MODULATOR"
            tag="OVERLAY FX · SHAPERS"
            subtitle="Draw the motion · six shapers in sync with the host"
            presetName={presetName}
            onPresetStep={onPresetStep}
            headerRight={header}
            screenHeight={ED_H}
            screen={<ShaperScreen sel={sel} setSel={setSel} curves={curves} getFrame={getFrame} />}
          >
            <ShapersDeck s={SHAPERS[sel]} meters={meters} />
          </PluginFrame>
        ) : (
          <PluginFrame
            name="MODULATOR"
            tag="OVERLAY FX · CLASSIC"
            subtitle="LFO effects · run after the shapers"
            presetName={presetName}
            onPresetStep={onPresetStep}
            headerRight={header}
            screenHeight={64}
            screen={<ClassicScreen meters={meters} />}
          >
            <ClassicDeck />
          </PluginFrame>
        )}
      </div>
    </PanelScope>
  );
}

function ClassicScreen({ meters }: { meters: PanelProps["meters"] }) {
  const has = useBound("wave");
  return (
    <div style={{ display: "flex", gap: 12, height: 64 }}>
      <div style={{ flexGrow: 1, minWidth: 0 }}>
        <ClassicChain />
      </div>
      {has && (
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <Readout size={11} style={{ opacity: 0.75 }}>
            LFO
          </Readout>
          <PCycle k="wave" style={{ minWidth: 110 }} />
        </div>
      )}
      {meters?.out && (
        <div
          style={{
            width: 170,
            display: "flex",
            flexDirection: "column",
            justifyContent: "center",
            gap: 4,
          }}
        >
          <Readout size={11} style={{ opacity: 0.75 }}>
            OUT
          </Readout>
          <StereoMeter read={meters.out} segments={14} label="Output level" />
        </div>
      )}
    </div>
  );
}
