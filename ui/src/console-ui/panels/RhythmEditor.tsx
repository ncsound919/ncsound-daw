// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useRef, type KeyboardEvent, type PointerEvent, type ReactNode } from "react";
import { PCycle, Readout, useBound, useOption, useValue, useHas } from "./controls";
import { Overlay } from "./PluginFrame";

// Rhythm Echo editor (Delay, topology "Rhythm"). Up to 8 taps; each tap is two
// host parameters, rhythmPosN (0..1 of the pattern) and rhythmLvlN (0..1).
// Drag a bar sideways to move it (snaps to the grid; hold Shift for free
// placement) and up/down for its level. Keyboard: Left/Right = one grid step,
// Up/Down = 5 % level. The bar at 0 is "the One": the pattern's own repeat,
// heard only with feedback.

export const RHYTHM_MAX_TAPS = 8;
export const RHYTHM_GRID_BEATS = [1, 1 / 2, 1 / 3, 1 / 4, 1 / 6, 1 / 8] as const; // 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
export const RHYTHM_SHAPES = ["Custom", "Decay", "Reverse", "Swell", "Fade"] as const;

/** Level multiplier of the tap that is `rank`-th in time out of `count`
 *  (same curve as the native DelayProcessor::rhythmPattern). */
export function rhythmShapeGain(shape: number, rank: number, count: number): number {
  const u = count > 1 ? rank / (count - 1) : 0;
  switch (shape) {
    case 1:
      return 1 - 0.75 * u;
    case 2:
      return 0.25 + 0.75 * u;
    case 3:
      return 0.25 + 0.75 * Math.sin(u * Math.PI);
    case 4:
      return 1 - 0.75 * Math.sin(u * Math.PI);
    default:
      return 1;
  }
}

const W = 1288,
  H = 300,
  TOP = 58,
  PAD_X = 28,
  PAD_B = 26;
const PLOT_W = W - 2 * PAD_X,
  PLOT_H = H - TOP - PAD_B;
const clamp01 = (v: number) => Math.max(0, Math.min(1, v));

function TapBar({
  k,
  count,
  beats,
  stepBeats,
  rank,
  shape,
}: {
  k: number;
  count: number;
  beats: number;
  stepBeats: number;
  rank: number;
  shape: number;
}) {
  const pos = useBound(`rhythmPos${k + 1}`);
  const lvl = useBound(`rhythmLvl${k + 1}`);
  const drag = useRef<{ x0: number; y0: number; p0: number; l0: number } | null>(null);
  if (!pos || !lvl || k >= count) return null;

  const steps = Math.max(1, Math.round(beats / stepBeats));
  const snap = (p: number, free: boolean) => clamp01(free ? p : Math.round(p * steps) / steps);
  const x = PAD_X + pos.value * PLOT_W;
  const h = lvl.value * PLOT_H;
  const eff = lvl.value * rhythmShapeGain(shape, rank, count);
  const stepPx = PLOT_W / steps;
  const bw = Math.max(14, Math.min(26, stepPx - 6));

  const down = (e: PointerEvent<HTMLDivElement>) => {
    e.currentTarget.setPointerCapture?.(e.pointerId);
    drag.current = { x0: e.clientX, y0: e.clientY, p0: pos.value, l0: lvl.value };
    pos.onGesture("start");
    lvl.onGesture("start");
  };
  const move = (e: PointerEvent<HTMLDivElement>) => {
    const d = drag.current;
    if (!d) return;
    // The panel is scaled by the host: convert screen pixels to panel pixels.
    const scale = e.currentTarget.getBoundingClientRect().width / bw || 1;
    const np = snap(d.p0 + (e.clientX - d.x0) / scale / PLOT_W, e.shiftKey);
    const nl = clamp01(d.l0 - (e.clientY - d.y0) / scale / PLOT_H);
    if (np !== pos.value) pos.onChange(np);
    if (nl !== lvl.value) lvl.onChange(nl);
  };
  const up = () => {
    if (drag.current) {
      drag.current = null;
      pos.onGesture("end");
      lvl.onGesture("end");
    }
  };
  const key = (e: KeyboardEvent<HTMLDivElement>) => {
    const step = 1 / steps;
    let np = pos.value,
      nl = lvl.value;
    if (e.key === "ArrowLeft") np = snap(pos.value - step, false);
    else if (e.key === "ArrowRight") np = snap(pos.value + step, false);
    else if (e.key === "ArrowUp") nl = clamp01(lvl.value + 0.05);
    else if (e.key === "ArrowDown") nl = clamp01(lvl.value - 0.05);
    else return;
    e.preventDefault();
    if (np !== pos.value) {
      pos.onGesture("start");
      pos.onChange(np);
      pos.onGesture("end");
    }
    if (nl !== lvl.value) {
      lvl.onGesture("start");
      lvl.onChange(nl);
      lvl.onGesture("end");
    }
  };

  const beatsAt = pos.value * beats;
  return (
    <div
      role="slider"
      tabIndex={0}
      aria-label={`Tap ${k + 1}`}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={Math.round(lvl.value * 100)}
      aria-valuetext={`beat ${(beatsAt + 1).toFixed(2)} of ${beats}, level ${Math.round(lvl.value * 100)}%`}
      onPointerDown={down}
      onPointerMove={move}
      onPointerUp={up}
      onPointerCancel={up}
      onKeyDown={key}
      style={{
        position: "absolute",
        left: x - bw / 2,
        top: TOP,
        width: bw,
        height: PLOT_H,
        cursor: "grab",
        touchAction: "none",
        outlineOffset: 2,
      }}
    >
      <div
        style={{
          position: "absolute",
          left: 3,
          right: 3,
          bottom: 0,
          height: Math.max(3, h),
          borderRadius: 3,
          background: "linear-gradient(180deg,#ffd08a,#ff8a00)",
          boxShadow: "0 0 12px rgba(255,138,0,.55)",
        }}
      />
      {shape !== 0 && (
        <div
          title="Level after Shape"
          style={{
            position: "absolute",
            left: 0,
            right: 0,
            bottom: eff * PLOT_H - 1,
            height: 2,
            background: "#3cd6dd",
          }}
        />
      )}
      <span
        className="cs-readout"
        style={{
          position: "absolute",
          left: "50%",
          transform: "translateX(-50%)",
          bottom: Math.max(3, h) + 4,
          fontSize: 11,
        }}
      >
        {k + 1}
      </span>
    </div>
  );
}

function OneBar() {
  const one = useBound("rhythmOne");
  const drag = useRef<{ y0: number; l0: number } | null>(null);
  if (!one) return null;
  const h = one.value * PLOT_H;
  return (
    <div
      role="slider"
      tabIndex={0}
      aria-label="The One (pattern repeat, heard with feedback)"
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={Math.round(one.value * 100)}
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture?.(e.pointerId);
        drag.current = { y0: e.clientY, l0: one.value };
        one.onGesture("start");
      }}
      onPointerMove={(e) => {
        const d = drag.current;
        if (!d) return;
        const sc = e.currentTarget.getBoundingClientRect().width / 22 || 1;
        one.onChange(clamp01(d.l0 - (e.clientY - d.y0) / sc / PLOT_H));
      }}
      onPointerUp={() => {
        if (drag.current) {
          drag.current = null;
          one.onGesture("end");
        }
      }}
      onKeyDown={(e) => {
        const nv =
          e.key === "ArrowUp" ? one.value + 0.05 : e.key === "ArrowDown" ? one.value - 0.05 : null;
        if (nv === null) return;
        e.preventDefault();
        one.onGesture("start");
        one.onChange(clamp01(nv));
        one.onGesture("end");
      }}
      style={{
        position: "absolute",
        left: PAD_X - 11,
        top: TOP,
        width: 22,
        height: PLOT_H,
        cursor: "ns-resize",
        touchAction: "none",
      }}
    >
      <div
        style={{
          position: "absolute",
          left: 4,
          right: 4,
          bottom: 0,
          height: Math.max(3, h),
          borderRadius: 3,
          background: "linear-gradient(180deg,#fff3dc,#e8c46a)",
          opacity: 0.85,
        }}
      />
      <span
        className="cs-readout"
        style={{
          position: "absolute",
          left: "50%",
          transform: "translateX(-50%)",
          bottom: Math.max(3, h) + 4,
          fontSize: 11,
        }}
      >
        1
      </span>
    </div>
  );
}

export function RhythmEditor({ right }: { right?: ReactNode }) {
  const beats = 1 + useOption("rhythmLength");
  const stepBeats =
    RHYTHM_GRID_BEATS[Math.min(RHYTHM_GRID_BEATS.length - 1, useOption("rhythmGrid"))];
  const count = 1 + useOption("rhythmTaps");
  const shape = useOption("rhythmShape");
  const fb = useValue("feedback");
  const hasFb = useHas("feedback");
  // Rank of each sounding tap in time order (for the shape marker).
  const p = [
    useValue("rhythmPos1"),
    useValue("rhythmPos2"),
    useValue("rhythmPos3"),
    useValue("rhythmPos4"),
    useValue("rhythmPos5"),
    useValue("rhythmPos6"),
    useValue("rhythmPos7"),
    useValue("rhythmPos8"),
  ];
  const order = p
    .slice(0, count)
    .map((v, i) => ({ v, i }))
    .sort((a, b) => a.v - b.v || a.i - b.i);
  const rankOf = (k: number) => order.findIndex((o) => o.i === k);

  const steps = Math.max(1, Math.round(beats / stepBeats));
  const lines = Array.from({ length: steps + 1 }, (_, i) => {
    const b = (i * beats) / steps;
    return {
      x: PAD_X + (i / steps) * PLOT_W,
      beat: Math.abs(b - Math.round(b)) < 1e-6,
      label: Math.round(b) + 1,
    };
  });

  return (
    <div
      className="cs-scope"
      style={{
        position: "relative",
        width: W,
        height: H,
        boxSizing: "border-box",
        borderRadius: 6,
        background: "radial-gradient(ellipse at 50% 40%, #1d1307, #0e0903 80%)",
        boxShadow: "inset 0 2px 6px #000, 0 0 0 1px #000",
        overflow: "hidden",
      }}
      role="group"
      aria-label={`Rhythm echo pattern, ${beats} beats, ${count} taps`}
    >
      <svg width={W} height={H} style={{ position: "absolute", inset: 0 }} aria-hidden="true">
        {lines.map((l, i) => (
          <g key={i}>
            <line
              x1={l.x + 0.5}
              x2={l.x + 0.5}
              y1={TOP}
              y2={TOP + PLOT_H}
              stroke={l.beat ? "rgba(255,174,58,.35)" : "rgba(255,174,58,.16)"}
            />
            {l.beat && i < steps && (
              <text
                x={l.x + 4}
                y={H - 8}
                fill="rgba(255,174,58,.6)"
                fontSize={11}
                fontFamily='"Share Tech Mono", monospace'
              >
                {l.label}
              </text>
            )}
          </g>
        ))}
        <line
          x1={PAD_X}
          x2={PAD_X + PLOT_W}
          y1={TOP + PLOT_H + 0.5}
          y2={TOP + PLOT_H + 0.5}
          stroke="rgba(255,174,58,.35)"
        />
      </svg>
      <Overlay
        interactive
        style={{ left: 14, right: 14, top: 10, display: "flex", gap: 8, alignItems: "center" }}
      >
        <Readout size={12} style={{ letterSpacing: ".12em", marginRight: 6 }}>
          RHYTHM
        </Readout>
        <PCycle k="rhythmLength" />
        <PCycle k="rhythmGrid" />
        <Readout size={11} style={{ opacity: 0.75 }}>
          TAPS
        </Readout>
        <PCycle k="rhythmTaps" />
        <PCycle k="rhythmShape" />
        <PCycle k="rhythmPan" />
        <span style={{ flexGrow: 1 }} />
        <Readout size={11} style={{ opacity: 0.75 }}>
          {hasFb && fb > 0.001 ? "PATTERN REPEATS" : "NO REPEAT · FEEDBACK 0"}
        </Readout>
        {right}
      </Overlay>
      <OneBar />
      {Array.from({ length: RHYTHM_MAX_TAPS }, (_, k) => (
        <TapBar
          key={k}
          k={k}
          count={count}
          beats={beats}
          stepBeats={stepBeats}
          rank={Math.max(0, rankOf(k))}
          shape={shape}
        />
      ))}
    </div>
  );
}
