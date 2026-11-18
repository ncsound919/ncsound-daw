// GENERATED from console-ui — edit there, then run `npm run sync`.
import {
  useCallback,
  useRef,
  useState,
  type CSSProperties,
  type KeyboardEvent,
  type PointerEvent,
  type WheelEvent,
} from "react";
import { dragStep, keyStep, knobAngles, modRange, wheelStep } from "./interaction";
import { formatValue, parseValue, toNorm, toValue, type ParamSpec } from "./param";
import { HIT_PAD, color, knobSize, type KnobSize } from "./tokens";

export type Gesture = "start" | "end";

export interface KnobProps {
  label: string;
  /** Normalised 0..1. Omit for an uncontrolled knob seeded from defaultValue. */
  value?: number;
  /** Double-click / Delete target, normalised. */
  defaultValue?: number;
  onChange?: (value: number) => void;
  /** Host automation gesture bracket (JUCE beginChangeGesture/endChangeGesture). */
  onGesture?: (phase: Gesture) => void;
  spec?: ParamSpec;
  size?: KnobSize;
  /** Arc fills from 12 o'clock (pan, gain, detune). */
  bipolar?: boolean;
  /** Modulation depth 0..1; draws the cyan swing ring. */
  modDepth?: number;
  modSource?: string;
  /** Seconds per swing of the ring's dot (visual only). */
  modPeriod?: number;
  /** Bypassed / inactive: dimmed but still editable. */
  off?: boolean;
  /** Always show the value under the label (use on the one hero knob per module). */
  showValue?: boolean;
  /** Outline the invisible hit area (design review aid). */
  debugHit?: boolean;
  id?: string;
  /** Double-click opens an inline number editor instead of resetting. */
  editable?: boolean;
  /** Right-click hook (per-parameter menu). Receives client coordinates. */
  onContextMenu?: (x: number, y: number) => void;
  /** Native tooltip on the control (falls back to the label). */
  title?: string;
}

export function Knob({
  label,
  value,
  defaultValue = 0.5,
  onChange,
  onGesture,
  spec = {},
  size = "m",
  bipolar = false,
  modDepth = 0,
  modSource = "Mod",
  modPeriod = 1.6,
  off = false,
  showValue = false,
  debugHit = false,
  id,
  editable = false,
  onContextMenu,
  title,
}: KnobProps) {
  const [inner, setInner] = useState(value ?? defaultValue);
  const v = value ?? inner;
  const [hover, setHover] = useState(false);
  const [drag, setDrag] = useState(false);
  const [fine, setFine] = useState(false);
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState("");
  const acc = useRef(v);
  const lastY = useRef(0);
  const dragging = useRef(false);

  const commit = useCallback(
    (nv: number) => {
      acc.current = nv;
      if (value === undefined) setInner(nv);
      onChange?.(nv);
    },
    [value, onChange],
  );

  const once = (nv: number) => {
    onGesture?.("start");
    commit(nv);
    onGesture?.("end");
  };

  // Enter and blur can both fire for one edit; commit only once.
  const editOpen = useRef(false);
  const commitEdit = () => {
    if (!editOpen.current) return;
    editOpen.current = false;
    setEditing(false);
    const parsed = parseValue(draft, spec);
    if (Number.isNaN(parsed)) return;
    if (spec.options) {
      once(toNorm(parsed, spec));
      return;
    }
    const min = spec.min ?? 0;
    const max = spec.max ?? 1;
    once(toNorm(Math.min(max, Math.max(min, parsed)), spec));
  };

  const D = knobSize[size];
  const B = D + HIT_PAD * 2;
  const text = formatValue(v, spec);
  // Protocol rule 5: the unit is a separate dim span, never part of the value
  // string. "1.20 kHz" renders as 1.20 + kHz so digits stay tabular and the
  // unit can drop out in compact layouts.
  const splitAt = text.lastIndexOf(" ");
  const valueText = splitAt > 0 ? text.slice(0, splitAt) : text;
  const unitText = splitAt > 0 ? text.slice(splitAt + 1) : "";
  // APG slider: expose the real range when the spec is numeric; option lists
  // keep a 0..100 position with aria-valuetext carrying the label.
  const numeric = !spec.options;
  const aMin = numeric ? (spec.min ?? 0) : 0;
  const aMax = numeric ? (spec.max ?? 100) : 100;
  const aNow = numeric ? Number(toValue(v, spec).toFixed(3)) : Math.round(v * 100);
  const { pointerDeg, fillFrom, fillTo } = knobAngles(v, bipolar);
  const arcColor = off ? "#6f6c66" : color.value;
  const track = `conic-gradient(from 225deg, ${color.track} 0deg ${fillFrom}deg, ${arcColor} ${fillFrom}deg ${fillTo}deg, ${color.track} ${fillTo}deg 270deg, transparent 270deg)`;
  const hasMod = modDepth > 0 && !off;
  const m = modRange(v, modDepth);
  const active = hover || drag;

  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture?.(e.pointerId);
    acc.current = v;
    lastY.current = e.clientY;
    dragging.current = true;
    setDrag(true);
    onGesture?.("start");
  };
  const onPointerMove = (e: PointerEvent<HTMLDivElement>) => {
    if (!dragging.current) return;
    const dy = lastY.current - e.clientY;
    lastY.current = e.clientY;
    setFine(e.shiftKey);
    if (dy !== 0) commit(dragStep(acc.current, dy, e.shiftKey));
  };
  const endDrag = (e: PointerEvent<HTMLDivElement>) => {
    if (!dragging.current) return;
    dragging.current = false;
    e.currentTarget.releasePointerCapture?.(e.pointerId);
    setDrag(false);
    setFine(false);
    onGesture?.("end");
  };
  const onWheel = (e: WheelEvent<HTMLDivElement>) => once(wheelStep(v, e.deltaY, e.shiftKey));
  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const nv = keyStep(v, e.key, e.shiftKey, defaultValue);
    if (nv === null) return;
    e.preventDefault();
    once(nv);
  };

  const H = B + 16 + (showValue ? 15 : 0);
  const rootStyle: CSSProperties = { width: Math.max(B + 8, 60), height: H };
  const bodyStyle: CSSProperties = { left: HIT_PAD, top: HIT_PAD, width: D, height: D };
  const skirtVars = {
    "--cs-shy": `${Math.round(D / 10)}px`,
    "--cs-shb": `${Math.round(D / 6)}px`,
  } as CSSProperties;
  const dotVars = {
    "--cs-a0": `${m.fromPointer}deg`,
    "--cs-a1": `${m.toPointer}deg`,
    "--cs-mod-dur": `${modPeriod}s`,
    transform: `rotate(${(m.fromPointer + m.toPointer) / 2}deg)`,
  } as CSSProperties;

  return (
    <div
      className={`cs-knob${active ? " cs-active" : ""}${off ? " cs-off" : ""}`}
      style={rootStyle}
      title={title}
    >
      {active && (
        <span className="cs-tip" style={{ bottom: H - 4 }} role="status">
          <span className="cs-tip-val">{text}</span>
          {hasMod && (
            <span className="cs-tip-mod">
              {modSource} ±{Math.round(modDepth * 100)}%
            </span>
          )}
          {drag && fine && <span className="cs-tip-fine">FINE ×0.1</span>}
        </span>
      )}
      {editing ? (
        <input
          autoFocus
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commitEdit}
          onKeyDown={(e) => {
            if (e.key === "Enter") {
              e.preventDefault();
              commitEdit();
            }
            if (e.key === "Escape") {
              e.preventDefault();
              editOpen.current = false;
              setEditing(false);
            }
          }}
          aria-label={`${label} value`}
          style={{
            position: "absolute",
            top: -2,
            left: "50%",
            transform: "translateX(-50%)",
            zIndex: 30,
            width: 76,
            textAlign: "center",
            fontFamily: "var(--cs-font-mono)",
            fontSize: 12,
            color: color.lcdText,
            background: color.panel,
            border: `1px solid ${color.value}`,
            borderRadius: 4,
            outline: "none",
          }}
        />
      ) : null}
      <div
        id={id}
        tabIndex={0}
        className={`cs-knob-hit${debugHit ? " cs-debug-hit" : ""}`}
        style={{ width: B, height: B }}
        role="slider"
        aria-label={label}
        aria-valuemin={aMin}
        aria-valuemax={aMax}
        aria-valuenow={aNow}
        aria-valuetext={text}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onPointerEnter={() => setHover(true)}
        onPointerLeave={() => setHover(false)}
        onDoubleClick={() => {
          if (editable) {
            setDraft(formatValue(v, spec));
            editOpen.current = true;
            setEditing(true);
          } else {
            once(defaultValue);
          }
        }}
        onContextMenu={(e) => {
          if (!onContextMenu) return;
          e.preventDefault();
          onContextMenu(e.clientX, e.clientY);
        }}
        onWheel={onWheel}
        onKeyDown={onKeyDown}
      >
        <span className="cs-knob-body" style={bodyStyle}>
          {hasMod && (
            <>
              <span
                className="cs-knob-modring"
                style={{
                  background: `conic-gradient(from 225deg, transparent 0deg ${m.fromDeg}deg, ${color.mod} ${m.fromDeg}deg ${m.toDeg}deg, transparent ${m.toDeg}deg 360deg)`,
                }}
              />
              <span className="cs-knob-moddot" style={dotVars} />
            </>
          )}
          <span className="cs-knob-track" style={{ background: track }} />
          <span className="cs-knob-well" />
          <span className="cs-knob-skirt" style={skirtVars} />
          <span className="cs-knob-cap" />
          <span className="cs-knob-rot" style={{ transform: `rotate(${pointerDeg}deg)` }}>
            <span
              className="cs-knob-ptr"
              style={{
                width: D >= 62 ? 4 : D >= 48 ? 3 : 2,
                marginLeft: D >= 62 ? -2 : D >= 48 ? -1.5 : -1,
              }}
            />
          </span>
        </span>
      </div>
      <span className="cs-knob-label">{label}</span>
      {showValue && (
        <span className="cs-knob-value">
          {valueText}
          {unitText ? <span className="cs-unit">{unitText}</span> : null}
        </span>
      )}
    </div>
  );
}
