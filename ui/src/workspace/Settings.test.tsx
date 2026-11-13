import { fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { useState } from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { backend, type NativeAudioState } from "../lib/bridge";
import { DEFAULT_SETTINGS, SettingsPanel, type UiSettings } from "./Settings";

vi.mock("../lib/bridge", () => ({
  backend: {
    embedded: true,
    getAudioState: vi.fn(),
    applyAudioSettings: vi.fn(),
  },
}));

const ENGINE_STATE: NativeAudioState = {
  deviceType: "ASIO",
  outputDevice: "Focusrite",
  sampleRate: 48000,
  bufferSize: 128,
  outputChannels: 2,
  inputChannels: 2,
  open: true,
  currentRate: 48000,
  currentBuffer: 128,
  device: "Focusrite",
  overruns: 0,
  blocks: 0,
  types: [{ name: "ASIO", outputs: ["Focusrite"] }],
};

const getAudioState = vi.mocked(backend.getAudioState);
const applyAudioSettings = vi.mocked(backend.applyAudioSettings);

function Harness({ initial }: { initial: UiSettings }) {
  const [settings, setSettings] = useState(initial);
  return <SettingsPanel settings={settings} onChange={setSettings} />;
}

const settings = (patch: Partial<UiSettings> = {}): UiSettings => ({
  ...DEFAULT_SETTINGS,
  driver: "asio",
  ...patch,
});

beforeEach(() => {
  vi.clearAllMocks();
  getAudioState.mockResolvedValue(ENGINE_STATE);
  applyAudioSettings.mockResolvedValue({
    ok: true,
    error: "",
    note: "Applied from test",
    state: ENGINE_STATE,
  });
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: {
      enumerateDevices: vi.fn().mockResolvedValue([
        { kind: "audioinput", deviceId: "mic-1", label: "Studio Mic" },
        { kind: "audiooutput", deviceId: "speaker-1", label: "Studio Speakers" },
      ]),
    },
  });
});

describe("Settings audio interface", () => {
  it("lists the devices the native engine actually opened", async () => {
    render(<Harness initial={settings()} />);

    const output = screen.getByRole("combobox", { name: "Audio output" });
    await waitFor(() =>
      expect(within(output).getByRole("option", { name: "Focusrite" })).toBeInTheDocument(),
    );
    expect(within(output).getByRole("option", { name: "Focusrite" })).toHaveValue("Focusrite");
    expect(getAudioState).toHaveBeenCalled();
  });

  it("saves the selected input device", async () => {
    render(<Harness initial={settings()} />);

    const input = screen.getByRole("combobox", { name: "Audio input" });
    await waitFor(() =>
      expect(within(input).getByRole("option", { name: "Studio Mic" })).toBeInTheDocument(),
    );

    fireEvent.change(input, { target: { value: "mic-1" } });
    expect(input).toHaveValue("mic-1");
  });

  it("applies the native output, rate and low-latency buffer", async () => {
    render(
      <Harness
        initial={settings({
          audioOut: "Focusrite",
          bufferSize: 512,
          lowLatency: true,
          sampleRate: 48000,
        })}
      />,
    );

    const output = screen.getByRole("combobox", { name: "Audio output" });
    await waitFor(() =>
      expect(within(output).getByRole("option", { name: "Focusrite" })).toBeInTheDocument(),
    );
    fireEvent.click(screen.getByRole("button", { name: "Apply to engine" }));

    await waitFor(() =>
      expect(applyAudioSettings).toHaveBeenCalledWith("ASIO", "Focusrite", 48000, 64),
    );
  });

  it("does not send a browser device id as a JUCE output name", async () => {
    render(<Harness initial={settings({ audioOut: "speaker-1" })} />);

    const output = screen.getByRole("combobox", { name: "Audio output" });
    await waitFor(() =>
      expect(within(output).getByRole("option", { name: "Focusrite" })).toBeInTheDocument(),
    );
    fireEvent.click(screen.getByRole("button", { name: "Apply to engine" }));

    await waitFor(() => expect(applyAudioSettings).toHaveBeenCalledWith("ASIO", "", 48000, 128));
  });
});
