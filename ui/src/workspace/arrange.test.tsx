import { fireEvent, render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { AppState, TransportState } from "../types";
import { zoomWindow } from "./format";
import { Workspace, type WorkspaceActions } from "./Workspace";

const STATE: AppState = {
  bpm: 128,
  selectedTrack: 1,
  globalPitch: { transpose: 0, pitchCents: 0, tuningHz: 440 },
  tracks: [
    {
      index: 0,
      name: "DRUMS",
      instrumentName: "kit",
      gainDb: -3,
      pan: 0,
      muted: false,
      soloed: false,
      inserts: [{ name: "Retro", missing: false }],
    },
    {
      index: 1,
      name: "BASS",
      instrumentName: "sub",
      gainDb: 0,
      pan: 0,
      muted: false,
      soloed: false,
    },
  ],
  clips: [
    { track: 0, startBar: 1, lengthBars: 4, assetId: 0, gainDb: 0, name: "INTRO" },
    { track: 1, startBar: 5, lengthBars: 4, assetId: 1, gainDb: 0, name: "SUB" },
  ],
};

const TRANSPORT: TransportState = {
  playing: false,
  positionSeconds: 0,
  positionBar: 1,
  bpm: 128,
  loopEnabled: true,
  metronome: false,
  masterPeakLeft: 0,
  masterPeakRight: 0,
  masterDim: false,
  masterMono: false,
  trackPeaks: [0.4, 0.1],
};

function actions() {
  return {
    play: vi.fn(),
    stop: vi.fn(),
    rewind: vi.fn(),
    bpm: vi.fn(),
    loop: vi.fn(),
    metronome: vi.fn(),
    seek: vi.fn(),
    gain: vi.fn(),
    pan: vi.fn(),
    mute: vi.fn(),
    solo: vi.fn(),
    selectTrack: vi.fn(),
    addTrack: vi.fn(),
    removeTrack: vi.fn(),
    masterGain: vi.fn(),
    sendA: vi.fn(),
    sendB: vi.fn(),
    arm: vi.fn(),
    masterDim: vi.fn(),
    masterMono: vi.fn(),
    globalPitch: vi.fn(),
    trackPitch: vi.fn(),
    instrumentPitch: vi.fn(),
    editClip: vi.fn(),
    splitClip: vi.fn(),
    duplicateClip: vi.fn(),
    deleteClip: vi.fn(),
    loopRange: vi.fn(),
    undo: vi.fn(),
    redo: vi.fn(),
  } satisfies WorkspaceActions;
}

function setup() {
  const list = actions();
  render(<Workspace state={STATE} transport={TRANSPORT} actions={list} />);
  return list;
}

// jsdom has no PointerEvent, and fireEvent.pointerMove drops coordinates, so
// gestures that depend on them are driven with a real MouseEvent-shaped one.
const pointer = (type: string, clientX: number, clientY = 20) =>
  new MouseEvent(type, { bubbles: true, cancelable: true, clientX, clientY });

beforeEach(() => {
  vi.spyOn(HTMLElement.prototype, "getBoundingClientRect").mockReturnValue({
    x: 0,
    y: 0,
    width: 800,
    height: 400,
    top: 0,
    left: 0,
    right: 800,
    bottom: 400,
    toJSON: () => ({}),
  } as DOMRect);
});

describe("arrange parity", () => {
  it("shows the native tool cluster, functions and loop region", () => {
    setup();

    for (const label of [
      "Select tool (1)",
      "Trim tool (2)",
      "Fade tool (3)",
      "Gain tool (4)",
      "Split tool (5)",
      "Erase tool (6)",
    ]) {
      expect(screen.getByRole("button", { name: label })).toBeInTheDocument();
    }

    expect(screen.getByRole("slider", { name: "Loop region" })).toHaveAttribute(
      "aria-valuetext",
      "Bars 1 to 17",
    );
    expect(document.querySelectorAll(".ws-lane .ws-clip")).toHaveLength(2);
    // Single channel is not in arranger view; header FIT never scrolls.
    expect(document.querySelector(".ws-arrange-side")).toBeNull();
    expect(document.querySelectorAll(".ws-head-insert")).toHaveLength(0);
    expect(screen.getByRole("button", { name: "FIT" })).toBeInTheDocument();
  });

  it("erases a clip with the erase tool and splits one with the split tool", () => {
    const list = setup();

    fireEvent.click(screen.getByRole("button", { name: "Erase tool (6)" }));
    fireEvent.pointerDown(document.querySelectorAll(".ws-lane .ws-clip")[0]);
    expect(document.querySelectorAll(".ws-lane .ws-clip")).toHaveLength(1);
    expect(list.deleteClip).toHaveBeenCalledWith(0);

    fireEvent.click(screen.getByRole("button", { name: "Split tool (5)" }));
    const clip = document.querySelectorAll(".ws-lane .ws-clip")[0];
    fireEvent.pointerDown(clip, { clientX: 150, clientY: 20 });
    expect(document.querySelectorAll(".ws-lane .ws-clip").length).toBeGreaterThanOrEqual(2);
    expect(document.body.textContent).not.toContain("NaN");
    expect(list.splitClip).toHaveBeenCalledTimes(1);
    expect(list.splitClip).toHaveBeenCalledWith(0, expect.any(Number));
  });

  it("arms a track from its header", () => {
    const list = setup();

    fireEvent.click(screen.getByLabelText("Arm DRUMS"));
    expect(list.arm).toHaveBeenCalledWith(0, true);
  });

  it("runs the DEL function on the selected clip", () => {
    const list = setup();

    fireEvent.pointerDown(document.querySelectorAll(".ws-lane .ws-clip")[0]);
    fireEvent.click(screen.getByRole("button", { name: "DEL" }));
    expect(document.querySelectorAll(".ws-lane .ws-clip")).toHaveLength(1);
    expect(list.deleteClip).toHaveBeenCalledWith(0);
  });

  it("hands a finished move, the loop range and duplicate edits to the host", () => {
    const list = setup();

    const clip = document.querySelectorAll(".ws-lane .ws-clip")[0];
    fireEvent(clip, pointer("pointerdown", 100));
    fireEvent(clip, pointer("pointermove", 150));
    fireEvent(clip, pointer("pointerup", 150));
    expect(list.editClip).toHaveBeenCalledTimes(1);
    expect(list.editClip).toHaveBeenCalledWith(
      0,
      expect.objectContaining({ startBar: 3, trimmed: false }),
    );

    const loop = screen.getByRole("slider", { name: "Loop region" });
    fireEvent(loop, pointer("pointerdown", 0, 5));
    fireEvent(loop, pointer("pointermove", 50, 5));
    fireEvent(loop, pointer("pointerup", 50, 5));
    expect(list.loopRange).toHaveBeenCalledWith(3, 17);

    fireEvent.click(screen.getByRole("button", { name: "Select tool (1)" }));
    fireEvent.pointerDown(document.querySelectorAll(".ws-lane .ws-clip")[0]);
    fireEvent.click(screen.getByRole("button", { name: "DUP" }));
    expect(list.duplicateClip).toHaveBeenCalledWith(0);
  });

  it("seeds the loop from the transport the native engine reports", () => {
    const list = actions();
    render(
      <Workspace
        state={STATE}
        transport={{ ...TRANSPORT, loopStartBar: 3, loopEndBar: 9 }}
        actions={list}
      />,
    );

    expect(screen.getByRole("slider", { name: "Loop region" })).toHaveAttribute(
      "aria-valuetext",
      "Bars 3 to 9",
    );
  });

  it("renders MIDI clips from the arrangement list with their notes", () => {
    render(
      <Workspace
        state={{
          ...STATE,
          clips: [
            ...(STATE.clips ?? []),
            {
              track: 1,
              startBar: 9,
              lengthBars: 4,
              gainDb: 0,
              name: "LEAD",
              kind: "midi",
              notes: [{ startBeat: 0, lengthBeats: 1, pitch: 60, velocity: 100 }],
            },
          ],
        }}
        transport={TRANSPORT}
        actions={actions()}
      />,
    );

    expect(screen.getByTitle(/LEAD · bar 9 · 4 bars/)).toBeInTheDocument();
    expect(document.querySelectorAll(".ws-clip-notes i")).toHaveLength(1);
  });

  it("wires toolbar history to the host depths", () => {
    const list = actions();
    render(
      <Workspace
        state={{ ...STATE, undoDepth: 2, redoDepth: 1 }}
        transport={TRANSPORT}
        actions={list}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Undo" }));
    fireEvent.click(screen.getByRole("button", { name: "Redo" }));
    expect(list.undo).toHaveBeenCalledTimes(1);
    expect(list.redo).toHaveBeenCalledTimes(1);
  });

  it("disables history at the ends of the stack", () => {
    render(
      <Workspace
        state={{ ...STATE, undoDepth: 0, redoDepth: 0 }}
        transport={TRANSPORT}
        actions={actions()}
      />,
    );

    expect(screen.getByRole("button", { name: "Undo" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Redo" })).toBeDisabled();
  });
});

describe("anchored zoom buttons", () => {
  const renderAtPlayhead = (positionBar: number) => {
    render(
      <Workspace state={STATE} transport={{ ...TRANSPORT, positionBar }} actions={actions()} />,
    );
  };
  const readout = () => document.querySelector(".ws-zoom-read b")?.textContent;

  it("zooms the + button around the playhead instead of the window start", () => {
    renderAtPlayhead(9);
    expect(readout()).toBe("32 BARS");
    fireEvent.click(screen.getByRole("button", { name: "Zoom in" }));
    const expected = zoomWindow(1, 32, (9 - 1) / 32, "in", 4, 256);
    expect(readout()).toBe(Math.round(expected.visibleBars) + " BARS");
    // The old unanchored math showed 24 BARS with the start pinned at bar 1.
    expect(readout()).not.toBe("24 BARS");
  });

  it("returns to the exact window when - follows +", () => {
    renderAtPlayhead(9);
    fireEvent.click(screen.getByRole("button", { name: "Zoom in" }));
    fireEvent.click(screen.getByRole("button", { name: "Zoom out" }));
    expect(readout()).toBe("32 BARS");
  });

  it("clamps the anchor when the playhead is past the window", () => {
    renderAtPlayhead(200);
    fireEvent.click(screen.getByRole("button", { name: "Zoom in" }));
    const expected = zoomWindow(1, 32, 1, "in", 4, 256);
    expect(readout()).toBe(Math.round(expected.visibleBars) + " BARS");
  });

  describe("track headers", () => {
    it("gives the name row to the name and the second row to the controls", () => {
      setup();

      const head = document.querySelector('.ws-head[aria-current="true"]') as HTMLElement;
      const rows = head.querySelectorAll(":scope .ws-head-main > .ws-head-row");
      expect(rows.length).toBe(2);
      expect(rows[0].textContent).toContain("BASS");
      expect(rows[0].querySelectorAll("button").length).toBe(1);
      expect(rows[1].querySelectorAll("button").length).toBe(3);
    });

    it("shows the gain value with the full track label as its title", () => {
      setup();

      const db = document.querySelector('.ws-head[aria-current="true"] .ws-head-db') as HTMLElement;
      expect(db.textContent).toBe("0.0 dB");
      expect(db.title).toBe("T2 · 0.0 dB");
    });
  });
});
