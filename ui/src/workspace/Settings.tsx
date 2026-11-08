import { useEffect, useState } from "react";

export interface UiSettings {
  accent: string;
  tintAmount: number; // 0..30 (% of track tint in strips/heads)
  clipBoldness: number; // 0..100
  density: "comfortable" | "compact";
  loopStrength: number; // 0..100
  // DAW behaviour (not cosmetics) — each feeds a live control below.
  bpm: number; // 20..300
  trackHeight: number; // 60..120 px default lane height
  snap: boolean; // arrangement snap default
  fadeBars: number; // 0..4 bars default fade for new splits
  clipGain: number; // -12..+12 dB default for new clips
  metronome: boolean; // click on?
  loop: boolean; // loop on?
  masterGain: number; // -12..+6 dB master default
  // Audio hardware (honest: WebAudio lists what the browser exposes; the
  // native JUCE engine picks the ASIO device at runtime).
  audioIn: string; // deviceId ("" = system default)
  audioOut: string;
  driver: "asio" | "wasapi" | "directsound" | "default"; // preferred driver
  sampleRate: number; // 44100 | 48000 | 88000 | 96000
  bufferSize: number; // 32 | 64 | 128 | 256 | 512 | 1024
  lowLatency: boolean;
  midiIn: string; // MIDI input id ("" = none)
  midiEnabled: boolean;
}

export const DEFAULT_SETTINGS: UiSettings = {
  accent: "#e0824d",
  tintAmount: 7,
  clipBoldness: 42,
  density: "comfortable",
  loopStrength: 60,
  bpm: 120,
  trackHeight: 84,
  snap: true,
  fadeBars: 0,
  clipGain: 0,
  metronome: false,
  loop: true,
  masterGain: 0,
  audioIn: "",
  audioOut: "",
  driver: "default",
  sampleRate: 48000,
  bufferSize: 128,
  lowLatency: false,
  midiIn: "",
  midiEnabled: true,
};

const KEY = "ncsound-ui-settings";

interface AudioDevice {
  deviceId: string;
  label: string;
}

const DRIVERS = [
  { id: "asio", label: "ASIO (native)" },
  { id: "wasapi", label: "WASAPI" },
  { id: "directsound", label: "DirectSound" },
  { id: "default", label: "Default" },
] as const;

const RATES = [44100, 48000, 88000, 96000];
const BUFFERS = [32, 64, 128, 256, 512, 1024];

function useAudioDevices() {
  const [devices, setDevices] = useState<AudioDevice[]>([]);
  const [note, setNote] = useState("Click Refresh to list devices (browser permission may be needed for names).");
  const refresh = async () => {
    try {
      const list = await navigator.mediaDevices.enumerateDevices();
      setDevices(
        list
          .filter((d) => d.kind === "audioinput" || d.kind === "audiooutput")
          .map((d) => ({ deviceId: d.deviceId, label: d.label || `${d.kind} ${d.deviceId.slice(0, 6)}` })),
      );
      setNote(
        list.some((d) => d.label)
          ? "Names shown."
          : "No names — grant microphone permission, then refresh (browser hides labels until then).",
      );
    } catch {
      setNote("Device enumeration is not available in this WebView.");
    }
  };
  useEffect(() => {
    refresh();
  }, []);
  return { devices, note, refresh };
}

function useMidiDevices(enabled: boolean) {
  const [names, setNames] = useState<string[]>([]);
  const [note, setNote] = useState("No controller selected.");
  useEffect(() => {
    if (!enabled) {
      setNames([]);
      setNote("MIDI disabled.");
      return;
    }
    let cancelled = false;
    const nav = navigator as Navigator & { requestMIDIAccess?: (o?: unknown) => Promise<MIDIAccess> };
    nav
      .requestMIDIAccess?.({ sysex: false })
      .then((access) => {
        if (cancelled) return;
        const found = Array.from(access.inputs.values()).map((input) => input.name ?? input.id ?? "MIDI input");
        setNames(found);
        setNote(found.length === 0 ? "No MIDI controllers found." : `${found.length} controller(s) found.`);
      })
      .catch(() => {
        if (!cancelled) setNote("Web MIDI needs permission (chrome://settings) or is unavailable in WebView2.");
      });
    return () => {
      cancelled = true;
    };
  }, [enabled]);
  return { names, note };
}

export function loadSettings(): UiSettings {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return DEFAULT_SETTINGS;
    return { ...DEFAULT_SETTINGS, ...(JSON.parse(raw) as Partial<UiSettings>) };
  } catch {
    return DEFAULT_SETTINGS;
  }
}

const ACCENTS: { hex: string; name: string }[] = [
  { hex: "#e0824d", name: "Rust" },
  { hex: "#ffb020", name: "Amber" },
  { hex: "#8fe06a", name: "Lime" },
  { hex: "#4fd6c0", name: "Teal" },
  { hex: "#e07ab8", name: "Rose" },
  { hex: "#f8e8c8", name: "Cream" },
];

export function accentHot(hex: string): string {
  return hex === "#f8e8c8" ? "#e0824d" : hex;
}

export function SettingsPanel({
  settings,
  onChange,
}: {
  settings: UiSettings;
  onChange: (next: UiSettings) => void;
}) {
  const set = (patch: Partial<UiSettings>) => {
    const next = { ...settings, ...patch };
    try {
      localStorage.setItem(KEY, JSON.stringify(next));
    } catch {
      // storage may be unavailable in the WebView — keep in-memory only
    }
    onChange(next);
  };

  const { devices, note: audioNote, refresh: refreshDevices } = useAudioDevices();
  const { names: midiNames, note: midiNote } = useMidiDevices(settings.midiEnabled);
  const windows = typeof window !== "undefined" && /win/i.test(window.navigator.platform);
  const asioNote = settings.driver === "asio"
    ? windows
      ? "ASIO is preferred — the native engine opens the ASIO device on Windows; WebView2 falls back to WASAPI for preview."
      : "ASIO is Windows-only — select WASAPI/WASAPI-exclusive on other platforms."
    : "ASIO available on Windows in the desktop app (native JUCE enables it). This panel prefers it and saves the choice.";

  return (
    <div className="ws-settings" aria-label="Settings">
      <section className="ws-settings-card">
        <h3>Accent</h3>
        <div className="ws-settings-row">
          {ACCENTS.map((entry) => (
            <button
              key={entry.hex}
              type="button"
              className="ws-settings-swatch"
              aria-pressed={settings.accent === entry.hex}
              aria-label={`${entry.name} accent`}
              title={entry.name}
              style={{ background: entry.hex }}
              onClick={() => set({ accent: entry.hex })}
            />
          ))}
          <span className="ws-settings-read">{settings.accent.toUpperCase()}</span>
        </div>
      </section>

      <section className="ws-settings-card">
        <h3>Track tint (strips &amp; heads)</h3>
        <label className="ws-settings-slider">
          <span>
            Tint <b>{settings.tintAmount}%</b>
          </span>
          <input
            type="range"
            min={0}
            max={30}
            step={1}
            value={settings.tintAmount}
            onChange={(event) => set({ tintAmount: Number(event.target.value) })}
          />
        </label>
      </section>

      <section className="ws-settings-card">
        <h3>Clip colour</h3>
        <label className="ws-settings-slider">
          <span>
            Boldness <b>{settings.clipBoldness}%</b>
          </span>
          <input
            type="range"
            min={0}
            max={100}
            step={5}
            value={settings.clipBoldness}
            onChange={(event) => set({ clipBoldness: Number(event.target.value) })}
          />
        </label>
      </section>

      <section className="ws-settings-card">
        <h3>Loop contrast</h3>
        <label className="ws-settings-slider">
          <span>
            Strength <b>{settings.loopStrength}%</b>
          </span>
          <input
            type="range"
            min={0}
            max={100}
            step={5}
            value={settings.loopStrength}
            onChange={(event) => set({ loopStrength: Number(event.target.value) })}
          />
        </label>
      </section>

      <section className="ws-settings-card">
        <h3>Session (DAW)</h3>
        <label className="ws-settings-slider">
          <span>
            Tempo <b>{settings.bpm.toFixed(1)} BPM</b>
          </span>
          <input
            type="range"
            min={20}
            max={300}
            step={0.1}
            value={settings.bpm}
            onChange={(event) => set({ bpm: Number(event.target.value) })}
          />
        </label>
        <label className="ws-settings-slider">
          <span>
            Default track height <b>{settings.trackHeight}px</b>
          </span>
          <input
            type="range"
            min={60}
            max={120}
            step={2}
            value={settings.trackHeight}
            onChange={(event) => set({ trackHeight: Number(event.target.value) })}
          />
        </label>
        <label className="ws-settings-slider">
          <span>
            Default fade <b>{settings.fadeBars}b</b>
          </span>
          <input
            type="range"
            min={0}
            max={4}
            step={0.25}
            value={settings.fadeBars}
            onChange={(event) => set({ fadeBars: Number(event.target.value) })}
          />
        </label>
        <label className="ws-settings-slider">
          <span>
            Default clip gain <b>{settings.clipGain > 0 ? "+" : ""}{settings.clipGain.toFixed(1)} dB</b>
          </span>
          <input
            type="range"
            min={-12}
            max={12}
            step={0.1}
            value={settings.clipGain}
            onChange={(event) => set({ clipGain: Number(event.target.value) })}
          />
        </label>
        <label className="ws-settings-slider">
          <span>
            Master gain <b>{settings.masterGain > 0 ? "+" : ""}{settings.masterGain.toFixed(1)} dB</b>
          </span>
          <input
            type="range"
            min={-12}
            max={6}
            step={0.1}
            value={settings.masterGain}
            onChange={(event) => set({ masterGain: Number(event.target.value) })}
          />
        </label>
        <div className="ws-settings-row">
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.snap}
            onClick={() => set({ snap: !settings.snap })}
          >
            Snap {settings.snap ? "ON" : "OFF"}
          </button>
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.metronome}
            onClick={() => set({ metronome: !settings.metronome })}
          >
            Click {settings.metronome ? "ON" : "OFF"}
          </button>
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.loop}
            onClick={() => set({ loop: !settings.loop })}
          >
            Loop {settings.loop ? "ON" : "OFF"}
          </button>
        </div>
        <p className="ws-settings-note">Changes apply live to the transport and arranger where the host exposes the route.</p>
      </section>

      <section className="ws-settings-card">
        <h3>Audio interface</h3>
        <label className="ws-settings-slider">
          <span>Driver</span>
          <select
            aria-label="Audio driver"
            value={settings.driver}
            onChange={(event) => set({ driver: event.target.value as UiSettings["driver"] })}
          >
            {DRIVERS.map((driver) => (
              <option key={driver.id} value={driver.id}>
                {driver.label}
              </option>
            ))}
          </select>
        </label>
        <label className="ws-settings-slider">
          <span>Input</span>
          <select
            aria-label="Audio input"
            value={settings.audioIn}
            onChange={(event) => set({ audioIn: event.target.value })}
          >
            <option value="">System default</option>
            {settings.audioIn !== "" && !devices.some((d) => d.deviceId === settings.audioIn) && (
              <option value={settings.audioIn}>Saved device (not listed)</option>
            )}
            {devices.map((device) => (
              <option key={device.deviceId} value={device.deviceId}>
                {device.label}
              </option>
            ))}
          </select>
        </label>
        <label className="ws-settings-slider">
          <span>Output</span>
          <select
            aria-label="Audio output"
            value={settings.audioOut}
            onChange={(event) => set({ audioOut: event.target.value })}
          >
            <option value="">System default</option>
            {devices.map((device) => (
              <option key={device.deviceId} value={device.deviceId}>
                {device.label}
              </option>
            ))}
          </select>
        </label>
        <label className="ws-settings-slider">
          <span>
            Sample rate <b>{settings.sampleRate} Hz</b>
          </span>
          <select
            aria-label="Sample rate"
            value={settings.sampleRate}
            onChange={(event) => set({ sampleRate: Number(event.target.value) })}
          >
            {RATES.map((rate) => (
              <option key={rate} value={rate}>
                {rate} Hz
              </option>
            ))}
          </select>
        </label>
        <label className="ws-settings-slider">
          <span>
            Buffer <b>{settings.bufferSize} samples</b>
          </span>
          <select
            aria-label="Buffer size"
            value={settings.bufferSize}
            onChange={(event) => set({ bufferSize: Number(event.target.value) })}
          >
            {BUFFERS.map((buffer) => (
              <option key={buffer} value={buffer}>
                {buffer} samples · {((buffer / settings.sampleRate) * 1000).toFixed(1)} ms
              </option>
            ))}
          </select>
        </label>
        <div className="ws-settings-row">
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.lowLatency}
            onClick={() => set({ lowLatency: !settings.lowLatency })}
          >
            Low latency {settings.lowLatency ? "ON" : "OFF"}
          </button>
          <button type="button" className="ws-btn" onClick={() => refreshDevices()}>
            Refresh devices
          </button>
        </div>
        <p className="ws-settings-note">{asioNote}</p>
        <p className="ws-settings-note">{audioNote}</p>
      </section>

      <section className="ws-settings-card">
        <h3>MIDI controller</h3>
        <label className="ws-settings-slider">
          <span>Controller</span>
          <select
            aria-label="MIDI controller"
            value={settings.midiIn}
            onChange={(event) => set({ midiIn: event.target.value })}
          >
            <option value="">None</option>
            {settings.midiIn !== "" && !midiNames.includes(settings.midiIn) && (
              <option value={settings.midiIn}>{settings.midiIn} (saved — not listed)</option>
            )}
            {midiNames.map((name) => (
              <option key={name} value={name}>
                {name}
              </option>
            ))}
          </select>
        </label>
        <div className="ws-settings-row">
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.midiEnabled}
            onClick={() => set({ midiEnabled: !settings.midiEnabled })}
          >
            MIDI {settings.midiEnabled ? "ON" : "OFF"}
          </button>
        </div>
        <p className="ws-settings-note">{midiNote}</p>
      </section>

      <section className="ws-settings-card">
        <h3>Density</h3>
        <div className="ws-settings-row">
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.density === "comfortable"}
            onClick={() => set({ density: "comfortable" })}
          >
            Comfortable
          </button>
          <button
            type="button"
            className="ws-btn"
            aria-pressed={settings.density === "compact"}
            onClick={() => set({ density: "compact" })}
          >
            Compact
          </button>
        </div>
        <p className="ws-settings-note">Compact tightens strip/toolbar gaps. Stored locally in this browser.</p>
      </section>
    </div>
  );
}
