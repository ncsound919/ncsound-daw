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
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute("data-density", "compact");
  });

  it("stays normal for 8 tracks at 1280", () => {
    render(<Mixer {...props({ tracks: tracks(8), viewportWidth: 1280 })} />);
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute("data-density", "normal");
  });
});

describe("Mixer fit control", () => {
  it("keeps the user's manual density until the window changes", () => {
    const { rerender } = render(<Mixer {...props({ tracks: tracks(16), viewportWidth: 1280 })} />);

    fireEvent.click(screen.getByRole("button", { name: "Fit strips" }));
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute("data-density", "normal");

    rerender(<Mixer {...props({ tracks: tracks(16), viewportWidth: 1300 })} />);
    expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute("data-density", "compact");
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

function makeDragData() {
  const store = new Map<string, string>();
  return {
    setData: (type: string, value: string) => {
      store.set(type, value);
    },
    getData: (type: string) => store.get(type) ?? "",
    clearData: () => {
      store.clear();
    },
  };
}

function twoInserts(): TrackInfo[] {
  const list = tracks(1);
  list[0] = {
    ...list[0],
    inserts: Array.from({ length: 8 }, (_, index) => ({
      name: index === 0 ? "Pro-Q" : index === 1 ? "Pro-C" : "",
      missing: false,
      bypassed: false,
    })),
  };
  return list;
}

describe("Mixer insert reorder", () => {
  it("drops slot 0 onto empty slot 2 and calls moveInsert", () => {
    const onMove = vi.fn();
    render(<Mixer {...props({ tracks: twoInserts(), onMove })} />);
    const dt = makeDragData() as unknown as DataTransfer;

    fireEvent.dragStart(screen.getByLabelText("Insert 1 Pro-Q"), { dataTransfer: dt });
    fireEvent.drop(screen.getByLabelText("Insert 3 Insert 3 is empty"), { dataTransfer: dt });
    expect(onMove).toHaveBeenCalledWith(0, 0, 2);
  });

  it("refuses to drop onto a slot that already holds a plugin", () => {
    const onMove = vi.fn();
    render(<Mixer {...props({ tracks: twoInserts(), onMove })} />);
    const dt = makeDragData() as unknown as DataTransfer;

    fireEvent.dragStart(screen.getByLabelText("Insert 1 Pro-Q"), { dataTransfer: dt });
    fireEvent.drop(screen.getByLabelText("Insert 2 Pro-C"), { dataTransfer: dt });
    expect(onMove).not.toHaveBeenCalled();
  });
});

describe("Mixer plugin editor", () => {
  it("opens the editor for a loaded slot", async () => {
    const onOpenEditor = vi.fn().mockResolvedValue({ ok: true });
    render(<Mixer {...props({ tracks: tracksWithInsert(0, 0), onOpenEditor })} />);

    fireEvent.doubleClick(screen.getByLabelText("Insert 1 Pro-Q"));
    expect(onOpenEditor).toHaveBeenCalledWith(0, 0);
  });

  it("surfaces the error when the slot has no plugin", async () => {
    const onOpenEditor = vi.fn().mockResolvedValue({ ok: false, error: "no plugin in slot" });
    render(<Mixer {...props({ tracks: tracksWithInsert(0, 0), onOpenEditor })} />);

    fireEvent.doubleClick(screen.getByLabelText("Insert 1 Pro-Q"));
    expect(await screen.findByRole("alert")).toHaveTextContent("no plugin in slot");
  });
});

describe("Mixer automation read", () => {
  it("toggles AUTO off for the strip's track", () => {
    const onAutomationRead = vi.fn();
    render(<Mixer {...props({ tracks: tracks(3), onAutomationRead })} />);

    fireEvent.click(screen.getByRole("button", { name: "AUTO Track 2" }));
    expect(onAutomationRead).toHaveBeenCalledWith(1, false);
  });

  it("reflects the parked state from the host", () => {
    const list = tracks(1);
    list[0] = { ...list[0], automationRead: false };
    render(<Mixer {...props({ tracks: list, onAutomationRead: vi.fn() })} />);

    expect(screen.getByRole("button", { name: "AUTO Track 1" })).toHaveAttribute(
      "aria-pressed",
      "false",
    );
  });
});
