import { describe, expect, it } from "vitest";
import {
  barPct,
  dbLabel,
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
