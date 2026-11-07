/** Touch- and mouse-friendly fader used by both the phone remote and the host console. */
export interface FaderOptions {
  value: number;
  min?: number;
  max?: number;
  step?: number;
  vertical?: boolean;
  defaultValue?: number;
  label?: string;
  onChange(value: number): void;
  onStart?(): void;
  onCommit?(): void;
}

export interface FaderHandle {
  el: HTMLDivElement;
  set(value: number): void;
  get(): number;
  isDragging(): boolean;
  setDisabled(disabled: boolean): void;
}

export function createFader(options: FaderOptions): FaderHandle {
  const min = options.min ?? 0;
  const max = options.max ?? 1;
  const step = options.step ?? 0.001;
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  const snap = (v: number) => {
    const snapped = Math.round((v - min) / step) * step + min;
    return Number(clamp(snapped).toFixed(6));
  };

  let value = clamp(options.value);
  let dragging = false;
  let disabled = false;

  const el = document.createElement('div');
  el.className = `fader ${options.vertical ? 'vertical' : 'horizontal'}`;
  el.setAttribute('role', 'slider');
  el.tabIndex = 0;
  el.setAttribute('aria-label', options.label ?? 'Level');
  el.setAttribute('aria-valuemin', String(min));
  el.setAttribute('aria-valuemax', String(max));

  const track = document.createElement('div');
  track.className = 'fader-track';
  const fill = document.createElement('div');
  fill.className = 'fader-fill';
  const thumb = document.createElement('div');
  thumb.className = 'fader-thumb';
  el.append(track, fill, thumb);

  const paint = () => {
    const p = max === min ? 0 : (value - min) / (max - min);
    el.style.setProperty('--p', String(p));
    el.setAttribute('aria-valuenow', String(value));
  };

  const apply = (next: number) => {
    const v = snap(next);
    if (v === value) return;
    value = v;
    paint();
    options.onChange(value);
  };

  const positionFromEvent = (event: PointerEvent) => {
    const rect = el.getBoundingClientRect();
    // Must match the 13px inset the stylesheet gives the track, so the ends
    // of the travel land exactly on min and max.
    const inset = 13;
    const raw = options.vertical
      ? 1 - (event.clientY - rect.top - inset) / Math.max(1, rect.height - inset * 2)
      : (event.clientX - rect.left - inset) / Math.max(1, rect.width - inset * 2);
    return min + Math.min(1, Math.max(0, raw)) * (max - min);
  };

  el.addEventListener('pointerdown', (event) => {
    if (disabled) return;
    dragging = true;
    el.classList.add('dragging');
    el.setPointerCapture(event.pointerId);
    options.onStart?.();
    apply(positionFromEvent(event));
    event.preventDefault();
  });

  el.addEventListener('pointermove', (event) => {
    if (!dragging || disabled) return;
    apply(positionFromEvent(event));
    event.preventDefault();
  });

  const finish = (event: PointerEvent) => {
    if (!dragging) return;
    dragging = false;
    el.classList.remove('dragging');
    try {
      el.releasePointerCapture(event.pointerId);
    } catch {
      // capture may already be gone
    }
    options.onCommit?.();
  };
  el.addEventListener('pointerup', finish);
  el.addEventListener('pointercancel', finish);

  el.addEventListener('keydown', (event) => {
    if (disabled) return;
    let next = value;
    const big = step * 10;
    switch (event.key) {
      case 'ArrowUp':
      case 'ArrowRight':
        next += step;
        break;
      case 'ArrowDown':
      case 'ArrowLeft':
        next -= step;
        break;
      case 'PageUp':
        next += big;
        break;
      case 'PageDown':
        next -= big;
        break;
      case 'Home':
        next = min;
        break;
      case 'End':
        next = max;
        break;
      case 'Enter':
      case ' ':
        if (options.defaultValue !== undefined) apply(options.defaultValue);
        options.onCommit?.();
        event.preventDefault();
        return;
      default:
        return;
    }
    apply(next);
    options.onCommit?.();
    event.preventDefault();
  });

  el.addEventListener('wheel', (event) => {
    if (disabled) return;
    event.preventDefault();
    apply(value + (event.deltaY < 0 ? step : -step) * (event.shiftKey ? 10 : 1));
    options.onCommit?.();
  }, { passive: false });

  el.addEventListener('dblclick', () => {
    if (disabled || options.defaultValue === undefined) return;
    apply(options.defaultValue);
    options.onCommit?.();
  });

  paint();

  return {
    el,
    set(next: number) {
      if (dragging) return;
      const v = clamp(next);
      if (v === value) return;
      value = v;
      paint();
    },
    get: () => value,
    isDragging: () => dragging,
    setDisabled(next: boolean) {
      disabled = next;
      el.classList.toggle('disabled', next);
      el.tabIndex = next ? -1 : 0;
      el.setAttribute('aria-disabled', String(next));
    },
  };
}
