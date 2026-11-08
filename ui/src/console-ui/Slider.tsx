// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useCallback, useRef, useState, type KeyboardEvent, type PointerEvent, type WheelEvent } from "react";
import { dragStep, keyStep, wheelStep } from "./interaction";
import type { Gesture } from "./Knob";
import { clamp01, formatValue, type ParamSpec } from "./param";

export interface SliderProps {
  label: string;
  value?: number;
  defaultValue?: number;
  onChange?: (value: number) => void;
  onGesture?: (phase: Gesture) => void;
  spec?: ParamSpec;
  /** Track width in px (default 220). */
  width?: number;
  bipolar?: boolean;
  off?: boolean;
  /** Fill colour override, e.g. the modulation cyan for sidechain/duck controls. */
  accent?: string;
  id?: string;
}

const THUMB = 16;

export function Slider({
  label, value, defaultValue = 0.5, onChange, onGesture, spec = {}, width = 220, bipolar = false, off = false, accent, id,
}: SliderProps) {
  const [inner, setInner] = useState(value ?? defaultValue);
  const v = value ?? inner;
  const [hover, setHover] = useState(false);
  const [drag, setDrag] = useState(false);
  const [fine, setFine] = useState(false);
  const acc = useRef(v);
  const lastX = useRef(0);
  const dragging = useRef(false);
  const travel = width - THUMB;

  const commit = useCallback((nv: number) => {
    acc.current = nv;
    if (value === undefined) setInner(nv);
    onChange?.(nv);
  }, [value, onChange]);
  const once = (nv: number) => { onGesture?.("start"); commit(nv); onGesture?.("end"); };

  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture?.(e.pointerId);
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    onGesture?.("start");
    dragging.current = true;
    setDrag(true);
    lastX.current = e.clientX;
    // Clicking the groove jumps; grabbing the thumb keeps a relative drag.
    const onThumb = Math.abs(x - (v * travel + THUMB / 2)) <= THUMB;
    if (onThumb) acc.current = v;
    else commit(clamp01((x - THUMB / 2) / travel));
  };
  const onPointerMove = (e: PointerEvent<HTMLDivElement>) => {
    if (!dragging.current) return;
    const dx = e.clientX - lastX.current;
    lastX.current = e.clientX;
    setFine(e.shiftKey);
    if (dx !== 0) commit(dragStep(acc.current, dx, e.shiftKey, travel));
  };
  const endDrag = (e: PointerEvent<HTMLDivElement>) => {
    if (!dragging.current) return;
    dragging.current = false;
    e.currentTarget.releasePointerCapture?.(e.pointerId);
    setDrag(false);
    setFine(false);
    onGesture?.("end");
  };

  const text = formatValue(v, spec);
  const tx = Math.round(v * travel);
  const mid = Math.round(width / 2);
  const cx = tx + THUMB / 2;
  const left = bipolar ? Math.min(mid, cx) : 0;
  const fillW = bipolar ? Math.abs(cx - mid) : cx;
  const active = hover || drag;

  return (
    <div className={`cs-slider${active ? " cs-active" : ""}${off ? " cs-off" : ""}`} style={{ width }}>
      <div className="cs-slider-head">
        <span className="cs-slider-label">{label}</span>
        <span className="cs-slider-val">{text}{drag && fine ? "  FINE" : ""}</span>
      </div>
      <div
        id={id}
        className="cs-slider-hit"
        style={{ width }}
        role="slider"
        tabIndex={0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(v * 100)}
        aria-valuetext={text}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onPointerEnter={() => setHover(true)}
        onPointerLeave={() => setHover(false)}
        onDoubleClick={() => once(defaultValue)}
        onWheel={(e: WheelEvent<HTMLDivElement>) => once(wheelStep(v, e.deltaY, e.shiftKey))}
        onKeyDown={(e: KeyboardEvent<HTMLDivElement>) => {
          const nv = keyStep(v, e.key, e.shiftKey, defaultValue);
          if (nv === null) return;
          e.preventDefault();
          once(nv);
        }}
      >
        <span className="cs-slider-groove" />
        <span className="cs-slider-fill" style={{ left, width: fillW, background: off ? "#555" : accent }} />
        {bipolar && <span className="cs-slider-zero" style={{ left: mid }} />}
        <span className="cs-slider-thumb" style={{ left: tx }} />
      </div>
    </div>
  );
}
