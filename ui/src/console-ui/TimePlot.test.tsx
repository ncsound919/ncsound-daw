import { act, render } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const plots = vi.hoisted(() => {
  const instances: Array<{
    initialData: unknown;
    setDataCalls: unknown[];
    destroyed: boolean;
  }> = [];

  class UPlotMock {
    static pxRatio = 1;
    setDataCalls: unknown[] = [];
    destroyed = false;
    initialData: unknown;

    constructor(_options: unknown, data: unknown, _target: HTMLElement) {
      this.initialData = data;
      instances.push(this);
    }

    setData(data: unknown) {
      this.setDataCalls.push(data);
    }

    destroy() {
      this.destroyed = true;
    }
  }

  return { instances, UPlotMock };
});

vi.mock("uplot", () => ({ default: plots.UPlotMock }));

import { TimePlot } from "./TimePlot";

let frame: FrameRequestCallback | undefined;
const cancelFrame = vi.fn();

beforeEach(() => {
  plots.instances.length = 0;
  frame = undefined;
  vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
    frame = callback;
    return 1;
  });
  vi.stubGlobal("cancelAnimationFrame", cancelFrame);
});

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  cancelFrame.mockClear();
});

function renderPlot(read?: () => number, refLine = -14) {
  return render(
    <TimePlot
      width={600}
      height={170}
      label="Loudness history"
      color="#ffae3a"
      fill
      seconds={6}
      y={[-30, -4]}
      refLine={refLine}
      read={read}
    />,
  );
}

function drawFrame(timestamp: number) {
  const callback = frame;
  expect(callback).toBeDefined();
  act(() => callback!(timestamp));
}

describe("TimePlot surface", () => {
  it("layers the Scope-style glass and scanline finish over the plot well", () => {
    const { container } = renderPlot();

    const well = container.querySelector('[role="img"]');
    const overlay = container.querySelector('[data-surface-finish="scope-glass"]');
    expect(well?.getAttribute("style")).toContain("0 0 8px rgba(255,138,0,.14)");
    expect(overlay?.getAttribute("style")).toContain("inset 0 0 18px rgba(0,0,0,.7)");
    expect(overlay?.getAttribute("style")).toContain(
      "linear-gradient(165deg, rgba(255,255,255,.09) 0%, rgba(255,255,255,.02) 28%, transparent 42%)",
    );
    expect(overlay?.getAttribute("style")).toContain(
      "repeating-linear-gradient(0deg, rgba(0,0,0,.16) 0 1px, transparent 1px 3px)",
    );
  });

  it("plots fresh host values and updates the reference line without rebuilding", () => {
    const read = vi.fn(() => -12.5);
    const { rerender } = renderPlot(read, -14);
    const plot = plots.instances[0];

    drawFrame(40);
    expect(read).toHaveBeenCalledOnce();
    expect(plot.setDataCalls.at(-1)).toEqual([[0], [-12.5], [-14]]);

    rerender(
      <TimePlot
        width={600}
        height={170}
        label="Loudness history"
        color="#ffae3a"
        fill
        seconds={6}
        y={[-30, -4]}
        refLine={-16}
        read={read}
      />,
    );
    drawFrame(80);
    expect(plots.instances).toHaveLength(1);
    expect(plot.setDataCalls.at(-1)).toEqual([
      [-0.04, 0],
      [-12.5, -12.5],
      [-16, -16],
    ]);
  });

  it("keeps axes and the reference line but no trace when host data is absent", () => {
    renderPlot(undefined, -14);
    const plot = plots.instances[0];
    expect(plot.initialData).toEqual([
      [-6, 0],
      [null, null],
      [-14, -14],
    ]);
    drawFrame(40);
    expect(plot.setDataCalls.at(-1)).toEqual([
      [-6, 0],
      [null, null],
      [-14, -14],
    ]);
  });

  it("cancels its animation frame and destroys uPlot on unmount", () => {
    const { unmount } = renderPlot(() => -12);
    const plot = plots.instances[0];
    unmount();
    expect(cancelFrame).toHaveBeenCalledWith(1);
    expect(plot.destroyed).toBe(true);
  });
});
