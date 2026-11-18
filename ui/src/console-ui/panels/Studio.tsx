// GENERATED from console-ui — edit there, then run `npm run sync`.
// FabFilter-style EQ surface and the "surgical" pop-out shell used by the
// Channel Strip and the Master.
//
// The surface is an SVG overlay on the shared spectrum Scope: band nodes drag
// in two dimensions, the mouse wheel over a node changes Q, Shift is fine,
// double-click opens that band's detail, and arrow keys nudge the selection.
// Everything writes through the same bindings as the knobs, so the graph, the
// deck and the host stay in one state.

import {
  useEffect,
  useRef,
  type CSSProperties,
  type KeyboardEvent,
  type PointerEvent as RPointerEvent,
  type ReactNode,
} from "react";
import { Scope } from "../Scope";
import { spectrumRenderer } from "../renderers";
import { color, system } from "../tokens";
import { toNorm, toValue } from "../param";
import { useBound, useSpec } from "./controls";
import { stripResponseDb, type StripShape } from "./eqCurve";

export const EQ_PAD_T = 14;
export const EQ_PAD_B = 20;
export const EQ_RANGE = 18;

export const logX = (hz: number, w: number) => (Math.log(hz / 20) / Math.log(1000)) * w;
export const xHz = (x: number, w: number) => 20 * Math.pow(1000, Math.min(1, Math.max(0, x / w)));

export interface SurfaceBand {
  /** 0-based index, drawn as n + 1. */
  n: number;
  /** Stable label for the node ("3", "HP"). */
  label?: string;
  color: string;
  shape: StripShape;
  freq: number;
  gain: number;
  q: number;
  on: boolean;
  /** Current dynamic gain in dB (drawn as a live bar when non-zero). */
  dyn?: number;
  freqKey: string;
  gainKey?: string;
  qKey?: string;
  /** Show a dot even when the band's gain shape is flat. */
  showNode?: boolean;
}

export interface SurfaceEdge {
  kind: "hp" | "lp";
  freq: number;
  on: boolean;
  color: string;
  freqKey: string;
  onKey?: string;
}

interface SurfaceProps {
  bands: SurfaceBand[];
  edges?: SurfaceEdge[];
  selected: number;
  onSelect: (n: number) => void;
  onPopout?: (n: number) => void;
  curve?: (hz: number) => number;
  width: number;
  height: number;
  getFrame?: () => { spectrum?: ArrayLike<number> } | undefined;
  label: string;
  children?: ReactNode;
}

/** Pointer position in the SVG's own units (the panel is scaled by the host). */
export function localPoint(e: RPointerEvent<Element>, el: Element, w: number, h: number) {
  const r = el.getBoundingClientRect();
  const sx = r.width / w || 1,
    sy = r.height / h || 1;
  return { x: (e.clientX - r.left) / sx, y: (e.clientY - r.top) / sy };
}

const fmtHz = (f: number) =>
  f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 1 : 2)}k` : `${Math.round(f)}`;
const fmtDb = (d: number) => `${d > 0 ? "+" : d < 0 ? "−" : ""}${Math.abs(d).toFixed(1)}`;
const isGainShape = (s: StripShape) => s === "bell" || s === "ls" || s === "hs";

function EqNode({
  band,
  selected,
  onSelect,
  onPopout,
  w,
  plotH,
  yOfDb,
  dbOfY,
}: {
  band: SurfaceBand;
  selected: boolean;
  onSelect: (n: number) => void;
  onPopout?: (n: number) => void;
  w: number;
  plotH: number;
  yOfDb: (db: number) => number;
  dbOfY: (y: number) => number;
}) {
  const freq = useBound(band.freqKey);
  // Hooks must run unconditionally; an empty key resolves to an unmapped
  // control and returns null, which keeps optional Q/gain lookups safe.
  const gain = useBound(band.gainKey ?? "");
  const q = useBound(band.qKey ?? "");
  const freqSpec = useSpec(band.freqKey);
  const gainSpec = useSpec(band.gainKey ?? band.freqKey);
  const qSpec = useSpec(band.qKey ?? band.freqKey);
  const drag = useRef(false);

  if (!freq) return null;
  const x = logX(band.freq, w);
  const y = yOfDb(isGainShape(band.shape) ? band.gain : 0);
  const size = selected ? 11 : 9;
  const dim = !band.on;

  const move = (px: number, py: number) => {
    freq.onChange(toNorm(xHz(px, w), freqSpec));
    if (gain && isGainShape(band.shape))
      gain.onChange(toNorm(Math.max(-EQ_RANGE, Math.min(EQ_RANGE, dbOfY(py))), gainSpec));
  };

  return (
    <g
      role="slider"
      tabIndex={0}
      aria-label={`EQ band ${band.n + 1}: ${band.shape} ${Math.round(band.freq)} Hz${isGainShape(band.shape) ? `, ${fmtDb(band.gain)} dB` : ""}${band.on ? "" : ", off"}`}
      aria-valuenow={Math.round(band.freq)}
      aria-valuemin={20}
      aria-valuemax={20000}
      style={{ cursor: "grab", outline: "none" }}
      onPointerDown={(e) => {
        e.stopPropagation();
        onSelect(band.n);
        (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
        drag.current = true;
        freq.onGesture("start");
        gain?.onGesture("start");
      }}
      onPointerMove={(e) => {
        if (!drag.current) return;
        const p = localPoint(e, e.currentTarget.ownerSVGElement ?? e.currentTarget, w, plotH);
        move(p.x, p.y);
      }}
      onPointerUp={() => {
        if (!drag.current) return;
        drag.current = false;
        freq.onGesture("end");
        gain?.onGesture("end");
      }}
      onPointerCancel={() => {
        if (!drag.current) return;
        drag.current = false;
        freq.onGesture("end");
        gain?.onGesture("end");
      }}
      onWheel={(e) => {
        if (!q) return;
        const cur = toValue(q.value, qSpec);
        const next = Math.max(
          qSpec.min ?? 0.1,
          Math.min(qSpec.max ?? 18, cur * (e.deltaY > 0 ? 0.88 : 1.14)),
        );
        q.onGesture("start");
        q.onChange(toNorm(next, qSpec));
        q.onGesture("end");
      }}
      onDoubleClick={() => onPopout?.(band.n)}
      onFocus={() => onSelect(band.n)}
      onKeyDown={(e: KeyboardEvent<SVGGElement>) => {
        const fine = e.shiftKey;
        if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
          e.preventDefault();
          const f = xHz(
            logX(band.freq, plotH) + (e.key === "ArrowRight" ? 1 : -1) * (fine ? 1 : 8),
            plotH,
          );
          freq.onGesture("start");
          freq.onChange(toNorm(f, freqSpec));
          freq.onGesture("end");
        } else if (
          (e.key === "ArrowUp" || e.key === "ArrowDown") &&
          gain &&
          isGainShape(band.shape)
        ) {
          e.preventDefault();
          const g = band.gain + (e.key === "ArrowUp" ? 1 : -1) * (fine ? 0.1 : 0.5);
          gain.onGesture("start");
          gain.onChange(toNorm(Math.max(-EQ_RANGE, Math.min(EQ_RANGE, g)), gainSpec));
          gain.onGesture("end");
        }
      }}
    >
      {band.dyn !== undefined && Math.abs(band.dyn) > 0.05 && (
        <rect
          x={x - 4}
          y={Math.min(yOfDb(0), yOfDb(band.dyn))}
          width={8}
          height={Math.max(1, Math.abs(yOfDb(band.dyn) - yOfDb(0)))}
          fill={system.mod}
          opacity={0.55}
          rx={2}
        />
      )}
      <line
        x1={x}
        x2={x}
        y1={y}
        y2={yOfDb(0)}
        stroke={band.color}
        strokeWidth={1}
        strokeDasharray="2 3"
        opacity={dim ? 0.25 : 0.4}
      />
      <circle cx={x} cy={y} r={16} fill="transparent" />
      <circle
        cx={x}
        cy={y}
        r={size}
        fill={selected ? band.color : "#101011"}
        stroke={band.color}
        strokeWidth={selected ? 2 : 1.5}
        strokeDasharray={dim ? "3 2" : undefined}
        opacity={dim ? 0.55 : 1}
        style={selected ? { filter: `drop-shadow(0 0 6px ${band.color})` } : undefined}
      />
      <text
        x={x}
        y={y + 4}
        textAnchor="middle"
        fontSize={11}
        fontFamily='"Share Tech Mono", monospace'
        fill={selected ? "#120c06" : band.on ? band.color : "rgba(200,200,200,.5)"}
        style={{ pointerEvents: "none" }}
      >
        {band.label ?? band.n + 1}
      </text>
    </g>
  );
}

function EdgeNode({
  edge,
  w,
  plotH,
  yOfDb,
}: {
  edge: SurfaceEdge;
  w: number;
  plotH: number;
  yOfDb: (db: number) => number;
}) {
  const freq = useBound(edge.freqKey);
  const on = useBound(edge.onKey ?? "");
  const spec = useSpec(edge.freqKey);
  const drag = useRef(false);
  if (!freq) return null;
  const x = logX(edge.freq, w);
  const y = yOfDb(0);
  const toggle = () => {
    if (!on) return;
    on.onGesture("start");
    on.onChange(on.value >= 0.5 ? 0 : 1);
    on.onGesture("end");
  };
  return (
    <g
      role="slider"
      tabIndex={0}
      aria-label={`${edge.kind === "hp" ? "High-pass" : "Low-pass"} filter: ${Math.round(edge.freq)} Hz, ${edge.on ? "on" : "off"}`}
      aria-valuenow={Math.round(edge.freq)}
      aria-valuemin={20}
      aria-valuemax={20000}
      style={{ cursor: "ew-resize", outline: "none" }}
      onPointerDown={(e) => {
        e.stopPropagation();
        (e.currentTarget as Element).setPointerCapture?.(e.pointerId);
        drag.current = true;
        freq.onGesture("start");
      }}
      onPointerMove={(e) => {
        if (!drag.current) return;
        const p = localPoint(e, e.currentTarget.ownerSVGElement ?? e.currentTarget, w, plotH);
        freq.onChange(toNorm(xHz(p.x, w), spec));
      }}
      onPointerUp={() => {
        if (drag.current) {
          drag.current = false;
          freq.onGesture("end");
        }
      }}
      onDoubleClick={toggle}
      onKeyDown={(e) => {
        if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
        e.preventDefault();
        const f = edge.freq * (e.key === "ArrowRight" ? 1.06 : 1 / 1.06);
        freq.onGesture("start");
        freq.onChange(toNorm(f, spec));
        freq.onGesture("end");
      }}
    >
      <line
        x1={x}
        x2={x}
        y1={0}
        y2={plotH}
        stroke={edge.color}
        strokeWidth={1.5}
        strokeDasharray="4 4"
        opacity={edge.on ? 0.7 : 0.25}
      />
      <circle
        cx={x}
        cy={y}
        r={8}
        fill="#101011"
        stroke={edge.color}
        strokeWidth={1.5}
        opacity={edge.on ? 1 : 0.45}
      />
      <text
        x={x}
        y={y + 3.5}
        textAnchor="middle"
        fontSize={9}
        fontFamily='"Share Tech Mono", monospace'
        fill={edge.color}
        style={{ pointerEvents: "none" }}
      >
        {edge.kind === "hp" ? "H" : "L"}
      </text>
      <text
        x={x + 6}
        y={12}
        fontSize={10}
        fontFamily='"Share Tech Mono", monospace'
        fill={edge.color}
        opacity={edge.on ? 0.9 : 0.4}
        style={{ pointerEvents: "none" }}
      >
        {fmtHz(edge.freq)}
      </text>
    </g>
  );
}

/** The interactive EQ graph. Drag nodes, wheel over one for Q, double-click
 *  for the surgical pop-out. */
export function EqSurface({
  bands,
  edges = [],
  selected,
  onSelect,
  onPopout,
  curve,
  width,
  height,
  getFrame,
  label,
  children,
}: SurfaceProps) {
  const w = width - 6;
  const h = height - 6;
  const plotH = h - EQ_PAD_T - EQ_PAD_B;
  const yOfDb = (db: number) =>
    EQ_PAD_T + (plotH / 2) * (1 - Math.max(-EQ_RANGE, Math.min(EQ_RANGE, db)) / EQ_RANGE);
  const dbOfY = (y: number) => EQ_RANGE * (1 - (y - EQ_PAD_T) / (plotH / 2));
  const curveDb =
    curve ??
    ((hz: number) =>
      stripResponseDb(
        bands
          .filter((b) => b.on)
          .map((b) => ({ shape: b.shape, freq: b.freq, gain: b.gain, q: b.q, slope: 0 })),
        hz,
      ));

  let d = "";
  for (let x = 0; x <= w; x += 2) {
    d += `${x ? "L" : "M"}${x} ${yOfDb(curveDb(xHz(x, w))).toFixed(1)}`;
  }

  return (
    <Scope
      renderer={spectrumRenderer}
      params={{ height: 0.84 }}
      width={width}
      height={height}
      getFrame={getFrame}
      label={label}
    >
      <svg
        width={w}
        height={h}
        style={{ position: "absolute", left: 3, top: 3 }}
        role="group"
        aria-label="EQ bands: drag to move, wheel for Q, double-click for detail"
      >
        {[-12, -6, 6, 12].map((db) => (
          <line
            key={db}
            x1={0}
            x2={w}
            y1={yOfDb(db) + 0.5}
            y2={yOfDb(db) + 0.5}
            stroke="rgba(255,174,58,.07)"
          />
        ))}
        <line x1={0} x2={w} y1={yOfDb(0) + 0.5} y2={yOfDb(0) + 0.5} stroke="rgba(255,174,58,.26)" />
        {[-12, -6, 9, 15].map((db) => (
          <text
            key={db}
            x={4}
            y={yOfDb(db) + 4}
            fontSize={11}
            fill="rgba(255,174,58,.5)"
            fontFamily='"Share Tech Mono", monospace'
          >
            {db > 0 ? `+${db}` : db}
          </text>
        ))}
        {[50, 100, 200, 500, 1000, 2000, 5000, 10000].map((f) => (
          <text
            key={f}
            x={logX(f, w)}
            y={h - 4}
            textAnchor="middle"
            fontSize={10}
            fill="rgba(255,174,58,.4)"
            fontFamily='"Share Tech Mono", monospace'
          >
            {fmtHz(f)}
          </text>
        ))}
        <path
          d={d}
          fill="none"
          stroke={color.value}
          strokeWidth={2.2}
          style={{ filter: "drop-shadow(0 0 5px rgba(255,138,0,.65))" }}
        />
        {edges.map((e) => (
          <EdgeNode key={e.kind} edge={e} w={w} plotH={plotH} yOfDb={yOfDb} />
        ))}
        {bands.map((b) => (
          <EqNode
            key={b.n}
            band={b}
            selected={selected === b.n}
            onSelect={onSelect}
            onPopout={onPopout}
            w={w}
            plotH={plotH}
            yOfDb={yOfDb}
            dbOfY={dbOfY}
          />
        ))}
        {children}
      </svg>
    </Scope>
  );
}

/** Floating "surgical" window over the whole faceplate. Esc or a backdrop
 *  click closes it. */
export function StudioDialog({
  title,
  subtitle,
  onClose,
  children,
  width = 1160,
  height = 620,
}: {
  title: string;
  subtitle?: string;
  onClose: () => void;
  children: ReactNode;
  width?: number;
  height?: number;
}) {
  useEffect(() => {
    const key = (e: globalThis.KeyboardEvent) => {
      if (e.key === "Escape") onClose();
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, [onClose]);
  const shell: CSSProperties = { width, height };
  return (
    <div
      className="cs-dialog-scrim"
      onPointerDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div className="cs-dialog" role="dialog" aria-modal="true" aria-label={title} style={shell}>
        <header className="cs-dialog-head">
          <div style={{ display: "flex", flexDirection: "column", gap: 2, minWidth: 0 }}>
            <span className="cs-wordmark" style={{ fontSize: 18 }}>
              {title}
            </span>
            {subtitle && <span className="cs-sub">{subtitle}</span>}
          </div>
          <span style={{ flexGrow: 1 }} />
          <button
            type="button"
            className="cs-key"
            onClick={onClose}
            aria-label="Close detail window"
          >
            Close ✕
          </button>
        </header>
        <div className="cs-dialog-body">{children}</div>
      </div>
    </div>
  );
}

/* ------------------------------------------------------------ compressor */
/** Input -> output curve of the strip compressor (soft knee, range cap), with
 *  the live input level and gain reduction marked. */
export function CompCurve({
  threshold,
  ratio,
  knee,
  range,
  width,
  height,
  liveIn,
  gr,
}: {
  threshold: number;
  ratio: number;
  knee: number;
  range: number;
  width: number;
  height: number;
  liveIn?: number;
  gr?: number;
}) {
  const pad = 22,
    w = width,
    h = height;
  const span = 42; // dB shown below/above 0
  const xOf = (db: number) => pad + ((db + span) / (span + 6)) * (w - pad * 2);
  const yOf = (db: number) => h - pad - ((db + span) / (span + 6)) * (h - pad * 2);
  const k = Math.max(0.1, knee);
  const curve = (db: number) => {
    const over = db - threshold;
    let y: number;
    if (over <= -k / 2) y = db;
    else if (over >= k / 2) y = threshold + over / Math.max(1.001, ratio);
    else {
      const t = over + k / 2;
      y = db + ((1 / Math.max(1.001, ratio) - 1) * (t * t)) / (2 * k);
    }
    return Math.max(threshold - range, y);
  };
  let d = "";
  for (let db = -span; db <= 6; db += 0.5)
    d += `${db > -span ? "L" : "M"}${xOf(db).toFixed(1)} ${yOf(curve(db)).toFixed(1)}`;
  const inDb =
    liveIn !== undefined && liveIn > 0 ? Math.max(-span, 20 * Math.log10(liveIn)) : undefined;
  return (
    <svg
      width={w}
      height={h}
      role="img"
      aria-label="Compressor input to output transfer curve"
      style={{ display: "block" }}
    >
      <rect x={0} y={0} width={w} height={h} fill="transparent" />
      {[-36, -24, -12, 0].map((db) => (
        <g key={db}>
          <line
            x1={xOf(db)}
            x2={xOf(db)}
            y1={pad / 2}
            y2={h - pad / 2}
            stroke="rgba(255,174,58,.07)"
          />
          <line
            x1={pad / 2}
            x2={w - pad / 2}
            y1={yOf(db)}
            y2={yOf(db)}
            stroke="rgba(255,174,58,.07)"
          />
          <text
            x={xOf(db)}
            y={h - 5}
            textAnchor="middle"
            fontSize={10}
            fill="rgba(255,174,58,.45)"
            fontFamily='"Share Tech Mono", monospace'
          >
            {db}
          </text>
          <text
            x={4}
            y={yOf(db) + 4}
            fontSize={10}
            fill="rgba(255,174,58,.45)"
            fontFamily='"Share Tech Mono", monospace'
          >
            {db}
          </text>
        </g>
      ))}
      <line
        x1={xOf(-span)}
        y1={yOf(-span)}
        x2={xOf(6)}
        y2={yOf(6)}
        stroke="rgba(255,243,220,.22)"
        strokeDasharray="4 4"
      />
      <line
        x1={xOf(threshold)}
        x2={xOf(threshold)}
        y1={pad / 2}
        y2={h - pad / 2}
        stroke="rgba(60,214,221,.4)"
        strokeDasharray="3 4"
      />
      <path
        d={d}
        fill="none"
        stroke={system.dynamics}
        strokeWidth={2.2}
        style={{ filter: "drop-shadow(0 0 5px rgba(255,90,74,.55))" }}
      />
      {inDb !== undefined && (
        <>
          <line
            x1={xOf(inDb)}
            x2={xOf(inDb)}
            y1={yOf(inDb)}
            y2={yOf(curve(inDb))}
            stroke="rgba(60,214,221,.55)"
            strokeWidth={2}
          />
          <circle cx={xOf(inDb)} cy={yOf(inDb)} r={4} fill="#ffcf8a" />
          <circle cx={xOf(inDb)} cy={yOf(curve(inDb))} r={4} fill={system.dynamics} />
        </>
      )}
      {gr !== undefined && gr > 0.05 && (
        <text
          x={w - 8}
          y={16}
          textAnchor="end"
          fontSize={11}
          fill={system.dynamics}
          fontFamily='"Share Tech Mono", monospace'
        >{`GR −${gr.toFixed(1)} dB`}</text>
      )}
      <text
        x={8}
        y={16}
        fontSize={10}
        fill="rgba(255,174,58,.5)"
        fontFamily='"Share Tech Mono", monospace'
      >
        OUT
      </text>
      <text
        x={w - 8}
        y={h - 5}
        textAnchor="end"
        fontSize={10}
        fill="rgba(255,174,58,.5)"
        fontFamily='"Share Tech Mono", monospace'
      >
        IN
      </text>
    </svg>
  );
}

/* ------------------------------------------------------------ saturation */
const satShape = (x: number, mode: number) => {
  switch (mode) {
    case 1:
      return Math.tanh(x * 0.85 + 0.12 * x * x);
    case 2:
      return (Math.tanh(x) + 0.5 * Math.tanh(x * 0.5)) / 1.5;
    default:
      return Math.tanh(x);
  }
};

/** Transfer curve of the saturator, including bias and drive, normalized the
 *  same way the DSP normalizes it (unity small-signal gain). */
export function SatCurve({
  mode,
  driveDb,
  bias,
  width,
  height,
}: {
  mode: number;
  driveDb: number;
  bias: number;
  width: number;
  height: number;
}) {
  const pad = 18,
    w = width,
    h = height;
  const pre = Math.pow(10, driveDb / 20),
    post = 1 / Math.max(1, Math.sqrt(pre));
  const X = (x: number) => pad + ((x + 1) / 2) * (w - pad * 2);
  const Y = (y: number) =>
    h - pad - ((Math.max(-1.25, Math.min(1.25, y)) + 1.25) / 2.5) * (h - pad * 2);
  let d = "";
  for (let i = 0; i <= 240; i++) {
    const x = -1 + (2 * i) / 240;
    const y = satShape(x * pre + bias, mode) * post;
    d += `${i ? "L" : "M"}${X(x).toFixed(1)} ${Y(y).toFixed(1)}`;
  }
  return (
    <svg
      width={w}
      height={h}
      role="img"
      aria-label="Saturation transfer curve"
      style={{ display: "block" }}
    >
      {[-0.5, 0.5].map((v) => (
        <g key={v}>
          <line x1={X(v)} x2={X(v)} y1={pad / 2} y2={h - pad / 2} stroke="rgba(255,174,58,.07)" />
          <line x1={pad / 2} x2={w - pad / 2} y1={Y(v)} y2={Y(v)} stroke="rgba(255,174,58,.07)" />
        </g>
      ))}
      <line x1={X(0)} x2={X(0)} y1={pad / 2} y2={h - pad / 2} stroke="rgba(255,174,58,.25)" />
      <line x1={pad / 2} x2={w - pad / 2} y1={Y(0)} y2={Y(0)} stroke="rgba(255,174,58,.25)" />
      <line
        x1={X(-1)}
        y1={Y(-1)}
        x2={X(1)}
        y2={Y(1)}
        stroke="rgba(255,243,220,.22)"
        strokeDasharray="4 4"
      />
      <path
        d={d}
        fill="none"
        stroke={system.colour}
        strokeWidth={2.2}
        style={{ filter: "drop-shadow(0 0 5px rgba(199,155,255,.55))" }}
      />
      <text
        x={w - 8}
        y={h - 5}
        textAnchor="end"
        fontSize={10}
        fill="rgba(255,174,58,.5)"
        fontFamily='"Share Tech Mono", monospace'
      >
        IN
      </text>
      <text
        x={8}
        y={15}
        fontSize={10}
        fill="rgba(255,174,58,.5)"
        fontFamily='"Share Tech Mono", monospace'
      >
        OUT
      </text>
    </svg>
  );
}

/** Compact band-response inset for the surgical band window. */
export function BandCurveInset({
  band,
  width,
  height,
}: {
  band: { shape: StripShape; freq: number; gain: number; q: number; slope: number };
  width: number;
  height: number;
}) {
  const w = width,
    h = height,
    pad = 10;
  const lo = Math.log10(20),
    hi = Math.log10(20000);
  const X = (f: number) =>
    pad + ((Math.log10(Math.max(20, Math.min(20000, f))) - lo) / (hi - lo)) * (w - pad * 2);
  const Y = (db: number) =>
    h / 2 - (Math.max(-EQ_RANGE, Math.min(EQ_RANGE, db)) * (h / 2 - pad)) / EQ_RANGE;
  let shape = "";
  for (let x = pad; x <= w - pad; x += 2) {
    const f = Math.pow(10, lo + ((x - pad) / (w - pad * 2)) * (hi - lo));
    shape += `${x > pad ? "L" : "M"}${x.toFixed(1)} ${Y(stripResponseDb([band], f)).toFixed(1)}`;
  }
  return (
    <svg
      width={w}
      height={h}
      role="img"
      aria-label="Selected band response"
      style={{ display: "block" }}
    >
      <line x1={pad} x2={w - pad} y1={Y(0)} y2={Y(0)} stroke="rgba(255,174,58,.25)" />
      <line
        x1={X(band.freq)}
        x2={X(band.freq)}
        y1={pad}
        y2={h - pad}
        stroke="rgba(255,174,58,.18)"
        strokeDasharray="3 3"
      />
      <path
        d={shape}
        fill="none"
        stroke={color.value}
        strokeWidth={2.2}
        style={{ filter: "drop-shadow(0 0 5px rgba(255,138,0,.6))" }}
      />
      <text
        x={X(band.freq) + 4}
        y={12}
        fontSize={10}
        fill="rgba(255,174,58,.75)"
        fontFamily='"Share Tech Mono", monospace'
      >
        {fmtHz(band.freq)} Hz
      </text>
    </svg>
  );
}
