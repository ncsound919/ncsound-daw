// Vitest setup shared by every test file.
import "@testing-library/jest-dom/vitest";
import { cleanup } from "@testing-library/react";
import { afterEach } from "vitest";

afterEach(() => {
  cleanup();
});

// jsdom ships no 2D context. MasterViz draws its spectrum/LUFS/correlation
// views through canvas, so without a stub every render logs a "Not implemented"
// error and the draw path is never exercised. The stub is a no-op recorder: it
// proves the code runs without touching real pixels.
// jsdom *defines* getContext but throws "Not implemented", so overriding
// unconditionally is the only way to reach the drawing code under test.
if (typeof HTMLCanvasElement !== "undefined") {
  HTMLCanvasElement.prototype.getContext = (() => {
    const noop = () => {};
    const ctx = {
      canvas: null as unknown as HTMLCanvasElement,
      fillStyle: "", strokeStyle: "", lineWidth: 1, font: "",
      clearRect: noop, fillRect: noop, strokeRect: noop,
      beginPath: noop, closePath: noop, moveTo: noop, lineTo: noop,
      arc: noop, fill: noop, stroke: noop, fillText: noop, save: noop, restore: noop,
      measureText: () => ({ width: 0 }) as TextMetrics,
      createLinearGradient: () => ({ addColorStop: noop }) as unknown as CanvasGradient,
    };
    return () => ctx as unknown as CanvasRenderingContext2D;
  })() as unknown as typeof HTMLCanvasElement.prototype.getContext;
}
