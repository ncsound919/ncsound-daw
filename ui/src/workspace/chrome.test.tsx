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
    expect(status?.textContent).toContain("2 OF 8 TRACKS");
    expect(status?.textContent).toContain("1 CLIPS");
    expect(status?.textContent).toContain("V SELECT");
  });

  it("arms record from the transport and opens the browser only from an insert", () => {
    setup();

    const record = screen.getByRole("button", { name: "Record" });
    expect(record).toBeEnabled();
    fireEvent.click(record);
    expect(record).toHaveAttribute("aria-pressed", "true");
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
