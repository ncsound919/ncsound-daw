import { describe, expect, it } from "vitest";
import {
  barPct,
  dbLabel,
  fitScale,
  formatBarsBeats,
  meterPct,
  tintForTrack,
  TRACK_TINTS,
  waveformPoints,
} from "./format";

describe("formatBarsBeats", () => {
  it("renders the host's 3|2|3 readout for bar 1", () => {
    expect(formatBarsBeats(1).main).toBe("001|01|000");
  });

  it("splits the fractional bar into beats and ticks", () => {
    expect(formatBarsBeats(1.5).main).toBe("001|03|000");
    expect(formatBarsBeats(2.25).main).toBe("002|02|000");
  });

  it("clamps positions before the session start", () => {
    expect(formatBarsBeats(0).main).toBe("001|01|000");
  });
});

describe("barPct", () => {
  it("maps the window start to 0 and the window end to 100", () => {
    expect(barPct(1, 1, 16)).toBe(0);
    expect(barPct(17, 1, 16)).toBe(100);
    expect(barPct(9, 1, 16)).toBe(50);
  });

  it("returns 0 rather than dividing by an empty window", () => {
    expect(barPct(5, 1, 0)).toBe(0);
  });
});

describe("waveformPoints", () => {
  it("has nothing to draw without enough peaks", () => {
    expect(waveformPoints([])).toBe("");
    expect(waveformPoints([0.5])).toBe("");
  });

  it("mirrors peaks around the centre line", () => {
    const points = waveformPoints([0, 1]).split(" ");
    expect(points).toHaveLength(4);
    expect(points[0]).toBe("0.00,50.00");
    expect(points[1]).toBe("100.00,4.00");
    expect(points[2]).toBe("100.00,96.00");
    expect(points[3]).toBe("0.00,50.00");
  });

  it("clamps peaks outside 0..1", () => {
    expect(waveformPoints([2, -1])).toBe(waveformPoints([1, 0]));
  });
});

describe("levels", () => {
  it("reads the fader floor as -60 and silence as an em dash", () => {
    expect(dbLabel(-60)).toBe("-60.0");
    expect(dbLabel(Number.NEGATIVE_INFINITY)).toBe("—");
    expect(dbLabel(3.24)).toBe("+3.2");
  });

  it("gives a quiet signal a visible meter floor on the fader law", () => {
    expect(meterPct(0)).toBe(0);
    expect(meterPct(0.0001)).toBeGreaterThanOrEqual(2);
    expect(meterPct(1)).toBe(100);
    // Meter and fader share one law: -12 dB fills 75%, 0 dB fills 100%.
    expect(meterPct(Math.pow(10, -12 / 20))).toBeCloseTo(75, 0);
  });

  it("keeps one tint per track and wraps past the palette", () => {
    expect(tintForTrack(0)).toBe(TRACK_TINTS[0]);
    expect(tintForTrack(TRACK_TINTS.length)).toBe(TRACK_TINTS[0]);
  });
});

describe("faceplate fit", () => {
  // The channel faceplate is a 1320x780 panel drawn inside whatever box the
  // editor has. Fitting it is the difference between seeing the module and
  // seeing its top-left corner.
  it("scales to the tighter of the two axes", () => {
    expect(fitScale(1003, 261, 1320, 780)).toBeCloseTo(261 / 780, 4);
    expect(fitScale(660, 780, 1320, 780)).toBeCloseTo(0.5, 4);
    expect(fitScale(660, 300, 1320, 780)).toBeCloseTo(300 / 780, 4);
  });

  it("never enlarges past life size and never shrinks below the legibility floor", () => {
    expect(fitScale(4000, 4000, 1320, 780)).toBe(1);
    expect(fitScale(10, 10, 1320, 780)).toBe(0.3);
  });

  it("falls back to a safe scale for an unmeasured box", () => {
    expect(fitScale(0, 0, 1320, 780)).toBe(0.3);
    expect(fitScale(100, 100, 0, 0)).toBe(0.65);
  });
});
