// GENERATED from console-ui — edit there, then run `npm run sync`.
// Pure gesture maths shared by Knob and Slider, so behaviour is identical
// everywhere and unit-testable without a DOM.

import { clamp01 } from "./param";
import { DRAG_TRAVEL, FINE_FACTOR, WHEEL_STEP, WHEEL_STEP_FINE } from "./tokens";

/**
 * Incremental drag: apply the movement since the last pointer event to the
 * running value. Accumulating per event (not from the drag origin) means
 * pressing or releasing Shift mid-drag never makes the value jump.
 */
export function dragStep(current: number, deltaPx: number, fine: boolean, travel = DRAG_TRAVEL): number {
  return clamp01(current + (deltaPx / travel) * (fine ? FINE_FACTOR : 1));
}

export function wheelStep(current: number, deltaY: number, fine: boolean): number {
  if (deltaY === 0) return current;
  return clamp01(current - Math.sign(deltaY) * (fine ? WHEEL_STEP_FINE : WHEEL_STEP));
}

/** Returns the new value, or null when the key is not a control key. */
export function keyStep(current: number, key: string, fine: boolean, defaultValue: number): number | null {
  const st = fine ? WHEEL_STEP_FINE : WHEEL_STEP;
  switch (key) {
    case "ArrowUp":
    case "ArrowRight":
      return clamp01(current + st);
    case "ArrowDown":
    case "ArrowLeft":
      return clamp01(current - st);
    case "PageUp":
      return clamp01(current + 0.1);
    case "PageDown":
      return clamp01(current - 0.1);
    case "Home":
      return 0;
    case "End":
      return 1;
    case "Delete":
    case "Backspace":
      return clamp01(defaultValue);
    default:
      return null;
  }
}

/** Knob arc geometry: 270° sweep starting at 7:30 (−135°). */
export function knobAngles(value: number, bipolar: boolean) {
  const arc = 270 * clamp01(value);
  const from = bipolar ? Math.min(135, arc) : 0;
  const to = bipolar ? Math.max(135, arc) : arc;
  return { pointerDeg: -135 + arc, fillFrom: from, fillTo: to };
}

/** Modulation swing range drawn as the outer ring, symmetric around value. */
export function modRange(value: number, depth: number) {
  const lo = clamp01(value - depth);
  const hi = clamp01(value + depth);
  return { fromDeg: 270 * lo, toDeg: 270 * hi, fromPointer: -135 + 270 * lo, toPointer: -135 + 270 * hi };
}
