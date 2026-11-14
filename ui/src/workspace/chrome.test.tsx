import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
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
      pan: 0,
      muted: true,
      soloed: false,
      inserts: [{ name: "Overlay Delay", missing: false }],
    },
  ],
  clips: [{ track: 0, startBar: 1, lengthBars: 4, assetId: 0, gainDb: 0, name: "INTRO" }],
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

describe("native chrome", () => {
  it("mirrors the native menus and the status strip", () => {
    setup();

    for (const name of ["FILE", "EDIT", "TRACK", "HELP"]) {
      expect(screen.getByRole("button", { name })).toBeInTheDocument();
    }
    expect(screen.queryByRole("button", { name: "VIEW" })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "FILE" }));
    expect(screen.getByRole("menuitem", { name: /Reset Transport/ })).toBeEnabled();
    expect(screen.getByRole("menuitem", { name: /Open Session/ })).toBeDisabled();

    const status = document.querySelector(".ws-status");
    expect(status?.textContent).toContain("2 OF 64 TRACKS");
    expect(status?.textContent).toContain("1 CLIPS");
    expect(status?.textContent).toContain("V SELECT");
  });

  it("keeps record honest until a host route exists and opens the browser only from an insert", () => {
    setup();

    const record = screen.getByRole("button", { name: "Record" });
    expect(record).toBeDisabled();
    expect(record).toHaveAttribute("title", "Recording is not available");
    expect(screen.queryByRole("button", { name: "Browse plug-ins" })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("tab", { name: "Mix" }));
    fireEvent.click(screen.getAllByLabelText(/^Insert 1 /)[0]);
    expect(document.querySelector(".ws-browser-target")?.textContent).toContain("TRACK 1");
  });

  it("shows the master A/B and automix controls next to a real peak readout", () => {
    setup();
    fireEvent.click(screen.getByRole("tab", { name: "Mix" }));

    expect(screen.getByRole("button", { name: "Master A/B" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Automix" })).toBeDisabled();
    expect(screen.getAllByText(/dBFS/).length).toBeGreaterThan(0);
    expect(screen.getByRole("button", { name: "Rack BASS" })).toBeInTheDocument();
  });

  it("names the inspector sends after the native returns", () => {
    setup();
    fireEvent.click(screen.getByRole("tab", { name: "Channel" }));

    expect(screen.getAllByText(/REVERB/i).length).toBeGreaterThan(0);
    expect(screen.getAllByText(/DELAY/i).length).toBeGreaterThan(0);
    expect(screen.queryByRole("slider", { name: "Width" })).not.toBeInTheDocument();
  });

  it("changes tempo from the keyboard", () => {
    const list = setup();
    const tempo = screen.getByRole("spinbutton", { name: "Tempo" });

    fireEvent.keyDown(tempo, { key: "ArrowUp" });
    expect(list.bpm).toHaveBeenCalledWith(129);
  });
});

describe("Mixology menu", () => {
  const PRESETS = ["VOCAL FOCUS", "808 WEIGHT", "VOCAL PRESENCE", "SUBTRACTIVE", "FULL CHAIN"];

  it("lists the presets in the Edit menu and applies the picked one", async () => {
    const apply = vi.fn().mockResolvedValue("MIXOLOGY / 5 INSERTS APPLIED");
    render(
      <Workspace
        state={STATE}
        transport={TRANSPORT}
        actions={{ ...actions(), applyMixology: apply }}
        mixologyPresets={PRESETS}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    for (const name of PRESETS) {
      expect(screen.getByRole("menuitem", { name })).toBeEnabled();
    }

    fireEvent.click(screen.getByRole("menuitem", { name: "808 WEIGHT" }));
    expect(apply).toHaveBeenCalledWith(1);
    expect(await screen.findByText("MIXOLOGY / 5 INSERTS APPLIED")).toBeInTheDocument();
  });
});

describe("Import Audio", () => {
  it("enables Import Audio when the host route exists and calls it", () => {
    const importAudio = vi.fn();
    render(
      <Workspace
        state={STATE}
        transport={TRANSPORT}
        actions={actions()}
        onImportAudio={importAudio}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "FILE" }));
    const item = screen.getByRole("menuitem", { name: /Import Audio/ });
    expect(item).toBeEnabled();
    fireEvent.click(item);
    expect(importAudio).toHaveBeenCalled();
  });

  it("keeps Import Audio honest until the host route exists", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    fireEvent.click(screen.getByRole("button", { name: "FILE" }));
    expect(screen.getByRole("menuitem", { name: /Import Audio/ })).toBeDisabled();
  });
});

describe("session and export menus", () => {
  const session = () => ({
    onOpenSession: vi.fn(),
    onSaveSession: vi.fn(),
    onSaveSessionAs: vi.fn(),
    onMixdownWav: vi.fn(),
    onExportStems: vi.fn(),
    onCollectMedia: vi.fn(),
  });

  it("wires every session and export entry when the host routes exist", () => {
    const hooks = session();
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} {...hooks} />);

    fireEvent.click(screen.getByRole("button", { name: "FILE" }));
    for (const name of [
      /Open Session/,
      /Save Session$/,
      /Save Session As/,
      /Mixdown WAV/,
      /Export Stems/,
      /Collect Media/,
    ]) {
      expect(screen.getByRole("menuitem", { name })).toBeEnabled();
    }
    fireEvent.click(screen.getByRole("menuitem", { name: /Save Session$/ }));
    expect(hooks.onSaveSession).toHaveBeenCalled();
  });

  it("keeps session entries honest without host routes", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    fireEvent.click(screen.getByRole("button", { name: "FILE" }));
    for (const name of [
      /Open Session/,
      /Save Session$/,
      /Save Session As/,
      /Mixdown WAV/,
      /Export Stems/,
      /Collect Media/,
    ]) {
      expect(screen.getByRole("menuitem", { name })).toBeDisabled();
    }
  });

  it("says where the desktop-only export formats live", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    fireEvent.click(screen.getByRole("button", { name: "FILE" }));
    for (const name of [/Mixdown FLAC/, /Mixdown MP3/, /Audition Stems/]) {
      const item = screen.getByRole("menuitem", { name });
      expect(item).toBeDisabled();
      expect(item.title).toMatch(/Desktop app/);
    }
  });
});

describe("edit menu parity", () => {
  it("wires undo and redo with their depths", () => {
    const list = { ...actions(), undo: vi.fn(), redo: vi.fn() };
    render(
      <Workspace
        state={{ ...STATE, undoDepth: 2, redoDepth: 1 }}
        transport={TRANSPORT}
        actions={list}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    expect(screen.getByRole("menuitem", { name: /Undo \(2\)/ })).toBeEnabled();
    expect(screen.getByRole("menuitem", { name: /Redo \(1\)/ })).toBeEnabled();
    fireEvent.click(screen.getByRole("menuitem", { name: /Undo \(2\)/ }));
    expect(list.undo).toHaveBeenCalled();
  });

  it("transcribes the selected audio clip from the MIDI section", () => {
    const list = { ...actions(), transcribeClip: vi.fn() };
    render(
      <Workspace
        state={{
          ...STATE,
          clips: [
            {
              track: 0,
              startBar: 1,
              lengthBars: 4,
              assetId: 0,
              gainDb: 0,
              name: "INTRO",
              kind: "audio",
            },
          ],
        }}
        transport={TRANSPORT}
        actions={list}
      />,
    );

    const clip = screen.getByRole("group", { name: /INTRO/ });
    clip.focus();
    fireEvent.keyDown(clip, { key: "Enter" });
    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    const item = screen.getByRole("menuitem", { name: /Transcribe Clip to MIDI/ });
    expect(item).toBeEnabled();
    fireEvent.click(item);
    expect(list.transcribeClip).toHaveBeenCalledWith(0);
  });

  it("marks the desktop-only edit entries with where they live", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    for (const name of [/MIDI Learn/, /Add Marker/, /Reverse Clip/, /Automix/]) {
      const item = screen.getByRole("menuitem", { name });
      expect(item).toBeDisabled();
      expect(item.title).toMatch(/Desktop app/);
    }
  });
});
