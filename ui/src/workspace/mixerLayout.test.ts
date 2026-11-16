import { describe, expect, it } from "vitest";
import { densityFor, stripMinWidthPx } from "./mixerLayout";

describe("mixer density", () => {
  it("fits 16 tracks at 1280 by going compact", () => {
    expect(densityFor(16, 1280)).toBe("compact");
    expect(16 * stripMinWidthPx("compact") + 96).toBeLessThanOrEqual(1280);
  });

  it("stays normal for 8 tracks at 1280", () => {
    expect(densityFor(8, 1280)).toBe("normal");
  });

  it("keeps compact at 17 tracks and scrolls instead of collapsing", () => {
    expect(densityFor(17, 1280)).toBe("compact");
    expect(stripMinWidthPx("compact")).toBeGreaterThanOrEqual(72);
  });

  it("does not collapse below the usable minimum on an 800 px window", () => {
    expect(stripMinWidthPx(densityFor(16, 800))).toBe(72);
  });

  it("keeps the two densities and the master width in one place", () => {
    expect(stripMinWidthPx("normal")).toBe(112);
    expect(stripMinWidthPx("compact")).toBe(72);
  });
});
