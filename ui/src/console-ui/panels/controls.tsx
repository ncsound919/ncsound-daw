// GENERATED from console-ui — edit there, then run `npm run sync`.
// Schema-bound controls: a panel declares its controls once (label, spec,
// default) and renders them by key. Unmapped keys render nothing.

import { createContext, useContext, useMemo, type CSSProperties, type ReactNode } from "react";
import { Knob } from "../Knob";
import { Slider } from "../Slider";
import { Group, Key, Led } from "../Surface";
import { formatValue, toValue, type ParamSpec } from "../param";
import {
  optionIndex,
  optionNorm,
  useBinder,
  type Bound,
  type ParamMap,
  type ParamStore,
} from "../store";
import type { KnobSize } from "../tokens";

export interface CtlDef {
  label: string;
  spec?: ParamSpec;
  /** Normalised default (double-click target). */
  def: number;
  bipolar?: boolean;
}
export type Schema = Readonly<Record<string, CtlDef>>;

/** Host truth for individual keys: range, unit, curve, options and default.
 *  Merged over the panel's own schema so every display, knob and visualiser
 *  uses the plugin's real parameter ranges instead of the panel's guesses. */
export type SpecOverrides = Readonly<Record<string, Partial<CtlDef>>>;

/** Pure merge of a panel schema with host overrides (spec fields merge; a
 *  host `options` list replaces the numeric range, and vice versa). */
export function mergeSchema(schema: Schema, specs?: SpecOverrides): Schema {
  if (!specs) return schema;
  const out: Record<string, CtlDef> = { ...schema };
  for (const [k, o] of Object.entries(specs)) {
    const base = schema[k];
    if (!base || !o) continue;
    let spec: ParamSpec | undefined = base.spec;
    if (o.spec) {
      spec = o.spec.options
        ? { options: o.spec.options }
        : { ...(base.spec?.options ? {} : base.spec), ...o.spec };
    }
    out[k] = { ...base, ...o, spec };
  }
  return out;
}

/** Normalises an option label for semantic matching: "Ping-Pong" -> "pingpong". */
export const optionKey = (label: string | undefined): string =>
  (label ?? "").toLowerCase().replace(/[^a-z0-9]/g, "");

export interface PanelProps {
  store: ParamStore;
  map: ParamMap;
  presetName?: string;
  onPresetStep?: (dir: -1 | 1) => void;
  /** Level taps read once per frame: [left, right] 0..1. */
  meters?: { in?: () => [number, number]; out?: () => [number, number]; gr?: () => number };
  /** Live data for the main display; omit for the synthesised preview. */
  getFrame?: () => unknown;
  /** Host ranges/options per key (see SpecOverrides). */
  specs?: SpecOverrides;
  /** Plugin data that is not a parameter (host `state.extra`, e.g. drawn curves). */
  extra?: Readonly<Record<string, unknown>>;
  /** Writes such data back (host `setExtra`). Absent in previews. */
  setExtra?: (key: string, value: unknown) => void;
  /** The plugin's own undo history. Undo/Redo keys are hidden when absent. */
  onUndo?: () => void;
  onRedo?: () => void;
}

export interface Ctx {
  schema: Schema;
  bind: (k: string) => Bound | null;
  store: ParamStore;
}
const PanelCtx = createContext<Ctx | null>(null);

export function PanelScope({
  schema,
  specs,
  store,
  map,
  children,
}: {
  schema: Schema;
  specs?: SpecOverrides;
  store: ParamStore;
  map: ParamMap;
  children: ReactNode;
}) {
  const bind = useBinder(store, map);
  const merged = useMemo(() => mergeSchema(schema, specs), [schema, specs]);
  return <PanelCtx.Provider value={{ schema: merged, bind, store }}>{children}</PanelCtx.Provider>;
}

/** The resolved panel (schema, binder, store). Call it once per component,
 *  then use the pure `panel*` reads below wherever a hook would be illegal. */
export function usePanelContext(): Ctx {
  const c = useContext(PanelCtx);
  if (!c) throw new Error("panel control used outside <PanelScope>");
  return c;
}

function def(schema: Schema, k: string): CtlDef {
  const d = schema[k];
  if (!d) throw new Error(`control "${k}" missing from schema`);
  return d;
}

/** Pure reads of a resolved panel: identical results to the `useXxx` accessors,
 *  but not hooks, so they are legal in loops, `.map()` bodies, conditionals and
 *  callbacks - exactly where the accessors would break the rules of hooks. */
export const panelValue = (p: Ctx, k: string): number => p.bind(k)?.value ?? def(p.schema, k).def;
export const panelSpec = (p: Ctx, k: string): ParamSpec => def(p.schema, k).spec ?? {};
export const panelOption = (p: Ctx, k: string): number =>
  optionIndex(panelValue(p, k), panelSpec(p, k).options?.length ?? 2);
export const panelNum = (p: Ctx, k: string): number => toValue(panelValue(p, k), panelSpec(p, k));
export const panelLabel = (p: Ctx, k: string): string =>
  panelSpec(p, k).options?.[panelOption(p, k)] ?? "";
export const panelBound = (p: Ctx, k: string): Bound | null => p.bind(k);
export const panelHas = (p: Ctx, ...keys: string[]): boolean =>
  keys.some((k) => p.bind(k) !== null);
export const panelText = (p: Ctx, k: string): string =>
  formatValue(panelValue(p, k), panelSpec(p, k));

/** Current normalised value of a key, or its schema default when unmapped. */
export function useValue(k: string): number {
  return panelValue(usePanelContext(), k);
}
/** Current option index of an enum key (default when unmapped). */
export function useOption(k: string): number {
  return panelOption(usePanelContext(), k);
}
/** The effective (host-merged) spec of a key. */
export function useSpec(k: string): ParamSpec {
  return panelSpec(usePanelContext(), k);
}
/** Current value of a key in real units (Hz, dB, ms...) using the effective spec. */
export function useNum(k: string): number {
  return panelNum(usePanelContext(), k);
}
/** Label of the current option of an enum key ("" for non-enum keys). */
export function useLabel(k: string): string {
  return panelLabel(usePanelContext(), k);
}
/** Raw binding of a key (value + onChange + onGesture), or null when unmapped.
 *  For custom editors that are not a single knob/slider (e.g. the rhythm grid). */
export function useBound(k: string): Bound | null {
  return panelBound(usePanelContext(), k);
}
export function useHas(...keys: string[]): boolean {
  return panelHas(usePanelContext(), ...keys);
}
export function useText(k: string): string {
  return panelText(usePanelContext(), k);
}

interface LearnStore {
  midiLearn?: (key: string) => void;
  action?: (action: string) => void;
}

export function PLearn({ k, title }: { k: string; title?: string }) {
  const { store } = usePanelContext();
  // A host store may expose a direct MIDI-learn method or the generic action
  // channel; both are optional, so the surface is typed rather than cast.
  const learnStore = store as LearnStore;
  const hasLearn =
    typeof learnStore.midiLearn === "function" || typeof learnStore.action === "function";
  if (!hasLearn) return null;
  return (
    <button
      type="button"
      className="cs-key"
      aria-label={`Learn MIDI for ${title ?? k}`}
      title={`Learn MIDI → ${title ?? k}`}
      onClick={() => {
        if (typeof learnStore.midiLearn === "function") learnStore.midiLearn(k);
        else learnStore.action?.(`midiLearn:${k}`);
      }}
      style={{ minWidth: 36, fontSize: 10, cursor: "pointer" }}
    >
      LEARN
    </button>
  );
}

export function PKnob({
  k,
  size = "l",
  showValue,
  modDepth,
  modSource,
  modPeriod,
  off,
  label,
  learn,
}: {
  k: string;
  size?: KnobSize;
  showValue?: boolean;
  modDepth?: number;
  modSource?: string;
  modPeriod?: number;
  off?: boolean;
  label?: string;
  learn?: boolean;
}) {
  const { schema, bind } = usePanelContext();
  const b = bind(k);
  if (!b) return null;
  const d = def(schema, k);
  const knob = (
    <Knob
      label={label ?? d.label}
      {...b}
      defaultValue={d.def}
      spec={d.spec}
      bipolar={d.bipolar}
      size={size}
      showValue={showValue ?? size === "xl"}
      modDepth={modDepth}
      modSource={modSource}
      modPeriod={modPeriod}
      off={off}
    />
  );
  if (!learn) return knob;
  return (
    <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 2 }}>
      {knob}
      <PLearn k={k} title={label ?? d.label} />
    </div>
  );
}

export function PSlider({
  k,
  width = 220,
  off,
  accent,
  label,
}: {
  k: string;
  width?: number;
  off?: boolean;
  accent?: string;
  label?: string;
}) {
  const { schema, bind } = usePanelContext();
  const b = bind(k);
  if (!b) return null;
  const d = def(schema, k);
  return (
    <Slider
      label={label ?? d.label}
      {...b}
      defaultValue={d.def}
      spec={d.spec}
      bipolar={d.bipolar}
      width={width}
      off={off}
      accent={accent}
    />
  );
}

/** On/off key bound to a 0/1 parameter. */
export function PToggle({
  k,
  children,
  style,
}: {
  k: string;
  children?: ReactNode;
  style?: CSSProperties;
}) {
  const { schema, bind } = usePanelContext();
  const b = bind(k);
  if (!b) return null;
  const on = b.value >= 0.5;
  return (
    <Key
      pressed={on}
      style={style}
      onClick={() => {
        b.onGesture("start");
        b.onChange(on ? 0 : 1);
        b.onGesture("end");
      }}
    >
      {children ?? def(schema, k).label}
    </Key>
  );
}

/** Enum parameter as a row/grid of keys ("keys") or a compact segmented switch ("seg"). */
export function PChoice({
  k,
  variant = "keys",
  columns,
  style,
  keyStyle,
  labels,
}: {
  k: string;
  variant?: "keys" | "seg";
  columns?: number;
  style?: CSSProperties;
  keyStyle?: CSSProperties;
  labels?: readonly string[];
}) {
  const { schema, bind } = usePanelContext();
  const b = bind(k);
  if (!b) return null;
  const d = def(schema, k);
  const opts = labels ?? d.spec?.options ?? [];
  const cur = optionIndex(b.value, opts.length);
  const pick = (i: number) => {
    b.onGesture("start");
    b.onChange(optionNorm(i, opts.length));
    b.onGesture("end");
  };
  if (variant === "seg") {
    return (
      <div className="cs-seg" role="group" aria-label={d.label} style={style}>
        {opts.map((o, i) => (
          <button key={o} type="button" aria-pressed={i === cur} onClick={() => pick(i)}>
            {o}
          </button>
        ))}
      </div>
    );
  }
  return (
    <div
      role="group"
      aria-label={d.label}
      className="cs-grid"
      style={{
        display: "grid",
        gridTemplateColumns: `repeat(${columns ?? opts.length}, minmax(0, 1fr))`,
        gap: 8,
        ...style,
      }}
    >
      {opts.map((o, i) => (
        <Key
          key={o}
          pressed={i === cur}
          onClick={() => pick(i)}
          style={{ minHeight: 40, fontSize: 11, ...keyStyle }}
        >
          {o}
        </Key>
      ))}
    </div>
  );
}

/** Enum parameter shown as an LCD that steps to the next option on click
 *  (keeps narrow slots narrow). The full list is in the accessible name. */
export function PCycle({ k, style }: { k: string; style?: CSSProperties }) {
  const { schema, bind } = usePanelContext();
  const b = bind(k);
  if (!b) return null;
  const opts = def(schema, k).spec?.options ?? [];
  const cur = optionIndex(b.value, opts.length);
  const next = () => {
    b.onGesture("start");
    b.onChange(optionNorm((cur + 1) % opts.length, opts.length));
    b.onGesture("end");
  };
  return (
    <button
      type="button"
      className="cs-lcd"
      onClick={next}
      aria-label={`${def(schema, k).label}: ${opts[cur]}. Next of ${opts.join(", ")}`}
      style={{
        alignSelf: "stretch",
        minHeight: 36,
        minWidth: 64,
        padding: "0 12px",
        fontSize: 12,
        cursor: "pointer",
        justifyContent: "center",
        whiteSpace: "nowrap",
        ...style,
      }}
    >
      {opts[cur]} ⇅
    </button>
  );
}

/** One-shot action key; hidden when the host doesn't support the action. */
export function PAction({
  name,
  children,
  style,
}: {
  name: string;
  children: ReactNode;
  style?: CSSProperties;
}) {
  const { store } = usePanelContext();
  if (!store.hasAction?.(name)) return null;
  return (
    <Key onClick={() => store.action?.(name)} style={style}>
      {children}
    </Key>
  );
}

/** Engraved group that disappears when none of its keys are mapped.
 *  `enable` names an on/off key: when the host maps it, the title bar gets an
 *  LED + On/Off key and the contents show the bypassed state (still editable). */
export function PGroup({
  title,
  chip,
  keys,
  extra,
  enable,
  style,
  children,
}: {
  title: string;
  chip?: string;
  keys: string[];
  extra?: ReactNode;
  enable?: string;
  style?: CSSProperties;
  children: ReactNode;
}) {
  const { bind } = usePanelContext();
  const has = useHas(...keys);
  if (!has) return null;
  const en = enable ? bind(enable) : null;
  const on = en ? en.value >= 0.5 : true;
  const head = en ? (
    <>
      {extra}
      <Led state={on ? "on" : "off"} />
      <PToggle k={enable!} style={{ padding: "0 10px" }}>
        {on ? "On" : "Off"}
      </PToggle>
    </>
  ) : (
    extra
  );
  return (
    <Group title={title} chip={chip} extra={head} bypassed={!on} style={style}>
      {children}
    </Group>
  );
}

/** Row helper: children laid out evenly, dropping nulls automatically via flex. */
export function Row({
  children,
  gap = 8,
  justify = "space-between",
  align = "flex-end",
  style,
}: {
  children: ReactNode;
  gap?: number;
  justify?: CSSProperties["justifyContent"];
  align?: CSSProperties["alignItems"];
  style?: CSSProperties;
}) {
  return (
    <div style={{ display: "flex", gap, justifyContent: justify, alignItems: align, ...style }}>
      {children}
    </div>
  );
}

export function Grid({
  cols,
  gap = 8,
  children,
  style,
}: {
  cols: number;
  gap?: number;
  children: ReactNode;
  style?: CSSProperties;
}) {
  return (
    <div
      className="cs-grid"
      style={{
        display: "grid",
        gridTemplateColumns: `repeat(${cols}, minmax(0, 1fr))`,
        gap,
        ...style,
      }}
    >
      {children}
    </div>
  );
}

export function Readout({
  children,
  size = 13,
  style,
}: {
  children: ReactNode;
  size?: number;
  style?: CSSProperties;
}) {
  return (
    <span className="cs-readout" style={{ fontSize: size, ...style }}>
      {children}
    </span>
  );
}

/** Build a full identity map + default values for previews: key -> key. */
export function previewBinding(schema: Schema, overrides: Record<string, number> = {}) {
  const map: Record<string, string> = {};
  const values: Record<string, number> = {};
  for (const [k, d] of Object.entries(schema)) {
    map[k] = k;
    values[k] = overrides[k] ?? d.def;
  }
  return { map, values };
}
