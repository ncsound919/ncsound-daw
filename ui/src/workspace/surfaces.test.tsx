import { fireEvent, render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { AppState, TransportState } from "../types";
import { Workspace, type WorkspaceActions } from "./Workspace";

const PITCH = { transpose: 0, pitchCents: 0, tuningHz: 440 };

const STATE: AppState = {
  bpm: 128,
  selectedTrack: 1,
  globalPitch: PITCH,
  tracks: [
    {
      index: 0,
      name: "DRUMS",
      instrumentName: "kit",
      gainDb: -3,
      pan: 0,
      muted: false,
      soloed: false,
      inserts: [{ name: "Overlay Retro Color", missing: false }],
    },
    {
      index: 1,
      name: "BASS",
      instrumentName: "sub",
      gainDb: 0,
      pan: -0.2,
      muted: true,
      soloed: false,
      sendA: 0.2,
      sendB: 0.1,
    },
  ],
  clips: [
    { track: 0, startBar: 1, lengthBars: 4, assetId: 0, gainDb: 0, name: "INTRO" },
    { track: 1, startBar: 5, lengthBars: 4, assetId: 1, gainDb: 0, name: "SUB MOTION" },
  ],
};

const PLUGIN = {
  id: "acme.retro",
  name: "Retro",
  vendor: "Acme",
  category: "effect" as const,
  scanned: true,
};

const TRANSPORT: TransportState = {
  playing: false,
  positionSeconds: 0,
  positionBar: 1,
  bpm: 128,
  loopEnabled: true,
  metronome: false,
  masterPeakLeft: 0.2,
  masterPeakRight: 0.1,
  masterDim: false,
  masterMono: false,
  trackPeaks: [0.3, 0.1],
};

function actions(): WorkspaceActions {
  const noop = vi.fn();
  return {
    play: noop,
    stop: noop,
    rewind: noop,
    bpm: noop,
    loop: noop,
    metronome: noop,
    seek: noop,
    gain: noop,
    pan: noop,
    mute: noop,
    solo: noop,
    selectTrack: noop,
    addTrack: noop,
    removeTrack: noop,
    masterGain: noop,
    sendA: noop,
    sendB: noop,
    arm: noop,
    masterDim: noop,
    masterMono: noop,
    globalPitch: noop,
    trackPitch: noop,
    instrumentPitch: noop,
  };
}

function setup() {
  const list = actions();
  render(<Workspace state={STATE} transport={TRANSPORT} actions={list} />);
  return list;
}

function openView(label: string) {
  fireEvent.click(screen.getByRole("tab", { name: label }));
}

const pointer = (type: string, clientX: number, clientY = 20) =>
  new MouseEvent(type, { bubbles: true, cancelable: true, clientX, clientY });

beforeEach(() => {
  // jsdom has no layout engine: every rect is zero-sized, which would make the
  // pointer-to-beat maths divide by zero.
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

describe("workspace surfaces", () => {
  it("reaches every surface the native app owns", () => {
    setup();

    for (const label of ["Arrange", "Mix", "Channel", "Piano Roll", "Automation"]) {
      expect(screen.getByRole("tab", { name: label })).toBeInTheDocument();
    }
    expect(screen.queryByRole("tab", { name: "Browser" })).not.toBeInTheDocument();
    expect(screen.queryByRole("tab", { name: "Pitch" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Browse plug-ins" })).not.toBeInTheDocument();
  });

  it("inspector shows the selected track with four hosted inserts and live flags", () => {
    const list = setup();
    openView("Channel");

    expect(screen.getAllByText("TRACK 2").length).toBeGreaterThan(0);
    expect(document.querySelectorAll(".ws-ins-slot")).toHaveLength(4);
    expect(screen.getByText("20%")).toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Mute BASS"));
    expect(list.mute).toHaveBeenCalledWith(1, false);
  });

  it("fuses the insert chain with the stock faceplate in one channel view", () => {
    // Selection is owned by the host, so render the fixture already on the
    // track that owns the insert rather than clicking a mocked action.
    render(<Workspace state={{ ...STATE, selectedTrack: 0 }} transport={TRANSPORT} actions={actions()} />);
    fireEvent.click(screen.getByRole("tab", { name: "Channel" }));

    expect(document.querySelector(".ws-editor-face")).not.toBeNull();
    // Preview controls must not be reachable by keyboard or assistive tech.
    expect(document.querySelector(".ws-editor-face")).toHaveAttribute("inert", "");
    expect(document.querySelector(".ws-editor-title")?.textContent).toContain("Overlay Retro Color");

    expect(document.querySelectorAll(".ws-ins-slot")).toHaveLength(4);
  });

  it("piano roll adds a note and deletes it again", () => {
    setup();

    fireEvent.pointerDown(screen.getByTitle(/INTRO · bar 1 · 4 bars/), { button: 0 });
    openView("Piano Roll");

    expect(screen.getByText(/0 NOTES/)).toBeInTheDocument();

    fireEvent.pointerDown(screen.getByTitle("Click to add a note"), { clientX: 120, clientY: 60 });
    expect(screen.getByText(/1 NOTES/)).toBeInTheDocument();

    fireEvent.keyDown(screen.getByLabelText(/Piano roll for INTRO/), { key: "Delete" });
    expect(screen.getByText(/0 NOTES/)).toBeInTheDocument();
  });

  it("automation adds a breakpoint and clears the lane", () => {
    setup();
    openView("Automation");

    fireEvent.pointerDown(screen.getByRole("application"), { clientX: 200, clientY: 120 });
    expect(screen.getByText(/1 POINTS/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Clear" }));
    expect(screen.getByText(/0 POINTS/)).toBeInTheDocument();
  });

  it("shows MIDI clip notes in the arrangement and the piano roll", () => {
    render(
      <Workspace
        state={{
          ...STATE,
          clips: [
            {
              track: 0,
              startBar: 1,
              lengthBars: 4,
              gainDb: 0,
              name: "LEAD",
              kind: "midi",
              notes: [
                { startBeat: 0, lengthBeats: 1, pitch: 60, velocity: 100 },
                { startBeat: 1, lengthBeats: 0.5, pitch: 64, velocity: 90 },
              ],
            },
          ],
        }}
        transport={TRANSPORT}
        actions={actions()}
      />,
    );

    expect(document.querySelectorAll(".ws-clip-notes i")).toHaveLength(2);
    fireEvent.pointerDown(screen.getByTitle(/LEAD · bar 1 · 4 bars/));
    openView("Piano Roll");
    expect(screen.getByText(/2 NOTES/)).toBeInTheDocument();
  });

  it("commits a piano-roll drag once, on pointer up", () => {
    const onNotes = vi.fn();
    render(
      <Workspace state={STATE} transport={TRANSPORT} actions={actions()} onNotesChanged={onNotes} />,
    );
    fireEvent.pointerDown(screen.getByTitle(/INTRO · bar 1 · 4 bars/));
    openView("Piano Roll");

    const grid = screen.getByTitle("Click to add a note");
    fireEvent.pointerDown(grid, { clientX: 120, clientY: 60 });
    expect(onNotes).toHaveBeenCalledTimes(1);

    const note = document.querySelector(".pr-note") as HTMLElement;
    fireEvent(note, pointer("pointerdown", 120, 60));
    // jsdom has no pointer capture: the grid would be the captured target in
    // the browser, so drive the move/up there.
    fireEvent(grid, pointer("pointermove", 160, 60));
    expect(onNotes).toHaveBeenCalledTimes(1);

    fireEvent(grid, pointer("pointerup", 160, 60));
    expect(onNotes).toHaveBeenCalledTimes(2);
    expect(onNotes.mock.calls[1][1]).toHaveLength(1);
  });

  it("commits an automation drag once, on pointer up", () => {
    const onAuto = vi.fn();
    render(
      <Workspace
        state={STATE}
        transport={TRANSPORT}
        actions={actions()}
        onAutomationChanged={onAuto}
      />,
    );
    openView("Automation");

    const surface = screen.getByRole("application");
    fireEvent.pointerDown(surface, { clientX: 200, clientY: 120 });
    expect(onAuto).toHaveBeenCalledTimes(1);

    const point = document.querySelector(".ws-auto-point") as HTMLElement;
    fireEvent(point, pointer("pointerdown", 200, 120));
    fireEvent(surface, pointer("pointermove", 250, 100));
    expect(onAuto).toHaveBeenCalledTimes(1);

    fireEvent(surface, pointer("pointerup", 250, 100));
    expect(onAuto).toHaveBeenCalledTimes(2);
  });

  it("loads a catalogue row by id into the target insert", () => {
    const onLoad = vi.fn();
    const onScan = vi.fn();
    render(
      <Workspace
        state={{ ...STATE, plugins: [PLUGIN] }}
        transport={TRANSPORT}
        actions={actions()}
        onPluginLoad={onLoad}
        onScan={onScan}
      />,
    );

    fireEvent.click(screen.getByRole("tab", { name: "Mix" }));
    fireEvent.click(screen.getAllByLabelText(/^Insert 1 /)[0]);
    expect(document.querySelector(".ws-browser-target")?.textContent).toContain("TRACK 1");
    fireEvent.click(screen.getByRole("button", { name: "Scan" }));
    expect(onScan).toHaveBeenCalledTimes(1);

    fireEvent.click(screen.getByRole("button", { name: "Load" }));
    expect(onLoad).toHaveBeenCalledWith(expect.objectContaining({ id: "acme.retro" }), 0, 0);
  });

  it("loads the master insert from the master slot", () => {
    const onMaster = vi.fn();
    render(
      <Workspace
        state={{ ...STATE, plugins: [PLUGIN] }}
        transport={TRANSPORT}
        actions={actions()}
        onMasterPluginLoad={onMaster}
      />,
    );

    openView("Mix");
    fireEvent.click(screen.getByRole("button", { name: "Load master insert" }));
    expect(document.querySelector(".ws-browser-target")?.textContent).toContain("MASTER");
    fireEvent.click(screen.getByRole("button", { name: "Load" }));
    expect(onMaster).toHaveBeenCalledWith(expect.objectContaining({ id: "acme.retro" }));
  });

  it("browser states that the catalogue is empty instead of inventing plug-ins", () => {
    const list = setup();
    // Browser only appears as the insert-load dialog, not a tab.
    expect(screen.queryByRole("dialog", { name: "Load plug-in" })).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("tab", { name: "Mix" }));
    fireEvent.click(screen.getAllByLabelText(/^Insert 1 /)[0]);
    expect(screen.getByRole("dialog", { name: "Load plug-in" })).toBeInTheDocument();
    expect(screen.getByText(/No plug-ins indexed yet/)).toBeInTheDocument();
    expect(list.globalPitch).toBeDefined();
  });
});
