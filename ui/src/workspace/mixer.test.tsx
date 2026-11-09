import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { Mixer } from "./Mixer";

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

    fireEvent.click(
      screen.getByRole("button", { name: "Apply Mixology preset 808 WEIGHT" }),
    );
    expect(apply).toHaveBeenCalledWith(1);
    expect(await screen.findByText("MIXOLOGY / 5 INSERTS APPLIED")).toBeInTheDocument();
  });

  it("hides the Mixology section without presets", () => {
    render(<Mixer {...props()} />);
    expect(screen.queryByRole("group", { name: "Mixology presets" })).not.toBeInTheDocument();
  });
});
