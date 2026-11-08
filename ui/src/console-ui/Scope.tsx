// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useEffect, useRef, type CSSProperties, type ReactNode } from "react";
import { drawGrid, type Renderer } from "./renderers/common";
import { FRAME_MS } from "./tokens";

export interface ScopeProps<P, S, F> {
  renderer: Renderer<P, S, F>;
  params: P;
  width: number;
  height: number;
  /** Pull live analysis data once per frame (spectrum, GR, samples). Read
   *  from a ref/shared buffer; never pass React state here. Omit for the
   *  synthesised preview. */
  getFrame?: () => F | undefined;
  /** Accessible description of what the display shows. */
  label: string;
  /** Pause drawing (e.g. editor hidden). */
  paused?: boolean;
  children?: ReactNode;
  style?: CSSProperties;
}

/**
 * DPR-aware canvas visualiser. Its rAF loop is throttled to ~30 fps and never
 * touches React state, so audio-rate data can't cause re-renders, and the UI
 * can never block the audio thread (it only ever reads snapshots).
 */
export function Scope<P, S, F>({
  renderer,
  params,
  width,
  height,
  getFrame,
  label,
  paused = false,
  children,
  style,
}: ScopeProps<P, S, F>) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const live = useRef({ renderer, params, getFrame });
  live.current = { renderer, params, getFrame };
  const state = useRef<S>((renderer.init?.() ?? {}) as S);
  const rendererRef = useRef(renderer);
  if (rendererRef.current !== renderer) {
    rendererRef.current = renderer;
    state.current = (renderer.init?.() ?? {}) as S;
  }

  const cw = width - 6,
    ch = height - 6;

  useEffect(() => {
    if (paused) return;
    let raf = 0;
    let last = 0;
    const t0 = performance.now();
    const tick = (ts: number) => {
      raf = requestAnimationFrame(tick);
      if (ts - last < FRAME_MS) return;
      last = ts;
      const c = canvas.current;
      const g = c?.getContext("2d");
      if (!c || !g) return;
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      const pw = Math.round(cw * dpr),
        ph = Math.round(ch * dpr);
      if (c.width !== pw || c.height !== ph) {
        c.width = pw;
        c.height = ph;
      }
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, cw, ch);
      const { renderer: r, params: p, getFrame: gf } = live.current;
      if (r.grid && r.grid !== "none") drawGrid(g, cw, ch, r.grid);
      r.draw(g, cw, ch, (ts - t0) / 1000, p, state.current, gf?.());
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [cw, ch, paused]);

  return (
    <div
      className="cs-scope"
      style={{
        position: "relative",
        width,
        height,
        boxSizing: "border-box",
        borderRadius: 6,
        padding: 3,
        background: "linear-gradient(180deg,#050506,#111113)",
        boxShadow: "inset 0 2px 6px #000, 0 1px 0 rgba(255,255,255,.07), 0 0 0 1px #000",
        ...style,
      }}
    >
      <canvas
        ref={canvas}
        role="img"
        aria-label={label}
        style={{
          display: "block",
          width: cw,
          height: ch,
          borderRadius: 4,
          background: "radial-gradient(ellipse at 50% 40%, #1d1307, #0e0903 80%)",
        }}
      />
      <span
        aria-hidden="true"
        style={{
          position: "absolute",
          inset: 3,
          borderRadius: 4,
          pointerEvents: "none",
          background:
            "linear-gradient(165deg, rgba(255,255,255,.09) 0%, rgba(255,255,255,.02) 28%, transparent 42%), repeating-linear-gradient(0deg, rgba(0,0,0,.16) 0 1px, transparent 1px 3px)",
          boxShadow: "inset 0 0 18px rgba(0,0,0,.7)",
        }}
      />
      {children}
    </div>
  );
}
