import { useEffect, useRef, useState } from "react";
import type {
  AutomationCurve,
  AutomationLaneDraft,
  AutomationParam,
  AutomationPoint,
} from "../types";

const SNAP = 0.25;

const PARAMS: { id: AutomationParam; label: string }[] = [
  { id: "volume", label: "Volume" },
  { id: "pan", label: "Pan" },
];

const CURVES: { id: AutomationCurve; label: string }[] = [
  { id: "linear", label: "Linear" },
  { id: "step", label: "Step" },
  { id: "smooth", label: "Smooth" },
];

const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));
const num = (value: number | undefined) => (Number.isFinite(value) ? (value as number) : 0);
const byBeat = (a: AutomationPoint, b: AutomationPoint) => a.beat - b.beat;

/** Path in a 0..100 box, so it scales with the lane without measuring pixels. */
function lanePath(points: AutomationPoint[], lengthBeats: number, curve: AutomationCurve): string {
  if (points.length === 0) return "";
  const sorted = [...points].sort(byBeat);
  const xy = sorted.map((point) => {
    const x = (clamp(point.beat, 0, lengthBeats) / lengthBeats) * 100;
    const y = (1 - clamp(point.value, 0, 1)) * 100;
    return `${x.toFixed(2)},${y.toFixed(2)}`;
  });

  if (curve === "step") {
    let path = `M ${xy[0]}`;
    for (let i = 1; i < sorted.length; i += 1) {
      const x = ((clamp(sorted[i].beat, 0, lengthBeats) / lengthBeats) * 100).toFixed(2);
      path += ` H ${x}`;
      const y = ((1 - clamp(sorted[i].value, 0, 1)) * 100).toFixed(2);
      path += ` V ${y}`;
    }
    return path;
  }

  if (curve === "smooth" && sorted.length > 1) {
    let path = `M ${xy[0]}`;
    for (let i = 1; i < xy.length; i += 1) {
      const [x0, y0] = xy[i - 1].split(",").map(Number);
      const [x1, y1] = xy[i].split(",").map(Number);
      const mid = ((x0 + x1) / 2).toFixed(2);
      path += ` C ${mid},${y0} ${mid},${y1} ${x1.toFixed(2)},${y1.toFixed(2)}`;
    }
    return path;
  }

  return `M ${xy.join(" L ")}`;
}

export interface AutomationProps {
  track: number;
  draft: AutomationLaneDraft;
  lengthBeats?: number;
  persisted?: boolean;
  onDraft: (draft: AutomationLaneDraft) => void;
  onParam: (param: AutomationParam) => void;
}

/**
 * One automation lane, matching the native AutomationLaneView: a parameter and
 * curve selector, Clear, Undo, and a surface where a click adds a breakpoint,
 * dragging moves it and Alt-click deletes it. A drag stays local and is handed
 * to the host once, on pointer up.
 */
export function Automation({
  track,
  draft,
  lengthBeats = 16,
  persisted = false,
  onDraft,
  onParam,
}: AutomationProps) {
  const surfaceRef = useRef<HTMLDivElement | null>(null);
  const drag = useRef<{ index: number } | null>(null);
  const pendingRef = useRef<AutomationLaneDraft | null>(null);
  const [pending, setPending] = useState<AutomationLaneDraft | null>(null);
  const [undo, setUndo] = useState<AutomationPoint[][]>([]);

  // Undo belongs to one lane. Switching track or parameter must not replay a
  // breakpoint from the previous lane into the new one.
  useEffect(() => {
    setUndo([]);
  }, [track, draft.param]);

  // The drag preview wins while the gesture is live; host truth applies again
  // once it ends and the draft arrives back from the bridge.
  const shown = pending ?? draft;
  const points = [...shown.points].sort(byBeat);

  const pushUndo = (current: AutomationPoint[]) => {
    setUndo((stack) => [...stack.slice(-19), current.map((point) => ({ ...point }))]);
  };

  const commit = (next: AutomationLaneDraft, remember = true) => {
    if (remember) pushUndo(draft.points);
    pendingRef.current = null;
    setPending(null);
    onDraft(next);
  };

  const defer = (next: AutomationLaneDraft) => {
    pendingRef.current = next;
    setPending(next);
  };

  const flush = () => {
    const next = pendingRef.current;
    if (!next) return;
    pendingRef.current = null;
    setPending(null);
    onDraft(next);
  };

  const pointFrom = (event: React.PointerEvent) => {
    const box = surfaceRef.current?.getBoundingClientRect();
    const width = box?.width ?? 0;
    const height = box?.height ?? 0;
    const x = clamp(num(event.clientX) - (box?.left ?? 0), 0, width);
    const y = clamp(num(event.clientY) - (box?.top ?? 0), 0, height);
    return {
      beat: width > 0 ? (x / width) * lengthBeats : 0,
      value: height > 0 ? 1 - y / height : 0.5,
    };
  };

  const addPoint = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button > 0 || drag.current) return;
    const { beat, value } = pointFrom(event);
    commit({
      ...shown,
      points: [
        ...shown.points,
        {
          beat: clamp(Math.round(beat / SNAP) * SNAP, 0, lengthBeats),
          value: clamp(Number(value.toFixed(3)), 0, 1),
        },
      ],
    });
  };

  const onPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    const current = drag.current;
    if (!current) return;
    const { beat, value } = pointFrom(event);
    const base = pendingRef.current ?? draft;
    const next = [...base.points].sort(byBeat).map((point, index) =>
      index === current.index
        ? {
            beat: clamp(Math.round(beat / SNAP) * SNAP, 0, lengthBeats),
            value: clamp(Number(value.toFixed(3)), 0, 1),
          }
        : point,
    );
    defer({ ...base, points: next });
  };

  const endDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    if (!drag.current) return;
    drag.current = null;
    surfaceRef.current?.releasePointerCapture?.(event.pointerId);
    flush();
  };

  return (
    <div className="ws-auto">
      <header className="ws-auto-head">
        <span className="ws-auto-track">TRACK {track + 1}</span>

        <label className="ws-auto-field">
          <span>Param</span>
          <select
            value={shown.param}
            aria-label="Automation parameter"
            onChange={(event) => onParam(event.target.value as AutomationParam)}
          >
            {PARAMS.map((entry) => (
              <option key={entry.id} value={entry.id}>
                {entry.label}
              </option>
            ))}
          </select>
        </label>

        <label className="ws-auto-field">
          <span>Curve</span>
          <select
            value={shown.curve}
            aria-label="Automation curve"
            onChange={(event) =>
              commit({ ...shown, curve: event.target.value as AutomationCurve }, false)
            }
          >
            {CURVES.map((entry) => (
              <option key={entry.id} value={entry.id}>
                {entry.label}
              </option>
            ))}
          </select>
        </label>

        <button
          className="ws-btn"
          onClick={() => commit({ ...shown, points: [] })}
          disabled={shown.points.length === 0}
          title="Clear every breakpoint in this lane"
        >
          Clear
        </button>
        <button
          className="ws-btn"
          onClick={() => {
            const previous = undo[undo.length - 1];
            if (!previous) return;
            setUndo((stack) => stack.slice(0, -1));
            commit({ ...shown, points: previous }, false);
          }}
          disabled={undo.length === 0}
        >
          Undo
        </button>

        <span className="ws-spacer" />
        <span className="ws-lcd-mini">
          {points.length} POINTS · {shown.param === "volume" ? "−60 TO +6 DB" : "LEFT TO RIGHT"}
        </span>
      </header>

      <div
        className="ws-auto-surface"
        ref={surfaceRef}
        onPointerDown={addPoint}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onPointerLeave={() => {
          // A captured drag normally ends on pointer up; if the pointer leaves
          // the surface, commit what the gesture has so far instead of
          // stranding it in the local preview.
          drag.current = null;
          flush();
        }}
        role="application"
        aria-label={`${shown.param} automation, ${points.length} points`}
        title="Click adds a point · drag moves · Alt-click deletes"
      >
        <span className="ws-auto-grid" aria-hidden="true" />
        {points.length === 0 && (
          <span className="ws-auto-empty" aria-hidden="true">
            CLICK TO ADD A BREAKPOINT
          </span>
        )}
        <svg
          className="ws-auto-line"
          viewBox="0 0 100 100"
          preserveAspectRatio="none"
          aria-hidden="true"
          focusable="false"
        >
          <path d={lanePath(points, lengthBeats, shown.curve)} />
        </svg>

        {points.map((point, index) => (
          <span
            key={`${point.beat}-${index}`}
            className="ws-auto-point"
            style={{
              left: `${(point.beat / lengthBeats) * 100}%`,
              top: `${(1 - point.value) * 100}%`,
            }}
            role="button"
            tabIndex={0}
            aria-label={`Automation point at beat ${point.beat.toFixed(2)}, ${Math.round(point.value * 100)} percent`}
            title={`beat ${point.beat.toFixed(2)} · ${Math.round(point.value * 100)}% — arrows nudge, Del removes`}
            onKeyDown={(event) => {
              if (event.key === "Delete" || event.key === "Backspace") {
                event.preventDefault();
                event.stopPropagation();
                commit({ ...shown, points: points.filter((_, i) => i !== index) });
                return;
              }
              const fine = event.shiftKey ? 0.01 : 0.05;
              const beatStep = event.shiftKey ? 0.05 : 0.25;
              let next: AutomationPoint | null = null;
              if (event.key === "ArrowUp")
                next = { ...point, value: clamp(point.value + fine, 0, 1) };
              else if (event.key === "ArrowDown")
                next = { ...point, value: clamp(point.value - fine, 0, 1) };
              else if (event.key === "ArrowRight")
                next = {
                  ...point,
                  beat: clamp(Math.round((point.beat + beatStep) / SNAP) * SNAP, 0, lengthBeats),
                };
              else if (event.key === "ArrowLeft")
                next = {
                  ...point,
                  beat: clamp(Math.round((point.beat - beatStep) / SNAP) * SNAP, 0, lengthBeats),
                };
              else return;
              event.preventDefault();
              event.stopPropagation();
              commit({
                ...shown,
                points: points
                  .map((entry, i) => (i === index ? (next as AutomationPoint) : entry))
                  .sort(byBeat),
              });
            }}
            onPointerDown={(event) => {
              event.stopPropagation();
              if (event.altKey) {
                commit({ ...shown, points: points.filter((_, i) => i !== index) });
                return;
              }
              drag.current = { index };
              pendingRef.current = { ...shown, points };
              setPending({ ...shown, points });
              surfaceRef.current?.setPointerCapture?.(event.pointerId);
            }}
          />
        ))}
      </div>

      <footer className="ws-auto-foot">
        {persisted
          ? shown.param === "volume"
            ? "LANE WRITES THE TRACK FADER · 0 = −60 dB, CENTRE = UNITY, TOP = +6 dB"
            : "LANE WRITES THE TRACK PAN · LEFT · CENTRE · RIGHT"
          : "DRAFT LANE · BREAKPOINTS FOLLOW THE SELECTED TRACK"}
      </footer>
    </div>
  );
}
