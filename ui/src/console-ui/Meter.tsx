// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useEffect, useRef } from "react";
import { FRAME_MS } from "./tokens";

export interface MeterProps {
  /** Called once per animation frame; return level 0..1. Read from a ref or
   *  a shared buffer — never React state — so audio-rate data never re-renders. */
  read: () => number;
  segments?: number;
  vertical?: boolean;
  /** "level" = green/yellow/red ladder, "gr" = amber gain-reduction bar. */
  kind?: "level" | "gr";
  thickness?: number;
  length?: number;
  label?: string;
}

/** LED ladder driven by requestAnimationFrame with direct class updates. */
export function Meter({ read, segments = 18, vertical = false, kind = "level", thickness = 8, length, label }: MeterProps) {
  const root = useRef<HTMLDivElement>(null);
  const readRef = useRef(read);
  readRef.current = read;

  useEffect(() => {
    let raf = 0;
    let last = 0;
    let lit = -1;
    const tick = (ts: number) => {
      raf = requestAnimationFrame(tick);
      if (ts - last < FRAME_MS) return;
      last = ts;
      const el = root.current;
      if (!el) return;
      const n = Math.round(Math.max(0, Math.min(1, readRef.current())) * segments);
      if (n === lit) return;
      lit = n;
      const cells = el.children;
      for (let i = 0; i < cells.length; i++) {
        const on = i < n;
        const cls = !on ? "" : kind === "gr" ? "cs-lit-gr" : i >= segments - 2 ? "cs-lit-high" : i >= segments - 5 ? "cs-lit-mid" : "cs-lit-low";
        (cells[i] as HTMLElement).className = cls;
      }
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [segments, kind]);

  const style = vertical ? { width: thickness, height: length } : { height: thickness, width: length };
  return (
    <div ref={root} className={`cs-meter${vertical ? " cs-meter-v" : ""}`} style={style} role="meter" aria-label={label ?? "Level"} aria-valuemin={0} aria-valuemax={1}>
      {Array.from({ length: segments }, (_, i) => <i key={i} />)}
    </div>
  );
}
