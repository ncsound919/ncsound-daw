import { act, renderHook } from "@testing-library/react";
import { describe, expect, it } from "vitest";

import { useBinder, type ParamStore } from "./store";

function makeStore(values: Record<string, number> = {}) {
  let revision = 0;
  const listeners = new Set<() => void>();
  const store: ParamStore = {
    get: (id) => values[id],
    set: (id, value) => {
      values[id] = value;
      revision += 1;
      for (const listener of listeners) listener();
    },
    subscribe: (listener) => {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    version: () => revision,
  };
  return store;
}

describe("useBinder", () => {
  it("reads the fresh store value after a change", () => {
    const store = makeStore({ "track.0.gain": 0.5 });
    const { result } = renderHook(() => useBinder(store, { gain: "track.0.gain" }));

    expect(result.current("gain")?.value).toBe(0.5);

    act(() => store.set("track.0.gain", 0.8));

    expect(result.current("gain")?.value).toBe(0.8);
  });

  it("returns null for an unmapped key or an unknown parameter", () => {
    const store = makeStore();
    const { result } = renderHook(() => useBinder(store, { gain: "track.0.gain" }));

    expect(result.current("missing")).toBeNull();
    expect(result.current("gain")).toBeNull();
  });

  it("writes edits back through the same parameter id", () => {
    const store = makeStore({ "track.0.gain": 0.5 });
    const { result } = renderHook(() => useBinder(store, { gain: "track.0.gain" }));

    act(() => result.current("gain")?.onChange(0.25));

    expect(store.get("track.0.gain")).toBe(0.25);
  });
});
