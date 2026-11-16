import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { Mixer } from "./Mixer";
import type { TrackInfo } from "../types";

const PRESETS = ["VOCAL FOCUS", "808 WEIGHT", "VOCAL PRESENCE", "SUBTRACTIVE", "FULL CHAIN"];

function props(overrides = {}) {
  return {
    tracks: [],
    selectedTrack: 0,
    masterGainDb: 0,
    onSelectTrack: vi.fn(),
    onGain: vi.fn(),
    onPan: vi.fn(),
    onMute: vi.fn(),
    onSolo: vi.fn(),
    onSendA: vi.fn(),
    onSendB: vi.fn(),
    onArm: vi.fn(),
    onMasterGain: vi.fn(),
    onMasterDim: vi.fn(),
    onMasterMono: vi.fn(),
    onRack: vi.fn(),
    ...overrides,
  };
}

describe("Mixer Mixology", () => {
  it("applies the picked preset and shows the host report", async () => {
    const apply = vi.fn().mockResolvedValue("MIXOLOGY / 5 INSERTS APPLIED");
    render(<Mixer {...props()} mixologyPresets={PRESETS} onApplyMixology={apply} />);

    for (const name of PRESETS) {
      expect(
        screen.getByRole("button", { name: "Apply Mixology preset " + name }),
      ).toBeInTheDocument();
    }

    fireEvent.click(screen.getByRole("button", { name: "Apply Mixology preset 808 WEIGHT" }));
    expect(apply).toHaveBeenCalledWith(1);
    expect(await screen.findByText("MIXOLOGY / 5 INSERTS APPLIED")).toBeInTheDocument();
  });

  it("hides the Mixology section without presets", () => {
    render(<Mixer {...props()} />);
    expect(screen.queryByRole("group", { name: "Mixology presets" })).not.toBeInTheDocument();
  });
});

function tracks(count: number): TrackInfo[] {
  return Array.from({ length: count }, (_, index) => ({
    index,
    name: `Track ${index + 1}`,
    instrumentName: "Synth",
    gainDb: 0,
    pan: 0,
    muted: false,
    soloed: false,
  }));
}

describe("Mixer density", () => {
  it("renders 16 tracks in compact density at 1280", () => {
    render(<Mixer {...props({ tracks: tracks(16), viewportWidth: 1280 })} />);
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute(
      "data-density",
      "compact",
    );
  });

  it("stays normal for 8 tracks at 1280", () => {
    render(<Mixer {...props({ tracks: tracks(8), viewportWidth: 1280 })} />);
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute(
      "data-density",
      "normal",
    );
  });
});

describe("Mixer fit control", () => {
  it("keeps the user's manual density until the window changes", () => {
    const { rerender } = render(
      <Mixer {...props({ tracks: tracks(16), viewportWidth: 1280 })} />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Fit strips" }));
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute(
      "data-density",
      "normal",
    );

    rerender(<Mixer {...props({ tracks: tracks(16), viewportWidth: 1300 })} />);
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute(
      "data-density",
      "compact",
    );
  });
});

function tracksWithInsert(track: number, slot: number, name = "Pro-Q"): TrackInfo[] {
  const list = tracks(track + 1);
  list[track] = {
    ...list[track],
    inserts: Array.from({ length: 8 }, (_, index) => ({
      name: index === slot ? name : "",
      missing: false,
      bypassed: false,
    })),
  };
  return list;
}

describe("Mixer insert bypass", () => {
  it("alt-clicks a loaded slot to bypass it", () => {
    const onBypass = vi.fn();
    render(<Mixer {...props({ tracks: tracksWithInsert(0, 0), onBypass })} />);

    fireEvent.click(screen.getByLabelText(/Insert 1/), { altKey: true });
    expect(onBypass).toHaveBeenCalledWith(0, 0, true);
  });

  it("a plain click still opens the browser instead of bypassing", () => {
    const onBypass = vi.fn();
    const onInsert = vi.fn();
    render(<Mixer {...props({ tracks: tracksWithInsert(0, 0), onBypass, onInsert })} />);

    fireEvent.click(screen.getByLabelText(/Insert 1/));
    expect(onInsert).toHaveBeenCalledWith(0, 0);
    expect(onBypass).not.toHaveBeenCalled();
  });
});
