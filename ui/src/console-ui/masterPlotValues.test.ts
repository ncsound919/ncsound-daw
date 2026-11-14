import { describe, expect, it, vi } from "vitest";
import type { MasterFrame } from "./panels/MasterPanel";
import { maximizerPlotValues } from "./masterPlotValues";

describe("Maximizer plot values", () => {
  it("passes short-term LUFS through and converts host GR to negative dB", () => {
    const frame: MasterFrame = { lufs: { shortTerm: -13.5 }, meters: { gr: 4.25 } };
    const readFrame = vi.fn(() => frame);
    const values = maximizerPlotValues(readFrame)!;

    expect(values.shortTermLufs()).toBe(-13.5);
    expect(values.gainReductionDb()).toBe(-4.25);
    expect(readFrame).toHaveBeenCalledTimes(2);
  });

  it("keeps silence at the chart floor when a frame omits LUFS or GR", () => {
    const values = maximizerPlotValues(() => ({}))!;

    expect(values.shortTermLufs()).toBe(-70);
    expect(values.gainReductionDb()).toBe(0);
  });

  it("returns no readers when there is no host frame source", () => {
    expect(maximizerPlotValues(undefined)).toBeUndefined();
  });
});
