import { describe, expect, it } from "vitest";
import { zoomWindow } from "./format";

const close = (a: number, b: number) => expect(Math.abs(a - b)).toBeLessThan(1e-9);

describe("zoomWindow", () => {
  it("keeps the bar under the pointer fixed while zooming in", () => {
    const before = { viewStart: 10, visibleBars: 20 };
    const anchorRatio = 0.25;
    const anchorBar = before.viewStart + anchorRatio * before.visibleBars;

    const after = zoomWindow(before.viewStart, before.visibleBars, anchorRatio, "in", 4, 256);

    expect(after.visibleBars).toBeLessThan(before.visibleBars);
    close(after.viewStart + anchorRatio * after.visibleBars, anchorBar);
  });

  it("returns to the exact same window after zooming in then out", () => {
    const start = 1;
    const bars = 32;

    const inOnce = zoomWindow(start, bars, 0.5, "in", 4, 256);
    const outAgain = zoomWindow(inOnce.viewStart, inOnce.visibleBars, 0.5, "out", 4, 256);

    close(outAgain.viewStart, start);
    close(outAgain.visibleBars, bars);
  });

  it("does not creep down the timeline over repeated zoom pairs", () => {
    let window = { viewStart: 1, visibleBars: 32 };

    for (let i = 0; i < 20; i++) {
      window = zoomWindow(window.viewStart, window.visibleBars, 0.5, "in", 4, 256);
      window = zoomWindow(window.viewStart, window.visibleBars, 0.5, "out", 4, 256);
    }

    close(window.viewStart, 1);
    close(window.visibleBars, 32);
  });
});
