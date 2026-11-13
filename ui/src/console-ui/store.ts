// GENERATED from console-ui — edit there, then run `npm run sync`.
// Parameter plumbing between Console panels and a plugin host.
//
// Panels address controls by their own stable *keys* ("feedback", "duckKey").
// Each plugin supplies a ParamMap from key -> its real parameter id. A key with
// no mapping is simply not rendered: that is how controls whose DSP doesn't
// exist yet stay hidden without forking the panel.

import { useCallback, useSyncExternalStore } from "react";
import type { Gesture } from "./Knob";

export interface ParamStore {
  /** Normalised 0..1 value, or undefined if the id is unknown. */
  get(id: string): number | undefined;
  set(id: string, value: number): void;
  gesture?(id: string, phase: Gesture): void;
  /** One-shot actions (tap tempo, clear buffer, load IR...). */
  action?(name: string): void;
  /** Actions the host supports; unsupported action keys are hidden. */
  hasAction?(name: string): boolean;
  subscribe(listener: () => void): () => void;
  /** Monotonic counter bumped on every change (for useSyncExternalStore). */
  version(): number;
}

export type ParamMap = Readonly<Record<string, string | undefined>>;

export interface Bound {
  value: number;
  onChange: (v: number) => void;
  onGesture: (p: Gesture) => void;
}

/** Subscribe a component to the store and return a key-based binder. */
export function useBinder(store: ParamStore, map: ParamMap) {
  useSyncExternalStore(store.subscribe, store.version, store.version);
  return useCallback(
    (key: string): Bound | null => {
      const id = map[key];
      if (!id) return null;
      const value = store.get(id);
      if (value === undefined) return null;
      return {
        value,
        onChange: (v) => store.set(id, v),
        onGesture: (p) => store.gesture?.(id, p),
      };
    },
    [store, map],
  );
}

/** Index of a discrete option from its normalised value. */
export const optionIndex = (norm: number, count: number): number =>
  count < 2 ? 0 : Math.round(norm * (count - 1));
/** Normalised value of option `index` of `count`. */
export const optionNorm = (index: number, count: number): number =>
  count < 2 ? 0 : index / (count - 1);

/**
 * In-memory store for previews and tests. Values are normalised; unknown ids
 * are ignored. `actions` lists supported one-shot actions.
 */
export function createMemoryStore(
  initial: Record<string, number>,
  actions: string[] = [],
): ParamStore & { log: string[] } {
  const values = new Map(Object.entries(initial));
  const listeners = new Set<() => void>();
  let ver = 0;
  const log: string[] = [];
  const emit = () => {
    ver++;
    listeners.forEach((l) => l());
  };
  return {
    log,
    get: (id) => values.get(id),
    set(id, v) {
      if (!values.has(id)) return;
      values.set(id, Math.max(0, Math.min(1, v)));
      emit();
    },
    gesture(id, p) {
      log.push(`${p}:${id}`);
    },
    action(name) {
      log.push(`action:${name}`);
    },
    hasAction: (name) => actions.includes(name),
    subscribe(l) {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    version: () => ver,
  };
}

/**
 * Store over a live host snapshot. The instance is created once and pointed at
 * fresh data via `update`; `subscribe`/`version` never change identity, which
 * is what React's `useSyncExternalStore` requires (react.dev: "subscribe should
 * be stable, or React resubscribes every render"). Readers/writers receive the
 * current source so per-control layer addressing stays correct.
 */
export interface StateStore<T> extends ParamStore {
  /** Point the store at the latest source snapshot and notify subscribers. */
  update(source: T): void;
}

export interface StateStoreOptions<T> {
  /** Normalised 0..1 value for a control key, or undefined if unmapped. */
  read: (source: T, id: string) => number | undefined;
  /** Apply a normalised 0..1 change for a control key. */
  write: (id: string, value: number, source: T) => void;
  gesture?: (id: string, phase: Gesture, source: T) => void;
  actions?: (source: T) => readonly string[];
  action?: (name: string, source: T) => void;
}

export function createStateStore<T>(opts: StateStoreOptions<T>): StateStore<T> {
  let source: T | null = null;
  let ver = 0;
  const listeners = new Set<() => void>();
  // Optimistic values: what the user just set, shown until the host echoes.
  // Ids inside an open gesture keep their pending value across host pushes so
  // a slow or out-of-date push can't yank a knob back mid-drag.
  const pending = new Map<string, number>();
  const active = new Set<string>();
  const emit = () => {
    ver += 1;
    listeners.forEach((l) => l());
  };

  return {
    update(next) {
      source = next;
      for (const id of [...pending.keys()]) if (!active.has(id)) pending.delete(id);
      emit();
    },
    get(id) {
      if (source === null) return undefined;
      const real = opts.read(source, id);
      if (real === undefined) return undefined;
      return pending.get(id) ?? real;
    },
    set(id, value) {
      if (source === null) return;
      pending.set(id, value);
      opts.write(id, value, source);
      emit();
    },
    gesture(id, phase) {
      if (phase === "start") active.add(id);
      else active.delete(id);
      if (source !== null) opts.gesture?.(id, phase, source);
    },
    action(name) {
      if (source !== null) opts.action?.(name, source);
    },
    hasAction(name) {
      return source !== null && (opts.actions?.(source)?.includes(name) ?? false);
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    version: () => ver,
  };
}
