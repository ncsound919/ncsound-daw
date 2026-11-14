// Local to this app (not generated from console-ui): the Master panel's
// time-history plots are drawn by uPlot (MIT) instead of bespoke canvas code.
import { useEffect, useRef } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { FRAME_MS } from "./tokens";

const AXIS = "rgba(255,174,58,.5)";
const GRID = "rgba(255,174,58,.08)";
const CYAN = "#3cd6dd";
const MONO = '11px "Share Tech Mono", monospace';

export interface TimePlotProps {
  width: number;
  height: number;
  /** Accessible description of what the plot shows. */
  label: string;
  /** Line colour; the same amber the console renderers use. */
  color: string;
  /** Shade the area under the line down to the bottom of `y`. */
  fill?: boolean;
  /** Seconds of history kept on screen. */
  seconds: number;
  /** Fixed vertical range in the value's own unit. */
  y: readonly [number, number];
  /** Dashed horizontal reference line (e.g. the LUFS target). */
  refLine?: number;
  /** Corner caption for the reference line. */
  refText?: string;
  /** Latest value, pulled once per frame. Omit with no host attached: then the
   *  plot keeps its axes and reference line but draws no trace. */
  read?: () => number | undefined;
}

/**
 * Rolling single-trace plot on uPlot. The value arrives through `read` once per
 * animation frame and never passes through React state (same contract as
 * <Scope>), but axes, ticks and the trace are uPlot's rather than hand-rolled.
 */
export function TimePlot({
  width,
  height,
  label,
  color,
  fill,
  seconds,
  y,
  refLine,
  refText,
  read,
}: TimePlotProps) {
  const host = useRef<HTMLDivElement>(null);
  const live = useRef({ read, refLine });
  live.current = { read, refLine };
  const yLo = y[0],
    yHi = y[1];
  const pw = width - 6,
    ph = height - 6;

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const ref = () => live.current.refLine ?? null;

    /** Two points spanning the window: axes plus the reference line, no trace. */
    const blank = (): uPlot.AlignedData => {
      const x = [-seconds, 0];
      return [x, [null, null], x.map(ref)];
    };

    const plot = new uPlot(
      {
        width: pw,
        height: ph,
        padding: [2, 8, 0, 0],
        legend: { show: false },
        cursor: { show: false },
        scales: {
          x: { time: false, range: () => [-seconds, 0] },
          y: { auto: false, range: [yLo, yHi] },
        },
        axes: [
          {
            stroke: AXIS,
            font: MONO,
            size: 20,
            gap: 4,
            space: 64,
            grid: { stroke: GRID, width: 1 },
            ticks: { stroke: AXIS, width: 1, size: 3 },
            values: (_u, splits) => splits.map((v) => (v === 0 ? "now" : `${v}s`)),
          },
          {
            stroke: AXIS,
            font: MONO,
            size: 32,
            gap: 4,
            grid: { stroke: GRID, width: 1 },
            ticks: { stroke: AXIS, width: 1, size: 3 },
            values: (_u, splits) => splits.map((v) => String(v)),
          },
        ],
        series: [
          {},
          {
            stroke: color,
            width: 1.5,
            fillTo: yLo,
            points: { show: false },
            fill: fill
              ? (u: uPlot) => {
                  const g = u.ctx.createLinearGradient(0, 0, 0, u.bbox.height);
                  g.addColorStop(0, "rgba(255,138,0,.28)");
                  g.addColorStop(1, "rgba(255,138,0,0)");
                  return g;
                }
              : undefined,
          },
          // Always allocated so the target can move without rebuilding the chart.
          { stroke: CYAN, width: 1, dash: [5, 4], points: { show: false } },
        ],
        hooks: {
          // uPlot has no shadow option: restroke the series path with the amber
          // glow the canvas renderers use.
          drawSeries: [
            (u, seriesIdx) => {
              if (seriesIdx !== 1) return;
              const [xs, ys] = [u.data[0], u.data[1]];
              const ctx = u.ctx;
              ctx.save();
              ctx.beginPath();
              let connected = false;
              for (let i = 0; i < xs.length; i++) {
                const x = xs[i],
                  y = ys[i];
                if (x == null || y == null) {
                  connected = false;
                  continue;
                }
                const px = u.valToPos(x, "x", true),
                  py = u.valToPos(y, "y", true);
                if (connected) ctx.lineTo(px, py);
                else {
                  ctx.moveTo(px, py);
                  connected = true;
                }
              }
              ctx.strokeStyle = color;
              ctx.lineWidth = 2 * uPlot.pxRatio;
              ctx.shadowColor = "#ff8a00";
              ctx.shadowBlur = 8 * uPlot.pxRatio;
              ctx.stroke();
              ctx.restore();
            },
          ],
        },
      },
      blank(),
      el,
    );

    const cap = Math.max(2, Math.ceil((seconds * 1000) / FRAME_MS) + 1);
    const xs: number[] = [];
    const ys: (number | null)[] = [];
    let raf = 0;
    let last = 0;

    const paint = () => {
      if (!xs.length) {
        plot.setData(blank());
        return;
      }
      const x = xs.map((t) => t - xs[xs.length - 1]);
      plot.setData([x, ys.slice(), x.map(ref)]);
    };

    const tick = (ts: number) => {
      raf = requestAnimationFrame(tick);
      if (ts - last < FRAME_MS) return;
      last = ts;
      const value = live.current.read?.();
      if (value !== undefined) {
        xs.push(ts / 1000);
        ys.push(Number.isFinite(value) ? value : null);
        if (xs.length > cap) {
          xs.shift();
          ys.shift();
        }
      }
      paint();
    };
    raf = requestAnimationFrame(tick);
    return () => {
      cancelAnimationFrame(raf);
      plot.destroy();
    };
  }, [pw, ph, seconds, yLo, yHi, color, fill]);

  return (
    <div
      ref={host}
      role="img"
      aria-label={label}
      style={{
        position: "relative",
        width,
        height,
        boxSizing: "border-box",
        padding: 3,
        borderRadius: 6,
        overflow: "hidden",
        background: "linear-gradient(180deg,#050506,#111113)",
        boxShadow:
          "inset 0 2px 6px #000, 0 1px 0 rgba(255,255,255,.07), 0 0 0 1px #000, 0 0 8px rgba(255,138,0,.14)",
      }}
    >
      {/* Same glass finish as <Scope>: sheen over scanlines over an inset vignette. */}
      <span
        aria-hidden="true"
        data-surface-finish="scope-glass"
        style={{
          position: "absolute",
          zIndex: 2,
          inset: 3,
          borderRadius: 4,
          pointerEvents: "none",
          backgroundImage:
            "linear-gradient(165deg, rgba(255,255,255,.09) 0%, rgba(255,255,255,.02) 28%, transparent 42%), repeating-linear-gradient(0deg, rgba(0,0,0,.16) 0 1px, transparent 1px 3px)",
          boxShadow: "inset 0 0 18px rgba(0,0,0,.7)",
        }}
      />
      {refText && refLine !== undefined && (
        <span
          aria-hidden="true"
          style={{
            position: "absolute",
            top: 6,
            right: 10,
            zIndex: 3,
            pointerEvents: "none",
            font: MONO,
            fontSize: 11,
            letterSpacing: ".06em",
            color: CYAN,
            opacity: 0.85,
          }}
        >
          {refText}
        </span>
      )}
    </div>
  );
}
