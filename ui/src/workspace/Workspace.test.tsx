import { fireEvent, render, screen, within } from "@testing-library/react";
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
    },
    {
      index: 1,
      name: "BASS",
      instrumentName: "sub",
      gainDb: 0,
      pan: -0.2,
      muted: true,
      soloed: false,
    },
  ],
  clips: [
    { track: 0, startBar: 1, lengthBars: 4, assetId: 0, gainDb: 0, name: "INTRO" },
    { track: 1, startBar: 5, lengthBars: 4, assetId: 1, gainDb: 0, name: "SUB MOTION" },
  ],
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

describe("Workspace", () => {
  it("shows the session transport and every track header", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    expect(screen.getByText("128.0")).toBeInTheDocument();
    expect(screen.getByText("001|01|000")).toBeInTheDocument();
    expect(screen.getByText("DRUMS")).toBeInTheDocument();
    // The header leads with the track name; the numbered badge keeps the index.
    expect(document.querySelector('.ws-head[aria-current="true"] .ws-head-db')).toHaveAttribute(
      "title",
      "T2 · 0.0 dB",
    );
    expect(document.querySelector('.ws-head-name[title="BASS"]')).not.toBeNull();
  });

  it("draws timeline clips with their bar position", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    expect(screen.getByTitle(/INTRO · bar 1 · 4 bars/)).toBeInTheDocument();
    expect(screen.getByTitle(/SUB MOTION · bar 5 · 4 bars/)).toBeInTheDocument();
  });

  it("reflects mute state on the track header", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    const arrange = document.querySelector(".ws-arrange") as HTMLElement;
    const bassHead = document.querySelector('.ws-head[aria-current="true"]') as HTMLElement;

    expect(within(bassHead).getByLabelText("Mute BASS")).toHaveAttribute("aria-pressed", "true");
    expect(within(arrange).getByLabelText("Mute DRUMS")).toHaveAttribute("aria-pressed", "false");
  });

  it("moves between workspace tabs with the arrow keys", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);

    const arrange = screen.getByRole("tab", { name: "Arrange" });
    arrange.focus();
    fireEvent.keyDown(arrange, { key: "ArrowRight" });

    const mix = screen.getByRole("tab", { name: "Mix" });
    expect(mix).toHaveAttribute("aria-selected", "true");
    expect(mix).toHaveFocus();
  });

  it("does not steal Space from a focused button", () => {
    const list = actions();
    render(<Workspace state={STATE} transport={TRANSPORT} actions={list} />);

    const mute = screen.getByLabelText("Mute DRUMS");
    mute.focus();
    fireEvent.keyDown(mute, { code: "Space", key: " " });

    expect(list.play).not.toHaveBeenCalled();
  });
});

describe("instrument sync", () => {
  const syncActions = () => ({ ...actions(), instrumentPitch: vi.fn() });
  const pitched = (transpose: number) => ({
    ...STATE,
    tracks: STATE.tracks.map((t) => (t.index === 1 ? { ...t, transpose } : t)),
  });

  it("mirrors track-pitch changes onto the instrument while sync is on", () => {
    const list = syncActions();
    const { rerender } = render(<Workspace state={STATE} transport={TRANSPORT} actions={list} />);
    fireEvent.click(screen.getByRole("tab", { name: "Channel" }));
    fireEvent.click(screen.getByRole("button", { name: "Instrument follows track pitch" }));

    expect(list.instrumentPitch).toHaveBeenCalledWith(1, {
      transpose: 0,
      pitchCents: 0,
      tuningHz: 440,
    });

    rerender(<Workspace state={pitched(2)} transport={TRANSPORT} actions={list} />);
    expect(list.instrumentPitch).toHaveBeenLastCalledWith(1, {
      transpose: 2,
      pitchCents: 0,
      tuningHz: 440,
    });
  });

  it("leaves the instrument alone when sync is off", () => {
    const list = syncActions();
    const { rerender } = render(<Workspace state={STATE} transport={TRANSPORT} actions={list} />);
    rerender(<Workspace state={pitched(2)} transport={TRANSPORT} actions={list} />);
    expect(list.instrumentPitch).not.toHaveBeenCalled();
  });

  it("flips sync from the Inspector toggle", () => {
    render(<Workspace state={STATE} transport={TRANSPORT} actions={actions()} />);
    fireEvent.click(screen.getByRole("tab", { name: "Channel" }));

    expect(screen.getByRole("button", { name: "Instrument follows track pitch" })).toHaveAttribute(
      "aria-pressed",
      "false",
    );
    fireEvent.click(screen.getByRole("button", { name: "Instrument follows track pitch" }));
    expect(screen.getByRole("button", { name: "Instrument follows track pitch" })).toHaveAttribute(
      "aria-pressed",
      "true",
    );
  });
});

describe("plugin load targets", () => {
  const pluginState = {
    ...STATE,
    plugins: [
      { id: "acme.sub", name: "Sub", vendor: "Acme", category: "instrument", format: "VST3" },
      { id: "acme.retro", name: "Retro", vendor: "Acme", category: "effect", format: "VST3" },
    ],
  };

  it("refuses an instrument on the master bus", () => {
    const masterLoad = vi.fn();
    render(
      <Workspace
        state={pluginState}
        transport={TRANSPORT}
        actions={actions()}
        onMasterPluginLoad={masterLoad}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    fireEvent.click(screen.getByRole("menuitem", { name: /Load Master Plugin/ }));
    fireEvent.doubleClick(screen.getByRole("button", { name: /Sub/ }));

    expect(masterLoad).not.toHaveBeenCalled();
    expect(document.querySelector(".ws-status")?.textContent).toContain(
      "INSTRUMENTS LOAD ON TRACKS",
    );
  });

  it("loads an effect on the master bus", () => {
    const masterLoad = vi.fn();
    render(
      <Workspace
        state={pluginState}
        transport={TRANSPORT}
        actions={actions()}
        onMasterPluginLoad={masterLoad}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: "EDIT" }));
    fireEvent.click(screen.getByRole("menuitem", { name: /Load Master Plugin/ }));
    fireEvent.doubleClick(screen.getByRole("button", { name: /Retro/ }));

    expect(masterLoad).toHaveBeenCalledWith(expect.objectContaining({ id: "acme.retro" }));
  });

  describe("native status", () => {
    it("shows the host status line in the strip when no local flash is active", () => {
      render(
        <Workspace
          state={{ ...STATE, statusMessage: "BOUNCE / mix.wav", statusAlert: true }}
          transport={TRANSPORT}
          actions={actions()}
        />,
      );

      const msg = screen.getByRole("status");
      expect(msg).toHaveTextContent("BOUNCE / mix.wav");
      expect(msg.previousElementSibling).toHaveClass("is-alert");
    });
  });
});
